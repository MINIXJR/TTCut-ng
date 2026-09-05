/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttaudiocutter.h"
#include "../avstream/ttavutil.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <cmath>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

// ----------------------------------------------------------------------------
// Analyze AC3 acmod (audio coding mode) for a segment between cutInTime and cutOutTime.
// Returns the majority acmod and detects changes at cut boundaries.
// Uses direct AC3 sync word scanning on the raw file instead of libav (which crashes
// on av_seek_frame for raw AC3 ES files).
// ----------------------------------------------------------------------------
TTAudioCutter::AcmodInfo TTAudioCutter::analyzeAcmod(const QString& audioFile,
                                                          double cutInTime, double cutOutTime)
{
    AcmodInfo info = { -1, -1, -1 };

    QFile file(audioFile);
    if (!file.open(QIODevice::ReadOnly))
        return info;

    // AC3 frame size lookup table [fscod][frmsizecod] in 16-bit words
    static const int AC3FrameWords[3][38] = {
        { 64, 64, 80, 80, 96, 96,112,112,128,128,160,160,192,192,224,224,256,256,320,320,
         384,384,448,448,512,512,640,640,768,768,896,896,1024,1024,1152,1152,1280,1280},
        { 69, 70, 87, 88,104,105,121,122,139,140,174,175,208,209,243,244,278,279,348,349,
         417,418,487,488,557,558,696,697,835,836,975,976,1114,1115,1253,1254,1393,1394},
        { 96, 96,120,120,144,144,168,168,192,192,240,240,288,288,336,336,384,384,480,480,
         576,576,672,672,768,768,960,960,1152,1152,1344,1344,1536,1536,1728,1728,1920,1920}
    };

    // Scan AC3 frames by sync word (0x0B77)
    static const int SAMPLE_FRAMES = 100;
    int acmodCount[8] = {0};
    int firstAcmod = -1;
    int lastAcmod = -1;
    int totalFrames = 0;
    int frameIndex = 0;
    double frameTime = 0.032;  // AC3 = 32ms per frame at 48kHz

    // Calculate frame indices for cutIn/cutOut
    int cutInFrame = static_cast<int>(cutInTime / frameTime);
    int cutOutFrame = static_cast<int>(cutOutTime / frameTime);

    quint8 header[8];
    qint64 pos = 0;

    while (pos < file.size() - 8) {
        file.seek(pos);
        if (file.read(reinterpret_cast<char*>(header), 8) != 8)
            break;

        // Check sync word
        if (header[0] != 0x0B || header[1] != 0x77) {
            pos++;
            continue;
        }

        int fscod = (header[4] >> 6) & 0x03;
        int frmsizecod = header[4] & 0x3F;
        if (fscod >= 3 || frmsizecod >= 38) {
            pos++;
            continue;
        }

        int frameSize = AC3FrameWords[fscod][frmsizecod] * 2;
        if (frameSize <= 0) {
            pos++;
            continue;
        }

        int acmod = (header[6] >> 5) & 0x07;

        // Sample first SAMPLE_FRAMES from CutIn and last SAMPLE_FRAMES before CutOut
        bool inCutInRange  = (frameIndex >= cutInFrame && frameIndex < cutInFrame + SAMPLE_FRAMES);
        bool inCutOutRange = (frameIndex >= cutOutFrame - SAMPLE_FRAMES && frameIndex < cutOutFrame);

        if (inCutInRange || inCutOutRange) {
            acmodCount[acmod]++;
            totalFrames++;
            if (firstAcmod < 0) firstAcmod = acmod;
            lastAcmod = acmod;
        }

        // Stop scanning well past cutOut
        if (frameIndex > cutOutFrame + SAMPLE_FRAMES)
            break;

        frameIndex++;
        pos += frameSize;
    }

    file.close();

    if (totalFrames == 0)
        return info;

    // Determine main acmod (majority)
    int mainAcmod = 0;
    int maxCount = 0;
    for (int i = 0; i < 8; i++) {
        if (acmodCount[i] > maxCount) {
            maxCount = acmodCount[i];
            mainAcmod = i;
        }
    }

    info.mainAcmod = mainAcmod;
    info.cutInAcmod = firstAcmod;
    info.cutOutAcmod = lastAcmod;

    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "analyzeAcmod:" << QFileInfo(audioFile).fileName()
                 << "main=" << mainAcmod << "cutIn=" << firstAcmod << "cutOut=" << lastAcmod
                 << "frames=" << totalFrames;
    }

    return info;
}

