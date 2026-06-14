/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask.h"

#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../extern/ttffmpegwrapper.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QDebug>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <cstring>

TTSearchTask::TTSearchTask(const QString& taskName,
                           const QString& videoFilePath,
                           TTAVTypes::AVStreamType streamType,
                           TTVideoIndexList* indexList,
                           TTVideoHeaderList* headerList,
                           int startPos, int direction, int frameCount,
                           const QList<TTFrameInfo>& preBuiltFrameIndex)
  : TTThreadTask(taskName),
    mIndexList(indexList),
    mHeaderList(headerList),
    mStartPos(startPos),
    mDirection(direction),
    mFrameCount(frameCount),
    mFilePath(videoFilePath),
    mStreamType(streamType),
    mPreBuiltFrameIndex(preBuiltFrameIndex)
{
}

TTSearchTask::~TTSearchTask()
{
  closeDecoder();
}

void TTSearchTask::onUserAbort()
{
  mIsAborted = true;
}

bool TTSearchTask::openDecoder()
{
  if (mStreamType == TTAVTypes::h264_video || mStreamType == TTAVTypes::h265_video) {
    mFFmpegWrapper = new TTFFmpegWrapper();
    mFFmpegWrapper->setAnalysisMode(true);
    if (!mFFmpegWrapper->openFile(mFilePath)) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("TTSearchTask: openFile failed for %1").arg(mFilePath));
      delete mFFmpegWrapper;
      mFFmpegWrapper = nullptr;
      return false;
    }
    if (!mPreBuiltFrameIndex.isEmpty())
      mFFmpegWrapper->setFrameIndex(mPreBuiltFrameIndex);
    else
      mFFmpegWrapper->buildFrameIndex();
    return true;
  }

  if (mStreamType == TTAVTypes::mpeg2_demuxed_video) {
    try {
      mMpeg2Decoder = new TTMpeg2Decoder(mFilePath, mIndexList, mHeaderList, formatRGB32);
      return true;
    } catch (TTMpeg2DecoderException& ex) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("TTSearchTask: TTMpeg2Decoder ctor failed: %1").arg(ex.message()));
      mMpeg2Decoder = nullptr;
      return false;
    }
  }

  log->errorMsg(__FILE__, __LINE__,
                QString("TTSearchTask: unsupported stream type %1").arg((int)mStreamType));
  return false;
}

void TTSearchTask::closeDecoder()
{
  if (mFFmpegWrapper) {
    mFFmpegWrapper->closeFile();
    delete mFFmpegWrapper;
    mFFmpegWrapper = nullptr;
  }
  if (mMpeg2Decoder) {
    delete mMpeg2Decoder;
    mMpeg2Decoder = nullptr;
  }
}

QImage TTSearchTask::decodeFrameAt(int pos)
{
  if (mFFmpegWrapper) return mFFmpegWrapper->decodeFrame(pos);

  if (mMpeg2Decoder) {
    try {
      mMpeg2Decoder->moveToFrameIndex(pos);
      TFrameInfo* fi = mMpeg2Decoder->getFrameInfo();
      if (!fi || !fi->Y) return QImage();
      // fi->Y holds RGB32 data when the decoder was constructed with formatRGB32.
      return QImage(fi->Y, fi->width, fi->height, QImage::Format_RGB32).copy();
    } catch (TTMpeg2DecoderException&) {
      return QImage();
    }
  }
  return QImage();
}

bool TTSearchTask::isFrameBlackAt(int pos, int pixelThreshold, float ratioThreshold)
{
  if (mFFmpegWrapper)
    return mFFmpegWrapper->isFrameBlack(pos, pixelThreshold, ratioThreshold);

  if (!mMpeg2Decoder) return false;

  // MPEG-2 path: replicate TTMPEG2Window2::isBlackAt (master line 432-477)
  // verbatim, but on a worker-owned decoder.
  QImage gray;
  try {
    mMpeg2Decoder->moveToFrameIndex(pos);
    TFrameInfo* fi = mMpeg2Decoder->getFrameInfo();
    if (!fi || !fi->Y) return false;
    QImage rgb(fi->Y, fi->width, fi->height, QImage::Format_RGB32);
    gray = rgb.convertToFormat(QImage::Format_Grayscale8);
  } catch (TTMpeg2DecoderException&) {
    return false;
  }
  if (gray.isNull()) return false;

  int w = gray.width(), h = gray.height();
  int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;

  const int step = 2;
  const int earlyExitSamples = 500;
  long lumaSum = 0;
  int totalPixels = 0, blackPixels = 0;

  for (int row = y0; row < y1; row += step) {
    const uchar* line = gray.constScanLine(row);
    for (int col = x0; col < x1; col += step) {
      totalPixels++;
      lumaSum += line[col];
      if (line[col] < pixelThreshold) blackPixels++;
    }
    if (totalPixels >= earlyExitSamples) {
      float avgSoFar = (float)lumaSum / totalPixels;
      if (avgSoFar > 5.0f) return false;
    }
  }
  if (totalPixels == 0) return false;
  if ((float)lumaSum / totalPixels > 5.0f) return false;
  return (float)blackPixels / totalPixels >= ratioThreshold;
}

