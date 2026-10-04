// Diagnostic harness for TTAudioAnomalyScanTask (Task 6 of the
// audio-anomaly-repair plan).
//
// Usage:
//   test_anomalyscan
//     Self-test: pure evaluate()/videoFrameForTime() unit cases (synthetic,
//     no file I/O), then an integration run on
//     /usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_short.ac3 (built via
//     make_anomaly_sample.sh if missing) - both the raw collectFrameStats()
//     + evaluate() combo (exact AC3 frame range) and a full task run via
//     runSynchron() (video-frame marker + description). Prints
//     PASS/FAIL per check and "ALL PASS"/"FAILED" at the end.
//
//   test_anomalyscan <ac3-file> [trackIndex] [frameRate] [seconds ...]
//     Real-file gate: runs the full scan (calibrated default settings) on
//     the given AC3 file and prints every finding (AC3 frame range, video
//     frame, LFE peak, confidence, description), plus one "LFE <from> <to>"
//     and one "STOP <seconds> <drop dB> <mask>" line per finding of the two
//     searches for gate_anomaly_real.sh. Each further argument is a time
//     in seconds (decimal point) around which the block levels the stop
//     search works on are printed. Exit 0.
//
// Build via `cmake --build build --target test_anomalyscan`.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QString>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "data/ttaudioanomalyscantask.h"
#include "common/ttsettings.h"

using FrameStat = TTAudioAnomalyScanTask::FrameStat;
using Finding   = TTAudioAnomalyScanTask::Finding;

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

// ---------------------------------------------------------------------------
// evaluate() unit tests - synthetic, no file I/O.
// ---------------------------------------------------------------------------

// Builds a stats vector of n frames: background LFE/contrast everywhere,
// with an island [islandFrom, islandTo] (inclusive) overridden to the given
// island LFE level and center-contrast.
static QVector<FrameStat> buildStats(int n, int islandFrom, int islandTo,
                                     float islandLfeDb, float islandContrast,
                                     float bgLfeDb = -120.0f, float bgContrast = 0.1f)
{
    QVector<FrameStat> stats(n);
    for (int i = 0; i < n; ++i) {
        const bool inIsland = i >= islandFrom && i <= islandTo;
        stats[i].is51 = true;
        stats[i].lfeRms = inIsland ? islandLfeDb : bgLfeDb;
        stats[i].centerRms = -20.0f;
        stats[i].centerMaxDiff = inIsland ? islandContrast : bgContrast;
    }
    return stats;
}

