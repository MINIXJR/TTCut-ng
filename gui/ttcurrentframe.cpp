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

#include "ttcurrentframe.h"
#include "ttmpvwrapper.h"
#include "../data/ttavlist.h"
#include "../data/ttcutlist.h"
#include "../avstream/ttavstream.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttcommon.h"
#include "../avstream/ttesinfo.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

extern "C" {
#include <libavcodec/codec_id.h>
}

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QIcon>
#include <QStackedLayout>
#include <QStyle>
#include <QWheelEvent>
#include <cmath>
#include <iterator>

// Speed step table: index 2 is 1× (normal forward playback)
static const double kSpeedSteps[] = { -4.0, -2.0, 1.0, 2.0, 4.0 };
static constexpr int kSpeedStepNormal = 2;  // index of 1× in kSpeedSteps

//! Default constructor
TTCurrentFrame::TTCurrentFrame(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  // mpegWindow steckt heute direkt im gbCurrentFrame-Grid. Wir kapseln es in
  // einen Stack-Container, sodass das libmpv-Render-Widget bei Playback
  // temporär an seine Stelle treten kann.
  {
    QGridLayout* gbLayout = qobject_cast<QGridLayout*>(gbCurrentFrame->layout());
    if (gbLayout) {
      const int row = 0, col = 0; // mpegWindow liegt heute auf (0,0)
      gbLayout->removeWidget(mpegWindow);

      mFrameStackContainer = new QWidget(gbCurrentFrame);
      mFrameStack = new QStackedLayout(mFrameStackContainer);
      mFrameStack->setContentsMargins(0, 0, 0, 0);
      mFrameStack->setSpacing(0);
      mFrameStack->addWidget(mpegWindow);  // Index 0 (default)
      // Index 1 wird im onPlayVideo gefüllt, sobald der Player existiert.

      mFrameStackContainer->setSizePolicy(mpegWindow->sizePolicy());
      gbLayout->addWidget(mFrameStackContainer, row, col);
    }
  }

  videoStream         = 0;
  mAVItem             = 0;
  isControlEnabled    = true;
  currentCutAVItem    = 0;
  currentCutItemIndex = -1;
  currentCutPosition  = -1;

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  pbPrevFrame->setIcon(QIcon::fromTheme("go-previous", style->standardIcon(QStyle::SP_MediaSkipBackward)));
  pbNextFrame->setIcon(QIcon::fromTheme("go-next", style->standardIcon(QStyle::SP_MediaSkipForward)));
  pbSetMarker->setIcon(QIcon::fromTheme("bookmark-new", style->standardIcon(QStyle::SP_DialogApplyButton)));
  // pbPlayVideo icon is managed by setPlayingButtonState() — Play (▶) ↔ Stop (⏹)

  connect(pbPrevFrame,  &QPushButton::clicked, this, &TTCurrentFrame::onWidgetPrevFrame);
  connect(pbNextFrame,  &QPushButton::clicked, this, &TTCurrentFrame::onWidgetNextFrame);
  connect(pbSetMarker,  &QPushButton::clicked, this, &TTCurrentFrame::onSetMarker);
  connect(pbPlayVideo,  &QPushButton::clicked, this, &TTCurrentFrame::onPlayVideo);
  connect(pbPlaySlower, &QPushButton::clicked, this, &TTCurrentFrame::onPlaySlower);
  connect(pbPlayFaster, &QPushButton::clicked, this, &TTCurrentFrame::onPlayFaster);

  // Initial stopped state: Stop and speed buttons disabled
  setPlayingButtonState(false);
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
  // Speed buttons are only enabled while playing; keep them off here
  pbPlaySlower->setEnabled(false);
  pbPlayFaster->setEnabled(false);
}


void TTCurrentFrame::clearCutContext()
{
  currentCutAVItem    = 0;
  currentCutItemIndex = -1;
  currentCutPosition  = -1;
}

//! Reflect playback state: pbPlayVideo toggles its icon between Play (▶) and
//! Stop (⏹); the speed buttons are enabled only while playing.
void TTCurrentFrame::setPlayingButtonState(bool playing)
{
  QStyle* style = QApplication::style();
  pbPlayVideo->setIcon(playing
      ? QIcon::fromTheme("media-playback-stop",  style->standardIcon(QStyle::SP_MediaStop))
      : QIcon::fromTheme("media-playback-start", style->standardIcon(QStyle::SP_MediaPlay)));
  pbPlaySlower->setEnabled(playing);
  pbPlayFaster->setEnabled(playing);
}

