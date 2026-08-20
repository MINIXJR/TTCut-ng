/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAUDIOANOMALYSCANTASK_H
#define TTAUDIOANOMALYSCANTASK_H

#include "../common/ttthreadtask.h"
#include "ttstreampoint.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

//! Background scan of one AC3 5.1 track for CRC-valid but structurally
//! defective bursts: center + LFE transients in material whose LFE is
//! otherwise digital silence (docs/superpowers/specs/2026-08-19-audio-
//! anomaly-repair-design.md, Komponente 1). One sequential decode pass
//! collects per-32ms-AC3-frame stats; evaluate() (pure, static) then finds
//! and fine-segments the actual burst blocks, separated from decoding so
//! the harness can exercise it without an AC3 file.
class TTAudioAnomalyScanTask : public TTThreadTask
{
  Q_OBJECT

public:
  // extraFrameIndices: sorted display-index list from TTAVData (same list
  // buildVideoKeepList corrects with); needed to map audio time back to
  // video frame indices for the marker positions.
  // gapFrameRanges: known audio-gap video-frame ranges (TTAVData's clustered
  // mAudioGapIndices, may be empty) — a finding whose marker range overlaps
  // one gets its description annotated, so the user does not read the same
  // defect as two unrelated problems.
  TTAudioAnomalyScanTask(const QString& audioFilePath, int trackIndex,
                         double frameRate, const QList<int>& extraFrameIndices,
                         const QList<QPair<int,int>>& gapFrameRanges);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

public:
  // Pure evaluation over per-frame stats — separated from decoding so the
  // harness can test it without an AC3 file. One entry per 32 ms AC3 frame.
  struct FrameStat { float lfeRms; float centerRms; float centerMaxDiff; bool is51; };
  // frameFrom/frameTo: AC3 frame index range, both inclusive (matches
  // TTAudioRepairItem::frameFrom()/frameTo() and TTStreamPoint's
  // audioFrameFrom()/audioFrameTo() - unlike TTStreamPoint::duration(),
  // which is end-exclusive).
  struct Finding { qint64 frameFrom; qint64 frameTo; float lfePeak; float confidence; };
  // Result of the material-suitability hard gate (Vorbedingung), always
  // written when gateOut is non-null - regardless of whether the gate
  // passed. Without this, an empty findings list is ambiguous: "material
  // unsuitable, no statement possible" and "material fine, nothing found"
  // both look the same to the caller. operation() uses it to log the
  // unsuitable case (the spec explicitly calls for a log line there), the
  // harness uses it to prove the two cases are actually distinguishable.
  struct GateStatus {
    bool   materialUnsuitable = false;
    double lfeNullPercent     = 0.0;   // measured, 0..100
    int    frames51           = 0;     // 5.1 frames evaluated (denominator)
  };
  static QList<Finding> evaluate(const QVector<FrameStat>& stats,
                                 double lfeRmsThresholdDb,
                                 double centerContrastFactor,
                                 double lfeNullPercent,
                                 double lfeMinPeakDb,
                                 GateStatus* gateOut = nullptr);
  // Audio time (s) -> video display frame index, inverse of
  // (index - extrasBefore)/fps; iterates until stable (<= 2 rounds).
  static int videoFrameForTime(double seconds, double fps,
                               const QList<int>& extraFrameIndices);

  // Decode one AC3 track into per-frame stats (libav, single pass, no
  // reordering). Exposed as its own static entry point — independent of
  // the QRunnable/thread-task machinery — so the diag harness can decode a
  // real or synthetic file and feed the exact frames straight into
  // evaluate() for precise AC3-frame-range assertions, instead of having
  // to reverse the video-frame mapping out of a TTStreamPoint.
  // decodeFailures (optional out param): count of AC3 frames that could not
  // be submitted to the decoder at all (distinct from frames that decoded
  // fine but are not 5.1 - those are expected and simply excluded by
  // evaluate(), not a failure).
  // shouldAbort/onFrame: optional callbacks; operation() passes its own
  // abort flag and a progress reporter, the harness passes neither.
  static QVector<FrameStat> collectFrameStats(
      const QString& audioFilePath,
      int* decodeFailures = nullptr,
      const std::function<bool()>& shouldAbort = std::function<bool()>(),
      const std::function<void(qint64)>& onFrame = std::function<void(qint64)>());

private:
  QString               mAudioFilePath;
  int                    mTrackIndex;
  double                 mFrameRate;
  QList<int>             mExtraFrameIndices;
  QList<QPair<int,int>>  mGapFrameRanges;
};

#endif // TTAUDIOANOMALYSCANTASK_H