static void testEvaluate()
{
    // Calibrated final thresholds (docs/superpowers/specs/2026-08-19-audio-
    // anomaly-repair-design.md + progress.md Task-6 ruling).
    const double kLfeRmsDb    = -55.0;
    const double kContrast    = 4.0;
    const double kNullPercent = 99.0;
    const double kMinPeakDb   = -22.0;

    // A) Positive finding: island 3000-3005 (6 frames, above -55dB gate,
    // loud enough for the -22dB MinPeak gate), with 4x center contrast vs.
    // background 0.1 everywhere else in 10000 frames. Fine segmentation
    // narrows the reported range to the actual burst frames (here: the
    // whole island, since it is uniformly loud throughout - a deliberate
    // synthetic edge case, see anomaly_short.ac3 below for the same
    // shape), with a +/-1 frame safety margin.
    {
        auto stats = buildStats(10000, 3000, 3005, -20.0f, 0.4f);
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.size() == 1,
              QString("A) uniform loud+contrast island -> exactly 1 finding (got %1)")
                  .arg(findings.size()));
        if (findings.size() == 1) {
            check(findings[0].frameFrom == 2999 && findings[0].frameTo == 3006,
                  QString("A) fine-segmented range == 2999-3006 (island +/-1 margin, got %1-%2)")
                      .arg(findings[0].frameFrom).arg(findings[0].frameTo));
            check(findings[0].lfePeak >= float(kMinPeakDb),
                  QString("A) lfePeak %1 dB >= MinPeak %2 dB")
                      .arg(findings[0].lfePeak).arg(kMinPeakDb));
        }
    }

    // A2) The same island, 38 frames long: longer than kMaxLfeFindingFrames -
    // a deep tone or an explosion, not a short defect.
    {
        auto stats = buildStats(10000, 3000, 3037, -20.0f, 0.4f);
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.isEmpty(),
              QString("A2) island of 38 frames -> no finding (got %1)").arg(findings.size()));
    }

    // B) Kontrast-Bedingung: LFE-active island but no center contrast
    // (0.1 in the island too, same as background) -> not confirmed, no
    // finding regardless of how loud the LFE is. This is the "material
    // fine, nothing found" gate case: the null% precondition passes
    // (island is only 6 of 10000 frames), so GateStatus must say so. The
    // island is short on purpose: the length limit must not be what drops it.
    {
        auto stats = buildStats(10000, 3000, 3005, -20.0f, 0.1f);
        TTAudioAnomalyScanTask::GateStatus gate;
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb, &gate);
        check(findings.isEmpty(),
              QString("B) LFE island without center contrast -> no finding (got %1)")
                  .arg(findings.size()));
        check(!gate.materialUnsuitable,
              QString("B) clean case: GateStatus.materialUnsuitable == false (null%=%1)")
                  .arg(gate.lfeNullPercent));
    }

    // C) Nullanteil-Vorbedingung: LFE active (-40dB, above the -55dB gate)
    // across ALL frames -> material unsuitable, no fallback to a weaker
    // heuristic, no finding even though contrast is present. This is the
    // gate-failure case an empty findings list alone cannot be told apart
    // from B) - GateStatus must say so explicitly (Fix 1).
    {
        QVector<FrameStat> stats(10000);
        for (int i = 0; i < 10000; ++i) {
            stats[i].is51 = true;
            stats[i].lfeRms = -40.0f;
            stats[i].centerMaxDiff = (i >= 3000 && i <= 3037) ? 0.4f : 0.1f;
        }
        TTAudioAnomalyScanTask::GateStatus gate;
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb, &gate);
        check(findings.isEmpty(),
              QString("C) LFE active everywhere (-40dB) -> unsuitable material, no finding (got %1)")
                  .arg(findings.size()));
        check(gate.materialUnsuitable,
              QString("C) unsuitable case: GateStatus.materialUnsuitable == true (null%=%1, expect ~0)")
                  .arg(gate.lfeNullPercent));
        check(gate.lfeNullPercent < 1.0,
              QString("C) measured null percent is ~0 (got %1)").arg(gate.lfeNullPercent));
    }

    // D) MinPeak-Erweiterung: same shape as A) (LFE-active + contrast
    // confirmed), but the island's LFE peak is only -30dB, below the
    // -22dB MinPeak gate -> contrast-confirmed but not reported (User-
    // Klassifikation: real defects >= -19.7dBFS, false positives < -25dBFS).
    {
        auto stats = buildStats(10000, 3000, 3005, -30.0f, 0.4f);
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.isEmpty(),
              QString("D) island LFE peak -30dB < MinPeak -22dB -> no finding (got %1)")
                  .arg(findings.size()));
    }

    // E) Abklingschwanz-Fall (the real 02x06 pattern): island 3000-3037,
    // but only 3000-3005 is the actual audible burst (LFE >= MinPeak,
    // center contrast high); 3006-3037 is a quiet decay tail (LFE above
    // the bare -55dB activity gate but below the -22dB MinPeak, no center
    // contrast). A regressed implementation that reports the raw island
    // would pass Test A (uniform island) unchanged but fails here: the
    // reported range must stop at the burst, not run to 3037.
    {
        QVector<FrameStat> stats(10000);
        for (int i = 0; i < 10000; ++i) {
            const bool burst = i >= 3000 && i <= 3005;
            const bool tail  = i >= 3006 && i <= 3037;
            stats[i].is51 = true;
            stats[i].lfeRms = burst ? -20.0f : (tail ? -35.0f : -120.0f);
            stats[i].centerMaxDiff = burst ? 0.4f : 0.1f;
        }
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.size() == 1,
              QString("E) decay-tail island -> exactly 1 finding (got %1)").arg(findings.size()));
        if (findings.size() == 1) {
            check(findings[0].frameTo <= 3008,
                  QString("E) fine-segmented range stops at the burst, NOT the 3037 island end "
                          "(frameTo=%1, hard limit 3008)").arg(findings[0].frameTo));
            check(findings[0].frameFrom >= 2998 && findings[0].frameFrom <= 3001,
                  QString("E) fine-segmented range starts at the burst (frameFrom=%1, expect ~2999)")
                      .arg(findings[0].frameFrom));
        }
    }

    // F) Center-Vorläufer-Fall: center contrast starts 3 frames before the
    // LFE onset (2997-3008 contrast, LFE active only 3000-3008; reported
    // range 2996-3009 = 14 frames, inside the length limit) - spec:
    // the reported range start is allowed to precede the LFE onset.
    {
        QVector<FrameStat> stats(10000);
        for (int i = 0; i < 10000; ++i) {
            const bool lfeActive = i >= 3000 && i <= 3008;
            const bool contrastActive = i >= 2997 && i <= 3008;
            stats[i].is51 = true;
            stats[i].lfeRms = lfeActive ? -20.0f : -120.0f;
            stats[i].centerMaxDiff = contrastActive ? 0.4f : 0.1f;
        }
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.size() == 1,
              QString("F) center-precursor case -> exactly 1 finding (got %1)").arg(findings.size()));
        if (findings.size() == 1) {
            // Discriminating bound, not just "< 3000": a regressed
            // implementation that ignores the contrast precursor entirely
            // (block == raw island 3000-3008) still reports
            // frameFrom = islandStart-1 = 2999 via the +/-1 safety margin
            // alone, which would satisfy "< 3000" without ever having
            // detected the precursor. The precursor starts at 2997, so a
            // correct detector must land at <= 2997 (its own -1 margin);
            // 2999 is 2 frames short of that and must fail this bound.
            check(findings[0].frameFrom <= 2997,
                  QString("F) range start captures the 2997 contrast precursor, "
                          "not just the island's own -1 margin (frameFrom=%1, must be <= 2997)")
                      .arg(findings[0].frameFrom));
        }
    }
}

