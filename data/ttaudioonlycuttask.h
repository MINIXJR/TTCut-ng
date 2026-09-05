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

#include "ttabortabletask.h"
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
class TTAudioOnlyCutTask : public TTAbortableTask
{
  Q_OBJECT

  public:
    TTAudioOnlyCutTask(TTAVData* avData, TTAVItem* avItem);
    void init(const TTAudioOnlyCutParams& params);

    // Results, valid after the pool's exit signal (worker done):
    //! Descriptive result text (mLastCutOutputSummary in TTAVData).
    QString      outputSummary() const { return mOutputSummary; }
    //! Empty on success and on a deliberate user abort (TTAbortException
    //! bypasses every assignment site below via abortNow()/abortIfRequested()
    //! - see runAudioCut()). Set to a translatable, reason-carrying text on a
    //! genuine failure: no track produced an output file, only some of the
    //! requested tracks did (the other tracks' individual errorMsg()s from
    //! TTAVData::cutAudioTracks() do not reach here on their own - this is
    //! what surfaces that they happened), or the MKA mux failed. The first
    //! two are mutually exclusive by construction; either can still be
    //! overwritten by the MKA-mux-failure text below it if that stage also
    //! fails (single field, most-recent genuine failure wins - same rule
    //! outputSummary() below already followed before this task). This field
    //! is what TTAVData::onAudioOnlyCutFinished() checks via isEmpty() to
    //! decide CutOutcome::Success vs. Failed.
    QString      lastError()     const { return mError; }
    //! Text of the closing Exit bracket, emitted by TTAVData::
    //! onAudioOnlyCutFinished(). Short and reason-agnostic ("Audio cut
    //! failed"/"Audio cut complete") by design - the reason-specific detail
    //! belongs in lastError() above, not here. All three failure branches in
    //! runAudioCut() (no track produced an output file, a partial-track
    //! failure, a failed MKA mux) set BOTH mError and mExitMessage together;
    //! the unconditional-looking assignment at the end of runAudioCut() is
    //! actually guarded (mExitMessage.isEmpty()) and only supplies the
    //! success text when none of those branches ran.
    QString      exitMessage()   const { return mExitMessage; }
    //! First requested track's drift samples, for TTAVData::
    //! cutAudioDriftCalculated - emitted by the GUI-side finish slot, not
    //! from here (the signal belongs to TTAVData).
    QList<float> drifts()        const { return mDrifts; }

  protected:
    void operation() override;

  public slots:
    void onUserAbort() override;

  private:
    //! The pipeline itself; operation() only wraps it in the abort funnel.
    void runAudioCut();

    //! The item whose audio streams the worker reads. Stays alive for the
    //! same reason documented on TTH26xCutTask::mpAVItem.
    TTAVItem*            mpAVItem;
    TTAudioOnlyCutParams mParams;
    QString              mError;
    QString              mExitMessage;
    QString              mOutputSummary;
    QList<float>         mDrifts;

    //! Member, not a local, so onUserAbort() (GUI thread) can reach it
    //! without a pointer race against the worker that creates and uses it.
    //! Same arrangement as TTH26xCutTask's two engines / TTMuxTask's
    //! provider. Only actually used for AOF_OriginalMKA, but requestAbort()
    //! on an otherwise-idle provider is harmless for the other formats.
    TTMkvMergeProvider mMkvProvider;
};

#endif
