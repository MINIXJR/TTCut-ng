// Runtime repro for two user reports (2026-08-18):
//
//  (2) "Audiospuren umsortieren, zur Videoliste und zurück wechseln ->
//      Sortierung wieder auf Standard." Static analysis points at
//      TTAVData::onThreadPoolExit(), which re-sorts EVERY audio list by
//      language preference on EVERY pool exit (ttavdata.cpp) and then emits
//      avDataReloaded() -> TTCutMainWindow::onAVDataReloaded() rebuilds the
//      audio tree view from the re-sorted model. This harness replays the
//      user's steps against the real TTCutMainWindow and prints the audio
//      list order after each one, then forces a pool run (subtitle open) to
//      show the mechanism.
//
//  (1) "Schnittdialog-Einstellungen erst beim Programmende gespeichert."
//      TTCutAVCutDlg writes its values into the in-memory TTSettings on OK,
//      but nothing persists them; TTCutMainWindow::closeProject() calls
//      TTSettings::load(), which re-reads QSettings and discards them. The
//      harness drives the REAL dialog (leOutputPath + okButton) and then
//      File->New, and prints cutDirPath before/after.
//
// Run with XDG_CONFIG_HOME pointing somewhere disposable - phase B seeds and
// reads QSettings. Offscreen is fine: no mpv playback, no KIO proxy involved.
//
//   usage: test_audio_order_reset <project.ttcut> <subtitle.srt> <workdir>
//
// Build: cmake --build build --target test_audio_order_reset
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
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

// Wait until the window is enabled and stays enabled (the ready criterion
// test_mainwindow_then_cut measured out the hard way).
void waitReady(QWidget& window, int timeoutMs)
{
  QElapsedTimer t; t.start();
  qint64 enabledSince = -1;
  while (t.elapsed() < timeoutMs) {
    qApp->processEvents();
    if (window.isEnabled()) {
      if (enabledSince < 0) enabledSince = t.elapsed();
      else if (t.elapsed() - enabledSince > 2000) return;
    } else {
      enabledSince = -1;
    }
  }
}

