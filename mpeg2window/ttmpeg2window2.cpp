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
// TTMPEG2WINDOW
// ----------------------------------------------------------------------------

#include "ttmpeg2window2.h"
#include "../avstream/ttavstream.h"
#include "../avstream/tth26xvideostream.h"  // provideFrameIndexTo (index sharing)

#include <QDebug>
#include <QMouseEvent>

/*!
 * TTMPEG2Window2
 */
TTMPEG2Window2::TTMPEG2Window2(QWidget *parent )
: QLabel(parent)
{
  log = TTMessageLogger::getInstance();

  this->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  this->setAutoFillBackground(true);
  // Rahmen MUSS im Stylesheet entfernt werden: sobald ein QWidget ein Stylesheet
  // hat, übernimmt der Stylesheet-Mechanismus das Rahmen-Rendering und ignoriert
  // setFrameShape(). Der ~2px helle Default-QFrame-Rand blieb deshalb trotz
  // QFrame::NoFrame sichtbar. Das libmpv-renderWidget im selben Stack hat keinen
  // Rahmen — beim Umschalten Standbild↔Video erschien/verschwand der Rand als
  // sichtbares "Zucken". `border: none` macht beide Darstellungen deckungsgleich.
  this->setStyleSheet("QLabel { background-color: black; border: none; }");

  mpVideoStream    = 0;
  mpSubtitleStream = 0;
  mpeg2Decoder     = 0;
  mpFFmpegWrapper  = 0;
  mUseFFmpeg       = false;
  currentIndex     = 0;
  picBuffer        = 0;
  videoWidth       = 0;
  videoHeight      = 0;
  frameInfo        = 0;
  mLogoSelectionMode = false;
  mRubberBand      = nullptr;
}

/*!
 * resizeEvent
 */
void TTMPEG2Window2::resizeEvent (QResizeEvent*)
{
	showVideoFrame();
}

/*!
 * show the current video frame (picBuffer or FFmpeg QImage)
 */
