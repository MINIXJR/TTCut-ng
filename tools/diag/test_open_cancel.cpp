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
//   test_open_cancel pending <video> <cancel ms, -1 = none> <items>
//       a fresh open (TTAVData::openAVStreams) to the end of its pool run,
//       cancelled <ms> after the start; then exactly <items> TTAVItem may be
//       alive (counted through Qt's object hooks), and with 1 it must be the
//       listed item with its video stream. The item of a failed or cancelled
//       open stayed in mPendingVideoItems and was never freed (2026-10-16,
//       gdb: constructor 1, destructor 0 until the process ended). With an
//       extra-frame dialog the pool run ends INSIDE that modal dialog, while
//       onOpenVideoFinished still holds the item - it must survive.
//
// Build via `cmake --build build --target test_open_cancel`.
#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <clocale>
#include <cstdio>

#include "common/ttsettings.h"
#include "common/ttthreadtaskpool.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttopenvideotask.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttprogressbar.h"

// Qt's object hooks (QtCore private qhooks_p.h, the interface GammaRay uses;
// exported by libQt6Core): every QObject is reported at construction and
// destruction, so the live TTAVItem can be counted without any accessor.
extern Q_DECL_IMPORT quintptr qtHookData[];
static QMutex gLiveMutex;
static QSet<QObject*> gLive;
static void hookAdd(QObject* o)    { QMutexLocker l(&gLiveMutex); gLive.insert(o); }
static void hookRemove(QObject* o) { QMutexLocker l(&gLiveMutex); gLive.remove(o); }
static bool installObjectHooks()
{
  enum { HookDataVersion = 0, AddQObject = 3, RemoveQObject = 4 };
  if (qtHookData[HookDataVersion] < 1) return false;
  qtHookData[AddQObject]    = reinterpret_cast<quintptr>(&hookAdd);
  qtHookData[RemoveQObject] = reinterpret_cast<quintptr>(&hookRemove);
  return true;
}
// Only with every worker idle: an object still under construction elsewhere
// would not cast yet.
static QList<TTAVItem*> liveItems()
{
  QMutexLocker l(&gLiveMutex);
  QList<TTAVItem*> items;
  for (QObject* o : gLive)
    if (auto* item = qobject_cast<TTAVItem*>(o)) items.append(item);
  return items;
}

static void closeMessageBoxes()
{
  for (QWidget* t : QApplication::topLevelWidgets())
    if (auto* b = qobject_cast<QMessageBox*>(t); b && b->isVisible()) {
      printf("message box: \"%s\"\n", qPrintable(b->windowTitle()));
      b->done(0);
    }
}

static bool messageBoxOpen()
{
  for (QWidget* t : QApplication::topLevelWidgets())
    if (qobject_cast<QMessageBox*>(t) && t->isVisible()) return true;
  return false;
}

static int pendingCase(const QString& video, int cancelMs, int expected)
{
  TTAVData data;
  QTimer watch;
  watch.setInterval(100);
  QObject::connect(&watch, &QTimer::timeout, closeMessageBoxes);
  watch.start();

  int exits = 0;
  QObject::connect(data.threadTaskPool(), &TTThreadTaskPool::exit, [&exits] { ++exits; });
  data.openAVStreams(video);
  if (cancelMs >= 0)
    QTimer::singleShot(cancelMs, [&data] { data.threadTaskPool()->onUserAbortRequest(); });

  // The pool run is over and no dialog is open - for a second, so that the
  // handlers the run queued (and their deleteLater) have run too.
  QElapsedTimer t;
  t.start();
  qint64 since = -1;
  while (t.elapsed() < 120000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (exits > 0 && !messageBoxOpen()) { if (since < 0) since = t.elapsed(); else if (t.elapsed() - since > 1000) break; }
    else since = -1;
  }

  const QList<TTAVItem*> live = liveItems();
  bool ok = exits > 0 && live.size() == expected && data.avCount() == expected;
  if (ok && expected == 1)
    ok = data.avItemAt(0) == live.first() && data.avItemAt(0)->videoStream() != nullptr;
  printf("%s: %s, cancel %d ms - pool runs ended %d, live TTAVItem %lld, listed %d (expected %d%s)\n",
         ok ? "PASS" : "FAIL", qPrintable(video.section('/', -1)), cancelMs, exits,
         (long long)live.size(), data.avCount(), expected,
         expected == 1 ? ", the listed one with its video stream" : "");
  return ok ? 0 : 1;
}

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
  QObject::connect(&watch, &QTimer::timeout, closeMessageBoxes);
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
  const bool hooks = installObjectHooks();
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start otherwise, as in main()
  app.setApplicationName("TTCut-ng");
  app.setOrganizationName("TTCut-ng");
  (void)TTSettings::instance();

  const QString mode = argc > 1 ? argv[1] : QString();
  if (mode == "unit") return unitCase();
  if (mode == "cancel" && argc > 3) return cancelCase(QString::fromLocal8Bit(argv[2]), atoi(argv[3]));
  if (mode == "pending" && argc > 4) {
    if (!hooks) { fprintf(stderr, "Qt object hooks not available\n"); return 2; }
    return pendingCase(QString::fromLocal8Bit(argv[2]), atoi(argv[3]), atoi(argv[4]));
  }
  fprintf(stderr, "usage: %s unit | cancel <video or .ttcut> <ms> | pending <video> <cancel ms|-1> <items>\n", argv[0]);
  return 2;
}