// ----------------------------------------------------------------------------
// Cut audio elementary stream using libav stream-copy (no external process)
// All segments are handled in a single pass with PTS offset management.
// ----------------------------------------------------------------------------

// Per-call state of cut(): both containers, the output timeline, the progress
// and [DRIFT] accounting and the lazily created AC3 re-encode chain. Lives on
// cut()'s stack; the helpers below it are the named steps of its packet loop.
struct TTAudioCutter::CutSession
{
    AVFormatContext* inFmtCtx  = nullptr;
    AVFormatContext* outFmtCtx = nullptr;
    int              audioIdx  = -1;
    AVStream*        inStream  = nullptr;
    int64_t          frameDuration = 0;     // one audio frame in stream time_base ticks
    double           frameDurSec   = 0.0;   // the same in seconds (loop-invariant)

    // Output timeline: every packet goes out at pts + ptsOffset, the next
    // segment continues at nextOutputPts.
    int64_t ptsOffset     = 0;
    int64_t nextOutputPts = 0;

    // Progress and [DRIFT] accounting
    const std::function<void(int)>* progressCb = nullptr;
    double  totalKeepSec = 0.0;
    double  writtenSec   = 0.0;
    int     lastPercent  = -1;
    int     totalPacketsWritten = 0;
    int64_t lastWrittenPtsTicks = 0;        // out time_base ticks

    // AC3 acmod normalization, created on the first frame that needs it
    AVCodecContext*    ac3DecCtx = nullptr;
    AVCodecContext*    ac3EncCtx = nullptr;
    struct SwrContext* swrCtx    = nullptr;
    AVFrame*           ac3Frame  = nullptr;
    AVFrame*           ac3ConvertedFrame = nullptr;
    int                acmodReencoded = 0;

    void reportProgress()
    {
        if (!progressCb || !*progressCb || totalKeepSec <= 0.0) return;
        int p = qBound(0, (int)(writtenSec / totalKeepSec * 100.0), 100);
        if (p != lastPercent) { lastPercent = p; (*progressCb)(p); }
    }

    //! Bookkeeping after one packet reached the muxer at output pts `pts`.
    void notePacketWritten(int64_t pts)
    {
        nextOutputPts = pts + frameDuration;
        ++totalPacketsWritten;
        lastWrittenPtsTicks = pts;
        writtenSec += frameDurSec;
        reportProgress();
    }

    void closeCodecs()
    {
        if (swrCtx)             swr_free(&swrCtx);
        if (ac3ConvertedFrame)  av_frame_free(&ac3ConvertedFrame);
        if (ac3DecCtx)          avcodec_free_context(&ac3DecCtx);
        if (ac3EncCtx)          avcodec_free_context(&ac3EncCtx);
        if (ac3Frame)           av_frame_free(&ac3Frame);
    }

    void closeContainers()
    {
        avformat_close_input(&inFmtCtx);
        if (!outFmtCtx) return;
        if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        outFmtCtx = nullptr;
    }
};

