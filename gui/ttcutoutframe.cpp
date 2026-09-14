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

#include "ttcutoutframe.h"
#include "ttthemedicon.h"
#include "ttframepositiontext.h"
#include "../data/ttavlist.h"
#include "../avstream/ttcommon.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>

/*!
 * Constructor
 */
TTCutOutFrame::TTCutOutFrame(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  currentAVItem       = 0;
  videoStream         = 0;
  currentPosition     = -1;
  currentCutItemIndex = -1;
  isCutOut            = false;

  // Use theme icons with Qt standard icon fallback for cross-platform support
  pbPrevCutOutFrame->setIcon(ttThemedIcon("go-previous", QStyle::SP_MediaSeekBackward));
  pbNextCutOutFrame->setIcon(ttThemedIcon("go-next", QStyle::SP_MediaSeekForward));
  pbSearchFrame->setIcon(ttThemedIcon("edit-find", QStyle::SP_FileDialogContentsView));

  connect(pbPrevCutOutFrame, &QPushButton::clicked, this, &TTCutOutFrame::onPrevCutOutPos);
  connect(pbNextCutOutFrame, &QPushButton::clicked, this, &TTCutOutFrame::onNextCutOutPos);
  connect(pbSearchFrame,     &QPushButton::clicked, this, &TTCutOutFrame::onSearchFrame);
}

/*!
 * Destructor
 */
TTCutOutFrame::~TTCutOutFrame()
{
  // videoStream is owned by avItem, don't delete it
}

/*!
 * Needed by Qt Designer
 */
void TTCutOutFrame::setTitle ( const QString & title )
{
  gbCutOutFrame->setTitle( title );
}

/*!
 * controlEnabled
 * Enable/disable the navigation buttons based on whether a video stream is loaded
 */
void TTCutOutFrame::controlEnabled( bool enabled )
{
  pbPrevCutOutFrame->setEnabled(enabled);
  pbNextCutOutFrame->setEnabled(enabled);
  pbSearchFrame->setEnabled(enabled);
}

/*!
 * onAVDataChanged
 */
void TTCutOutFrame::onAVDataChanged(TTAVItem* avItem)
{
	if (avItem == 0) {
		mpegWindow->closeVideoStream();
		videoStream = 0;
		return;
	}

	if (currentAVItem == avItem) return;

	isCutOut      = false;
	currentAVItem = avItem;
	videoStream   = avItem->videoStream();

	mpegWindow->openVideoStream(videoStream);
	controlEnabled(videoStream != 0);
}

/*!
 * onCutOutChanged
 */
void TTCutOutFrame::onCutOutChanged(const TTCutItem& cutItem)
{
	isCutOut = true;
	onAVDataChanged(cutItem.avDataItem());
	onGotoCutOut(cutItem.cutOut());

	currentCutItemIndex = currentAVItem->cutIndexOf(cutItem);
}

/*!
 * Returns the current frame position in stream
 */
/*!
 * closeVideoStream
 */
/*
 * Goto specified cut out position
 */
void TTCutOutFrame::onGotoCutOut(int pos)
{
  if (videoStream == 0) return;

  currentPosition = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( currentPosition );

  updateCurrentPosition(currentPosition);
}

//! Goto previous possible cut-out position
void TTCutOutFrame::onPrevCutOutPos()
{
  if (videoStream == 0) return;

  // With a cut selected this button moves that cut's end, so it stops at the
  // cut-in: one step further would invert the range, which updateCutEntry
  // refuses anyway - and then the still would keep moving while the entry
  // stood still.
  if (currentCutItemIndex >= 0 && currentAVItem &&
      currentPosition <= currentAVItem->cutListItemAt(currentCutItemIndex).cutInIndex())
    return;

  videoStream->moveToIndexPos(currentPosition);

  int newFramePos = videoStream->moveToPrevFrame();

  currentPosition = newFramePos;

  if (currentCutItemIndex >= 0 && currentAVItem) {
    TTCutItem cutItem = currentAVItem->cutListItemAt(currentCutItemIndex);
    currentAVItem->updateCutEntry(cutItem, cutItem.cutInIndex(), newFramePos);
  }

  mpegWindow->showFrameAt(newFramePos);
  updateCurrentPosition(newFramePos);
}

/*!
 * Goto next possible cut-out position
 */
void TTCutOutFrame::onNextCutOutPos()
{
  if (videoStream == 0) return;

  videoStream->moveToIndexPos(currentPosition);

  int newFramePos = videoStream->moveToNextFrame();

  currentPosition = newFramePos;

  if (currentCutItemIndex >= 0 && currentAVItem) {
    TTCutItem cutItem = currentAVItem->cutListItemAt(currentCutItemIndex);
    currentAVItem->updateCutEntry(cutItem, cutItem.cutInIndex(), newFramePos);
  }

  mpegWindow->showFrameAt(newFramePos);
  updateCurrentPosition(newFramePos);
}

/*!
 * On search equal frame action
 * Search for a frame matching the current Cut-Out frame, starting from
 * the Current Frame position.
 */
void TTCutOutFrame::onSearchFrame()
{
  if (videoStream == 0 || currentAVItem == 0) return;

  emit searchEqualFrame(currentAVItem, currentPosition);
}

/*
 * updateCurrentPosition
 */
void TTCutOutFrame::updateCurrentPosition(int pos)
{
  if (videoStream == 0) return;

  const int actualPos = (pos >= 0) ? pos : videoStream->currentIndex();
  laCutOutFramePosition->setText(ttFramePositionText(videoStream, actualPos));

  laCutOutFramePosition->update();
}
