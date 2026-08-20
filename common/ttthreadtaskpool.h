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
    void  startNested(TTThreadTask* task);
    int   overallPercentage();

    //! Reason the last task in this run failed, or empty if none did (clean
    //! run, or plain user cancel). A failure and a cancel both end a task in
    //! aborted(), so the operation's owner needs this to tell them apart and
    //! to have a text to show. Reset by init(), i.e. once per operation.
    QString lastFailureMessage() const { return mLastFailureMessage; }

    //! True once every task from the current batch has been removed from
    //! the queue - the same condition onThreadTaskFinished() checks to
    //! decide whether to emit exit(). A caller whose own per-task completion
    //! handler can be delayed (e.g. behind a modal dialog) needs this to
    //! tell whether exit() - and any bookkeeping hung off it - has ALREADY
    //! run for this batch by the time the handler gets back to it (see
    //! TTAVData::onOpenVideoFinished()).
    bool isDrained() const { return mTaskQueue.isEmpty(); }

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
		void onThreadTaskDestroyed(QObject* task);
    void onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

	private:
		void cleanUpQueue();
    int  runningTaskCount();

	private:
    QQueue<TTThreadTask*> mTaskQueue;
    QString               mLastFailureMessage;
    QMap<QUuid, quint64>  mTotalMap;
    QMap<QUuid, quint64>  mProgressMap;
    TTMessageLogger*      log;
    quint64               mOverallTotalSteps;
    quint64               mOverallStepCount;
    //quint64               mCompletedStepCount;
    int                   mEstimateTaskCount;
    double                mCompleted; 
};
#endif