// evaluateStops() unit tests - synthetic, no file I/O. One FrameStat carries
// six 256-sample blocks; levels are given per block in dBFS.
static QVector<FrameStat> stopStats(int frames, double db, int channels = 6)
{
    QVector<FrameStat> stats(frames);
    const float p = float(std::pow(10.0, db / 10.0));
    for (FrameStat& s : stats) {
        s.channels = quint8(channels);
        for (float& b : s.blockPower) b = p;
    }
    return stats;
}
static void setBlocks(QVector<FrameStat>& stats, qint64 from, qint64 to, double db)   // blocks [from, to)
{
    const float p = db <= -170.0 ? 0.0f : float(std::pow(10.0, db / 10.0));
    for (qint64 b = from; b < to; ++b) stats[int(b / 6)].blockPower[b % 6] = p;
}

static void testEvaluateStops()
{
    using Kind = TTAudioAnomalyScanTask::FindingKind;
    {   // S1: sound stops, comes back later
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3600, -70.0);
        const auto f = TTAudioAnomalyScanTask::evaluateStops(st);
        check(f.size() == 1, QString("S1 abrupt stop -> 1 finding (got %1)").arg(f.size()));
        if (f.size() == 1) {
            check(f[0].frameFrom == 499 && f[0].frameTo == 501,
                  QString("S1 range 499-501 (got %1-%2)").arg(f[0].frameFrom).arg(f[0].frameTo));
            check(f[0].kind == Kind::AbruptStop && qAbs(f[0].dropDb - 50.0f) < 0.5f && f[0].channelMask == 0x3F,
                  QString("S1 kind stop, drop 50 dB, mask 0x3F (got drop %1, mask %2)").arg(f[0].dropDb).arg(f[0].channelMask));
        }
    }
    {   // S2: decay of 5 dB per block (60 dB in 64 ms) is not a stop
        auto st = stopStats(1000, -20.0);
        for (int i = 0; i < 12; ++i) setBlocks(st, 3000 + i, 3001 + i, -20.0 - 5.0 * (i + 1));
        setBlocks(st, 3012, 3600, -80.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S2 decay -> no finding");
    }
    {   // S3: a deep hole of three blocks (16 ms, 50 dB) is one event
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3003, -70.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).size() == 1, "S3 16 ms hole, 50 dB deep -> 1 finding");
    }
    {   // S4: silence from the stop to the end of the track is the demuxer's padding
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 5400, 6000, -180.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S4 stop into silence up to the end -> no finding");
    }
    {   // S5: too quiet before
        auto st = stopStats(1000, -40.0);
        setBlocks(st, 3000, 3600, -90.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S5 level before below -35 dBFS -> no finding");
    }
    {   // S6: two lasting stops 12 blocks (64 ms) apart are one event, the larger drop wins
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3008, -60.0);
        setBlocks(st, 3012, 3040, -75.0);
        const auto f = TTAudioAnomalyScanTask::evaluateStops(st);
        check(f.size() == 1 && qAbs(f[0].dropDb - 55.0f) < 0.5f,
              QString("S6 two stops 64 ms apart -> 1 finding with drop 55 dB (got %1)").arg(f.size()));
    }
    {   // S7 (review focus 1): an undecodable frame is not a stop
        auto st = stopStats(1000, -20.0);
        st[500].channels = 0;
        for (float& b : st[500].blockPower) b = 0.0f;
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S7 undecodable frame -> no finding");
    }
    {   // S8 (review focus 2): nothing to look at
        check(TTAudioAnomalyScanTask::evaluateStops(QVector<FrameStat>()).isEmpty(), "S8 empty input -> no finding");
        check(TTAudioAnomalyScanTask::evaluateStops(stopStats(1, -20.0)).isEmpty(), "S8 one frame -> no finding");
        check(TTAudioAnomalyScanTask::evaluateStops(stopStats(1000, -180.0)).isEmpty(), "S8 silent track -> no finding");
    }
    {   // S9 (review focus 3, 5): layout change with continuous level; a stop in the stereo part
        auto st = stopStats(1000, -20.0);
        for (int i = 500; i < 1000; ++i) st[i].channels = 2;
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S9 5.1 -> 2.0 at constant level -> no finding");
        setBlocks(st, 4200, 4800, -70.0);
        const auto f = TTAudioAnomalyScanTask::evaluateStops(st);
        check(f.size() == 1 && f[0].channelMask == 0x03,
              QString("S9 stop in the stereo part -> mask 0x03 (got %1 finding(s))").arg(f.size()));
    }
    // Two forms (spec, amendment of 2026-10-04): a hole - the sound is back
    // within six blocks - counts from 45 dB, a lasting stop from 30 dB. The
    // shapes are those measured on 02x06.
    {   // S10: hole of two blocks, 52 dB deep (video frame 77493, heard)
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3002, -72.0);
        const auto f = TTAudioAnomalyScanTask::evaluateStops(st);
        check(f.size() == 1 && qAbs(f[0].dropDb - 52.0f) < 0.5f,
              QString("S10 hole of 2 blocks, 52 dB -> 1 finding (got %1)").arg(f.size()));
    }
    {   // S11: the same hole 33 dB deep (3617, 59024: not heard)
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3002, -53.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S11 hole of 2 blocks, 33 dB -> no finding");
    }
    {   // S12: hole of three blocks, 41 dB deep (64477: not heard)
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3003, -61.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).isEmpty(), "S12 hole of 3 blocks, 41 dB -> no finding");
    }
    {   // S13: lasting stop of 32 dB (24238: click)
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3020, -52.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).size() == 1, "S13 lasting stop of 32 dB -> 1 finding");
    }
    {   // S14: 40 dB down, then back only to 12 dB below the level before:
        // the sound is not "back", so this is a lasting stop, not a hole
        auto st = stopStats(1000, -20.0);
        setBlocks(st, 3000, 3002, -60.0);
        setBlocks(st, 3002, 3600, -32.0);
        check(TTAudioAnomalyScanTask::evaluateStops(st).size() == 1,
              "S14 40 dB down, back to 12 dB below only -> lasting stop, 1 finding");
    }
}

