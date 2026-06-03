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
#include "ttmpvrenderwidget.h"
#include "../avstream/ttmpeg2videostream.h"
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
#include <QTimer>
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
      // StackAll statt des Default StackOne: ALLE Stack-Widgets bleiben sichtbar,
      // das aktuelle wird nur nach vorn gehoben. Nötig, weil das libmpv-
      // renderWidget im Hintergrund rendern können MUSS, bevor es nach vorn
      // geschaltet wird — ein verstecktes QOpenGLWidget bekommt kein paintGL,
      // sonst entstünde ein Deadlock (Switch wartet auf den ersten echten
      // Frame, der Frame braucht aber Sichtbarkeit). Das opake mpegWindow
      // verdeckt das hinten rendernde renderWidget vollständig, sodass dessen
      // erster (stale) Frame nie sichtbar wird.
      mFrameStack->setStackingMode(QStackedLayout::StackAll);
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

	// Player + renderWidget jetzt erzeugen und GL/Render-Context initialisieren,
	// damit er lange vor dem ersten PLAY bereitsteht (sonst scheitert der erste
	// Play an "No render context set"; siehe ensurePlayerCreated).
	ensurePlayerCreated();

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
//! Create the mpv player + render widget once and initialize its GL/render
//! context. Called at stream-open (onAVDataChanged) so the context exists long
//! before the first PLAY. Idempotent — does nothing if the player already exists.
void TTCurrentFrame::ensurePlayerCreated()
{
  if (mPlayer != nullptr) return;

  mPlayer = new TTMpvWrapper(this);
  if (QWidget* rw = mPlayer->renderWidget()) {
    // libmpv-Pfad: Widget in den Frame-Stack als Index 1 einreihen
    if (mFrameStack && mFrameStack->indexOf(rw) < 0) {
      mFrameStack->addWidget(rw);
      // GL-Context EINMAL realisieren: das renderWidget kurz nach vorn schalten
      // erzwingt show→initializeGL und damit einen voll initialisierten
      // QOpenGLContext (makeCurrent allein reicht bei einem versteckten
      // QOpenGLWidget NICHT — mpv_render_context_create scheitert sonst mit
      // "Can't load OpenGL functions"). Danach baut prepareRenderContext den
      // mpv-Render-Context auf und wir schalten sofort zurück auf mpegWindow.
      // Im StackAll-Modus bleibt mpegWindow vorne (Standbild), bis firstFrame
      // Ready beim Play umschaltet.
      mFrameStack->setCurrentWidget(rw);
      if (auto* mrw = qobject_cast<TTMpvRenderWidget*>(rw)) {
        if (!mrw->prepareRenderContext())
          TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("ensurePlayerCreated: prepareRenderContext failed"));
      }
      mFrameStack->setCurrentWidget(mpegWindow);
    }
  }
  connect(mPlayer, &TTMpvWrapper::playerFinished,  this, &TTCurrentFrame::onPlaybackFinished);
  connect(mPlayer, &TTMpvWrapper::positionChanged, this, &TTCurrentFrame::onPlaybackPositionChanged);
  connect(mPlayer, &TTMpvWrapper::playerError, this, [](const QString& msg) {
    // Nur loggen, NICHT setPlayingButtonState(false). mpv klassifiziert viele
    // non-fatale Decoder-Warnings (h264 mmco, reference-frame-exceeds-max bei
    // mid-stream-seek auf MBAFF/PAFF, etc.) als "error"-Level. Wenn mpv wirklich
    // abnormal terminiert, kommt playerFinished → onPlaybackFinished resetet die
    // UI sauber.
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Playback error: %1").arg(msg));
  });
}

