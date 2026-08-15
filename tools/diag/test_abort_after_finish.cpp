/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Does a cancel that arrives AFTER a task finished still report the task as
// aborted?
//
// TTThreadTask::abort() acts on two flags only:
//
//     if (!mIsRunning && !mIsAborted) { emit aborted(this); ...; cleanUp(); }
//
// and that one branch serves two states it cannot tell apart. "Never started"
// - the task still sits in the pool queue - genuinely needs the aborted()
// signal; TTCutMainWindow relies on it (a task aborted before the pool ran it
// emits aborted, never finished). "Already finished" looks identical from
// here: run() has set mIsRunning = false, called cleanUp() and emitted
// finished(this), which is sitting in the GUI thread's event queue. A cancel
// landing in that window adds an aborted() on top - and runs cleanUp() a
// second time, on the other thread.
//
// The window is opened deterministically here instead of being raced for: the
// main thread waits for the task's own "operation returned" flag WITHOUT
// processing events, so the queued finished() cannot be delivered yet, and
// only then issues the cancel.
//
// Two cases, and the second is the guard rail:
//   1. cancel after finish  - expected: finished×1, aborted×0, cleanUp×1
//   2. cancel before start  - expected: aborted×1 (must keep working)
//   3. cancel during a RE-RUN of the same task - expected: aborted×1
//
// Case 3 exists because a completion flag is the obvious fix and the obvious
// fix is wrong if it is never cleared: TTCutVideoTask re-starts one TTCutTask
// instance for every cut-list entry, so a flag left standing from entry 1
// would make every later cancel a no-op - a long cut would become unabortable
// again (1f372cca fixed exactly that once).
//
// usage: test_abort_after_finish
// Exit 0 = all cases as expected, 1 = case 1 reports a finished task as
// aborted (the defect), 2 = case 2 broke, 3 = case 3 broke (both mean a fix
// went too far).
#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <cstdio>

#include "common/istatusreporter.h"
#include "common/ttexception.h"
#include "common/ttthreadtask.h"
#include "common/ttthreadtaskpool.h"

// Returns from operation() immediately unless a duration is set, in which case
// it polls the abort flag the way the real tasks do. No Q_OBJECT: the harness
// declares no new signals or slots, so no moc pass is needed.
class QuickTask : public TTThreadTask
{
public:
  explicit QuickTask(const QString& name) : TTThreadTask(name) {}

  std::atomic<bool> operationReturned{false};
  std::atomic<int>  cleanUpCalls{0};
  std::atomic<int>  sleepMs{0};        // 0 = return immediately

protected:
  void operation() override
  {
    onStatusReport(StatusReportArgs::Start, "quick", 1);
    const int total = sleepMs.load();
    for (int slept = 0; slept < total; slept += 5) {
      if (mIsAborted) throw TTAbortException("aborted in operation");
      QThread::msleep(5);
    }
    onStatusReport(StatusReportArgs::Step,  "quick", 1);
    operationReturned.store(true);
  }

  // Counted, not just empty: the second call is half the defect.
  void cleanUp() override { cleanUpCalls.fetch_add(1); }

public:
  // The shape TTCutTask/TTMuxTask use: forward to the base bookkeeping.
  void onUserAbort() override { abort(); }
};

namespace {

struct Counts
{
  int finished = 0;
  int aborted  = 0;
};

// The receiver must be a QObject living on THIS thread, exactly as
// TTCutMainWindow is. A context-free lambda connects DIRECTLY and would count
// finished() on the worker thread the moment run() emits it - which hides the
// very window this harness is about: in the application that signal waits in
// the main thread's event queue until the loop gets to it.
void wire(QuickTask* task, QObject* receiver, Counts* c)
{
  QObject::connect(task, &TTThreadTask::finished, receiver,
                   [c](TTThreadTask*) { c->finished++; });
  QObject::connect(task, &TTThreadTask::aborted, receiver,
                   [c](TTThreadTask*) { c->aborted++; });
}

void drain(int rounds = 20)
{
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);

