/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
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

#include "ttcurrentframe.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavstream.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttesinfo.h"
#include "../common/ttcut.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalSocket>
#include <QProcess>
#include <QStyle>
#include <QThread>
#include <QWheelEvent>

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

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  pbPrevFrame->setIcon(QIcon::fromTheme("go-previous", style->standardIcon(QStyle::SP_MediaSkipBackward)));
  pbNextFrame->setIcon(QIcon::fromTheme("go-next", style->standardIcon(QStyle::SP_MediaSkipForward)));
  pbSetMarker->setIcon(QIcon::fromTheme("bookmark-new", style->standardIcon(QStyle::SP_DialogApplyButton)));
  pbPlayVideo->setIcon(QIcon::fromTheme("media-playback-start", style->standardIcon(QStyle::SP_MediaPlay)));

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
	// Stop any running playback and clean up temp file
	if (mPlayerProc != 0 && mPlayerProc->state() != QProcess::NotRunning) {
		mPlayerProc->terminate();
		mPlayerProc->waitForFinished(2000);
	}
	cleanupTempPlaybackFile();

	if (avData == 0) {
		mAVItem = 0;
		mpegWindow->closeVideoStream();
		return;
	}

	mAVItem = avData;
	videoStream = avData->videoStream();

	if (videoStream == 0) return;

	mpegWindow->openVideoStream(videoStream);
	mpegWindow->showFrameAt(videoStream->currentIndex());

	updateCurrentPosition();
}

//! Returns the current frame position in stream
int TTCurrentFrame::currentFramePos()
{
  return videoStream->currentIndex();
}

void TTCurrentFrame::closeVideoStream()
{
  // Stop any running playback and clean up temp file
  if (mPlayerProc != 0 && mPlayerProc->state() != QProcess::NotRunning) {
    mPlayerProc->terminate();
    mPlayerProc->waitForFinished(2000);
  }
  cleanupTempPlaybackFile();

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
  if (pos < 0) return;  // Invalid position (no match found)
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
    // Query mpv's current playback position via IPC before terminating
    double playbackPos = getMpvPlaybackPosition();

    mPlayerProc->terminate();
    mPlayerProc->waitForFinished(2000);

    // Clean up temp file and socket
    cleanupTempPlaybackFile();
    if (!mMpvSocketPath.isEmpty()) {
      QFile::remove(mMpvSocketPath);
      mMpvSocketPath.clear();
    }

    // Invalidate display cache so frame is re-decoded after mpv overlay
    mpegWindow->invalidateDisplay();

    // Calculate new frame position
    int newFrame;
    double frameRate = videoStream->frameRate();

    if (playbackPos >= 0) {
      // Use time position from mpv IPC - use floor to get the frame being displayed
      newFrame = static_cast<int>(playbackPos * frameRate);
      qDebug() << "mpv time position:" << playbackPos << "s -> frame" << newFrame
               << "(rate:" << frameRate << ")";
    } else {
      // Fallback: use elapsed time (less accurate)
      qint64 elapsedMs = mPlayTimer.elapsed();
      int elapsedFrames = static_cast<int>((elapsedMs / 1000.0) * frameRate);
      newFrame = mPlayStartFrame + elapsedFrames;
      qDebug() << "Fallback: elapsed" << elapsedMs << "ms -> frame" << newFrame;
    }

    // Clamp to valid range
    if (newFrame < 0) newFrame = 0;
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

  // Store start position (timer starts later, after MKV creation for H.264/H.265)
  mPlayStartFrame = videoStream->currentIndex();

  // Check stream type for H.264/H.265 which need special handling
  TTAVTypes::AVStreamType stype = videoStream->streamType();
  bool isH264orH265 = (stype == TTAVTypes::h264_video || stype == TTAVTypes::h265_video);

  // Build mpv command
  QStringList args;

  // Create IPC socket for querying playback position
  mMpvSocketPath = QDir(TTCut::tempDirPath).filePath("mpv-ipc.sock");
  QFile::remove(mMpvSocketPath);  // Remove stale socket

  // Embed mpv into mpegWindow
  // Use x11 first (xv has port conflicts), fall back to xv
  args << "--vo=x11,xv"
       << QString("--wid=%1").arg(mpegWindow->winId())
       << "--no-osc"
       << "--no-input-default-bindings"
       << "--keep-open=no"
       << "--hr-seek=yes"           // Precise seeking (not just keyframes)
       << "--hr-seek-framedrop=no"  // Don't skip frames during seek
       << QString("--input-ipc-server=%1").arg(mMpvSocketPath);

  if (isH264orH265) {
    // For H.264/H.265: Create a temporary MKV file with A/V muxed together
    // ES files have no timestamps, so we must mux them first for proper playback

    // Show wait cursor during MKV creation
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString tempMkv = createTempMkvForPlayback();
    QApplication::restoreOverrideCursor();

    if (tempMkv.isEmpty()) {
      qDebug() << "Failed to create temp MKV for H.264/H.265 playback";
      return;
    }

    // Store temp file path for cleanup
    mTempPlaybackFile = tempMkv;

    // Now we can seek in the muxed file
    QTime frameTime = videoStream->currentFrameTime();
    double startSec = frameTime.hour() * 3600.0 + frameTime.minute() * 60.0 +
                      frameTime.second() + frameTime.msec() / 1000.0;
    args << QString("--start=%1").arg(startSec, 0, 'f', 3);
    args << tempMkv;
  } else {
    // MPEG-2: Can seek to current position directly
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
  }

  qDebug() << "Starting mpv:" << args;

  // Start timer just before starting mpv (after MKV creation)
  mPlayTimer.start();
  mPlayerProc->start("mpv", args);
}

