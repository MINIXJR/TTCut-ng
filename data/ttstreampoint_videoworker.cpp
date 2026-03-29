/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_videoworker.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttcut.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"
#include "../extern/ttffmpegwrapper.h"

#include <QDebug>
#include <QImage>

TTStreamPointVideoWorker::TTStreamPointVideoWorker(
    const QString& videoFilePath, int streamType, float frameRate,
    bool detectAspectChange, bool detectPillarbox, int pillarboxThreshold,
    TTVideoHeaderList* videoHeaderList, TTVideoIndexList* videoIndexList)
  : TTThreadTask("StreamPointVideoAnalysis"),
    mVideoFilePath(videoFilePath),
    mStreamType(streamType),
    mFrameRate(frameRate),
    mDetectAspectChange(detectAspectChange),
    mDetectPillarbox(detectPillarbox),
    mPillarboxThreshold(pillarboxThreshold),
    mVideoHeaderList(videoHeaderList),
    mVideoIndexList(videoIndexList)
{
}

void TTStreamPointVideoWorker::operation()
{
  QList<TTStreamPoint> allPoints;

  int totalSteps = (mDetectAspectChange ? 1 : 0) + (mDetectPillarbox ? 1 : 0);
  if (totalSteps == 0) totalSteps = 1;

  onStatusReport(StatusReportArgs::Start, tr("Analyzing video..."), totalSteps);

  int step = 0;

  if (mDetectAspectChange && !mIsAborted) {
    onStatusReport(StatusReportArgs::Step, tr("Seitenverhältnis-Analyse..."), step);
    QList<TTStreamPoint> aspectPoints = detectAspectChanges();
    allPoints.append(aspectPoints);
    qDebug() << "StreamPointVideo: Found" << aspectPoints.size() << "aspect ratio changes";
    step++;
  }

  if (mDetectPillarbox && !mIsAborted) {
    onStatusReport(StatusReportArgs::Step, tr("Pillarbox-Analyse..."), step);
    QList<TTStreamPoint> pillarboxPoints = detectPillarboxChanges();
    allPoints.append(pillarboxPoints);
    qDebug() << "StreamPointVideo: Found" << pillarboxPoints.size() << "pillarbox changes";
    step++;
  }

  if (!mIsAborted) {
    emit pointsDetected(allPoints);
  }

  mStepCount = step;
  onStatusReport(StatusReportArgs::Finished, tr("Video analysis complete"), mStepCount);
}

void TTStreamPointVideoWorker::cleanUp()
{
}

void TTStreamPointVideoWorker::onUserAbort()
{
  mIsAborted = true;
}

// ---------------------------------------------------------------------------
// Aspect ratio change detection via MPEG-2 sequence headers
// ---------------------------------------------------------------------------
QList<TTStreamPoint> TTStreamPointVideoWorker::detectAspectChanges()
{
  QList<TTStreamPoint> results;

  if (!mVideoHeaderList || mVideoHeaderList->size() == 0)
    return results;

  // MPEG-2 only — sequence headers contain aspect_ratio_information
  if (mStreamType != TTAVTypes::mpeg2_demuxed_video &&
      mStreamType != TTAVTypes::mpeg2_mplexed_video)
    return results;

  int prevAspect = -1;
  int pictureCount = 0;

  // Iterate all headers (sequence, GOP, picture are interleaved)
  for (int i = 0; i < mVideoHeaderList->size() && !mIsAborted; ++i) {
    TTVideoHeader* hdr = mVideoHeaderList->headerAt(i);
    if (!hdr) continue;

    if (hdr->headerType() == TTMpeg2VideoHeader::picture_start_code) {
      pictureCount++;
    }

    if (hdr->headerType() == TTMpeg2VideoHeader::sequence_start_code) {
      TTSequenceHeader* seqHdr = static_cast<TTSequenceHeader*>(hdr);
      int aspect = seqHdr->aspectRatio();

      if (prevAspect >= 0 && aspect != prevAspect) {
        QString prevStr = (prevAspect == 2) ? "4:3" :
                          (prevAspect == 3) ? "16:9" :
                          QString::number(prevAspect);
        QString newStr  = (aspect == 2) ? "4:3" :
                          (aspect == 3) ? "16:9" :
                          QString::number(aspect);

        qDebug() << "detectAspectChanges:" << prevStr << "->" << newStr
                 << "at picture" << pictureCount;

        TTStreamPoint pt(pictureCount, StreamPointType::AspectChange,
          QString("%1 \u2192 %2").arg(prevStr, newStr),
          0.0f, 0.0f);
        results.append(pt);
      }
      prevAspect = aspect;
    }
  }

  qDebug() << "detectAspectChanges:" << pictureCount << "pictures scanned,"
           << results.size() << "changes found";

  return results;
}

// ---------------------------------------------------------------------------
// Static helper: check if a column in the scan band is black
// ---------------------------------------------------------------------------
static bool isColumnBlack(const uint8_t* yPlane, int yStride, int col,
                          int y0, int y1, int threshold)
{
  int total = 0;
  int black = 0;

  // Sample every 2nd row for speed
  for (int row = y0; row < y1; row += 2) {
    total++;
    if (yPlane[row * yStride + col] < threshold)
      black++;
  }

  if (total == 0) return false;

  // Column is black if >= 90% of sampled pixels are below threshold
  return (float)black / total >= 0.90f;
}

