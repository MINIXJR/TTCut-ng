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
#include "../avstream/tth26xvideostream.h"  // frameIndexBundle (index sharing)
#include "../avstream/ttframeindexer.h"

#include <QDebug>
#include <QMouseEvent>
#include <QTextDocument>
#include <QtMath>

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
  mSubtitleDelayMs = 0;
  mpeg2Decoder     = 0;
  mpFFmpegWrapper  = 0;
  mUseFFmpeg       = false;
  currentIndex     = 0;
  mAspectIndex     = 0;
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
// Aspect correction applied before the widget scaling: MPEG-2 signals the
// display aspect of the whole picture (DAR), H.26x the sample aspect (SAR);
// both are corrected in the upscale direction so no detail is lost.
void TTMPEG2Window2::computeDisplayScale(float& scaleFactorX, float& scaleFactorY) const
{
  if (mpVideoStream != 0 && !mUseFFmpeg && videoWidth > 0 && videoHeight > 0) {
    // MPEG-2 aspect_ratio_information signals the DISPLAY aspect ratio of
    // the whole picture (2 = 4:3, 3 = 16:9, 4 = 2.21:1), unlike H.26x SAR.
    // Correct in the upscale direction so no detail is lost before the
    // final widget scaling (same principle as the FFmpeg branch below);
    // mpv playback applies the same aspect, so still and playback keep the
    // same shape. Code 1 (square samples) needs no correction. This used
    // to handle only 16:9 — and shrank the height for it — leaving 4:3
    // material displayed at its 720/576 storage aspect.
    // mAspectIndex, NOT currentIndex: invalidateDisplay() parks currentIndex
    // at -1, and getSequenceHeader(-1) falls back to the file's FIRST
    // sequence header — the wrong aspect once the recording changes shape
    // (measured: 16:9 frame squeezed to 4:3 after stop + resize on a
    // recording that starts with a 4:3 block).
    TTSequenceHeader* seqHeader = mpVideoStream->getSequenceHeader(mAspectIndex);
    double dar = 0.0;
    if (seqHeader != 0) {
      switch (seqHeader->aspectRatio()) {
        case 2: dar = 4.0 / 3.0;  break;
        case 3: dar = 16.0 / 9.0; break;
        case 4: dar = 2.21;       break;
        default:                  break;
      }
    }
    if (dar > 0.0) {
      double displayWidth = videoHeight * dar;
      if (qAbs(displayWidth - videoWidth) > 0.5) {
        if (displayWidth > videoWidth)
          scaleFactorX = (float)(displayWidth / videoWidth);   // widen (720 -> 768 or 1024)
        else
          scaleFactorY = (float)(videoWidth / displayWidth);   // heighten
      }
    }
  }

  if (mUseFFmpeg && mpFFmpegWrapper != 0) {
    // Anamorphic H.264/H.265 (SAR != 1:1, e.g. SD DVB 720x576 SAR 16:11):
    // correct the display aspect in the upscale direction so no detail is
    // lost before the final widget scaling. mpv playback applies the same
    // correction — still frame and playback keep the same shape.
    double sar = mpFFmpegWrapper->sampleAspectRatio();
    if (sar > 0.0 && qAbs(sar - 1.0) > 0.005) {
      if (sar > 1.0)
        scaleFactorX = (float)sar;          // widen (e.g. 720 -> 1047)
      else
        scaleFactorY = (float)(1.0 / sar);  // heighten (SAR < 1, theoretical)
    }
  }
}

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

  float scaleFactorX = 1.0;
  float scaleFactorY = 1.0;
  computeDisplayScale(scaleFactorX, scaleFactorY);

  QImage scale = frameToShow.scaled(qRound(videoWidth*scaleFactorX), qRound(videoHeight*scaleFactorY), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

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
  mSubtitleDelayMs = 0;
}

/*!
 * Set subtitle delay for overlay lookup (mkvmerge convention:
 * positive = show the subtitles later)
 */
