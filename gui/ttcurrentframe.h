/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2006                                                      */
/* FILE     : ttcurrentframe.h                                                */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/19/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCURRENTFRAME
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

#ifndef TTCURRENTFRAME_H
#define TTCURRENTFRAME_H

#include "ui_currentframewidget.h"

#include "../common/ttcut.h"
#include "../avstream/ttavstream.h"

#include <QProcess>
#include <QElapsedTimer>
#include <QLocalSocket>

class TTAVItem;
class TTSubtitleStream;

class TTCurrentFrame: public QWidget, Ui::TTCurrentFrameWidget
{
	Q_OBJECT

		public:
		TTCurrentFrame(QWidget* parent = 0);

		void setTitle(const QString & title);
		void controlEnabled(bool enabled);
		int currentFramePos();
		void saveCurrentFrame();
		void closeVideoStream();
		void setSubtitleStream(TTSubtitleStream* subtitleStream);
		void clearSubtitleStream();

		void wheelEvent(QWheelEvent * e);

	public slots:
		void onAVDataChanged(TTAVItem* avData);
		void onPlayVideo();
		void onPrevIFrame();
		void onNextIFrame();
		void onPrevPFrame();
		void onNextPFrame();
		void onPrevBFrame();
		void onNextBFrame();
		void onSetMarker();
		void onGotoMarker(int markerPos);
		void onSetCutIn(int cutInPos);
		void onSetCutOut(int cutOutPos);
		void onGotoCutIn(int pos);
		void onGotoCutOut(int pos);
		void onGotoFrame(int pos);
		void onGotoFrame(int pos, int fast);
		void onMoveNumSteps(int);
		void onMoveToHome();
		void onMoveToEnd();

	signals:
		void newFramePosition(int);
    void prevFrame();
    void nextFrame();
    void setMarker(int);

	private:
		void updateCurrentPosition();
		QString createTempMkvForPlayback();
		void cleanupTempPlaybackFile();
		double getMpvPlaybackPosition();  // Query mpv's current position via IPC

	private:
		bool                isControlEnabled;
		TTVideoStream*      videoStream;
		TTAVItem*           mAVItem;
		QProcess*           mPlayerProc;
		int                 mPlayStartFrame;
		QElapsedTimer       mPlayTimer;
		QString             mTempPlaybackFile;  // Temp MKV for H.264/H.265 playback
		QString             mMpvSocketPath;     // IPC socket for mpv communication
};

#endif //TTCURRENTFRAME_H
