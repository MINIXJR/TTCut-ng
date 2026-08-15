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
// TTTHREADTASK
// ----------------------------------------------------------------------------

#include "ttthreadtask.h"
#include "ttmessagelogger.h"
#include "ttexception.h"

#include <QCoreApplication>
#include <QThread>
#include <QDebug>

/**
 * Threadtask construtctor
 */
TTThreadTask::TTThreadTask(QString name) : QObject()
{
	setAutoDelete(false);

  mTaskName   = name;
	log         = TTMessageLogger::getInstance();
  mTaskID     = QUuid::createUuid();
  mTotalSteps = 0;
  mStepCount  = 0;
  mIsSynchron = false;
  mIsRunning  = false;
  mIsAborted  = false;
}

/**
 * Destructor
 */
TTThreadTask::~TTThreadTask()
{
}

/**
 * Returns the task name
 */
QString TTThreadTask::taskName() const
{
  return mTaskName;
}

/**
 * Returns the unique task ID
 */
QUuid TTThreadTask::taskID() const
{
  return mTaskID;
}

/**
 * Return the estimate number of total task steps
 */
/** 
 * Return the current step count
 */
/**
 * Returns true if task is in running state, otherwise false
 */
bool TTThreadTask::isRunning() const
{
  return mIsRunning;
}

/**
 * Returns true if the task is sheduled for aborting
 */
bool TTThreadTask::isAborted() const 
{
  return mIsAborted;
}

/**
 * Wrap status report signal and append reference to task
 */
void TTThreadTask::onStatusReport(int state, const QString& msg, quint64 value)
{
	onStatusReport(this, state, msg, value);
}

/**
 * Status report signal with current as task
 */
void TTThreadTask::onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value)
{
  if (state == StatusReportArgs::Start) {
    mStepCount  = 0;
    mTotalSteps = value;
  }

  if (state == StatusReportArgs::Step ||
  		state == StatusReportArgs::Finished)
    mStepCount = value;

  emit statusReport(task, state, msg, value);
}

/**
 * Task abort
 */
void TTThreadTask::abort()
{
  qDebug() << "Task " << taskName() << " with UUID " << taskID() << " get's abort request. Is running " << isRunning() << " is aborted " << mIsAborted;

  if (!mIsRunning && !mIsAborted) {
    emit aborted(this);
    qApp->processEvents();
    cleanUp();
  }

  mIsAborted = true;  
}

/** 
 * Run's the task operation synchronus
 */
void TTThreadTask::runSynchron()
{
  //qDebug() << "running task " << taskName() << " with uuid " << taskID() << " synchron";
  mIsSynchron = true;
  run();
}

/**
 * Runable run method
 *
 * cleanUp() runs BEFORE the terminal signal, in all three exits. Every owner
 * of a task connects finished/aborted to deleteLater, so emitting first hands
 * the GUI thread permission to destroy the object while this thread is still
 * about to dispatch the virtual cleanUp() through its vptr. Proven with
 * tools/diag/test_task_cleanup_order under AddressSanitizer: heap-use-after-free
 * in cleanUp(), freed by the DeferredDelete event on the GUI thread.
 *
 * The order is also the more sensible one on its own: no task's cleanUp()
 * needs the completion to have been announced, and several of them release
 * resources or disconnect signals that observers have no business seeing
 * afterwards.
 */
void TTThreadTask::run()
{
  try
  {
    if (mIsAborted) {
      qDebug() << taskName() << " entering running state while already aborted!";
      throw TTAbortException("Aborting operation!");
    }

     //qDebug() << "run task " << taskName() << " with uuid " << taskID();
    mIsRunning = true;
    emit started(this);

    operation();

    mIsRunning = false;
    //qDebug() << "emit finished for task " << taskName() << " with UUID " << taskID();
    cleanUp();
    emit finished(this);
  }
  catch(const TTAbortException&)
  {
    qDebug() << taskName() << " with UUID " << taskID() << " catched TTAbortException";
    mIsRunning = false;
    cleanUp();
    emit aborted(this);

    if (mIsSynchron) {
      qDebug() << taskName() << " with UUID " << taskID() << " redirect TTAbortException";
      throw;
    }
  }
  catch(const TTException& e)
  {
    // A real failure, NOT a user cancel - and the two must not become
    // indistinguishable here. Two things used to go missing at this point:
    //
    // 1. The exception's own message. The catch did not even bind it, so a
    //    reason like "Index 3500 exceeds list bounds: 3000" was dropped on
    //    the floor - it appeared in no log, no dialog, nowhere. An operation
    //    that failed for a nameable reason reported nothing at all.
    // 2. The failure itself, whenever this task ran synchronously. The
    //    TTAbortException branch above re-throws for mIsSynchron so the
    //    caller of runSynchron() sees the abort; this branch did not, so a
    //    nested task's failure never reached the task that started it.
    //    TTCutPreviewTask has caught and re-raised TTException around its
    //    nested cut since it was written - that catch could never fire.
    //    Consequence, measured on the MPEG-2 preview: every clip failed to be
    //    written, three errors were logged, and the preview still emitted
    //    finished() and reported "done".
    mFailureMessage = e.getMessage();
    TTMessageLogger::getInstance()->errorMsg(__FILE__, __LINE__,
        QString("%1 failed: %2").arg(taskName(), mFailureMessage));
    mIsRunning = false;
    cleanUp();
    emit aborted(this);

    if (mIsSynchron) {
      qDebug() << taskName() << " with UUID " << taskID() << " redirect TTException";
      throw;
    }
  }
}

/**
 * Returns the current task progress in percent (0-100)
 */
int TTThreadTask::processValue() const
{
  int value = (mTotalSteps > 0)
      ? (int)(((double)mStepCount / (double)mTotalSteps) * 100.0)
      : 0;

  return value;
}
