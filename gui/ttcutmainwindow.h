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

#ifndef TTCUTMAINWINDOW_H
#define TTCUTMAINWINDOW_H

#include "ui_ttcutmainwindow.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QMutexLocker>

#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttprogressestimator.h"
#include "../common/ttcalibrationstore.h"
#include "../data/ttaudiolist.h"
#include "../data/ttcutlist.h"
#include "../data/ttstreampoint.h"
#include "../data/ttcutprojectdata.h"

#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videostream.h"

#include "ttcutpreview.h"

class TTAVData;
class TTAVItem;
class TTSubtitleItem;
class TTCutList;
class TTProgressBar;
class TTThreadTask;
class TTSearchTask;
class TTLogoDetector;
class TTStreamPointModel;
class TTStreamPointWidget;
class TTThreadTaskPool;

//class TTCutMainWindow: public QMainWindow, Ui::TTCutMainWindow
class TTCutMainWindow: public QMainWindow, Ui::TTCutMainWindowForm
{
  Q_OBJECT

    public:
    TTCutMainWindow();
    ~TTCutMainWindow();

    void keyPressEvent(QKeyEvent* e);

  public slots:
    void onOpenVideoFile();
    void onOpenAudioFile();
    void onOpenSubtitleFile();
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileRecent();
    void onFileExit();
    void closeEvent(QCloseEvent* event);
    void onActionSave();
    void onActionSettings();
    void onStreamPointSettingsRequested();

    void onHelpAbout();
    void onHelpKeyboardShortcuts();

    void onReadVideoStream(QString fName);
    void onReadAudioStream(QString fName);
    void onReadSubtitleStream(QString fName);

    void onVideoSliderChanged(int value);

    void onNewFramePos(int);

    void onAppendCutEntry(int cutIn, int cutOut);

    void onCutPreview(TTCutList* cutList, bool skipFirst = false, bool skipLast = false);
    void onCutPreviewFinished(TTCutList* cutList);

    void onAudioVideoCut(bool cutAudioOnly, TTCutList* cutList);
    void onCutFinished();

    void onCutSelectionChanged(const TTCutItem&, int column);
    void onSetCutOut(int index);
    void onSetStreamPointMarker();
    void onAnalyzeStreamPoints();
    //! Automatic AC3 anomaly scan after the streams finished loading
    //! (design: "Auslösung: automatisch nach dem Laden, abschaltbar").
    //! Deferred by a zero-timer out of onAVDataReloaded() AND out of
    //! onAVItemChanged() - both feed into this one gate, see the
    //! implementation comment for why two entry points are needed and
    //! for the ordering and the once-per-item and no-duplicate-markers
    //! guards.
    void maybeStartAutoAnomalyScan();
    void onAbortStreamPoints();
    void onStreamPointJump(int frameIndex);
    void onStreamPointDelete(int row);
    void onStreamPointDeleteAll();
    void onStreamPointSetCutIn(int frameIndex);
    void onStreamPointSetCutOut(int frameIndex);
    void onVideoPointsDetected(const QList<TTStreamPoint>& points);
    //! Stream points restored from a project file - adds them like
    //! onVideoPointsDetected, but marks AudioAnomaly markers whose
    //! repair the load validation disabled.
    void onStreamPointsLoaded(const QList<TTStreamPoint>& points);
    void onAudioPointsDetected(const QList<TTStreamPoint>& points);
    void onAnalysisWorkerFinished();
    void onQuickJump();
    void onSearchBlackFrame(int startPos, int direction, float threshold);
  void onAbortBlackSearch();
  void onBlackSearchFinished(int foundPos, bool wasAborted);
  void onSearchSceneChange(int startPos, int direction, float threshold);
  void onAbortSceneSearch();
  void onSceneSearchFinished(int foundPos, bool wasAborted);
    void onSelectLogoROI();
    void onCancelLogoROI();
    void onLoadLogoFile();
    void onLogoDataLoaded(const TTLogoProjectData& logoData);
    void onLogoROISelected(QRect imageCoords);
    void onSearchLogo(int startPos, int direction, float threshold);
    void onAbortLogoSearch();
    void onLogoSearchFinished(int foundPos, bool wasAborted);

    void onAVItemChanged(TTAVItem* avItem);
    void onAVDataReloaded();
    void onSubtitleItemAppended(const TTSubtitleItem& item);
    void onSubtitleItemUpdated(const TTSubtitleItem& cItem, const TTSubtitleItem& uItem);

