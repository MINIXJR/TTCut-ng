// Regression harness for the teardown order in TTThreadTask::run().
//
// run() used to emit its terminal signal (finished/aborted) BEFORE calling the
// virtual cleanUp(). Every owner of a task connects that signal to
// deleteLater, so the GUI thread could destroy the task while the worker
// thread was still about to dispatch cleanUp() through the object's vptr.
//
// The harness makes the window wide instead of narrow: cleanUp() sleeps, so
// the GUI thread has ample time to run deleteLater and the following
// DeferredDelete event. Build with -fsanitize=address and the wrong order
// reports a heap-use-after-free; the right order runs clean.
//
// Two more cases, for TTThreadTask::abort() (2026-10-16, found by cancelling
// a video open under AddressSanitizer). abort() took "not running, not
// aborted, not finished" for "never started" and then emitted aborted() and
// ran cleanUp() itself. Two moments look like that and are not:
//   end     run() has set mIsRunning = false and is inside cleanUp(), before
//           mIsFinished - abort() ran cleanUp() a second time (double free in
//           TTOpenSubtitleTask::cleanUp) and reported a finished task aborted;
//   queued  the task waits in QThreadPool's queue - abort() reported it, the
//           owner deleted it, and QThreadPool still ran it later.
// The same holds for a task QThreadPool has just started, before run() sets
// mIsRunning (TTOpenVideoTask::cleanUp freed mpVideoType under operation());
// that window cannot be held open from outside, the fix closes it the same
// way as "queued".
//
// usage: test_task_cleanup_order [order [repetitions] | end | queued]
//        (no argument: all three)
// Built by hand, not part of TOOLS - it needs an ASAN build of the sources
// rather than the project's prebuilt objects. See the comment at the bottom.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThreadPool>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <functional>

#include "common/ttthreadtask.h"
#include "common/ttthreadtaskpool.h"

static int  gCleanUpAfterDestroy = 0;
static bool gDestroyed           = false;

class SlowCleanupTask : public TTThreadTask
{
    Q_OBJECT

public:
    SlowCleanupTask() : TTThreadTask("SlowCleanupTask") {}

    ~SlowCleanupTask() override
    {
        mAlive = 0xDEAD;
        gDestroyed = true;
    }

protected:
    void operation() override
    {
        // Nothing to do - the interesting part is what happens after.
        mAlive = 0xA11AEu;
    }

    // Reading a member here is exactly what any real cleanUp() does. With the
    // wrong order the object may already be gone by the time we get here, and
    // the virtual dispatch itself already read a freed vptr.
    void cleanUp() override
    {
        QThread::msleep(80);
        if (mAlive != 0xA11AEu) gCleanUpAfterDestroy++;
    }

public slots:
    void onUserAbort() override { mIsAborted = true; }

private:
    volatile unsigned mAlive = 0;
};

// What a task did, counted outside the object: it may be gone when we look.
struct TaskLog
{
    std::atomic<int> operations{0};
    std::atomic<int> cleanUps{0};
    std::atomic<int> finished{0};
    std::atomic<int> aborted{0};
    std::atomic<bool> inOperation{false};
    std::atomic<bool> inCleanUp{false};
    QSemaphore releaseOperation;   // operation() waits here when holdOperation
    QSemaphore releaseCleanUp;     // the FIRST cleanUp() waits here when holdCleanUp
    bool holdOperation = false;
    bool holdCleanUp   = false;
};

// A task as the application has them: onUserAbort() goes through abort().
class GateTask : public TTThreadTask
{
    Q_OBJECT

public:
    explicit GateTask(TaskLog& log) : TTThreadTask("GateTask"), mLog(log) {}

protected:
    void operation() override
    {
        mLog.operations++;
        mLog.inOperation = true;
        if (mLog.holdOperation) mLog.releaseOperation.acquire();
    }
    void cleanUp() override
    {
        const int n = ++mLog.cleanUps;
        mLog.inCleanUp = true;
        if (n == 1 && mLog.holdCleanUp) mLog.releaseCleanUp.acquire();
    }

public slots:
    void onUserAbort() override { abort(); }

private:
    TaskLog& mLog;
};

static void pumpUntil(QCoreApplication& app, const std::function<bool()>& done, int ms = 5000)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms && !done()) { app.processEvents(); QThread::msleep(2); }
}

static GateTask* startWatched(TTThreadTaskPool& pool, TaskLog& log)
{
    auto* task = new GateTask(log);
    // Counted directly, then the wiring every owner in the application uses.
    QObject::connect(task, &TTThreadTask::finished, [&log] { log.finished++; });
    QObject::connect(task, &TTThreadTask::aborted,  [&log] { log.aborted++; });
    QObject::connect(task, &TTThreadTask::finished, task, &QObject::deleteLater);
    QObject::connect(task, &TTThreadTask::aborted,  task, &QObject::deleteLater);
    pool.start(task);
    return task;
}

// A cancel while run() is in cleanUp(), after operation() returned.
static int caseEnd(QCoreApplication& app)
{
    TTThreadTaskPool pool;
    TaskLog log;
    log.holdCleanUp = true;
    GateTask* task = startWatched(pool, log);
    pumpUntil(app, [&] { return log.inCleanUp.load(); });
    if (!log.inCleanUp) { printf("FAIL end: the task never reached cleanUp()\n"); return 1; }
    task->onUserAbort();
    log.releaseCleanUp.release();
    pumpUntil(app, [&] { return log.finished + log.aborted > 0; });
    QThreadPool::globalInstance()->waitForDone();
    app.processEvents();
    const bool ok = log.cleanUps == 1 && log.finished == 1 && log.aborted == 0;
    printf("%s end: cancel during cleanUp() - cleanUp %d, finished %d, aborted %d "
           "(expected 1, 1, 0)\n", ok ? "PASS" : "FAIL",
           log.cleanUps.load(), log.finished.load(), log.aborted.load());
    return ok ? 0 : 1;
}

