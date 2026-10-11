// Gate for two findings of the full map check (2026-10-11) about the
// quick-jump dialog's thumbnail workers:
//
//   1. Nobody deleted a TTQuickJumpWorker: TTThreadTask::setAutoDelete(false),
//      the dialog connected no deleteLater and only forgot its pointer, the
//      pool only takes the task out of its queue. Measured: one more worker
//      alive after every page turn, resize and interval change, none freed
//      when the dialog closed.
//   2. abortCurrentWorker() called onUserAbort() only while the worker
//      isRunning(). A worker that was started but had not reached run() yet
//      was dropped without the abort and decoded its whole page for nobody
//      (measured: two turns in one event-loop pass decoded both pages).
//
// Opens the dialog the way the user does (the navigation panel's button, a
// modal exec), turns pages through the dialog's own slots and reads the two
// counters of TTQuickJumpWorker.
//
//   usage: test_quickjump_worker <video-es>
// Build via `cmake --build build --target test_quickjump_worker`.
#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <cstdio>

#include "gui/ttcutframenavigation.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttquickjumpdialog.h"
#include "gui/ttquickjumpworker.h"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

bool waitForDirty(TTCutMainWindow& wnd, int timeoutMs)
{
  QElapsedTimer t; t.start();
  while (!wnd.windowTitle().endsWith("*") && t.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(20);
  }
  return wnd.windowTitle().endsWith("*");
}

// Pumps until the global pool has been idle for a moment (queued finished /
// deleteLater events get their turn too).
void waitIdle(int timeoutMs)
{
  QElapsedTimer t; t.start();
  int quiet = 0;
  while (t.elapsed() < timeoutMs && quiet < 10) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
    quiet = (QThreadPool::globalInstance()->activeThreadCount() == 0) ? quiet + 1 : 0;
  }
}

void turn(QObject* dlg, const char* slot) { QMetaObject::invokeMethod(dlg, slot, Qt::DirectConnection); }

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <video-es>\n", argv[0]);
    return 2;
  }
  const QString video = QString::fromUtf8(argv[1]);

  TTCutMainWindow mainWnd;
  mainWnd.show();
  QApplication::processEvents();

  QTimer::singleShot(0, &mainWnd, [&]() {
    mainWnd.onReadVideoStream(video);
    if (!waitForDirty(mainWnd, 60000)) {
      printf("FAIL: the opened video did not mark the project modified\n");
      failures++;
      qApp->exit(1);
      return;
    }
    waitIdle(60000);   // open tasks and an auto-started scan are through

    auto* nav = mainWnd.findChild<TTCutFrameNavigation*>();
    auto* pb  = nav ? nav->findChild<QPushButton*>("pbQuickJump") : nullptr;
    if (!pb) { printf("FAIL: quick-jump button not found\n"); failures++; qApp->exit(1); return; }

    int live[4] = {-1, -1, -1, -1}, dec[4] = {-1, -1, -1, -1};
    bool drove = false;
    int liveOpen = -1;

    // Runs inside the dialog's exec(): drives it, then closes it.
    QTimer::singleShot(200, [&]() {
      auto* dlg = qobject_cast<TTQuickJumpDialog*>(QApplication::activeModalWidget());
      if (!dlg) { printf("FAIL: the quick-jump dialog did not open\n"); failures++; return; }

      waitIdle(60000);                       // first page (and the resize restart) decoded
      liveOpen = TTQuickJumpWorker::liveCount();

      // The Tux fixtures have few keyframes: every I-frame and a small window
      // give more than one page. Both restart the worker (interval change at
      // once, the resize after its 200 ms debounce).
      if (auto* spin = dlg->findChild<QSpinBox*>()) spin->setValue(spin->minimum());
      dlg->resize(360, 260);
      { QElapsedTimer t; t.start(); while (t.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20); }
      waitIdle(60000);
      live[0] = TTQuickJumpWorker::liveCount(); dec[0] = TTQuickJumpWorker::decodedCount();

      turn(dlg, "onPageForward"); waitIdle(60000);
      live[1] = TTQuickJumpWorker::liveCount(); dec[1] = TTQuickJumpWorker::decodedCount();

      turn(dlg, "onPageBack"); waitIdle(60000);
      live[2] = TTQuickJumpWorker::liveCount(); dec[2] = TTQuickJumpWorker::decodedCount();

      // Two turns without a trip through the event loop: the worker of the
      // first turn is dropped before it can have reached run().
      turn(dlg, "onPageForward"); turn(dlg, "onPageBack"); waitIdle(60000);
      live[3] = TTQuickJumpWorker::liveCount(); dec[3] = TTQuickJumpWorker::decodedCount();

      drove = true;
      dlg->reject();
    });

    pb->click();                             // blocks in exec() until the timer closes the dialog
    waitIdle(10000);
    const int liveEnd = TTQuickJumpWorker::liveCount();

    const int pageFwd  = dec[1] - dec[0];    // thumbnails of the second page
    const int pageBack = dec[2] - dec[1];    // thumbnails of the first page
    const int twoTurns = dec[3] - dec[2];
    printf("observed: live after open=%d, after shrinking=%d, after forward=%d, after back=%d, after two quick turns=%d, after close=%d\n",
           liveOpen, live[0], live[1], live[2], live[3], liveEnd);
    printf("observed: decoded after open=%d; forward page=%d, back page=%d, two quick turns=%d\n",
           dec[0], pageFwd, pageBack, twoTurns);

    check(drove, "the dialog opened and was driven");
    check(pageFwd > 0 && pageBack > 0, "both page turns decoded thumbnails (the stream has two pages)");
    check(live[3] <= 1, "page turns do not pile up workers");
    check(liveEnd == 0, "no worker is left after the dialog closed");
    check(twoTurns < pageFwd + pageBack, "a worker dropped before it ran does not decode its page");
    qApp->exit(0);
  });

  app.exec();
  printf("%s\n", failures ? "QUICKJUMP-WORKER FAIL" : "QUICKJUMP-WORKER PASS");
  return failures ? 1 : 0;
}
