/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutmainwindow.h                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTMAINWINDOW
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

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
	void onSearchSceneChange(int startPos, int direction, float threshold);
	void onAbortSceneSearch();
    void onSelectLogoROI();
    void onCancelLogoROI();
    void onLoadLogoFile();
    void onLogoDataLoaded(const TTLogoProjectData& logoData);
    void onLogoROISelected(QRect imageCoords);
    void onSearchLogo(int startPos, int direction, float threshold);
    void onAbortLogoSearch();

		void onAVItemChanged(TTAVItem* avItem);
    void onAVDataReloaded();

    void onOpenProjectFileFinished(const QString&);
    void onProjectModified();
    void runScreenshotMode();
    void runAutoCutMode(QString projectFile, QString outputPath);

		void onStatusReport(TTThreadTask* task, int state, const QString& msg,	quint64 value);

		signals:

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
    bool                 mBlackSearchAborted;
    bool                 mSceneSearchAborted;
    TTLogoDetector*      mLogoDetector;
    bool                 mLogoSearchAborted;

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
