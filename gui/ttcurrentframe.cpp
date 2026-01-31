/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / www.tritime.org                         */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2006                                                      */
/* FILE     : ttcurrentframe.cpp                                              */
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
/* either version 2 of the License, or (at your option) any later version.    */
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

#include "ttcurrentframe.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavstream.h"

#include <QDebug>
#include <QWheelEvent>
#include <QProcess>

//! Default constructor
TTCurrentFrame::TTCurrentFrame(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  videoStream      = 0;
  mAVItem          = 0;
  mPlayerProc      = 0;
  mPlayStartFrame  = 0;
  isControlEnabled = true;

  connect(pbPrevFrame,  SIGNAL(clicked()), this, SLOT(onPrevBFrame()));
  connect(pbNextFrame,  SIGNAL(clicked()), this, SLOT(onNextBFrame()));
  connect(pbSetMarker,  SIGNAL(clicked()), this, SLOT(onSetMarker()));
  connect(pbPlayVideo,  SIGNAL(clicked()), this, SLOT(onPlayVideo()));
}

//! Needeb by Qt Designer
void TTCurrentFrame::setTitle ( const QString & title )
{
  gbCurrentFrame->setTitle( title );
}

void TTCurrentFrame::controlEnabled( bool enabled )
{
  isControlEnabled = enabled;
  pbPrevFrame->setEnabled(enabled);
  pbNextFrame->setEnabled(enabled);
  pbSetMarker->setEnabled(enabled);
  pbPlayVideo->setEnabled(enabled);
}


void TTCurrentFrame::onAVDataChanged(TTAVItem* avData)
{
	qDebug() << "TTCurrentFrame::onAVDataChanged() called";

	if (avData == 0) {
		qDebug() << "avData is null, closing stream";
		mAVItem = 0;
		mpegWindow->closeVideoStream();
		return;
	}

	mAVItem = avData;
	qDebug() << "Getting video stream from avData";
	videoStream = avData->videoStream();

	if (videoStream == 0) {
		qDebug() << "videoStream is null!";
		return;
	}

	qDebug() << "Video stream type:" << videoStream->streamType();
	qDebug() << "Opening video stream in mpegWindow";
	mpegWindow->openVideoStream(videoStream);
	qDebug() << "Calling moveToFirstFrame";
	mpegWindow->moveToFirstFrame();
	qDebug() << "TTCurrentFrame::onAVDataChanged() done";
}

//! Returns the current frame position in stream
int TTCurrentFrame::currentFramePos()
{
  return videoStream->currentIndex();
}

void TTCurrentFrame::closeVideoStream()
{
  mpegWindow->closeVideoStream();
}

void TTCurrentFrame::setSubtitleStream(TTSubtitleStream* subtitleStream)
{
  mpegWindow->setSubtitleStream(subtitleStream);
}

void TTCurrentFrame::clearSubtitleStream()
{
  mpegWindow->clearSubtitleStream();
}

void TTCurrentFrame::wheelEvent ( QWheelEvent * e )
{
  if (!isControlEnabled)
    return;

  int currentPosition = videoStream->currentIndex();
  int wheelDelta      = TTCut::stepMouseWheel;

  if ( e->modifiers() == Qt::ControlModifier )
        wheelDelta += TTCut::stepPlusCtrl;

  //wheel was rotated forwards away from the user
  if ( e->angleDelta().y() > 0 )
    currentPosition -= wheelDelta;
  else
    currentPosition += wheelDelta;

  if ( currentPosition < 0 )
    currentPosition = 0;

  if( currentPosition >= (int)videoStream->frameCount() )
    currentPosition = videoStream->frameCount()-1;

  onGotoFrame(currentPosition, 0);
}

// Signals from the navigation widget
// ----------------------------------------------------------------------------

