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
// usage: test_task_cleanup_order [repetitions]
// Built by hand, not part of TOOLS - it needs an ASAN build of the sources
// rather than the project's prebuilt objects. See the comment at the bottom.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const int reps = (argc > 1) ? atoi(argv[1]) : 5;

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

    printf("%s (%d runs, %d cleanUp calls on a destroyed object)\n",
           gCleanUpAfterDestroy == 0 ? "PASS" : "FAIL",
           survived, gCleanUpAfterDestroy);
    return gCleanUpAfterDestroy == 0 ? 0 : 1;
}

// Build (from tools/diag), compiling the sources rather than linking the
// project's objects so AddressSanitizer covers them too. Mocs are generated
// into a throwaway temp dir with Qt's own moc binary, same pattern as
// gate_pool_crossthread.sh:28-33 (MOC="$(pkg-config --variable=host_bins
// Qt5Core)/moc"):
//
//   MOC="$(pkg-config --variable=host_bins Qt5Core)/moc"
//   MOCDIR=$(mktemp -d)
//   for h in ttthreadtask ttthreadtaskpool ttsettings istatusreporter; do
//     "$MOC" ../../common/$h.h -o "$MOCDIR/moc_$h.cpp"
//   done
//   "$MOC" test_task_cleanup_order.cpp -o "$MOCDIR/moc_test_task_cleanup_order.cpp"
//
//   g++ -g -O1 -fsanitize=address -fno-omit-frame-pointer -fPIC -std=gnu++17 \
//       -I../.. -I"$MOCDIR" $(pkg-config --cflags Qt5Core) \
//       -o test_task_cleanup_order test_task_cleanup_order.cpp \
//       ../../common/ttthreadtask.cpp ../../common/ttthreadtaskpool.cpp \
//       ../../common/ttmessagelogger.cpp ../../common/ttexception.cpp \
//       ../../common/ttsettings.cpp ../../common/istatusreporter.cpp \
//       "$MOCDIR"/moc_ttthreadtask.cpp "$MOCDIR"/moc_ttthreadtaskpool.cpp \
//       "$MOCDIR"/moc_ttsettings.cpp "$MOCDIR"/moc_istatusreporter.cpp \
//       $(pkg-config --libs Qt5Core) -lpthread
//
// Do NOT also list moc_test_task_cleanup_order.cpp as a separate g++ input:
// the #include at the bottom of this file already pulls it in (its
// SlowCleanupTask class is declared right here, in this translation unit -
// compiling the moc output standalone as a second TU fails to see it).
// -I"$MOCDIR" is what lets that #include find it.

#include "moc_test_task_cleanup_order.cpp"
