/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTPROGRESSESTIMATOR_H
#define TTPROGRESSESTIMATOR_H

#include <QString>
#include <QVector>

#include <functional>

class ITTCalibrationStore;

//! One planned stage of an operation.
struct TTStagePlan
{
  int     stage;      // StatusReportArgs::ProgressStage
  QString calibKey;   // calibration key; empty = no persistence (video, pool)
  double  workUnits;  // stage work amount (media seconds, frames, ...); > 0
};

//! GUI-free progress arithmetic: weighted total percent + remaining time.
//! Time source is injected so tests can run on a simulated clock.
class TTProgressEstimator
{
  public:
    enum RemainingKind { RemainingUnknown, RemainingStageOnly, RemainingTotal };

    struct Result
    {
      int           totalPercent = 0;                // 0-100, monotone over the operation
      RemainingKind kind         = RemainingUnknown;
      int           stage        = -1;                // current stage (valid for StageOnly)
      qint64        remainingMs  = 0;                  // valid for StageOnly/Total
    };

    TTProgressEstimator(ITTCalibrationStore* store, std::function<qint64()> clockMs);

    void   setPlan(const QVector<TTStagePlan>& plan);
    void   beginStage(int stage);
    Result update(int stagePercent);
    void   finishOperation(bool regular);

    bool   active() const  { return mCurrentIdx >= 0; }
    bool   planned() const { return !mStages.isEmpty(); }
    qint64 operationDurationMs() const { return mOpDurationMs; }

    //! Sum of estimatedMs over every planned stage, IF all of them are
    //! calibrated (estimatedMs > 0); -1 otherwise (uncalibrated stage, or
    //! no plan / ad-hoc operation). Used by update() to give short
    //! operations a whole-plan ETA before the per-stage rate gates open -
    //! see the RemainingTotal short-circuit there.
    qint64 plannedEstimateMs() const;

  private:
    struct StageState
    {
      TTStagePlan plan;
      double      estimatedMs;  // store factor * workUnits; < 0 = unknown
      double      measuredMs;   // actual duration after stage end; < 0 = open
    };

    void   resetToAdHoc(int stage);
    void   closeCurrentStage(bool wroteCalibrationAllowed);
    double currentProjectionMs(qint64 now) const;  // 100% duration; < 0 = none

    //! One (elapsed-ms, percent) sample of the current stage, for the
    //! recent-rate window used by the remaining-time output (see mRateSamples).
    struct RateSample
    {
      qint64 ms;
      int    percent;
    };
    // Windowed rate = (last.percent - first.percent) / (last.ms - first.ms)
    // over the trailing ~15s of samples of the CURRENT stage. Feeds ONLY the
    // remaining-time output (remStage in update()) - the whole-stage average
    // (currentProjectionMs) keeps driving the stage WEIGHT used for
    // totalPercent and the in-flight correction blend, so the progress bar
    // stays stable while the ETA becomes responsive to a recent slowdown/
    // speedup instead of being dragged down by an initial burst for minutes.
    double windowedRemainingMs() const;

    ITTCalibrationStore*    mStore;
    std::function<qint64()> mClock;
    QVector<StageState>     mStages;
    QVector<RateSample>     mRateSamples;
    int     mCurrentIdx     = -1;
    int     mStagePercent   = 0;
    qint64  mOpStartMs      = -1;
    qint64  mStageStartMs   = 0;
    qint64  mOpDurationMs   = 0;
    double  mCorrection     = 1.0;  // EMA of actual/estimated for done work
    int     mLastTotalPercent = 0;
    // True while the current operation is ad-hoc (resetToAdHoc: pool ops,
    // and any unplanned/foreign-stage fallback) rather than setPlan-based.
    // Only planned operations get the monotone clamp on totalPercent below
    // - stage weights there make "never decreases" the correct contract.
    // Ad-hoc single-stage operations register their total INCREMENTALLY
    // (tiny tasks finish first, so overallPercentage() legitimately reads
    // high early and then drops once a big task registers its own total);
    // clamping that would freeze the bar at a false peak (UAT 2026-08-09:
    // bar stuck near 99% for seconds during file open). Passthrough mode
    // (mCurrentIdx < 0) already bypasses the clamp entirely.
    bool    mAdHoc          = true;
};

#endif // TTPROGRESSESTIMATOR_H
