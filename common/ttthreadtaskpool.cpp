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
// TTTHREADTASKPOOL
// ----------------------------------------------------------------------------


#include "ttthreadtaskpool.h"
#include "ttthreadtask.h"

#include "../common/ttmessagelogger.h"

#include <QThreadPool>
#include <QPointer>
#include <QThread>
#include <QDebug>

/**
 * Threadtaskpool constructor
 */
TTThreadTaskPool::TTThreadTaskPool() : QObject()
{
  // Keep threads alive for 30s between tasks (default 30000ms).
  // The previous value of 100ms caused thread thrashing for longer operations.
  QThreadPool::globalInstance()->setExpiryTimeout(30000);

  mOverallTotalSteps  = 0;
  mOverallStepCount   = 0;
  mEstimateTaskCount  = 1;
  mCompleted          = 0.0;
  mTotalMap.clear();
  mProgressMap.clear();

  log = TTMessageLogger::getInstance();
}

/**
 * Threadtaskpool destructor
 */
TTThreadTaskPool::~TTThreadTaskPool()
{
  cleanUpQueue();
}

/**
 * Initialize the task pool with the estimate number of tasks
 */
void TTThreadTaskPool::init(int estimateTaskCount)
{
  mEstimateTaskCount = estimateTaskCount;
  mTotalMap.clear();
  mProgressMap.clear();
  // Once per operation: a failure recorded here must never outlive the run it
  // belongs to, or the next cancelled operation would report the old reason.
  mLastFailureMessage.clear();

  // Ensure the thread pool has enough threads for all tasks to run in parallel
  // (1 video + N audio + M subtitle). This allows audio/subtitle cutting to
  // proceed concurrently with video cutting instead of waiting in the queue.
  QThreadPool* pool = QThreadPool::globalInstance();
  if (pool->maxThreadCount() < estimateTaskCount) {
    qDebug() << "TTThreadTaskPool: Raising maxThreadCount from" << pool->maxThreadCount()
             << "to" << estimateTaskCount << "for parallel cutting";
    pool->setMaxThreadCount(estimateTaskCount);
  }
}

/**
 * Remove all tasks from the pool
 */
void TTThreadTaskPool::cleanUpQueue()
{
  // Blocks the calling thread — which is the GUI thread when a dialog owning a
  // pool is destroyed (~TTQuickJumpDialog → ~TTThreadTaskPool). The wait is
  // deliberate: without it the pool dies while a running task can still signal
  // into it. Do NOT give it a deadline; an expired deadline trades a visible
  // freeze for an occasional crash.
  // What keeps it short is the caller aborting first (abortCurrentWorker before
  // delete mTaskPool) AND the abort reaching into a running decode
  // (TTFFmpegWrapper::setCancelToken). Before that existed, a single quick-jump
  // thumbnail could hold the GUI here for minutes (spec 2026-08-28).
  // This bounds only the dialog's own worker, though: the wait is on the
  // *global* pool, which every TTThreadTaskPool in the app shares, so an
  // unrelated task already running there (e.g. TTAudioAnomalyScanTask, which
  // auto-starts, has no cancel token, and is untouched by abortCurrentWorker)
  // still bounds this wait by its own remaining runtime.
  QThreadPool::globalInstance()->waitForDone();

  QMutableListIterator<TTThreadTask*> t(mTaskQueue);
  while (t.hasNext())
  {
    TTThreadTask* task = t.next();

    if (task == 0) continue;

    disconnect(task, &TTThreadTask::started,  this, &TTThreadTaskPool::onThreadTaskStarted);
    disconnect(task, &TTThreadTask::finished, this, &TTThreadTaskPool::onThreadTaskFinished);
    disconnect(task, &TTThreadTask::aborted,  this, &TTThreadTaskPool::onThreadTaskAborted);

    disconnect(task, &TTThreadTask::statusReport,
      this, &TTThreadTaskPool::onStatusReport);

    disconnect(task, &QObject::destroyed,
      this, &TTThreadTaskPool::onThreadTaskDestroyed);

    //qDebug() << "remove task " << task->taskName() << " with UUID " << task->taskID();
    t.remove();
  }

  mOverallTotalSteps  = 0;
  mOverallStepCount   = 0;
  mEstimateTaskCount  = 1;
  mCompleted          = 0.0;
  mTotalMap.clear();
  mProgressMap.clear();
}

void TTThreadTaskPool::wireTask(TTThreadTask* task)
{
  connect(task, &TTThreadTask::started,  this, &TTThreadTaskPool::onThreadTaskStarted);
  connect(task, &TTThreadTask::finished, this, &TTThreadTaskPool::onThreadTaskFinished);
  connect(task, &TTThreadTask::aborted,  this, &TTThreadTaskPool::onThreadTaskAborted);
  connect(task, &TTThreadTask::statusReport,
    this, &TTThreadTaskPool::onStatusReport);
}

