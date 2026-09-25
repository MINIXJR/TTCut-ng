/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TTCut-ng - gui/ttcutmainwindow_headless.cpp                               */
/* The two headless modes of the main window: --auto-cut (cut a project and */
/* exit, used by the QC gates) and --screenshots (walk every dialog for the  */
/* documentation images). Both are entered from gui/ttcutmain.cpp only.      */

#include <QtGui>
#include <functional>
#include <QApplication>
#include <QPixmap>
#include <QDebug>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QFileInfo>
#include <QThreadPool>
#include "ttcutmainwindow.h"
#include "ttcutavcutdlg.h"
#include "ttquickjumpdialog.h"
#include "ttstreampointwidget.h"
#include "ttaudiorepairdialog.h"
#include "../common/ttexception.h"
#include "../common/ttthreadtask.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/ttsettings.h"
#include "../data/ttstreampointmodel.h"
#include "../data/ttstreampoint_videoworker.h"
#include "../data/ttstreampoint_audioworker.h"
#include "../data/ttsearchtask.h"
#include "../data/ttsearchtask_blackframe.h"
#include "../data/ttsearchtask_scenechange.h"
#include "../data/ttsearchtask_logo.h"
#include "../data/ttsearchtask_aspectscan.h"
#include "../data/ttaudioanomalyscantask.h"
#include "ttcutavcutdlg.h"
#include "ttcutsettingsdlg.h"
#include "ttprogressbar.h"
#include "ttcutaboutdlg.h"
#include "ttgotoframedialog.h"
#include "ttwindowgeometry.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttlogodetector.h"
#include "../mpeg2window/ttmpeg2window2.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttaudioheaderlist.h"

/* /////////////////////////////////////////////////////////////////////////////
 * Screenshot mode: capture all widgets and dialogs, then exit
 */
void TTCutMainWindow::saveWidgetScreenshot(QWidget* widget, const QString& filename, int maxWidth)
{
  QPixmap pixmap = widget->grab();
  if (maxWidth > 0 && pixmap.width() > maxWidth) {
    pixmap = pixmap.scaledToWidth(maxWidth, Qt::SmoothTransformation);
  }
  QString path = QDir(TTSettings::instance()->screenshotDir()).filePath(filename);
  pixmap.save(path, "PNG");
  if (TTSettings::instance()->logUI())
    qDebug() << "Screenshot:" << path << pixmap.width() << "x" << pixmap.height();
}

bool TTCutMainWindow::waitForProjectLoad(int timeoutMs)
{
  // mProjectLoadInProgress falls when readProjectFileFinished/Aborted arrive,
  // i.e. after every open task - the audio and subtitle tracks included -
  // has left the pool. That replaces the former "avCount() > 0, then sleep
  // two seconds for the audio" guess (stream-open-project-load.md A3).
  QElapsedTimer timer;
  timer.start();
  while (mProjectLoadInProgress && timer.elapsed() < timeoutMs) {
    QApplication::processEvents();
    QThread::msleep(50);
  }
  QApplication::processEvents();
  return !mProjectLoadInProgress && mpAVData->avCount() > 0;
}

