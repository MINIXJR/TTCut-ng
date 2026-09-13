// Gate for finding B1 of code-audit run 4 (2026-09-13): every analysis task
// TTCutMainWindow starts on its stream-point pool must be deleted after it
// ran - finished or aborted. The two stream-point workers were started
// without a deleteLater connection (the pool does not own its tasks,
// TTThreadTask::setAutoDelete(false), no QObject parent), so one instance
// per analysis stayed alive, reachable through its signal connections and
// therefore invisible to LeakSanitizer.
//
// Drives the real TTCutMainWindow::startAnalysisTask() offscreen with two
// dummy tasks: one that finishes, one that throws TTAbortException (the
// pool's abort route). Both must emit destroyed() within the timeout.
//
// Run with XDG_CONFIG_HOME/XDG_CACHE_HOME pointing at disposable
// directories (run-gates.sh does). Build via
// `cmake --build build --target test_analysis_task_lifetime`.
#include <QApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <cstdio>

#include "common/ttexception.h"
#include "common/ttthreadtask.h"
#include "gui/ttcutmainwindow.h"

namespace {

class LifetimeTask : public TTThreadTask
{
public:
  explicit LifetimeTask(bool throwAbort)
    : TTThreadTask(throwAbort ? "LifetimeAbort" : "LifetimeFinish"), mThrowAbort(throwAbort) {}
protected:
  void operation() override { if (mThrowAbort) throw TTAbortException("aborting for the gate"); }
  void cleanUp() override {}
public slots:
  void onUserAbort() override { abort(); }
private:
  bool mThrowAbort;
};

int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

bool waitGone(QPointer<QObject>& p, int timeoutMs)
{
  QElapsedTimer t; t.start();
  while (!p.isNull() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return p.isNull();
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (!qEnvironmentVariableIsSet("XDG_CONFIG_HOME")) {
    fprintf(stderr, "XDG_CONFIG_HOME must point at a disposable directory\n");
    return 2;
  }

  TTCutMainWindow mainWnd;

  QPointer<QObject> finishing = new LifetimeTask(false);
  QPointer<QObject> aborting  = new LifetimeTask(true);
  mainWnd.startAnalysisTask(static_cast<TTThreadTask*>(finishing.data()));
  mainWnd.startAnalysisTask(static_cast<TTThreadTask*>(aborting.data()));

  check(waitGone(finishing, 10000), "a task that finished is deleted");
  check(waitGone(aborting, 10000),  "a task that aborted is deleted");

  printf("%s\n", failures ? "ANALYSIS-TASK-LIFETIME FAIL" : "ANALYSIS-TASK-LIFETIME PASS");
  return failures ? 1 : 0;
}