void TTThreadTaskPool::unwireTask(TTThreadTask* task)
{
  disconnect(task, &TTThreadTask::started,  this, &TTThreadTaskPool::onThreadTaskStarted);
  disconnect(task, &TTThreadTask::finished, this, &TTThreadTaskPool::onThreadTaskFinished);
  disconnect(task, &TTThreadTask::aborted,  this, &TTThreadTaskPool::onThreadTaskAborted);
  disconnect(task, &TTThreadTask::statusReport,
    this, &TTThreadTaskPool::onStatusReport);
  mTaskQueue.removeAll(task);
}

/**
 * Threadtask has emitted start signal
 *
 * mTaskQueue is a plain QQueue with no lock, and this method reads and writes
 * it (runningTaskCount, contains, enqueue). It therefore belongs to the pool's
 * own thread - use startNested() to run a task from inside another one.
 * ThreadSanitizer on the pre-`startNested` code reported data races on the
 * queue's QListData and a SEGV in runningTaskCount(), reading a pointer out of
 * the buffer another thread had just reallocated
 * (tools/diag/test_pool_crossthread).
 */
void TTThreadTaskPool::start(TTThreadTask* task, bool runSyncron, int priority)
{
  Q_ASSERT(thread() == QThread::currentThread());

  wireTask(task);

  // Safety net for the task lifetime. The pool does not own the tasks; their
  // owners are free to delete them (the main window wires finished/aborted to
  // deleteLater). Without this the queue can end up holding a pointer to an
  // already destroyed task and the next traversal walks freed memory.
  // The connection is deliberately direct: the pointer has to leave the queue
  // while the destructor is running, not whenever the event loop gets around
  // to it. It stays in place until the task really dies - unique, because
  // some tasks (TTCutTask) are re-started for every cut of a cut list.
  connect(task, &QObject::destroyed,
    this, &TTThreadTaskPool::onThreadTaskDestroyed,
    Qt::ConnectionType(Qt::DirectConnection | Qt::UniqueConnection));

  if (runningTaskCount() == 0)
  {
    emit init();
  }

  if (!mTaskQueue.contains(task))
    mTaskQueue.enqueue(task);

  //log->debugMsg(__FILE__, __LINE__, QString("enqueue task %1, current task count %2").
  //    arg(task->taskName()).
  //    arg(mTaskQueue.count()));
  qDebug() << "enqueue task " << (runSyncron ? "(synchron) " : "(asynchron)" ) << task->taskName() << " with UUID " << task->taskID();


  if (runSyncron)
    task->runSynchron();
  else
    QThreadPool::globalInstance()->start(task, priority);
}

/**
 * Run an embedded task synchronously, from inside a task that is already
 * running.
 *
 * Same wiring as start(), minus everything that touches mTaskQueue. The
 * callers (TTCutVideoTask for every cut of a list, TTCutPreviewTask for the
 * per-clip video and subtitle cut) run in a pool thread, while the GUI thread
 * keeps enqueuing tasks of its own and removes finished ones through the
 * queued finished/aborted slots - two threads on one unguarded QQueue.
 *
 * Connecting is safe from any thread, and the slots run in the pool's thread
 * because the task objects live there (Qt picks a queued connection when the
 * emitting thread differs from the receiver's).
 *
 * Nothing else changes for the caller:
 *   - start()'s `emit init()` only fires while no task is running, and the
 *     outer task always is, so it never fired for these calls anyway.
 *   - overallPercentage() feeds off mTotalMap/mProgressMap, which are filled
 *     through onStatusReport - not through the queue. The embedded task still
 *     reports progress, which matters because TTCutTask forwards
 *     TTVideoStream::statusReport (the fine-grained progress inside one cut).
 */
void TTThreadTaskPool::startNested(TTThreadTask* task)
{
  wireTask(task);

  // No destroyed() connection: that one only exists to take a dead task out of
  // the queue, and this task never enters it.

  qDebug() << "run nested task" << task->taskName() << "with UUID" << task->taskID();

  task->runSynchron();
}

/**
 * Threadtask emitted start signal
 */
void TTThreadTaskPool::onThreadTaskStarted(TTThreadTask* task)
{
  (void)task;
}

/**
 * Threadtask emitted finished signal
 */
void TTThreadTaskPool::onThreadTaskFinished(TTThreadTask* task)
{
  unwireTask(task);

  qDebug() << "finished " << task->taskName() << " with UUID " << task->taskID() << " remaining tasks " << mTaskQueue.count();

  if (mTaskQueue.isEmpty())
  {
    mOverallTotalSteps  = 0;
    mOverallStepCount   = 0;
    mEstimateTaskCount  = 1;
    mCompleted          = 0.0;
    mTotalMap.clear();
    mProgressMap.clear();
    emit exit();
  }
}

/**
 * Threadtask was successfully aborted
 */