// A cancel while the task still waits in QThreadPool's queue.
static int caseQueued(QCoreApplication& app)
{
    QThreadPool* qtp = QThreadPool::globalInstance();
    const int threads = qtp->maxThreadCount();
    qtp->setMaxThreadCount(1);
    TTThreadTaskPool pool;
    TaskLog blockerLog, log;
    blockerLog.holdOperation = true;
    startWatched(pool, blockerLog);
    pumpUntil(app, [&] { return blockerLog.inOperation.load(); });
    GateTask* task = startWatched(pool, log);     // queued behind the blocker
    task->onUserAbort();
    pumpUntil(app, [&] { return log.aborted > 0; }, 1000);   // the owner's deleteLater runs here
    blockerLog.releaseOperation.release();
    qtp->waitForDone();
    app.processEvents();
    qtp->setMaxThreadCount(threads);
    const bool ok = log.operations == 0 && log.cleanUps == 1 && log.aborted == 1 && log.finished == 0;
    printf("%s queued: cancel before QThreadPool started it - operation %d, cleanUp %d, "
           "aborted %d, finished %d (expected 0, 1, 1, 0)\n", ok ? "PASS" : "FAIL",
           log.operations.load(), log.cleanUps.load(), log.aborted.load(), log.finished.load());
    return ok ? 0 : 1;
}

static int caseOrder(QCoreApplication& app, int reps)
{
    TTThreadTaskPool pool;
    int survived = 0;

    for (int i = 0; i < reps; ++i) {
        gDestroyed = false;

        auto* task = new SlowCleanupTask();
        // The wiring every owner in the application uses.
        QObject::connect(task, &TTThreadTask::finished, task, &QObject::deleteLater);

        pool.start(task);

        // Pump the event loop until the task has been destroyed and its
        // worker thread has had time to finish cleanUp().
        QElapsedTimer t; t.start();
        while (t.elapsed() < 2000) {
            app.processEvents();
            QThread::msleep(5);
            if (gDestroyed && t.elapsed() > 400) break;
        }
        QThreadPool::globalInstance()->waitForDone();
        app.processEvents();
        survived++;
        printf("run %d: destroyed=%s cleanUp-after-destroy=%d\n",
               i + 1, gDestroyed ? "yes" : "no", gCleanUpAfterDestroy);
    }

    printf("%s order: %d runs, %d cleanUp calls on a destroyed object\n",
           gCleanUpAfterDestroy == 0 ? "PASS" : "FAIL",
           survived, gCleanUpAfterDestroy);
    return gCleanUpAfterDestroy == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = (argc > 1) ? argv[1] : QString();
    if (mode == "order")  return caseOrder(app, (argc > 2) ? atoi(argv[2]) : 5);
    if (mode == "end")    return caseEnd(app);
    if (mode == "queued") return caseQueued(app);
    if (!mode.isEmpty()) { fprintf(stderr, "usage: %s [order [reps] | end | queued]\n", argv[0]); return 2; }
    const int failed = caseOrder(app, 5) + caseEnd(app) + caseQueued(app);
    printf(failed == 0 ? "ALL PASS\n" : "%d CASE(S) FAILED\n", failed);
    return failed == 0 ? 0 : 1;
}

// Build (from tools/diag), compiling the sources rather than linking the
// project's objects so AddressSanitizer covers them too. Mocs are generated
// into a throwaway temp dir with Qt's own moc binary, same pattern as
// gate_pool_crossthread.sh:34-40 (MOC="$(pkg-config --variable=libexecdir
// Qt6Core)/moc" - Qt6 keeps moc under libexec, not bindir):
//
//   MOC="$(pkg-config --variable=libexecdir Qt6Core)/moc"
//   MOCDIR=$(mktemp -d)
//   for h in ttthreadtask ttthreadtaskpool ttsettings istatusreporter; do
//     "$MOC" ../../common/$h.h -o "$MOCDIR/moc_$h.cpp"
//   done
//   "$MOC" test_task_cleanup_order.cpp -o "$MOCDIR/moc_test_task_cleanup_order.cpp"
//
//   g++ -g -O1 -fsanitize=address -fno-omit-frame-pointer -fPIC -std=gnu++17 \
//       -I../.. -I"$MOCDIR" $(pkg-config --cflags Qt6Core) \
//       -o test_task_cleanup_order test_task_cleanup_order.cpp \
//       ../../common/ttthreadtask.cpp ../../common/ttthreadtaskpool.cpp \
//       ../../common/ttmessagelogger.cpp ../../common/ttexception.cpp \
//       ../../common/ttsettings.cpp ../../common/istatusreporter.cpp \
//       "$MOCDIR"/moc_ttthreadtask.cpp "$MOCDIR"/moc_ttthreadtaskpool.cpp \
//       "$MOCDIR"/moc_ttsettings.cpp "$MOCDIR"/moc_istatusreporter.cpp \
//       $(pkg-config --libs Qt6Core) -lpthread
//
// Do NOT also list moc_test_task_cleanup_order.cpp as a separate g++ input:
// the #include at the bottom of this file already pulls it in (its
// SlowCleanupTask class is declared right here, in this translation unit -
// compiling the moc output standalone as a second TU fails to see it).
// -I"$MOCDIR" is what lets that #include find it.

#include "moc_test_task_cleanup_order.cpp"
