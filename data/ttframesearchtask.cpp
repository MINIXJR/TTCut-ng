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
#include "../avstream/ttframeindexer.h"
#include "../avstream/tth264videostream.h"
#include "../avstream/tth265videostream.h"
#include "../avstream/tth26xvideostream.h"  // frameIndexBundle (index sharing)
#include "../mpeg2decoder/ttmpeg2decoder.h"
#include "../avstream/ttcommon.h"

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
TTFrameSearchTask::DecoderKind TTFrameSearchTask::decoderKindFor(TTVideoStream* stream)
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
    const TFrameInfo* fi = refDecoder->getFrameInfo();
    refInfo = *fi;
    captureRefBuffers(refInfo);

    delete refDecoder;
  } else {
    TTFFmpegWrapper* refWrapper = openFFmpegWrapperFor(mpReferenceStream, "reference");

    if (!refWrapper->decodeFrameYUV(mReferenceIndex, refInfo)) {
      refWrapper->closeFile();
      delete refWrapper;
      throw TTAbortException("TTFrameSearchTask: decodeFrameYUV failed for reference frame");
    }
    captureRefBuffers(refInfo);

    refWrapper->closeFile();
    delete refWrapper;
  }
}

TTFFmpegWrapper* TTFrameSearchTask::openFFmpegWrapperFor(TTVideoStream* stream, const char* role)
{
  TTFFmpegWrapper* wrapper = new TTFFmpegWrapper();
  if (!wrapper->openFile(stream->filePath())) {
    delete wrapper;
    throw TTAbortException(QString("TTFrameSearchTask: could not open %1 stream for FFmpeg decode").arg(role));
  }
  wrapper->setCancelToken(&mAbort);   // a cancel must end a decode in flight
  // Index sharing (spec 2026-06-05): an H.26x stream has already built its
  // frame index at stream-open - adopt it instead of rescanning the file
  // (measured with tools/diag/test_framesearch_progress on a 224 930-frame
  // H.264 recording: the rescan took 5553 ms of an 11 464 ms search, with
  // nothing to see in the progress dialog). `stream` IS the source stream
  // object and the wrapper opened its filePath(), so file identity is
  // guaranteed by object identity. The bundle is empty when the stream has
  // no index yet (different item, never opened) - then the scan runs.
  const TTH26xVideoStream* h26x = dynamic_cast<const TTH26xVideoStream*>(stream);
  if (!wrapper->adoptOrBuildFrameIndex(h26x ? h26x->frameIndexBundle() : TTFrameIndexBundle(),
                                       stream->filePath())) {
    wrapper->closeFile();
    delete wrapper;
    throw TTAbortException(QString("TTFrameSearchTask: frame index build failed for %1 stream").arg(role));
  }
  wrapper->setSearchMode(false);
  return wrapper;
}

void TTFrameSearchTask::captureRefBuffers(const TFrameInfo& refInfo)
{
  mRefWidth  = refInfo.width;
  mRefHeight = refInfo.height;
  mpRefY = new quint8[refInfo.size];
  mpRefU = new quint8[refInfo.chroma_size];
  mpRefV = new quint8[refInfo.chroma_size];
  memcpy(mpRefY, refInfo.Y, refInfo.size);
  memcpy(mpRefU, refInfo.U, refInfo.chroma_size);
  memcpy(mpRefV, refInfo.V, refInfo.chroma_size);
}

//! Compare two frames in YUV420 pixel format using per-plane buffers
quint64 TTFrameSearchTask::compareFrames(const TFrameInfo& searchInfo)
{
  // Sum of squared differences over the three planes.
  auto planeDelta = [](const quint8* a, const quint8* b, int n) {
    quint64 sum = 0;
    for (int j = 0; j < n; j++) {
      int d = (int)a[j] - (int)b[j];
      sum += (quint64)(d * d);
    }
    return sum;
  };
  return planeDelta(searchInfo.Y, mpRefY, searchInfo.size)
       + planeDelta(searchInfo.U, mpRefU, searchInfo.chroma_size)
       + planeDelta(searchInfo.V, mpRefV, searchInfo.chroma_size);
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
  // Report the start BEFORE the expensive preparation below, not after it.
  //
  // The window is disabled the moment the pool emits its Init, but the
  // progress dialog only appears when a task reports Start - and this task
  // used to do that after opening its own decoder, building a frame index and
  // seeking to the search position. Measured on a 210 954-frame H.264 stream:
  // 13 s of locked, silent UI followed by a dialog for the remaining 8 s of a
  // 21 s search. The user sees an application that looks hung and then briefly
  // explains itself.
  //
  // Everything this needs is stream metadata (frame rate, frame count), so it
  // can be computed before any decoder exists; the search loop below uses the
  // same values.
  QTime searchTime(0, 0, 0, 0);
  int   searchFrameCount = ttTimeToFrames(
      searchTime.addSecs(TTSettings::instance()->searchLength()),
      mpSearchStream->frameRate());
  // Clamp to remaining frames in stream - without this, MPEG-2's
  // moveToFrameIndex() crashes on out-of-range positions and the worker
  // terminates silently without emitting finished().
  int   remainingFrames  = mpSearchStream->frameCount() - mSearchIndex;
  if (searchFrameCount > remainingFrames) searchFrameCount = remainingFrames;
  if (searchFrameCount < 0)               searchFrameCount = 0;

  onStatusReport(this, StatusReportArgs::Start, tr("Searching frame"), searchFrameCount);

  initFrameSearch();

  TTMpeg2Decoder*  searchMpeg2   = nullptr;
  TTFFmpegWrapper* searchWrapper = nullptr;
  bool useFFmpeg = (decoderKindFor(mpSearchStream) == DecoderKind::FFmpeg);

  if (useFFmpeg) {
    searchWrapper = openFFmpegWrapperFor(mpSearchStream, "search");
  } else {
    searchMpeg2 = new TTMpeg2Decoder(
        mpSearchStream->filePath(),
        mpSearchStream->indexList(),
        mpSearchStream->headerList(),
        formatYV12);
    searchMpeg2->decodeFirstMPEG2Frame(formatYV12);
    searchMpeg2->moveToFrameIndex(mSearchIndex);
  }

  int     index            = 0;
  int     foundPosition    = 0;
  // Threshold based on frame size: allow ~10% average difference per pixel
  // For YUV420: w*h + 2*(w/2)*(h/2) bytes total, squared difference per byte
  // threshold = totalBytes * 625 where 25 ≈ 10% of 255
  quint64 threshold        = (quint64)(mRefWidth * mRefHeight
                                       + 2 * (mRefWidth/2) * (mRefHeight/2)) * 625;
  quint64 minDelta         = threshold;

  do
  {
    if (mAbort)
    {
      if (searchMpeg2)   delete searchMpeg2;
      if (searchWrapper) { searchWrapper->closeFile(); delete searchWrapper; }
      throw TTAbortException("User abort in TTFrameSearchTask!");
    }

    TFrameInfo searchInfo;
    if (useFFmpeg) {
      const bool decodeOK = searchWrapper->decodeFrameYUV(mSearchIndex + index, searchInfo);
      if (!decodeOK) {
        index++;
        continue;
      }
    } else {
      const TFrameInfo* fi = searchMpeg2->getFrameInfo();
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
