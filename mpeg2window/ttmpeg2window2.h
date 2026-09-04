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


#ifndef TTMPEG2WINDOW2_H
#define TTMPEG2WINDOW2_H

//#include <QDateTime>
#include <QImage>
#include <QFileDialog>
#include <QLabel>
#include <QPainter>
#include <QFont>
#include <QRubberBand>

#include "../common/ttcut.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"
#include "../common/ttmessagelogger.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../avstream/ttsubtitleheaderlist.h"
#include "../avstream/ttavheader.h"
#include "../avstream/ttavtypes.h"
#include "../extern/ttffmpegwrapper.h"

class TTSubtitleStream;

class TTMPEG2Window2 : public QLabel
{
  Q_OBJECT

  public:
    explicit TTMPEG2Window2( QWidget* parent=0 );

    void resizeEvent(QResizeEvent * event);

    void openVideoFile(const QString& fName, TTVideoIndexList* viIndex=0, TTVideoHeaderList* viHeader=0);
    void openVideoStream(TTVideoStream* vStream);
    void closeVideoStream();

    // Check if using FFmpeg decoder (H.264/H.265)
    bool isFFmpegStream() const { return mUseFFmpeg; }
    TTFFmpegWrapper* ffmpegWrapper() const { return mpFFmpegWrapper; }

    // navigation
    void moveToVideoFrame(int iFramePos);

    void showVideoFrame();
    void showFrameAt(int index);
    // Slider-drag preview: show the keyframe at/before index (H.26x: decoded
    // without DPB prefill - see TTFFmpegWrapper::decodeNearestKeyframe). For
    // MPEG-2 this is a plain showFrameAt (its decoder is cheap per frame).
    // Returns the display position actually shown, or -1.
    int  showKeyframeFastAt(int index);
    void showDecodedSlice();

    void saveCurrentFrame(QString fName, const char* format);

    void invalidateDisplay();

    void setSubtitleStream(TTSubtitleStream* subtitleStream);
    void clearSubtitleStream();
    void setSubtitleDelay(int delayMs);

    void setLogoSelectionMode(bool enable);
    void setLogoROIOverlay(const QRect& imageCoords);
    void clearLogoROIOverlay();
    QImage grabFrameImage() const;

  signals:
    void logoROISelected(QRect imageCoords);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  	void getFrameInfo();
    QString getSubtitleTextAtCurrentFrame();
    static void drawSubtitleOnImage(QImage& image, const QString& text);
    void computeDisplayScale(float& scaleFactorX, float& scaleFactorY) const;
    bool pixmapGeometry(QRect& pmRect) const;

  private:
    // Logo ROI selection
    bool            mLogoSelectionMode;
    QRubberBand*    mRubberBand;
    QPoint          mRubberBandOrigin;
    QRect           mLogoROIOverlay;  // in image coordinates, empty = no overlay

    // Coordinate transform helpers
    QRect imageToWidgetRect(const QRect& imageRect) const;
    QRect widgetToImageRect(const QRect& widgetRect) const;
    QRect currentPixmapRect() const;

    TFrameInfo*         frameInfo;
    quint8*             picBuffer;
    int                 videoWidth;
    int                 videoHeight;
    int                 currentIndex;
    //! Index of the frame the display actually shows, for the sequence-header
    //! (aspect) lookup. Unlike currentIndex it survives invalidateDisplay():
    //! that sets currentIndex to -1 as a re-decode marker, and an aspect
    //! lookup with -1 falls back to the FIRST sequence header of the file —
    //! wrong shape when the recording starts in a different aspect than the
    //! frame on screen (e.g. 4:3 ad block before a 16:9 show).
    int                 mAspectIndex;
    TTVideoStream*      mpVideoStream;
    TTSubtitleStream*   mpSubtitleStream;
    int                 mSubtitleDelayMs;  // mkvmerge convention: positive = show later
    TTMessageLogger*    log;
    TTMpeg2Decoder*     mpeg2Decoder;

    // FFmpeg decoder for H.264/H.265
    TTFFmpegWrapper*    mpFFmpegWrapper;
    bool                mUseFFmpeg;
    QImage              mCurrentFrame;   // For FFmpeg decoded frames
};

#endif //TTMPEG2WINDOW_H