// Open the input, create the output with the input's codec parameters and
// time base, write its header and derive the frame duration. On failure the
// error text is set, everything opened so far is closed, and false comes back.
bool TTAudioCutter::openCutSession(CutSession& s, const QString& inputFile, const QString& outputFile)
{
    int ret = avformat_open_input(&s.inFmtCtx, inputFile.toUtf8().constData(),
                                  nullptr, nullptr);
    if (ret < 0) {
        setError(QString("Cannot open audio input: %1").arg(ttAvErrorToString(ret)));
        return false;
    }

    ret = avformat_find_stream_info(s.inFmtCtx, nullptr);
    if (ret < 0) {
        s.closeContainers();
        setError("Cannot find audio stream info");
        return false;
    }

    s.audioIdx = av_find_best_stream(s.inFmtCtx, AVMEDIA_TYPE_AUDIO,
                                     -1, -1, nullptr, 0);
    if (s.audioIdx < 0) {
        s.closeContainers();
        setError("No audio stream found in input");
        return false;
    }
    s.inStream = s.inFmtCtx->streams[s.audioIdx];

    // Open output — format auto-detected from file extension (.ac3, .mp2, etc.)
    ret = avformat_alloc_output_context2(&s.outFmtCtx, nullptr, nullptr,
                                         outputFile.toUtf8().constData());
    if (ret < 0 || !s.outFmtCtx) {
        s.closeContainers();
        setError("Cannot create audio output context");
        return false;
    }

    AVStream* outStream = avformat_new_stream(s.outFmtCtx, nullptr);
    if (!outStream) {
        s.closeContainers();
        setError("Cannot create output audio stream");
        return false;
    }
    avcodec_parameters_copy(outStream->codecpar, s.inStream->codecpar);
    outStream->time_base = s.inStream->time_base;

    if (!(s.outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&s.outFmtCtx->pb, outputFile.toUtf8().constData(),
                        AVIO_FLAG_WRITE);
        if (ret < 0) {
            s.closeContainers();
            setError(QString("Cannot open output file: %1").arg(ttAvErrorToString(ret)));
            return false;
        }
    }

    ret = avformat_write_header(s.outFmtCtx, nullptr);
    if (ret < 0) {
        s.closeContainers();
        setError("Cannot write audio output header");
        return false;
    }

    // Audio frame duration in stream time_base units
    if (s.inStream->codecpar->frame_size > 0 && s.inStream->codecpar->sample_rate > 0) {
        s.frameDuration = av_rescale_q(s.inStream->codecpar->frame_size,
            AVRational{1, s.inStream->codecpar->sample_rate}, s.inStream->time_base);
    }
    if (s.frameDuration <= 0) {
        s.frameDuration = av_rescale_q(1, AVRational{32, 1000}, s.inStream->time_base);
    }
    s.frameDurSec = s.frameDuration * av_q2d(s.inStream->time_base);
    return true;
}

// Decoder and encoder for the acmod re-encode, created on first use and kept
// for the rest of the run (the encoder therefore carries the layout of the
// first target it was created for). Any failure returns false and the frame
// takes the stream-copy path instead.
bool TTAudioCutter::ensureAc3Codecs(CutSession& s, int targetAcmod)
{
    if (!s.ac3DecCtx) {
        const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
        s.ac3DecCtx = dec ? avcodec_alloc_context3(dec) : nullptr;
        if (!s.ac3DecCtx) return false;
        if (avcodec_parameters_to_context(s.ac3DecCtx, s.inStream->codecpar) < 0 ||
            avcodec_open2(s.ac3DecCtx, dec, nullptr) < 0) {
            avcodec_free_context(&s.ac3DecCtx);
            return false;
        }
        s.ac3Frame = av_frame_alloc();
        if (!s.ac3Frame) {
            avcodec_free_context(&s.ac3DecCtx);
            return false;
        }
    }
    if (!s.ac3EncCtx) {
        const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_AC3);
        if (!enc) {
            qWarning() << "AC3 encoder not available — skipping AC3 re-encode";
            return false;
        }
        s.ac3EncCtx = avcodec_alloc_context3(enc);
        if (!s.ac3EncCtx) {
            qWarning() << "avcodec_alloc_context3 failed for AC3 encoder";
            return false;
        }
        s.ac3EncCtx->sample_rate = s.inStream->codecpar->sample_rate;
        s.ac3EncCtx->bit_rate = s.inStream->codecpar->bit_rate > 0
            ? s.inStream->codecpar->bit_rate : 384000;
        s.ac3EncCtx->time_base = s.inStream->time_base;
        // Set target channel layout based on target acmod
        if (targetAcmod == 7 || targetAcmod == 6) {
            // 5.1: 3/2 + LFE
            AVChannelLayout layout51 = AV_CHANNEL_LAYOUT_5POINT1;
            av_channel_layout_copy(&s.ac3EncCtx->ch_layout, &layout51);
        } else {
            // Stereo: 2/0
            AVChannelLayout layoutStereo = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&s.ac3EncCtx->ch_layout, &layoutStereo);
        }
        s.ac3EncCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        if (avcodec_open2(s.ac3EncCtx, enc, nullptr) < 0) {
            qWarning() << "avcodec_open2 failed for AC3 encoder";
            avcodec_free_context(&s.ac3EncCtx);
            return false;
        }
    }
    return true;
}