bool TTSearchTask::buildHistogramAt(int pos, int hist[256], int& totalPixels)
{
  std::memset(hist, 0, 256 * sizeof(int));
  totalPixels = 0;

  if (mFFmpegWrapper)
    return mFFmpegWrapper->buildHistogram(pos, hist, totalPixels);

  if (!mMpeg2Decoder) return false;

  // MPEG-2 path: replicate TTMPEG2Window2::buildHistogramAt MPEG-2 branch.
  try {
    mMpeg2Decoder->moveToFrameIndex(pos);
    TFrameInfo* fi = mMpeg2Decoder->getFrameInfo();
    if (!fi || !fi->Y) return false;
    QImage rgb(fi->Y, fi->width, fi->height, QImage::Format_RGB32);
    QImage gray = rgb.convertToFormat(QImage::Format_Grayscale8);

    int w = gray.width(), h = gray.height();
    int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;
    const int step = 2;

    for (int row = y0; row < y1; row += step) {
      const uchar* line = gray.constScanLine(row);
      for (int col = x0; col < x1; col += step) {
        hist[line[col]]++;
        totalPixels++;
      }
    }
    return totalPixels > 0;
  } catch (TTMpeg2DecoderException&) {
    return false;
  }
}

void TTSearchTask::cleanUp()
{
  // No-op: closeDecoder() runs only in the destructor (GUI thread via deleteLater).
  // Calling it here from the worker thread races with the destructor on GUI thread
  // — without a happens-before edge between the two, the destructor can re-read a
  // stale non-null wrapper pointer and double-free.
}

bool TTSearchTask::setupWorkers()
{
  // Resolve worker count: 0 = auto (idealThreadCount/2 cap 4), clamp [1, 16].
  int n = TTSettings::instance()->searchWorkerCount();
  if (n <= 0) n = qBound(1, QThread::idealThreadCount() / 2, 4);
  mWorkerCount = qBound(1, n, 16);

  // MPEG-2: single decoder for now (libmpeg2 multi-decoder is future work).
  if (mStreamType == TTAVTypes::mpeg2_demuxed_video) {
    mWorkerCount = 1;
    if (!openDecoder()) return false;   // populates mMpeg2Decoder (existing path)
    return true;
  }

  // H.264 / H.265: open N FFmpeg wrappers in parallel.
  if (mStreamType != TTAVTypes::h264_video && mStreamType != TTAVTypes::h265_video) {
    log->errorMsg(__FILE__, __LINE__,
                  QString("TTSearchTask::setupWorkers: unsupported stream type %1")
                      .arg((int)mStreamType));
    return false;
  }

  mSubWrappers.reserve(mWorkerCount);
  for (int i = 0; i < mWorkerCount; ++i) {
    auto* w = new TTFFmpegWrapper();
    w->setAnalysisMode(true);
    w->setSearchMode(true);
    if (!w->openFile(mFilePath)) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("TTSearchTask::setupWorkers: openFile failed for %1 (worker %2)")
                        .arg(mFilePath).arg(i));
      delete w;
      teardownWorkers();   // delete previously-opened wrappers
      return false;
    }
    if (!mPreBuiltFrameIndex.isEmpty())
      w->setFrameIndex(mPreBuiltFrameIndex);
    else
      w->buildFrameIndex();
    mSubWrappers.append(w);
  }

  mDecodePool = new QThreadPool();
  mDecodePool->setMaxThreadCount(mWorkerCount);
  return true;
}

void TTSearchTask::teardownWorkers()
{
  if (mDecodePool) {
    mDecodePool->waitForDone();
    delete mDecodePool;
    mDecodePool = nullptr;
  }
  for (TTFFmpegWrapper* w : mSubWrappers) {
    if (w) {
      w->closeFile();
      delete w;
    }
  }
  mSubWrappers.clear();
  // mMpeg2Decoder is freed by the existing closeDecoder() chain (~TTSearchTask).
}

// Index-space note (display-order unification): batch positions come from
// moveToNextIndexPos/moveToPrevIndexPos, i.e. positions in the (display-order
// sorted) index list — DISPLAY positions for all codecs. They are passed to
// isFrameBlack()/decodeFrameYUV(), which interpret their argument as a display
// position (the seek+skip-loop counts decoder output, which is display order),
// and the resulting hit is emitted as a display position consumed by
// navigation (onVideoSliderChanged). The whole search path is therefore
// display-order consistent end to end; do NOT convert these to decode/AU
// indices.
QVector<int> TTSearchTask::collectNextBatch(int& currentPos)
{
  QVector<int> batch;
  batch.reserve(mWorkerCount);
  int p = currentPos;
  while (batch.size() < mWorkerCount && p >= 0 && p < mFrameCount) {
    batch.append(p);
    p = (mDirection > 0)
        ? mIndexList->moveToNextIndexPos(p, 1)
        : mIndexList->moveToPrevIndexPos(p, 1);
  }
  currentPos = p;   // first position AFTER the batch (or -1 / out-of-range)
  return batch;
}