static void testVideoFrameForTime()
{
    check(TTAudioAnomalyScanTask::videoFrameForTime(2044.8, 25.0, {}) == 51120,
          "videoFrameForTime(2044.8, 25.0, {}) == 51120");

    // All 100 extras sit below index 20000, well clear of the target index
    // (~25000) - countExtrasBefore is therefore stable at 100 in one round.
    QList<int> extras;
    for (int i = 0; i < 100; ++i) extras.append(100 + i * 190);   // 100..18910, all < 20000
    const int result = TTAudioAnomalyScanTask::videoFrameForTime(1000.0, 25.0, extras);
    check(result == 25100,
          QString("videoFrameForTime(1000.0, 25.0, 100 extras < 20000) == 25100 (got %1)")
              .arg(result));

    // Runs of extras right below the target (audio-repair.md H4): the index
    // has to pass every extra of the run, which took more than the two rounds
    // the function used to allow - measured 103 instead of 106 for five in a
    // row. Expected value: the smallest index whose extras-corrected time is
    // the target, found by brute force.
    auto extrasBelow = [](const QList<int>& e, int idx) {
        int n = 0; for (int x : e) if (x < idx) ++n; return n;
    };
    const QList<QList<int>> runs = { {100, 101, 102}, {100, 101, 102, 103, 104}, {50, 100, 101, 102} };
    for (const QList<int>& run : runs) {
        for (double sec : {4.04, 4.08, 4.20}) {
            const int base = qRound(sec * 25.0);
            int want = base;
            while (want - extrasBelow(run, want) != base) ++want;
            const int got = TTAudioAnomalyScanTask::videoFrameForTime(sec, 25.0, run);
            QStringList s; for (int x : run) s << QString::number(x);
            check(got == want, QString("videoFrameForTime(%1 s, extras %2) == %3 (got %4)")
                                   .arg(sec).arg(s.join(",")).arg(want).arg(got));
        }
    }
}