void TTCutMainWindow::runAutoCutMode(const QString& projectFile, const QString& outputPath)
{
  if (TTSettings::instance()->logUI())
    qDebug() << "Auto-cut: loading project" << projectFile;
  // Headless: no modal dialogs (burst warning would block forever)
  mpAVData->setNonInteractive(true);
  openProjectFile(projectFile);

  // Exit code for the caller: 0 only when the cut completed, 1 for every
  // run that ends without its output.
  if (!waitForProjectLoad(60000)) {
    if (mProjectLoadInProgress)
      qWarning() << "Auto-cut: project did not load within 60 s";
    else
      qWarning() << "Auto-cut: project failed to load";
    endAutoCut(1);
    return;
  }

  TTCutList* cutData = mpAVData->cutList();
  if (cutData == 0 || cutData->count() == 0) {
    qWarning() << "Auto-cut: no cut entries in project";
    endAutoCut(1);
    return;
  }

  TTSettings* settings = TTSettings::instance();
  if (mpCurrentAVDataItem && mpCurrentAVDataItem->videoStream()) {
  settings->setEncoderCodec(
    TTAVTypes::encoderCodecFor(mpCurrentAVDataItem->videoStream()->streamType()));
  }

  // The requested path decides container and place, the way the cut dialog's
  // getCommonData() turns its output field into pipeline settings. Without
  // this the run fell back to the codec's default muxer (mplex for MPEG-2),
  // wrote the intermediate ES without an extension and put the .mpg into
  // muxOutputPath() - the home directory unless the dialog's directory
  // button had ever been used. Set after setEncoderCodec(), which resets
  // the working container to the codec default.
  QFileInfo outFI(outputPath);
  const QString ext = outFI.suffix().toLower();
  const int container = (ext == QLatin1String("mkv")) ? 1
                      : (ext == QLatin1String("mpg")) ? 0 : -1;
  if (container < 0 || (container == 0 && settings->encoderCodec() != 0)) {
    qWarning() << "Auto-cut: output must end in .mkv (or .mpg for MPEG-2):" << outputPath;
    endAutoCut(1);
    return;
  }
  settings->setWorkingOutputContainer(container);
  settings->setCutDirPath(outFI.absolutePath());
  settings->setMuxOutputPath(outFI.absolutePath());
  settings->setCutVideoName(TTCutAVCutDlg::stripKnownExtension(outFI.fileName()) + "."
      + TTCutAVCutDlg::expectedEsExtension(container, settings->encoderCodec()));

  if (settings->logUI())
    qDebug() << "Auto-cut: cutting" << cutData->count() << "segments to" << outputPath
             << "container" << container << "ES" << settings->cutVideoName();

  // finishCutOperation() records lastCutError() before it emits
  // cutFinished(), so the outcome is final here.
  connect(mpAVData, &TTAVData::cutFinished, this, [this] {
    const QString error = mpAVData->lastCutError();
    if (error.isEmpty()) {
      qInfo() << "Auto-cut: cut complete";
      endAutoCut(0);
    } else {
      qWarning().noquote() << "Auto-cut: cut failed -" << error;
      endAutoCut(1);
    }
  });
  mpAVData->onDoCut(QFileInfo(QDir(settings->cutDirPath()), settings->cutVideoName()).absoluteFilePath(),
          cutData, false);
}

void TTCutMainWindow::endAutoCut(int exitCode)
{
  // Not QApplication::exit(): only quit() runs closeEvent, which saves the
  // settings and calls closeProject() - that aborts the stream-point tasks
  // and waits for the global pool (the automatic anomaly scan may still be
  // on it) before anything is destroyed.
  // Nobody can answer closeEvent's "Save changes?" here, and the dialog
  // would hold the run forever. --auto-cut changes nothing worth saving.
  // (A project whose video never opened no longer stays loaded and modified
  // since 2026-09-24 - onOpenProjectFileAborted closes it - but whether a
  // completed cut marks the project modified is not measured, so the guard
  // stays.)
  setProjectModified(false);
  mHeadlessExitCode = exitCode;
  QApplication::quit();
}

