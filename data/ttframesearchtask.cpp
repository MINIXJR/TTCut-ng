/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFRAMESEARCHTASK
// ----------------------------------------------------------------------------

#include "ttframesearchtask.h"

#include <QDebug>

#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttexception.h"
#include "../common/istatusreporter.h"
#include "../avstream/ttavstream.h"
#include "../extern/ttffmpegwrapper.h"
#include "../avstream/tth264videostream.h"
#include "../avstream/tth265videostream.h"

//! Search for an equal frame
TTFrameSearchTask::TTFrameSearchTask(TTVideoStream* referenceStream, int referenceIndex,
                                 TTVideoStream* searchStream, int searchIndex)
                  : TTThreadTask("FrameSearchTask"),
                    mpReferenceStream(referenceStream),
                    mpSearchStream(searchStream),
                    mReferenceIndex(referenceIndex),
                    mSearchIndex(searchIndex),
                    mpRefY(nullptr),
                    mpRefU(nullptr),
                    mpRefV(nullptr),
                    mRefWidth(0),
                    mRefHeight(0),
                    mAbort(false)
{
}

//! Decide which decoder backend to use for a given video stream.
TTFrameSearchTask::DecoderKind TTFrameSearchTask::decoderKindFor(TTVideoStream* stream) const
{
  if (dynamic_cast<TTH264VideoStream*>(stream)) return DecoderKind::FFmpeg;
  if (dynamic_cast<TTH265VideoStream*>(stream)) return DecoderKind::FFmpeg;
  return DecoderKind::Mpeg2;
}

//! Init frame search task
void TTFrameSearchTask::initFrameSearch()
{
  TFrameInfo refInfo;

  if (decoderKindFor(mpReferenceStream) == DecoderKind::Mpeg2) {
    TTMpeg2Decoder* refDecoder = new TTMpeg2Decoder(
        mpReferenceStream->filePath(),
        mpReferenceStream->indexList(),
        mpReferenceStream->headerList(),
        formatYV12);
    refDecoder->moveToFrameIndex(mReferenceIndex);
    TFrameInfo* fi = refDecoder->getFrameInfo();
    refInfo = *fi;

    mRefWidth  = refInfo.width;
    mRefHeight = refInfo.height;
    mpRefY = new quint8[refInfo.size];
    mpRefU = new quint8[refInfo.chroma_size];
    mpRefV = new quint8[refInfo.chroma_size];
    memcpy(mpRefY, refInfo.Y, refInfo.size);
    memcpy(mpRefU, refInfo.U, refInfo.chroma_size);
    memcpy(mpRefV, refInfo.V, refInfo.chroma_size);

    delete refDecoder;
  } else {
    TTFFmpegWrapper* refWrapper = new TTFFmpegWrapper();
    if (!refWrapper->openFile(mpReferenceStream->filePath())) {
      delete refWrapper;
      throw TTAbortException("TTFrameSearchTask: could not open reference stream for FFmpeg decode");
    }
    if (!refWrapper->buildFrameIndex()) {
      refWrapper->closeFile();
      delete refWrapper;
      throw TTAbortException("TTFrameSearchTask: buildFrameIndex failed for reference stream");
    }
    refWrapper->setSearchMode(false);

    if (!refWrapper->decodeFrameYUV(mReferenceIndex, refInfo)) {
      refWrapper->closeFile();
      delete refWrapper;
      throw TTAbortException("TTFrameSearchTask: decodeFrameYUV failed for reference frame");
    }

    mRefWidth  = refInfo.width;
    mRefHeight = refInfo.height;
    mpRefY = new quint8[refInfo.size];
    mpRefU = new quint8[refInfo.chroma_size];
    mpRefV = new quint8[refInfo.chroma_size];
    memcpy(mpRefY, refInfo.Y, refInfo.size);
    memcpy(mpRefU, refInfo.U, refInfo.chroma_size);
    memcpy(mpRefV, refInfo.V, refInfo.chroma_size);

    refWrapper->closeFile();
    delete refWrapper;
  }
}

//! Compare two frames in YUV420 pixel format using per-plane buffers
quint64 TTFrameSearchTask::compareFrames(const TFrameInfo& searchInfo)
{
  quint64 delta = 0;

  // Y plane
  for (int j = 0; j < searchInfo.size; j++) {
    int d = (int)searchInfo.Y[j] - (int)mpRefY[j];
    delta += (quint64)(d * d);
  }
  // U plane
  for (int j = 0; j < searchInfo.chroma_size; j++) {
    int d = (int)searchInfo.U[j] - (int)mpRefU[j];
    delta += (quint64)(d * d);
  }
  // V plane
  for (int j = 0; j < searchInfo.chroma_size; j++) {
    int d = (int)searchInfo.V[j] - (int)mpRefV[j];
    delta += (quint64)(d * d);
  }

  return delta;
}

//! Clean up after operation
void TTFrameSearchTask::cleanUp()
{
  delete[] mpRefY;
  delete[] mpRefU;
  delete[] mpRefV;
  mpRefY = nullptr;
  mpRefU = nullptr;
  mpRefV = nullptr;
}

