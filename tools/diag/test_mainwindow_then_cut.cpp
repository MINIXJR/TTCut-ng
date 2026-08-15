// The doH264Cut use-after-free hunt with the REAL main window in the process.
//
// Where this stands (TODO.md, docs/completed-work.md): the crash died in
// QAbstractProxyModelPrivate::_q_sourceModelDestroyed inside TTAVData::
// doH264Cut, on a proxy block that was already freed and reused. The proxy is
// named - a file dialog brings a KDirSortFilterProxyModel (KIO) along - and
// doH264Cut re-enters the event loop through cutAudioTracks'/cutSubtitle-
// Tracks' qApp->processEvents(), which is where deferred deletions from
// dialogs destroyed with plain `delete` are carried out.
//
// Ruled out so far, all under ASAN and all clean: the file dialog alone, a
// whole cut alone (--auto-cut), both in one process (test_dialog_then_cut),
// and the preview dialog including its clip generation and mpv playback on
// the original material with the original cut bounds
// (test_preview_then_cut, PREVIEW_FULL=1).
//
// What those harnesses do NOT have, and the crash sequence did: the real
// TTCutMainWindow with its views and item models, the cut dialog
// TTCutAVCutDlg (whose getExistingDirectory() is followed immediately by
// processEvents()), and the buttons actually being pressed. This harness adds
// all three: it drives the application's own widgets - openProjectFile(),
// then pbPreview, then pbCutAudioVideo - and closes the modal dialogs from a
// driver timer the way a user would.
//
// Still missing afterwards, and worth naming: real mouse events. click()
// raises QAbstractButton::clicked without going through QWidget::
// mouseReleaseEvent, which is the frame the crash backtrace started in.
//
// NOT offscreen: no platform theme means no KIO and no proxy model, and mpv
// gets no GL context - the run would prove nothing and look like it did.
//
//   usage: test_mainwindow_then_cut <project.ttcut> <workdir> [totalTimeoutSec]
//          PROBE_DISMISS=accept|click  how message boxes are dismissed
//                                      (default click - see the driver below)
//
// The project must CONTAIN a cut; without one there is nothing to preview or
// cut and the harness stops. None of the tux projects in
// tools/test-videos/cache carries one, so make a copy and add a <Cut> block
// INSIDE the <Video> element (a sibling after </Video> is silently ignored -
// that mistake cost a run):
//
//   <Cut><Order>0</Order><CutIn>200</CutIn><CutOut>900</CutOut></Cut>
//
// Run it with XDG_CONFIG_HOME pointing somewhere disposable: the cut path
// calls TTSettings::save(), which would otherwise rewrite the user's own
// settings (cut directory, output name, encoder codec).
//
// Build: cmake --build build --target test_mainwindow_then_cut
#include <QAbstractProxyModel>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>

#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "gui/ttcutmainwindow.h"

namespace {

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) qApp->processEvents();
}

// Visible top-level widget of a given class, or nullptr.
QWidget* visibleWidget(const char* className)
{
  for (QWidget* w : qApp->topLevelWidgets())
    if (w->isVisible() && w->inherits(className))
      return w;
  return nullptr;
}