void TTCutMainWindow::runScreenshotMode()
{
  // One screenshot per page of a dialog: select(i) switches to page i.
  auto captureEachPage = [](QWidget& dlg, int pageCount, const QStringList& names,
                const QString& filePattern, const std::function<void(int)>& select) {
    for (int i = 0; i < pageCount && i < names.size(); ++i) {
      select(i);
      QApplication::processEvents();
      saveWidgetScreenshot(&dlg, filePattern.arg(names[i]), 0);
    }
  };

  const QString screenshotProject = TTSettings::instance()->screenshotProject();
  if (screenshotProject.isEmpty()) {
    if (TTSettings::instance()->logUI())
      qDebug() << "Screenshot mode: no --project specified";
    QApplication::quit();
    return;
  }

  // Fixed window size for reproducible screenshots, independent of the screen
  // the run happens on and of whatever size the user last left the window at.
  // Anything much smaller clips the stream point settings tab: its layout has
  // to squeeze the rows below their minimum height, and the grab then shows
  // cut-off text and controls flattened to lines.
  resize(1920, 1080);
  QApplication::processEvents();

  QDir outDir(TTSettings::instance()->screenshotDir());
  if (!outDir.exists() && !outDir.mkpath(".")) {
    qWarning("Screenshot mode: cannot create %s", qPrintable(outDir.absolutePath()));
    QApplication::exit(1);
    return;
  }

  // Load project (video and audio tracks alike, see waitForProjectLoad)
  openProjectFile(screenshotProject);
  if (!waitForProjectLoad(30000))
    qWarning("Screenshot mode: project did not load within 30 s");

  if (TTSettings::instance()->logUI())
    qDebug() << "Screenshot mode: project loaded, avCount=" << mpAVData->avCount();

  // 1. Main window
  saveWidgetScreenshot(this, "ttcutng-main.png", 1200);

  // 2. Both frames (CutOut + Current) — grab parent widget containing both
  QWidget* framesParent = cutOutFrame->parentWidget();
  if (framesParent)
    saveWidgetScreenshot(framesParent, "ttcutng-frames.png", 1200);

  // 3. Navigation panel
  saveWidgetScreenshot(navigation, "ttcutng-nav-panel.png", 0);

  // 4. Cut list
  saveWidgetScreenshot(cutList, "ttcutng-cutlist-detail.png", 1200);

  // 5. Stream navigator / controls
  saveWidgetScreenshot(streamNavigator, "ttcutng-controls.png", 1200);

  // 6. Landezonen: run analysis and wait for results
  onAnalyzeStreamPoints();
  QElapsedTimer timer;
  timer.start();
  while (mStreamPointWorkersRunning > 0 && timer.elapsed() < 60000) {
    QApplication::processEvents();
    QThread::msleep(100);
  }
  QApplication::processEvents();
  QThread::msleep(500);
  QApplication::processEvents();

  saveWidgetScreenshot(mpStreamPointWidget, "ttcutng-landezonen.png", 0);

  // The settings used to be a second tab here and were captured separately.
  // They are a category in the settings dialog now, so they arrive with the
  // per-category captures further down.

  // 9. Zeitsprung dialog (non-modal for screenshot)
  if (mpCurrentAVDataItem && mpCurrentAVDataItem->videoStream()) {
    TTQuickJumpDialog zeitsprungDlg(mpCurrentAVDataItem->videoStream(),
                                         mpCurrentAVDataItem->videoStream()->currentIndex(), this);
    zeitsprungDlg.show();
    QThread::msleep(5000);
    QApplication::processEvents();
    saveWidgetScreenshot(&zeitsprungDlg, "ttcutng-zeitsprung.png", 1200);
    zeitsprungDlg.close();
  }

  // 10. Settings dialog — one screenshot per category
  {
    TTCutSettingsDlg settingsDlg(this);
    settingsDlg.show();
    QApplication::processEvents();

    QListWidget* catList = settingsDlg.findChild<QListWidget*>("categoryList");
    const QStackedWidget* pages = settingsDlg.findChild<QStackedWidget*>("stackedPages");
    if (catList && pages) {
      // Must match the category order in TTCutSettingsDlg's constructor.
      const QStringList catNames = {"navigation", "search", "audio", "encoder",
                      "muxer", "paths", "logging", "streampoints"};
      captureEachPage(settingsDlg, catList->count(), catNames, "ttcutng-settings-%1.png",
              [catList](int i) { catList->setCurrentRow(i); });
    } else {
      saveWidgetScreenshot(&settingsDlg, "ttcutng-settings.png", 0);
    }
    settingsDlg.close();
  }

  // 11. Cut dialog (AV Cut) — one screenshot per tab
  {
    TTCutAVCutDlg cutDlg(this);
    cutDlg.show();
    QApplication::processEvents();

    QTabWidget* cutTab = cutDlg.findChild<QTabWidget*>("tabWidget");
    if (cutTab) {
      const QStringList tabNames = {"common", "encoding"};
      captureEachPage(cutDlg, cutTab->count(), tabNames, "ttcutng-cutdlg-%1.png",
              [cutTab](int i) { cutTab->setCurrentIndex(i); });
    } else {
      saveWidgetScreenshot(&cutDlg, "ttcutng-cutdlg.png", 0);
    }
    cutDlg.close();
  }

  // 12. About dialog (non-modal for screenshot)
  {
    TTCutAboutDlg aboutDlg(this);
    aboutDlg.show();
    QApplication::processEvents();
    saveWidgetScreenshot(&aboutDlg, "ttcutng-about.png", 0);
    aboutDlg.close();
  }

  // 13. Stream Integrity Warning dialog (simulated decode errors)
  {
    QString warnMsg = tr("%1 decode errors detected in %2 region(s) during demux.\n\n"
                             "This MPEG-2 stream has defective GOPs that may cause A/V sync issues.\n"
                             "Recommendation: demux the recording again with the current "
                             "ttcut-demux - it finds and repairs such gaps.")
              .arg(333).arg(7);
    warnMsg += "\n\n" + tr("Affected regions:");
    warnMsg += "\n  ~Frame 0 (00:00:00.00): 1 " + tr("errors");
    warnMsg += "\n  ~Frame 10645 (00:07:05): 105 " + tr("errors");
    warnMsg += "\n  ~Frame 22220 (00:14:48): 6 " + tr("errors");
    warnMsg += "\n  ~Frame 34220 (00:22:48): 12 " + tr("errors");
    warnMsg += "\n  ~Frame 46803 (00:31:12): 81 " + tr("errors");
    warnMsg += "\n  ~Frame 57597 (00:38:23): 57 " + tr("errors");
    warnMsg += "\n  ~Frame 72384 (00:48:15): 71 " + tr("errors");

    QMessageBox msgBox(QMessageBox::Warning,
                           tr("Stream Integrity Warning"),
                           warnMsg, QMessageBox::NoButton, this);
    msgBox.addButton(tr("Import as Stream Points"), QMessageBox::AcceptRole);
    QPushButton* okBtn = msgBox.addButton(QMessageBox::Ok);
    // Two AcceptRole buttons leave QMessageBox without an escape button;
    // without this the msgBox.close() below is silently ignored.
    msgBox.setEscapeButton(okBtn);
    msgBox.show();
    QApplication::processEvents();
    saveWidgetScreenshot(&msgBox, "ttcutng-integrity-warning.png", 0);
    msgBox.close();
  }

  // 14. Goto frame/timecode dialog (opened by clicking the position display).
  // Uses the loaded stream when available so frame count and timecode match
  // the recording shown in the other screenshots; falls back to plausible
  // values otherwise, so the shot is produced either way.
  {
    int   curFrame  = 0;
    int   frameCnt  = 90000;
    float frameRate = 25.0f;
    if (mpCurrentAVDataItem && mpCurrentAVDataItem->videoStream()) {
      TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
      if (vs->frameCount() > 0) {
        frameCnt  = static_cast<int>(vs->frameCount());
        curFrame  = qMin(vs->currentIndex(), frameCnt - 1);
        if (vs->frameRate() > 0) frameRate = vs->frameRate();
      }
    }
    // Frame 0 would show 00:00:00.000 in both fields, which says nothing
    // about how the two stay in sync — put the shot mid-recording instead.
    if (curFrame == 0) curFrame = frameCnt / 2;
    TTGotoFrameDialog gotoDlg(curFrame, frameCnt, frameRate, this);
    gotoDlg.show();
    QApplication::processEvents();
    saveWidgetScreenshot(&gotoDlg, "ttcutng-goto-frame.png", 0);
    gotoDlg.close();
  }

  // 15. Cut completion dialog (simulated durations — the real one appears
  // only after a finished cut). Text kept identical to onCutFinished().
  {
    // cutVideoName() is empty until a cut has run, which would leave the
    // dialog showing a bare directory — name the file for the shot.
    QString cutName = TTSettings::instance()->cutVideoName();
    if (cutName.isEmpty()) cutName = "Recording_cut.mkv";
    QString outputFile = QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
                                       cutName).absoluteFilePath();
    const qint64 srcMs = 5400000;   // 1:30:00
    const qint64 resMs = 2535000;   // 42:15
    QString lengths = tr("\n\nSource:  %1\nResult:  %2  (%3 removed)")
      .arg(formatDurationMs(srcMs), formatDurationMs(resMs),
                 formatDurationMs(srcMs - resMs));

    QMessageBox doneBox(QMessageBox::Information,
              tr("Cutting Complete"),
              tr("Video cutting has finished successfully.\n\nOutput file:\n%1")
                .arg(outputFile) + lengths,
              QMessageBox::Ok, this);
    doneBox.show();
    QApplication::processEvents();
    saveWidgetScreenshot(&doneBox, "ttcutng-cut-complete.png", 0);
    doneBox.close();
  }

  // 16. Copy main window as docs/MainWindow.png
  QString docsPath = QFileInfo(QApplication::applicationDirPath() + "/../docs/MainWindow.png").absoluteFilePath();
  QFile::remove(docsPath);
  QFile::copy(outDir.filePath("ttcutng-main.png"), docsPath);

  if (TTSettings::instance()->logUI())
    qDebug() << "Screenshot mode complete:" << outDir.absolutePath();
  QApplication::quit();
}