QStringList audioViewOrder(QTreeWidget* audioTree)
{
  QStringList names;
  for (int i = 0; i < audioTree->topLevelItemCount(); i++)
    names << audioTree->topLevelItem(i)->text(0);
  return names;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);

  if (argc < 4) {
    fprintf(stderr, "usage: %s <project.ttcut> <subtitle.srt> <workdir>\n", argv[0]);
    return 2;
  }
  const QString project = argv[1];
  const QString srtFile = argv[2];
  const QString workDir = argv[3];

  QDir().mkpath(workDir);
  TTMessageLogger::getInstance()->setLogFilePath(
      QDir(workDir).absoluteFilePath("audio_order_reset.log"));

  printf("platform: %s   config home: %s\n",
         qPrintable(qApp->platformName()), qgetenv("XDG_CONFIG_HOME").constData());

  TTCutMainWindow window;
  window.show();
  window.setWindowTitle("PROBE test_audio_order_reset - running, please leave open");
  pump(500);

  // Driver: closes whatever modal the application opens. The cut dialog in
  // phase B gets the user's path change plus OK; File->New pops a
  // confirmation box (default No) - answer Yes. Anything else modal gets
  // its default button.
  QString driverCutPath;                          // set by phase B
  QTimer driver;
  driver.setInterval(200);
  QObject::connect(&driver, &QTimer::timeout, [&] {
    for (QWidget* w : qApp->topLevelWidgets()) {
      if (w->isVisible() && w->inherits("TTCutAVCutDlg") && !driverCutPath.isEmpty()) {
        auto* lePath = w->findChild<QLineEdit*>("leOutputPath");
        auto* okBtn  = w->findChild<QPushButton*>("okButton");
        if (lePath && okBtn) {
          printf("  (cut dialog: setting path + OK)\n");
          lePath->setText(driverCutPath);
          driverCutPath.clear();
          okBtn->click();
        }
        return;
      }
    }
    if (auto* box = qobject_cast<QMessageBox*>(qApp->activeModalWidget())) {
      QAbstractButton* yes = box->button(QMessageBox::Yes);
      QAbstractButton* btn = yes ? yes : box->defaultButton();
      if (btn == nullptr && !box->buttons().isEmpty()) btn = box->buttons().first();
      printf("  (dismissing message box '%s' via '%s')\n",
             qPrintable(box->text().left(60)),
             btn ? qPrintable(btn->text()) : "?");
      if (btn) btn->click();
      else box->accept();
    }
  });
  driver.start();

  // ---- load the two-audio-track project ------------------------------------
  window.openProjectFile(project);
  waitReady(window, 600000);

  QTreeWidget* audioTree = window.findChild<QTreeWidget*>("audioListView");
  QTreeWidget* videoTree = window.findChild<QTreeWidget*>("videoListView");
  QTabWidget*  tabs      = window.findChild<QTabWidget*>("tabFileView");
  auto* pbUp             = window.findChild<QPushButton*>("pbAudioEntryUp");
  if (!audioTree || !videoTree || !tabs || !pbUp) {
    fprintf(stderr, "FAIL: widgets not found (audio=%p video=%p tabs=%p up=%p)\n",
            (void*)audioTree, (void*)videoTree, (void*)tabs, (void*)pbUp);
    return 1;
  }
  if (audioTree->topLevelItemCount() < 2) {
    fprintf(stderr, "FAIL: expected 2 audio tracks, got %d\n",
            audioTree->topLevelItemCount());
    return 1;
  }

  printf("step 0 - after load:            %s\n",
         qPrintable(audioViewOrder(audioTree).join(" | ")));

  // ---- user reorder: second track up via the widget's own button -----------
  tabs->setCurrentIndex(1);                       // Audiofiles tab
  pump(200);
  audioTree->setCurrentItem(audioTree->topLevelItem(1));
  pump(100);
  pbUp->click();
  pump(300);
  const QStringList userOrder = audioViewOrder(audioTree);
  printf("step 1 - after user reorder:    %s\n", qPrintable(userOrder.join(" | ")));

  // ---- the user's trigger: tab to Videofile and back ------------------------
  tabs->setCurrentIndex(0);
  pump(400);
  tabs->setCurrentIndex(1);
  pump(400);
  printf("step 2 - after tab switch:      %s  [%s]\n",
         qPrintable(audioViewOrder(audioTree).join(" | ")),
         audioViewOrder(audioTree) == userOrder ? "kept" : "RESET");

  // ---- re-select the video row (fires selectionChanged) ---------------------
  tabs->setCurrentIndex(0);
  pump(200);
  videoTree->clearSelection();
  pump(100);
  if (videoTree->topLevelItemCount() > 0)
    videoTree->setCurrentItem(videoTree->topLevelItem(0));
  pump(400);
  tabs->setCurrentIndex(1);
  pump(200);
  printf("step 3 - after video re-select: %s  [%s]\n",
         qPrintable(audioViewOrder(audioTree).join(" | ")),
         audioViewOrder(audioTree) == userOrder ? "kept" : "RESET");

  // ---- force a thread-pool run: open a subtitle file ------------------------
  window.onReadSubtitleStream(srtFile);
  waitReady(window, 120000);
  pump(500);
  printf("step 4 - after pool run (srt):  %s  [%s]\n",
         qPrintable(audioViewOrder(audioTree).join(" | ")),
         audioViewOrder(audioTree) == userOrder ? "kept" : "RESET");

  // ---- project round-trip: save, close, reload ------------------------------
  // "Projektreihenfolge gewinnt" (user decision 2026-08-18): a saved .ttcut
  // must bring the tracks back in the order the user left them, not in
  // language-preference order. openProjectFile() does NOT set the project
  // file name (only Save-As/Save do), so onFileSave() would block in a
  // native getSaveFileName() - preset the name the way the user's dialog
  // choice would.
  TTSettings::instance()->setProjectFileName(project);
  window.onFileSave();
  pump(500);
  window.onFileNew();                              // driver answers Yes
  pump(1000);
  window.openProjectFile(project);
  waitReady(window, 600000);
  pump(500);
  printf("step 5 - after save+reload:     %s  [%s]\n",
         qPrintable(audioViewOrder(audioTree).join(" | ")),
         audioViewOrder(audioTree) == userOrder ? "kept" : "RESET");

  // ================= phase B: cut dialog settings persistence ===============
  // The user's real flow: press "Cut A/V", change the output path in the
  // cut dialog (the driver does both), OK starts the cut. Then File->New
  // (closeProject -> TTSettings::load()). If nothing persisted the dialog's
  // change at cut start, load() reverts the path to the on-disk value.
  //
  // On a real system the CutOptions/DirPath key exists on disk (written by
  // every previous program exit). Without it, load() falls back to the
  // in-memory member (settings.value("DirPath/", mCutDirPath)) and would
  // mask the defect - so persist the CURRENT (pre-dialog) state first, the
  // way closeEvent()/onActionSettings() of earlier sessions did.
  TTSettings::instance()->save();

  const QString beforeDialog = TTSettings::instance()->cutDirPath();
  const QString newPath = QDir(workDir).absoluteFilePath("dialog-chosen-path");
  QDir().mkpath(newPath);

  // MKV container via libav - no dependency on an installed mplex.
  TTSettings::instance()->setMpeg2Muxer(1);
  TTSettings::instance()->setWorkingOutputContainer(1);

  auto* pbCutAV = window.findChild<QPushButton*>("pbCutAudioVideo");
  if (!pbCutAV) {
    fprintf(stderr, "FAIL: pbCutAudioVideo not found\n");
    return 1;
  }
  driverCutPath = newPath;                         // driver edits + accepts
  pbCutAV->click();
  {
    QElapsedTimer t; t.start();
    while (t.elapsed() < 120000 && !driverCutPath.isEmpty()) qApp->processEvents();
    if (!driverCutPath.isEmpty()) {
      fprintf(stderr, "FAIL: cut dialog never appeared\n");
      return 1;
    }
  }
  waitReady(window, 600000);                       // cut runs on the pool
  pump(1000);

  printf("phase B - cutDirPath before dialog: %s\n", qPrintable(beforeDialog));
  printf("phase B - cutDirPath after cut:     %s\n",
         qPrintable(TTSettings::instance()->cutDirPath()));

  window.onFileNew();                              // closeProject -> load()
  pump(500);

  const QString afterNew = TTSettings::instance()->cutDirPath();
  printf("phase B - cutDirPath after File-New: %s  [%s]\n",
         qPrintable(afterNew),
         afterNew == newPath ? "kept" : "DISCARDED");

  driver.stop();
  return 0;
}
