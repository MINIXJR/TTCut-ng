/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTMAINWINDOW
// ----------------------------------------------------------------------------

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

#include "ttcutavcutdlg.h"
#include "ttcutsettingsdlg.h"
#include "ttprogressbar.h"
#include "ttcutaboutdlg.h"
#include "ttgotoframedialog.h"
#include "ttwindowgeometry.h"

#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttlogodetector.h"

// TTMPEG2Window2 is the preview window type (videoWindow(), logo ROI signal)
#include "../mpeg2window/ttmpeg2window2.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttaudioheaderlist.h"

#include "../ui//pixmaps/downarrow_18.xpm"
#include "../ui/pixmaps/uparrow_18.xpm"
#include "../ui/pixmaps/cancel_18.xpm"
#include "../ui/pixmaps/fileopen_24.xpm"
#include "../ui/pixmaps/filenew_16.xpm"
#include "../ui/pixmaps/fileopen_16.xpm"
#include "../ui/pixmaps/filesave_16.xpm"
#include "../ui/pixmaps/filesaveas_16.xpm"
#include "../ui/pixmaps/saveimage_16.xpm"
#include "../ui/pixmaps/settings_16.xpm"
#include "../ui/pixmaps/settings_18.xpm"
#include "../ui/pixmaps/exit_16.xpm"
#include "../ui/pixmaps/play_18.xpm"
#include "../ui/pixmaps/stop_18.xpm"
#include "../ui/pixmaps/search_18.xpm"
#include "../ui/pixmaps/preview_18.xpm"
#include "../ui/pixmaps/cutav_18.xpm"
#include "../ui/pixmaps/cutaudio_18.xpm"
#include "../ui/pixmaps/goto_18.xpm"
#include "../ui/pixmaps/note_18.xpm"
#include "../ui/pixmaps/clock_16.xpm"
#include "../ui/pixmaps/apply_18.xpm"
#include "../ui/pixmaps/addtolist_18.xpm"
#include "../ui/pixmaps/fileclose_18.xpm"

#include <QStringList>
#include <QString>

/* /////////////////////////////////////////////////////////////////////////////
 * Application main window constructor
 */
