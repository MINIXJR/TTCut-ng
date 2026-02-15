/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutoutframe.cpp                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/19/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTOUTFRAME
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

#include "ttcutoutframe.h"
#include "../data/ttavlist.h"

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
  QStyle* style = QApplication::style();
  pbPrevCutOutFrame->setIcon(QIcon::fromTheme("go-previous", style->standardIcon(QStyle::SP_MediaSeekBackward)));
  pbNextCutOutFrame->setIcon(QIcon::fromTheme("go-next", style->standardIcon(QStyle::SP_MediaSeekForward)));
  pbSearchFrame->setIcon(QIcon::fromTheme("edit-find", style->standardIcon(QStyle::SP_FileDialogContentsView)));

  connect(pbPrevCutOutFrame, SIGNAL(clicked()), SLOT(onPrevCutOutPos()));
  connect(pbNextCutOutFrame, SIGNAL(clicked()), SLOT(onNextCutOutPos()));
  connect(pbSearchFrame,     SIGNAL(clicked()), SLOT(onSearchFrame()));
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
int TTCutOutFrame::currentFramePos()
{
  if (videoStream == 0) return 0;
  return videoStream->currentIndex();
}

/*!
 * closeVideoStream
 */
void TTCutOutFrame::closeVideoStream()
{
	mpegWindow->closeVideoStream();
}

/*
 * Goto specified cut out position
 */
void TTCutOutFrame::onGotoCutOut(int pos)
{
  if (videoStream == 0) return;

  currentPosition = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( currentPosition );

  updateCurrentPosition();
}

//! Goto previous possible cut-out position
void TTCutOutFrame::onPrevCutOutPos()
{
  if (videoStream == 0) return;

  videoStream->moveToIndexPos(currentPosition);

  int cutOutIndex;

  cutOutIndex = (!TTCut::encoderMode)
		? videoStream->moveToPrevPIFrame()
		: videoStream->moveToPrevFrame();

  if (currentCutItemIndex >= 0) {
  	TTCutItem cutItem = currentAVItem->cutListItemAt(currentCutItemIndex);
  	currentAVItem->updateCutEntry(cutItem, cutItem.cutIn(), cutOutIndex);
  }

  currentPosition = cutOutIndex;
  mpegWindow->showFrameAt(cutOutIndex);
  updateCurrentPosition();
}

/*!
 * Goto next possible cut-out position
 */
void TTCutOutFrame::onNextCutOutPos()
{
  if (videoStream == 0) return;

  videoStream->moveToIndexPos(currentPosition);

  int cutOutIndex;

  cutOutIndex = (!TTCut::encoderMode)
		  ? videoStream->moveToNextPIFrame()
		  : videoStream->moveToNextFrame();

  if (currentCutItemIndex >= 0) {
	  TTCutItem cutItem = currentAVItem->cutListItemAt(currentCutItemIndex);
		currentAVItem->updateCutEntry(cutItem, cutItem.cutIn(), cutOutIndex);
  }

  currentPosition = cutOutIndex;
  mpegWindow->showFrameAt(cutOutIndex);
  updateCurrentPosition();
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
void TTCutOutFrame::updateCurrentPosition()
{
  if (videoStream == 0) return;

  QString szTemp;
  QString szTemp1, szTemp2;
  int     frame_type = videoStream->currentFrameType();

  szTemp1 = videoStream->currentFrameTime().toString("hh:mm:ss.zzz");

  szTemp2 = QString(" (%1)").arg(videoStream->currentIndex());

  if ( frame_type == 1 ) szTemp2 += " [I]";
  if ( frame_type == 2 ) szTemp2 += " [P]";
  if ( frame_type == 3 ) szTemp2 += " [B]";

  szTemp1 += szTemp2;
  laCutOutFramePosition->setText( szTemp1 );

  laCutOutFramePosition->update();
}