  int exitCode = 0;

  // ---- case 1: cancel arrives after the task finished ----------------------
  {
    TTThreadTaskPool pool;
    QObject receiver;                       // stands in for TTCutMainWindow
    QuickTask* task = new QuickTask("AfterFinish");
    Counts c;
    wire(task, &receiver, &c);

    pool.start(task);

    // Wait for the worker WITHOUT running the event loop. isRunning() goes
    // false in run() right before cleanUp() + emit finished(), so both the
    // flag and the emit have happened once this loop ends plus a margin -
    // while the delivery to this thread has not.
    for (int i = 0; i < 500 && !(task->operationReturned.load() && !task->isRunning()); ++i)
      QThread::msleep(5);
    QThread::msleep(100);

    printf("case 1 - before cancel : finished=%d aborted=%d cleanUp=%d"
           "  (finished emitted, delivery still queued)\n",
           c.finished, c.aborted, task->cleanUpCalls.load());

    task->onUserAbort();
    drain();

    const int cleanUps = task->cleanUpCalls.load();
    printf("case 1 - after cancel  : finished=%d aborted=%d cleanUp=%d\n",
           c.finished, c.aborted, cleanUps);

    const bool ok = (c.finished == 1 && c.aborted == 0 && cleanUps == 1);
    printf("case 1 - %s: a task that finished must not report itself aborted\n\n",
           ok ? "PASS" : "FAIL");
    if (!ok) exitCode = 1;

    delete task;
  }

  // ---- case 2: cancel arrives before the task ever ran ---------------------
  // Load-bearing: TTCutMainWindow wires aborted -> deleteLater + "search
  // finished(-1, aborted)" precisely for a task the pool never got to.
  {
    QObject receiver;
    QuickTask* task = new QuickTask("BeforeStart");
    Counts c;
    wire(task, &receiver, &c);

    task->onUserAbort();
    drain(5);

    printf("case 2 - never started : finished=%d aborted=%d cleanUp=%d\n",
           c.finished, c.aborted, task->cleanUpCalls.load());

    const bool ok = (c.aborted == 1 && c.finished == 0);
    printf("case 2 - %s: a task cancelled before it ran must still report aborted\n",
           ok ? "PASS" : "FAIL");
    if (!ok) exitCode = 2;

    delete task;
  }

  // ---- case 3: cancel during a re-run of the same task ---------------------
  {
    TTThreadTaskPool pool;
    QObject receiver;
    QuickTask* task = new QuickTask("ReRun");
    Counts c;
    wire(task, &receiver, &c);

    pool.start(task);                       // first run: completes
    drain();
    printf("case 3 - after run 1   : finished=%d aborted=%d\n", c.finished, c.aborted);

    task->operationReturned.store(false);
    task->sleepMs.store(2000);              // second run: long enough to cancel
    pool.start(task);
    for (int i = 0; i < 200 && !task->isRunning(); ++i) {
      QCoreApplication::processEvents();
      QThread::msleep(5);
    }

    task->onUserAbort();
    drain(60);

    printf("case 3 - after cancel  : finished=%d aborted=%d\n", c.finished, c.aborted);
    const bool ok = (c.aborted == 1 && c.finished == 1);
    printf("case 3 - %s: a re-started task must still be abortable\n",
           ok ? "PASS" : "FAIL");
    if (!ok && exitCode == 0) exitCode = 3;

    // When the case fails, the task by definition ignored the cancel and is
    // still running - deleting it here would crash the harness and replace the
    // exit code with a signal, hiding the very result just printed. Wait it
    // out instead (it is bounded by sleepMs above).
    for (int i = 0; i < 400 && task->isRunning(); ++i) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    drain(10);
    delete task;
  }

  printf("\n%s\n", exitCode == 0 ? "PASS" : "FAIL");
  return exitCode;
}
