/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_videoworker.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttcut.h"

#include <QDebug>

TTStreamPointVideoWorker::TTStreamPointVideoWorker(
    const QString& videoFilePath, int streamType, float frameRate,
    bool detectAspectChange, TTVideoHeaderList* videoHeaderList)
  : TTThreadTask("StreamPointVideoAnalysis"),
    mVideoFilePath(videoFilePath),
    mStreamType(streamType),
    mFrameRate(frameRate),
    mDetectAspectChange(detectAspectChange),
    mVideoHeaderList(videoHeaderList)
{
}

void TTStreamPointVideoWorker::operation()
{
  QList<TTStreamPoint> allPoints;

  onStatusReport(StatusReportArgs::Start, tr("Analyzing video..."), 1);

  if (mDetectAspectChange && !mIsAborted) {
    QList<TTStreamPoint> aspectPoints = detectAspectChanges();
    allPoints.append(aspectPoints);
    qDebug() << "StreamPointVideo: Found" << aspectPoints.size() << "aspect ratio changes";
    mStepCount = 1;
    onStatusReport(StatusReportArgs::Step, QString(), mStepCount);
  }

  if (!mIsAborted) {
    emit pointsDetected(allPoints);
  }

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
