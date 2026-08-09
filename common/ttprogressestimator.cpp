/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttprogressestimator.h"

#include "ttcalibrationstore.h"

#include <QtGlobal>
#include <cmath>

namespace {
  // ETA is withheld until the stage rate is minimally trustworthy.
  const int    kMinPercentForRate = 2;
  const qint64 kMinElapsedForRate = 3000;   // ms
  const double kCorrectionEmaAlpha = 0.2;
  const double kMaxFactorJump = 100.0;      // plausibility vs previous factor
  // Recent-rate window for the remaining-time output (see windowedRemainingMs).
  const qint64 kRateWindowMs        = 15000;  // trailing window kept
  const qint64 kMinWindowSpanMs     = 2000;   // minimum span to trust the rate
  const int    kMinWindowSamples    = 2;
  // Short operations (whole plan under this ceiling) never accumulate
  // enough elapsed time/percent to open the per-stage rate gates above
  // before they finish - see the RemainingTotal short-circuit in update().
  const qint64 kShortOpCeilingMs    = 20000;
  const qint64 kShortOpMinRemainMs  = 1000;
}

TTProgressEstimator::TTProgressEstimator(ITTCalibrationStore* store,
                                         std::function<qint64()> clockMs)
  : mStore(store), mClock(std::move(clockMs))
{
}

void TTProgressEstimator::setPlan(const QVector<TTStagePlan>& plan)
{
  mStages.clear();
  for (const TTStagePlan& p : plan) {
    StageState s;
    s.plan = p;
    double f = (mStore && !p.calibKey.isEmpty()) ? mStore->factor(p.calibKey) : -1.0;
    s.estimatedMs = (f > 0 && p.workUnits > 0) ? f * p.workUnits : -1.0;
    s.measuredMs  = -1.0;
    mStages.append(s);
  }
  mCurrentIdx = -1;
  mStagePercent = 0;
  mOpStartMs = mClock();
  mOpDurationMs = 0;
  mCorrection = 1.0;
  mLastTotalPercent = 0;
  mAdHoc = false;
  mRateSamples.clear();
}

void TTProgressEstimator::resetToAdHoc(int stage)
{
  mStages.clear();
  StageState s;
  s.plan = { stage, QString(), 0.0 };
  s.estimatedMs = -1.0;
  s.measuredMs  = -1.0;
  mStages.append(s);
  mCurrentIdx = -1;
  mStagePercent = 0;
  mOpStartMs = mClock();
  mCorrection = 1.0;
  mLastTotalPercent = 0;
  mAdHoc = true;
  mRateSamples.clear();
}

void TTProgressEstimator::beginStage(int stage)
{
  if (mCurrentIdx >= 0 && mStages[mCurrentIdx].plan.stage == stage)
    return;   // repeated announce (processEvents nesting) is a no-op

  int idx = -1;
  for (int i = mCurrentIdx + 1; i < mStages.size(); ++i) {
    if (mStages[i].plan.stage == stage) { idx = i; break; }
  }

  if (idx < 0) {
    // Unknown/backward stage: different operation kind - never mix, reset
    // to an ad-hoc single-stage operation (spec: "kein Mischen"). This is
    // also the normal entry for pool operations, which have no plan.
    resetToAdHoc(stage);
    idx = 0;
  } else {
    closeCurrentStage(true);
  }

  if (mOpStartMs < 0) mOpStartMs = mClock();
  mCurrentIdx = idx;
  mStagePercent = 0;
  mStageStartMs = mClock();
  mRateSamples.clear();  // recent-rate window is per-stage
}

void TTProgressEstimator::closeCurrentStage(bool wroteCalibrationAllowed)
{
  if (mCurrentIdx < 0 || mCurrentIdx >= mStages.size()) return;
  StageState& s = mStages[mCurrentIdx];
  const qint64 now = mClock();
  s.measuredMs = double(now - mStageStartMs);

  // Calibration: only regular, essentially complete stages, with a key.
  if (wroteCalibrationAllowed && mStore && !s.plan.calibKey.isEmpty()
      && s.plan.workUnits > 0 && mStagePercent >= 99 && s.measuredMs > 0) {
    double f = s.measuredMs / s.plan.workUnits;
    double old = mStore->factor(s.plan.calibKey);
    bool plausible = std::isfinite(f) && f > 0
        && (old <= 0 || (f <= old * kMaxFactorJump && f >= old / kMaxFactorJump));
    if (plausible)
      mStore->setFactor(s.plan.calibKey, f);
  }

  // Correction EMA: actual vs estimate of the finished stage.
  if (s.estimatedMs > 0 && s.measuredMs > 0) {
    double ratio = s.measuredMs / s.estimatedMs;
    mCorrection = (1.0 - kCorrectionEmaAlpha) * mCorrection
                + kCorrectionEmaAlpha * ratio;
  }
}