void TTThreadTaskPool::onThreadTaskAborted(TTThreadTask* task)
{
  /*qDebug(qPrintable(QString("TTThreadTaskPool::Task %1 with uuid %2 aborted. IsRunning %3").
          arg(task->taskName()).
          arg(task->taskID()).
          arg(task->isRunning())));*/

  unwireTask(task);

  // Keep the reason, if there was one. aborted() carries no argument and says
  // only "this task did not finish" - a user cancel and a genuine failure look
  // identical from here on, and the operation's owner has to tell them apart
  // to decide between "Cut cancelled" and an error report. Only a non-empty
  // message overwrites: on a multi-task run the cancel that follows a failure
  // must not erase the failure's reason.
  if (!task->failureMessage().isEmpty())
    mLastFailureMessage = task->failureMessage();

  qDebug() << "aborted " << task->taskName() << " with UUID " << task->taskID() << " remaining tasks " << mTaskQueue.count();

  if (mTaskQueue.isEmpty())
  {
    qDebug() << "Last thread task aborted -> exit the thread queue!";
    mOverallTotalSteps  = 0;
    mOverallStepCount   = 0;
    mEstimateTaskCount  = 1;
    mCompleted          = 0.0;
    mTotalMap.clear();
    mProgressMap.clear();
    emit aborted();
    emit exit();
  }
}

/**
 * Threadtask was destroyed by its owner
 *
 * Last line of defence only: in the regular flow the task has already left the
 * queue via onThreadTaskFinished() or onThreadTaskAborted() and removeAll() is
 * a no-op here. The TTThreadTask part of the object is gone at this point, so
 * the pointer may only be compared, never dereferenced.
 */
void TTThreadTaskPool::onThreadTaskDestroyed(QObject* task)
{
  int removed = mTaskQueue.removeAll(static_cast<TTThreadTask*>(task));

  if (removed > 0)
    qDebug() << "task destroyed while still enqueued, removed from queue; remaining tasks " << mTaskQueue.count();
}

/**
 * Status reporting
 */
void TTThreadTaskPool::onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value)
{
  if (state == StatusReportArgs::Start)
  {
    qDebug() << task->taskID() << " total steps " << value;
    mTotalMap.insert(task->taskID(), value);
    mProgressMap.insert(task->taskID(), 0);
  }

  if (state == StatusReportArgs::Step)
  {
    // Clamp monotone per task: senders emit cumulative progress positions, so
    // a smaller value than what is already recorded can only be a stale Step
    // signal delivered late through the queued connection (observed as the
    // overall percentage flickering down, e.g. 55->56->55, when an
    // already-finished task's earlier Step arrives after its Finished).
    mProgressMap[task->taskID()] = qMax(mProgressMap.value(task->taskID(), 0), value);
  }

  if (state == StatusReportArgs::Finished)
  {
    qDebug() << task->taskID() << " finished " << value;
    // Mark this task as 100% complete
    if (mTotalMap.contains(task->taskID())) {
      mProgressMap[task->taskID()] = mTotalMap[task->taskID()];
    }
  }

  emit statusReport(task, state, msg, value);
}

/**
 * User request to abort all current operations
 */
void TTThreadTaskPool::onUserAbortRequest()
{
  //qDebug() << "-----------------------------------------------------";
  //qDebug() << "TTThreadTaskPool -> request to abort all tasks";

  // Never disconnect started/finished here. onThreadTaskFinished() is what
  // takes a task out of mTaskQueue; a task that ignores the abort flag and
  // completes normally would otherwise stay enqueued forever while its owner
  // deletes it - which leaves a dangling pointer in the queue.
  //
  // Work on a guarded snapshot instead of on mTaskQueue itself. onUserAbort()
  // may re-enter the event loop (TTThreadTask::abort() calls processEvents()),
  // and that both mutates mTaskQueue through the queued finished/aborted
  // signals and runs the deferred deleteLater of the tasks. The snapshot keeps
  // the traversal stable, the guarded pointers keep us from touching a task
  // that died while a sibling was being aborted.
  QList< QPointer<TTThreadTask> > abortList;

  for (int i = 0; i < mTaskQueue.count(); i++)
    abortList.append(QPointer<TTThreadTask>(mTaskQueue.at(i)));

  for (int i = 0; i < abortList.count(); i++)
  {
    TTThreadTask* task = abortList.at(i).data();

    if (task == 0) continue;

    //onStatusReport(task, StatusReportArgs::Step, "Aborting task...", 0);
    task->onUserAbort();
  }

  //qDebug() << "-----------------------------------------------------";
}

//! Calculate the total percentage progress value of all enqueued tasks

int TTThreadTaskPool::overallPercentage()
{
  // Sum progress and totals from all tracked tasks
  quint64 totalProgress = 0;
  quint64 totalSteps = 0;

  QMapIterator<QUuid, quint64> it(mTotalMap);
  while (it.hasNext()) {
    it.next();
    QUuid taskId = it.key();
    totalSteps += it.value();
    if (mProgressMap.contains(taskId)) {
      totalProgress += mProgressMap[taskId];
    }
  }

  if (totalSteps == 0)
    return 0;

  // Return percentage (0-100)
  return (int)((double)totalProgress / (double)totalSteps * 100.0);
}

/**
 * Returns the current running task count
 */
int TTThreadTaskPool::runningTaskCount()
{
  int runningCount = 0;

  for (int i = 0; i < mTaskQueue.count(); i++)
  {
    TTThreadTask* task = mTaskQueue.at(i);
    //if (task == 0) continue;
    if (task->isRunning()) runningCount++;
  }
  return runningCount;
}