    void onOpenProjectFileFinished(const QString&);
    void onOpenProjectFileAborted();
    void onProjectModified();
    void runScreenshotMode();
    void runAutoCutMode(QString projectFile, QString outputPath);

    void onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

  public:
    // Called from main() to load a project given on the command line.
    void openProjectFile(QString fName);

  private slots:
    void onSliderDecodeTimer();

  private:
    // Slider debounce: valueChanged only records the newest position and
    // (re)starts this timer; the decode happens when it fires. See
    // onVideoSliderChanged() for why.
    QTimer* mpSliderDebounce  = nullptr;
    int     mPendingSliderPos = -1;

    // Opens the settings dialog; category >= 0 selects a sidebar entry.
    void openSettingsDialog(int category);
    void closeProject();
    void navigationEnabled(bool enabled);
    void updateRecentFileActions();
    // Constructor stages, one per comment section of the original constructor
    void setupImagesAndIcons();
    void restoreWindowGeometry();
    void connectMenuSignals();
    void connectNavigationSignals();
    void connectStreamPointSignals();
    void connectVideoSliderSignals();
    void connectFrameAndCutListSignals();
    void connectAVDataSignals();
    static void insertRecentFile(const QString& fName);
    void setProjectModified(bool modified);
    void updateWindowTitle();
    static void saveWidgetScreenshot(QWidget* widget, const QString& filename, int maxWidth = 1200);
    QString formatRemaining(const TTProgressEstimator::Result& r) const;
    static QString formatDurationMs(qint64 ms);  // h:mm:ss or m:ss
    QString progressStageName(int stage) const;
    //! Start the AC3 anomaly scan for the current AV item on the
    //! stream-point pool. Returns false when there is no AC3 track to
    //! scan (nothing started). Shared by the explicit analysis and the
    //! automatic post-load start.
    bool    startAudioAnomalyScan();

  private:
    TTAVData*        mpAVData;
    TTAVItem*        mpCurrentAVDataItem;
    TTProgressBar*   progressBar;
    TTCutList*       mpPreviewOriginalCutList;
  bool             mPreviewSkipFirst;
  bool             mPreviewSkipLast;

    TTMessageLogger* log;

    // Stream point detection
    TTStreamPointModel*  mpStreamPointModel;
    TTStreamPointWidget* mpStreamPointWidget;
    TTThreadTaskPool*    mpStreamPointTaskPool;
    int                  mStreamPointWorkersRunning;
    bool                 mStreamPointAnalysisAborted = false;
    //! True between openProjectFile() and onOpenProjectFileFinished(). Blocks
    //! the automatic anomaly scan while a project is still being restored -
    //! its saved stream points arrive after the pool exit that would trigger
    //! the scan, so scanning earlier duplicates them.
    bool                 mProjectLoadInProgress = false;
    //! Why an enabled stream-point analysis did not run at all. Collected in
    //! onAnalyzeStreamPoints(), which is where that decision is made - a
    //! worker that is never built cannot report anything itself. Handed to
    //! the detail area once the progress bar exists (Start branch of
    //! onStatusReport), or shown in the dialog when no worker runs at all.
    QStringList          mSkippedAnalysisNotes;
    //! Same notes, waiting to be put in the progress dialog's details area.
    //! Held back until after the dialog has processed the status report that
    //! may clear that area (resetForNewOperation) - see onStatusReport().
    QStringList          mPendingSkipNotesForDialog;
    TTSettingsCalibrationStore mCalibStore;
    TTProgressEstimator*       mpProgressEstimator;
    QElapsedTimer              mEstimatorClock;   // monotone time source
    TTSearchTask*        mpRunningSearch = nullptr;
    int                  mLastSearchStartPos = -1;
    TTLogoDetector*      mLogoDetector;

    // Dirty tracking
    bool                 mProjectModified;
    //! Base name of the project currently open, for the window title only.
    //! Deliberately NOT TTSettings::projectFileName(): that one is the save
    //! target and is only set when saving (opening a project leaves it empty),
    //! and changing that would change the overwrite semantics of File->Save.
    //! Empty when no project is open (plain video file, or after close).
    QString              mProjectDisplayName;

    // recent files menu
    enum
    {
      MaxRecentFiles = 5
    };
    QAction* recentFileAction[MaxRecentFiles];
};

#endif //TTCUTMAINWINDOW_H
