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
#include <QMutexLocker>

#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"
#include "../data/ttaudiolist.h"
#include "../data/ttcutlist.h"
#include "../data/ttstreampoint.h"
#include "../data/ttcutprojectdata.h"

#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videostream.h"

#include "ttcutpreview.h"

class TTAVData;
class TTAVItem;
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
		void onAbortStreamPoints();
		void onStreamPointJump(int frameIndex);
		void onStreamPointDelete(int row);
		void onStreamPointDeleteAll();
		void onStreamPointSetCutIn(int frameIndex);
		void onStreamPointSetCutOut(int frameIndex);
		void onVideoPointsDetected(const QList<TTStreamPoint>& points);
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

    void onOpenProjectFileFinished(const QString&);
    void onProjectModified();
    void runScreenshotMode();
    void runAutoCutMode(QString projectFile, QString outputPath);

		void onStatusReport(TTThreadTask* task, int state, const QString& msg,	quint64 value);

	public:
		// Called from main() to load a project given on the command line.
		void openProjectFile(QString fName);

	private:
		void closeProject();
		void navigationEnabled(bool enabled);
		void updateRecentFileActions();
		void insertRecentFile(const QString& fName);
		void setProjectModified(bool modified);
		void updateWindowTitle();
		void saveWidgetScreenshot(QWidget* widget, const QString& filename, int maxWidth = 1200);

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
    QElapsedTimer        mDirectProgressTimer;
    TTSearchTask*        mpRunningSearch = nullptr;
    int                  mLastSearchStartPos = -1;
    TTLogoDetector*      mLogoDetector;

    // Dirty tracking
    bool                 mProjectModified;

		// recent files menu
		enum
		{
			MaxRecentFiles = 5
		};
		QAction* recentFileAction[MaxRecentFiles];
};

#endif //TTCUTMAINWINDOW_H