void TTCurrentFrame::onAVDataChanged(TTAVItem* avData)
{
	// Stop any running playback and clean up temp file
	if (mPlayer && mPlayer->isPlaying())
		mPlayer->stop();
	cleanupTempPlaybackFile();
	clearCutContext();

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

void TTCurrentFrame::onCutInChanged(const TTCutItem& cutItem)
{
	currentCutAVItem    = cutItem.avDataItem();
	currentCutItemIndex = currentCutAVItem->cutIndexOf(cutItem);
	currentCutPosition  = cutItem.cutIn();
	onGotoCutIn(cutItem.cutIn());
}

//! Returns the current frame position in stream
int TTCurrentFrame::currentFramePos()
{
  if (videoStream == nullptr) return 0;
  return videoStream->currentIndex();
}

void TTCurrentFrame::closeVideoStream()
{
  // Stop any running playback and clean up temp file
  if (mPlayer && mPlayer->isPlaying())
    mPlayer->stop();
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
  int wheelDelta      = TTSettings::instance()->stepMouseWheel();

  if ( e->modifiers() == Qt::ControlModifier )
        wheelDelta += TTSettings::instance()->stepPlusCtrl();

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

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

//! Navigate to next I-Frame
void TTCurrentFrame::onNextIFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToNextIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

//! Navigate to previous P-Frame
void TTCurrentFrame::onPrevPFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToPrevPIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

//! Navigate to next P-Frame
void TTCurrentFrame::onNextPFrame()
{
  int newFramePos;

  newFramePos = videoStream->moveToNextPIFrame( );
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

//! Navigate to previous frame (any type)
void TTCurrentFrame::onPrevBFrame()
{
  if (videoStream == 0) return;

  int newFramePos = videoStream->moveToPrevFrame();

  currentCutPosition = newFramePos;

  mpegWindow->showFrameAt(newFramePos);
  updateCurrentPosition(newFramePos);
}

//! Navigate to next frame (any type)
void TTCurrentFrame::onNextBFrame()
{
  if (videoStream == 0) return;

  int newFramePos = videoStream->moveToNextFrame();

  currentCutPosition = newFramePos;

  mpegWindow->showFrameAt(newFramePos);
  updateCurrentPosition(newFramePos);
}

//! Widget button: navigate to previous frame + auto-save CutIn if cut selected
void TTCurrentFrame::onWidgetPrevFrame()
{
  if (videoStream != 0)
    videoStream->moveToIndexPos(currentCutPosition);

  onPrevBFrame();

  if (currentCutItemIndex >= 0 && currentCutAVItem) {
    TTCutItem cutItem = currentCutAVItem->cutListItemAt(currentCutItemIndex);
    currentCutAVItem->updateCutEntry(cutItem, currentCutPosition, cutItem.cutOut());
  }
}

//! Widget button: navigate to next frame + auto-save CutIn if cut selected
void TTCurrentFrame::onWidgetNextFrame()
{
  if (videoStream != 0)
    videoStream->moveToIndexPos(currentCutPosition);

  onNextBFrame();

  if (currentCutItemIndex >= 0 && currentCutAVItem) {
    TTCutItem cutItem = currentCutAVItem->cutListItemAt(currentCutItemIndex);
    currentCutAVItem->updateCutEntry(cutItem, currentCutPosition, cutItem.cutOut());
  }
}

//! Navigate to marker position
void TTCurrentFrame::onGotoMarker(int markerPos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(markerPos);
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

void TTCurrentFrame::onSetMarker()
{
	if (videoStream == 0) return;

	emit setMarker(videoStream->currentIndex());
}

//! Goto cut in position
void TTCurrentFrame::onGotoCutIn(int pos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( newFramePos );

  updateCurrentPosition(newFramePos);
}

//! Goto cut out position
void TTCurrentFrame::onGotoCutOut(int pos)
{
  int newFramePos;

  newFramePos = videoStream->moveToIndexPos(pos);
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
}

void TTCurrentFrame::onGotoFrame(int pos)
{
  if (pos < 0) return;  // Invalid position (no match found)
  onGotoFrame(pos, 0);
}

//! Goto arbitrary frame at given position
void TTCurrentFrame::onGotoFrame(int pos, int fast)
{
  clearCutContext();

  int newFramePos;

  newFramePos = videoStream->moveToIndexPos( pos, fast );
  mpegWindow->showFrameAt( newFramePos );

  currentCutPosition = newFramePos;
  updateCurrentPosition(newFramePos);
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
  onGotoFrame(videoStream->frameCount() - 1, 0);
}

void TTCurrentFrame::updateCurrentPosition(int pos)
{
  QString szTemp;
  QString szTemp1, szTemp2;
  int actualPos   = (pos >= 0) ? pos : videoStream->currentIndex();
  int frame_type  = videoStream->frameType(actualPos);

  szTemp1 = videoStream->frameTime(actualPos).toString("hh:mm:ss.zzz");

  szTemp2 = QString(" (%1)").arg(actualPos);

  szTemp2 += ttFrameTypeTag(frame_type);

  szTemp1 += szTemp2;
  laCurrentPosition->setText( szTemp1 );

  laCurrentPosition->update();

  emit newFramePosition( actualPos );
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
      TTSettings::instance()->lastDirPath(),
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

  // Combined Play/Stop button — while playing, a click stops.
  if (mPlayer && mPlayer->isPlaying()) {
    mPlayer->stop();
    return;
  }

  // Lazily create the wrapper
  if (mPlayer == nullptr) {
    mPlayer = new TTMpvWrapper(this);
    if (QWidget* rw = mPlayer->renderWidget()) {
      // libmpv-Pfad: Widget in den Frame-Stack als Index 1 einreihen
      if (mFrameStack && mFrameStack->indexOf(rw) < 0)
        mFrameStack->addWidget(rw);
    }
    connect(mPlayer, &TTMpvWrapper::playerFinished,       this, &TTCurrentFrame::onPlaybackFinished);
    connect(mPlayer, &TTMpvWrapper::positionChanged,      this, &TTCurrentFrame::onPlaybackPositionChanged);
    connect(mPlayer, &TTMpvWrapper::playerError, this, [this](const QString& msg) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("Playback error: %1").arg(msg));
      mSpeedStep = kSpeedStepNormal;
      laPlaySpeed->setText(QString("1\xC3\x97")); // "1×"
      setPlayingButtonState(false);
    });
  }

  // Stack auf Render-Widget umschalten, bevor mpv geladen wird
  if (mFrameStack && mPlayer) {
    if (QWidget* rw = mPlayer->renderWidget())
      mFrameStack->setCurrentWidget(rw);
  }

  // Reset speed to 1× on every fresh play
  mSpeedStep = kSpeedStepNormal;
  laPlaySpeed->setText(QString("1\xC3\x97")); // "1×"

  TTAVTypes::AVStreamType stype = videoStream->streamType();
  bool isH264orH265 = (stype == TTAVTypes::h264_video || stype == TTAVTypes::h265_video);

  // Compute start position in seconds from the current frame time
  QTime frameTime = videoStream->currentFrameTime();
  double startSec = frameTime.hour() * 3600.0 + frameTime.minute() * 60.0
                    + frameTime.second() + frameTime.msec() / 1000.0;

  if (isH264orH265) {
    // ES files have no timestamps: mux into a temp MKV first
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString tempMkv = createTempMkvForPlayback();
    QApplication::restoreOverrideCursor();

    if (tempMkv.isEmpty()) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("Failed to create temp MKV for H.264/H.265 playback"));
      return;
    }
    mTempPlaybackFile = tempMkv;

    // Switch buttons: Play disabled, Stop/speed enabled — only after all early returns
    setPlayingButtonState(true);
    // Audio is already muxed into the temp MKV — no separate audio file needed
    mPlayer->load(tempMkv, startSec);
  } else {
    // MPEG-2: seek directly in the ES; pass first audio track separately if present
    QString audioFile;
    if (mAVItem->audioCount() > 0) {
      TTAudioStream* audioStream = mAVItem->audioStreamAt(0);
      if (audioStream != 0)
        audioFile = audioStream->filePath();
    }
    // Switch buttons: Play disabled, Stop/speed enabled — only after all early returns
    setPlayingButtonState(true);
    mPlayer->load(videoStream->filePath(), startSec, audioFile);
  }
}

