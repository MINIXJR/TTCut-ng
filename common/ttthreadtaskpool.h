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


#ifndef TTTHREADTASKPOOL_H
#define TTTHREADTASKPOOL_H

#include <QObject>
#include <QTime>
#include <QQueue>
#include <QMap>
#include <QUuid>

class TTMessageLogger;
class TTThreadTask;

//TODO: rename to TTThreadTaskManager
//! Class to manage various thread tasks
class TTThreadTaskPool : public QObject
{
	Q_OBJECT

	public:
		TTThreadTaskPool();
    ~TTThreadTaskPool();

    void  init(int estimateTaskCount);
		void  start(TTThreadTask* task, bool runSyncron=false, int priority=0);
    int   overallPercentage();
    QTime overallTime();

  signals:
    void init();
    void aborted();
    void exit();
    void statusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

  public slots:
		void onUserAbortRequest();

	private slots:
	  void onThreadTaskStarted(TTThreadTask* task);
		void onThreadTaskFinished(TTThreadTask* task);
		void onThreadTaskAborted(TTThreadTask* task);
    void onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

	private:
		void cleanUpQueue();
    int  runningTaskCount();

	private:
    QQueue<TTThreadTask*> mTaskQueue;
    QMap<QUuid, quint64>  mTotalMap;
    QMap<QUuid, quint64>  mProgressMap;
    TTMessageLogger*      log;
    QTime                 mOverallTotalTime;
    quint64               mOverallTotalSteps;
    quint64               mOverallStepCount;
    //quint64               mCompletedStepCount;
    int                   mEstimateTaskCount;
    double                mCompleted; 
};
#endif
