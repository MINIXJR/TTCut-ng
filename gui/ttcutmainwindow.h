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
#include <functional>
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
// moc must see TTAVItem complete to register TTAVItem* in its signals/slots
Q_MOC_INCLUDE("data/ttavlist.h")
class TTSubtitleItem;
class TTCutList;
class TTProgressBar;
class TTThreadTask;
class TTFFmpegWrapper;
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

    //! Exit code of an --auto-cut run: 0 when the cut completed, 1 when the
    //! run ended without its output. main() returns it.
    int headlessExitCode() const { return mHeadlessExitCode; }

    void keyPressEvent(QKeyEvent* e);

  public slots:
    void onOpenVideoFile();
    void onOpenAudioFile();
    void onOpenSubtitleFile();
    void onFileNew();
    void onFileOpen();
    //! Writes the project; false when nothing was written - no AV item, the
    //! file dialog was cancelled, or writeProjectFile threw. The close
    //! handler needs to know, so that "Save" in the unsaved-changes dialog
    //! cannot end in a silent exit without a file.
    bool onFileSave();
    void onFileSaveAs();
    void onFileRecent();
    void onFileExit();
    void closeEvent(QCloseEvent* event);
    void onActionSave();
    void onActionSettings();
    void onStreamPointSettingsRequested();

    void onHelpAbout();
    void onHelpKeyboardShortcuts();

    void onReadVideoStream(const QString& fName);
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
    //! Result of every detector (video, aspect, anomaly, audio) and of the
    //! VDR/defect import: straight into the marker model.
    void onPointsDetected(const QList<TTStreamPoint>& points);
    //! Stream points restored from a project file - adds them like
    //! onPointsDetected, but marks AudioAnomaly markers whose
    //! repair the load validation disabled.
    void onStreamPointsLoaded(const QList<TTStreamPoint>& points);
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
    void onSubtitleItemsSwapped(int oldIndex, int newIndex);
    void onSubtitleItemRemoved(int index);
    void onAudioItemAppended(const TTAudioItem& item);

    void onOpenProjectFileFinished(const QString&);
    void onOpenProjectFileAborted();
    void onProjectModified();
    void runScreenshotMode();
    void runAutoCutMode(const QString& projectFile, const QString& outputPath);
    //! Ends an --auto-cut run through quit(), so closeEvent still saves and
    //! tears the project down in order; the code reaches main().
    void endAutoCut(int exitCode);

    void onStatusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);

  public:
    // Called from main() to load a project given on the command line.
    void openProjectFile(QString fName);
    //! Wire an analysis task for the stream-point pool - finished and
    //! aborted to onAnalysisWorkerFinished and to deleteLater - count it in
    //! mStreamPointWorkersRunning and start it. The caller connects the
    //! task's own pointsDetected first. Public so that
    //! tools/diag/test_analysis_task_lifetime can drive it with a dummy task.
    void startAnalysisTask(TTThreadTask* task);
    //! Connect a detector's own pointsDetected to the shared result slot and
    //! hand the task to startAnalysisTask(). The four stream-point detectors
    //! (video, aspect, audio, anomaly) declare the same pointsDetected
    //! signature, so this is the whole start sequence they share.
    template <class Task>
    void startDetectorTask(Task* task)
    {
      connect(task, &Task::pointsDetected, this, &TTCutMainWindow::onPointsDetected);
      startAnalysisTask(task);
    }

  private slots:
    void onSliderDecodeTimer();

  private:
    //! Still-frame overlay = subtitle track 0 of the current item (stream and
    //! delay), or none; re-run after anything that can change track 0.
    void showSubtitleTrackZero();
    //! "Length Mismatch" warning when 'audio' and the current video differ
    //! by more than a second.
    void warnIfLengthMismatch(TTAudioStream* audio);
    //! Audio file opened by hand whose length is checked once it has been
    //! appended to the current item (the open runs on the pool).
    QString mPendingLengthCheckFile;

    // Slider debounce: valueChanged only records the newest position and
    // (re)starts this timer; the decode happens when it fires. See
    // onVideoSliderChanged() for why.
    QTimer* mpSliderDebounce  = nullptr;
    int     mPendingSliderPos = -1;

    // Opens the settings dialog; category >= 0 selects a sidebar entry.
    void openSettingsDialog(int category);
    //! File-open dialog starting in lastDirPath; the chosen file's directory
    //! becomes the new lastDirPath. Empty when cancelled.
    QString pickFileAndRememberDir(const QString& title, const QString& filter);
    //! Save dialog for the project file, starting at <video base>.ttcut in
    //! lastDirPath. Returns the chosen path, or an empty string when the
    //! dialog was cancelled - the caller decides whether it becomes the new
    //! save target, so a cancel never clears the one in place.
    QString askProjectFileName(const QString& title);
    //! Create the progress dialog on first use and wire its Cancel.
    void ensureProgressBar();

    //! What the three directed searches (black frame, scene change, logo)
    //! take from the current item; directedSearchSource() fills it and
    //! records the start position for the not-found return.
    struct DirectedSearchSource {
      TTVideoStream*    vs        = nullptr;
      TTVideoIndexList* idxList   = nullptr;
      int               frameCount = 0;
      TTFrameIndexBundle preBuiltIndex;
    };
    bool directedSearchSource(int startPos, DirectedSearchSource& src);
    //! Wire a directed-search task (progress, found, the abort-before-run
    //! case), mark it running and start it on the stream-point pool.
    void launchDirectedSearch(TTSearchTask* task, void (TTCutMainWindow::*finished)(int, bool),
                              const std::function<void(bool)>& setRunning, const QString& startMessage);
    //! Common end of the three searches: clear the running state, jump to
    //! the hit or back to the start position with a status message.
    void finishDirectedSearch(int foundPos, bool wasAborted, const std::function<void(bool)>& setRunning,
                              const QString& abortedMessage, const QString& notFoundMessage);
    //! Headless modes: pump until the project load chain has run (or
    //! timeoutMs passed); true when a project is in.
    bool waitForProjectLoad(int timeoutMs);
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
    static QString formatRemaining(const TTProgressEstimator::Result& r);
    static QString formatDurationMs(qint64 ms);  // h:mm:ss or m:ss
    static QString progressStageName(int stage);
    //! Start the AC3 anomaly scan for the current AV item on the
    //! stream-point pool. Returns false when there is no AC3 track to
    //! scan (nothing started). Shared by the explicit analysis and the
    //! automatic post-load start.
    bool    startAudioAnomalyScan();
    //! A dedicated TTFFmpegWrapper in analysis mode for logo work on an
    //! H.26x stream, its frame index adopted from the preview wrapper;
    //! nullptr for MPEG-2 (no libav wrapper) or when the file does not open.
    TTFFmpegWrapper* createAnalysisWrapper(TTVideoStream* vs);
    //! Load a markad PGM logo as the logo profile: frames come from an
    //! analysis wrapper for H.26x, from the preview window for MPEG-2;
    //! overlay, logo-search buttons and status text follow the outcome.
    //! The one implementation behind the file dialog, the project restore
    //! and the automatic <video>.logo.pgm load.
    bool    loadMarkadLogoProfile(const QString& pgmPath, bool withProgress);

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
    //! See headlessExitCode(); set by endAutoCut().
    int                  mHeadlessExitCode = 0;
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