//! Navigate to previous I-Frame
void TTCurrentFrame::onPrevIFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToPrevIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to next I-Frame
void TTCurrentFrame::onNextIFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToNextIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to previous P-Frame
void TTCurrentFrame::onPrevPFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToPrevPIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to next P-Frame
void TTCurrentFrame::onNextPFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToNextPIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to previous B-Frame
void TTCurrentFrame::onPrevBFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToPrevFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to next B-Frame
void TTCurrentFrame::onNextBFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToNextFrame( );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Navigate to marker position
void TTCurrentFrame::onGotoMarker(int markerPos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(markerPos);
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

void TTCurrentFrame::onSetMarker()
{
	if (videoStream == 0) return;

	emit setMarker(videoStream->currentIndex());
}

//! Cut in position was set
void TTCurrentFrame::onSetCutIn(__attribute__((unused))int cutInPos)
{
}

//! Cut out position was set
void TTCurrentFrame::onSetCutOut(__attribute__((unused))int cutOutPos)
{
}

//! Goto cut in position
void TTCurrentFrame::onGotoCutIn(int pos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

//! Goto cut out position
void TTCurrentFrame::onGotoCutOut(int pos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

void TTCurrentFrame::onGotoFrame(int pos)
{
  onGotoFrame(pos, 0);
}

//! Goto arbitrary frame at given position
void TTCurrentFrame::onGotoFrame(int pos, int fast)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos( pos, fast );
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition();
}

void TTCurrentFrame::onMoveNumSteps(int steps)
{
  int position = videoStream->currentIndex()+steps;
  onGotoFrame(position, 0);
}

void TTCurrentFrame::onMoveToHome()
{
  onGotoFrame(0, 0);
}

void TTCurrentFrame::onMoveToEnd()
{
  onGotoFrame(videoStream->frameCount(), 0);
}

void TTCurrentFrame::updateCurrentPosition()
{
  QString szTemp;
  QString szTemp1, szTemp2;
  int     frame_type = videoStream->currentFrameType();

  szTemp1 = videoStream->currentFrameTime().toString("hh:mm:ss.zzz");

  szTemp2 = QString(" (%1)").arg(videoStream->currentIndex());

  if ( frame_type == 1 ) szTemp2 += " [I]";
  if ( frame_type == 2 ) szTemp2 += " [P]";
  if ( frame_type == 3 ) szTemp2 += " [B]";

  szTemp1 += szTemp2;
  laCurrentPosition->setText( szTemp1 );

  laCurrentPosition->update();

  emit newFramePosition( videoStream->currentIndex() );
}

void TTCurrentFrame::saveCurrentFrame()
{
  QString      szTemp;
  QString      extension;
  QString      format;
  QStringList  fileList;
  QString      fileName;
  QFileDialog* fileDlg;

  if (videoStream == 0) return;

  // get the image file name
  fileDlg = new QFileDialog( this,
      "save current frame",
      TTCut::lastDirPath,
      "Portable Network Graphics (*.png);;JPEG (*.jpg);;Bitmap (*.bmp)" );

  // enable specifying a file that doesn't exist
  fileDlg->setFileMode( QFileDialog::AnyFile );
  fileDlg->setAcceptMode( QFileDialog::AcceptSave );

  // input filename specified
  if ( fileDlg->exec() == QDialog::Accepted )
  {
    szTemp   = fileDlg->selectedNameFilter();
    fileList = fileDlg->selectedFiles();
    fileName = fileList.at(0);

    if ( szTemp == "Portable Network Graphics (*.png)" )
    {
      format    = "PNG";
      extension = "png";
    }
    else if ( szTemp == "JPEG (*.jpg)" )
    {
      format    = "JPG";
      extension = "jpg";
    }
    else if ( szTemp == "Bitmap (*.bmp)" )
    {
      format    = "BMP";
      extension = "bmp";
    }
    else
    {
      qDebug( "unsupported format" );
      return;
    }

    fileName = ttChangeFileExt( fileName, qPrintable(extension) );

    mpegWindow->saveCurrentFrame( fileName, qPrintable(format) );
  }
  delete fileDlg;
}

//! Play video with audio from current position using mpv
void TTCurrentFrame::onPlayVideo()
{
  if (videoStream == 0 || mAVItem == 0) return;

  // Stop any existing playback
  if (mPlayerProc != 0 && mPlayerProc->state() != QProcess::NotRunning) {
    mPlayerProc->terminate();
    mPlayerProc->waitForFinished(2000);

    // Calculate new position based on elapsed time
    qint64 elapsedMs = mPlayTimer.elapsed();
    double frameRate = videoStream->frameRate();
    int elapsedFrames = static_cast<int>((elapsedMs / 1000.0) * frameRate);
    int newFrame = mPlayStartFrame + elapsedFrames;

    // Clamp to valid range
    if (newFrame >= static_cast<int>(videoStream->frameCount()))
      newFrame = videoStream->frameCount() - 1;

    // Navigate to the new position
    onGotoFrame(newFrame);
    return;
  }

  // Create process if needed
  if (mPlayerProc == 0) {
    mPlayerProc = new QProcess(this);
  }

  // Store start position and start timer
  mPlayStartFrame = videoStream->currentIndex();
  mPlayTimer.start();

  // Build mpv command
  QStringList args;

  // Embed mpv into the mpegWindow widget
  args << "--vo=x11"
       << QString("--wid=%1").arg(mpegWindow->winId())
       << "--no-osc"
       << "--no-input-default-bindings"
       << "--keep-open=no";

  // Get current position in seconds
  QTime frameTime = videoStream->currentFrameTime();
  double startSec = frameTime.hour() * 3600.0 + frameTime.minute() * 60.0 +
                    frameTime.second() + frameTime.msec() / 1000.0;

  args << QString("--start=%1").arg(startSec, 0, 'f', 3);

  // Add audio file if available
  if (mAVItem->audioCount() > 0) {
    TTAudioStream* audioStream = mAVItem->audioStreamAt(0);
    if (audioStream != 0) {
      args << QString("--audio-file=%1").arg(audioStream->filePath());
    }
  }

  // Add video file
  args << videoStream->filePath();

  qDebug() << "Starting mpv:" << args;
  mPlayerProc->start("mpv", args);
}

