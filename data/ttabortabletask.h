/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttabortabletask.h                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                       DATE: 09/05/2026 */
/*----------------------------------------------------------------------------*/

#ifndef TTABORTABLETASK_H
#define TTABORTABLETASK_H

#include "../common/ttthreadtask.h"

#include <QString>
#include <QStringList>
#include <atomic>

class TTAVData;

//! Base of the pool tasks that produce files and can be cancelled between
//! phases: TTH26xCutTask, TTAudioOnlyCutTask, TTMuxTask.
//!
//! It owns what those three had each spelled out on their own: the task's
//! own cancel flag, the list of files the run created, the abort funnel
//! (abortIfRequested / abortNow / abortCleanup) and the Step/Stage
//! forwarding to TTAVData. The subclasses keep what differs - the engines
//! that receive requestAbort(), the pipeline itself, and (TTMuxTask) a
//! cleanUp() that removes the files when the cancel arrived before run().
//!
//! The cancel flag is deliberately separate from the engines' own flags:
//! TTESSmartCut clears its request in initialize(), so a cancel arriving in
//! the window between task start and the end of initialize() would be lost
//! there - this flag keeps it and the next poll point acts on it. It is set
//! on the GUI thread (onUserAbort) and polled by the worker between phases.
class TTAbortableTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTAbortableTask(TTAVData* avData, const QString& name);

  protected:
    //! Set the cancel flag. Called first thing in the subclass's
    //! onUserAbort(), before the engines get their requestAbort() and
    //! TTThreadTask::abort() does its bookkeeping; only an atomic store, so
    //! it is safe on the GUI thread while operation() runs on the pool.
    void requestCancel();
    bool cancelRequested() const
        { return mCancelRequested.load(std::memory_order_relaxed); }
    //! Worker-side poll point: THROWS TTAbortException (via abortNow()) to
    //! leave operation() if a cancel arrived, otherwise returns.
    void abortIfRequested();
    //! Unconditional abort exit (cleanup + TTAbortException). Used where an
    //! engine already reported the cancel through its own false return.
    [[noreturn]] void abortNow();
    //! Delete everything this run created (abort only). Idempotent.
    void abortCleanup();
    //! Forward one Step report / a stage change to the GUI via TTAVData.
    void reportStep(const QString& msg, quint64 percent);
    void reportStage(int stage);
    //! Nothing to do after operation() by default: the abort path cleans up
    //! before it throws. TTMuxTask overrides this.
    void cleanUp() override {}

    TTAVData*   mpAVData;
    //! Every file this run produced, in creation order - the cleanup list
    //! for an aborted run. Registered unconditionally (success or not): a
    //! failed or cancelled step still leaves a partial file on disk.
    QStringList mCreatedFiles;

  private:
    std::atomic<bool> mCancelRequested { false };
};

#endif // TTABORTABLETASK_H
