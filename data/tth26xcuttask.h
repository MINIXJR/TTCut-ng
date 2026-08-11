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
// TTH26XCUTTASK
// ----------------------------------------------------------------------------

#ifndef TTH26XCUTTASK_H
#define TTH26XCUTTASK_H

#include "../common/ttthreadtask.h"
#include "../avstream/ttdisplayordermap.h"
#include "../extern/ttessmartcut.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <atomic>

class TTAVData;
class TTAVItem;

//! Value bundle for the H.26x final cut. Everything derived from the cut list
//! is copied on the GUI thread before the task starts, so the worker never
//! dereferences a GUI-owned TTCutList.
struct TTH26xCutParams
{
  QString sourceFile;            // video ES path
  QString finalOutput;           // .mkv target (already normalized)
  QString tempVideoFile;         // _cut.<suffix> path
  double  frameRate  = 0.0;
  int     avOffsetMs = 0;
  bool    isH265     = false;
  bool    isPAFF     = false;
  int     paffLog2MaxFrameNum = 0;
  qint64  totalDurationMs = 0;   // for chapter generation (mLastCutResultMs)
  QList<QPair<int,int>>       cutFrames;  // display-order frame ranges
  QList<QPair<double,double>> keepList;   // seconds, extra-frame-corrected
  TTDisplayOrderMap           displayMap; // frame-granularity (PAFF-safe)
  bool    hasDisplayMap = false;
};

//! Pool task running the whole H.26x final cut pipeline
//! (Smart Cut video -> audio -> subtitles -> MKV mux) off the GUI thread.
class TTH26xCutTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTH26xCutTask(TTAVData* avData, TTAVItem* avItem);
    void init(const TTH26xCutParams& params);

    // Results, valid after the pool's exit signal (worker done):
    //! Empty on success; otherwise the text for TTAVData::mLastCutError.
    QString     lastError()    const { return mError; }
    //! Text of the closing Exit bracket. The synchronous predecessor used a
    //! shorter wording for the progress window than for the error dialog, so
    //! both strings have to travel back separately.
    QString     exitMessage()  const { return mExitMessage; }
    QString     finalOutput()  const { return mParams.finalOutput; }
    QStringList seamNotes()    const { return mSeamNotes; }

  protected:
    void cleanUp() override;
    void operation() override;

  public slots:
    void onUserAbort() override;

  private:
    //! The pipeline itself; operation() only wraps it in the abort funnel.
    void runCut();
    void reportStep(const QString& msg, quint64 percent);
    void reportStage(int stage);
    void fail(const QString& exitMessage, const QString& errorText);
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

    TTAVData*        mpAVData;
    //! The item whose streams the worker reads (audio/subtitle lists, the video
    //! stream's decodeToDisplayIndex). It stays alive because the only thing
    //! that destroys it is TTAVData::clear(), and its every caller goes through
    //! TTCutMainWindow::closeProject(), which does an unconditional
    //! QThreadPool::globalInstance()->waitForDone() before the clear - that
    //! drains this task too. The disabled main window only blocks the *menu*
    //! route; a window-manager close bypasses it and relies purely on the
    //! waitForDone. Removing or narrowing that wait turns this pointer into a
    //! use-after-free. (Same dependency as TTCutPreviewTask's stream access.)
    TTAVItem*        mpAVItem;
    TTH26xCutParams  mParams;
    QString          mError;
    QString          mExitMessage;
    QStringList      mSeamNotes;
    //! Every file this run produced, in creation order — the cleanup list for
    //! an aborted cut.
    QStringList      mCreatedFiles;

    //! The two engines are members, not locals, so onUserAbort() (GUI thread)
    //! can reach them without a pointer race against the worker that creates
    //! and destroys them. Both only receive an atomic store from that side.
    TTESSmartCut       mSmartCut;
    TTMkvMergeProvider mMkvProvider;

    //! Cancel flag of the task itself. Set by onUserAbort() on the GUI thread,
    //! polled by the worker between phases. It is deliberately separate from
    //! the engines' flags: TTESSmartCut clears its own request in
    //! initialize(), so a cancel arriving in the window between task start and
    //! the end of initialize() would be lost there — this flag keeps it and
    //! the next poll point acts on it.
    std::atomic<bool>  mCancelRequested { false };
};

#endif
