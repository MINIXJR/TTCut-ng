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

#include "../gui/ttprogressbar.h"

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
 * Elapsed time since task was started in msecs
 */
int TTThreadTask::elapsedTime() const
{
  return (mTaskTime.elapsed() <= 8640000) ? mTaskTime.elapsed() : 0;
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
 */
void TTThreadTask::run()
{
 mTaskTime.start();

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
    emit finished(this);
    cleanUp();
  }
  catch(const TTAbortException&)
  {
    qDebug() << taskName() << " with UUID " << taskID() << " catched TTAbortException";
    mIsRunning = false;
    emit aborted(this);
    cleanUp();

    if (mIsSynchron) {
      qDebug() << taskName() << " with UUID " << taskID() << " redirect TTAbortException";
      throw;
    }
  }
  catch(const TTException&)
  {
    qDebug() << taskName() << "with UUID " << taskID() << " catched TTException";
    mIsRunning = false;
    emit aborted(this);
    cleanUp();
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