// Repair-table hit: the substitute bytes go out with the packet's PTS offset
// and the usual accounting. Returns false when the substitute packet could
// not be allocated (OOM) - the caller then writes the ORIGINAL frame rather
// than leaving a gap, exactly like the re-encode path falls back on its own
// init failures.
bool TTAudioCutter::writeRepairedPacket(CutSession& s, const AVPacket* pkt,
                                        const QByteArray& bytes, double pktTime)
{
    AVPacket* rp = av_packet_alloc();
    const bool allocOk = rp && av_new_packet(rp, bytes.size()) == 0;
    if (!allocOk) {
        if (rp) av_packet_free(&rp);
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("  Warning: repair packet allocation failed at %1 -- writing original frame instead").arg(pktTime));
        return false;
    }
    memcpy(rp->data, bytes.constData(), bytes.size());
    rp->pts = pkt->pts + s.ptsOffset;
    rp->dts = rp->pts;
    rp->duration = pkt->duration;
    rp->stream_index = 0;
    rp->pos = -1;

    const int ret = av_write_frame(s.outFmtCtx, rp);
    if (ret < 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("  Warning: av_write_frame (repair) failed at %1").arg(pktTime));
    } else {
        s.notePacketWritten(rp->pts);
    }
    av_packet_free(&rp);
    return true;
}

// Decode the frame, convert its channel layout to the encoder's and write the
// re-encoded packet. A failure anywhere drops the frame (neither written nor
// stream-copied), as the inline version did.
void TTAudioCutter::writeReencodedPacket(CutSession& s, AVPacket* pkt)
{
    avcodec_send_packet(s.ac3DecCtx, pkt);
    const int decRet = avcodec_receive_frame(s.ac3DecCtx, s.ac3Frame);
    if (decRet != 0) return;

    // Setup resampler on first use (channel layout conversion)
    if (!s.swrCtx) {
        int swrRet = swr_alloc_set_opts2(&s.swrCtx,
            &s.ac3EncCtx->ch_layout, s.ac3EncCtx->sample_fmt, s.ac3EncCtx->sample_rate,
            &s.ac3Frame->ch_layout, (AVSampleFormat)s.ac3Frame->format, s.ac3Frame->sample_rate,
            0, nullptr);
        if (swrRet < 0 || !s.swrCtx) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("AC3 re-encode: swr_alloc_set_opts2 failed: %1").arg(ttAvErrorToString(swrRet)));
            return;
        }
        swrRet = swr_init(s.swrCtx);
        if (swrRet < 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("AC3 re-encode: swr_init failed: %1").arg(ttAvErrorToString(swrRet)));
            swr_free(&s.swrCtx);
            return;
        }

        s.ac3ConvertedFrame = av_frame_alloc();
        if (!s.ac3ConvertedFrame) return;
        av_channel_layout_copy(&s.ac3ConvertedFrame->ch_layout, &s.ac3EncCtx->ch_layout);
        s.ac3ConvertedFrame->format = s.ac3EncCtx->sample_fmt;
        s.ac3ConvertedFrame->sample_rate = s.ac3EncCtx->sample_rate;
        s.ac3ConvertedFrame->nb_samples = s.ac3Frame->nb_samples;
        av_frame_get_buffer(s.ac3ConvertedFrame, 0);
    }

    // Convert channel layout
    s.ac3ConvertedFrame->nb_samples = s.ac3Frame->nb_samples;
    swr_convert(s.swrCtx,
        s.ac3ConvertedFrame->data, s.ac3ConvertedFrame->nb_samples,
        (const uint8_t**)s.ac3Frame->data, s.ac3Frame->nb_samples);

    s.ac3ConvertedFrame->pts = pkt->pts + s.ptsOffset;

    // Re-encode with target channel layout
    const int sendRet = avcodec_send_frame(s.ac3EncCtx, s.ac3ConvertedFrame);
    if (sendRet < 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("AC3 re-encode: avcodec_send_frame failed: %1").arg(ttAvErrorToString(sendRet)));
        return;
    }
    AVPacket* encPkt = av_packet_alloc();
    if (encPkt && avcodec_receive_packet(s.ac3EncCtx, encPkt) == 0) {
        encPkt->pts = pkt->pts + s.ptsOffset;
        encPkt->dts = encPkt->pts;
        encPkt->stream_index = 0;
        encPkt->pos = -1;
        if (av_write_frame(s.outFmtCtx, encPkt) >= 0) {
            s.acmodReencoded++;
            s.notePacketWritten(encPkt->pts);
        }
    }
    av_packet_free(&encPkt);
}

