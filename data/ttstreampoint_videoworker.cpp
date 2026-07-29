/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_videoworker.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttsettings.h"

#include <QDebug>

TTStreamPointVideoWorker::TTStreamPointVideoWorker(
    bool detectAspectChange, int streamType, TTVideoHeaderList* videoHeaderList)
  : TTThreadTask("StreamPointVideoAnalysis"),
    mStreamType(streamType),
    mDetectAspectChange(detectAspectChange),
    mVideoHeaderList(videoHeaderList)
{
}

void TTStreamPointVideoWorker::operation()
{
  QList<TTStreamPoint> allPoints;

  onStatusReport(StatusReportArgs::Start, tr("Analyzing video..."), 1);

  if (mDetectAspectChange && !mIsAborted) {
    onStatusReport(StatusReportArgs::Step, tr("Aspect ratio analysis..."), 0);
    QList<TTStreamPoint> aspectPoints = detectAspectChanges();
    allPoints.append(aspectPoints);
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "StreamPointVideo: Found" << aspectPoints.size() << "aspect ratio changes";
  }

  if (!mIsAborted)
    emit pointsDetected(allPoints);

  mStepCount = 1;
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
  if (mStreamType != TTAVTypes::mpeg2_demuxed_video)
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

        if (TTSettings::instance()->logCutPipeline())
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

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "detectAspectChanges:" << pictureCount << "pictures scanned,"
               << results.size() << "changes found";

  return results;
}

