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

#ifndef TTTHREADTASK_H
#define TTTHREADTASK_H

#include <QRunnable>
#include <QObject>
#include <QUuid>

#include <atomic>

#include "../common/istatusreporter.h"

class TTMessageLogger;

//! Baseclass for all task runables

class TTThreadTask : public QObject, public QRunnable
{
  Q_OBJECT

public:
  TTThreadTask(QString name);
  virtual ~TTThreadTask();

  void run();
  void runSynchron();
  QString taskName() const;
  QUuid taskID() const;
  bool isRunning() const;
  bool isAborted() const;

  //! Reason this task failed, or empty for a clean run / plain user cancel.
  //! Both outcomes end in aborted(), which on its own says only "did not
  //! finish" - this is what lets the pool and the operation's owner tell a
  //! genuine failure from a cancel, and gives them a text to show.
  QString failureMessage() const { return mFailureMessage; }

protected:
  virtual void operation() = 0;
  virtual void cleanUp() = 0;
  virtual void abort();
  //! The end every failed or cancelled run shares (see run()): leave the
  //! running state, let cleanUp() release what operation() acquired, tell
  //! the pool.
  void finishAborted();

  //! Route a stream's statusReport(int, const QString&, quint64) into
  //! onStatusReport(), and undo it again from cleanUp(). Templates so that
  //! common/ needs no stream type: any QObject with that signal will do.
  template<class Source>
  void linkStatusSource(Source* source)
  {
    connect(source, &Source::statusReport,
            this,   qOverload<int, const QString&, quint64>(&TTThreadTask::onStatusReport));
  }
  template<class Source>
  void unlinkStatusSource(Source* source)
  {
    disconnect(source, &Source::statusReport,
               this,   qOverload<int, const QString&, quint64>(&TTThreadTask::onStatusReport));
  }

public slots:
  virtual void onUserAbort() = 0;

protected slots:
  virtual void onStatusReport(int state, const QString& msg, quint64 value);
  virtual void onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

signals:
  void started(TTThreadTask* task); /**<internal signal thread was started  */
  void finished(TTThreadTask* task); /**<internal signal thread has finished */
  void aborted(TTThreadTask* task); /**<internal signal thread was aborted  */

  void statusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

protected:
  QUuid mTaskID; /**<unique task ID                             */
  quint64 mTotalSteps; /**<estimate count of total task steps         */
  quint64 mStepCount; /**<current step count                         */
  TTMessageLogger* log; /**<message logger istance                     */
  QString mTaskName; /**<task name                                  */
  QString mFailureMessage; /**<why the task failed, empty if it did not  */
  bool mIsSynchron;
  bool mIsRunning;
  // Written by the GUI thread (onUserAbort), read by the worker thread — and,
  // since the decode loop checks it, read in a tight loop. A plain bool is a
  // data race there and the compiler may hoist the load out of the loop.
  std::atomic<bool> mIsAborted;
  /**<the task ran to completion; written by the worker thread just before
      finished() is emitted, read by abort() on the GUI thread. std::atomic
      because those are two different threads - mIsSynchron and mIsRunning
      above are plain bools that predate that concern and are left as they
      are. */
  std::atomic<bool> mIsFinished{false};
};

#endif
