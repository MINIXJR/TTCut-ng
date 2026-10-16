// Gate for cancelling a video open (TODO.md, reading findings of the full map
// check 2026-10-11, "Abbruch während eines Projektladens"). TTOpenVideoTask::
// onUserAbort deleted the TTAVItem it was opening, although TTAVData owns it
// (mPendingVideoItems) and the task's own finished() and the audio and
// subtitle tasks of the same item still carry the pointer: a cancel while a
// video or a project was loading ended in a use-after-free
// (onOpenVideoFinished -> setVideoStream, onOpenAudioFinished ->
// appendAudioEntry; AddressSanitizer, 2026-10-16).
//
//   test_open_cancel unit
//       onUserAbort() on a task that never ran leaves its item alive -
//       decidable without a sanitizer and without timing.
//   test_open_cancel cancel <video or .ttcut> <ms>
//       the real main window opens the file (a video through the fresh-open
//       path, a project through openProjectFile) and the progress bar's
//       Cancel fires <ms> later; the process must survive and the window must
//       come back. A smoke test: whether the old code crashed here depends on
//       what reused the freed memory (3 of 4 runs did).
//
// Build via `cmake --build build --target test_open_cancel`.
#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <clocale>
#include <cstdio>

#include "common/ttsettings.h"
#include "data/ttavlist.h"
#include "data/ttopenvideotask.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttprogressbar.h"

static int unitCase()
{
  QPointer<TTAVItem> item = new TTAVItem(nullptr);
  auto* task = new TTOpenVideoTask(item, "/nonexistent.264", 0);
  task->onUserAbort();
  const bool alive = !item.isNull();
  printf("%s: onUserAbort() on a task that never ran leaves its item %s\n",
         alive ? "PASS" : "FAIL", alive ? "alive" : "deleted");
  delete task;
  delete item.data();
  return alive ? 0 : 1;
}

static int cancelCase(const QString& file, int ms)
{
  TTCutMainWindow w;
  w.show();
  QTimer watch;
  watch.setInterval(100);
  QObject::connect(&watch, &QTimer::timeout, [] {
    for (QWidget* t : QApplication::topLevelWidgets())
      if (auto* b = qobject_cast<QMessageBox*>(t); b && b->isVisible()) {
        printf("message box: \"%s\"\n", qPrintable(b->windowTitle()));
        b->done(0);
      }
  });
  watch.start();

  QElapsedTimer clock;
  bool cancelled = false;
  QTimer::singleShot(200, [&] {
    clock.start();
    if (file.endsWith(".ttcut")) w.openProjectFile(file);
    else                         w.onReadVideoStream(file);
  });
  QTimer::singleShot(200 + ms, [&] {
    auto* bar = w.findChild<TTProgressBar*>();
    printf("cancel at %lld ms, window %s\n", (long long)clock.elapsed(),
           w.isEnabled() ? "enabled (load already over)" : "disabled (loading)");
    if (bar) { emit bar->cancel(); cancelled = true; }
  });

  // The window comes back: enabled again and staying so for two seconds.
  QElapsedTimer t;
  t.start();
  qint64 since = -1;
  while (t.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!cancelled) continue;
    if (w.isEnabled()) { if (since < 0) since = t.elapsed(); else if (t.elapsed() - since > 2000) break; }
    else since = -1;
  }
  const bool back = cancelled && w.isEnabled();
  printf("%s: cancel of %s after %d ms - process alive, window %s\n", back ? "PASS" : "FAIL",
         qPrintable(file.section('/', -1)), ms, w.isEnabled() ? "usable" : "still disabled after 60 s");
  return back ? 0 : 1;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start otherwise, as in main()
  app.setApplicationName("TTCut-ng");
  app.setOrganizationName("TTCut-ng");
  (void)TTSettings::instance();

  const QString mode = argc > 1 ? argv[1] : QString();
  if (mode == "unit") return unitCase();
  if (mode == "cancel" && argc > 3) return cancelCase(QString::fromLocal8Bit(argv[2]), atoi(argv[3]));
  fprintf(stderr, "usage: %s unit | cancel <video or .ttcut> <ms>\n", argv[0]);
  return 2;
}