QStringList proxyModelNames()
{
  QStringList found;
  for (QWidget* root : qApp->topLevelWidgets())
    for (QObject* child : root->findChildren<QObject*>())
      if (auto* p = qobject_cast<QAbstractProxyModel*>(child))
        found << p->metaObject()->className();
  found.removeDuplicates();
  return found;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr, "REFUSING: offscreen loads no platform theme (no KIO, no proxy)\n"
                    "and gives mpv no GL context - the run would prove nothing.\n");
    return 2;
  }
  if (argc < 3) {
    fprintf(stderr, "usage: %s <project.ttcut> <workdir> [totalTimeoutSec]\n", argv[0]);
    return 2;
  }
  const QString project = argv[1];
  const QString workDir = argv[2];
  const int totalTimeout = (argc > 3) ? QString(argv[3]).toInt() : 1800;

  QDir().mkpath(workDir);
  TTMessageLogger::getInstance()->setLogFilePath(
      QDir(workDir).absoluteFilePath("mainwindow_then_cut.log"));

  printf("platform: %s   config home: %s\n",
         qPrintable(qApp->platformName()), qgetenv("XDG_CONFIG_HOME").constData());

  TTCutMainWindow window;
  window.show();
  // Say what this is. The window is the real thing, indistinguishable from a
  // normal start otherwise - and a probe that looks like the application
  // invites someone to close it mid-run (it happened).
  window.setWindowTitle("PROBE test_mainwindow_then_cut - running, please leave open");
  pump(1000);
  printf("phase 1 - main window up, proxy models: %s\n",
         proxyModelNames().isEmpty() ? "none yet" : qPrintable(proxyModelNames().join(", ")));

  // ---- the driver: closes whatever modal dialog the application opens ------
  // A user with a very steady hand. Each dialog gets a dwell time first, so
  // the thing being tested (its construction, its clips, its KIO jobs) really
  // happens before it is torn down again.
  int previewSeenAt = -1;
  QElapsedTimer clock; clock.start();
  QTimer driver;
  driver.setInterval(250);
  QObject::connect(&driver, &QTimer::timeout, [&] {
    if (QWidget* w = visibleWidget("TTCutPreview")) {
      if (previewSeenAt < 0) {
        previewSeenAt = int(clock.elapsed());
        printf("phase 3 - preview dialog is up (%d ms)\n", previewSeenAt);
      } else if (clock.elapsed() - previewSeenAt > 4000) {
        printf("phase 3 - closing the preview dialog\n");
        static_cast<QDialog*>(w)->reject();
      }
      return;
    }
    if (QWidget* w = visibleWidget("TTCutAVCutDlg")) {
      printf("phase 4 - accepting the cut dialog\n");
      static_cast<QDialog*>(w)->accept();
      return;
    }
    // Anything else modal would block the run - a burst warning, an error box.
    //
    // HOW it is dismissed is itself under test. accept() ends exec() from the
    // Qt side; a user presses the button. Under the Plasma platform theme a
    // QMessageBox can be backed by a NATIVE dialog helper, and the two routes
    // need not leave that helper in the same state - the crash being chased
    // here dies in QDialogPrivate::setNativeDialogVisible() while the box is
    // being destroyed. PROBE_DISMISS=accept|click selects the route so the
    // difference can be measured instead of argued.
    if (auto* box = qobject_cast<QMessageBox*>(qApp->activeModalWidget())) {
      const bool byClick = (qgetenv("PROBE_DISMISS") != "accept");
      printf("  (dismissing message box by %s: %s)\n",
             byClick ? "button click" : "accept()", qPrintable(box->text().left(50)));
      QAbstractButton* btn = box->defaultButton();
      if (btn == nullptr && !box->buttons().isEmpty()) btn = box->buttons().first();
      if (byClick && btn != nullptr) btn->click();
      else box->accept();
    }
  });
  driver.start();

  // ---- phase 2: load the project the way the GUI does ---------------------
  window.openProjectFile(project);
  {
    // By name, not by type: findChild<QTreeView*>() returns whichever tree
    // comes first in the object tree - the audio list or the stream points -
    // and waiting for THAT to fill means waiting out the timeout while the
    // cut list has been ready for two minutes. The cut list is the
    // QTreeWidget "videoCutList" inside TTCutTreeView "cutList"
    // (ui/cutlistwidget.ui).
    QElapsedTimer t; t.start();
    QTreeWidget* tree = window.findChild<QTreeWidget*>("videoCutList");
    while (t.elapsed() < 120000) {
      qApp->processEvents();
      if (tree == nullptr) tree = window.findChild<QTreeWidget*>("videoCutList");
      if (tree && tree->topLevelItemCount() > 0) break;
    }
    printf("phase 2 - project loaded after %lld ms, cut entries: %d\n",
           (long long)t.elapsed(), tree ? tree->topLevelItemCount() : -1);
    if (tree == nullptr || tree->topLevelItemCount() == 0) {
      fprintf(stderr, "FAIL: no cut entries - nothing to preview or cut.\n"
                      "      (None of the tux test projects carries a cut;\n"
                      "       use a project that has one.)\n");
      return 1;
    }
  }
  // Wait until the application is actually READY, not just until the cut list
  // has rows. Measured the hard way: the cut list is populated 5 ms after
  // openProjectFile() while the video stream is still being read - on a 7.9 GB
  // recording, pressing Preview at that moment sent the GUI thread spinning at
  // 100% of one core for over ten minutes and no preview dialog ever appeared
  // (with AND without AddressSanitizer, so not an instrumentation effect).
  // A user does not do that; they wait for the window to come back.
  //
  // "Ready" is exactly what the user sees: TTCutMainWindow disables itself for
  // the duration of an operation (onStatusReport's Init branch) and re-enables
  // it on Exit/Canceled. So: enabled, and staying enabled for two seconds.
  {
    QElapsedTimer t; t.start();
    qint64 enabledSince = -1;
    while (t.elapsed() < 600000) {
      qApp->processEvents();
      if (window.isEnabled()) {
        if (enabledSince < 0) enabledSince = t.elapsed();
        else if (t.elapsed() - enabledSince > 2000) break;
      } else {
        enabledSince = -1;
      }
    }
    printf("phase 2 - window ready after %lld ms\n", (long long)t.elapsed());
  }

  TTSettings::instance()->setCutDirPath(workDir);
  TTSettings::instance()->setTempDirPath(workDir);
  TTSettings::instance()->setCutVideoName("mainwindow_then_cut");

  // ---- the file dialog, for the KIO proxy ---------------------------------
  {
    QFileDialog* dlg = new QFileDialog(&window, "probe", QDir::homePath());
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);
    dlg->show();
    pump(800);
    printf("phase 2 - proxy models with the file dialog open: %s\n",
           proxyModelNames().isEmpty() ? "NONE" : qPrintable(proxyModelNames().join(", ")));
    dlg->close();
    delete dlg;                     // plain delete, as the application does
  }
  pump(300);

  // ---- phase 3: the preview, started from its own button ------------------
  auto* pbPreview = window.findChild<QPushButton*>("pbPreview");
  auto* pbCutAV   = window.findChild<QPushButton*>("pbCutAudioVideo");
  if (pbPreview == nullptr || pbCutAV == nullptr) {
    fprintf(stderr, "FAIL: buttons not found (pbPreview=%p pbCutAudioVideo=%p)\n",
            (void*)pbPreview, (void*)pbCutAV);
    return 1;
  }

  printf("phase 3 - pressing Preview\n");
  // click() returns at once: onCutPreview() only starts doCutPreview(), and
  // the dialog is created later, when cutPreviewFinished arrives. Waiting for
  // the dialog to appear AND disappear again is what keeps phase 4 from
  // pressing Cut into a preview that is still generating its clips.
  pbPreview->click();
  {
    QElapsedTimer t; t.start();
    while (t.elapsed() < 600000 && previewSeenAt < 0) qApp->processEvents();
    if (previewSeenAt < 0) {
      fprintf(stderr, "FAIL: the preview dialog never appeared\n");
      return 1;
    }
    while (t.elapsed() < 600000 && visibleWidget("TTCutPreview") != nullptr)
      qApp->processEvents();
    printf("phase 3 - preview done after %lld ms\n", (long long)t.elapsed());
  }
  pump(500);

  // ---- phase 4: the cut, from its own button ------------------------------
  printf("phase 4 - pressing Cut A/V\n");
  QElapsedTimer cutClock; cutClock.start();
  pbCutAV->click();                 // returns once onDoCut has been issued

  // The cut runs in the pool; wait for the progress dialog to appear and go.
  bool progressSeen = false;
  qint64 goneSince = -1;
  QElapsedTimer wait; wait.start();
  while (wait.elapsed() < qint64(totalTimeout) * 1000) {
    qApp->processEvents();
    const bool up = (visibleWidget("TTProgressBar") != nullptr);
    if (up) { progressSeen = true; goneSince = -1; }
    else if (progressSeen) {
      if (goneSince < 0) goneSince = wait.elapsed();
      else if (wait.elapsed() - goneSince > 10000) break;   // settled
    }
  }
  printf("phase 4 - cut phase ended after %lld ms (progress dialog %s)\n",
         (long long)cutClock.elapsed(), progressSeen ? "was seen" : "NEVER APPEARED");

  // Deferred deletions are the whole point - give them a last turn.
  pump(3000);
  driver.stop();

  printf("proxy models still reachable at the end: %s\n",
         proxyModelNames().isEmpty() ? "none" : qPrintable(proxyModelNames().join(", ")));
  printf("\nNo crash in this run. Under ASAN that is a statement about this\n"
         "shape; without it, much less.\n");
  return 0;
}