//! Abort request
void TTFrameSearchTask::onUserAbort()
{
  mAbort = true;
}

//! Task operation method
void TTFrameSearchTask::operation()
{
  initFrameSearch();

  TTMpeg2Decoder*  searchMpeg2   = nullptr;
  TTFFmpegWrapper* searchWrapper = nullptr;
  bool useFFmpeg = (decoderKindFor(mpSearchStream) == DecoderKind::FFmpeg);

  if (useFFmpeg) {
    searchWrapper = new TTFFmpegWrapper();
    if (!searchWrapper->openFile(mpSearchStream->filePath())) {
      delete searchWrapper;
      throw TTAbortException("TTFrameSearchTask: could not open search stream for FFmpeg decode");
    }
    if (!searchWrapper->buildFrameIndex()) {
      searchWrapper->closeFile();
      delete searchWrapper;
      throw TTAbortException("TTFrameSearchTask: buildFrameIndex failed for search stream");
    }
    searchWrapper->setSearchMode(false);
  } else {
    searchMpeg2 = new TTMpeg2Decoder(
        mpSearchStream->filePath(),
        mpSearchStream->indexList(),
        mpSearchStream->headerList(),
        formatYV12);
    searchMpeg2->decodeFirstMPEG2Frame(formatYV12);
    searchMpeg2->moveToFrameIndex(mSearchIndex);
  }

  QTime   searchTime(0,0,0,0);
  int     searchFrameCount = ttTimeToFrames(
      searchTime.addSecs(TTSettings::instance()->searchLength()),
      mpSearchStream->frameRate());
  // Clamp to remaining frames in stream — without this, MPEG-2's
  // moveToFrameIndex() crashes on out-of-range positions and the worker
  // terminates silently without emitting finished().
  int     remainingFrames  = mpSearchStream->frameCount() - mSearchIndex;
  if (searchFrameCount > remainingFrames) searchFrameCount = remainingFrames;
  if (searchFrameCount < 0)               searchFrameCount = 0;
  int     index            = 0;
  int     foundPosition    = 0;
  // Threshold based on frame size: allow ~10% average difference per pixel
  // For YUV420: w*h + 2*(w/2)*(h/2) bytes total, squared difference per byte
  // threshold = totalBytes * 625 where 25 ≈ 10% of 255
  quint64 threshold        = (quint64)(mRefWidth * mRefHeight
                                       + 2 * (mRefWidth/2) * (mRefHeight/2)) * 625;
  quint64 minDelta         = threshold;

  onStatusReport(this, StatusReportArgs::Start, tr("Searching frame"), searchFrameCount);

  do
  {
    if (mAbort)
    {
      if (searchMpeg2)   delete searchMpeg2;
      if (searchWrapper) { searchWrapper->closeFile(); delete searchWrapper; }
      throw TTAbortException("User abort in TTFrameSearchTask!");
    }

    TFrameInfo searchInfo;
    bool decodeOK = true;

    if (useFFmpeg) {
      decodeOK = searchWrapper->decodeFrameYUV(mSearchIndex + index, searchInfo);
      if (!decodeOK) {
        index++;
        continue;
      }
    } else {
      TFrameInfo* fi = searchMpeg2->getFrameInfo();
      searchInfo = *fi;
    }

    if (searchInfo.width  != mRefWidth ||
        searchInfo.height != mRefHeight) {
      index++;
      if (!useFFmpeg) {
        searchMpeg2->moveToFrameIndex(mSearchIndex + index);
      }
      continue;
    }

    quint64 delta = compareFrames(searchInfo);

    if (delta == 0) {
      foundPosition = index;
      break;
    }

    if (delta < minDelta) {
      minDelta      = delta;
      foundPosition = index;
    }

    index++;
    onStatusReport(this, StatusReportArgs::Step, tr("Searching frame"), index);

    // Only advance the MPEG-2 decoder if the next iteration will run.
    // moveToFrameIndex on an out-of-range position crashes silently.
    if (!useFFmpeg && index < searchFrameCount) {
      searchMpeg2->moveToFrameIndex(mSearchIndex + index);
    }
  } while (index < searchFrameCount);

  if (searchMpeg2)   delete searchMpeg2;
  if (searchWrapper) { searchWrapper->closeFile(); delete searchWrapper; }

  if (minDelta >= threshold) {
    log->debugMsg(__FILE__, __LINE__, QString("no matching frame found (minDelta %1 >= threshold %2)")
        .arg(minDelta).arg(threshold));
    onStatusReport(this, StatusReportArgs::Step, tr("No match found"), searchFrameCount);
    emit finished(-1);
    return;
  }

  log->debugMsg(__FILE__, __LINE__, QString("found equal frame at %1 searchIndex %2 foundPos %3 delta %4")
      .arg(mSearchIndex+foundPosition).arg(mSearchIndex).arg(foundPosition).arg(minDelta));

  onStatusReport(this, StatusReportArgs::Step, tr("Frame found"), searchFrameCount);

  emit finished(mSearchIndex+foundPosition);
}
