/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutvideoinfo.cpp                                              */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (b.altendorf@tritime.de)           DATE: 12/07/2008 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTVIDEOINFO
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

#include "ttcutvideoinfo.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../common/ttcut.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../avstream/tth264videostream.h"
#include "../avstream/tth265videostream.h"

#include <QFileInfo>
#include <QFileDialog>

/*
 * Constructor
 */
TTCutVideoInfo::TTCutVideoInfo(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  connect(pbVideoOpen, SIGNAL(clicked()), SIGNAL(openFile()));
  connect(pbPrevVideo, SIGNAL(clicked()), SIGNAL(prevAVClicked()));
  connect(pbNextVideo, SIGNAL(clicked()), SIGNAL(nextAVClicked()));
}

/*
 * Set widget title
 */
void TTCutVideoInfo::setTitle(const QString&)
{
}

/*
 * Enable / disable the control and his child controls
 */
void TTCutVideoInfo::controlEnabled(bool value)
{
  pbVideoOpen->setEnabled(value);
  pbPrevVideo->setEnabled(videoName->text() != "---");
  pbNextVideo->setEnabled(videoName->text() != "---");
}

/*
 * Reset the video file info text labels to default values
 */
void TTCutVideoInfo::clearControl()
{
  videoName->setText("---");
  videoLength->setText("---");
  videoResolution->setText("---");
  videoAspectratio->setText("---");
  currentIndex->setText("-/-");
}

/*
 * AV-Item changed
 */
void TTCutVideoInfo::onAVDataChanged(TTAVData* av, TTAVItem* avData)
{
	if (avData == 0 || av->avCount() == 0) {
		clearControl();
		return;
	}

  TTVideoStream* videoStream = avData->videoStream();
  if (videoStream == nullptr) {
    clearControl();
    return;
  }

  // video file name
  videoName->setText(videoStream->fileName());

  // video length
  int   numFrames = videoStream->frameCount();
  QTime time      = ttFramesToTime(numFrames, videoStream->frameRate());
  setLength(time, numFrames);

  // Get resolution and aspect based on stream type
  TTAVTypes::AVStreamType streamType = videoStream->streamType();

  if (streamType == TTAVTypes::h264_video) {
    // H.264 stream - get info from SPS
    TTH264VideoStream* h264Stream = dynamic_cast<TTH264VideoStream*>(videoStream);
    if (h264Stream && h264Stream->getSPS()) {
      setResolution(h264Stream->getSPS()->width(), h264Stream->getSPS()->height());
      // H.264 typically uses square pixels unless SAR says otherwise
      videoAspectratio->setText("16:9"); // Default assumption for HD content
    }
  } else if (streamType == TTAVTypes::h265_video) {
    // H.265 stream - get info from SPS
    TTH265VideoStream* h265Stream = dynamic_cast<TTH265VideoStream*>(videoStream);
    if (h265Stream && h265Stream->getSPS()) {
      setResolution(h265Stream->getSPS()->width(), h265Stream->getSPS()->height());
      videoAspectratio->setText("16:9"); // Default assumption for HD content
    }
  } else {
    // MPEG-2 stream - use sequence header
    TTMpeg2VideoStream* mpeg2Stream = dynamic_cast<TTMpeg2VideoStream*>(videoStream);
    if (mpeg2Stream) {
      TTSequenceHeader* currentSequence = mpeg2Stream->currentSequenceHeader();
      if (currentSequence != nullptr) {
        setResolution(currentSequence->horizontalSize(), currentSequence->verticalSize());
        videoAspectratio->setText(currentSequence->aspectRatioText());
      }
    }
  }

  // set index
  currentIndex->setText(QString("%1/%2").arg(av->avIndexOf(avData)+1).arg(av->avCount()));
}

/*
 * Refresh item info / horizontal and vertical size / aspect ratio info
 */
void TTCutVideoInfo::refreshInfo(TTAVItem* avItem)
{
	if (avItem == 0)  return;

  TTVideoStream* videoStream = avItem->videoStream();
  if (videoStream == nullptr) return;

  TTAVTypes::AVStreamType streamType = videoStream->streamType();

  if (streamType == TTAVTypes::h264_video) {
    TTH264VideoStream* h264Stream = dynamic_cast<TTH264VideoStream*>(videoStream);
    if (h264Stream && h264Stream->getSPS()) {
      setResolution(h264Stream->getSPS()->width(), h264Stream->getSPS()->height());
      videoAspectratio->setText("16:9");
    }
  } else if (streamType == TTAVTypes::h265_video) {
    TTH265VideoStream* h265Stream = dynamic_cast<TTH265VideoStream*>(videoStream);
    if (h265Stream && h265Stream->getSPS()) {
      setResolution(h265Stream->getSPS()->width(), h265Stream->getSPS()->height());
      videoAspectratio->setText("16:9");
    }
  } else {
    TTMpeg2VideoStream* mpeg2Stream = dynamic_cast<TTMpeg2VideoStream*>(videoStream);
    if (mpeg2Stream) {
      TTSequenceHeader* currentSequence = mpeg2Stream->currentSequenceHeader();
      if (currentSequence != nullptr) {
        setResolution(currentSequence->horizontalSize(), currentSequence->verticalSize());
        videoAspectratio->setText(currentSequence->aspectRatioText());
      }
    }
  }
}

/*
 * Set's the stream length text
 */
void TTCutVideoInfo::setLength(QTime total, int numFrames)
{
  videoLength->setText(QString("%1 (%2)").arg(total.toString("hh:mm:ss:zzz")).arg(numFrames));
}

/*
 * Set the video stream resolution as width x heigth
 */
void TTCutVideoInfo::setResolution(int width, int height)
{
  videoResolution->setText(QString("%1x%2").arg(width).arg(height));
}

/*
 * Set the video stream aspect ratio as QString
 */
void TTCutVideoInfo::setAspect(QString aspect)
{
  videoAspectratio->setText(aspect);
}

