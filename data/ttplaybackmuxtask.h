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
// ----------------------------------------------------------------------------
#ifndef TTPLAYBACKMUXTASK_H
#define TTPLAYBACKMUXTASK_H

#include "../common/ttthreadtask.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

//! Everything TTCurrentFrame::onPlayVideo() feeds into the playback mux,
//! copied on the GUI thread before the task starts.
struct TTPlaybackMuxParams
{
  QString      outputFile;              // temp .mkv
  QString      videoFile;               // H.264/H.265 ES
  QStringList  audioFiles;              // first audio track, if any
  TTMkvVideoOptions video;              // incl. displayOrder (empty = linear PTS)
};

//! One TTMkvMergeProvider::mux() call for the H.264/H.265 playback temp MKV,
//! run on QThreadPool so the GUI stays responsive and the user can cancel.
//!
//! Deliberately not a TTAbortableTask on TTAVData's pool: that pool's exit
//! reloads the tree views and its reports land in the cut progress window,
//! and the widget that plays has no TTAVData anyway. The task owns its
//! provider so onUserAbort() (GUI thread) can reach it with an atomic store
//! while operation() runs on the worker.
class TTPlaybackMuxTask : public TTThreadTask
{
  Q_OBJECT
  public:
    explicit TTPlaybackMuxTask(const TTPlaybackMuxParams& params);

    // Results, valid after finished()/aborted():
    QString outputFile() const { return mParams.outputFile; }
    //! Empty on success; otherwise TTMkvMergeProvider::lastError().
    QString lastError()  const { return mError; }
    const TTPlaybackMuxParams& params() const { return mParams; }

    //! The owner no longer wants the result (stream switched, widget going
    //! away): abort, and remove the output even if the mux completes before
    //! the abort is noticed. GUI thread; atomic stores only.
    void discard();

  signals:
    //! Mux progress in percent; emitted from the worker thread.
    void progress(int percent);

  public slots:
    void onUserAbort() override;

  protected:
    void operation() override;
    void cleanUp() override;

  private:
    TTPlaybackMuxParams mParams;
    TTMkvMergeProvider  mProvider;
    QString             mError;
    //! Set by operation() once mux() returned true. Worker thread only; read
    //! by cleanUp() on the same thread: anything but a complete file is
    //! removed there (abort, failure, cancel before run()).
    bool                mSucceeded = false;
    std::atomic<bool>   mDiscard { false };
};

#endif // TTPLAYBACKMUXTASK_H