// Normal stream-copy of one frame onto the output timeline.
void TTAudioCutter::writeStreamCopyPacket(CutSession& s, AVPacket* pkt, double pktTime)
{
    pkt->pts += s.ptsOffset;
    pkt->dts = pkt->pts;
    pkt->stream_index = 0;
    pkt->pos = -1;

    const int ret = av_write_frame(s.outFmtCtx, pkt);
    if (ret < 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("  Warning: av_write_frame failed at %1").arg(pktTime));
    } else {
        s.notePacketWritten(pkt->pts);
    }
}

bool TTAudioCutter::cut(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList,
                                      bool normalizeAcmod,
                                      const QList<int>& targetAcmods,
                                      const std::function<void(int)>& progressCb,
                                      const std::function<bool()>& shouldAbort,
                                      const TTAudioRepair::FrameTable* repairTable)
{
    if (!QFile::exists(inputFile)) {
        setError(QString("Audio file not found: %1").arg(inputFile));
        return false;
    }

    if (cutList.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "cutAudioStream: libav stream-copy";
        qDebug() << "  Input:" << inputFile;
        qDebug() << "  Output:" << outputFile;
        qDebug() << "  Segments:" << cutList.size();
    }

    CutSession s;
    if (!openCutSession(s, inputFile, outputFile))
        return false;
    AVStream* inStream = s.inStream;

    // Progress state: per-packet duration is loop-invariant, the keep total
    // is the yardstick.
    s.progressCb = &progressCb;
    for (const auto& seg : cutList)
        s.totalKeepSec += qMax(0.0, seg.second - seg.first);

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        s.closeContainers();
        setError("Cannot allocate packet");
        return false;
    }

    // AC3 acmod normalization: decoder/encoder are created lazily by
    // ensureAc3Codecs() on the first frame that needs them.
    const bool acmodNormActive = normalizeAcmod &&
                                 inStream->codecpar->codec_id == AV_CODEC_ID_AC3;

    qDebug() << "[DRIFT] cutAudioStream start: input"
             << inputFile << "segments" << cutList.size()
             << "outTimeBase" << s.outFmtCtx->streams[0]->time_base.num
             << "/" << s.outFmtCtx->streams[0]->time_base.den;

    // Process all segments in a single pass with PTS offset management
    bool aborted = false;
    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        double startTime = cutList[segIdx].first;
        double endTime = cutList[segIdx].second;

        // Determine target acmod for this segment
        int segTargetAcmod = -1;
        if (acmodNormActive && segIdx < targetAcmods.size()) {
            segTargetAcmod = targetAcmods[segIdx];
        }

        if (TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "  Segment" << segIdx << ":" << startTime << "->" << endTime
                     << (segTargetAcmod >= 0 ? QString("targetAcmod=%1").arg(segTargetAcmod) : "");
        }

        // Seek to just before start time using audio stream timebase
        int64_t seekTs = static_cast<int64_t>(startTime / av_q2d(inStream->time_base));
        int seekRet = av_seek_frame(s.inFmtCtx, s.audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);
        if (seekRet < 0) {
            if (TTSettings::instance()->logFFmpegDecoder()) {
                qDebug() << "cutAudioStream: av_seek_frame to" << seekTs
                         << "failed:" << ttAvErrorToString(seekRet)
                         << "— audio segment may start past intended cut-in";
            }
        }

        bool segmentStarted = false;
        while (av_read_frame(s.inFmtCtx, pkt) >= 0) {
            if (shouldAbort && shouldAbort()) {
                // Deliberately not setError(): a user cancel is not a failure
                // and must not read as one in the log (setError() logs at
                // warning level via TTMessageLogger). Set mLastError directly
                // so lastError() still contains "aborted", without the
                // error-level log line. Mirrors TTESSmartCut::checkAbort().
                mLastError = "aborted by user";
                aborted = true;
                av_packet_unref(pkt);
                break;
            }

            if (pkt->stream_index != s.audioIdx) {
                av_packet_unref(pkt);
                continue;
            }

            if (pkt->pts == AV_NOPTS_VALUE) {
                av_packet_unref(pkt);
                continue;
            }

            double pktTime = pkt->pts * av_q2d(inStream->time_base);

            // Skip packets before start time (1ms tolerance for frame alignment)
            if (pktTime < startTime - 0.001) {
                av_packet_unref(pkt);
                continue;
            }

            // Stop at end time — only include frames that fit completely
            if (pktTime + s.frameDurSec > endTime + 0.001) {
                av_packet_unref(pkt);
                break;
            }

            // Set PTS offset on first packet of each segment
            if (!segmentStarted) {
                s.ptsOffset = s.nextOutputPts - pkt->pts;
                segmentStarted = true;
            }

            // Repair lookup: replace the packet's payload before any acmod
            // handling. Frame number = packet time snapped to the 32 ms grid
            // (CBR raster) -- same grid TTAudioRepairItem's frame numbers use.
            // A hit writes the substitute bytes with the same PTS-offset/
            // accounting as the stream-copy path below and skips the acmod
            // re-encode check entirely: repaired frames never go through it.
            if (repairTable && !repairTable->isEmpty()) {
                qint64 frameNo = qRound64(pktTime / s.frameDurSec);
                auto it = repairTable->constFind(frameNo);
                if (it != repairTable->constEnd() && writeRepairedPacket(s, pkt, *it, pktTime)) {
                    av_packet_unref(pkt);
                    continue;
                }
            }

            // Check if this frame needs acmod re-encoding
            bool needsReencode = false;
            if (segTargetAcmod >= 0 && pkt->size >= 7) {
                int frameAcmod = (pkt->data[6] >> 5) & 0x07;
                needsReencode = (frameAcmod != segTargetAcmod);
            }
            if (needsReencode)
                needsReencode = ensureAc3Codecs(s, segTargetAcmod);

            if (needsReencode)
                writeReencodedPacket(s, pkt);
            else
                writeStreamCopyPacket(s, pkt, pktTime);
            av_packet_unref(pkt);
        }
        if (aborted) break;
    }

    // Guarantee the contract's final 100 even when rounding stopped short.
    // Skipped on abort — the cut did not actually reach 100% of the keep list.
    if (!aborted && progressCb && s.totalKeepSec > 0.0 && s.lastPercent < 100)
        progressCb(100);

    s.closeCodecs();
    if (s.acmodReencoded > 0 && TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "  AC3 acmod normalization: re-encoded" << s.acmodReencoded << "frames";
    }

    av_packet_free(&pkt);
    av_write_trailer(s.outFmtCtx);

    // Capture out-stream time_base before context cleanup (used by [DRIFT] log)
    AVRational outTimeBase = s.outFmtCtx->streams[0]->time_base;

    s.closeContainers();

    // Deliberate user abort: all AV contexts are already closed above via the
    // function's normal cleanup sequence (same code the error paths use).
    // lastError() already carries "aborted by user" from the poll site; do
    // not let the size-0 check below overwrite it with a generic failure —
    // the caller deletes the partial/empty output file.
    if (aborted) {
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "cutAudioStream: aborted by user";
        return false;
    }

    // Verify output
    QFileInfo outInfo(outputFile);
    if (!outInfo.exists() || outInfo.size() == 0) {
        setError(QString("Audio cut produced 0-byte output: %1").arg(outputFile));
        return false;
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "cutAudioStream: Complete, output size:" << outInfo.size() << "bytes";

    {
        double lastSec = s.lastWrittenPtsTicks * av_q2d(outTimeBase);
        qDebug() << "[DRIFT] cutAudioStream done: output" << outputFile
                 << "totalPackets" << s.totalPacketsWritten
                 << "lastWrittenPtsTicks" << s.lastWrittenPtsTicks
                 << "outTimeBase" << outTimeBase.num << "/" << outTimeBase.den
                 << "lastSec" << lastSec
                 << "outputBytes" << outInfo.size();
    }
    return true;
}

