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
// TTCUTOUTFRAME
// ----------------------------------------------------------------------------

#ifndef TTCUTOUTFRAME_H
#define TTCUTOUTFRAME_H

#include "../ui_h/ui_cutoutframewidget.h"
#include "../avstream/ttavstream.h"
#include "../data/ttcutlist.h"
#include "ttprogressbar.h"

class TTAVItem;

class TTCutOutFrame: public QWidget, Ui::TTCutOutFrameWidget
{
	Q_OBJECT

	public:
		TTCutOutFrame(QWidget* parent = 0);
		~TTCutOutFrame();

		void setTitle(const QString & title);
		void controlEnabled(bool enabled);
		int  currentFramePos();
		void closeVideoStream();

	public slots:
		void onAVDataChanged(TTAVItem* avData);
		void onCutOutChanged(const TTCutItem& cutItem);
		void onGotoCutOut(int pos);
		void onPrevCutOutPos();
		void onNextCutOutPos();
		void onSearchFrame();

	signals:
	  void searchEqualFrame(TTAVItem* avItem, int startIndex);

	private:
		void updateCurrentPosition(int pos = -1);

	private:
		TTAVItem*           currentAVItem;
		int                 currentCutItemIndex;
		TTVideoStream*      videoStream;
		int                 currentPosition;
		bool                isCutOut;
};

#endif //TTCUTOUTFRAME_H