void TTMPEG2Window2::setSubtitleDelay(int delayMs)
{
  mSubtitleDelayMs = delayMs;
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

  // Delay shifts the subtitles relative to the video (positive = later), so
  // the lookup into the subtitle source runs d ms EARLIER than the video time.
  int currentTimeMs = (int)((currentIndex / frameRate) * 1000.0) - mSubtitleDelayMs;

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
 * Draw subtitle text on image. SRT markup (<font color>, <i>, <b>) is
 * rendered via QTextDocument; the black outline is produced by tinting
 * the rendered text image, so italic metrics cannot ghost against the
 * outline pass.
 */
void TTMPEG2Window2::drawSubtitleOnImage(QImage& image, const QString& text)
{
  int fontSize = qMax(12, image.height() / 20);

  QTextDocument doc;
  QFont font("Sans", fontSize, QFont::Bold);
  doc.setDefaultFont(font);
  QTextOption opt;
  opt.setAlignment(Qt::AlignHCenter);
  opt.setWrapMode(QTextOption::WordWrap);
  doc.setDefaultTextOption(opt);
  doc.setTextWidth(image.width());

  // SRT line breaks -> <br/>; tags (<font color>, <i>, <b>) pass through.
  //
  // Deliberately passing tags through means a crafted .srt could also carry
  // an <img> whose src points at a local file or data: URI - harmless in
  // effect (no scripts, no network, no exfiltration; security review
  // 2026-08-15 rated it below reporting threshold) but there is no reason to
  // load ANY resource from subtitle text. The empty resource provider turns
  // every such reference into a no-op while the formatting tags keep working.
  doc.setResourceProvider([](const QUrl&) { return QVariant(); });
  QString html = text;
  html.replace("\r\n", "<br/>");
  html.replace("\n", "<br/>");
  doc.setHtml(QString("<span style=\"color:white;\">%1</span>").arg(html));

  // Render the text into a transparent image
  QImage textImg(image.width(), qCeil(doc.size().height()),
                 QImage::Format_ARGB32_Premultiplied);
  textImg.fill(Qt::transparent);
  {
    QPainter tp(&textImg);
    doc.drawContents(&tp);
  }

  // Black copy for the outline: keep alpha, replace all color with black
  QImage outlineImg = textImg;
  {
    QPainter op(&outlineImg);
    op.setCompositionMode(QPainter::CompositionMode_SourceIn);
    op.fillRect(outlineImg.rect(), Qt::black);
  }

  QPainter painter(&image);
  int y = image.height() - textImg.height() - fontSize / 2;   // bottom margin
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx != 0 || dy != 0)
        painter.drawImage(dx, y + dy, outlineImg);
  painter.drawImage(0, y, textImg);
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

int TTMPEG2Window2::showKeyframeFastAt(int index)
{
  if (!mUseFFmpeg) {
    // MPEG-2: the libmpeg2 path decodes a GOP in a few tens of ms - the
    // ordinary route is fast enough and keeps one code path.
    moveToVideoFrame(index);
    return currentIndex;
  }

  if (mpFFmpegWrapper == 0) return -1;

  int shownPos = -1;
  QImage img = mpFFmpegWrapper->decodeNearestKeyframe(index, &shownPos);
  if (img.isNull()) return -1;

  mCurrentFrame = img;
  currentIndex  = shownPos;
  mAspectIndex  = shownPos;
  showVideoFrame();
  return shownPos;
}

/*!
 * Open a video file and assign the mpeg2 decoder object
 */
void TTMPEG2Window2::openVideoFile( const QString& fName, TTVideoIndexList* viIndex, TTVideoHeaderList* viHeader )
{
	if (fName.isEmpty()) return;

  if (mpeg2Decoder != 0) delete mpeg2Decoder;

  try
  {
  	mpeg2Decoder = new TTMpeg2Decoder(fName, viIndex, viHeader);
  }
  catch (const TTMpeg2DecoderException& ex)
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
  mAspectIndex = 0;

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

    // Index sharing (spec 2026-06-05): Owner A (the H.26x stream) already built
    // the frame index at stream-open. Instead of running an identical second scan
    // of the same file here (~2 s), we adopt Owner A's index (Qt COW, cheap).
    // Consumers that in turn pull from THIS wrapper (Black/Scene/Logo search,
    // analysisWrapper) thus transitively get Owner A's index too. Falls back to
    // building our own if vStream is not an H.26x stream or its index is not
    // available yet.
    bool indexAdopted = false;
    if (const TTH26xVideoStream* h26x = dynamic_cast<const TTH26xVideoStream*>(vStream)) {
      const TTFrameIndexBundle bundle = h26x->frameIndexBundle();
      if (!bundle.isEmpty()) { mpFFmpegWrapper->setFrameIndex(bundle); indexAdopted = true; }
    }
    if (!indexAdopted) {
      qDebug() << "Building frame index for preview...";
      TTFrameIndexer indexer;
      if (indexer.build(vStream->filePath(), -1, nullptr))
        mpFFmpegWrapper->setFrameIndex(indexer.bundle());
      else
        log->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(indexer.lastError()));
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
  mAspectIndex = 0;

  QImage dummy;
  this->setPixmap(QPixmap::fromImage(dummy));
  repaint();
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
      mAspectIndex = iFramePos;
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
		mAspectIndex = iFramePos;
		getFrameInfo();
	}
	catch (const TTMpeg2DecoderException& ex)
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
  QPixmap pm = pixmap();
  if (pm.isNull()) return QRect();

  QSize pmSize = pm.size();
  int x = (width() - pmSize.width()) / 2;
  int y = (height() - pmSize.height()) / 2;
  return QRect(x, y, pmSize.width(), pmSize.height());
}

// Pixmap rectangle inside the widget, false when nothing is displayed yet.
bool TTMPEG2Window2::pixmapGeometry(QRect& pmRect) const
{
  pmRect = currentPixmapRect();
  return !(pmRect.isEmpty() || videoWidth <= 0 || videoHeight <= 0);
}

QRect TTMPEG2Window2::imageToWidgetRect(const QRect& imageRect) const
{
  QRect pmRect;
  if (!pixmapGeometry(pmRect)) return QRect();

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
  QRect pmRect;
  if (!pixmapGeometry(pmRect)) return QRect();

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
    mRubberBandOrigin = event->position().toPoint();
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
    mRubberBand->setGeometry(QRect(mRubberBandOrigin, event->position().toPoint()).normalized());
    return;
  }
  QLabel::mouseMoveEvent(event);
}

void TTMPEG2Window2::mouseReleaseEvent(QMouseEvent* event)
{
  if (mLogoSelectionMode && mRubberBand && event->button() == Qt::LeftButton) {
    mRubberBand->hide();
    QRect widgetRect = QRect(mRubberBandOrigin, event->position().toPoint()).normalized();
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
