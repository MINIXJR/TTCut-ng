/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Regression harness for TTThreadTaskPool::onUserAbortRequest().
//
// The pool walks mTaskQueue by index and calls qApp->processEvents() inside
// the loop. That delivers the queued finished/aborted signals, whose slots
// call mTaskQueue.removeAll() - mutating the very container being iterated -
// and it also runs the deferred deleteLater deletions for tasks the loop
// still holds raw pointers to. Cancelling an analysis therefore walks freed
// memory.
//
// This harness reproduces that headlessly: three tasks are enqueued with the
// same finished/aborted -> deleteLater wiring the main window uses, then the
// abort request is issued while they run.
//
// usage: test_pool_abort [taskCount] [taskMillis]
// Exit 0 = survived (bug fixed), non-zero / crash = bug present.
//
// Build via `make test_pool_abort` in tools/diag.
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

#include "common/ttthreadtask.h"
#include "common/ttthreadtaskpool.h"
#include "common/istatusreporter.h"

// A task that reports progress for a while and honours the abort flag.
// No Q_OBJECT: the pool calls onUserAbort() directly through the vtable and
// this harness declares no new signals or slots, so no moc pass is needed.
class SleepTask : public TTThreadTask
{
public:
    SleepTask(const QString& name, int millis)
      : TTThreadTask(name), mMillis(millis) {}

protected:
    void operation() override
    {
        onStatusReport(StatusReportArgs::Start, "working", 10);
        for (int i = 0; i < 10 && !mIsAborted; ++i) {
            QThread::msleep(mMillis / 10);
            onStatusReport(StatusReportArgs::Step, "step", i);
        }
        onStatusReport(StatusReportArgs::Finished, "done", 10);
    }

    void cleanUp() override {}

public:
    void onUserAbort() override { mIsAborted = true; }

private:
    int mMillis;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const int taskCount  = (argc > 1) ? atoi(argv[1]) : 3;
    const int taskMillis = (argc > 2) ? atoi(argv[2]) : 2000;

    TTThreadTaskPool pool;

    for (int i = 0; i < taskCount; ++i) {
        SleepTask* t = new SleepTask(QString("SleepTask%1").arg(i), taskMillis);
        // Exactly the wiring TTCutMainWindow uses for the stream-point tasks.
        QObject::connect(t, &TTThreadTask::finished, t, &QObject::deleteLater);
        QObject::connect(t, &TTThreadTask::aborted,  t, &QObject::deleteLater);
        pool.start(t);
    }

    printf("started %d tasks, letting them run...\n", taskCount);
    fflush(stdout);

    // Let the tasks get going and let some queued signals pile up.
    QThread::msleep(300);
    QCoreApplication::processEvents();

    printf("issuing abort request\n");
    fflush(stdout);

    pool.onUserAbortRequest();

    printf("abort request returned without crashing\n");
    fflush(stdout);

    // A second request straight away. The user can hit Cancel again, and
    // TTThreadTask::abort() re-enters the event loop, so onUserAbortRequest()
    // can be called while the first one is still unwinding. The queue has been
    // mutated by the queued finished/aborted signals in between.
    pool.onUserAbortRequest();

    printf("second abort request returned without crashing\n");
    fflush(stdout);

    // Drain: give the pool time to finish unwinding.
    for (int i = 0; i < 40; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }

    // Once more on the drained queue, then let ~TTThreadTaskPool run
    // cleanUpQueue() - that is where the dangling pointers used to be
    // dereferenced.
    pool.onUserAbortRequest();

    printf("PASS: pool survived the abort request\n");
    return 0;
}