// ----------------------------------------------------------------------------
// Detect audio burst near a cut boundary using libav (no external process).
// Decodes ~200ms of audio around a boundary, calculates per-frame RMS,
// and checks if boundary frames are significantly louder than context.
// Returns true if a sudden loudness burst (>20dB above median) is detected.
// Sum of squared samples over one decoded frame, planar or interleaved,
// each sample of type T scaled by `scale` into [-1, 1].
template <typename T>
static double sumSquares(const AVFrame* frame, bool planar, int channels, double scale)
{
    double sumSq = 0.0;
    for (int ch = 0; ch < channels; ch++) {
        const T*  data   = reinterpret_cast<const T*>(frame->data[planar ? ch : 0]);
        const int stride = planar ? 1 : channels;
        const int offset = planar ? 0 : ch;
        for (int s = 0; s < frame->nb_samples; s++) {
            const double v = data[s * stride + offset] * scale;
            sumSq += v * v;
        }
    }
    return sumSq;
}

// ----------------------------------------------------------------------------
// Absolute audibility floor: a chunk this quiet is inaudible in practice, however far
// it sticks out of a near-silent context. Without it, noise in digital silence
// (context ~-80 dB) would trigger on every cut -- on the reference recording 766
// positions clear a 20 dB delta while peaking below this floor.
//
// The value stays at -40 dB. Measured with the real detector on DVB material
// (ServusTV, 2022-10-25): the three advertising-burst peaks clear this floor by only
// 2.5 / 12.7 / 3.5 dB (peaks -37.48 / -27.30 / -36.49 dB) -- the floor sits close under
// real bursts, so raising it would start discarding them.
//
// Lowering the floor to -50 dB was considered and rejected: it would admit 709 further
// positions (the floor rejects 766 at -40 dB, only 57 at -50 dB) -- a large jump in
// false positives for two of the reference bursts that already sit barely above -40 dB.
//
// Known risk: a broadcaster whose burst peaks below -40 dB is missed silently. The
// reference material comes close (-37.48 dB); revisit if a real miss appears.
static constexpr double kBurstAbsoluteFloorDb = -40.0;