qint64 TTProgressEstimator::plannedEstimateMs() const
{
  // Raw sum, not multiplied by mCorrection: at setPlan() time there is no
  // in-run measurement yet to correct with (mCorrection defaults to 1.0
  // and only starts moving once a stage closes), so the raw calibrated
  // sum already IS the best estimate available at plan start. The one
  // caller (update()'s short-operation fallback) only ever asks for this
  // near the beginning of the operation, before the gates would open.
  if (mAdHoc || mStages.isEmpty()) return -1;
  double sum = 0.0;
  for (const StageState& s : mStages) {
    if (s.estimatedMs <= 0) return -1;
    sum += s.estimatedMs;
  }
  return qint64(sum);
}

double TTProgressEstimator::currentProjectionMs(qint64 now) const
{
  if (mCurrentIdx < 0) return -1.0;
  const qint64 elapsed = now - mStageStartMs;
  if (mStagePercent >= kMinPercentForRate && elapsed >= kMinElapsedForRate)
    return double(elapsed) * 100.0 / double(mStagePercent);
  return -1.0;
}

// Remaining ms of the CURRENT stage projected from the trailing rate window
// only (see mRateSamples doc in the header) - never used for stage weights.
// Returns < 0 when the window is too thin/degenerate; caller falls back to
// the whole-stage average (currentProjectionMs-based remStage) in that case.
double TTProgressEstimator::windowedRemainingMs() const
{
  if (mRateSamples.size() < kMinWindowSamples) return -1.0;

  const RateSample& first = mRateSamples.first();
  const RateSample& last  = mRateSamples.last();
  const qint64 dt = last.ms - first.ms;
  const int    dp = last.percent - first.percent;
  if (dt < kMinWindowSpanMs || dp <= 0) return -1.0;

  const double rate = double(dp) / double(dt);  // percent per ms
  return qMax(0.0, (100.0 - double(last.percent)) / rate);
}

