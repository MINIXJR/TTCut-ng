/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TTCut-ng - gui/ttcutmainwindow_headless.cpp                               */
/* The two headless modes of the main window: --auto-cut (cut a project and */
/* exit, used by the QC gates) and --screenshots (walk every dialog for the  */
/* documentation images). Both are entered from gui/ttcutmain.cpp only.      */

#include <QtGui>
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

void TTCutMainWindow::runAutoCutMode(QString projectFile, QString outputPath)
{
  if (TTSettings::instance()->logUI())
      qDebug() << "Auto-cut: loading project" << projectFile;
  // Headless: no modal dialogs (burst warning would block forever)
  mpAVData->setNonInteractive(true);
  openProjectFile(projectFile);

  QElapsedTimer timer;
  timer.start();
  while (mpAVData->avCount() == 0 && timer.elapsed() < 60000) {
    QApplication::processEvents();
    QThread::msleep(100);
  }
  if (mpAVData->avCount() == 0) {
    qWarning() << "Auto-cut: project failed to load within 60s";
    QApplication::quit();
    return;
  }

  // Audio streams load asynchronously after the video stream — wait for them.
  QThread::msleep(2000);
  QApplication::processEvents();

  TTCutList* cutData = mpAVData->cutList();
  if (cutData == 0 || cutData->count() == 0) {
    qWarning() << "Auto-cut: no cut entries in project";
    QApplication::quit();
    return;
  }

  QFileInfo outFI(outputPath);
  TTSettings::instance()->setCutDirPath(outFI.absolutePath());
  TTSettings::instance()->setCutVideoName(outFI.completeBaseName());

  if (mpCurrentAVDataItem && mpCurrentAVDataItem->videoStream()) {
    TTAVTypes::AVStreamType streamType = mpCurrentAVDataItem->videoStream()->streamType();
    if (streamType == TTAVTypes::h264_video)      TTSettings::instance()->setEncoderCodec(1);
    else if (streamType == TTAVTypes::h265_video) TTSettings::instance()->setEncoderCodec(2);
    else                                          TTSettings::instance()->setEncoderCodec(0);
  }

  if (TTSettings::instance()->logUI())
      qDebug() << "Auto-cut: cutting" << cutData->count() << "segments to" << outputPath;

  connect(mpAVData, &TTAVData::cutFinished, &QApplication::quit);
  mpAVData->onDoCut(QFileInfo(QDir(outFI.absolutePath()), outFI.completeBaseName()).absoluteFilePath(),
                    cutData, false);
}

void TTCutMainWindow::runScreenshotMode()
{
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
    if (!outDir.exists()) outDir.mkpath(".");

    // Load project
    openProjectFile(screenshotProject);

    // Wait for project to load
    QElapsedTimer timer;
    timer.start();
    while (mpAVData->avCount() == 0 && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QThread::msleep(100);
    }
    // Wait for audio streams
    QThread::msleep(2000);
    QApplication::processEvents();

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
    timer.restart();
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
        QStackedWidget* pages = settingsDlg.findChild<QStackedWidget*>("stackedPages");
        if (catList && pages) {
            // Must match the category order in TTCutSettingsDlg::TTCutSettingsDlg
            // Must match the category order in TTCutSettingsDlg's constructor.
            QStringList catNames = {"navigation", "search", "audio", "encoder",
                                    "muxer", "paths", "logging", "streampoints"};
            for (int i = 0; i < catList->count() && i < catNames.size(); ++i) {
                catList->setCurrentRow(i);
                QApplication::processEvents();
                saveWidgetScreenshot(&settingsDlg,
                    QString("ttcutng-settings-%1.png").arg(catNames[i]), 0);
            }
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
            QStringList tabNames = {"common", "encoding"};
            for (int i = 0; i < cutTab->count() && i < tabNames.size(); ++i) {
                cutTab->setCurrentIndex(i);
                QApplication::processEvents();
                saveWidgetScreenshot(&cutDlg,
                    QString("ttcutng-cutdlg-%1.png").arg(tabNames[i]), 0);
            }
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