bool TTAudioCutter::detectBurst(const QString& audioFile, double boundaryTime,
                                        bool isCutOut, int minDeltaDb,
                                        double& burstRmsDb, double& contextRmsDb)
{
    // Callers short-circuit on <= 0 before opening the file; guard anyway.
    if (minDeltaDb <= 0) return false;

    // Open audio file
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, audioFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        TTMessageLogger::getInstance()->errorMsg(__FILE__, __LINE__,
            QString("detectAudioBurst: cannot open %1").arg(audioFile));
        return false;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Find audio stream
    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = i;
            break;
        }
    }
    if (audioIdx < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVStream* stream = fmtCtx->streams[audioIdx];

    // Open decoder
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVCodecContext* decCtx = avcodec_alloc_context3(codec);
    if (!decCtx) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    if (avcodec_parameters_to_context(decCtx, stream->codecpar) < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }
    ret = avcodec_open2(decCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    int sampleRate = decCtx->sample_rate;
    int channels   = decCtx->ch_layout.nb_channels;
    if (sampleRate <= 0 || channels <= 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Audio frame duration is the natural per-codec snap unit:
    //   MP2 @48k = 24 ms, AC3 @48k = 32 ms.
    // planAudioCut snaps the audio cut to this grid, so the boundary can
    // round at most ½ frame past the video cut. Anything further past the
    // boundary in the SOURCE can never end up in the kept audio — clamping
    // the analysis tail to frameDuration/2 keeps the detector honest for
    // all codecs without a separate code path per format.
    double frameDuration = (double)decCtx->frame_size / sampleRate;
    if (frameDuration <= 0) frameDuration = 0.032;  // AC3 default
    double tailSec = frameDuration * 0.5;

    // Analyze a 200 ms context window on the kept side, plus the tail
    // (the only part of the source that frame-snapping could leak in).
    double windowStart, windowEnd;
    if (isCutOut) {
        windowStart = qMax(0.0, boundaryTime - 0.200);
        windowEnd   = boundaryTime + tailSec;
    } else {
        windowStart = qMax(0.0, boundaryTime - tailSec);
        windowEnd   = boundaryTime + 0.200;
    }

    // Seek to window start using stream timebase for precision
    int64_t seekTs = (int64_t)(windowStart / av_q2d(stream->time_base));
    ret = av_seek_frame(fmtCtx, audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // Fallback: seek from beginning
        av_seek_frame(fmtCtx, audioIdx, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(decCtx);

    // Decode audio and collect per-frame RMS values
    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }
    QList<double> rmsValues;

    while (av_read_frame(fmtCtx, packet) >= 0) {
        if (packet->stream_index != audioIdx) {
            av_packet_unref(packet);
            continue;
        }

        // Check if we're past the window. The frame-level reject below uses
        // a strict windowEnd, so the packet stop matches — no extra frame
        // duration of slack here either.
        double pktTime = packet->pts * av_q2d(stream->time_base);
        if (pktTime >= windowEnd) {
            av_packet_unref(packet);
            break;
        }

        ret = avcodec_send_packet(decCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(decCtx, frame) == 0) {
            // Check frame timestamp
            // Keep frames that overlap the window: reject those whose end is
            // at/before windowStart, or whose start is at/after windowEnd.
            // Earlier code allowed a +frameDuration slack on the upper bound,
            // which made the effective tail another full frame longer than
            // intended and produced false-positive bursts on material that
            // can't actually leak through frame snapping.
            double frameTime = frame->pts * av_q2d(stream->time_base);
            if (frameTime + frameDuration <= windowStart) {
                av_frame_unref(frame);
                continue;
            }
            if (frameTime >= windowEnd) {
                av_frame_unref(frame);
                goto done_reading;
            }

            // Actual channel count of THIS frame. An AC3 stream can change
            // acmod per frame (e.g. 5.1 -> 2.0), so the decoder context reports
            // the maximum layout (channels) while an individual frame may carry
            // fewer planes. For planar formats frame->data[ch] is then NULL for
            // the missing channels; for interleaved formats a constant stride
            // would over-read past the frame. Use the frame's own count.
            int frameChannels = frame->ch_layout.nb_channels;
            if (frameChannels <= 0) frameChannels = channels;

            // Calculate RMS from decoded samples
            double sumSq = 0.0;
            int totalSamples = frame->nb_samples * frameChannels;

            if (totalSamples > 0) {
                // Handle different sample formats
                switch (decCtx->sample_fmt) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP:
                    sumSq = sumSquares<float>(frame, decCtx->sample_fmt == AV_SAMPLE_FMT_FLTP,
                                              frameChannels, 1.0);
                    break;
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P:
                    sumSq = sumSquares<int16_t>(frame, decCtx->sample_fmt == AV_SAMPLE_FMT_S16P,
                                                frameChannels, 1.0 / 32768.0);
                    break;
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P:
                    sumSq = sumSquares<int32_t>(frame, decCtx->sample_fmt == AV_SAMPLE_FMT_S32P,
                                                frameChannels, 1.0 / 2147483648.0);
                    break;
                default:
                    // Unsupported format — skip
                    av_frame_unref(frame);
                    continue;
                }

                double rms = sqrt(sumSq / totalSamples);
                double rmsDb = (rms > 0.0) ? 20.0 * log10(rms) : -120.0;
                rmsValues.append(rmsDb);
            }
            av_frame_unref(frame);
        }
    }

done_reading:
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decCtx);
    avformat_close_input(&fmtCtx);

    if (rmsValues.size() < 3) {
        TTMessageLogger::getInstance()->errorMsg(__FILE__, __LINE__,
            QString("detectAudioBurst: only %1 chunks at %2 (need >=3)")
                .arg(rmsValues.size()).arg(boundaryTime));
        return false;
    }

    // Calculate median RMS
    QList<double> sorted = rmsValues;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];

    // Check for burst: the PEAK of the boundary chunks must exceed the surrounding
    // level by at least minDeltaDb and clear the absolute audibility floor.
    // Taking the peak rather than the first chunk above the threshold keeps the
    // decision independent of where the audio frame raster happens to fall on the
    // burst's onset ramp, which climbs 38..51 dB within a single 32 ms frame.
    // For CutOut: check last 2 chunks; for CutIn: check first 2 chunks
    int checkStart = isCutOut ? qMax(0, rmsValues.size() - 2) : 0;
    int checkEnd   = isCutOut ? rmsValues.size() : qMin(2, rmsValues.size());

    double peak = -120.0;   // same floor rmsDb uses for silent chunks
    for (int i = checkStart; i < checkEnd; i++)
        peak = qMax(peak, rmsValues[i]);

    if (peak - median >= minDeltaDb && peak > kBurstAbsoluteFloorDb) {
        burstRmsDb = peak;
        contextRmsDb = median;
        if (TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "detectAudioBurst: BURST at" << boundaryTime
                     << (isCutOut ? "CutOut" : "CutIn")
                     << "burst=" << burstRmsDb << "dB, context=" << median << "dB"
                     << "(" << rmsValues.size() << "chunks)"
                     << "file=" << QFileInfo(audioFile).fileName();
        }
        return true;
    }

    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "detectAudioBurst: OK at" << boundaryTime
                 << (isCutOut ? "CutOut" : "CutIn")
                 << "median=" << median << "dB (" << rmsValues.size() << "chunks)"
                 << "file=" << QFileInfo(audioFile).fileName();
    }
    return false;
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTAudioCutter::setError(const QString& error)
{
    ttSetLastError(mLastError, __FILE__, __LINE__, "TTAudioCutter", error);
}