//! Called repeatedly by TTMpvWrapper with the current playback position in seconds.
//! Updates the timecode / frame-position display without moving the decoded-frame stream
//! or triggering any decode work — mpv is drawing into mpegWindow via --wid.
void TTCurrentFrame::onPlaybackPositionChanged(double seconds)
{
  if (videoStream == nullptr) return;

  float frameRate = videoStream->frameRate();
  if (frameRate <= 0.0f) return;

  int framePos = static_cast<int>(std::floor(seconds * static_cast<double>(frameRate)));
  if (framePos < 0) framePos = 0;
  if (framePos >= static_cast<int>(videoStream->frameCount()))
    framePos = static_cast<int>(videoStream->frameCount()) - 1;

  // updateCurrentPosition(pos) with an explicit index only reads metadata
  // (frameType / frameTime) — no stream seek, no decode, no cut-position write.
  // It emits newFramePosition which advances the slider and stores the integer
  // in TTAVData; both are safe to call at playback rate (~25 Hz).
  updateCurrentPosition(framePos);
}

//! Called by TTMpvWrapper when playback finishes (natural end or stop())
void TTCurrentFrame::onPlaybackFinished()
{
  if (videoStream == nullptr) return;

  if (mFrameStack && mpegWindow)
    mFrameStack->setCurrentWidget(mpegWindow);

  double playbackPos = mPlayer->playbackPosition();
  double frameRate   = videoStream->frameRate();

  int newFrame = static_cast<int>(std::floor(playbackPos * frameRate));
  if (newFrame < 0) newFrame = 0;
  if (newFrame >= static_cast<int>(videoStream->frameCount()))
    newFrame = videoStream->frameCount() - 1;

  if (TTSettings::instance()->logUI())
    qDebug() << "Playback finished: pos" << playbackPos << "s -> frame" << newFrame
             << "(rate:" << frameRate << ")";

  videoStream->moveToIndexPos(newFrame);
  mpegWindow->showFrameAt(newFrame);
  mpegWindow->invalidateDisplay();
  updateCurrentPosition(newFrame);

  // Reset speed display and restore stopped button state
  mSpeedStep = kSpeedStepNormal;
  laPlaySpeed->setText(QString("1\xC3\x97")); // "1×"
  setPlayingButtonState(false);

  cleanupTempPlaybackFile();
}