void TTMPEG2Window2::showVideoFrame()
{
  QImage frameToShow;

  if (mUseFFmpeg) {
    // Use FFmpeg decoded frame
    if (mCurrentFrame.isNull()) return;
    frameToShow = mCurrentFrame;
    videoWidth = frameToShow.width();
    videoHeight = frameToShow.height();
  } else {
    // Use MPEG-2 decoder
    if (mpeg2Decoder == 0) return;
    if (frameInfo    == 0) return;
    if (picBuffer    == 0) return;

    frameToShow = QImage(picBuffer, videoWidth, videoHeight, QImage::Format_RGB32);
  }

  float scaleFactorY = 1.0;

  if (mpVideoStream != 0 && !mUseFFmpeg) {
    TTSequenceHeader* seqHeader = mpVideoStream->getSequenceHeader(currentIndex);
    if (seqHeader != 0 && seqHeader->aspectRatio() == 3) {
      scaleFactorY = (float)(videoWidth*9.0/(videoHeight*16.0));
    }
  }

  QImage scale = frameToShow.scaled(videoWidth, videoHeight*scaleFactorY, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

  // Draw subtitle overlay if available
  QString subtitleText = getSubtitleTextAtCurrentFrame();
  if (!subtitleText.isEmpty()) {
    drawSubtitleOnImage(scale, subtitleText);
  }

  this->setPixmap(QPixmap::fromImage(scale.scaled(width(), height(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

/*!
 * Set subtitle stream for overlay display
 */
void TTMPEG2Window2::setSubtitleStream(TTSubtitleStream* subtitleStream)
{
  mpSubtitleStream = subtitleStream;
}

/*!
 * Clear subtitle stream
 */
void TTMPEG2Window2::clearSubtitleStream()
{
  mpSubtitleStream = 0;
}

/*!
 * Get subtitle text at current frame position
 */
QString TTMPEG2Window2::getSubtitleTextAtCurrentFrame()
{
  if (mpSubtitleStream == 0) return QString();
  if (mpVideoStream == 0) return QString();

  // Calculate current time in milliseconds
  float frameRate = mpVideoStream->frameRate();
  if (frameRate <= 0) frameRate = 25.0;

  int currentTimeMs = (int)((currentIndex / frameRate) * 1000.0);

  // Get subtitle header list and search for subtitle at current time
  TTSubtitleHeaderList* headerList = mpSubtitleStream->headerList();
  if (headerList == 0) return QString();

  int index = headerList->searchTimeIndex(currentTimeMs);
  if (index < 0) return QString();

  TTSubtitleHeader* header = headerList->subtitleHeaderAt(index);
  if (header == 0) return QString();

  // Check if current time is within subtitle time range
  if (currentTimeMs >= header->startMSec() && currentTimeMs <= header->endMSec()) {
    return header->text();
  }

  return QString();
}

/*!
 * Draw subtitle text on image
 */
void TTMPEG2Window2::drawSubtitleOnImage(QImage& image, const QString& text)
{
  QPainter painter(&image);

  // Calculate font size based on image height (approx. 5% of height)
  int fontSize = qMax(12, image.height() / 20);
  QFont font("Sans", fontSize, QFont::Bold);
  painter.setFont(font);

  // Draw text outline (black) for better visibility
  QPen outlinePen(Qt::black);
  outlinePen.setWidth(2);
  painter.setPen(outlinePen);

  QRect textRect = image.rect();
  textRect.setTop(image.height() - fontSize * 3);  // Position at bottom

  // Draw outline by drawing text multiple times with offset
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx != 0 || dy != 0) {
        QRect offsetRect = textRect.translated(dx, dy);
        painter.drawText(offsetRect, Qt::AlignBottom | Qt::AlignHCenter | Qt::TextWordWrap, text);
      }
    }
  }

  // Draw main text (white/yellow)
  painter.setPen(Qt::yellow);
  painter.drawText(textRect, Qt::AlignBottom | Qt::AlignHCenter | Qt::TextWordWrap, text);

  painter.end();
}

/*!
 * Invalidate the display cache so the next moveToVideoFrame() forces a re-decode
 */
void TTMPEG2Window2::invalidateDisplay()
{
  currentIndex = -1;
}

void TTMPEG2Window2::showFrameAt(int index)
{
  moveToVideoFrame(index);
}

/*!
 * Open a video file and assign the mpeg2 decoder object
 */
void TTMPEG2Window2::openVideoFile( QString fName, TTVideoIndexList* viIndex, TTVideoHeaderList* viHeader )
{
	if (fName.isEmpty()) return;

  if (mpeg2Decoder != 0) delete mpeg2Decoder;

  try
  {
  	mpeg2Decoder = new TTMpeg2Decoder(fName, viIndex, viHeader);
  }
  catch (TTMpeg2DecoderException ex)
  {
  	log->errorMsg(__FILE__, __LINE__, ex.message());
  }
}

/*!
 * openVideoStream - supports MPEG-2, H.264, and H.265
 */
void TTMPEG2Window2::openVideoStream(TTVideoStream* vStream)
{
  qDebug() << "TTMPEG2Window2::openVideoStream() called";
  mpVideoStream = vStream;
  currentIndex = -1;

  // Check stream type
  TTAVTypes::AVStreamType streamType = vStream->streamType();
  qDebug() << "Stream type:" << streamType;

  if (streamType == TTAVTypes::h264_video || streamType == TTAVTypes::h265_video) {
    // Use FFmpeg for H.264/H.265
    mUseFFmpeg = true;
    qDebug() << "Using FFmpeg for H.264/H.265";

    if (mpFFmpegWrapper != 0) {
      mpFFmpegWrapper->closeFile();
      delete mpFFmpegWrapper;
    }

    mpFFmpegWrapper = new TTFFmpegWrapper();
    if (!mpFFmpegWrapper->openFile(vStream->filePath())) {
      log->errorMsg(__FILE__, __LINE__,
          QString("Failed to open H.264/H.265 stream: %1").arg(mpFFmpegWrapper->lastError()));
      delete mpFFmpegWrapper;
      mpFFmpegWrapper = 0;
      return;
    }

    // Index sharing (spec 2026-06-05): Owner A (vStream->mFFmpeg) already built
    // the frame index at stream-open. Instead of running an identical second scan
    // of the same file here (~2 s), we adopt Owner A's index (Qt COW, cheap).
    // Consumers that in turn pull from THIS wrapper (Black/Scene/Logo search,
    // analysisWrapper) thus transitively get Owner A's index too. Falls back to
    // building our own if vStream is not an H.26x stream or its index is not
    // available yet.
    bool indexAdopted = false;
    if (TTH26xVideoStream* h26x = dynamic_cast<TTH26xVideoStream*>(vStream)) {
      indexAdopted = h26x->provideFrameIndexTo(mpFFmpegWrapper);
    }
    if (!indexAdopted) {
      qDebug() << "Building frame index for preview...";
      if (!mpFFmpegWrapper->buildFrameIndex()) {
        log->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(mpFFmpegWrapper->lastError()));
      }
    }
    qDebug() << (indexAdopted ? "Frame index adopted:" : "Frame index built:")
             << mpFFmpegWrapper->frameCount() << "frames"
             << "(videoStream:" << vStream->frameCount() << "headers)";

    qDebug() << "Opened H.264/H.265 stream with FFmpeg decoder";
  } else {
    // Use MPEG-2 decoder for MPEG-2 streams
    mUseFFmpeg = false;
    qDebug() << "Using MPEG-2 decoder";
    TTMpeg2VideoStream* mpeg2Stream = dynamic_cast<TTMpeg2VideoStream*>(vStream);
    if (mpeg2Stream) {
      openVideoFile(mpeg2Stream->filePath(), mpeg2Stream->indexList(), mpeg2Stream->headerList());
    }
  }
  qDebug() << "TTMPEG2Window2::openVideoStream() done";
}

/*!
 * Close video stream
 */
void TTMPEG2Window2::closeVideoStream()
{
  // Clean up FFmpeg decoder
  if (mpFFmpegWrapper != 0)
  {
    mpFFmpegWrapper->closeFile();
    delete mpFFmpegWrapper;
    mpFFmpegWrapper = 0;
    mCurrentFrame = QImage();
  }

  // Clean up MPEG-2 decoder
  if (mpeg2Decoder != 0)
  {
    delete mpeg2Decoder;
    mpeg2Decoder = 0;
    picBuffer    = 0;
  }

  mUseFFmpeg = false;
  mpVideoStream = 0;
  currentIndex = 0;

  QImage dummy;
  this->setPixmap(QPixmap::fromImage(dummy));
  repaint();
}

static float histogramDifference(const int histA[256], const int histB[256],
                                  int totalA, int totalB)
{
    float diff = 0.0f;
    for (int i = 0; i < 256; i++) {
        diff += qAbs((float)histA[i]/totalA - (float)histB[i]/totalB);
    }
    return diff / 2.0f;  // normalize to 0.0–1.0
}

void TTMPEG2Window2::moveToVideoFrame(int iFramePos)
{
  if (iFramePos == currentIndex) return;

  if (mUseFFmpeg) {
    // Use FFmpeg decoder for H.264/H.265
    if (mpFFmpegWrapper == 0) return;

    mCurrentFrame = mpFFmpegWrapper->decodeFrame(iFramePos);
    if (!mCurrentFrame.isNull()) {
      currentIndex = iFramePos;
      showVideoFrame();
    }
    return;
  }

  // Use MPEG-2 decoder
	if (mpeg2Decoder == 0) return;

	try
	{
		mpeg2Decoder->moveToFrameIndex(iFramePos);
		currentIndex = iFramePos;
		getFrameInfo();
	}
	catch (TTMpeg2DecoderException ex)
	{
		log->errorMsg(__FILE__, __LINE__, ex.message());
	}

  showDecodedSlice();
}

/*!
 * Save current frame to file (jpeg, png, bmp)
 */
void TTMPEG2Window2::saveCurrentFrame(QString fName, const char* format)
{
  if (picBuffer == 0) return;

  QImage screenShot(picBuffer, videoWidth, videoHeight, QImage::Format_RGB32);
  screenShot.save(fName, format);
}

/*!
 * Show current decoded slice
 */
void TTMPEG2Window2::showDecodedSlice()
{
	if (mpeg2Decoder == 0) return;

	getFrameInfo();
  showVideoFrame();
}

/*!
 * Decode current video frame and show the resulting slice
 */
/*!
 * getFrameInfo
 */
void TTMPEG2Window2::getFrameInfo()
{
	frameInfo = mpeg2Decoder->getFrameInfo();
	if (frameInfo == 0) {
		qDebug("getFrameInfo: frameInfo is NULL");
		picBuffer = 0;
		return;
	}
	picBuffer   = frameInfo->Y;
	videoWidth  = frameInfo->width;
	videoHeight = frameInfo->height;
}

// ---------------------------------------------------------------------------
// Coordinate transform helpers
// ---------------------------------------------------------------------------

QRect TTMPEG2Window2::currentPixmapRect() const
{
  QPixmap pm = pixmap(Qt::ReturnByValue);
  if (pm.isNull()) return QRect();

  QSize pmSize = pm.size();
  int x = (width() - pmSize.width()) / 2;
  int y = (height() - pmSize.height()) / 2;
  return QRect(x, y, pmSize.width(), pmSize.height());
}

QRect TTMPEG2Window2::imageToWidgetRect(const QRect& imageRect) const
{
  QRect pmRect = currentPixmapRect();
  if (pmRect.isEmpty() || videoWidth <= 0 || videoHeight <= 0) return QRect();

  float scaleX = (float)pmRect.width() / videoWidth;
  float scaleY = (float)pmRect.height() / videoHeight;

  return QRect(
    pmRect.x() + (int)(imageRect.x() * scaleX),
    pmRect.y() + (int)(imageRect.y() * scaleY),
    (int)(imageRect.width() * scaleX),
    (int)(imageRect.height() * scaleY)
  );
}

QRect TTMPEG2Window2::widgetToImageRect(const QRect& widgetRect) const
{
  QRect pmRect = currentPixmapRect();
  if (pmRect.isEmpty() || videoWidth <= 0 || videoHeight <= 0) return QRect();

  float scaleX = (float)videoWidth / pmRect.width();
  float scaleY = (float)videoHeight / pmRect.height();

  QRect imageRect(
    (int)((widgetRect.x() - pmRect.x()) * scaleX),
    (int)((widgetRect.y() - pmRect.y()) * scaleY),
    (int)(widgetRect.width() * scaleX),
    (int)(widgetRect.height() * scaleY)
  );

  return imageRect.intersected(QRect(0, 0, videoWidth, videoHeight));
}

// ---------------------------------------------------------------------------
// Logo ROI selection mode
// ---------------------------------------------------------------------------

void TTMPEG2Window2::setLogoSelectionMode(bool enable)
{
  mLogoSelectionMode = enable;
  setCursor(enable ? Qt::CrossCursor : Qt::ArrowCursor);
  if (!enable && mRubberBand) {
    mRubberBand->hide();
  }
}

void TTMPEG2Window2::mousePressEvent(QMouseEvent* event)
{
  if (mLogoSelectionMode && event->button() == Qt::LeftButton) {
    mRubberBandOrigin = event->pos();
    if (!mRubberBand)
      mRubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    mRubberBand->setGeometry(QRect(mRubberBandOrigin, QSize()));
    mRubberBand->show();
    return;
  }
  QLabel::mousePressEvent(event);
}

void TTMPEG2Window2::mouseMoveEvent(QMouseEvent* event)
{
  if (mLogoSelectionMode && mRubberBand && mRubberBand->isVisible()) {
    mRubberBand->setGeometry(QRect(mRubberBandOrigin, event->pos()).normalized());
    return;
  }
  QLabel::mouseMoveEvent(event);
}

void TTMPEG2Window2::mouseReleaseEvent(QMouseEvent* event)
{
  if (mLogoSelectionMode && mRubberBand && event->button() == Qt::LeftButton) {
    mRubberBand->hide();
    QRect widgetRect = QRect(mRubberBandOrigin, event->pos()).normalized();
    QRect imageRect = widgetToImageRect(widgetRect);

    if (imageRect.width() >= 4 && imageRect.height() >= 4) {
      mLogoSelectionMode = false;
      setCursor(Qt::ArrowCursor);
      emit logoROISelected(imageRect);
    }
    return;
  }
  QLabel::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Logo ROI overlay drawing
// ---------------------------------------------------------------------------

void TTMPEG2Window2::setLogoROIOverlay(const QRect& imageCoords)
{
  mLogoROIOverlay = imageCoords;
  update();
}

void TTMPEG2Window2::clearLogoROIOverlay()
{
  mLogoROIOverlay = QRect();
  update();
}

void TTMPEG2Window2::paintEvent(QPaintEvent* event)
{
  QLabel::paintEvent(event);

  if (mLogoROIOverlay.isValid()) {
    QRect widgetRect = imageToWidgetRect(mLogoROIOverlay);
    if (!widgetRect.isEmpty()) {
      QPainter painter(this);
      painter.setPen(QPen(QColor(0xcc, 0x44, 0xcc), 1));  // #cc44cc magenta
      painter.drawRect(widgetRect);
    }
  }
}

// ---------------------------------------------------------------------------
// Grab current frame as QImage
// ---------------------------------------------------------------------------

QImage TTMPEG2Window2::grabFrameImage() const
{
  if (mUseFFmpeg)
    return mCurrentFrame;

  if (picBuffer && videoWidth > 0 && videoHeight > 0)
    return QImage(picBuffer, videoWidth, videoHeight, QImage::Format_RGB32).copy();

  return QImage();
}
