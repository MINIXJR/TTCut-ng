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
// TTMUXTASK
//
// One TTMkvMergeProvider::mux() call as a thread pool task, so the MPEG-2
// cut's MKV mux can be cancelled. The provider's cooperative abort
// (requestAbort()/wasAborted()) does the actual stopping; this task delivers
// the request from the GUI thread and cleans up afterwards.
//
// Status messages keep the shape of the synchronous version: progress is
// forwarded through TTAVData::onMuxProgress, i.e. as Step reports with the
// percentage as value - the progress dialog and its remaining-time estimator
// are calibrated on them.
// ----------------------------------------------------------------------------

#include "ttmuxtask.h"
#include "../avstream/ttcommon.h"

#include "ttavdata.h"

#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"

#include <QFile>

/**
 * MKV mux task
 */
TTMuxTask::TTMuxTask(TTAVData* avData) : TTAbortableTask(avData, "MuxTask")
{
}

/**
 * Init task
 *
 * mCreatedFiles is built here, on the GUI thread, so it is complete before the
 * task can possibly be aborted (see cleanUp()).
 */
void TTMuxTask::init(const TTMuxTaskParams& params)
{
  mParams = params;

  mCreatedFiles = mParams.cleanupOnAbort;
  if (!mParams.chapterFile.isEmpty()) mCreatedFiles.append(mParams.chapterFile);
  mCreatedFiles.append(mParams.mkvOutput);
}

/**
 * Operation abort request
 *
 * Runs on the GUI thread while operation() runs on the pool. Only atomic flag
 * stores happen here - no worker-owned state is touched.
 */
void TTMuxTask::onUserAbort()
{
  requestCancel();
  mMkvProvider.requestAbort();
  abort();   // TTThreadTask bookkeeping (mIsAborted; pool Canceled chain)
}

/**
 * Clean up after operation
 *
 * TTThreadTask::run() calls this on every exit, so the abort guard is what
 * separates the two cases. It also covers the window in which the cancel
 * arrives BEFORE the pool schedules run(): operation() never executes then,
 * run() throws on its entry check, and this is the only place left that can
 * remove the files (mCreatedFiles is filled by init(), not by operation(),
 * precisely for that case).
 */
void TTMuxTask::cleanUp()
{
  // mOperationDone guards the one case in which isAborted() alone would be
  // wrong: a cancel that arrives in the microseconds AFTER mux() returned
  // successfully. The bookkeeping flag is set by then, but the result is
  // finished and must not be deleted (same rule as the missing poll point at
  // the end of operation()).
  if (isAborted() && !mOperationDone) abortCleanup();
}

/**
 * Run the mux
 */
void TTMuxTask::operation()
{
  // Direct connection on purpose: it keeps the forwarding on this thread, so
  // TTAVData::onMuxProgress runs on the worker and simply re-emits. That is
  // what makes it safe for it to carry no processEvents() at all (it is now
  // only ever reached from a worker; see the comment at its definition). The
  // report itself reaches the GUI through its receivers' own queued
  // connections. Same arrangement as TTH26xCutTask::reportStep.
  connect(&mMkvProvider, &TTMkvMergeProvider::progressChanged, this,
      [this](int percent, const QString& msg) { mpAVData->onMuxProgress(percent, msg); },
      Qt::DirectConnection);

  mMkvProvider.setDefaultDuration("0", mParams.defaultDurationNs);
  mMkvProvider.setIsPAFF(mParams.isPAFF, mParams.paffLog2MaxFrameNum);
  mMkvProvider.setVideoCodecId(mParams.videoCodecId);

  if (mParams.audioSyncOffsetMs != 0)
    mMkvProvider.setAudioSyncOffset(mParams.audioSyncOffsetMs);

  mMkvProvider.setAudioLanguages(mParams.audioLanguages);
  mMkvProvider.setSubtitleLanguages(mParams.subtitleLanguages);

  if (mParams.totalDurationMs > 0)
    mMkvProvider.setTotalDurationMs(mParams.totalDurationMs);
  if (!mParams.chapterFile.isEmpty())
    mMkvProvider.setChapterFile(mParams.chapterFile);

  // A cancel that arrived between init() and here has not been polled by
  // anything yet - mux()'s own entry poll would catch it, but only after the
  // output file has been created. Leave before that.
  if (cancelRequested()) abortNow();

  if (!mMkvProvider.mux(mParams.mkvOutput, mParams.videoFile,
                        mParams.audioFiles, mParams.subtitleFiles)) {
    // A cancel comes back through the same false return as a real failure.
    if (mMkvProvider.wasAborted() || cancelRequested()) abortNow();
    mError = mMkvProvider.lastError();
  }

  // Reached on success and on a real mux failure alike: the mux ran to a
  // conclusion, so whatever is on disk is a result, not abort leftovers.
  //
  // No poll point after a SUCCESSFUL mux, deliberately: the cut is complete at
  // this point and there is nothing left to cancel. A cancel arriving in the
  // microseconds afterwards would otherwise delete a finished result - the run
  // reports its regular Exit instead (same rule as TTH26xCutTask).
  mOperationDone = true;
}
