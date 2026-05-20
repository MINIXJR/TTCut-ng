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
#include <QElapsedTimer>
#include <QUuid>

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
  int elapsedTime() const;
  quint64 totalSteps() const;
  quint64 stepCount() const;
  int processValue() const;
  bool isRunning() const;
  bool isAborted() const;
  void setIsRunning(bool value);

protected:
  virtual void operation() = 0;
  virtual void cleanUp() = 0;
  virtual void abort();

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
  QElapsedTimer mTaskTime; /**<timer started when task started */
  quint64 mTotalSteps; /**<estimate count of total task steps         */
  quint64 mStepCount; /**<current step count                         */
  TTMessageLogger* log; /**<message logger istance                     */
  QString mTaskName; /**<task name                                  */
  bool mIsSynchron;
  bool mIsRunning;
  bool mIsAborted;
};

#endif
