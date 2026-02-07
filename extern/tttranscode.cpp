/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : tttranscode.cpp                                                 */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 08/07/2005 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTTRANSCODE
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#include "tttranscode.h"
#include "ttencodeparameter.h"

#include "../avstream/ttavstream.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"
#include "../common/ttcut.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QCoreApplication>

/* ////////////////////////////////////////////////////////////////////////////
 * Constructor
 */
TTTranscodeProvider::TTTranscodeProvider(TTEncodeParameter& enc_par)
                    : IStatusReporter()
{
  log           = TTMessageLogger::getInstance();
  this->enc_par = enc_par;
  mEncoder      = nullptr;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTTranscodeProvider::~TTTranscodeProvider()
{
  freeEncoder();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Setup the libav mpeg2video encoder from encode parameters
 */
bool TTTranscodeProvider::setupEncoder()
{
  freeEncoder();

  const AVCodec* codec = avcodec_find_encoder_by_name("mpeg2video");
  if (!codec) {
    log->errorMsg(__FILE__, __LINE__, "Cannot find mpeg2video encoder");
    return false;
  }

  mEncoder = avcodec_alloc_context3(codec);
  if (!mEncoder) {
    log->errorMsg(__FILE__, __LINE__, "Cannot allocate encoder context");
    return false;
  }

  mEncoder->width  = enc_par.videoWidth();
  mEncoder->height = enc_par.videoHeight();
  mEncoder->pix_fmt = AV_PIX_FMT_YUV420P;

  // Time base and frame rate
  float fps = enc_par.videoFPS();
  mEncoder->time_base = (AVRational){1001, static_cast<int>(fps * 1001 + 0.5)};
  mEncoder->framerate = (AVRational){static_cast<int>(fps * 1000 + 0.5), 1000};

  // Quality: qscale mode using mpeg2Crf setting (2-31)
  mEncoder->flags |= AV_CODEC_FLAG_QSCALE;
  mEncoder->global_quality = FF_QP2LAMBDA * TTCut::mpeg2Crf;

  // GOP size based on frame rate (PAL=15, NTSC=18)
  int gopSize = 15;
  if (fps > 28.0f)
    gopSize = 18;
  else if (fps > 48.0f)
    gopSize = 30;
  mEncoder->gop_size = gopSize;

  // No B-frames for clean segment transitions (like TTESSmartCut)
  mEncoder->max_b_frames = 0;

  // Rate control: use original bitrate as constraint
  float bitrateKbit = enc_par.videoBitrate();
  if (bitrateKbit > 0) {
    mEncoder->rc_max_rate   = static_cast<int64_t>(bitrateKbit) * 1000;
    mEncoder->rc_buffer_size = static_cast<int>(bitrateKbit) * 2000;
  }

  // Sample aspect ratio from MPEG-2 aspect ratio code + dimensions
  // MPEG-2 aspect codes: 1=1:1(SAR), 2=4:3(DAR), 3=16:9(DAR), 4=2.21:1(DAR)
  int w = enc_par.videoWidth();
  int h = enc_par.videoHeight();
  switch (enc_par.videoAspectCode()) {
    case 2:  // 4:3 DAR → SAR = (4/3) * (h/w)
      mEncoder->sample_aspect_ratio = (AVRational){4 * h, 3 * w};
      break;
    case 3:  // 16:9 DAR → SAR = (16/9) * (h/w)
      mEncoder->sample_aspect_ratio = (AVRational){16 * h, 9 * w};
      break;
    default: // 1:1 SAR
      mEncoder->sample_aspect_ratio = (AVRational){1, 1};
      break;
  }

  // Interlace flags
  if (enc_par.videoInterlaced()) {
    mEncoder->flags |= AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME;
    if (enc_par.videoTopFieldFirst())
      mEncoder->field_order = AV_FIELD_TT;
    else
      mEncoder->field_order = AV_FIELD_BB;
  }

  mEncoder->thread_count = 0;  // Auto

  int ret = avcodec_open2(mEncoder, codec, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    log->errorMsg(__FILE__, __LINE__, QString("Cannot open mpeg2video encoder: %1").arg(errbuf));
    avcodec_free_context(&mEncoder);
    return false;
  }

  qDebug() << "MPEG-2 encoder setup:" << mEncoder->width << "x" << mEncoder->height
           << "qscale=" << TTCut::mpeg2Crf << "gop=" << gopSize
           << "interlaced=" << enc_par.videoInterlaced()
           << "bitrate_cap=" << bitrateKbit << "kbit/s";

  return true;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Free the encoder context
 */
void TTTranscodeProvider::freeEncoder()
{
  if (mEncoder) {
    avcodec_free_context(&mEncoder);
    mEncoder = nullptr;
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Encode frames from start to end using libmpeg2 decoder + libav encoder
 * Writes encoded MPEG-2 data directly to .m2v file
 */
bool TTTranscodeProvider::encodeFrames(TTVideoStream* vs, int start, int end)
{
  int frameCount = end - start + 1;

  // Create decoder (same pattern as TTAVIWriter)
  TTMpeg2Decoder* decoder = new TTMpeg2Decoder(
      vs->filePath(), vs->indexList(), vs->headerList());

  // Init decoder with YV12 pixel format
  decoder->decodeFirstMPEG2Frame(formatYV12);

  // Open output file
  QString outputFile = enc_par.mpeg2FileInfo().absoluteFilePath() + ".m2v";
  QFile outFile(outputFile);
  if (!outFile.open(QIODevice::WriteOnly)) {
    log->errorMsg(__FILE__, __LINE__, QString("Cannot create output file: %1").arg(outputFile));
    delete decoder;
    return false;
  }

  // Allocate AVFrame (we reuse it, pointing data[] to decoder buffers)
  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    log->errorMsg(__FILE__, __LINE__, "Cannot allocate AVFrame");
    outFile.close();
    delete decoder;
    return false;
  }

  frame->format = AV_PIX_FMT_YUV420P;
  frame->width  = mEncoder->width;
  frame->height = mEncoder->height;

  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    log->errorMsg(__FILE__, __LINE__, "Cannot allocate AVPacket");
    av_frame_free(&frame);
    outFile.close();
    delete decoder;
    return false;
  }

  int framesSent = 0;
  int packetsReceived = 0;

  // Decode and encode each frame
  for (int i = 0; i < frameCount; i++) {
    int frameIndex = start + i;

    // Move decoder to frame (in display order)
    decoder->moveToFrameIndex(frameIndex);
    TFrameInfo* frameInfo = decoder->getFrameInfo();

    if (!frameInfo || !frameInfo->Y) {
      log->errorMsg(__FILE__, __LINE__, QString("Failed to decode frame %1").arg(frameIndex));
      continue;
    }

    // Point AVFrame data to libmpeg2 decoder buffers (zero-copy)
    // libmpeg2 with formatYV12 outputs YUV420P planar data
    frame->data[0] = frameInfo->Y;
    frame->data[1] = frameInfo->U;
    frame->data[2] = frameInfo->V;
    frame->linesize[0] = frameInfo->width;
    frame->linesize[1] = frameInfo->chroma_width;
    frame->linesize[2] = frameInfo->chroma_width;
    frame->pts = framesSent;

    // buf[] stays NULL — we don't own these buffers, libmpeg2 does
    // av_frame_free() only frees buf[] refs, so this is safe

    // Send frame to encoder
    int ret = avcodec_send_frame(mEncoder, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      log->errorMsg(__FILE__, __LINE__, QString("avcodec_send_frame failed: %1").arg(errbuf));
      break;
    }
    framesSent++;

    // Receive any available encoded packets
    while (true) {
      ret = avcodec_receive_packet(mEncoder, packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        log->errorMsg(__FILE__, __LINE__, QString("avcodec_receive_packet failed: %1").arg(errbuf));
        break;
      }

      if (outFile.write(reinterpret_cast<char*>(packet->data), packet->size) != packet->size) {
        log->errorMsg(__FILE__, __LINE__, "Failed to write encoded data");
        av_packet_unref(packet);
        goto cleanup;
      }
      packetsReceived++;
      av_packet_unref(packet);
    }

    emit statusReport(StatusReportArgs::AddProcessLine,
        QString("Encoding frame %1/%2").arg(i + 1).arg(frameCount), 0);

    if (i % 5 == 0)
      qApp->processEvents();
  }

  // Flush encoder — send null frame to get remaining packets
  avcodec_send_frame(mEncoder, nullptr);
  while (true) {
    int ret = avcodec_receive_packet(mEncoder, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0)
      break;

    if (outFile.write(reinterpret_cast<char*>(packet->data), packet->size) != packet->size) {
      log->errorMsg(__FILE__, __LINE__, "Failed to write encoded data (flush)");
      av_packet_unref(packet);
      goto cleanup;
    }
    packetsReceived++;
    av_packet_unref(packet);
  }

  qDebug() << "MPEG-2 encoding complete: sent" << framesSent
           << "frames, received" << packetsReceived << "packets";

cleanup:
  // Clear AVFrame data pointers before freeing (they point to decoder buffers)
  frame->data[0] = nullptr;
  frame->data[1] = nullptr;
  frame->data[2] = nullptr;
  av_frame_free(&frame);
  av_packet_free(&packet);
  outFile.close();
  delete decoder;

  return (packetsReceived > 0 && packetsReceived >= framesSent);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Encode a part of the video stream
 * Public API — unchanged from previous version
 */
bool TTTranscodeProvider::encodePart(TTVideoStream* vStream, int start, int end)
{
  emit statusReport(StatusReportArgs::ShowProcessForm, "encode part", 0);
  qApp->processEvents();

  if (!setupEncoder()) {
    emit statusReport(StatusReportArgs::HideProcessForm, "encode failed - encoder setup", 0);
    return false;
  }

  bool success = encodeFrames(vStream, start, end);

  freeEncoder();

  emit statusReport(StatusReportArgs::HideProcessForm, "encode finished", 0);
  qApp->processEvents();

  return success;
}
