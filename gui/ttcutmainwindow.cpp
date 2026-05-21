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
#include "../common/ttmessagebox.h"
#include "../common/ttsettings.h"

#include "../data/ttstreampointmodel.h"
#include "../data/ttstreampoint_videoworker.h"
#include "../data/ttstreampoint_audioworker.h"
#include "../data/ttsearchtask.h"
#include "../data/ttsearchtask_blackframe.h"
#include "../data/ttsearchtask_scenechange.h"
#include "../data/ttsearchtask_logo.h"

#include "ttcutavcutdlg.h"
#include "ttcutsettingsdlg.h"
#include "ttprogressbar.h"
#include "ttcutaboutdlg.h"

#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttlogodetector.h"

// TTMPEG2Window2 for black frame detection via isBlackAt()
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
  QByteArray savedGeometry = geom.value("MainWindow/geometry").toByteArray();
  bool restored = false;
  if (!savedGeometry.isEmpty()) {
    restoreGeometry(savedGeometry);
    // Verify window center is still on an existing screen
    QPoint center = geometry().center();
    bool onScreen = false;
    for (QScreen* s : QGuiApplication::screens()) {
      if (s->availableGeometry().contains(center)) {
        onScreen = true;
        break;
      }
    }
    restored = onScreen;
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
  connect(mpStreamPointWidget, &TTStreamPointWidget::jumpToFrame,         this, &TTCutMainWindow::onStreamPointJump);
  connect(mpStreamPointWidget, &TTStreamPointWidget::deleteRequested,     this, &TTCutMainWindow::onStreamPointDelete);
  connect(mpStreamPointWidget, &TTStreamPointWidget::deleteAllRequested,  this, &TTCutMainWindow::onStreamPointDeleteAll);
  connect(mpStreamPointWidget, &TTStreamPointWidget::setCutIn,            this, &TTCutMainWindow::onStreamPointSetCutIn);
  connect(mpStreamPointWidget, &TTStreamPointWidget::setCutOut,           this, &TTCutMainWindow::onStreamPointSetCutOut);

  // Connect signal from video slider
  // --------------------------------------------------------------------------
  connect(streamNavigator, &TTStreamNavigator::sliderValueChanged, this, &TTCutMainWindow::onVideoSliderChanged);

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
  // app settings, not per-window UI state).
  QSettings geom("TTCut-ng", "TTCut-ng");
  geom.setValue("MainWindow/geometry", saveGeometry());

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
  TTCutSettingsDlg* settingsDlg = new TTCutSettingsDlg( this );
  settingsDlg->exec();

  log->enableLogFile(TTSettings::instance()->createLogFile());
  log->setLogModeConsole(TTSettings::instance()->logModeConsole());
  log->setLogModeExtended(TTSettings::instance()->logModeExtended());

  TTSettings::instance()->save();

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

  if( TTSettings::instance()->fastSlider() )
    currentFrame->onGotoFrame( sPos, 1 );
  else
    currentFrame->onGotoFrame( sPos, 0 );

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

  // Save settings from widget
  mpStreamPointWidget->saveSettings();

  // Clear previous auto-detected points
  mpStreamPointModel->clearAutoDetected();

  mStreamPointWorkersRunning = 0;

  // Video worker (aspect ratio changes, pillarbox detection)
  if (TTSettings::instance()->spDetectAspectChange() || TTSettings::instance()->spDetectPillarbox()) {
    TTVideoHeaderList* videoHeaders = vs->headerList();
    TTVideoIndexList*  videoIndex   = vs->indexList();
    if (videoHeaders && videoHeaders->size() > 0) {
      TTStreamPointVideoWorker* videoWorker = new TTStreamPointVideoWorker(
        vs->filePath(), vs->streamType(), vs->frameRate(),
        TTSettings::instance()->spDetectAspectChange(), TTSettings::instance()->spDetectPillarbox(),
        TTSettings::instance()->spPillarboxThreshold(), videoHeaders, videoIndex);

      connect(videoWorker, &TTStreamPointVideoWorker::pointsDetected,
              this, &TTCutMainWindow::onVideoPointsDetected);
      connect(videoWorker, &TTThreadTask::finished,
              this, &TTCutMainWindow::onAnalysisWorkerFinished);

      mpStreamPointTaskPool->start(videoWorker);
      mStreamPointWorkersRunning++;
    }
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

      mpStreamPointTaskPool->start(audioWorker);
      mStreamPointWorkersRunning++;
    }
  }

  if (mStreamPointWorkersRunning > 0) {
    mpStreamPointWidget->setAnalysisRunning(true);
  } else {
    QMessageBox::information(this, tr("Stream Points"),
      tr("No detection methods enabled. Check Settings tab."));
  }
}

void TTCutMainWindow::onAbortStreamPoints()
{
  mpStreamPointTaskPool->onUserAbortRequest();
}