//! Clean up temporary playback file
void TTCurrentFrame::cleanupTempPlaybackFile()
{
  if (!mTempPlaybackFile.isEmpty()) {
    if (QFile::exists(mTempPlaybackFile)) {
      QFile::remove(mTempPlaybackFile);
      qDebug() << "Removed temp playback file:" << mTempPlaybackFile;
    }
    mTempPlaybackFile.clear();
  }
}

//! Query mpv's current playback position via IPC socket
//! Returns the position in seconds, or -1 on error
//! First pauses mpv to get the exact displayed frame position
double TTCurrentFrame::getMpvPlaybackPosition()
{
  if (mMpvSocketPath.isEmpty()) {
    return -1.0;
  }

  QLocalSocket socket;
  socket.connectToServer(mMpvSocketPath);

  if (!socket.waitForConnected(500)) {
    qDebug() << "Failed to connect to mpv IPC socket";
    return -1.0;
  }

  // First pause mpv to freeze the current frame
  QByteArray cmdPause = "{ \"command\": [\"set_property\", \"pause\", true] }\n";
  socket.write(cmdPause);
  socket.flush();
  socket.waitForReadyRead(200);
  socket.readAll();  // Discard pause response

  // Small delay to ensure pause is processed
  QThread::msleep(50);

  // Now get time-pos for the paused frame
  QByteArray cmdTime = "{ \"command\": [\"get_property\", \"time-pos\"] }\n";
  socket.write(cmdTime);
  socket.flush();

  if (!socket.waitForReadyRead(500)) {
    qDebug() << "mpv IPC timeout";
    socket.disconnectFromServer();
    return -1.0;
  }

  QByteArray response = socket.readAll();

  // Parse JSON response: {"data":123.456,"error":"success"}
  QString respStr = QString::fromUtf8(response);
  qDebug() << "mpv time-pos response:" << respStr;

  int dataIdx = respStr.indexOf("\"data\":");
  if (dataIdx < 0) {
    socket.disconnectFromServer();
    return -1.0;
  }

  int numStart = dataIdx + 7;
  int numEnd = respStr.indexOf(',', numStart);
  if (numEnd < 0) {
    numEnd = respStr.indexOf('}', numStart);
  }
  if (numEnd < 0) {
    socket.disconnectFromServer();
    return -1.0;
  }

  QString numStr = respStr.mid(numStart, numEnd - numStart).trimmed();
  bool ok;
  double pos = numStr.toDouble(&ok);

  socket.disconnectFromServer();

  if (!ok) {
    qDebug() << "Failed to parse mpv position:" << numStr;
    return -1.0;
  }

  return pos;
}

//! Create a temporary MKV file for H.264/H.265 playback
//! This muxes the ES video and audio so mpv can seek and sync properly
QString TTCurrentFrame::createTempMkvForPlayback()
{
  QString tempMkv = QDir(TTCut::tempDirPath).filePath("playback_temp.mkv");

  // Remove old temp file if exists
  QFile::remove(tempMkv);

  // Build mkvmerge command
  QStringList mkvArgs;
  mkvArgs << "-o" << tempMkv;

  // Get frame rate
  double frameRate = videoStream->frameRate();

  // Check for frame rate and A/V offset in .info file
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(videoStream->filePath());
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
      }
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        avOffsetMs = esInfo.avOffsetMs();
        qDebug() << "Playback: A/V sync offset from .info:" << avOffsetMs << "ms";
      }
    }
  }

  // Set frame duration for correct timing
  int frameDurationNs = static_cast<int>(1000000000.0 / frameRate);
  mkvArgs << "--default-duration" << QString("0:%1ns").arg(frameDurationNs);

  // Add video file
  mkvArgs << videoStream->filePath();

  // Add audio file if available, with sync offset
  if (mAVItem->audioCount() > 0) {
    TTAudioStream* audioStream = mAVItem->audioStreamAt(0);
    if (audioStream != 0) {
      if (avOffsetMs != 0) {
        // Apply A/V sync offset: positive offset delays audio
        mkvArgs << "--sync" << QString("0:%1").arg(avOffsetMs);
      }
      mkvArgs << audioStream->filePath();
    }
  }

  qDebug() << "Creating temp MKV:" << mkvArgs;

  // Run mkvmerge synchronously
  QProcess mkvProc;
  mkvProc.start("mkvmerge", mkvArgs);
  if (!mkvProc.waitForFinished(60000)) {  // 60 second timeout
    qDebug() << "mkvmerge timeout or error";
    return QString();
  }

  if (mkvProc.exitCode() != 0 && mkvProc.exitCode() != 1) {
    // mkvmerge returns 1 for warnings, 0 for success
    qDebug() << "mkvmerge failed:" << mkvProc.readAllStandardError();
    return QString();
  }

  qDebug() << "Temp MKV created:" << tempMkv;
  return tempMkv;
}

