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
// TTAUDIOONLYCUTTASK
// ----------------------------------------------------------------------------

#ifndef TTAUDIOONLYCUTTASK_H
#define TTAUDIOONLYCUTTASK_H

#include "../common/ttthreadtask.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <atomic>

class TTAVData;
class TTAVItem;

//! Value bundle for the audio-only cut. Everything derived from the cut list
//! is copied on the GUI thread before the task starts, so the worker never
//! dereferences a GUI-owned TTCutList. Mirrors TTH26xCutParams.
struct TTAudioOnlyCutParams
{
  QString targetFileName;                     // tgtFileName; feeds createCutFileName() and the .mka basename
  QList<QPair<double, double>> videoKeepList; // seconds, extra-frame-corrected (buildVideoKeepList)
  bool    normalizeAcmod  = false;
  int     audioOnlyFormat = 0;                // TTCut::AudioOnlyFormat, copied at dispatch time
  QString mkaOutputPath;                      // pre-computed .mka target (AOF_OriginalMKA only)
};

//! Pool task running the audio-only cut (per-track audio extraction, optional
//! MKA mux) off the GUI thread. No video is touched by this path.
class TTAudioOnlyCutTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTAudioOnlyCutTask(TTAVData* avData, TTAVItem* avItem);
    void init(const TTAudioOnlyCutParams& params);

    // Results, valid after the pool's exit signal (worker done):
    //! Descriptive result text (mLastCutOutputSummary in TTAVData).
    QString      outputSummary() const { return mOutputSummary; }
    //! Empty on every path this task takes today: the synchronous
    //! doAudioOnlyCut() it replaces never assigned TTAVData::mLastCutError,
    //! only mLastCutOutputSummary - that quirk is carried forward unchanged
    //! (see operation()). Kept for interface parity with the other cut tasks.
    QString      lastError()     const { return mError; }
    //! Text of the closing Exit bracket, emitted by TTAVData::
    //! onAudioOnlyCutFinished(). Unlike TTH26xCutTask, doAudioOnlyCut() used
    //! the SAME Exit text ("Audio cut complete") regardless of a sub-failure
    //! (e.g. a failed MKA mux) - only outputSummary() carries the failure
    //! detail then. Preserved as-is.
    QString      exitMessage()   const { return mExitMessage; }
    //! First requested track's drift samples, for TTAVData::
    //! cutAudioDriftCalculated - emitted by the GUI-side finish slot, not
    //! from here (the signal belongs to TTAVData).
    QList<float> drifts()        const { return mDrifts; }

  protected:
    void cleanUp() override;
    void operation() override;

  public slots:
    void onUserAbort() override;

  private:
    //! The pipeline itself; operation() only wraps it in the abort funnel.
    void runAudioCut();
    void reportStep(const QString& msg, quint64 percent);
    void reportStage(int stage);
    //! Worker-side poll point: THROWS TTAbortException (via abortNow()) to
    //! leave operation() if a cancel arrived, otherwise returns.
    void abortIfRequested();
    //! Unconditional abort exit (cleanup + TTAbortException). Used where an
    //! engine already reported the cancel through its own false return.
    [[noreturn]] void abortNow();
    //! Delete everything this run created (abort only).
    void abortCleanup();
    bool cancelRequested() const
        { return mCancelRequested.load(std::memory_order_relaxed); }

    TTAVData*            mpAVData;
    //! The item whose audio streams the worker reads. Stays alive for the
    //! same reason documented on TTH26xCutTask::mpAVItem.
    TTAVItem*            mpAVItem;
    TTAudioOnlyCutParams mParams;
    QString              mError;
    QString              mExitMessage;
    QString              mOutputSummary;
    QList<float>         mDrifts;
    //! Every file this run produced, in creation order - the cleanup list for
    //! an aborted cut. Registered unconditionally (success or not): a failed
    //! or cancelled track still leaves a partial file on disk.
    QStringList          mCreatedFiles;

    //! Member, not a local, so onUserAbort() (GUI thread) can reach it
    //! without a pointer race against the worker that creates and uses it.
    //! Same arrangement as TTH26xCutTask's two engines / TTMuxTask's
    //! provider. Only actually used for AOF_OriginalMKA, but requestAbort()
    //! on an otherwise-idle provider is harmless for the other formats.
    TTMkvMergeProvider mMkvProvider;

    //! Cancel flag of the task itself. Set by onUserAbort() on the GUI
    //! thread, polled by the worker between phases and forwarded into
    //! cutAudioTracks()'s own shouldAbort predicate.
    std::atomic<bool>  mCancelRequested { false };
};

#endif
