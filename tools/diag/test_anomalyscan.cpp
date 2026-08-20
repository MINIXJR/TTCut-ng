// Diagnostic harness for TTAudioAnomalyScanTask (Task 6 of the
// audio-anomaly-repair plan).
//
// Usage:
//   test_anomalyscan
//     Self-test: pure evaluate()/videoFrameForTime() unit cases (synthetic,
//     no file I/O), then an integration run on
//     /usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3 (built via
//     make_anomaly_sample.sh if missing) - both the raw collectFrameStats()
//     + evaluate() combo (exact AC3 frame range) and a full task run via
//     runSynchron() (video-frame marker + description). Prints
//     PASS/FAIL per check and "ALL PASS"/"FAILED" at the end.
//
//   test_anomalyscan <ac3-file> [trackIndex] [frameRate]
//     Real-file gate: runs the full scan (calibrated default settings) on
//     the given AC3 file and prints every finding (AC3 frame range, video
//     frame, LFE peak, confidence, description). Exit 0.
//
// Build via `cmake --build build --target test_anomalyscan`.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QString>

#include <cstdio>
#include <cstdlib>

#include "data/ttaudioanomalyscantask.h"
#include "common/ttthreadtask.h"
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

    // A) Positive finding: island 3000-3037 (38 frames, above -55dB gate,
    // loud enough for the -22dB MinPeak gate), with 4x center contrast vs.
    // background 0.1 everywhere else in 10000 frames. Fine segmentation
    // narrows the reported range to the actual burst frames (here: the
    // whole island, since it is uniformly loud throughout - a deliberate
    // synthetic edge case, see anomaly_sample.ac3 below for the same
    // shape), with a +/-1 frame safety margin.
    {
        auto stats = buildStats(10000, 3000, 3037, -20.0f, 0.4f);
        auto findings = TTAudioAnomalyScanTask::evaluate(stats, kLfeRmsDb, kContrast,
                                                          kNullPercent, kMinPeakDb);
        check(findings.size() == 1,
              QString("A) uniform loud+contrast island -> exactly 1 finding (got %1)")
                  .arg(findings.size()));
        if (findings.size() == 1) {
            check(findings[0].frameFrom == 2999 && findings[0].frameTo == 3038,
                  QString("A) fine-segmented range == 2999-3038 (island +/-1 margin, got %1-%2)")
                      .arg(findings[0].frameFrom).arg(findings[0].frameTo));
            check(findings[0].lfePeak >= float(kMinPeakDb),
                  QString("A) lfePeak %1 dB >= MinPeak %2 dB")
                      .arg(findings[0].lfePeak).arg(kMinPeakDb));
        }
    }

    // B) Kontrast-Bedingung: LFE-active island but no center contrast
    // (0.1 in the island too, same as background) -> not confirmed, no
    // finding regardless of how loud the LFE is. This is the "material
    // fine, nothing found" gate case: the null% precondition passes
    // (island is only 38 of 10000 frames), so GateStatus must say so.
    {
        auto stats = buildStats(10000, 3000, 3037, -20.0f, 0.1f);
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
        auto stats = buildStats(10000, 3000, 3037, -30.0f, 0.4f);
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
    // LFE onset (2997-3010 contrast, LFE active only 3000-3010) - spec:
    // the reported range start is allowed to precede the LFE onset.
    {
        QVector<FrameStat> stats(10000);
        for (int i = 0; i < 10000; ++i) {
            const bool lfeActive = i >= 3000 && i <= 3010;
            const bool contrastActive = i >= 2997 && i <= 3010;
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
            // (block == raw island 3000-3010) still reports
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
}

// ---------------------------------------------------------------------------
// Integration: anomaly_sample.ac3 (synthetic, Task 4's fixture - a
// continuous 1.2s loud burst, so fine segmentation collapses onto ~the
// whole island, unlike the real corpus case).
// ---------------------------------------------------------------------------
static const QString kSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3");
static const QString kMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_anomaly_sample.sh");

static bool ensureSample()
{
    if (QFileInfo::exists(kSampleFile)) return true;
    QDir().mkpath(QFileInfo(kSampleFile).absolutePath());
    QProcess proc;
    proc.start(kMakeScript, {kSampleFile});
    if (!proc.waitForStarted(5000)) return false;
    if (!proc.waitForFinished(120000)) return false;
    return proc.exitCode() == 0 && QFileInfo::exists(kSampleFile);
}

static void testSyntheticIntegration()
{
    check(ensureSample(), "fixture: anomaly_sample.ac3 available (built if missing)");
    if (!QFileInfo::exists(kSampleFile)) return;

    // (a) collectFrameStats() + evaluate() directly, for an exact AC3-frame
    // range assertion (the full task only exposes the video-frame mapping).
    int decodeFailures = 0;
    QVector<FrameStat> stats = TTAudioAnomalyScanTask::collectFrameStats(kSampleFile, &decodeFailures);
    check(!stats.isEmpty(), QString("collectFrameStats: decoded %1 AC3 frames").arg(stats.size()));
    check(decodeFailures == 0, QString("collectFrameStats: 0 decode failures (got %1)").arg(decodeFailures));

    TTSettings* cfg = TTSettings::instance();
    QList<Finding> findings = TTAudioAnomalyScanTask::evaluate(
        stats, cfg->anomalyLfeRmsDb(), cfg->anomalyCenterContrast(),
        cfg->anomalyLfeNullPercent(), cfg->anomalyLfeMinPeakDb());

    check(findings.size() == 1,
          QString("anomaly_sample.ac3: exactly 1 finding (got %1)").arg(findings.size()));
    if (findings.size() == 1) {
        const Finding& f = findings[0];
        printf("  synthetic finding: AC3 frames %lld-%lld, lfePeak=%.1fdB, confidence=%.2f\n",
               (long long)f.frameFrom, (long long)f.frameTo, f.lfePeak, f.confidence);
        // Continuous 1.2s burst (t=30..31.2s == AC3 frames 937.5..975): the
        // fine segmentation is expected to land close to the whole island
        // here, not the narrow real-corpus case.
        check(qAbs(f.frameFrom - 937) <= 2,
              QString("anomaly_sample.ac3: frameFrom %1 == 937 +/-2").arg(f.frameFrom));
        check(qAbs(f.frameTo - 975) <= 2,
              QString("anomaly_sample.ac3: frameTo %1 == 975 +/-2").arg(f.frameTo));
    }

    // (b) Full task run via runSynchron() - the actual production path,
    // checking the video-frame marker and description text.
    QList<TTStreamPoint> points;
    TTAudioAnomalyScanTask task(kSampleFile, 0, 25.0, QList<int>(), QList<QPair<int,int>>());
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
// Final review M6: the scan's whole time base is kFrameDurSec = 1536/48000.
// At any other sample rate an AC3 frame is still 1536 samples but no longer
// 32 ms, so every reported position would be off by that ratio - silently.
// collectFrameStats() must refuse such a track (empty result + log line)
// instead of computing positions against the wrong grid.
// ---------------------------------------------------------------------------
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
static int runRealFileGate(const QString& file, int trackIndex, double frameRate)
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
        return runRealFileGate(file, trackIndex, frameRate);
    }

    testEvaluate();
    testVideoFrameForTime();
    testSyntheticIntegration();
    testNon48kRefused();

    if (gFailures == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", gFailures);
    return 1;
}
