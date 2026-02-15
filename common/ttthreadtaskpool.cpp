/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttthreadtaskpool.cpp                                            */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 01/11/2009 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTTHREADTASKPOOL
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/


#include "ttthreadtaskpool.h"
#include "ttthreadtask.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttexception.h"
#include "../gui/ttprogressbar.h"

#include <QThreadPool>
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
  QThreadPool::globalInstance()->waitForDone();

  QMutableListIterator<TTThreadTask*> t(mTaskQueue);
  while (t.hasNext())
  {
    TTThreadTask* task = t.next();

    if (task == 0) continue;

    disconnect(task, SIGNAL(started(TTThreadTask*)),  this, SLOT(onThreadTaskStarted(TTThreadTask*)));
    disconnect(task, SIGNAL(finished(TTThreadTask*)), this, SLOT(onThreadTaskFinished(TTThreadTask*)));
    disconnect(task, SIGNAL(aborted(TTThreadTask*)),  this, SLOT(onThreadTaskAborted(TTThreadTask*)));

    disconnect(task, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
      this, SLOT(onStatusReport(TTThreadTask*, int, const QString&, quint64)));


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

/**
 * Threadtask has emitted start signal
 */
void TTThreadTaskPool::start(TTThreadTask* task, bool runSyncron, int priority)
{
  connect(task, SIGNAL(started(TTThreadTask*)),  this, SLOT(onThreadTaskStarted(TTThreadTask*)));
  connect(task, SIGNAL(finished(TTThreadTask*)), this, SLOT(onThreadTaskFinished(TTThreadTask*)));
  connect(task, SIGNAL(aborted(TTThreadTask*)),  this, SLOT(onThreadTaskAborted(TTThreadTask*)));

  connect(task, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
    this, SLOT(onStatusReport(TTThreadTask*, int, const QString&, quint64)));

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
  disconnect(task, SIGNAL(started(TTThreadTask*)),  this, SLOT(onThreadTaskStarted(TTThreadTask*)));
  disconnect(task, SIGNAL(finished(TTThreadTask*)), this, SLOT(onThreadTaskFinished(TTThreadTask*)));
  disconnect(task, SIGNAL(aborted(TTThreadTask*)),  this, SLOT(onThreadTaskAborted(TTThreadTask*)));

  disconnect(task, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
    this, SLOT(onStatusReport(TTThreadTask*, int, const QString&, quint64)));

  mTaskQueue.removeAll(task);

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

  disconnect(task, SIGNAL(started(TTThreadTask*)),  this, SLOT(onThreadTaskStarted(TTThreadTask*)));
  disconnect(task, SIGNAL(finished(TTThreadTask*)), this, SLOT(onThreadTaskFinished(TTThreadTask*)));
  disconnect(task, SIGNAL(aborted(TTThreadTask*)),  this, SLOT(onThreadTaskAborted(TTThreadTask*)));

  disconnect(task, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
    this, SLOT(onStatusReport(TTThreadTask*, int, const QString&, quint64)));

  mTaskQueue.removeAll(task);

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
    mProgressMap[task->taskID()] = value;
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

  for (int i = 0; i < mTaskQueue.count(); i++)
  {
    TTThreadTask* task = mTaskQueue.at(i);

    disconnect(task, SIGNAL(started(TTThreadTask*)),  this, SLOT(onThreadTaskStarted(TTThreadTask*)));
    disconnect(task, SIGNAL(finished(TTThreadTask*)), this, SLOT(onThreadTaskFinished(TTThreadTask*)));
    //disconnect(task, SIGNAL(aborted(TTThreadTask*)),  this, SLOT(onThreadTaskAborted(TTThreadTask*)));

    //disconnect(task, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
    //           this, SLOT(onStatusReport(TTThreadTask*, int, const QString&, quint64)));

    //onStatusReport(task, StatusReportArgs::Step, "Aborting task...", 0);
    task->onUserAbort();
    qApp->processEvents();
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

  // Return percentage in permille (0-1000 = 0-100%)
  return (int)((double)totalProgress / (double)totalSteps * 1000.0);
}

//! Calculate the total progress time value of all enqueued tasks

QTime TTThreadTaskPool::overallTime()
{
  mOverallTotalTime.setHMS(0, 0, 0, 0);
  int totalTimeMsecs = 0;

  for (int i = 0; i < mTaskQueue.count(); i++)
  {
    TTThreadTask* task = mTaskQueue.at(i);

    //if (task == 0) continue;

    totalTimeMsecs += task->elapsedTime();
  }

  return QTime(0, 0, 0, 0).addMSecs(totalTimeMsecs);
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