TTCutMainWindow::TTCutMainWindow()
: QMainWindow()
{
  // Register metatype for cross-thread signal/slot
  qRegisterMetaType<QList<TTStreamPoint>>("QList<TTStreamPoint>");
  qRegisterMetaType<QList<float>>("QList<float>");

  mProjectModified = false;

  // setup Qt Designer UI
  setupUi( this );

  // images
  // --------------------------------------------------------------------------
  TTCut::imgDownArrow  = new QPixmap( downarrow_18_xpm );
  TTCut::imgUpArrow    = new QPixmap( uparrow_18_xpm );
  TTCut::imgDelete     = new QPixmap( cancel_18_xpm );
  TTCut::imgFileOpen24 = new QPixmap( fileopen_24_xpm );
  TTCut::imgFileNew    = new QPixmap( filenew_16_xpm );
  TTCut::imgFileOpen   = new QPixmap( fileopen_16_xpm );
  TTCut::imgFileSave   = new QPixmap( filesave_16_xpm );;
  TTCut::imgFileSaveAs = new QPixmap( filesaveas_16_xpm );
  TTCut::imgSaveImage  = new QPixmap( saveimage_16_xpm );
  TTCut::imgSettings   = new QPixmap( settings_16_xpm );
  TTCut::imgSettings18 = new QPixmap( settings_18_xpm );
  TTCut::imgExit       = new QPixmap( exit_16_xpm );
  TTCut::imgPlay       = new QPixmap( play_18_xpm );
  TTCut::imgStop       = new QPixmap( stop_18_xpm );
  TTCut::imgSearch     = new QPixmap( search_18_xpm );
  TTCut::imgPreview    = new QPixmap( preview_18_xpm );
  TTCut::imgCutAV      = new QPixmap( cutav_18_xpm );
  TTCut::imgCutAudio   = new QPixmap( cutaudio_18_xpm );
  TTCut::imgGoTo       = new QPixmap( goto_18_xpm );
  TTCut::imgMarker     = new QPixmap( note_18_xpm );
  TTCut::imgClock      = new QPixmap( clock_16_xpm );
  TTCut::imgApply      = new QPixmap( apply_18_xpm );
  TTCut::imgAddToList  = new QPixmap( addtolist_18_xpm );
  TTCut::imgFileClose  = new QPixmap( fileclose_18_xpm );

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  actionFileNew->setIcon(QIcon::fromTheme("document-new", style->standardIcon(QStyle::SP_FileIcon)));
  actionFileOpen->setIcon(QIcon::fromTheme("document-open", style->standardIcon(QStyle::SP_DialogOpenButton)));
  actionFileSave->setIcon(QIcon::fromTheme("document-save", style->standardIcon(QStyle::SP_DialogSaveButton)));
  actionFileSaveAs->setIcon(QIcon::fromTheme("document-save-as", style->standardIcon(QStyle::SP_DialogSaveButton)));
  actionExit->setIcon(QIcon::fromTheme("application-exit", style->standardIcon(QStyle::SP_DialogCloseButton)));
  actionOpenVideo->setIcon(QIcon::fromTheme("video-x-generic", style->standardIcon(QStyle::SP_DriveDVDIcon)));
  actionOpenAudio->setIcon(QIcon::fromTheme("audio-x-generic", style->standardIcon(QStyle::SP_DriveCDIcon)));
  actionOpenSubtitle->setIcon(QIcon::fromTheme("text-x-generic", style->standardIcon(QStyle::SP_FileDialogContentsView)));
  actionSaveCurrentFrame->setIcon(QIcon::fromTheme("image-x-generic", style->standardIcon(QStyle::SP_DesktopIcon)));
  actionSettings->setIcon(QIcon::fromTheme("preferences-system", style->standardIcon(QStyle::SP_ComputerIcon)));
  actionAbout->setIcon(QIcon::fromTheme("help-about", style->standardIcon(QStyle::SP_MessageBoxInformation)));

  setFocusPolicy(Qt::StrongFocus);

  // Message logger instance
  log = TTMessageLogger::getInstance();

  // Get the current Qt version at runtime
  log->infoMsg(__FILE__, __LINE__, QString("TTCut-Version: %1").arg(TTCut::versionString));
  log->infoMsg(__FILE__, __LINE__, QString("Qt-Version:    %1").arg(qVersion()));

  // Settings
  TTSettings::instance()->setRecentFileList(QStringList{});
  TTSettings::instance()->load();

  // Initialize navigation spinboxes from saved settings
  navigation->setThresholds(TTSettings::instance()->navBlackThreshold(), TTSettings::instance()->navSceneThreshold());

  // Restore window geometry or default to 80% of screen.
  // QSettings here is the per-window UI-state persistence — outside
  // Phase B scope (TTSettings owns app settings, not window geometry).
  QSettings geom("TTCut-ng", "TTCut-ng");
  // Both one-time upgrades run here, on the first start of this version —
  // not when some dialog happens to be opened. No-ops afterwards.
  ttMigrateGeometryBlob(geom, "MainWindow");
  ttImportStrayQuickJumpSize(geom);

  bool restored = false;
  const TTWindowGeometry saved = ttLoadWindowGeometry(geom, "MainWindow");
  if (saved.valid) {
    // Find the screen the saved window belongs to, then keep it inside that
    // screen's work area. This replaces what the old blob did with its stored
    // screen width.
    const QPoint center = saved.rect.center();
    for (QScreen* s : QGuiApplication::screens()) {
      if (!s->availableGeometry().contains(center)) continue;
      setGeometry(ttClampToArea(saved.rect, s->availableGeometry()));
      if (saved.maximized) showMaximized();
      restored = true;
      if (TTSettings::instance()->logUI())
        qDebug() << "geometry restore: stored" << saved.rect
                 << "maximized" << saved.maximized
                 << "-> geometry" << geometry()
                 << "normalGeometry" << normalGeometry();
      break;
    }
  }
  if (!restored) {
    QRect screenGeom = QGuiApplication::primaryScreen()->availableGeometry();
    int w = screenGeom.width() * 80 / 100;
    int h = screenGeom.height() * 80 / 100;
    setGeometry(
      screenGeom.x() + (screenGeom.width() - w) / 2,
      screenGeom.y() + (screenGeom.height() - h) / 2,
      w, h);
  }

  log->enableLogFile(TTSettings::instance()->createLogFile());
  log->setLogModeConsole(TTSettings::instance()->logModeConsole());
  log->setLogModeExtended(TTSettings::instance()->logModeExtended());

  //AV stream controller instance
  mpAVData = new TTAVData();
  mpPreviewOriginalCutList = nullptr;

  videoFileList->setAVData(mpAVData);
  cutList->setAVData(mpAVData);

  // Stream point model and widget
  mpStreamPointModel = new TTStreamPointModel(this);
  mpStreamPointWidget = new TTStreamPointWidget(mpStreamPointModel, this);
  mpStreamPointTaskPool = new TTThreadTaskPool();
  connect(mpStreamPointTaskPool, &TTThreadTaskPool::statusReport,
          this, &TTCutMainWindow::onStatusReport);
  mStreamPointWorkersRunning = 0;
  mLogoDetector = new TTLogoDetector();

  // Add stream point widget below navigation in the Navigation GroupBox
  QGridLayout* navLayout = qobject_cast<QGridLayout*>(gbNavigation->layout());
  if (navLayout) {
    navLayout->addWidget(mpStreamPointWidget, 1, 0);
  }

  // no navigation
  navigationEnabled( false );

  // init
  mpCurrentAVDataItem     = 0;
  progressBar            = 0;
  TTSettings::instance()->setProjectFileName("");

  // Signal and slot connections
  //
  // Connect signals from main menu
  // --------------------------------------------------------------------------
  connect(actionOpenVideo,         &QAction::triggered, this, &TTCutMainWindow::onOpenVideoFile);
  connect(actionOpenAudio,         &QAction::triggered, this, &TTCutMainWindow::onOpenAudioFile);
  connect(actionOpenSubtitle,      &QAction::triggered, this, &TTCutMainWindow::onOpenSubtitleFile);
  connect(actionFileNew,           &QAction::triggered, this, &TTCutMainWindow::onFileNew);
  connect(actionFileOpen,          &QAction::triggered, this, &TTCutMainWindow::onFileOpen);
  connect(actionFileSave,          &QAction::triggered, this, &TTCutMainWindow::onFileSave);
  connect(actionFileSaveAs,        &QAction::triggered, this, &TTCutMainWindow::onFileSaveAs);
  connect(actionExit,              &QAction::triggered, this, &TTCutMainWindow::onFileExit);
  connect(actionSaveCurrentFrame,  &QAction::triggered, this, &TTCutMainWindow::onActionSave);
  connect(actionSettings,          &QAction::triggered, this, &TTCutMainWindow::onActionSettings);
  connect(actionAbout,             &QAction::triggered, this, &TTCutMainWindow::onHelpAbout);
  connect(actionKeyboardShortcuts, &QAction::triggered, this, &TTCutMainWindow::onHelpKeyboardShortcuts);

  // recent files
  for (int i = 0; i < MaxRecentFiles; ++i) {
    recentFileAction[i] = new QAction(this);
    recentFileAction[i]->setVisible(false);
    menuRecentProjects->addAction(recentFileAction[i]);
    connect(recentFileAction[i], &QAction::triggered, this, &TTCutMainWindow::onFileRecent);
  }

  updateRecentFileActions();
  connect(mpAVData, qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
          this, &TTCutMainWindow::onStatusReport);

  mEstimatorClock.start();
  mpProgressEstimator = new TTProgressEstimator(
      &mCalibStore, [this]() { return mEstimatorClock.elapsed(); });

  connect(mpAVData, &TTAVData::operationPlanReady,
          this, [this](const QVector<TTStagePlan>& plan) {
            mpProgressEstimator->setPlan(plan);
          });

  connect(videoFileList,    &TTVideoTreeView::openFile,    this, &TTCutMainWindow::onOpenVideoFile);
  connect(audioFileList,    &TTAudioTreeView::openFile,    this, &TTCutMainWindow::onOpenAudioFile);
  connect(subtitleFileList, &TTSubtitleTreeView::openFile, this, &TTCutMainWindow::onOpenSubtitleFile);

  // Connect signals from navigation widget
  // --------------------------------------------------------------------------
  connect(navigation, &TTCutFrameNavigation::prevIFrame,         currentFrame, &TTCurrentFrame::onPrevIFrame);
  connect(navigation, &TTCutFrameNavigation::nextIFrame,         currentFrame, &TTCurrentFrame::onNextIFrame);
  connect(navigation, &TTCutFrameNavigation::prevPFrame,         currentFrame, &TTCurrentFrame::onPrevPFrame);
  connect(navigation, &TTCutFrameNavigation::nextPFrame,         currentFrame, &TTCurrentFrame::onNextPFrame);
  connect(navigation, &TTCutFrameNavigation::prevBFrame,         currentFrame, &TTCurrentFrame::onPrevBFrame);
  connect(navigation, &TTCutFrameNavigation::nextBFrame,         currentFrame, &TTCurrentFrame::onNextBFrame);
  connect(navigation, &TTCutFrameNavigation::setCutOut,          this, &TTCutMainWindow::onSetCutOut);
  connect(navigation, &TTCutFrameNavigation::searchBlackFrame,   this, &TTCutMainWindow::onSearchBlackFrame);
  connect(navigation, &TTCutFrameNavigation::abortBlackSearch,   this, &TTCutMainWindow::onAbortBlackSearch);
  connect(navigation, &TTCutFrameNavigation::searchSceneChange,  this, &TTCutMainWindow::onSearchSceneChange);
  connect(navigation, &TTCutFrameNavigation::abortSceneSearch,   this, &TTCutMainWindow::onAbortSceneSearch);

  connect(navigation, &TTCutFrameNavigation::selectLogoROI,  this, &TTCutMainWindow::onSelectLogoROI);
  connect(navigation, &TTCutFrameNavigation::cancelLogoROI,  this, &TTCutMainWindow::onCancelLogoROI);
  connect(navigation, &TTCutFrameNavigation::loadLogoFile,   this, &TTCutMainWindow::onLoadLogoFile);
  connect(navigation, &TTCutFrameNavigation::searchLogo,     this, &TTCutMainWindow::onSearchLogo);
  connect(navigation, &TTCutFrameNavigation::abortLogoSearch, this, &TTCutMainWindow::onAbortLogoSearch);
  connect(currentFrame->videoWindow(), &TTMPEG2Window2::logoROISelected, this, &TTCutMainWindow::onLogoROISelected);

  connect(navigation, &TTCutFrameNavigation::setCutOut, cutOutFrame, &TTCutOutFrame::onGotoCutOut);

  connect(navigation, &TTCutFrameNavigation::gotoCutIn,     currentFrame, &TTCurrentFrame::onGotoCutIn);
  connect(navigation, &TTCutFrameNavigation::gotoCutOut,    currentFrame, &TTCurrentFrame::onGotoCutOut);
  connect(navigation, &TTCutFrameNavigation::addCutRange,   this,         &TTCutMainWindow::onAppendCutEntry);
  connect(navigation, &TTCutFrameNavigation::moveNumSteps,  currentFrame, &TTCurrentFrame::onMoveNumSteps);
  connect(navigation, &TTCutFrameNavigation::moveToHome,    currentFrame, &TTCurrentFrame::onMoveToHome);
  connect(navigation, &TTCutFrameNavigation::moveToEnd,     currentFrame, &TTCurrentFrame::onMoveToEnd);
  connect(navigation, &TTCutFrameNavigation::openQuickJump, this,         &TTCutMainWindow::onQuickJump);

  // Stream point widget signals
  connect(mpStreamPointWidget, &TTStreamPointWidget::analyzeRequested,    this, &TTCutMainWindow::onAnalyzeStreamPoints);
  connect(mpStreamPointWidget, &TTStreamPointWidget::abortRequested,      this, &TTCutMainWindow::onAbortStreamPoints);
  connect(mpStreamPointWidget, &TTStreamPointWidget::settingsRequested,   this, &TTCutMainWindow::onStreamPointSettingsRequested);
  connect(mpStreamPointWidget, &TTStreamPointWidget::jumpToFrame,         this, &TTCutMainWindow::onStreamPointJump);
  connect(mpStreamPointWidget, &TTStreamPointWidget::deleteRequested,     this, &TTCutMainWindow::onStreamPointDelete);
  connect(mpStreamPointWidget, &TTStreamPointWidget::deleteAllRequested,  this, &TTCutMainWindow::onStreamPointDeleteAll);
  connect(mpStreamPointWidget, &TTStreamPointWidget::setCutIn,            this, &TTCutMainWindow::onStreamPointSetCutIn);
  connect(mpStreamPointWidget, &TTStreamPointWidget::setCutOut,           this, &TTCutMainWindow::onStreamPointSetCutOut);

  // Connect signal from video slider
  // --------------------------------------------------------------------------
  connect(streamNavigator, &TTStreamNavigator::sliderValueChanged, this, &TTCutMainWindow::onVideoSliderChanged);
  // Debounced decode for the slider; fires 50 ms after the last movement.
  // A release must not wait for the debounce: deliver the exact frame at once.
  mpSliderDebounce = new QTimer(this);
  mpSliderDebounce->setSingleShot(true);
  mpSliderDebounce->setInterval(50);
  connect(mpSliderDebounce, &QTimer::timeout, this, &TTCutMainWindow::onSliderDecodeTimer);
  connect(streamNavigator->slider(), &QAbstractSlider::sliderReleased,
          this, &TTCutMainWindow::onSliderDecodeTimer);

  // Connect signals from cut-out frame widget
  // --------------------------------------------------------------------------
  connect(cutOutFrame, &TTCutOutFrame::searchEqualFrame, mpAVData, &TTAVData::onDoFrameSearch);

  // Connect signals from current frame widget
  // --------------------------------------------------------------------------
  connect(currentFrame, &TTCurrentFrame::newFramePosition, this,     &TTCutMainWindow::onNewFramePos);
  connect(currentFrame, &TTCurrentFrame::newFramePosition, mpAVData, &TTAVData::onCurrentFramePositionChanged);
  // "Set marker" button now handled via navigation signal → onSetMarker adds to model

  // Connect signals from cut list widget
  // --------------------------------------------------------------------------
  connect(cutList, &TTCutTreeView::selectionChanged, this,            &TTCutMainWindow::onCutSelectionChanged);
  connect(cutList, &TTCutTreeView::entryEdit,        navigation,      &TTCutFrameNavigation::onEditCut);
  connect(cutList, &TTCutTreeView::gotoCutIn,        currentFrame,    qOverload<int>(&TTCurrentFrame::onGotoFrame));
  connect(cutList, &TTCutTreeView::gotoCutOut,       currentFrame,    qOverload<int>(&TTCurrentFrame::onGotoFrame));
  connect(cutList, &TTCutTreeView::refreshDisplay,   streamNavigator, &TTStreamNavigator::onRefreshDisplay);
  connect(cutList, &TTCutTreeView::previewCut,       this,            &TTCutMainWindow::onCutPreview);
  connect(cutList, &TTCutTreeView::audioVideoCut,    this,            &TTCutMainWindow::onAudioVideoCut);
  connect(cutList, &TTCutTreeView::itemUpdated,      cutOutFrame,     &TTCutOutFrame::onCutOutChanged);

  // Navigation "Set marker" → add manual marker to stream point model
  connect(navigation,   &TTCutFrameNavigation::setMarker, this, &TTCutMainWindow::onSetStreamPointMarker);
  connect(currentFrame, &TTCurrentFrame::setMarker,       this, &TTCutMainWindow::onSetStreamPointMarker);

  connect(mpAVData, &TTAVData::currentAVItemChanged, this, &TTCutMainWindow::onAVItemChanged);
  connect(mpAVData, &TTAVData::avDataReloaded,       this, &TTCutMainWindow::onAVDataReloaded);
  connect(mpAVData, &TTAVData::foundEqualFrame,      currentFrame, qOverload<int>(&TTCurrentFrame::onGotoFrame));
  connect(mpAVData, &TTAVData::streamPointsLoaded,
          this, &TTCutMainWindow::onVideoPointsDetected);
  connect(mpAVData, &TTAVData::vdrMarkersLoaded,
          this, &TTCutMainWindow::onVideoPointsDetected);
  connect(mpAVData, &TTAVData::logoDataLoaded,
          this, &TTCutMainWindow::onLogoDataLoaded);

  // Dirty tracking: set mProjectModified on any data change
  connect(mpAVData, &TTAVData::cutItemAppended,    this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::cutItemRemoved,     this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::cutItemUpdated,     this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::cutOrderUpdated,    this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::avItemAppended,     this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::avItemRemoved,      this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::markerAppended,     this, &TTCutMainWindow::onProjectModified);
  connect(mpAVData, &TTAVData::markerRemoved,      this, &TTCutMainWindow::onProjectModified);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTCutMainWindow::~TTCutMainWindow()
{
  delete mpAVData;
  mpAVData = nullptr;
  delete mLogoDetector;
  mLogoDetector = nullptr;
  delete mpProgressEstimator;
  mpProgressEstimator = nullptr;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signals from the application menu
 */
void TTCutMainWindow::keyPressEvent(QKeyEvent* e)
{
  navigation->keyPressEvent(e);
}

/* //////////////////////////////////////////////////////////////////////////////
 * show video file open dialog
 */
void TTCutMainWindow::onOpenVideoFile()
{
  QString fn = QFileDialog::getOpenFileName( this,
      tr("Open video file"),
      TTSettings::instance()->lastDirPath(),
      tr("All Video ES (*.m2v *.mpv *.264 *.h264 *.265 *.h265 *.hevc);;"
         "MPEG-2 Video (*.m2v *.mpv);;"
         "H.264/AVC (*.264 *.h264);;"
         "H.265/HEVC (*.265 *.h265 *.hevc);;"
         "All Files (*)"));

  if (fn.isEmpty()) return;

  QFileInfo fInfo( fn );
  TTSettings::instance()->setLastDirPath(fInfo.absolutePath());
  onReadVideoStream(fn);
}

/* //////////////////////////////////////////////////////////////////////////////
 * show audio file open dialog
 */
void TTCutMainWindow::onOpenAudioFile()
{
	if (mpAVData->avCount() == 0) return;

	QString fn = QFileDialog::getOpenFileName( this,
      tr("Open audio file"),
      TTSettings::instance()->lastDirPath(),
      tr("All Audio Files (*.mpa *.mp2 *.ac3 *.aac *.m4a *.eac3 *.dts);;"
         "MPEG Audio (*.mpa *.mp2);;"
         "AC3/Dolby Digital (*.ac3 *.eac3);;"
         "AAC Audio (*.aac *.m4a);;"
         "DTS Audio (*.dts);;"
         "All Files (*)"));

  if (fn.isEmpty())
    return;

  QFileInfo fInfo(fn);
  TTSettings::instance()->setLastDirPath(fInfo.absolutePath());
  onReadAudioStream(fn);
}

/* //////////////////////////////////////////////////////////////////////////////
 * show subtitle file open dialog
 */
void TTCutMainWindow::onOpenSubtitleFile()
{
  if (mpAVData->avCount() == 0) return;

  QString fn = QFileDialog::getOpenFileName( this,
      tr("Open subtitle file"),
      TTSettings::instance()->lastDirPath(),
      "Subtitle (*.srt)" );

  if (fn.isEmpty())
    return;

  QFileInfo fInfo(fn);
  TTSettings::instance()->setLastDirPath(fInfo.absolutePath());
  onReadSubtitleStream(fn);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "File new" action
 */
void TTCutMainWindow::onFileNew()
{
  if (mpAVData->avCount() == 0) return;

  // Warn user only if there are unsaved changes
  if (mProjectModified) {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("New Project"),
        tr("Close current project and start a new one?\nUnsaved changes will be lost."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes) return;
  }

  closeProject();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "File open" action
 */
void TTCutMainWindow::onFileOpen()
{
  QString fn = QFileDialog::getOpenFileName(this,
      tr("Open project-file"),
      TTSettings::instance()->lastDirPath(),
      "TTCut Project (*.ttcut);;Legacy Project (*.prj)");

  if (!fn.isEmpty()) {
    openProjectFile(fn);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "File save" action
 */
void TTCutMainWindow::onFileSave()
{
  if (mpAVData->avCount() == 0) return;

  // Ask for file name
  if (TTSettings::instance()->projectFileName().isEmpty())
  {
    QString prjName = ttChangeFileExt(mpCurrentAVDataItem->videoStream()->fileName(), "ttcut");
    TTSettings::instance()->setProjectFileName(prjName);
    QFileInfo prjFileInfo(QDir(TTSettings::instance()->lastDirPath()), prjName);

    QString chosen = QFileDialog::getSaveFileName(this,
        tr("Save project-file"),
        prjFileInfo.absoluteFilePath(),
        "TTCut Project (*.ttcut)");
    TTSettings::instance()->setProjectFileName(chosen);

    if (TTSettings::instance()->projectFileName().isEmpty()) return;
  }

  // append project file extension
  QFileInfo fInfo(TTSettings::instance()->projectFileName());

  if (fInfo.suffix().isEmpty())
    TTSettings::instance()->setProjectFileName(TTSettings::instance()->projectFileName() + ".ttcut");

  try
  {
    TTLogoProjectData logoData;
    if (mLogoDetector->hasProfile()) {
      logoData.valid = true;
      if (mLogoDetector->isFromMarkadLogo()) {
        logoData.isMarkad = true;
        logoData.markadPath = mLogoDetector->markadLogoPath();
      } else {
        logoData.isMarkad = false;
        logoData.roi = mLogoDetector->roi();
      }
    }
    mpAVData->writeProjectFile(fInfo, mpStreamPointModel->points(), logoData);
  }
  catch (const TTException& ex)
  {
    log->errorMsg(__FILE__, __LINE__, tr("error save project file: %1").arg(TTSettings::instance()->projectFileName()));
    return;
  }

  setProjectModified(false);
}


/* /////////////////////////////////////////////////////////////////////////////
 * Menu "File save as" action
 */
void TTCutMainWindow::onFileSaveAs()
{
  if (mpAVData->avCount() == 0) {
    return;
  }

  {
    QString prjName = ttChangeFileExt(mpCurrentAVDataItem->videoStream()->fileName(), "ttcut");
    TTSettings::instance()->setProjectFileName(prjName);
  }
  QFileInfo prjFileInfo(QDir(TTSettings::instance()->lastDirPath()), TTSettings::instance()->projectFileName());

  TTSettings::instance()->setProjectFileName(QFileDialog::getSaveFileName( this,
      tr("Save project-file as"),
      prjFileInfo.absoluteFilePath(),
      "TTCut Project (*.ttcut)" ));

  if (!TTSettings::instance()->projectFileName().isEmpty())
  {
    QFileInfo fInfo(TTSettings::instance()->projectFileName());
    TTSettings::instance()->setLastDirPath(fInfo.absolutePath());

    onFileSave();
  }
}


/* /////////////////////////////////////////////////////////////////////////////
 * Menu "Recent files..." action
 */
void TTCutMainWindow::onFileRecent()
{
  QAction* action = qobject_cast<QAction*>(sender());

  if (action)
    openProjectFile(action->data().toString());
}


/* /////////////////////////////////////////////////////////////////////////////
 * Menu "Exit" action
 */
void TTCutMainWindow::onFileExit()
{
  close();

  qApp->quit();
}

/* /////////////////////////////////////////////////////////////////////////////
 * React to the application window close event
 * - save settings
 * - ask for saving changes
 * - close the project
 */
void TTCutMainWindow::closeEvent(QCloseEvent* event)
{
  // Window geometry persistence — outside Phase B scope (TTSettings owns
  // app settings, not per-window UI state). normalGeometry() is the
  // un-maximised rectangle, so a maximised window still records a sensible
  // size to come back to.
  QSettings geom("TTCut-ng", "TTCut-ng");
  if (TTSettings::instance()->logUI())
    qDebug() << "geometry save: geometry" << geometry()
             << "normalGeometry" << normalGeometry()
             << "maximized" << isMaximized()
             << "minimumSize" << minimumSize();
  ttSaveWindowGeometry(geom, "MainWindow", normalGeometry(), isMaximized());

  TTSettings::instance()->save();

  if (mProjectModified) {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Exit"),
        tr("Save changes before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (reply == QMessageBox::Cancel) {
      event->ignore();
      return;
    }
    if (reply == QMessageBox::Save) {
      onFileSave();
    }
  }

  closeProject();

  // Don't delete mpAVData here — the destructor handles cleanup.
  // Doing it twice (closeEvent then ~TTCutMainWindow) is a double-free.

  event->accept();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "Save current frame" action
 */
void TTCutMainWindow::onActionSave()
{
  currentFrame->saveCurrentFrame();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "Settings" action
 */
void TTCutMainWindow::onActionSettings()
{
  openSettingsDialog(-1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * The settings button in the stream point panel
 */
void TTCutMainWindow::onStreamPointSettingsRequested()
{
  // 7 = "Stream Points", the eighth category added in TTSettingsDialog's
  // constructor. The dialog owns that order; this is the one place that has
  // to follow it.
  openSettingsDialog(7);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Settings dialog, optionally opened on a given sidebar category
 */
void TTCutMainWindow::openSettingsDialog(int category)
{
  TTCutSettingsDlg* settingsDlg = new TTCutSettingsDlg( this );
  if (category >= 0) {
    if (QListWidget* cats = settingsDlg->findChild<QListWidget*>("categoryList")) {
      if (category < cats->count()) cats->setCurrentRow(category);
    }
  }
  settingsDlg->exec();

  log->enableLogFile(TTSettings::instance()->createLogFile());
  log->setLogModeConsole(TTSettings::instance()->logModeConsole());
  log->setLogModeExtended(TTSettings::instance()->logModeExtended());

  TTSettings::instance()->save();

  // Burst filter setting may have changed - re-evaluate the hint column
  // (burst warnings and AC3 format-change hints share it)
  cutList->refreshHintIcons();

  delete settingsDlg;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "About" action
 */
void TTCutMainWindow::onHelpAbout()
{
  TTCutAboutDlg about(this);
  about.exec();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Menu "Keyboard Shortcuts" action
 */
void TTCutMainWindow::onHelpKeyboardShortcuts()
{
  QString shortcuts = tr(
    "<h3>Navigation</h3>"
    "<table>"
    "<tr><td><b>Left/Right</b></td><td>Previous/Next frame</td></tr>"
    "<tr><td><b>j / k</b></td><td>Next / Previous frame (vim-style)</td></tr>"
    "<tr><td><b>Ctrl+Left/Right</b></td><td>Jump %1 frames</td></tr>"
    "<tr><td><b>Shift+Left/Right</b></td><td>Jump %2 frames</td></tr>"
    "<tr><td><b>Alt+Left/Right</b></td><td>Jump %3 frames</td></tr>"
    "<tr><td><b>Page Up/Down</b></td><td>Jump %4 frames</td></tr>"
    "<tr><td><b>Home / g</b></td><td>Go to first frame</td></tr>"
    "<tr><td><b>End / G</b></td><td>Go to last frame</td></tr>"
    "</table>"
    "<h3>Frame Types</h3>"
    "<table>"
    "<tr><td><b>I / Ctrl+I</b></td><td>Next / Previous I-frame</td></tr>"
    "<tr><td><b>P / Ctrl+P</b></td><td>Next / Previous P- or I-frame</td></tr>"
    "<tr><td><b>B / Ctrl+B</b></td><td>Next / Previous frame (B, P, or I)</td></tr>"
    "<tr><td><b>F / Ctrl+F</b></td><td>Next / Previous frame (same as B)</td></tr>"
    "</table>"
    "<h3>Cutting</h3>"
    "<table>"
    "<tr><td><b>[</b></td><td>Set cut-in point</td></tr>"
    "<tr><td><b>]</b></td><td>Set cut-out point</td></tr>"
    "</table>"
    "<h3>Mouse</h3>"
    "<table>"
    "<tr><td><b>Mouse wheel</b></td><td>Navigate frames</td></tr>"
    "<tr><td><b>Ctrl+Wheel</b></td><td>Navigate faster</td></tr>"
    "</table>"
  ).arg(TTSettings::instance()->stepPlusCtrl())
   .arg(TTSettings::instance()->stepPlusShift())
   .arg(TTSettings::instance()->stepPlusAlt())
   .arg(TTSettings::instance()->stepPgUpDown());

  QMessageBox msgBox(this);
  msgBox.setWindowTitle(tr("Keyboard Shortcuts"));
  msgBox.setTextFormat(Qt::RichText);
  msgBox.setText(shortcuts);
  msgBox.setIcon(QMessageBox::Information);
  msgBox.exec();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signals from the video info widget
 */

/* /////////////////////////////////////////////////////////////////////////////
 * Signal from open video action
 */
void TTCutMainWindow::onReadVideoStream(QString fName)
{
  // Fresh video open (no existing AV-item): clear the output filename so
  // the Cut dialog derives a fresh default from the current video.
  // If an AV-item already exists (multi-video project or project-loaded
  // session), keep the current name so project-defined custom names are
  // preserved.
  if (mpAVData->avCount() == 0) {
    TTSettings::instance()->setCutVideoName("");
  }
  mpAVData->openAVStreams(fName);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signals from the audio list view widget
 */

/* /////////////////////////////////////////////////////////////////////////////
 * Signal from open audio action
 */
void TTCutMainWindow::onReadAudioStream(QString fName)
{
  QFileInfo fInfo(fName);
  mpAVData->appendAudioStream(mpCurrentAVDataItem, fInfo);

  // Check if audio length differs significantly from video length
  if (mpCurrentAVDataItem != 0 &&
      mpCurrentAVDataItem->videoStream() != 0 &&
      mpCurrentAVDataItem->audioCount() > 0)
  {
    TTVideoStream* video = mpCurrentAVDataItem->videoStream();
    TTAudioStream* audio = mpCurrentAVDataItem->audioStreamAt(mpCurrentAVDataItem->audioCount() - 1);

    if (audio != 0) {
      QTime videoLen = video->streamLengthTime();
      QTime audioLen = audio->streamLengthTime();

      // Calculate difference in milliseconds
      int videoMs = videoLen.hour() * 3600000 + videoLen.minute() * 60000 +
                    videoLen.second() * 1000 + videoLen.msec();
      int audioMs = audioLen.hour() * 3600000 + audioLen.minute() * 60000 +
                    audioLen.second() * 1000 + audioLen.msec();
      int diffMs = qAbs(videoMs - audioMs);

      // Warn if difference is more than 1 second
      if (diffMs > 1000) {
        QString msg = tr("Audio and video length differ by %1 seconds.\n\n"
                         "Video: %2\nAudio: %3\n\n"
                         "This may cause A/V sync issues.")
                      .arg(diffMs / 1000.0, 0, 'f', 1)
                      .arg(videoLen.toString("hh:mm:ss.zzz"))
                      .arg(audioLen.toString("hh:mm:ss.zzz"));
        QMessageBox::warning(this, tr("Length Mismatch"), msg);
      }
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signal from open subtitle action
 */
void TTCutMainWindow::onReadSubtitleStream(QString fName)
{
  QFileInfo fInfo(fName);
  mpAVData->appendSubtitleStream(mpCurrentAVDataItem, fInfo);
}

void TTCutMainWindow::onAppendCutEntry(int cutIn, int cutOut)
{
  if (mpAVData->avCount() == 0) return;

  try
  {
  mpAVData->appendCutEntry(mpCurrentAVDataItem, cutIn, cutOut);
  }
  catch (const TTInvalidOperationException& ex)
  {
  	QMessageBox msgBox;
  	msgBox.setText(ex.getMessage());
  	msgBox.exec();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Video slider position changed signal
 */
void TTCutMainWindow::onVideoSliderChanged(int sPos)
{
  if (mpAVData->avCount() == 0) return;

  // Record the newest position and let the debounce timer decode it. The
  // decode is synchronous in this thread and costs up to seconds per call on
  // UHD material (measured: H.264 1080p 70 ms, HEVC 4K 0.6 s, UHD 2.6 s per
  // event); decoding every valueChanged of a drag multiplied that by the
  // event count and froze the window. Stale intermediate positions are now
  // simply never decoded.
  mPendingSliderPos = sPos;
  mpSliderDebounce->start();
}

void TTCutMainWindow::onSliderDecodeTimer()
{
  if (mpAVData->avCount() == 0) return;
  const int sPos = mPendingSliderPos;
  if (sPos < 0) return;
  mPendingSliderPos = -1;

  // While the handle is held down, show the nearest keyframe without the
  // DPB prefill - fast enough to track the drag. The exact frame (full
  // prefill, identical to every other decoder instance, WYSIWYG for cuts)
  // follows from the sliderReleased-driven call, where isSliderDown() is
  // already false.
  if (streamNavigator->slider()->isSliderDown()) {
    currentFrame->onGotoFramePreview(sPos);
  } else if (TTSettings::instance()->fastSlider()) {
    currentFrame->onGotoFrame(sPos, 1);
  } else {
    currentFrame->onGotoFrame(sPos, 0);
  }

  navigation->checkCutPosition(mpCurrentAVDataItem);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signals from the current frame widget
 */

/* /////////////////////////////////////////////////////////////////////////////
 * New current frame position
 */
void TTCutMainWindow::onNewFramePos(int newPos)
{
  streamNavigator->slider()->blockSignals(true);
  streamNavigator->slider()->setValue( newPos );
  streamNavigator->slider()->blockSignals(false);
  navigation->checkCutPosition(mpCurrentAVDataItem, newPos);
}

void TTCutMainWindow::onSetStreamPointMarker()
{
  if (!mpCurrentAVDataItem) return;
  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  int pos = vs->currentIndex();
  TTStreamPoint pt(pos, StreamPointType::ManualMarker,
    QString("Marker (manuell)"));
  mpStreamPointModel->addPoint(pt);
}

void TTCutMainWindow::onAnalyzeStreamPoints()
{
  if (!mpCurrentAVDataItem) {
    QMessageBox::information(this, tr("Stream Points"),
      tr("No video stream loaded."));
    return;
  }

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  // Clear previous auto-detected points
  mpStreamPointModel->clearAutoDetected();

  mStreamPointWorkersRunning = 0;
  mStreamPointAnalysisAborted = false;
  mSkippedAnalysisNotes.clear();

  TTVideoHeaderList* videoHeaders = vs->headerList();
  TTVideoIndexList*  videoIndex   = vs->indexList();

  // Header-based aspect detection reads MPEG-2 sequence headers; only MPEG-2
  // streams have a header list at all.
  const bool haveHeaders = (videoHeaders && videoHeaders->size() > 0);
  if (TTSettings::instance()->spDetectAspectChange() && !haveHeaders) {
    // Enabled but unbuildable. Saying so here is the whole point: the worker
    // carries a "not an MPEG-2 stream - skipped" line, but it is unreachable
    // twice over - it is never constructed without a header list, and
    // detectAspectChanges() returns on the empty list before it reaches that
    // line. Without this note the detail area stays silent, and silence reads
    // as "found nothing", which is the very ambiguity this area exists to
    // remove.
    mSkippedAnalysisNotes << tr("Aspect ratio (sequence headers): stream has no "
                                "sequence headers (not MPEG-2) - skipped");
  }
  if (TTSettings::instance()->spDetectAspectChange() && haveHeaders) {
    // videoIndex is display-sorted (TTOpenVideoTask sorts right after building
    // it); the worker needs it to report markers in navigation positions.
    TTStreamPointVideoWorker* videoWorker = new TTStreamPointVideoWorker(
      vs->streamType(), videoHeaders, videoIndex, vs->frameRate());

    connect(videoWorker, &TTStreamPointVideoWorker::pointsDetected,
            this, &TTCutMainWindow::onVideoPointsDetected);
    connect(videoWorker, &TTThreadTask::finished,
            this, &TTCutMainWindow::onAnalysisWorkerFinished);
    connect(videoWorker, &TTThreadTask::aborted,
            this, &TTCutMainWindow::onAnalysisWorkerFinished);

    mpStreamPointTaskPool->start(videoWorker);
    mStreamPointWorkersRunning++;
  }

  // Pillarbox detection decodes I-frames; it needs the index list, which every
  // codec has. The frame index comes from the preview wrapper, so the scan does
  // not re-scan the file (and, for H.26x, has a valid index at all).
  const bool haveIndex = (videoIndex && videoIndex->count() > 0);
  if (TTSettings::instance()->spDetectPillarbox() && !haveIndex) {
    mSkippedAnalysisNotes << tr("Pillarbox detection: the stream has no frame "
                                "index - skipped");
  }
  if (TTSettings::instance()->spDetectPillarbox() && haveIndex) {
    QList<TTFrameInfo> preBuiltIndex;
    if (TTFFmpegWrapper* preview = currentFrame->videoWindow()->ffmpegWrapper())
      preBuiltIndex = preview->frameIndex();

    TTAspectScanTask* aspectTask = new TTAspectScanTask(
      vs->filePath(), vs->streamType(), videoIndex, videoHeaders,
      vs->frameCount(), vs->frameRate(),
      TTSettings::instance()->spPillarboxThreshold(),
      TTSettings::instance()->spPillarboxSampleSeconds(),
      preBuiltIndex);

    connect(aspectTask, &TTAspectScanTask::pointsDetected,
            this, &TTCutMainWindow::onVideoPointsDetected);
    connect(aspectTask, &TTThreadTask::finished,
            this, &TTCutMainWindow::onAnalysisWorkerFinished);
    connect(aspectTask, &TTThreadTask::aborted,
            this, &TTCutMainWindow::onAnalysisWorkerFinished);
    // TTSearchTask's ownership contract puts real teardown (freeing the
    // MPEG-2 decoder etc.) in the destructor, only reached via deleteLater.
    // Connect both terminal signals so a task aborted before it ever started
    // (still queued in the pool) is freed too.
    connect(aspectTask, &TTThreadTask::finished, aspectTask, &QObject::deleteLater);
    connect(aspectTask, &TTThreadTask::aborted,  aspectTask, &QObject::deleteLater);

    mpStreamPointTaskPool->start(aspectTask);
    mStreamPointWorkersRunning++;
  }

  // Audio worker (silence, audio format changes)
  if (TTSettings::instance()->spDetectSilence() || TTSettings::instance()->spDetectAudioChange()) {
    // Use first audio stream if available
    TTAudioStream* audio = nullptr;
    TTAudioHeaderList* audioHeaders = nullptr;
    if (mpCurrentAVDataItem->audioCount() > 0) {
      audio = mpCurrentAVDataItem->audioStreamAt(0);
      if (audio) {
        audioHeaders = audio->headerList();
      }
    }

    if (audio) {
      TTStreamPointAudioWorker* audioWorker = new TTStreamPointAudioWorker(
        audio->filePath(),
        vs->frameRate(),
        TTSettings::instance()->spDetectSilence(), TTSettings::instance()->spSilenceThresholdDb(), TTSettings::instance()->spSilenceMinDuration(),
        TTSettings::instance()->spDetectAudioChange(), audioHeaders);

      connect(audioWorker, &TTStreamPointAudioWorker::pointsDetected,
              this, &TTCutMainWindow::onAudioPointsDetected);
      connect(audioWorker, &TTThreadTask::finished,
              this, &TTCutMainWindow::onAnalysisWorkerFinished);
      connect(audioWorker, &TTThreadTask::aborted,
              this, &TTCutMainWindow::onAnalysisWorkerFinished);

      mpStreamPointTaskPool->start(audioWorker);
      mStreamPointWorkersRunning++;
    } else {
      mSkippedAnalysisNotes << tr("Audio analysis (silence, format changes): no "
                                  "audio track loaded - skipped");
    }
  }

  if (mStreamPointWorkersRunning > 0) {
    mpStreamPointWidget->setAnalysisRunning(true);
  } else if (!mSkippedAnalysisNotes.isEmpty()) {
    // Methods ARE enabled - they just cannot run on this material. Saying
    // "no detection methods enabled" here sent the user to the settings tab
    // to look for a checkbox that was already ticked.
    //
    // Logged as well, for the same reason as in onStatusReport(): with no
    // worker there is no progress dialog and no details area, so the dialog
    // below is the only trace - and it is gone the moment it is dismissed.
    for (const QString& note : mSkippedAnalysisNotes)
      TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__, note);
    QMessageBox::information(this, tr("Stream Points"),
      tr("No analysis could be run on this stream:\n\n%1")
          .arg(mSkippedAnalysisNotes.join("\n")));
    mSkippedAnalysisNotes.clear();
  } else {
    QMessageBox::information(this, tr("Stream Points"),
      tr("No detection methods enabled. Check Settings tab."));
  }
}

void TTCutMainWindow::onAbortStreamPoints()
{
  mStreamPointAnalysisAborted = true;
  mpStreamPointTaskPool->onUserAbortRequest();
}

void TTCutMainWindow::onStreamPointJump(int frameIndex)
{
  if (!mpCurrentAVDataItem) return;

  // Deliberately not onVideoSliderChanged(): that one passes fastSlider() on
  // as onGotoFrame's second argument, and that argument is a FRAME TYPE, not a
  // speed switch - 1 means "look for the next I frame from here". With
  // FastSlider on, a marker at 7045 therefore landed on 7050, and the three
  // error markers of one defect (7045, 7048, 7048) all ended up on the same
  // picture. A marker jump has to land where the marker points; the fast mode
  // belongs to dragging the slider, where skipping to I frames is the point.
  currentFrame->onGotoFrame(frameIndex, 0);
  navigation->checkCutPosition(mpCurrentAVDataItem);
}

void TTCutMainWindow::onStreamPointDelete(int row)
{
  mpStreamPointModel->removeAt(row);
}

void TTCutMainWindow::onStreamPointDeleteAll()
{
  mpStreamPointModel->clear();
}

void TTCutMainWindow::onStreamPointSetCutIn(int frameIndex)
{
  if (!mpCurrentAVDataItem) return;
  currentFrame->onGotoFrame(frameIndex);
  navigation->onSetCutIn();
}

void TTCutMainWindow::onStreamPointSetCutOut(int frameIndex)
{
  if (!mpCurrentAVDataItem) return;
  currentFrame->onGotoFrame(frameIndex);
  navigation->onSetCutOut();
}

void TTCutMainWindow::onVideoPointsDetected(const QList<TTStreamPoint>& points)
{
  mpStreamPointModel->addPoints(points);
}

void TTCutMainWindow::onAudioPointsDetected(const QList<TTStreamPoint>& points)
{
  mpStreamPointModel->addPoints(points);
}

void TTCutMainWindow::onAnalysisWorkerFinished()
{
  mStreamPointWorkersRunning--;
  if (mStreamPointWorkersRunning <= 0) {
    mStreamPointWorkersRunning = 0;
    mpStreamPointWidget->setAnalysisRunning(false, mStreamPointAnalysisAborted);

    // End the scan through the status chain instead of hiding the dialog
    // manually: the dialog decides itself whether to stay open (details
    // pane) and needs the finished state for its next-operation reset.
    // onStatusReport's Exit/Canceled branch also re-enables the window.
    onStatusReport(nullptr,
        mStreamPointAnalysisAborted ? StatusReportArgs::Canceled
                                    : StatusReportArgs::Exit,
        mStreamPointAnalysisAborted ? tr("Stream point analysis cancelled")
                                    : tr("Stream point analysis complete"),
        0);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Signals from the cut list widget
 */

void TTCutMainWindow::onCutSelectionChanged(const TTCutItem& cutItem, int column)
{
	(void)column;
	mpAVData->onChangeCurrentAVItem(cutItem.avDataItem());

	cutOutFrame->onCutOutChanged(cutItem);
	currentFrame->onCutInChanged(cutItem);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Create cut preview for current cut list
 */
void TTCutMainWindow::onCutPreview(TTCutList* cutList, bool skipFirst, bool skipLast)
{
  if (cutList == 0 || cutList->count() == 0)
    return;

  mpPreviewOriginalCutList = cutList;
  mPreviewSkipFirst = skipFirst;
  mPreviewSkipLast = skipLast;

  // Re-arm instead of blindly connecting: onCutPreviewFinished() drops both
  // again, but a preview that was cancelled or that failed never reaches it,
  // so the connections would survive the operation. cutPreviewFinished is
  // harmless (that slot disconnects itself), but
  // cutAudioDriftCalculated -> onAudioDriftUpdated does not, so N cancelled
  // previews left N+1 live connections and the slot ran N+1 times per drift
  // emission. disconnect() removes EVERY matching connection, so exactly one
  // of each is live from here on - without relying on a slot disconnecting
  // itself, which is the fragility this class keeps producing.
  disconnect(mpAVData, &TTAVData::cutPreviewFinished,      this,           &TTCutMainWindow::onCutPreviewFinished);
  disconnect(mpAVData, &TTAVData::cutAudioDriftCalculated, this->cutList,  &TTCutTreeView::onAudioDriftUpdated);

  connect(mpAVData, &TTAVData::cutPreviewFinished,        this,           &TTCutMainWindow::onCutPreviewFinished);
  connect(mpAVData, &TTAVData::cutAudioDriftCalculated,   this->cutList,  &TTCutTreeView::onAudioDriftUpdated);
  mpAVData->doCutPreview(cutList);
}

/*!
 * onCutPreviewFinished
 */
void TTCutMainWindow::onCutPreviewFinished(TTCutList* cutList)
{
  TTCutPreview* cutPreview = new TTCutPreview(this);

  cutPreview->initPreview(cutList, mpPreviewOriginalCutList, mpAVData, mPreviewSkipFirst, mPreviewSkipLast);
  cutPreview->exec();

  delete cutPreview;

  disconnect(mpAVData, &TTAVData::cutPreviewFinished,      this,           &TTCutMainWindow::onCutPreviewFinished);
  disconnect(mpAVData, &TTAVData::cutAudioDriftCalculated, this->cutList,  &TTCutTreeView::onAudioDriftUpdated);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Do video and audio cut
 */
void TTCutMainWindow::onAudioVideoCut(bool audioOnly, TTCutList* cutData)
{
  // no video stream open or no cut sequences defined; exit
  if (mpAVData->avCount() == 0 || cutData->count() == 0 )
    return;

  TTSettings::instance()->save();

  // Detect source video codec and set encoder codec to match
  TTVideoStream* vStream = mpCurrentAVDataItem->videoStream();
  TTAVTypes::AVStreamType streamType = vStream->streamType();

  if (streamType == TTAVTypes::h264_video) {
    TTSettings::instance()->setEncoderCodec(1);  // H.264
  } else if (streamType == TTAVTypes::h265_video) {
    TTSettings::instance()->setEncoderCodec(2);  // H.265
  } else {
    TTSettings::instance()->setEncoderCodec(0);  // MPEG-2
  }

  // Set default video cut name from video file name if not already set
  // (project settings may have loaded a custom name)
  if (TTSettings::instance()->cutVideoName().isEmpty()) {
    QString baseName = QFileInfo(vStream->fileName()).completeBaseName();
    if (TTSettings::instance()->cutAddSuffix()) {
      TTSettings::instance()->setCutVideoName(QString("%1_cut").arg(baseName));
    } else {
      TTSettings::instance()->setCutVideoName(baseName);
    }
  }

  // start dialog for cut options
  TTCutAVCutDlg* cutAVDlg = new TTCutAVCutDlg(this, audioOnly);

  // Cancel (button), X (window-close) or ESC → don't start the cut.
  if ( cutAVDlg->exec() != QDialog::Accepted )
  {
    delete cutAVDlg;
    return;
  }

  // dialog exit with start
  delete cutAVDlg;

  // Connect to cutFinished signal for notification. Re-armed rather than
  // blindly connected: the disconnect sits in onCutFinished(), which a
  // cancelled cut never reaches (no cutFinished() on abort - that is the
  // spec), so the connection would otherwise survive the cancelled operation
  // and accumulate. Same reasoning as onCutPreview() above.
  if (TTSettings::instance()->logUI())
      qDebug() << "Connecting cutFinished signal to onCutFinished slot";
  disconnect(mpAVData, &TTAVData::cutFinished, this, &TTCutMainWindow::onCutFinished);
  bool connected = connect(mpAVData, &TTAVData::cutFinished, this, &TTCutMainWindow::onCutFinished);
  if (TTSettings::instance()->logUI())
      qDebug() << "Connection result:" << connected;

  mpAVData->onDoCut(QFileInfo(QDir(TTSettings::instance()->cutDirPath()), TTSettings::instance()->cutVideoName()).absoluteFilePath(), cutData, audioOnly);
}

// ms -> "H:MM:SS" (hours dropped when 0 -> "M:SS").
static QString formatDurationMs(qint64 ms)
{
  if (ms < 0) ms = 0;
  qint64 total = ms / 1000, h = total/3600, m = (total%3600)/60, s = total%60;
  if (h > 0)
    return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
  return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Cutting finished - notify user
 */
void TTCutMainWindow::onCutFinished()
{
  if (TTSettings::instance()->logUI())
      qDebug() << "TTCutMainWindow::onCutFinished() called!";
  disconnect(mpAVData, &TTAVData::cutFinished, this, &TTCutMainWindow::onCutFinished);

  QString lengths;
  qint64 srcMs = mpAVData->lastCutSourceMs();
  qint64 resMs = mpAVData->lastCutResultMs();
  if (srcMs > 0 && resMs > 0) {
    qint64 removed = srcMs > resMs ? srcMs - resMs : 0;
    lengths = tr("\n\nSource:  %1\nResult:  %2  (%3 removed)")
        .arg(formatDurationMs(srcMs), formatDurationMs(resMs), formatDurationMs(removed));
  }

  // A failing cut now emits cutFinished() too, so that a headless --auto-cut
  // run cannot hang on a failure. That makes it this slot's job to tell the
  // two apart — otherwise a failed cut would be reported as a success.
  const QString cutError = mpAVData->lastCutError();
  if (!cutError.isEmpty()) {
    QMessageBox::warning(this, tr("Cutting Failed"),
        tr("The cut did not complete.\n\n%1").arg(cutError));
    return;
  }

  if (mpAVData->lastCutWasAudioOnly()) {
    QString summary = mpAVData->lastCutOutputSummary();
    QMessageBox::information(this, tr("Audio Cut Complete"),
        tr("Audio cutting has finished.\n\n%1").arg(summary) + lengths);
    return;
  }

  QString outputFile = QFileInfo(QDir(TTSettings::instance()->cutDirPath()), TTSettings::instance()->cutVideoName()).absoluteFilePath();
  if (TTSettings::instance()->logUI())
      qDebug() << "Showing completion dialog for:" << outputFile;

  QMessageBox::information(this, tr("Cutting Complete"),
      tr("Video cutting has finished successfully.\n\nOutput file:\n%1").arg(outputFile) + lengths);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Service methods
 */

/* /////////////////////////////////////////////////////////////////////////////
 * Dirty tracking: project has been modified
 */
void TTCutMainWindow::onProjectModified()
{
  if (!mProjectModified) {
    mProjectModified = true;
    updateWindowTitle();
  }
}

void TTCutMainWindow::setProjectModified(bool modified)
{
  mProjectModified = modified;
  updateWindowTitle();
}

void TTCutMainWindow::updateWindowTitle()
{
  QString title = TTCut::versionString;
  if (mProjectModified)
    title += " *";
  setWindowTitle(title);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Close current project or video file
 */
void TTCutMainWindow::closeProject()
{
  // Abort any running search worker BEFORE stream teardown — the worker holds
  // pointers to TTVideoIndexList / TTVideoHeaderList owned by the stream.
  // Wait for the QThreadPool runnable to actually return before we let
  // mpAVData->clear() free those lists.
  if (mpRunningSearch) {
    mpRunningSearch->onUserAbort();
    QThreadPool::globalInstance()->waitForDone();
    mpRunningSearch = nullptr;
  }

  // Stream-point tasks hold pointers into the stream's index and header lists,
  // which mpAVData->clear() below frees. Abort and wait before that happens.
  // Unconditional: mStreamPointWorkersRunning drops to 0 as soon as the GUI
  // thread processes a queued finished/aborted signal, which fires before
  // the task's run() actually returns - so the counter can already read 0
  // while a pool runnable is still executing.
  mpStreamPointTaskPool->onUserAbortRequest();
  QThreadPool::globalInstance()->waitForDone();
  mStreamPointWorkersRunning = 0;

	disconnect(cutList,  &TTCutTreeView::selectionChanged,    this, &TTCutMainWindow::onCutSelectionChanged);
  disconnect(mpAVData, &TTAVData::currentAVItemChanged,     this, &TTCutMainWindow::onAVItemChanged);

  audioFileList->onAVDataChanged(0);
  subtitleFileList->onAVDataChanged(0);
  streamNavigator->onAVItemChanged(0);
  currentFrame->onAVDataChanged(0);
  currentFrame->clearSubtitleStream();
  cutOutFrame->onAVDataChanged(0);

  navigationEnabled(false);

  mpStreamPointModel->clear();
  mpAVData->clear();
  mpCurrentAVDataItem = 0;  // AVItem was deleted by clear(), null the dangling pointer

  // Restore global settings from QSettings (discard project overrides)
  TTSettings::instance()->load();
  // Clear the project identity AFTER load() — load() must never be able to
  // resurrect the closed project's file name (would let the next save silently
  // overwrite it). This clear is the last word on project identity.
  TTSettings::instance()->setProjectFileName("");
  // Clear cut video name so next cut dialog derives it from video filename
  TTSettings::instance()->setCutVideoName("");

  setProjectModified(false);

  connect(cutList,  &TTCutTreeView::selectionChanged,    this, &TTCutMainWindow::onCutSelectionChanged);
  connect(mpAVData, &TTAVData::currentAVItemChanged,     this, &TTCutMainWindow::onAVItemChanged);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Enable or disable navigation
 */
void TTCutMainWindow::navigationEnabled( bool enabled )
{
  cutOutFrame->controlEnabled(enabled);
  currentFrame->controlEnabled(enabled);
  navigation->controlEnabled(enabled);
  streamNavigator->controlEnabled(enabled);
  cutList->controlEnabled(enabled);
}

/*! //////////////////////////////////////////////////////////////////////////////////////
 * Opening TTCut project file
 */

/**
 * Open ttcut project file
 */
void TTCutMainWindow::openProjectFile(QString fName)
{
  if (mpAVData->avCount() > 0) {
    closeProject();
  }

  QFileInfo fInfo(fName );
  TTSettings::instance()->setLastDirPath(fInfo.absolutePath());

  connect(mpAVData, &TTAVData::readProjectFileFinished, this, &TTCutMainWindow::onOpenProjectFileFinished);
  mpAVData->readProjectFile(fInfo);
}

/**
 * Open project file finished
 */
void TTCutMainWindow::onOpenProjectFileFinished(const QString& fName)
{
  if (mpCurrentAVDataItem == 0) return;

  insertRecentFile(fName);
  setProjectModified(false);

  // Refresh cut list to update acmod icons (audio streams are now loaded)
  mpAVData->emitCutDataReloaded();

  disconnect(mpAVData, &TTAVData::readProjectFileFinished, this, &TTCutMainWindow::onOpenProjectFileFinished);
}

void TTCutMainWindow::onAVItemChanged(TTAVItem* avItem)
{
  if (avItem == mpCurrentAVDataItem)  return;

  if (avItem == 0) {
	  closeProject();
	  return;
   }

  // Re-wire the async-subtitle-load hook (see onSubtitleItemAppended) to the
  // new current item — subtitle files load via TTOpenSubtitleTask on the
  // thread pool and can finish after this switch.
  if (mpCurrentAVDataItem != 0) {
    disconnect(mpCurrentAVDataItem, &TTAVItem::subtitleItemAppended,
               this, &TTCutMainWindow::onSubtitleItemAppended);
    disconnect(mpCurrentAVDataItem, &TTAVItem::subtitleItemUpdated,
               this, &TTCutMainWindow::onSubtitleItemUpdated);
  }

  mpCurrentAVDataItem = avItem;

  connect(mpCurrentAVDataItem, &TTAVItem::subtitleItemAppended,
          this, &TTCutMainWindow::onSubtitleItemAppended);
  connect(mpCurrentAVDataItem, &TTAVItem::subtitleItemUpdated,
          this, &TTCutMainWindow::onSubtitleItemUpdated);

  // Update stream point model frame rate for time display
  if (avItem->videoStream()) {
    mpStreamPointModel->setFrameRate(avItem->videoStream()->frameRate());

    // Sync encoderCodec + transient working values from the stream's codec.
    // setEncoderCodec() also resets encoderCrf/Preset/Profile to the
    // codec-specific App-Defaults so the cut pipeline reads correct values.
    // Project-Load fires deserializeSettings() AFTER this signal, so the
    // .ttcut transient values overwrite App-Defaults last — see
    // TTAVData::onReadProjectFileFinished().
    TTAVTypes::AVStreamType streamType = avItem->videoStream()->streamType();
    if (streamType == TTAVTypes::h264_video)      TTSettings::instance()->setEncoderCodec(1);
    else if (streamType == TTAVTypes::h265_video) TTSettings::instance()->setEncoderCodec(2);
    else                                          TTSettings::instance()->setEncoderCodec(0);
  }

  currentFrame->onAVDataChanged(avItem);
  cutOutFrame->onAVDataChanged(avItem);
  audioFileList->onAVDataChanged(avItem);
  subtitleFileList->onAVDataChanged(avItem);

  // Set subtitle stream for preview overlay (use first subtitle if available).
  // If the subtitle file is still loading (async TTOpenSubtitleTask), this is
  // a no-op here and onSubtitleItemAppended() wires it in once it lands.
  if (avItem->subtitleCount() > 0) {
    currentFrame->setSubtitleStream(avItem->subtitleStreamAt(0));
    currentFrame->setSubtitleDelay(avItem->subtitleListItemAt(0).getDelayMs());
    // currentFrame->onAVDataChanged() above already showed the first still
    // frame — refresh so the overlay is on it right away, not only after the
    // next navigation.
    currentFrame->refreshCurrentFrame();
  } else {
    currentFrame->clearSubtitleStream();
  }

  // Remember position when switching between videos
  onNewFramePos( avItem->videoStream()->currentIndex() );

  streamNavigator->onAVItemChanged(mpCurrentAVDataItem);

  navigationEnabled( true );

  // Clear previous logo profile
  mLogoDetector->clearProfile();
  currentFrame->videoWindow()->clearLogoROIOverlay();
  navigation->setLogoSearchEnabled(false);

  // Auto-load markad logo if available (deferred to let UI finish initialization)
  if (mpCurrentAVDataItem) {
    TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
    if (vs) {
      QString videoPath = vs->filePath();
      QString logoPath = videoPath.left(videoPath.lastIndexOf('.')) + ".logo.pgm";
      if (QFile::exists(logoPath)) {
        QTimer::singleShot(0, this, [this, logoPath]() {
          if (!mpCurrentAVDataItem) return;
          TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
          if (!vs) return;

          TTVideoIndexList* idxList = vs->indexList();

          auto decodeFn = [this](int idx) -> QImage {
            currentFrame->videoWindow()->moveToVideoFrame(idx);
            return currentFrame->videoWindow()->grabFrameImage();
          };
          auto nextIFn = [idxList](int pos) -> int {
            return idxList->moveToNextIndexPos(pos, 1);
          };

          QApplication::setOverrideCursor(Qt::WaitCursor);

          auto progressFn = [this](int current, int total) {
            statusBar()->showMessage(tr("Loading logo profile (%1/%2 frames)...").arg(current).arg(total), 0);
            QApplication::processEvents();
          };

          progressFn(0, 10);

          if (mLogoDetector->loadMarkadLogo(logoPath, decodeFn, nextIFn, 0, progressFn)) {
            currentFrame->videoWindow()->setLogoROIOverlay(mLogoDetector->roi());
            navigation->setLogoSearchEnabled(true);
            statusBar()->showMessage(tr("Logo profile loaded: %1").arg(QFileInfo(logoPath).fileName()), 3000);
          } else {
            statusBar()->showMessage(tr("Logo profile could not be verified"), 3000);
          }

          QApplication::restoreOverrideCursor();
          currentFrame->videoWindow()->showFrameAt(vs->currentIndex());
        });
      }
    }
  }
}

/*!
 * onAVDataReloaded
 * Refresh audio/subtitle tree views after sort in onThreadPoolExit
 */
void TTCutMainWindow::onAVDataReloaded()
{
  if (mpCurrentAVDataItem) {
    audioFileList->onReloadList(mpCurrentAVDataItem);
    subtitleFileList->onReloadList(mpCurrentAVDataItem);
  }
}

/*!
 * onSubtitleItemAppended
 * Fires when a subtitle file finishes loading asynchronously (TTOpenSubtitleTask,
 * see TTAVData::onOpenSubtitleFinished -> TTAVItem::appendSubtitleEntry) for the
 * currently displayed AV item. onAVItemChanged only wires the still-frame overlay
 * once, at item-switch time; for small/fast videos the video can finish opening
 * before the subtitle load task does, so subtitleCount() is still 0 there and the
 * overlay never gets wired. This is the same connect/disconnect pattern
 * TTSubtitleTreeView::onAVDataChanged uses for its own subtitleItemAppended hook.
 */
void TTCutMainWindow::onSubtitleItemAppended(const TTSubtitleItem&)
{
  if (mpCurrentAVDataItem == 0) return;
  if (mpCurrentAVDataItem->subtitleCount() == 0) return;

  currentFrame->setSubtitleStream(mpCurrentAVDataItem->subtitleStreamAt(0));
  currentFrame->setSubtitleDelay(mpCurrentAVDataItem->subtitleListItemAt(0).getDelayMs());
  currentFrame->refreshCurrentFrame();
}

/*!
 * onSubtitleItemUpdated
 * Fires on any subtitle item change (delay spinbox, language combo, pending
 * project-file values applied after the async load). The overlay only shows
 * track 0 — re-push its delay and refresh the still frame so a delay edit is
 * visible immediately.
 */
void TTCutMainWindow::onSubtitleItemUpdated(const TTSubtitleItem&, const TTSubtitleItem&)
{
  if (mpCurrentAVDataItem == 0) return;
  if (mpCurrentAVDataItem->subtitleCount() == 0) return;

  currentFrame->setSubtitleDelay(mpCurrentAVDataItem->subtitleListItemAt(0).getDelayMs());
  currentFrame->refreshCurrentFrame();
}

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

// needed by frame navigation!
void TTCutMainWindow::onSetCutOut(int index)
{
	if (mpCurrentAVDataItem == 0) return;

	cutOutFrame->onAVDataChanged(mpCurrentAVDataItem);
  cutOutFrame->onGotoCutOut(index);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onStatusReport;
 */
void TTCutMainWindow::onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value)
{
  // Stage announcements feed the progress estimator; they must never reach
  // the percent/display path below. mpProgressEstimator is guarded here and
  // below because it is deleted before some child widgets in ~TTCutMainWindow
  // (destructor order) - a late-arriving signal must not dereference it.
  if (state == StatusReportArgs::Stage) {
    if (mpProgressEstimator)
      mpProgressEstimator->beginStage(int(value));
    return;
  }

  switch(state) {
    case StatusReportArgs::Init:
      if (progressBar == 0) {
        progressBar = new TTProgressBar(this);
        connect(progressBar, &TTProgressBar::cancel, mpAVData,              &TTAVData::onUserAbortRequest);
        // Route through onAbortStreamPoints() (not directly to the pool) so a
        // cancel from this dialog also marks the run as aborted - otherwise a
        // stream-point scan cancelled here delivers its partial results as if
        // the run had completed normally (TTAspectScanTask reports on abort).
        connect(progressBar, &TTProgressBar::cancel, this,                  &TTCutMainWindow::onAbortStreamPoints);
      }
      this->setEnabled(false);
      // ...but not the progress dialog. It is a child of this window, and Qt
      // disables children along with their parent - child windows included. A
      // disabled dialog is painted in its disabled state, which no style
      // animates, so the bar's pulse mode (setRange(0, 0) after 5 s without a
      // Step) showed frozen stripes.
      //
      // This has to sit HERE, next to the disable, not in showBar(): Init
      // arrives once per operation - and a cut produces several pool runs in a
      // row (cut, then mux) - so anything set earlier is undone by the next
      // Init. Measured: three Inits in one session, each re-disabling the
      // dialog. That is why the pulse was ALWAYS static, not just on the first
      // operation.
      //
      // Enabled is also what the Cancel button needs.
      if (progressBar != 0) progressBar->setEnabled(true);
      break;

    case StatusReportArgs::Start:
      // Stream-point analysis never emits Init (only open/cut does), so the
      // bar has to be created here as well - otherwise a long scan runs with
      // no visible feedback whenever no open operation created it earlier.
      if (progressBar == 0) {
        progressBar = new TTProgressBar(this);
        connect(progressBar, &TTProgressBar::cancel, mpAVData,              &TTAVData::onUserAbortRequest);
        // Route through onAbortStreamPoints() (not directly to the pool) so a
        // cancel from this dialog also marks the run as aborted - otherwise a
        // stream-point scan cancelled here delivers its partial results as if
        // the run had completed normally (TTAspectScanTask reports on abort).
        connect(progressBar, &TTProgressBar::cancel, this,                  &TTCutMainWindow::onAbortStreamPoints);
      }
      progressBar->showBar();
      // The notes collected in onAnalyzeStreamPoints() (analyses that were
      // enabled but could not run) go into the LOG right here - and into the
      // details area only at the END of this function, never at this point.
      //
      // Why: TTProgressBar::onSetProgress()'s own Start branch calls
      // resetForNewOperation() when the dialog is still in its finished state
      // from the previous run, and that clears detailsView. Writing the notes
      // here put them in the view moments before that clear removed them
      // again - they reached the log but never the screen, which is exactly
      // how this was reported ("ich sehe nix, keine Meldung") and why the
      // details area appeared to begin with the first worker's own line.
      if (!mSkippedAnalysisNotes.isEmpty()) {
        for (const QString& note : mSkippedAnalysisNotes)
          TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__, note);
        mPendingSkipNotesForDialog = mSkippedAnalysisNotes;
        mSkippedAnalysisNotes.clear();
      }
      break;

    case StatusReportArgs::Exit:
    case StatusReportArgs::Canceled:
      // Hiding (or keeping the dialog open to read the details log) is
      // decided in TTProgressBar::enterFinishedState() via the
      // onSetProgress() call below. Error is deliberately NOT listed here:
      // it is emitted mid-task (e.g. H.26x frame-index build during open)
      // and is never operation-terminal — the operation still ends with
      // Exit or Canceled, so the window must stay disabled until then.
      this->setEnabled(true);
      break;
  }

  if (progressBar != 0) {
    int rawPercent;
    if (task == 0) {
      // No ThreadTask (e.g. Smart Cut, MKV mux) - value IS the percent
      rawPercent = static_cast<int>(value);
    } else if (mStreamPointWorkersRunning > 0) {
      rawPercent = mpStreamPointTaskPool->overallPercentage();
      // Pool operations are single-stage; enter the ad-hoc stage once.
      if (mpProgressEstimator && !mpProgressEstimator->active())
        mpProgressEstimator->beginStage(StatusReportArgs::StagePool);
    } else {
      rawPercent = mpAVData->totalProcess();
      if (mpProgressEstimator && !mpProgressEstimator->active() && !mpProgressEstimator->planned())
        mpProgressEstimator->beginStage(StatusReportArgs::StagePool);
    }

    if (state == StatusReportArgs::Exit || state == StatusReportArgs::Canceled) {
      // Exit is "regular" only for a cut that didn't fail. mLastCutError is
      // cleared at the start of every onDoCut and only ever set by cut
      // paths, so a stale value from a PRIOR failed cut can in theory
      // linger into the Exit of a later non-cut operation (open, scan) -
      // those never write a calibKey anyway, so the only effect would be a
      // skipped (harmless) calibration write, not a wrong display.
      const bool regular = state == StatusReportArgs::Exit
                         && mpAVData->lastCutError().isEmpty();
      if (mpProgressEstimator) {
        mpProgressEstimator->finishOperation(regular);
        qint64 dur = mpProgressEstimator->operationDurationMs();
        if (state == StatusReportArgs::Canceled)
          progressBar->setRemaining(tr("Cancelled"));
        else if (regular && dur > 0)
          progressBar->setRemaining(tr("Finished after %1")
              .arg(QTime(0, 0).addMSecs(int(dur)).toString("hh:mm:ss")));
        // Exit on a failed cut: leave the remaining label as-is (no stale
        // countdown, but no fabricated "Finished after" either) - the
        // failure itself is already visible in the Exit message/action line.
      } else if (state == StatusReportArgs::Canceled) {
        progressBar->setRemaining(tr("Cancelled"));
      }
      progressBar->onSetProgress(task, state, msg, rawPercent);
      return;
    }

    // Only Step carries a real progress sample (spec §7) - every other
    // state (Init/Start/Error/AddProcessLine/ShowProcessForm/...) forwards
    // its raw/last-known percent untouched. onSetProgress() ignores the
    // totalProgress argument for all of those states anyway (only Step
    // reads it; Exit hardcodes 100), so this is a display no-op for them -
    // it only stops those filler values (often 0) from resetting the
    // estimator's stage percent / flipping the remaining label back to
    // "calculating...".
    if (state == StatusReportArgs::Step && mpProgressEstimator) {
      TTProgressEstimator::Result r = mpProgressEstimator->update(rawPercent);
      progressBar->setRemaining(formatRemaining(r));
      progressBar->onSetProgress(task, state, msg, r.totalPercent);
    } else {
      progressBar->onSetProgress(task, state, msg, rawPercent);
    }

    // After the dialog has seen this report - and therefore after any
    // resetForNewOperation() it triggered - the skipped-analysis notes are
    // safe to add. See the Start branch above for why they cannot go in
    // earlier.
    if (!mPendingSkipNotesForDialog.isEmpty()) {
      for (const QString& note : mPendingSkipNotesForDialog)
        progressBar->onSetProgress(0, StatusReportArgs::AddProcessLine, note, 0);
      mPendingSkipNotesForDialog.clear();
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Human-readable remaining time (spec rounding rules): coarse on purpose -
 * a seconds-precise countdown suggests an accuracy the estimate cannot have.
 */
QString TTCutMainWindow::formatRemaining(const TTProgressEstimator::Result& r) const
{
  if (r.kind == TTProgressEstimator::RemainingUnknown)
    return tr("calculating...");

  const qint64 s = r.remainingMs / 1000;
  QString t;
  if (s < 10) {
    t = tr("almost done");
  } else {
    qint64 rounded;
    if (s < 60)       rounded = ((s + 5) / 10) * 10;
    else if (s < 600) rounded = ((s + 15) / 30) * 30;
    else              rounded = ((s + 30) / 60) * 60;
    QString clock = (rounded >= 3600)
        ? QString("%1:%2:%3").arg(rounded / 3600)
              .arg((rounded % 3600) / 60, 2, 10, QChar('0'))
              .arg(rounded % 60, 2, 10, QChar('0'))
        : QString("%1:%2").arg(rounded / 60, 2, 10, QChar('0'))
              .arg(rounded % 60, 2, 10, QChar('0'));
    t = tr("about %1").arg(clock);
  }

  if (r.kind == TTProgressEstimator::RemainingStageOnly) {
    QString stage = progressStageName(r.stage);
    if (!stage.isEmpty())
      return QString("%1: %2").arg(stage, t);
  }
  return t;
}

QString TTCutMainWindow::progressStageName(int stage) const
{
  switch (stage) {
    case StatusReportArgs::StageVideo: return tr("Video");
    case StatusReportArgs::StageAudio: return tr("Audio");
    case StatusReportArgs::StageMux:   return tr("Muxing");
    default:                           return QString();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Update recent file menu actions
 */
void TTCutMainWindow::updateRecentFileActions()
{
  const QStringList& recentFiles = TTSettings::instance()->recentFileList();
  int numRecentFiles = qMin(recentFiles.size(), (int)MaxRecentFiles);

  for (int i = 0; i < numRecentFiles; ++i) {
    QString text = tr("&%1 %2").arg(i+1).
      arg(QFileInfo(recentFiles[i]).fileName());
    recentFileAction[i]->setText(text);
    recentFileAction[i]->setData(recentFiles[i]);
    recentFileAction[i]->setVisible(true);
  }

  for (int j = numRecentFiles; j < MaxRecentFiles; ++j) {
    recentFileAction[j]->setVisible(false);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Open Quick Jump thumbnail browser dialog
 */
void TTCutMainWindow::onQuickJump()
{
  if (!mpCurrentAVDataItem) return;

  TTVideoStream* videoStream = mpCurrentAVDataItem->videoStream();
  if (!videoStream) return;

  int currentPos = videoStream->currentIndex();

  TTQuickJumpDialog dlg(videoStream, currentPos, this);
  if (dlg.exec() == QDialog::Accepted) {
    int selectedFrame = dlg.selectedFrameIndex();
    if (selectedFrame >= 0) {
      currentFrame->onGotoFrame(selectedFrame);
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Insert new file in recent file list
 */
void TTCutMainWindow::insertRecentFile(const QString& fName)
{
  // Read-modify-write through the setter so recentFilesChanged() fires
  // exactly once.
  QStringList list = TTSettings::instance()->recentFileList();
  list.removeAll(fName);
  list.prepend(fName);

  while (list.size() > MaxRecentFiles) {
    list.removeLast();
  }
  TTSettings::instance()->setRecentFileList(list);

  for (QWidget* widget : QApplication::topLevelWidgets()) {
    TTCutMainWindow* mainWin = qobject_cast<TTCutMainWindow*>(widget);
    if (mainWin) {
      mainWin->updateRecentFileActions();
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Search for next/previous black frame from current position
 * Runs a TTBlackFrameSearchTask on a worker-owned decoder, using the shared
 * frame index for correct display-order mapping (matching the video stream).
 */
void TTCutMainWindow::onSearchBlackFrame(int startPos, int direction, float threshold)
{
  if (!mpCurrentAVDataItem || mpRunningSearch) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  int frameCount = vs->frameCount();
  if (frameCount <= 0) return;

  TTVideoIndexList* idxList = vs->indexList();
  if (!idxList) return;

  mLastSearchStartPos = startPos;

  // Index sharing (spec 2026-06-05): pulls the frame index from Owner B
  // (mpegWindow), which itself adopted Owner A's index — avoids another ~2 s
  // scan in the search worker.
  QList<TTFrameInfo> preBuiltIndex;
  if (TTFFmpegWrapper* preview = currentFrame->videoWindow()->ffmpegWrapper())
    preBuiltIndex = preview->frameIndex();

  auto* task = new TTBlackFrameSearchTask(
      vs->filePath(),
      vs->streamType(),
      idxList,
      vs->headerList(),
      startPos, direction, frameCount,
      threshold,
      preBuiltIndex);

  connect(task, &TTSearchTask::progress, this,
          [this](int n) {
            statusBar()->showMessage(tr("Searching... %1 frames checked").arg(n));
          });
  connect(task, &TTSearchTask::found,
          this, &TTCutMainWindow::onBlackSearchFinished);
  connect(task, &TTThreadTask::finished, task, &QObject::deleteLater);

  // A task aborted before the pool ever ran it emits aborted, never finished
  // and never found: TTThreadTask::run() throws TTAbortException before
  // reaching operation(). Without this, the task leaks and - worse -
  // mpRunningSearch stays set, which blocks every later search. The pointer
  // comparison keeps the reset from firing on a task that already reported
  // through found(), which is possible if operation() throws after emitting.
  connect(task, &TTThreadTask::aborted, task, &QObject::deleteLater);
  connect(task, &TTThreadTask::aborted, this,
          [this, task]() { if (mpRunningSearch == task) onBlackSearchFinished(-1, true); });

  mpRunningSearch = task;
  navigation->setBlackSearchRunning(true);
  statusBar()->showMessage(tr("Searching black frame from frame %1...").arg(startPos));
  mpStreamPointTaskPool->start(task);
}

void TTCutMainWindow::onAbortBlackSearch()
{
  if (mpRunningSearch) mpRunningSearch->onUserAbort();
}

void TTCutMainWindow::onBlackSearchFinished(int foundPos, bool wasAborted)
{
  navigation->setBlackSearchRunning(false);
  mpRunningSearch = nullptr;

  if (foundPos >= 0) {
    onVideoSliderChanged(foundPos);
    statusBar()->clearMessage();
  } else {
    currentFrame->videoWindow()->showFrameAt(mLastSearchStartPos);
    statusBar()->showMessage(
        wasAborted ? tr("Black frame search aborted")
                   : tr("No black frame found"),
        3000);
  }
}

/*!
 * Scene change search: compare luma histograms of consecutive I-frame pairs
 */
void TTCutMainWindow::onSearchSceneChange(int startPos, int direction, float threshold)
{
  if (!mpCurrentAVDataItem || mpRunningSearch) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  int frameCount = vs->frameCount();
  if (frameCount <= 0) return;

  TTVideoIndexList* idxList = vs->indexList();
  if (!idxList) return;

  mLastSearchStartPos = startPos;

  // Index sharing (spec 2026-06-05): pulls the frame index from Owner B
  // (mpegWindow), which itself adopted Owner A's index — avoids another ~2 s
  // scan in the search worker.
  QList<TTFrameInfo> preBuiltIndex;
  if (TTFFmpegWrapper* preview = currentFrame->videoWindow()->ffmpegWrapper())
    preBuiltIndex = preview->frameIndex();

  auto* task = new TTSceneChangeSearchTask(
      vs->filePath(),
      vs->streamType(),
      idxList,
      vs->headerList(),
      startPos, direction, frameCount,
      threshold,
      preBuiltIndex);

  connect(task, &TTSearchTask::progress, this,
          [this](int n) {
            statusBar()->showMessage(tr("Searching... %1 frames checked").arg(n));
          });
  connect(task, &TTSearchTask::found,
          this, &TTCutMainWindow::onSceneSearchFinished);
  connect(task, &TTThreadTask::finished, task, &QObject::deleteLater);

  // A task aborted before the pool ever ran it emits aborted, never finished
  // and never found: TTThreadTask::run() throws TTAbortException before
  // reaching operation(). Without this, the task leaks and - worse -
  // mpRunningSearch stays set, which blocks every later search. The pointer
  // comparison keeps the reset from firing on a task that already reported
  // through found(), which is possible if operation() throws after emitting.
  connect(task, &TTThreadTask::aborted, task, &QObject::deleteLater);
  connect(task, &TTThreadTask::aborted, this,
          [this, task]() { if (mpRunningSearch == task) onSceneSearchFinished(-1, true); });

  mpRunningSearch = task;
  navigation->setSceneSearchRunning(true);
  statusBar()->showMessage(tr("Searching scene change from frame %1...").arg(startPos));
  mpStreamPointTaskPool->start(task);
}

void TTCutMainWindow::onAbortSceneSearch()
{
  if (mpRunningSearch) mpRunningSearch->onUserAbort();
}

void TTCutMainWindow::onSceneSearchFinished(int foundPos, bool wasAborted)
{
  navigation->setSceneSearchRunning(false);
  mpRunningSearch = nullptr;

  if (foundPos >= 0) {
    onVideoSliderChanged(foundPos);
    statusBar()->clearMessage();
  } else {
    currentFrame->videoWindow()->showFrameAt(mLastSearchStartPos);
    statusBar()->showMessage(
        wasAborted ? tr("Scene change search aborted")
                   : tr("No scene change found"),
        3000);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Logo detection: ROI selection, profile creation, search loop
 */
void TTCutMainWindow::onSelectLogoROI()
{
  if (!mpCurrentAVDataItem) return;
  currentFrame->videoWindow()->setLogoSelectionMode(true);
  statusBar()->showMessage(tr("Select the logo area in the video frame..."), 0);
}

void TTCutMainWindow::onLoadLogoFile()
{
  if (!mpCurrentAVDataItem) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  // Start in the video file's directory
  QString startDir = QFileInfo(vs->filePath()).absolutePath();

  QString pgmPath = QFileDialog::getOpenFileName(this,
    tr("Load logo file"), startDir, tr("PGM Logo (*.pgm)"));

  if (pgmPath.isEmpty()) return;

  TTVideoIndexList* idxList = vs->indexList();

  auto decodeFn = [this](int idx) -> QImage {
    currentFrame->videoWindow()->moveToVideoFrame(idx);
    return currentFrame->videoWindow()->grabFrameImage();
  };
  auto nextIFn = [idxList](int pos) -> int {
    return idxList->moveToNextIndexPos(pos, 1);
  };

  QApplication::setOverrideCursor(Qt::WaitCursor);
  statusBar()->showMessage(tr("Loading logo profile..."), 0);
  QApplication::processEvents();

  if (mLogoDetector->loadMarkadLogo(pgmPath, decodeFn, nextIFn, 0)) {
    currentFrame->videoWindow()->setLogoROIOverlay(mLogoDetector->roi());
    navigation->setLogoSearchEnabled(true);
    statusBar()->showMessage(tr("Logo profile loaded: %1").arg(QFileInfo(pgmPath).fileName()), 3000);
  } else {
    statusBar()->showMessage(tr("Logo profile could not be verified"), 3000);
  }

  QApplication::restoreOverrideCursor();
  currentFrame->videoWindow()->showFrameAt(vs->currentIndex());
}

void TTCutMainWindow::onCancelLogoROI()
{
  currentFrame->videoWindow()->setLogoSelectionMode(false);
  mLogoDetector->clearProfile();
  currentFrame->videoWindow()->clearLogoROIOverlay();
  navigation->setLogoSearchEnabled(false);
  statusBar()->showMessage(tr("Logo profile removed"), 3000);
}

void TTCutMainWindow::onLogoDataLoaded(const TTLogoProjectData& logoData)
{
  if (!mpCurrentAVDataItem) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  if (logoData.isMarkad) {
    // Reload markad PGM file
    QFileInfo fi(logoData.markadPath);
    if (!fi.exists()) {
      statusBar()->showMessage(tr("Logo file not found: %1").arg(logoData.markadPath), 5000);
      return;
    }

    TTFFmpegWrapper* analysisWrapper = nullptr;
    bool useAnalysis = currentFrame->videoWindow()->isFFmpegStream();
    if (useAnalysis) {
      analysisWrapper = new TTFFmpegWrapper();
      analysisWrapper->setAnalysisMode(true);
      if (analysisWrapper->openFile(vs->filePath())) {
        TTFFmpegWrapper* previewWrapper = currentFrame->videoWindow()->ffmpegWrapper();
        if (previewWrapper)
          analysisWrapper->setFrameIndex(previewWrapper->frameIndex());
        else
          analysisWrapper->buildFrameIndex();
      } else {
        delete analysisWrapper;
        analysisWrapper = nullptr;
        useAnalysis = false;
      }
    }

    TTVideoIndexList* idxList = vs->indexList();

    auto decodeFn = [&](int frameIndex) -> QImage {
      if (useAnalysis && analysisWrapper)
        return analysisWrapper->decodeFrame(frameIndex);
      currentFrame->videoWindow()->moveToVideoFrame(frameIndex);
      return currentFrame->videoWindow()->grabFrameImage();
    };
    auto nextIFn = [&](int pos) -> int {
      return idxList ? idxList->moveToNextIndexPos(pos, 1) : -1;
    };

    if (mLogoDetector->loadMarkadLogo(logoData.markadPath, decodeFn, nextIFn, 0)) {
      currentFrame->videoWindow()->setLogoROIOverlay(mLogoDetector->roi());
      navigation->setLogoSearchEnabled(true);
      statusBar()->showMessage(tr("Logo profile loaded: %1").arg(fi.fileName()), 3000);
    }

    if (analysisWrapper) {
      analysisWrapper->closeFile();
      delete analysisWrapper;
    }
  } else {
    // Recreate manual ROI profile from saved coordinates
    onLogoROISelected(logoData.roi);
  }
}

void TTCutMainWindow::onLogoROISelected(QRect imageCoords)
{
  if (!mpCurrentAVDataItem) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  TTVideoIndexList* idxList = vs->indexList();
  if (!idxList) return;

  mLogoDetector->setROI(imageCoords);

  const int profileFrames = 10;

  // For H.264/H.265: create dedicated analysis decoder
  TTFFmpegWrapper* analysisWrapper = nullptr;
  bool useAnalysis = currentFrame->videoWindow()->isFFmpegStream();
  if (useAnalysis) {
    analysisWrapper = new TTFFmpegWrapper();
    analysisWrapper->setAnalysisMode(true);
    if (analysisWrapper->openFile(vs->filePath())) {
      TTFFmpegWrapper* previewWrapper = currentFrame->videoWindow()->ffmpegWrapper();
      if (previewWrapper)
        analysisWrapper->setFrameIndex(previewWrapper->frameIndex());
      else
        analysisWrapper->buildFrameIndex();
    } else {
      delete analysisWrapper;
      analysisWrapper = nullptr;
      useAnalysis = false;
    }
  }

  int pos = idxList->moveToIndexPos(vs->currentIndex(), 1);
  int collected = 0;

  while (pos >= 0 && pos < vs->frameCount() && collected < profileFrames) {
    statusBar()->showMessage(tr("Creating logo profile (%1/%2 frames)")
      .arg(collected + 1).arg(profileFrames));
    QApplication::processEvents();

    QImage frame;
    if (useAnalysis && analysisWrapper) {
      frame = analysisWrapper->decodeFrame(pos);
    } else {
      currentFrame->videoWindow()->moveToVideoFrame(pos);
      frame = currentFrame->videoWindow()->grabFrameImage();
    }

    if (!frame.isNull()) {
      mLogoDetector->addEdgeSample(frame);
      collected++;
    }

    pos = idxList->moveToNextIndexPos(pos, 1);
  }

  if (analysisWrapper) {
    analysisWrapper->closeFile();
    delete analysisWrapper;
  }

  if (collected > 0) {
    mLogoDetector->finalizeProfile();
    currentFrame->videoWindow()->setLogoROIOverlay(imageCoords);
    navigation->setLogoSearchEnabled(true);
    statusBar()->showMessage(tr("Logo profile created (%1 frames)").arg(collected), 3000);
  } else {
    mLogoDetector->clearProfile();
    statusBar()->showMessage(tr("Logo profile could not be created"), 3000);
  }

  currentFrame->videoWindow()->showFrameAt(vs->currentIndex());
}

void TTCutMainWindow::onSearchLogo(int startPos, int direction, float threshold)
{
  if (!mpCurrentAVDataItem || mpRunningSearch) return;
  if (!mLogoDetector || !mLogoDetector->hasProfile()) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  int frameCount = vs->frameCount();
  if (frameCount <= 0) return;

  TTVideoIndexList* idxList = vs->indexList();
  if (!idxList) return;

  mLastSearchStartPos = startPos;

  // Index sharing (spec 2026-06-05): pulls the frame index from Owner B
  // (mpegWindow), which itself adopted Owner A's index — avoids another ~2 s
  // scan in the search worker.
  QList<TTFrameInfo> preBuiltIndex;
  if (TTFFmpegWrapper* preview = currentFrame->videoWindow()->ffmpegWrapper())
    preBuiltIndex = preview->frameIndex();

  auto* task = new TTLogoSearchTask(
      vs->filePath(),
      vs->streamType(),
      idxList,
      vs->headerList(),
      startPos, direction, frameCount,
      mLogoDetector,
      threshold,
      preBuiltIndex);

  connect(task, &TTSearchTask::progress, this,
          [this](int n) {
            statusBar()->showMessage(tr("Searching... %1 frames checked").arg(n));
          });
  connect(task, &TTSearchTask::found,
          this, &TTCutMainWindow::onLogoSearchFinished);
  connect(task, &TTThreadTask::finished, task, &QObject::deleteLater);

  // A task aborted before the pool ever ran it emits aborted, never finished
  // and never found: TTThreadTask::run() throws TTAbortException before
  // reaching operation(). Without this, the task leaks and - worse -
  // mpRunningSearch stays set, which blocks every later search. The pointer
  // comparison keeps the reset from firing on a task that already reported
  // through found(), which is possible if operation() throws after emitting.
  connect(task, &TTThreadTask::aborted, task, &QObject::deleteLater);
  connect(task, &TTThreadTask::aborted, this,
          [this, task]() { if (mpRunningSearch == task) onLogoSearchFinished(-1, true); });

  mpRunningSearch = task;
  navigation->setLogoSearchRunning(true);
  statusBar()->showMessage(tr("Searching logo change from frame %1...").arg(startPos));
  mpStreamPointTaskPool->start(task);
}

void TTCutMainWindow::onAbortLogoSearch()
{
  if (mpRunningSearch) mpRunningSearch->onUserAbort();
}

void TTCutMainWindow::onLogoSearchFinished(int foundPos, bool wasAborted)
{
  navigation->setLogoSearchRunning(false);
  mpRunningSearch = nullptr;

  if (foundPos >= 0) {
    onVideoSliderChanged(foundPos);
    statusBar()->clearMessage();
  } else {
    currentFrame->videoWindow()->showFrameAt(mLastSearchStartPos);
    statusBar()->showMessage(
        wasAborted ? tr("Logo search aborted")
                   : tr("No logo state change found"),
        3000);
  }
}
