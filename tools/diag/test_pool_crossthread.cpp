/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Regression harness for concurrent access to TTThreadTaskPool::mTaskQueue.
//
// TTThreadTaskPool lives in the GUI thread and its queue is a plain QList with
// no lock. Two call sites reach start() from a WORKER thread instead:
//
//   data/ttcutvideotask.cpp:114     threadTaskPool()->start(mpCutTask, true)
//   data/ttcutpreviewtask.cpp:215   threadTaskPool()->start(cutVideoTask, true)
//   data/ttcutpreviewtask.cpp:232   threadTaskPool()->start(cutSubtitleTask, true)
//
// Both of those tasks were themselves started asynchronously, so their
// operation() runs in a pool thread. In the real cut (data/ttavdata.cpp doCut)
// the GUI thread keeps enqueuing subtitle tasks after cutVideoTask is already
// running - two threads calling enqueue() on the same QList.
//
// This harness reproduces that shape: an outer task runs in a worker thread and
// enqueues inner tasks through the pool, while the main thread enqueues its own
// tasks and pumps the event loop (which is what delivers the queued
// finished/aborted slots that call mTaskQueue.removeAll()).
//
// The mode argument picks which of the two the outer task uses:
//   nested (default) - startNested(), the shape after the fix -> no report
//   queued           - start(task, true), the shape before it  -> races + SEGV
// The old shape is kept so the evidence stays reproducible. Note that start()
// now carries a Q_ASSERT on the calling thread, so "queued" only reaches the
// race in a build with NDEBUG or with assertions non-fatal.
//
// usage: test_pool_crossthread [nested|queued] [innerCount] [mainCount]
//
// Built by hand, not part of TOOLS - it needs a ThreadSanitizer build of the
// sources rather than the project's prebuilt objects. See the comment at the
// bottom. Without -fsanitize=thread the run is meaningless: the race is benign
// often enough that a plain run usually passes.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <cstdio>
#include <cstdlib>

#include "common/ttthreadtask.h"
#include "common/ttthreadtaskpool.h"
#include "common/istatusreporter.h"

// A trivial task, the stand-in for TTCutTask / TTCutSubtitleTask.
class LeafTask : public TTThreadTask
{
public:
    explicit LeafTask(const QString& name) : TTThreadTask(name) {}

protected:
    void operation() override { QThread::msleep(2); }
    void cleanUp() override {}

public:
    void onUserAbort() override { mIsAborted = true; }
};

// The stand-in for TTCutVideoTask: runs in a worker thread and drives the pool
// from there, exactly as ttcutvideotask.cpp:114 does for every cut of a list.
class OuterTask : public TTThreadTask
{
public:
    OuterTask(TTThreadTaskPool* pool, int innerCount, bool useNested)
      : TTThreadTask("OuterTask"), mPool(pool), mInnerCount(innerCount),
        mUseNested(useNested), mInner("InnerTask") {}

protected:
    void operation() override
    {
        // One long-lived embedded task, re-run per iteration - the lifetime
        // TTCutVideoTask gives its mpCutTask. A per-iteration object would
        // die before its queued finished/statusReport signals are delivered
        // and drown the run in use-after-free reports that say nothing about
        // the queue.
        for (int i = 0; i < mInnerCount; ++i) {
            if (mUseNested)
                mPool->startNested(&mInner);     // queue untouched
            else
                mPool->start(&mInner, true);     // synchronous, but enqueued first
        }
    }
    void cleanUp() override {}

public:
    void onUserAbort() override { mIsAborted = true; }

private:
    TTThreadTaskPool* mPool;
    int               mInnerCount;
    bool              mUseNested;
    LeafTask          mInner;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString mode  = (argc > 1) ? QString::fromLatin1(argv[1]) : QStringLiteral("nested");
    const int innerCount = (argc > 2) ? atoi(argv[2]) : 200;
    const int outerCount = (argc > 3) ? atoi(argv[3]) : 200;

    if (mode != QLatin1String("nested") && mode != QLatin1String("queued")) {
        fprintf(stderr, "usage: %s [nested|queued] [innerCount] [mainCount]\n", argv[0]);
        return 2;
    }
    printf("mode=%s inner=%d main=%d\n", qPrintable(mode), innerCount, outerCount);

    TTThreadTaskPool pool;
    pool.init(4);

    OuterTask outer(&pool, innerCount, mode == QLatin1String("nested"));
    pool.start(&outer);            // asynchronous - runs in a pool thread

    // The GUI thread's own share of the work: doCut() keeps enqueuing subtitle
    // tasks while cutVideoTask is already running, and the event loop delivers
    // the queued finished/aborted slots that remove entries from the queue.
    for (int i = 0; i < outerCount; ++i) {
        LeafTask* t = new LeafTask(QString("Main%1").arg(i));
        QObject::connect(t, &TTThreadTask::finished, t, &QObject::deleteLater);
        QObject::connect(t, &TTThreadTask::aborted,  t, &QObject::deleteLater);
        pool.start(t);
        app.processEvents();
    }

    QElapsedTimer t; t.start();
    while (t.elapsed() < 5000 && outer.isRunning()) {
        app.processEvents();
        QThread::msleep(5);
    }
    QThreadPool::globalInstance()->waitForDone();
    app.processEvents();

    printf("done (no ThreadSanitizer report above means no race was observed "
           "in this run)\n");
    return 0;
}

// Build (from tools/diag), compiling the sources rather than linking the
// project's objects so ThreadSanitizer covers them too:
//
//   g++ -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIC -std=gnu++17 \
//       -I../.. -I../../moc -I../../ui_h \
//       $(pkg-config --cflags Qt5Core Qt5Widgets) \
//       -o test_pool_crossthread test_pool_crossthread.cpp \
//       ../../common/ttthreadtask.cpp ../../common/ttthreadtaskpool.cpp \
//       ../../common/ttmessagelogger.cpp ../../common/ttexception.cpp \
//       ../../common/ttsettings.cpp ../../common/istatusreporter.cpp \
//       ../../moc/moc_ttthreadtask.cpp ../../moc/moc_ttthreadtaskpool.cpp \
//       ../../moc/moc_ttsettings.cpp ../../moc/moc_istatusreporter.cpp \
//       $(pkg-config --libs Qt5Core) -lpthread
//
// Qt5Widgets cflags and -I../../ui_h are needed because
// common/ttthreadtask.h reaches gui/ttprogressbar.h -> QDialog ->
// ui_ttprogressform.h.
