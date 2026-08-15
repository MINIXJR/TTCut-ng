// Probe for the doH264Cut use-after-free (core.500359, 2026-08-07).
//
// What the crash looked like:
//
//   #0 QAbstractItemModelPrivate::invalidatePersistentIndexes
//   #1 QAbstractProxyModelPrivate::_q_sourceModelDestroyed
//   #2 TTAVData::doH264Cut            (frames missing in between, -O2 unwind)
//   #3 TTAVData::onDoCut
//   #4 TTCutMainWindow::onAudioVideoCut  ... #15 mouseReleaseEvent
//
// A proxy model was told its source model had been destroyed, and acted on
// that through an already-freed private block (garbage vtable, persistent
// index list with m_size 2 951 281).
//
// The 2026-08-07 investigation concluded "TTCut has no proxy model and no
// QCompleter (grep) -> Qt-internal -> not attributable". That exclusion has a
// hole: the grep could not find them because nothing in TTCut *writes* one.
// File dialogs bring their own. Qt's own QFileDialog holds a QFileSystemModel
// plus a QCompleter whose QCompletionModel IS a QAbstractProxyModel; under a
// KDE platform theme (KDEPlasmaPlatformTheme6 is installed here) the dialog is
// KIO-based instead and brings KDirSortFilterProxyModel, which derives from
// QSortFilterProxyModel - same base, same _q_sourceModelDestroyed.
//
// TTCut opens file dialogs several times in the sequence that crashed:
// getOpenFileName when opening the stream, getExistingDirectory inside the cut
// dialog (gui/ttcutavcutdlg.cpp:249, with a qApp->processEvents() right after
// it), and a heap-allocated QFileDialog when saving a frame. The cut dialog is
// then destroyed with a plain `delete` (gui/ttcutmainwindow.cpp) immediately
// before onDoCut runs - and doH264Cut re-enters the event loop through
// cutAudioTracks/cutSubtitleTracks' qApp->processEvents(). That is where a
// deferred deletion posted by the dialog teardown gets carried out, and it is
// where the missing stack frames belong.
//
// This probe isolates exactly that shape, without any TTCut code:
//
//   open a file dialog -> close it -> delete it -> re-enter the event loop
//   for a while, repeatedly.
//
// It answers two questions, and reports them separately:
//
//   1. PRECONDITION - does a file dialog in THIS environment actually create
//      a QAbstractProxyModel, and which class? If it creates none, the trail
//      is dead and no amount of cycling will show anything. This is printed
//      whether or not the stress phase finds something.
//   2. THE ACCESS ITSELF - run under ASAN, the cycles below turn a
//      use-after-free into a report naming the freed block. Without ASAN a
//      surviving run proves much less; it is still worth running, because the
//      original crash was a plain SIGSEGV.
//
// Must NOT be run offscreen: QT_QPA_PLATFORM=offscreen loads no platform
// theme, so no native dialog and no KIO models - the precondition would read
// as "no proxy models" for the wrong reason. The probe refuses to run there.
//
//   usage: test_filedialog_proxy [cycles]     (default 20)
//
// Build:  cmake --build build --target test_filedialog_proxy
// ASAN:   see the command in TODO.md ("SIGSEGV nach Smart Cut"), then
//         cmake --build build-asan --target test_filedialog_proxy
#include <QAbstractProxyModel>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

// Every proxy model reachable from the application's widgets, by class name.
// Models are not necessarily children of a widget, so this walks the object
// tree of every top-level widget and of the dialog itself.
QStringList proxyModelsInTree(QObject* extraRoot)
{
  QStringList found;
  QList<QObject*> roots;
  for (QWidget* w : qApp->topLevelWidgets()) roots.append(w);
  if (extraRoot) roots.append(extraRoot);

  for (QObject* root : roots) {
    if (auto* p = qobject_cast<QAbstractProxyModel*>(root))
      found << QString("%1 (root)").arg(p->metaObject()->className());
    for (QObject* child : root->findChildren<QObject*>()) {
      if (auto* p = qobject_cast<QAbstractProxyModel*>(child))
        found << p->metaObject()->className();
    }
  }
  found.removeDuplicates();
  return found;
}

// Which of the interesting libraries this process has actually mapped. The
// dialog implementation is loaded lazily, so this only says something AFTER a
// dialog has been constructed.
QStringList loadedDialogLibs()
{
  QStringList hits;
  QFile maps("/proc/self/maps");
  if (!maps.open(QIODevice::ReadOnly | QIODevice::Text)) return hits;
  const QString text = QString::fromUtf8(maps.readAll());
  for (const char* needle : { "KF6KIOFileWidgets", "KF6KIOWidgets", "KF6KIOCore",
                              "KDEPlasmaPlatformTheme", "libqgtk3", "xdgdesktopportal" }) {
    if (text.contains(QLatin1String(needle))) hits << QLatin1String(needle);
  }
  return hits;
}

} // namespace