TTProgressEstimator::Result TTProgressEstimator::update(int stagePercent)
{
  Result r;
  if (mCurrentIdx < 0) {
    // Passthrough: no stage announced - today's behavior, no ETA.
    r.totalPercent = qBound(0, stagePercent, 100);
    r.kind = RemainingUnknown;
    r.stage = -1;
    r.remainingMs = 0;
    return r;
  }

  const qint64 now = mClock();
  mStagePercent = qBound(0, stagePercent, 100);
  const qint64 stageElapsed = now - mStageStartMs;
  const double proj = currentProjectionMs(now);

  // Recent-rate window for the remaining-time output (windowedRemainingMs);
  // record every sample, then trim to the trailing ~15s - but never below
  // kMinWindowSamples, so a slow-cadence stage still has something to rate.
  mRateSamples.append({ stageElapsed, mStagePercent });
  while (mRateSamples.size() > kMinWindowSamples
         && (stageElapsed - mRateSamples.first().ms) > kRateWindowMs) {
    mRateSamples.removeFirst();
  }

  // In-flight self-correction (spec §4.3): mCorrection is an EMA updated
  // only when a stage CLOSES (closeCurrentStage()), so during the very
  // first stage of an operation it is still 1.0 - future-stage estimates
  // would stay uncorrected until that stage ends. Once the current stage
  // has both a calibrated estimate and a live rate projection, blend a
  // same-stage "in-flight ratio" (proj / estimatedMs) 50/50 with the EMA
  // for THIS call's future-stage weights/remaining-time only. This is a
  // read-only blend: it is never written back into mCorrection itself,
  // which keeps updating solely at stage close as before. Stages with an
  // empty calibKey (video) have estimatedMs < 0, so no in-flight ratio is
  // available there - effectiveCorrection just falls back to mCorrection.
  double effectiveCorrection = mCorrection;
  if (mStages[mCurrentIdx].estimatedMs > 0 && proj > 0) {
    const double inflightRatio = proj / mStages[mCurrentIdx].estimatedMs;
    effectiveCorrection = 0.5 * mCorrection + 0.5 * inflightRatio;
  }

  // ---- effective per-stage weights (ms): measured > projection >
  //      corrected estimate > unknown
  const int n = mStages.size();
  QVector<double> w(n, -1.0);
  bool allKnown = true;
  for (int i = 0; i < n; ++i) {
    const StageState& s = mStages[i];
    if (s.measuredMs >= 0)            w[i] = s.measuredMs;
    else if (i == mCurrentIdx && proj > 0) w[i] = proj;
    else if (s.estimatedMs > 0)       w[i] = s.estimatedMs * effectiveCorrection;
    else                              allKnown = false;
  }

  // ---- total percent
  double pct;
  if (allKnown) {
    double all = 0, done = 0;
    for (int i = 0; i < n; ++i) all += w[i];
    for (int i = 0; i < mCurrentIdx; ++i) done += w[i];
    done += w[mCurrentIdx] * mStagePercent / 100.0;
    pct = (all > 0) ? 100.0 * done / all : 0.0;
  } else {
    // Cold start: equal bands, one single 0-100 sweep, honest but coarse.
    const double band = 100.0 / n;
    pct = band * mCurrentIdx + band * mStagePercent / 100.0;
  }
  // qRound (nearest), not int() (truncates toward zero): the done/all
  // ratio round-trips through proj (elapsed*100/percent, then back), and
  // that round-trip can land a mathematically-exact N.0 a few ULPs below
  // N (e.g. 51.999999999999986) - int() would then truncate to N-1, a
  // one-frame "regression" with no real cause (found by T-c below, which
  // is the first test to call update() with an unchanged percent across
  // many varying-elapsed calls).
  int ipct = qBound(0, qRound(pct), 100);
  // Monotone clamp applies only to planned (setPlan) operations, where
  // stage weights make "never decreases" correct. Ad-hoc single-stage
  // operations (pool: open/scan/search) register their total incrementally
  // and a drop is an honest re-estimate, not a glitch - see mAdHoc comment.
  if (!mAdHoc) {
    if (ipct < mLastTotalPercent) ipct = mLastTotalPercent;
  }
  mLastTotalPercent = ipct;

  // ---- remaining time
  r.totalPercent = ipct;
  r.stage = mStages[mCurrentIdx].plan.stage;
  double remStage = -1.0;
  if (proj > 0) {
    // Global whole-stage average - the previous behavior, and still the
    // fallback when the recent-rate window is too thin/degenerate.
    remStage = qMax(0.0, proj - double(stageElapsed));

    // Prefer the recent-rate window: an initial burst (e.g. Smart Cut's
    // stream-copy-first ordering) otherwise dominates the whole-stage
    // average for minutes, showing a far-too-optimistic ETA. The windowed
    // rate reacts to the CURRENT pace instead. Only remStage is replaced -
    // stage weights (w[] above, effectiveCorrection) stay on the global
    // projection so the progress bar itself does not jump when the window
    // takes over (see header doc on windowedRemainingMs).
    const double windowed = windowedRemainingMs();
    if (windowed >= 0)
      remStage = windowed;
  }

  if (remStage < 0) {
    // Short operations (whole plan under kShortOpCeilingMs) can finish
    // before the per-stage gates (kMinPercentForRate/kMinElapsedForRate)
    // ever open, leaving the display stuck on "calculating..." for the
    // entire run. When the plan is fully calibrated and short, fall back
    // to the whole-plan estimate minus time already spent since plan
    // start - a coarse but honest number beats a permanent "calculating".
    // Guarded to planned (non-ad-hoc) operations only: ad-hoc single-stage
    // ops (pool: open/scan/search) have no calibrated plan to fall back to.
    if (!mAdHoc) {
      const qint64 planned = plannedEstimateMs();
      if (planned > 0 && planned <= kShortOpCeilingMs) {
        r.kind = RemainingTotal;
        r.remainingMs = qMax(kShortOpMinRemainMs, planned - (now - mOpStartMs));
        return r;
      }
    }
    r.kind = RemainingUnknown;
    r.remainingMs = 0;
    return r;
  }

  double remFuture = 0;
  bool futureKnown = true;
  for (int i = mCurrentIdx + 1; i < n; ++i) {
    if (mStages[i].estimatedMs > 0) remFuture += mStages[i].estimatedMs * effectiveCorrection;
    else futureKnown = false;
  }

  if (futureKnown) {
    r.kind = RemainingTotal;
    r.remainingMs = qint64(remStage + remFuture);
  } else {
    r.kind = RemainingStageOnly;
    r.remainingMs = qint64(remStage);
  }
  return r;
}

void TTProgressEstimator::finishOperation(bool regular)
{
  closeCurrentStage(regular);
  if (mOpStartMs >= 0)
    mOpDurationMs = mClock() - mOpStartMs;
  mStages.clear();
  mCurrentIdx = -1;
  mStagePercent = 0;
  mOpStartMs = -1;
  mLastTotalPercent = 0;
}