// ---------------------------------------------------------------------------
// Analyze a single frame for pillarbox (black bars on left and right)
// ---------------------------------------------------------------------------
bool TTStreamPointVideoWorker::isPillarboxFrame(
    const uint8_t* yPlane, int yStride, int width, int height,
    int threshold, float& barWidthPercent)
{
  barWidthPercent = 0.0f;

  if (!yPlane || width < 20 || height < 20)
    return false;

  // Scan band: middle 40% of height
  int y0 = (int)(height * 0.30f);
  int y1 = (int)(height * 0.70f);

  // Minimum bar width: 10% of frame width
  int minBarWidth = width / 10;

  // Left bar: scan columns from 0 inward
  int leftBar = 0;
  for (int col = 0; col < width / 2; ++col) {
    if (isColumnBlack(yPlane, yStride, col, y0, y1, threshold))
      leftBar++;
    else
      break;
  }

  // Right bar: scan columns from width-1 inward
  int rightBar = 0;
  for (int col = width - 1; col >= width / 2; --col) {
    if (isColumnBlack(yPlane, yStride, col, y0, y1, threshold))
      rightBar++;
    else
      break;
  }

  // Pillarbox = both bars >= minimum bar width
  if (leftBar >= minBarWidth && rightBar >= minBarWidth) {
    barWidthPercent = (float)(leftBar + rightBar) / width * 100.0f;
    return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Pillarbox change detection via I-frame pixel analysis
// ---------------------------------------------------------------------------
QList<TTStreamPoint> TTStreamPointVideoWorker::detectPillarboxChanges()
{
  QList<TTStreamPoint> results;

  if (!mVideoIndexList || mVideoIndexList->count() == 0)
    return results;

  bool useMpeg2 = (mStreamType == TTAVTypes::mpeg2_demuxed_video ||
                   mStreamType == TTAVTypes::mpeg2_mplexed_video);
  bool useFFmpeg = (mStreamType == TTAVTypes::h264_video ||
                    mStreamType == TTAVTypes::h265_video);

  if (!useMpeg2 && !useFFmpeg)
    return results;

  // Create own decoder instance (thread-safe, not shared with UI)
  TTMpeg2Decoder*  mpeg2 = nullptr;
  TTFFmpegWrapper* ffmpeg = nullptr;

  if (useMpeg2) {
    try {
      mpeg2 = new TTMpeg2Decoder(mVideoFilePath, mVideoIndexList,
                                  mVideoHeaderList, formatYV12);
    } catch (TTMpeg2DecoderException&) {
      qDebug() << "detectPillarboxChanges: Failed to create MPEG-2 decoder";
      return results;
    }
  } else {
    ffmpeg = new TTFFmpegWrapper();
    if (!ffmpeg->openFile(mVideoFilePath)) {
      qDebug() << "detectPillarboxChanges: Failed to open file with FFmpeg";
      delete ffmpeg;
      return results;
    }
  }

  bool confirmedPillarbox = false;  // last confirmed state
  bool candidateState = false;      // current candidate state
  int candidateCount = 0;           // consecutive I-frames with candidate state
  int candidateFirstFrame = 0;      // frame index where candidate state started
  int iframeCount = 0;

  // Hysteresis: require 10 seconds of consistent state before confirming transition
  const float hysteresisSeconds = 10.0f;
  int hysteresisFrames = qMax(1, qRound(hysteresisSeconds * mFrameRate));

  // Start at first I-frame
  int pos = mVideoIndexList->moveToIndexPos(0, 1);

  while (pos >= 0 && !mIsAborted) {
    bool isPillarbox = false;
    float barPercent = 0.0f;

    if (useMpeg2) {
      try {
        mpeg2->moveToFrameIndex(pos);
        TFrameInfo* fi = mpeg2->decodeMPEG2Frame(formatYV12);
        if (fi && fi->Y) {
          isPillarbox = isPillarboxFrame(fi->Y, fi->width, fi->width, fi->height,
                                         mPillarboxThreshold, barPercent);
        }
      } catch (TTMpeg2DecoderException&) {
        // Skip frame on decode error
      }
    } else {
      QImage frame = ffmpeg->decodeFrame(pos);
      if (!frame.isNull()) {
        QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
        isPillarbox = isPillarboxFrame(gray.constBits(), gray.bytesPerLine(),
                                       gray.width(), gray.height(),
                                       mPillarboxThreshold, barPercent);
      }
    }

    // Hysteresis: accumulate consecutive frames with same state
    if (isPillarbox == candidateState) {
      candidateCount++;
    } else {
      candidateState = isPillarbox;
      candidateCount = 1;
      candidateFirstFrame = pos;
    }

    // Confirm transition after enough consecutive frames (10 seconds worth)
    if (candidateState != confirmedPillarbox &&
        (pos - candidateFirstFrame) >= hysteresisFrames) {
      confirmedPillarbox = candidateState;

      // Don't emit for the initial state detection
      if (iframeCount > candidateCount) {
        QString desc;
        if (confirmedPillarbox)
          desc = QString("16:9 \u2192 4:3pb");
        else
          desc = QString("4:3pb \u2192 16:9");

        TTStreamPoint pt(candidateFirstFrame, StreamPointType::PillarboxChange,
                         desc, 0.0f, 0.0f);
        results.append(pt);

        qDebug() << "detectPillarboxChanges:" << desc << "at frame" << candidateFirstFrame;
      }
    }

    iframeCount++;

    // Progress every 100 I-frames
    if (iframeCount % 100 == 0) {
      onStatusReport(StatusReportArgs::AddProcessLine,
        tr("Pillarbox: %1 I-frames analyzed, %2 changes")
          .arg(iframeCount).arg(results.size()), 0);
    }

    // Move to next I-frame
    int nextPos = mVideoIndexList->moveToNextIndexPos(pos, 1);
    if (nextPos <= pos)
      break;  // No more I-frames
    pos = nextPos;
  }

  qDebug() << "detectPillarboxChanges:" << iframeCount << "I-frames analyzed,"
           << results.size() << "changes found";

  // Cleanup
  delete mpeg2;
  delete ffmpeg;

  return results;
}
