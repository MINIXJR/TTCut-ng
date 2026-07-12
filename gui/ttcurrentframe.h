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
// TTCURRENTFRAME
// ----------------------------------------------------------------------------

#ifndef TTCURRENTFRAME_H
#define TTCURRENTFRAME_H

#include "ui_currentframewidget.h"

#include "../common/ttcut.h"
#include "../avstream/ttavstream.h"

class QStackedLayout;
class TTAVItem;
class TTCutItem;
class TTMpvWrapper;
class TTSubtitleStream;

class TTCurrentFrame: public QWidget, Ui::TTCurrentFrameWidget
{
	Q_OBJECT

		public:
		TTCurrentFrame(QWidget* parent = 0);

		void setTitle(const QString & title);
		void controlEnabled(bool enabled);
		TTMPEG2Window2* videoWindow() { return mpegWindow; }
		int currentFramePos();
		void saveCurrentFrame();
		void closeVideoStream();
		void setSubtitleStream(TTSubtitleStream* subtitleStream);
		void clearSubtitleStream();

		void wheelEvent(QWheelEvent * e);

	public slots:
		void onAVDataChanged(TTAVItem* avData);
		void onCutInChanged(const TTCutItem& cutItem);
		void onPlayVideo();
		void onPrevIFrame();
		void onNextIFrame();
		void onPrevPFrame();
		void onNextPFrame();
		void onPrevBFrame();
		void onNextBFrame();
		void onWidgetPrevFrame();
		void onWidgetNextFrame();
		void onSetMarker();
		void onGotoCutIn(int pos);
		void onGotoCutOut(int pos);
		void onGotoFrame(int pos);
		void onGotoFrame(int pos, int fast);
		void onMoveNumSteps(int);
		void onMoveToHome();
		void onMoveToEnd();

	signals:
		void newFramePosition(int);
    void setMarker(int);

	private:
		void updateCurrentPosition(int pos = -1);
		QString createTempMkvForPlayback();
		void cleanupTempPlaybackFile();
		QString playbackSourceFingerprint() const;
		// Playback time<->index conversion authority (display-PTS aware).
		double playbackSecondsForCurrentStill() const;
		int    streamIndexForPlaybackSlot(int slot) const;
		void ensurePlayerCreated();

	private:
		void                clearCutContext();
		void                setPlayingButtonState(bool playing);

	private slots:
		void                onPlaybackFinished();
		void                onPlaybackPositionChanged(double seconds);
		void                onPlaySlower();
		void                onPlayFaster();

	private:
		void                applySpeedStep();

	private:
		bool                isControlEnabled;
		TTVideoStream*      videoStream;
		TTAVItem*           mAVItem;
		TTAVItem*           currentCutAVItem;
		int                 currentCutItemIndex;
		int                 currentCutPosition;
		TTMpvWrapper*       mPlayer = nullptr;
		QString             mTempPlaybackFile;  // Temp MKV for H.264/H.265 playback (cached across STOP→PLAY)
		QString             mCachedPlaybackFingerprint;  // source fingerprint of the cached temp MKV
		// True when the cached temp MKV carries real display PTS (source
		// display-order map was passed to the muxer). ALL playback time<->index
		// conversions key off this flag - no mixed-scale states possible.
		bool                mTempPlaybackHasDisplayPts = false;
		int                 mSpeedStep = 2;     // Index into kSpeedSteps[]; 2 = kSpeedStepNormal (1×)
		QWidget*            mFrameStackContainer = nullptr;
		QStackedLayout*     mFrameStack = nullptr;
};

#endif //TTCURRENTFRAME_H
