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
// TTPLAYBACKMUXTASK
//
// The playback temp MKV used to be muxed synchronously inside
// TTCurrentFrame::onPlayVideo(): no progress, no cancel, and the window
// reported "not responding" for the ~6 s the mux takes. This task moves the
// provider call to a worker thread; the cooperative abort of
// TTMkvMergeProvider (requestAbort()/checkAbort() per packet) does the
// stopping, this task delivers the request and removes an incomplete file.
// ----------------------------------------------------------------------------

#include "ttplaybackmuxtask.h"

#include "../common/ttexception.h"

#include <QFile>

TTPlaybackMuxTask::TTPlaybackMuxTask(const TTPlaybackMuxParams& params)
  : TTThreadTask("PlaybackMuxTask"), mParams(params)
{
}

/**
 * Abort request; GUI thread while operation() runs on the worker. Only
 * atomic stores happen here.
 */
void TTPlaybackMuxTask::onUserAbort()
{
  mProvider.requestAbort();
  // The flag directly, not TTThreadTask::abort(): for a task that has not
  // entered run() yet, abort() emits aborted() and calls cleanUp() on the
  // GUI thread right away, and the owner's deleteLater on that signal would
  // then race the worker that still executes run(). The pool has its queue
  // bookkeeping for that window; this task has only the flag, which run()
  // checks on entry and operation() before the mux.
  mIsAborted = true;
}

void TTPlaybackMuxTask::discard()
{
  mDiscard.store(true, std::memory_order_relaxed);
  onUserAbort();
}

/**
 * Removes the output unless the mux ran to a successful conclusion and the
 * owner still wants it. run() calls this on every exit, including a cancel
 * that arrived before the pool scheduled run() - then operation() never
 * executed and there is nothing to remove.
 */
void TTPlaybackMuxTask::cleanUp()
{
  const bool keep = mSucceeded && !mDiscard.load(std::memory_order_relaxed);
  if (!keep && QFile::exists(mParams.outputFile))
    QFile::remove(mParams.outputFile);
}

/**
 * The mux itself.
 */
void TTPlaybackMuxTask::operation()
{
  // Direct connection: the provider emits on this thread, the re-emitted
  // progress() reaches the widget through its own queued connection.
  connect(&mProvider, &TTMkvMergeProvider::progressChanged, this,
      [this](int percent, const QString&) { emit progress(percent); },
      Qt::DirectConnection);

  mProvider.setVideoOptions(mParams.video);
  // Playback without its audio is still playback; the muxer logs the skip.
  mProvider.setRequireAllInputs(false);

  // A cancel between construction and here has not been polled yet; mux()'s
  // own entry poll would catch it only after creating the output file.
  if (isAborted()) throw TTAbortException("playback mux cancelled");

  if (!mProvider.mux(mParams.outputFile, mParams.videoFile, mParams.audioFiles)) {
    // A cancel comes back through the same false return as a real failure.
    if (mProvider.wasAborted() || isAborted())
      throw TTAbortException("playback mux cancelled");
    mError = mProvider.lastError();
    return;
  }
  mSucceeded = true;
}