// ---------------------------------------------------------------------------
// Integration: anomaly_short.ac3 (synthetic - a continuous 0.2 s loud burst,
// so fine segmentation collapses onto ~the whole island, unlike the real
// corpus case) and anomaly_sample.ac3 (the same with a burst of 1.2 s:
// longer than the limit, nothing reported).
// ---------------------------------------------------------------------------
static const QString kSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3");
static const QString kShortFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_short.ac3");
static const QString kMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_anomaly_sample.sh");

static bool ensureFixture(const QString& file, const QStringList& extraArgs)
{
    if (QFileInfo::exists(file)) return true;
    QDir().mkpath(QFileInfo(file).absolutePath());
    QProcess proc;
    proc.start(kMakeScript, QStringList{file} + extraArgs);
    if (!proc.waitForStarted(5000)) return false;
    if (!proc.waitForFinished(120000)) return false;
    return proc.exitCode() == 0 && QFileInfo::exists(file);
}
static bool ensureSample() { return ensureFixture(kSampleFile, {}); }
static bool ensureShort()  { return ensureFixture(kShortFile, {QStringLiteral("0.2")}); }

static void testSyntheticIntegration()
{
    check(ensureShort(), "fixture: anomaly_short.ac3 available (built if missing)");
    if (!QFileInfo::exists(kShortFile)) return;

    // (a) collectFrameStats() + evaluate() directly, for an exact AC3-frame
    // range assertion (the full task only exposes the video-frame mapping).
    int decodeFailures = 0;
    QVector<FrameStat> stats = TTAudioAnomalyScanTask::collectFrameStats(kShortFile, &decodeFailures);
    check(!stats.isEmpty(), QString("collectFrameStats: decoded %1 AC3 frames").arg(stats.size()));
    check(decodeFailures == 0, QString("collectFrameStats: 0 decode failures (got %1)").arg(decodeFailures));

    // Input of the stop search: every decoded frame carries its plane count
    // and six block levels (the fixture has sound on all main channels).
    int framesWithBlocks = 0;
    for (int i = 100; i < 200 && i < stats.size(); ++i) {
        bool allBlocks = stats[i].channels == 6;
        for (float b : stats[i].blockPower) allBlocks = allBlocks && b > 1e-6f;
        if (allBlocks) ++framesWithBlocks;
    }
    check(framesWithBlocks == 100,
          QString("collectFrameStats: frames 100-199 carry 6 planes and six block levels (got %1 of 100)")
              .arg(framesWithBlocks));

    TTSettings* cfg = TTSettings::instance();
    QList<Finding> findings = TTAudioAnomalyScanTask::evaluate(
        stats, cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
        cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb());

    check(findings.size() == 1,
          QString("anomaly_short.ac3: exactly 1 finding (got %1)").arg(findings.size()));
    if (findings.size() == 1) {
        const Finding& f = findings[0];
        printf("  synthetic finding: AC3 frames %lld-%lld, lfePeak=%.1fdB, confidence=%.2f\n",
               (long long)f.frameFrom, (long long)f.frameTo, f.lfePeak, f.confidence);
        // Continuous 0.2s burst (t=30..30.2s == AC3 frames 937.5..943.75):
        // the fine segmentation is expected to land close to the whole
        // island here, not the narrow real-corpus case.
        check(qAbs(f.frameFrom - 937) <= 2,
              QString("anomaly_short.ac3: frameFrom %1 == 937 +/-2").arg(f.frameFrom));
        check(qAbs(f.frameTo - 944) <= 2,
              QString("anomaly_short.ac3: frameTo %1 == 944 +/-2").arg(f.frameTo));
    }

    // The 1.2 s sample is longer than the limit: nothing reported.
    check(ensureSample(), "fixture: anomaly_sample.ac3 available");
    QVector<FrameStat> longStats = TTAudioAnomalyScanTask::collectFrameStats(kSampleFile, nullptr);
    check(!longStats.isEmpty(), "anomaly_sample.ac3 decoded");
    check(TTAudioAnomalyScanTask::evaluate(longStats, cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
              cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb()).isEmpty(),
          "anomaly_sample.ac3 (1.2 s burst): no finding");

    // (b) Full task run via runSynchron() - the actual production path,
    // checking the video-frame marker and description text.
    QList<TTStreamPoint> points;
    TTAudioAnomalyScanTask task(kShortFile, 0, 25.0, QList<int>(), QList<QPair<int,int>>());
    QObject::connect(&task, &TTAudioAnomalyScanTask::pointsDetected,
                     [&points](const QList<TTStreamPoint>& p) { points = p; });
    task.runSynchron();

    check(points.size() == 1,
          QString("task run: exactly 1 StreamPoint (got %1)").arg(points.size()));
    if (points.size() == 1) {
        printf("  synthetic marker: frame=%d desc=%s duration=%.3fs\n",
               points[0].frameIndex(), qPrintable(points[0].description()), points[0].duration());
        check(qAbs(points[0].frameIndex() - 750) <= 2,
              QString("task run: video frame %1 == 750 +/-2").arg(points[0].frameIndex()));
        check(points[0].description().contains("C+LFE"),
              QString("task run: description contains \"C+LFE\" (got: %1)")
                  .arg(points[0].description()));
        check(points[0].type() == StreamPointType::AudioAnomaly,
              "task run: point type == AudioAnomaly");
        // Final review I3: the marker carries the finding's own AC3 frame
        // range, so the repair dialog does not have to invert the lossy
        // video-frame projection.
        check(points[0].hasAudioFrameRange(), "task run: marker carries an exact AC3 frame range");
        if (findings.size() == 1) {
            check(points[0].audioFrameFrom() == findings[0].frameFrom &&
                  points[0].audioFrameTo()   == findings[0].frameTo,
                  QString("task run: marker AC3 range == finding range (%1-%2, got %3-%4)")
                      .arg(findings[0].frameFrom).arg(findings[0].frameTo)
                      .arg(points[0].audioFrameFrom()).arg(points[0].audioFrameTo()));
        }
    }
}