//! Clean up temporary playback file
void TTCurrentFrame::cleanupTempPlaybackFile()
{
  if (!mTempPlaybackFile.isEmpty()) {
    if (QFile::exists(mTempPlaybackFile)) {
      QFile::remove(mTempPlaybackFile);
      if (TTSettings::instance()->logUI())
          qDebug() << "Removed temp playback file:" << mTempPlaybackFile;
    }
    mTempPlaybackFile.clear();
  }
}

//! Decrease playback speed by one step
void TTCurrentFrame::onPlaySlower()
{
  if (mSpeedStep > 0) {
    --mSpeedStep;
    applySpeedStep();
  }
}

//! Increase playback speed by one step
void TTCurrentFrame::onPlayFaster()
{
  if (mSpeedStep < static_cast<int>(std::size(kSpeedSteps)) - 1) {
    ++mSpeedStep;
    applySpeedStep();
  }
}

//! Apply current speed step to the player and update the label
void TTCurrentFrame::applySpeedStep()
{
  double f = kSpeedSteps[mSpeedStep];
  if (mPlayer)
    mPlayer->setSpeed(f);

  // Format: -4×, -2×, 1×, 2×, 4×  (U+00D7 = ×, UTF-8: 0xC3 0x97)
  long fi = static_cast<long>(f);
  laPlaySpeed->setText(QString("%1\xC3\x97").arg(fi));
}

//! Create a temporary MKV file for H.264/H.265 playback
//! This muxes the ES video and audio so mpv can seek and sync properly
QString TTCurrentFrame::createTempMkvForPlayback()
{
  QString tempMkv = QDir(TTSettings::instance()->tempDirPath()).filePath("playback_temp.mkv");

  // Remove old temp file if exists
  QFile::remove(tempMkv);

  // Get frame rate and A/V offset from .info file
  double frameRate = videoStream->frameRate();
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(videoStream->filePath());
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (frameRate <= 0 && esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
      }
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        avOffsetMs = esInfo.avOffsetMs();
        if (TTSettings::instance()->logUI())
            qDebug() << "Playback: A/V sync offset from .info:" << avOffsetMs << "ms";
      }
    }
  }

  // Set up MKV muxer
  int frameDurationNs = static_cast<int>(1000000000.0 / frameRate);
  TTMkvMergeProvider mkvProvider;
  mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
  mkvProvider.setIsPAFF(videoStream->isPAFF(), videoStream->paffLog2MaxFrameNum());
  {
    AVCodecID codecId;
    switch (videoStream->streamType()) {
      case TTAVTypes::h265_video: codecId = AV_CODEC_ID_HEVC;       break;
      case TTAVTypes::h264_video: codecId = AV_CODEC_ID_H264;       break;
      default:                    codecId = AV_CODEC_ID_MPEG2VIDEO; break;
    }
    mkvProvider.setVideoCodecId(codecId);
  }
  if (avOffsetMs != 0) {
    mkvProvider.setAudioSyncOffset(avOffsetMs);
  }

  // Collect audio file(s)
  QStringList audioFiles;
  if (mAVItem->audioCount() > 0) {
    TTAudioStream* audioStream = mAVItem->audioStreamAt(0);
    if (audioStream != 0) {
      audioFiles << audioStream->filePath();
    }
  }

  if (TTSettings::instance()->logUI())
      qDebug() << "Creating temp MKV via libav:" << videoStream->filePath();

  if (!mkvProvider.mux(tempMkv, videoStream->filePath(), audioFiles)) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Temp MKV creation failed: %1").arg(mkvProvider.lastError()));
    return QString();
  }

  if (TTSettings::instance()->logUI())
      qDebug() << "Temp MKV created:" << tempMkv;
  return tempMkv;
}