void TTCutMainWindow::onStreamPointJump(int frameIndex)
{
  if (!mpCurrentAVDataItem) return;
  onVideoSliderChanged(frameIndex);
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
    mpStreamPointWidget->setAnalysisRunning(false);

    // Close progress dialog
    if (progressBar != 0) {
      progressBar->hideBar();
    }
    this->setEnabled(true);
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

  // Connect to cutFinished signal for notification
  if (TTSettings::instance()->logUI())
      qDebug() << "Connecting cutFinished signal to onCutFinished slot";
  bool connected = connect(mpAVData, &TTAVData::cutFinished, this, &TTCutMainWindow::onCutFinished);
  if (TTSettings::instance()->logUI())
      qDebug() << "Connection result:" << connected;

  mpAVData->onDoCut(QFileInfo(QDir(TTSettings::instance()->cutDirPath()), TTSettings::instance()->cutVideoName()).absoluteFilePath(), cutData, audioOnly);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Cutting finished - notify user
 */
void TTCutMainWindow::onCutFinished()
{
  if (TTSettings::instance()->logUI())
      qDebug() << "TTCutMainWindow::onCutFinished() called!";
  disconnect(mpAVData, &TTAVData::cutFinished, this, &TTCutMainWindow::onCutFinished);

  if (mpAVData->lastCutWasAudioOnly()) {
    QString summary = mpAVData->lastCutOutputSummary();
    QMessageBox::information(this, tr("Audio Cut Complete"),
        tr("Audio cutting has finished.\n\n%1").arg(summary));
    return;
  }

  QString outputFile = QFileInfo(QDir(TTSettings::instance()->cutDirPath()), TTSettings::instance()->cutVideoName()).absoluteFilePath();
  if (TTSettings::instance()->logUI())
      qDebug() << "Showing completion dialog for:" << outputFile;

  QMessageBox::information(this, tr("Cutting Complete"),
      tr("Video cutting has finished successfully.\n\nOutput file:\n%1").arg(outputFile));
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

	disconnect(cutList,  &TTCutTreeView::selectionChanged,    this, &TTCutMainWindow::onCutSelectionChanged);
  disconnect(mpAVData, &TTAVData::currentAVItemChanged,     this, &TTCutMainWindow::onAVItemChanged);

  audioFileList->onAVDataChanged(0);
  subtitleFileList->onAVDataChanged(0);
  streamNavigator->onAVItemChanged(0);
  currentFrame->onAVDataChanged(0);
  currentFrame->clearSubtitleStream();
  cutOutFrame->onAVDataChanged(0);

  TTSettings::instance()->setProjectFileName("");
  navigationEnabled(false);

  mpStreamPointModel->clear();
  mpAVData->clear();
  mpCurrentAVDataItem = 0;  // AVItem was deleted by clear(), null the dangling pointer

  // Restore global settings from QSettings (discard project overrides)
  TTSettings::instance()->load();
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

  mpCurrentAVDataItem = avItem;

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

  // Set subtitle stream for preview overlay (use first subtitle if available)
  if (avItem->subtitleCount() > 0) {
    currentFrame->setSubtitleStream(avItem->subtitleStreamAt(0));
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

    // 8. Landezonen settings tab
    mpStreamPointWidget->showSettingsTab();
    QApplication::processEvents();
    saveWidgetScreenshot(mpStreamPointWidget, "ttcutng-landezonen-settings.png", 0);
    mpStreamPointWidget->showLandezonenTab();

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
            QStringList catNames = {"navigation", "search", "audio", "encoder",
                                    "muxer", "paths", "logging"};
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
                             "Recommendation: Use ProjectX to demux this file instead.")
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
        msgBox.addButton(QMessageBox::Ok);
        msgBox.show();
        QApplication::processEvents();
        saveWidgetScreenshot(&msgBox, "ttcutng-integrity-warning.png", 0);
        msgBox.close();
    }

    // 14. Copy main window as docs/MainWindow.png
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
  switch(state) {
    case StatusReportArgs::Init:
      if (progressBar == 0) {
        progressBar = new TTProgressBar(this);
        connect(progressBar, &TTProgressBar::cancel, mpAVData,              &TTAVData::onUserAbortRequest);
        connect(progressBar, &TTProgressBar::cancel, mpStreamPointTaskPool, &TTThreadTaskPool::onUserAbortRequest);
      }
      this->setEnabled(false);
      break;

    case StatusReportArgs::Start:
      if (progressBar != 0)
        progressBar->showBar();
      break;

    case StatusReportArgs::Exit:
    case StatusReportArgs::Error:
    case StatusReportArgs::Canceled:
      if (progressBar != 0) {
        progressBar->hideBar();
      }
      this->setEnabled(true);
      break;
  }

  if (progressBar != 0) {
    int progress;
    QTime time;
    if (task == 0) {
      // No ThreadTask (e.g. Smart Cut, MKV mux) — use value directly as percent
      progress = static_cast<int>(value);
      if (!mDirectProgressTimer.isValid())
        mDirectProgressTimer.start();
      time = QTime(0, 0, 0, 0).addMSecs(mDirectProgressTimer.elapsed());
      if (state == StatusReportArgs::Exit || state == StatusReportArgs::Finished ||
          state == StatusReportArgs::Error || state == StatusReportArgs::Canceled)
        mDirectProgressTimer.invalidate();
    } else if (mStreamPointWorkersRunning > 0) {
      progress = mpStreamPointTaskPool->overallPercentage();
      time = mpStreamPointTaskPool->overallTime();
    } else {
      progress = mpAVData->totalProcess();
      time = mpAVData->totalTime();
    }
    progressBar->onSetProgress(task, state, msg, progress, time);
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
 * Uses TTCut-ng's own decoder via TTMPEG2Window2::isBlackAt() for correct
 * frame index mapping (display order, matching the video stream's index).
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

  // Reuse the GUI preview wrapper's frame index — avoids a costly rescan in the worker.
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