// ---------------------------------------------------------------------------
// Stop search on decoded fixtures (make_stop_sample.sh), 5.1 and 2.0: the
// findings, and the markers the task makes of them.
// ---------------------------------------------------------------------------
static void testStopIntegration()
{
    const QString script = QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_stop_sample.sh");
    for (const QString& mode : {QStringLiteral("5.1"), QStringLiteral("stereo")}) {
        const QString file = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/stop_sample_%1.ac3").arg(mode);
        if (!QFileInfo::exists(file)) {
            QProcess proc;
            proc.start(script, {file, mode});
            if (!proc.waitForStarted(5000) || !proc.waitForFinished(120000) || proc.exitCode() != 0) {
                check(false, "stop fixture " + mode + ": make_stop_sample.sh did not run");
                continue;
            }
        }
        const quint8 wantMask = mode == "5.1" ? 0x3F : 0x03;
        const QVector<FrameStat> stats = TTAudioAnomalyScanTask::collectFrameStats(file, nullptr);
        const QList<Finding> stops = TTAudioAnomalyScanTask::evaluateStops(stats);
        check(stops.size() == 2, QString("stop fixture %1: 2 findings (got %2)").arg(mode).arg(stops.size()));
        for (const Finding& f : stops)
            printf("  stop fixture %s: AC3 frames %lld-%lld, drop %s dB, mask %d\n", qPrintable(mode),
                   (long long)f.frameFrom, (long long)f.frameTo,
                   qPrintable(QString::number(f.dropDb, 'f', 1)), int(f.channelMask));
        if (stops.size() == 2) {
            // 10.003 s lies in AC3 frame 312, 20.000 s is the start of frame 625.
            check(qAbs(stops[0].frameFrom + 1 - 312) <= 1 && qAbs(stops[1].frameFrom + 1 - 625) <= 1,
                  QString("stop fixture %1: at frames 312 and 625 +/-1 (got %2, %3)").arg(mode)
                      .arg(stops[0].frameFrom + 1).arg(stops[1].frameFrom + 1));
            check(stops[0].channelMask == wantMask, QString("stop fixture %1: mask %2 (got %3)").arg(mode).arg(wantMask).arg(stops[0].channelMask));
        }
        QList<TTStreamPoint> points;
        TTAudioAnomalyScanTask task(file, 0, 25.0, QList<int>(), QList<QPair<int,int>>());
        QObject::connect(&task, &TTAudioAnomalyScanTask::pointsDetected,
                         [&points](const QList<TTStreamPoint>& p) { points = p; });
        task.runSynchron();
        check(points.size() == 2, QString("stop fixture %1: task run gives 2 markers (got %2)").arg(mode).arg(points.size()));
        if (points.size() == 2) {
            check(points[0].description().startsWith("Audio anomaly: sound stops abruptly (track 1, drop "),
                  "stop marker text: " + points[0].description());
            check(points[0].hasAudioFrameRange() && points[0].audioFrameTo() - points[0].audioFrameFrom() == 2,
                  "stop marker covers three AC3 frames");
            check(points[0].audioChannelMask() == wantMask, QString("stop marker carries mask %1").arg(wantMask));
            check(points[0].frameIndex() < points[1].frameIndex(), "markers ordered by position");
            check(qAbs(points[0].frameIndex() - 249) <= 2, QString("first stop marker near video frame 249 (got %1)").arg(points[0].frameIndex()));
        }
    }
    // LFE markers carry C+LFE.
    QList<TTStreamPoint> lfePoints;
    TTAudioAnomalyScanTask lfeTask(kShortFile, 0, 25.0, QList<int>(), QList<QPair<int,int>>());
    QObject::connect(&lfeTask, &TTAudioAnomalyScanTask::pointsDetected,
                     [&lfePoints](const QList<TTStreamPoint>& p) { lfePoints = p; });
    lfeTask.runSynchron();
    check(lfePoints.size() == 1 && lfePoints[0].audioChannelMask() == 0x0C, "LFE marker carries mask C+LFE");
}