int main(int argc, char** argv)
{
  // Unbuffered: a run that has to be killed (the modal loop below can hang if
  // the platform dialog ignores reject()) would otherwise lose everything
  // printed so far, including the precondition result - which is the part
  // worth having even when the stress phase does not finish.
  setvbuf(stdout, nullptr, _IONBF, 0);

  QApplication app(argc, argv);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr,
        "REFUSING: QT_QPA_PLATFORM=offscreen loads no platform theme, so this\n"
        "probe would report \"no proxy models\" for the wrong reason. Run it on\n"
        "the real session (Wayland/KDE).\n");
    return 2;
  }

  const int cycles = (argc > 1) ? QString(argv[1]).toInt() : 20;
  const QString startDir = QDir::homePath();

  printf("platform: %s   cycles: %d\n", qPrintable(qApp->platformName()), cycles);

  // ---- 1. Precondition -----------------------------------------------------
  // One dialog, inspected while it is alive.
  QStringList proxiesWhileOpen;
  QStringList libs;
  {
    QFileDialog* dlg = new QFileDialog(nullptr, "probe", startDir);
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);

    // show(), not exec(): exec() would block until the timer fires, and this
    // first pass only needs the dialog to have built its widgets and models.
    dlg->show();
    // Let the dialog finish construction, including anything the platform
    // theme sets up asynchronously.
    QElapsedTimer t; t.start();
    while (t.elapsed() < 600) qApp->processEvents();

    proxiesWhileOpen = proxyModelsInTree(dlg);
    libs             = loadedDialogLibs();

    dlg->close();
    delete dlg;
  }

  printf("\n=== PRECONDITION ===\n");
  printf("dialog libraries mapped: %s\n",
         libs.isEmpty() ? "(none of the known ones)" : qPrintable(libs.join(", ")));
  if (proxiesWhileOpen.isEmpty()) {
    printf("proxy models while the dialog was open: NONE\n");
    printf("  -> The file-dialog trail does not hold in this environment: the\n"
           "     dialog creates no QAbstractProxyModel, so it cannot be the\n"
           "     object in _q_sourceModelDestroyed. Look elsewhere.\n");
  } else {
    printf("proxy models while the dialog was open: %s\n",
           qPrintable(proxiesWhileOpen.join(", ")));
    printf("  -> Precondition HOLDS: a file dialog does bring a proxy model\n"
           "     into this process, which is the class of object the crash\n"
           "     backtrace was operating on.\n");
  }

  // ---- 2. The access itself ------------------------------------------------
  // open -> close -> delete -> re-enter the event loop, the shape TTCut
  // produces between closing the cut dialog and running doH264Cut.
  // What this phase does NOT do, and why that is worth writing down: a second
  // version ran the dialog modally with exec() and closed it a few
  // milliseconds after pointing it at a large directory, to tear it down with
  // KIO jobs still in flight. That is much closer to TTCut, which runs its
  // dialogs modally, and to the crash stack, which sits in a nested loop
  // inside a click handler.
  //
  // That version HANGS. The modal loop never returns - neither reject() nor a
  // 1.5 s backstop timer gets it back - and KIO reports "Socket not connected
  // / Connection::send() called with connection not inited" beforehand. Three
  // attempts, then abandoned rather than tuned further.
  //
  // So the shape below is the brave one: show(), no navigation, no live jobs
  // at teardown. A clean run through it says correspondingly less. The
  // modal-with-live-KIO-jobs case remains the one worth reaching - but not
  // through this harness.
  printf("\n=== STRESS (%d cycles) ===\n", cycles);
  int leftOver = 0;
  for (int i = 0; i < cycles; i++) {
    QFileDialog* dlg = new QFileDialog(nullptr, "probe", startDir);
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);
    dlg->show();

    QElapsedTimer tOpen; tOpen.start();
    while (tOpen.elapsed() < 120) qApp->processEvents();

    dlg->close();
    // Plain delete, deliberately: that is what TTCut does to the cut dialog
    // and to the frame-save dialog. Objects the teardown hands to
    // deleteLater() survive this call and are collected in the loop below.
    delete dlg;

    // The doH264Cut equivalent: a long stretch that keeps re-entering the
    // event loop while the previous dialog's deferred deletions are carried
    // out. cutAudioTracks/cutSubtitleTracks do this via their status reports.
    QElapsedTimer t; t.start();
    while (t.elapsed() < 250) qApp->processEvents();

    const QStringList still = proxyModelsInTree(nullptr);
    if (!still.isEmpty()) {
      leftOver++;
      if (leftOver <= 3)
        printf("  cycle %2d: proxy model(s) still reachable after delete: %s\n",
               i, qPrintable(still.join(", ")));
    }
    if ((i + 1) % 5 == 0) { printf("  %d/%d cycles\n", i + 1, cycles); fflush(stdout); }
  }

  printf("\n=== RESULT ===\n");
  printf("cycles completed without a crash: %d\n", cycles);
  printf("cycles that left a proxy model reachable after the dialog was deleted: %d\n", leftOver);
  printf("\nRun this under ASAN for the finding that matters. Surviving without\n"
         "ASAN says only that this shape alone did not fault today - the\n"
         "original crash was timing-dependent and happened once.\n");
  return 0;
}
