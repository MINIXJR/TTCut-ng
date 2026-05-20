/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttquickjumpworker.h"

#include "../avstream/ttavtypes.h"
#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../common/ttmessagelogger.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QDebug>
#include <QImage>

TTQuickJumpWorker::TTQuickJumpWorker(
    const QString& filePath, int streamType,
    const QList<int>& frameIndices, const QSize& thumbSize,
    TTVideoIndexList* indexList, TTVideoHeaderList* headerList,
    const QList<TTFrameInfo>& prebuiltFrameIndex)
  : TTThreadTask("QuickJumpThumbnails"),
    mFilePath(filePath),
    mStreamType(streamType),
    mFrameIndices(frameIndices),
    mThumbSize(thumbSize),
    mIndexList(indexList),
    mHeaderList(headerList),
    mPrebuiltFrameIndex(prebuiltFrameIndex)
{
}

void TTQuickJumpWorker::operation()
{
  mTotalSteps = mFrameIndices.size();
  onStatusReport(StatusReportArgs::Start, tr("Decoding thumbnails"), mTotalSteps);

  bool isMpeg2 = (mStreamType == TTAVTypes::mpeg2_demuxed_video);

  // Create decoder ONCE for all frames (thread-safe: own instance per worker)
  TTMpeg2Decoder* mpeg2Decoder = 0;
  TTFFmpegWrapper* ffmpegWrapper = 0;

  if (isMpeg2) {
    try {
      mpeg2Decoder = new TTMpeg2Decoder(mFilePath, mIndexList, mHeaderList, formatRGB32);
    } catch (TTMpeg2DecoderException) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("QuickJump: Failed to create MPEG-2 decoder"));
      return;
    }
  } else {
    // TTFFmpegWrapper is QObject-based -- MUST be created in worker thread (here)
    ffmpegWrapper = new TTFFmpegWrapper();
    if (!ffmpegWrapper->openFile(mFilePath)) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("QuickJump: Failed to open file: %1").arg(ffmpegWrapper->lastError()));
      delete ffmpegWrapper;
      return;
    }
    // Use prebuilt frame index if available (avoids 6s rebuild for H.264/H.265)
    if (!mPrebuiltFrameIndex.isEmpty()) {
      ffmpegWrapper->setFrameIndex(mPrebuiltFrameIndex);
    } else {
      if (!ffmpegWrapper->buildFrameIndex()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("QuickJump: Failed to build frame index: %1").arg(ffmpegWrapper->lastError()));
        delete ffmpegWrapper;
        return;
      }
    }
  }

  for (int i = 0; i < mFrameIndices.size(); ++i) {
    if (mIsAborted) break;

    int frameIndex = mFrameIndices.at(i);
    QImage thumb;

    if (isMpeg2) {
      try {
        mpeg2Decoder->moveToFrameIndex(frameIndex);
        TFrameInfo* frame = mpeg2Decoder->decodeMPEG2Frame(formatRGB32);
        if (frame && frame->Y) {
          QImage img(frame->Y, frame->width, frame->height, QImage::Format_RGB32);
          thumb = img.scaled(mThumbSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
      } catch (TTMpeg2DecoderException) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("QuickJump: MPEG-2 decode failed for frame %1").arg(frameIndex));
      }
    } else {
      QImage img = ffmpegWrapper->decodeFrame(frameIndex);
      if (!img.isNull()) {
        thumb = img.scaled(mThumbSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      }
    }

    // Emit QImage (not QPixmap!) -- QPixmap is NOT thread-safe.
    // Model converts to QPixmap in main thread.
    emit thumbnailReady(frameIndex, thumb);  // null QImage = decode failed

    mStepCount = i + 1;
    onStatusReport(StatusReportArgs::Step, QString(), mStepCount);
  }

  delete mpeg2Decoder;
  if (ffmpegWrapper) {
    ffmpegWrapper->closeFile();
    delete ffmpegWrapper;
  }

  onStatusReport(StatusReportArgs::Finished, tr("Thumbnails complete"), mStepCount);
}

void TTQuickJumpWorker::cleanUp()
{
}

void TTQuickJumpWorker::onUserAbort()
{
  mIsAborted = true;
}