// ---------------------------------------------------------------------------
// Final review M6: the scan's whole time base is kFrameDurSec = 1536/48000.
// At any other sample rate an AC3 frame is still 1536 samples but no longer
// 32 ms, so every reported position would be off by that ratio - silently.
// collectFrameStats() must refuse such a track (empty result + log line)
// instead of computing positions against the wrong grid.
// ---------------------------------------------------------------------------
// The log line of the suitability gate. QString::arg does not know "%%" as
// an escape - the line used to read "98.8%% null".
static void testUnsuitableMessage()
{
    TTAudioAnomalyScanTask::GateStatus gate;
    gate.materialUnsuitable = true;
    gate.lfeNullPercent = 98.8;
    gate.frames51 = 131219;
    const QString msg = TTAudioAnomalyScanTask::unsuitableMessage(0, gate, 99.0);
    check(msg.contains("track 1:"), "unsuitable message: 1-based track number: " + msg);
    check(msg.contains("(98.8% null over 131219 5.1 frames, need >= 99.0%)"),
          "unsuitable message: percentages with one percent sign: " + msg);
    check(!msg.contains("%%"), "unsuitable message: no doubled percent sign: " + msg);
}

static void testNon48kRefused()
{
    const QString file = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_44100.ac3");
    if (!QFileInfo::exists(file)) {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QProcess proc;
        proc.start(QStringLiteral("ffmpeg"), {
            "-y", "-v", "error", "-f", "lavfi", "-i",
            "aevalsrc=exprs=0.3*sin(2*PI*440*t)|0.3*sin(2*PI*550*t)|0.4*sin(2*PI*330*t)|0|"
            "0.1*sin(2*PI*660*t)|0.1*sin(2*PI*770*t):channel_layout=5.1(side):"
            "sample_rate=44100:duration=3",
            "-c:a", "ac3", "-b:a", "384k", file});
        if (!proc.waitForStarted(5000) || !proc.waitForFinished(60000)) {
            check(false, "44.1 kHz fixture: ffmpeg did not run");
            return;
        }
    }
    if (!QFileInfo::exists(file)) {
        check(false, QString("44.1 kHz fixture: %1 was not created").arg(file));
        return;
    }

    int decodeFailures = 0;
    QVector<FrameStat> stats = TTAudioAnomalyScanTask::collectFrameStats(file, &decodeFailures);
    check(stats.isEmpty(),
          QString("44.1 kHz track is refused, not scanned against the 48 kHz grid "
                  "(got %1 frame stats)").arg(stats.size()));

    // Counter-check: the 48 kHz fixture right next to it IS scanned, so the
    // assertion above cannot pass just because collectFrameStats is broken.
    QVector<FrameStat> ok48 = TTAudioAnomalyScanTask::collectFrameStats(kSampleFile, &decodeFailures);
    check(!ok48.isEmpty(), "counter-check: the 48 kHz fixture is still scanned normally");
}

