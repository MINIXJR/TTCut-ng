// Diagnostic: TTProgressEstimator contract on a simulated clock.
// Build: cmake --build build --target diag ; Usage: test_progressestimator
#include <cstdio>
#include "common/istatusreporter.h"
#include "common/ttcalibrationstore.h"
#include "common/ttprogressestimator.h"

static int gFail = 0;
#define CHECK(cond, name) do { \
  bool ok = (cond); \
  fprintf(stderr, "%s %s\n", ok ? "PASS" : "FAIL", name); \
  if (!ok) gFail++; } while (0)

int main()
{
  using SRA = StatusReportArgs;
  qint64 now = 0;
  auto clock = [&now]() { return now; };

  // --- 1) Durchreich-Modus: update ohne Plan/Stage reicht Prozent roh durch
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    auto r = est.update(42);
    CHECK(r.totalPercent == 42 && r.kind == TTProgressEstimator::RemainingUnknown,
          "passthrough without plan");
  }

  // --- 2) Kaltstart, 2 Stufen unkalibriert: Gleichband + StageOnly-Restzeit
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0},
                  {SRA::StageMux,   "mux/test",  60.0} });
    est.beginStage(SRA::StageAudio);
    now = 1000;  auto r1 = est.update(10);
    CHECK(r1.kind == TTProgressEstimator::RemainingUnknown,
          "no ETA before 3s");
    now = 4000;  auto r2 = est.update(40);
    // Stufe unkalibriert, Zukunft (Mux) unkalibriert -> Gleichband:
    // Band 50%, in der Stufe 40% -> total 20; Restzeit nur Stufe.
    CHECK(r2.totalPercent == 20, "equal-band total on cold start");
    CHECK(r2.kind == TTProgressEstimator::RemainingStageOnly
          && r2.stage == SRA::StageAudio, "stage-only ETA on cold start");
    // Rate: 40% in 4s -> 100% = 10s -> Rest 6s
    CHECK(r2.remainingMs >= 5500 && r2.remainingMs <= 6500,
          "stage ETA from measured rate");
    now = 9000;  est.update(100);                    // Stufe regulär zu Ende
    now = 10000; est.beginStage(SRA::StageMux);      // Audio endet regulär
    CHECK(store.factor("audio/ac3") > 0, "calibration written at stage end");
    // 10000 ms fuer 60 units -> ~166.7 ms/unit
    CHECK(store.factor("audio/ac3") > 160 && store.factor("audio/ac3") < 173,
          "calibration factor value");
    now = 11000; auto r3 = est.update(10);
    CHECK(r3.totalPercent >= 50, "monotone across stage boundary");
    est.finishOperation(true);
  }

  // --- 3) Kalibrierter Lauf: Gesamt-ETA ab Beginn + Korrekturfaktor
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3", 100.0);   // 100 ms/unit
    store.setFactor("mux/test",  50.0);
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0},    // est 6000 ms
                  {SRA::StageMux,   "mux/test",  60.0} }); // est 3000 ms
    est.beginStage(SRA::StageAudio);
    now = 4000;  auto r1 = est.update(40);
    CHECK(r1.kind == TTProgressEstimator::RemainingTotal,
          "total ETA when all stages calibrated");
    // In-flight self-correction (spec §4.3): mCorrection is still 1.0 (no
    // stage has CLOSED yet), so remFuture/w[] use an in-flight blend
    // instead: proj = elapsed*100/pct = 4000*100/40 = 10000;
    // inflightRatio = proj/estimatedMs(6000) = 1.66667;
    // effectiveCorrection = 0.5*1.0 + 0.5*1.66667 = 1.33333.
    // remStage = proj - elapsed = 10000 - 4000 = 6000
    // remFuture = mux.estimatedMs(3000) * 1.33333 = 4000
    // remainingMs = 6000 + 4000 = 10000 (exact, modulo fp rounding).
    // Window deliberately excludes the OLD uncorrected value (6000 +
    // 3000*mCorrection(1.0) = 9000) - this gate must fail against the
    // pre-fix code, proving the in-flight blend is actually applied.
    CHECK(r1.remainingMs >= 9500 && r1.remainingMs <= 10500,
          "total ETA self-corrects in-flight for slower machine (excludes stale 9000)");
    // Gewichteter Gesamtfortschritt: Stufe projiziert 10s, Mux ~3s*corr
    CHECK(r1.totalPercent >= 25 && r1.totalPercent <= 40,
          "weighted total percent");
    est.finishOperation(true);
  }

  // --- 4) Abbruch schreibt keine Kalibrierung
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0} });
    est.beginStage(SRA::StageAudio);
    now = 2000; est.update(30);
    est.finishOperation(false);
    CHECK(store.factor("audio/ac3") < 0, "abort writes no calibration");
  }

  // --- 5) Regulaeres Ende bei <99% schreibt keine Kalibrierung
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0} });
    est.beginStage(SRA::StageAudio);
    now = 2000; est.update(30);
    est.finishOperation(true);   // Stufe nie bei >=99%
    CHECK(store.factor("audio/ac3") < 0, "incomplete stage writes no calibration");
  }

  // --- 6) Plausibilisierung: Ausreisser (>100x Vorwert) wird verworfen
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3", 1.0);
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 1.0} });  // 1 unit
    est.beginStage(SRA::StageAudio);
    now = 500; est.update(50);
    now = 1000; est.update(100);
    est.finishOperation(true);   // 1000 ms / 1 unit = 1000x Vorwert
    CHECK(store.factor("audio/ac3") == 1.0, "outlier factor rejected");
  }

  // --- 7) Fremde Stage waehrend laufender Operation -> Reset auf Ad-hoc
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageVideo, "", 100.0},
                  {SRA::StageAudio, "audio/ac3", 60.0} });
    est.beginStage(SRA::StageVideo);
    now = 2000; est.update(50);
    est.beginStage(SRA::StagePool);   // kommt im Plan nicht (mehr) vor
    now = 3000; auto r = est.update(10);
    CHECK(est.active() && r.totalPercent <= 10,
          "foreign stage resets to ad-hoc single stage");
    est.finishOperation(true);
  }

  // --- 8) Doppelte Stage-Meldung ist No-op
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0} });
    est.beginStage(SRA::StageAudio);
    now = 4000; est.update(40);
    est.beginStage(SRA::StageAudio);          // No-op, darf nichts nullen
    now = 5000; auto r = est.update(50);
    CHECK(r.kind != TTProgressEstimator::RemainingUnknown,
          "repeated stage announce is a no-op");
  }

  // --- 9) Ad-hoc-Einzelstufe (Pool): Total-ETA sobald messbar
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.beginStage(SRA::StagePool);           // ohne setPlan
    now = 4000; auto r = est.update(40);
    CHECK(r.kind == TTProgressEstimator::RemainingTotal,
          "single ad-hoc stage reports Total ETA");
    CHECK(r.totalPercent == 40, "ad-hoc stage percent passes through");
    est.finishOperation(true);
    CHECK(est.operationDurationMs() == 4000, "operation duration recorded");
  }

  // --- T-a) Design decision (2026-08-09 UAT fix): the monotone clamp on
  // totalPercent applies ONLY to planned (setPlan) operations, where stage
  // weights make "never decreases" the correct contract. Ad-hoc single-stage
  // operations (resetToAdHoc: pool open/scan/search) register their total
  // INCREMENTALLY as sub-tasks announce their own totals - tiny tasks (e.g.
  // subtitle/audio header) finish first, so overallPercentage() legitimately
  // reads ~99% early and then DROPS once a big task (frame index) registers
  // its total. Clamping that drop away froze the GUI bar at a false 99%
  // peak for seconds (UAT screenshot 2026-08-09). So: planned stages still
  // clamp backward/filler steps; ad-hoc stages now pass the raw bounded
  // percent through, drop included.
  //
  // T-a1) Planned stage: backward/filler Step must not move percent
  // backwards, and a subsequent real Step must recover an ETA. (With
  // IMPORTANT-2 the GUI no longer routes filler states through update(),
  // but the estimator itself must stay robust against it.)
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StagePool, "", 100.0} });   // planned - clamp applies
    est.beginStage(SRA::StagePool);
    now = 4000; auto r1 = est.update(50);
    CHECK(r1.kind == TTProgressEstimator::RemainingTotal,
          "T-a1: mid-stage ETA established at 50% (planned)");
    now = 4500; auto r2 = est.update(0);      // backward/filler Step
    CHECK(r2.totalPercent >= r1.totalPercent,
          "T-a1: planned stage clamps backward/filler step (no decrease)");
    now = 5000; auto r3 = est.update(55);     // real Step recovers
    CHECK(r3.kind != TTProgressEstimator::RemainingUnknown,
          "T-a1: ETA returns after a real step following a filler");
    est.finishOperation(true);
  }

  // T-a2) Ad-hoc stage (pool ops, no setPlan): a genuine drop (e.g. a big
  // task registering its total after small tasks finished first) must be
  // visible, not clamped away - this is the actual UAT fix.
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.beginStage(SRA::StagePool);           // ad-hoc single stage
    now = 4000; auto r1 = est.update(99);
    CHECK(r1.totalPercent == 99, "T-a2: ad-hoc stage reaches 99% early");
    now = 4500; auto r2 = est.update(60);     // total re-estimated downward
    CHECK(r2.totalPercent == 60,
          "T-a2: ad-hoc stage passes a real drop through unclamped");
    now = 5000; auto r3 = est.update(70);     // recovers upward again
    CHECK(r3.totalPercent == 70, "T-a2: ad-hoc stage resumes upward");
    est.finishOperation(true);
  }

  // --- T-b) MPEG-2 three-stage order Audio->Video->Mux, video stage
  // WITHOUT a calibKey: StageOnly while a future stage (video) is unknown,
  // Total once video itself has a live projection, monotone percent across
  // all boundaries, and calibration written only for audio+mux.
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3", 100.0);   // pre-calibrated, will be overwritten
    store.setFactor("mux/test",  50.0);
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0},   // est 6000 ms
                  {SRA::StageVideo, "",          100.0},  // no calibKey - never known
                  {SRA::StageMux,   "mux/test",  60.0} }); // est 3000 ms
    QVector<int> percents;

    est.beginStage(SRA::StageAudio);
    now = 4000; auto ra = est.update(40);
    percents.append(ra.totalPercent);
    CHECK(ra.kind == TTProgressEstimator::RemainingStageOnly && ra.stage == SRA::StageAudio,
          "T-b: audio stage is StageOnly (video future unknown)");
    now = 9000; percents.append(est.update(100).totalPercent);

    now = 10000; est.beginStage(SRA::StageVideo);   // closes+calibrates audio
    now = 14000; auto rv = est.update(40);
    percents.append(rv.totalPercent);
    CHECK(rv.kind == TTProgressEstimator::RemainingTotal && rv.stage == SRA::StageVideo,
          "T-b: video stage reaches Total once it has its own live projection");
    now = 20000; percents.append(est.update(100).totalPercent);

    now = 21000; est.beginStage(SRA::StageMux);     // closes video (no calibKey)
    now = 25000; percents.append(est.update(50).totalPercent);
    now = 27000; percents.append(est.update(100).totalPercent);

    bool monotone = true;
    for (int i = 1; i < percents.size(); ++i)
      if (percents[i] < percents[i - 1]) monotone = false;
    CHECK(monotone, "T-b: totalPercent monotone across all three stage boundaries");

    est.finishOperation(true);
    CHECK(store.factor("audio/ac3") > 0, "T-b: audio calibration written");
    CHECK(store.factor("mux/test") > 0, "T-b: mux calibration written");
    CHECK(store.factor(QString()) < 0, "T-b: no calibration written for the video stage");
  }

  // --- T-b2) User-decided spec change 2026-08-09: the video stage now
  // carries a per-codec calibKey (video/mpeg2cut, video/h264, video/h265)
  // instead of T-b's empty one. With the video factor pre-calibrated,
  // futureKnown (update()'s remFuture loop) can be true for every stage
  // after the current one as soon as the FIRST stage (audio) opens its own
  // rate gate - the StageOnly limitation T-b exercises for an uncalibrated
  // video stage disappears. This is the arithmetic that makes it so:
  //   proj  = stageElapsed*100/stagePercent = 4000*100/40 = 10000 (gate
  //           open: elapsed 4000 >= kMinElapsedForRate 3000, percent 40 >=
  //           kMinPercentForRate 2)
  //   video.estimatedMs = 50 ms/unit * 100 units = 5000 (>0, was -1 in T-b)
  //   mux.estimatedMs   = 50 ms/unit *  60 units = 3000 (>0, same as T-b)
  //   -> futureKnown = true already during the audio stage -> RemainingTotal
  // If video/h264's factor were absent (estimatedMs = -1, T-b's case),
  // futureKnown would be false and this would be RemainingStageOnly instead
  // - so the assertion below is false by construction whenever the video
  // stage lacks a calibKey, which is exactly the regression this covers.
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3",  100.0);   // 100 ms/unit
    store.setFactor("video/h264",  50.0);   // 50 ms/unit  (video now calibrated)
    store.setFactor("mux/test",    50.0);
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3",  60.0},    // est 6000 ms
                  {SRA::StageVideo, "video/h264", 100.0},    // est 5000 ms
                  {SRA::StageMux,   "mux/test",   60.0} });  // est 3000 ms
    est.beginStage(SRA::StageAudio);
    now = 4000; auto ra = est.update(40);
    CHECK(ra.kind == TTProgressEstimator::RemainingTotal && ra.stage == SRA::StageAudio,
          "T-b2: total ETA already during audio stage once video is calibrated too");
    est.finishOperation(true);
  }

  // --- T-c) "fast-then-slow": recent-rate window must dominate the ETA over
  // the whole-stage average (round 6 UAT fix: H.264 cut showed an optimistic
  // ~1:30 ETA for over a minute because the initial stream-copy burst
  // dragged the global average down). Ad-hoc single stage - simplest setup
  // that isolates the remaining-time behavior; the spec requires the window
  // to feed ONLY remStage, never the stage weight, and for a single-stage
  // ad-hoc operation totalPercent == stagePercent by construction regardless
  // of remStage, so a stable totalPercent across the window takeover is
  // guaranteed by that separation - the assertion below still locks the
  // contract against a future refactor that merges the two paths.
  {
    TTMemoryCalibrationStore store;
    TTProgressEstimator est(&store, clock);
    now = 0;
    est.beginStage(SRA::StagePool);   // ad-hoc single stage, no setPlan

    QVector<int> percents;
    // Fast burst: 0 -> 50% over 5s (1s steps).
    for (int t = 1000; t <= 5000; t += 1000) {
      now = t;
      percents.append(est.update(t / 100).totalPercent);   // 10,20,30,40,50
    }
    // Slow phase: 50 -> 52% over the following 20s (1s steps).
    TTProgressEstimator::Result last;
    for (int t = 6000; t <= 25000; t += 1000) {
      now = t;
      double pct = 50.0 + double(t - 5000) / 20000.0 * 2.0;
      last = est.update(int(pct + 0.5));
      percents.append(last.totalPercent);
    }

    bool monotone = true;
    for (int i = 1; i < percents.size(); ++i)
      if (percents[i] < percents[i - 1]) monotone = false;
    CHECK(monotone,
          "T-c: totalPercent does not jump/regress when the window takes over");

    // Global whole-stage average at t=25000, percent=52 (pre-fix behavior):
    // proj = elapsed*100/percent = 25000*100/52 = 48076.9 ms (100% duration)
    // remaining = proj - elapsed = 48076.9 - 25000 = 23076.9 ms - dominated
    // by the fast burst, wildly optimistic given the CURRENT (slow) pace.
    double globalProj = double(25000) * 100.0 / 52.0;
    double globalRemaining = globalProj - 25000.0;

    // Windowed rate (last ~15s, entirely inside the slow phase, so it reads
    // the constant 2%/20000ms = 0.0001 %/ms rate regardless of the exact
    // window edge): remaining = (100-52) / (2/20000) = 480000 ms.
    CHECK(last.remainingMs >= 300000,
          "T-c: remaining time reflects the slow recent rate, not the fast burst");
    CHECK(double(last.remainingMs) >= 3.0 * globalRemaining,
          "T-c: windowed remaining is at least 3x the (optimistic) global average");
    est.finishOperation(true);
  }

  // --- T-d) Short-operation ETA fallback (round 8 UAT fix): a fully
  // calibrated plan whose TOTAL duration is short enough that the
  // per-stage rate gates (kMinPercentForRate/kMinElapsedForRate) never
  // open before the operation ends must still show a real ETA instead of
  // "calculating..." for the whole run - see plannedEstimateMs() and
  // update()'s short-op fallback.
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3", 50.0);   // 50 ms/unit * 60  units = 3000 ms
    store.setFactor("mux/test",  50.0);   // 50 ms/unit * 100 units = 5000 ms
    TTProgressEstimator est(&store, clock);        // plan total = 8000 ms
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0},
                  {SRA::StageMux,   "mux/test",  100.0} });
    est.beginStage(SRA::StageAudio);
    now = 500; auto r = est.update(10);   // 10% >= 2%, but 500ms < 3000ms: gate closed
    CHECK(r.kind == TTProgressEstimator::RemainingTotal,
          "T-d: short fully-calibrated plan reports Total ETA before gates open");
    CHECK(r.remainingMs <= 8000,
          "T-d: short-plan remaining bounded by the whole-plan estimate");
  }

  // T-d2) Counter-test, same shape: when the whole-plan estimate exceeds
  // the short-op ceiling, stay Unknown until the gates open normally - the
  // fallback must not mask genuinely long operations behind an early guess.
  {
    TTMemoryCalibrationStore store;
    store.setFactor("audio/ac3", 500.0);   // 500 ms/unit * 60  units = 30000 ms
    store.setFactor("mux/test",  300.0);   // 300 ms/unit * 100 units = 30000 ms
    TTProgressEstimator est(&store, clock);        // plan total = 60000 ms
    now = 0;
    est.setPlan({ {SRA::StageAudio, "audio/ac3", 60.0},
                  {SRA::StageMux,   "mux/test",  100.0} });
    est.beginStage(SRA::StageAudio);
    now = 500; auto r = est.update(10);
    CHECK(r.kind == TTProgressEstimator::RemainingUnknown,
          "T-d2: long fully-calibrated plan stays Unknown before gates open");
  }

  fprintf(stderr, "%s\n", gFail == 0 ? "ALL PASS" : "FAILURES");
  return gFail == 0 ? 0 : 1;
}