void TTCurrentFrame::onPlayVideo()
{
  if (videoStream == 0 || mAVItem == 0) return;

  // Combined Play/Stop button — while playing, a click stops.
  if (mPlayer && mPlayer->isPlaying()) {
    mPlayer->stop();
    return;
  }

  // Player + renderWidget werden beim Stream-Open (ensurePlayerCreated, via
  // onAVDataChanged) erzeugt und ihr GL-Context dort initialisiert. Als Netz
  // hier nochmal sicherstellen, falls onPlayVideo ohne vorheriges Open läuft.
  ensurePlayerCreated();

  // Stack-Switch zu renderWidget erst, wenn das Widget seinen ZWEITEN echten
  // mpv-Frame gerendert hat (firstFrameReady). Per Log belegt: mpv liefert nach
  // dem Lade-Seek den ersten Frame stale — den GOP-Keyframe vor dem Ziel, bei
  // einem Cut-In nach Werbung also einen Werbe-Frame; er trägt zwar die korrekte
  // time-pos, aber den falschen Bildinhalt. Erst ab dem zweiten Render stimmt
  // der Inhalt. Bis dahin bleibt das mpegWindow mit dem gewählten Standbild
  // vorne. Würden wir schon bei PLAYBACK_RESTART umschalten, würde dieser
  // stale erste Frame für einen Moment sichtbar. Gilt für alle Codecs.
  if (mFrameStack && mPlayer) {
    if (auto* rw = qobject_cast<TTMpvRenderWidget*>(mPlayer->renderWidget())) {
      auto conn = std::make_shared<QMetaObject::Connection>();
      *conn = connect(rw, &TTMpvRenderWidget::firstFrameReady, this,
                      [this, rw, conn]() {
        if (mFrameStack)
          mFrameStack->setCurrentWidget(rw);
        QObject::disconnect(*conn);
      });
    }
  }

  // Reset speed to 1× on every fresh play
  mSpeedStep = kSpeedStepNormal;
  laPlaySpeed->setText(QString("1\xC3\x97")); // "1×"

  TTAVTypes::AVStreamType stype = videoStream->streamType();
  bool isH264orH265 = (stype == TTAVTypes::h264_video || stype == TTAVTypes::h265_video);

  // Compute start position in seconds from the current frame time.
  // Default: naive currentIndex / frameRate.
  QTime frameTime = videoStream->currentFrameTime();
  double startSec = frameTime.hour() * 3600.0 + frameTime.minute() * 60.0
                    + frameTime.second() + frameTime.msec() / 1000.0;

  // MPEG-2 field-picture-Korrektur: bei interlaced Stream enthält der
  // videoStream-Index einen Eintrag pro Picture (frame_picture ODER
  // jeweils ein top/bottom field_picture). Display-Frames = Index minus
  // extras. mpv positioniert per echter Stream-Sekunde, also umrechnen.
  if (auto* mpeg2vs = dynamic_cast<TTMpeg2VideoStream*>(videoStream)) {
    const QList<int>& extras = mpeg2vs->extraIndices();
    if (!extras.isEmpty()) {
      int idx = videoStream->currentIndex();
      int lo = 0, hi = extras.size();
      while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (extras[mid] < idx) lo = mid + 1;
        else hi = mid;
      }
      int displayIdx = idx - lo;
      startSec = static_cast<double>(displayIdx) / static_cast<double>(videoStream->frameRate());
    }
  }

  // H.264/H.265 decode-order vs display-order correction. The app frame index is
  // decode order, but mpv seeks by display time. decodeFrame() records the true
  // decode-order index of the frame it delivers for the current (decode-order)
  // position in TTFrameInfo::deliveredDecodeIndex. The temp playback MKV assigns
  // PTS in decode order (pts = frameCount * frameDur), so the displayed frame's
  // time is deliveredDecodeIndex / frameRate. Without this, mpv lands on a
  // different frame than the still shown in mpegWindow (e.g. the GOP keyframe
  // before the cut-in). Read from mpegWindow's wrapper — the one that decoded
  // the visible still. Falls back to currentIndex on -1 (frame never decoded).
  if (isH264orH265 && mpegWindow && mpegWindow->ffmpegWrapper()) {
    int idx     = videoStream->currentIndex();
    int decIdx  = mpegWindow->ffmpegWrapper()->frameAt(idx).deliveredDecodeIndex;
    float fr    = videoStream->frameRate();
    if (decIdx >= 0 && fr > 0.0f) {
      startSec = static_cast<double>(decIdx) / static_cast<double>(fr);
    } else {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("onPlayVideo: deliveredDecodeIndex unavailable for frame %1 "
                "(decIdx=%2), falling back to currentIndex/frameRate")
          .arg(idx).arg(decIdx));
    }
  }

  if (isH264orH265) {
    // ES files have no timestamps: mux into a temp MKV first. The temp MKV is
    // cached across STOP→PLAY cycles — re-muxing the whole ES (~5 s) is only
    // needed when the source (video/audio path) changed. STOP no longer deletes
    // it (see onPlaybackFinished); the fingerprint guards reuse.
    QString fp = playbackSourceFingerprint();
    bool cacheValid = !mTempPlaybackFile.isEmpty()
                      && fp == mCachedPlaybackFingerprint
                      && QFile::exists(mTempPlaybackFile);

    if (!cacheValid) {
      cleanupTempPlaybackFile();   // drop a stale cache (e.g. source changed)
      QApplication::setOverrideCursor(Qt::WaitCursor);
      QString tempMkv = createTempMkvForPlayback();
      QApplication::restoreOverrideCursor();

      if (tempMkv.isEmpty()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("Failed to create temp MKV for H.264/H.265 playback"));
        return;
      }
      mTempPlaybackFile = tempMkv;
      mCachedPlaybackFingerprint = fp;
    }

    // Switch buttons: Play disabled, Stop/speed enabled — only after all early returns
    setPlayingButtonState(true);
    // Audio is already muxed into the temp MKV — no separate audio file needed
    mPlayer->load(mTempPlaybackFile, startSec);
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

  double frameRate   = videoStream->frameRate();

  // Stop-Position aus dem ZULETZT GERENDERTEN Frame, nicht aus time-pos.
  // Bei vo=libmpv hängt die Anzeige der internen mpv-Clock um eine feste
  // Pipeline-Tiefe (~16 Frames) hinterher: time-pos meldet bereits Frame N,
  // während das renderWidget noch Frame N-16 zeigt. Würden wir time-pos
  // nehmen, spränge das Standbild beim Stop sichtbar nach vorn. lastRendered
  // TimePos ist die time-pos, die paintGL beim zuletzt tatsächlich gemalten
  // Frame gelesen hat — also genau das, was der Nutzer beim Stop SAH. Fallback
  // auf playbackPosition(), falls noch nichts gerendert wurde.
  double playbackPos = mPlayer->playbackPosition();
  if (auto* mrw = qobject_cast<TTMpvRenderWidget*>(mPlayer->renderWidget())) {
    double lr = mrw->lastRenderedTimePos();
    if (lr >= 0.0)
      playbackPos = lr;
  }

  // MPEG-2-Korrektur: videoStream zählt field-pictures als eigene Index-
  // Einträge, mpv's time-pos respektiert nur Display-Frames. Die Konversion:
  //     time = (rawIndex - extras_before(rawIndex)) / fps
  // Wir lösen nach `rawIndex` per Fixpunkt: starten mit baseFrame=round(time*fps)
  // und addieren extras_before bis stabil. Quelle der extras ist der Bitstream-
  // Parser (TTMpeg2VideoStream::extraIndices), NICHT das .info-File (das ist
  // für unsere Recordings oft leer; TTAVData::countExtraFramesBefore wäre dann 0).
  int baseFrame = static_cast<int>(std::round(playbackPos * frameRate));
  int newFrame  = baseFrame;
  if (auto* mpeg2vs = dynamic_cast<TTMpeg2VideoStream*>(videoStream)) {
    const QList<int>& extras = mpeg2vs->extraIndices();
    if (!extras.isEmpty()) {
      for (int it = 0; it < 5; ++it) {
        // count extras strictly less than newFrame
        int lo = 0, hi = extras.size();
        while (lo < hi) {
          int mid = (lo + hi) / 2;
          if (extras[mid] < newFrame) lo = mid + 1;
          else hi = mid;
        }
        int corrected = baseFrame + lo;
        if (corrected == newFrame) break;
        newFrame = corrected;
      }
    }
  }
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

  // NOTE: the temp playback MKV is deliberately NOT deleted here. It is cached
  // so a subsequent PLAY of the same source reuses it instead of re-muxing the
  // whole ES (~5 s). It is dropped on source change / close (onAVDataChanged,
  // closeVideoStream) and invalidated via fingerprint in onPlayVideo.
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
  mCachedPlaybackFingerprint.clear();
}

//! Fingerprint of everything that determines the playback temp MKV's content.
//! Used to decide whether a cached temp MKV can be reused on a subsequent PLAY
//! instead of re-muxing the whole ES (~5 s). Covers exactly what
//! createTempMkvForPlayback() feeds into the muxer: the video file path and the
//! first audio track's path. startSec (mpv --start), subtitles (mpegWindow
//! overlay only) and the UI audio delay (final cut only) deliberately do NOT
//! enter the MKV and are therefore excluded.
QString TTCurrentFrame::playbackSourceFingerprint() const
{
  if (videoStream == nullptr) return QString();
  QString fp = videoStream->filePath();
  if (mAVItem != nullptr && mAVItem->audioCount() > 0) {
    TTAudioStream* a0 = mAVItem->audioStreamAt(0);
    if (a0 != nullptr)
      fp += QLatin1Char('|') + a0->filePath();
  }
  return fp;
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
  QString tempMkv = QDir(TTSettings::instance()->tempDirPath()).filePath("ttcut-ng_playback_temp.mkv");

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