// ---------------------------------------------------------------------------
// Real-file gate mode.
// ---------------------------------------------------------------------------
static int runRealFileGate(const QString& file, int trackIndex, double frameRate,
                           const QList<double>& dumpSeconds)
{
    printf("Real-file gate: %s (track %d, %.3f fps)\n", qPrintable(file), trackIndex, frameRate);

    int decodeFailures = 0;
    QVector<FrameStat> stats = TTAudioAnomalyScanTask::collectFrameStats(file, &decodeFailures);
    printf("collectFrameStats: %d AC3 frames, %d decode failures\n", int(stats.size()), decodeFailures);
    if (stats.isEmpty()) {
        fprintf(stderr, "FAIL: could not decode %s\n", qPrintable(file));
        return 1;
    }

    TTSettings* cfg = TTSettings::instance();
    QList<Finding> findings = TTAudioAnomalyScanTask::evaluate(
        stats, cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
        cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb());

    printf("evaluate(): %d finding(s) with LFE-RMS=%.1fdB contrast=%.1f null%%=%.1f MinPeak=%.1fdB\n",
           int(findings.size()), cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
           cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb());
    for (const Finding& f : findings) {
        const double startSec = f.frameFrom * (1536.0 / 48000.0);
        const double endSec   = (f.frameTo + 1) * (1536.0 / 48000.0);
        const int videoFrom = TTAudioAnomalyScanTask::videoFrameForTime(startSec, frameRate, {});
        printf("  finding: AC3 frames %lld-%lld (%.3fs-%.3fs), video frame ~%d, "
               "lfePeak=%.1fdB, confidence=%.2f\n",
               (long long)f.frameFrom, (long long)f.frameTo, startSec, endSec,
               videoFrom, f.lfePeak, f.confidence);
    }

    // Machine-readable lines for gate_anomaly_real.sh. STOP: start of the
    // frame that holds the boundary (the middle one of the three reported).
    for (const Finding& f : findings)
        printf("LFE %lld %lld\n", (long long)f.frameFrom, (long long)f.frameTo);
    const QList<Finding> stops = TTAudioAnomalyScanTask::evaluateStops(stats);
    printf("evaluateStops(): %d finding(s)\n", int(stops.size()));
    for (const Finding& f : stops) {
        const qint64 mid = f.frameTo - f.frameFrom == 2 ? f.frameFrom + 1 : f.frameFrom;
        // QString::number, not printf: the harness runs under the user's
        // locale and "%.3f" prints a decimal comma there.
        printf("STOP %s %s %d\n", qPrintable(QString::number(mid * (1536.0 / 48000.0), 'f', 3)),
               qPrintable(QString::number(f.dropDb, 'f', 1)), int(f.channelMask));
    }

    // Levels around given times (arguments 4..): what evaluateStops() sees at
    // each block boundary - for judging a missed or a surplus stop.
    for (double sec : dumpSeconds) {
        const qint64 nb = qint64(stats.size()) * 6;
        const qint64 centre = qint64(sec * 48000.0 / 256.0);
        auto db = [](double p) { return p > 1e-18 ? 10.0 * std::log10(p) : -180.0; };
        printf("LEVELS around %s s (block, start s, planes, block dB, mean of 3 before dB, drop to 3 after dB)\n",
               qPrintable(QString::number(sec, 'f', 3)));
        for (qint64 b = qMax<qint64>(3, centre - 14); b <= centre + 14 && b + 3 <= nb; ++b) {
            double pre = 0.0, post = 0.0;
            for (int i = 1; i <= 3; ++i) pre  += stats[int((b - i) / 6)].blockPower[(b - i) % 6];
            for (int i = 0; i <  3; ++i) post += stats[int((b + i) / 6)].blockPower[(b + i) % 6];
            printf("  %lld %s %d %s %s %s\n", (long long)b,
                   qPrintable(QString::number(b * 256.0 / 48000.0, 'f', 4)),
                   int(stats[int(b / 6)].channels),
                   qPrintable(QString::number(db(stats[int(b / 6)].blockPower[b % 6]), 'f', 1)),
                   qPrintable(QString::number(db(pre / 3.0), 'f', 1)),
                   qPrintable(QString::number(db(pre / 3.0) - db(post / 3.0), 'f', 1)));
        }
    }

    // Also run the full task, for the description text and the exact
    // video-frame marker via the real videoFrameForTime path.
    QList<TTStreamPoint> points;
    TTAudioAnomalyScanTask task(file, trackIndex, frameRate, QList<int>(), QList<QPair<int,int>>());
    QObject::connect(&task, &TTAudioAnomalyScanTask::pointsDetected,
                     [&points](const QList<TTStreamPoint>& p) { points = p; });
    task.runSynchron();
    for (const TTStreamPoint& p : points)
        printf("  marker: frame=%d desc=%s duration=%.3fs confidence=%.2f\n",
               p.frameIndex(), qPrintable(p.description()), p.duration(), p.confidence());

    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc > 1) {
        const QString file = argv[1];
        const int trackIndex = argc > 2 ? atoi(argv[2]) : 0;
        const double frameRate = argc > 3 ? atof(argv[3]) : 25.0;
        QList<double> dumpSeconds;
        for (int i = 4; i < argc; ++i) dumpSeconds.append(QString::fromLatin1(argv[i]).toDouble());
        return runRealFileGate(file, trackIndex, frameRate, dumpSeconds);
    }

    testEvaluate();
    testEvaluateStops();
    testUnsuitableMessage();
    testVideoFrameForTime();
    testSyntheticIntegration();
    testStopIntegration();
    testNon48kRefused();

    if (gFailures == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", gFailures);
    return 1;
}
