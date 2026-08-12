/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampoint_videoworker.h"
#include "ttanalysislog.h"
#include "../avstream/ttvideoheaderlist.h"
#include "../avstream/ttvideoindexlist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <QDebug>

TTStreamPointVideoWorker::TTStreamPointVideoWorker(
    int streamType, TTVideoHeaderList* videoHeaderList,
    TTVideoIndexList* videoIndexList, float videoFrameRate)
  : TTThreadTask("StreamPointVideoAnalysis"),
    mStreamType(streamType),
    mVideoHeaderList(videoHeaderList),
    mVideoIndexList(videoIndexList),
    mVideoFrameRate(videoFrameRate),
    mLog([this](const QString& s) {
           onStatusReport(StatusReportArgs::AddProcessLine, s, 0);
         }, 20)
{
}

void TTStreamPointVideoWorker::operation()
{
  QList<TTStreamPoint> allPoints;

  onStatusReport(StatusReportArgs::Start, tr("Analyzing video..."), 1);

  if (!mIsAborted) {
    onStatusReport(StatusReportArgs::Step, tr("Aspect ratio analysis..."), 0);
    mLog.line(tr("Aspect ratio (sequence headers): scanning"));
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
// Position, in the display-sorted index list, of the first picture that
// follows the header at headerIndex.
//
// Everything downstream of a marker treats its number as a navigation
// position: a double click goes through TTStreamPointWidget::jumpToFrame and
// TTCutMainWindow::onStreamPointJump into onVideoSliderChanged, and for MPEG-2
// the navigation index is a DISPLAY position - TTOpenVideoTask sorts the index
// list with sortDisplayOrder() right after building it.
//
// Counting picture headers, on the other hand, counts in bitstream (decode)
// order. The two only agree while every GOP starts on its first displayed
// picture. Measured over three recordings (tools/diag/test_streampoint_order):
// TELE5 576p25 has temporal_reference 2 on all 626 sequence headers, Comedy
// Central 3 on 262 of 350, so markers landed 2 to 6 frames off; RTLZWEI runs
// closed GOPs and was correct by accident.
//
// Computing base_number + temporal_reference instead would be wrong as well:
// the navigation works with the RANK in the sorted list, and field-picture
// pairs make rank and display_order value diverge (65 of 7507 entries on
// Comedy Central, 14 of 136319 on RTLZWEI). So the position is looked up, not
// derived.
//
// The linear scan is fine here because it only runs for an actual aspect
// change - a handful per recording. A hash over all entries would cost
// megabytes for those few lookups.
// ---------------------------------------------------------------------------
int TTStreamPointVideoWorker::displayPositionAfter(int headerIndex) const
{
  if (!mVideoIndexList) return -1;

  int pictureHeader = -1;
  for (int i = headerIndex + 1; i < mVideoHeaderList->size(); ++i) {
    TTVideoHeader* hdr = mVideoHeaderList->headerAt(i);
    if (hdr && hdr->headerType() == TTMpeg2VideoHeader::picture_start_code) {
      pictureHeader = i;
      break;
    }
  }
  if (pictureHeader < 0) return -1;

  for (int p = 0; p < mVideoIndexList->count(); ++p) {
    if (mVideoIndexList->headerListIndex(p) == pictureHeader)
      return p;
  }
  return -1;
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
  if (mStreamType != TTAVTypes::mpeg2_demuxed_video) {
    mLog.line(tr("Aspect ratio: not an MPEG-2 stream - skipped"));
    return results;
  }

  int prevAspect = -1;
  int pictureCount = 0;
  int sequenceHeaders = 0;

  // Iterate all headers (sequence, GOP, picture are interleaved)
  for (int i = 0; i < mVideoHeaderList->size() && !mIsAborted; ++i) {
    TTVideoHeader* hdr = mVideoHeaderList->headerAt(i);
    if (!hdr) continue;

    if (hdr->headerType() == TTMpeg2VideoHeader::picture_start_code) {
      pictureCount++;
    }

    if (hdr->headerType() == TTMpeg2VideoHeader::sequence_start_code) {
      TTSequenceHeader* seqHdr = static_cast<TTSequenceHeader*>(hdr);
      sequenceHeaders++;
      int aspect = seqHdr->aspectRatio();

      if (prevAspect >= 0 && aspect != prevAspect) {
        QString prevStr = (prevAspect == 2) ? "4:3" :
                          (prevAspect == 3) ? "16:9" :
                          QString::number(prevAspect);
        QString newStr  = (aspect == 2) ? "4:3" :
                          (aspect == 3) ? "16:9" :
                          QString::number(aspect);

        // The bitstream counter is only the fallback for the (impossible)
        // case that the picture following the header is missing from the
        // index list. A marker at roughly the right place beats none.
        int position = displayPositionAfter(i);
        if (position < 0) {
          if (mVideoIndexList) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
              QString("no index entry for the picture after sequence header %1, "
                      "marker keeps its bitstream position %2")
                .arg(i).arg(pictureCount));
          }
          mLog.event(tr("no index entry for the picture after sequence header %1"
                        " - marker keeps its bitstream position %2")
                         .arg(i).arg(pictureCount));
          position = pictureCount;
        }

        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "detectAspectChanges:" << prevStr << "->" << newStr
                     << "at display position" << position
                     << "(bitstream position" << pictureCount << ")";

        TTStreamPoint pt(position, StreamPointType::AspectChange,
          QString("%1 \u2192 %2").arg(prevStr, newStr),
          0.0f, 0.0f);
        results.append(pt);
        mLog.event(tr("%1: aspect %2 -> %3")
                       .arg(ttFormatStreamPosition(position, mVideoFrameRate))
                       .arg(prevStr, newStr));
      }
      prevAspect = aspect;
    }
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "detectAspectChanges:" << pictureCount << "pictures scanned,"
               << results.size() << "changes found";

  QString summary = mIsAborted
      ? tr("Aspect ratio cancelled after %1 sequence headers, %2 pictures")
            .arg(sequenceHeaders).arg(pictureCount)
      : tr("Aspect ratio: %1 sequence headers, %2 pictures")
            .arg(sequenceHeaders).arg(pictureCount);
  if (results.isEmpty() && prevAspect >= 0) {
    const QString aspectStr = (prevAspect == 2) ? QString("4:3")
                            : (prevAspect == 3) ? QString("16:9")
                                                : QString::number(prevAspect);
    summary += tr(" - aspect constant %1, no changes").arg(aspectStr);
  } else if (results.isEmpty()) {
    summary += tr(" - no sequence header with aspect information");
  } else {
    summary += tr(" - %n change(s)", "", results.size());
  }
  if (mLog.suppressed() > 0)
    summary += tr(" (%1 more events suppressed)").arg(mLog.suppressed());
  mLog.line(summary);

  return results;
}

