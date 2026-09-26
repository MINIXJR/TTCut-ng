// Model-only test for TTAudioRepairDialog (audio-anomaly-repair Task 7):
// instantiates the dialog offscreen against a real AVItem+AudioAnomaly
// marker and checks the prefill/accept() logic a human eye/ear cannot be
// substituted for at this layer (the actual audition/before-after-listen
// step is manual, see task-7-brief.md Step 5 - this only exercises the
// data path).
//
// Fixture: tools/testdata/tux_test.264 (25 fps - ffprobe's r_frame_rate
// shows 50/1 for the raw H.264 ES, but TTAVData/TTVideoStream::frameRate()
// resolves the actual 25 fps, matching the documented "raw H.264 ES ->
// r_frame_rate 2x real" quirk) + tux_test.ac3 (48 kHz AC3, same fixture
// test_audiorepair_persist.cpp already uses). The AC3 alternates stereo
// (192 kbit/s) and 5.1 (384 kbit/s): frames 0-938 stereo, 939-1877 5.1,
// 1878-2972 stereo, 2973-3754 5.1. Cases 1 and 2 sit at marker frame 1250
// (50 s, inside the first 5.1 block), where the default C+LFE repair can be
// built; the brief's original 750 (30 s) straddles the 938/939 change, and a
// repair there is refused since accept() builds it once (audio-repair.md
// H3) - Case 6 checks exactly that.
//
//   usage: test_repairdialog_model
//
// Build via `cmake --build build --target test_repairdialog_model`.
#include <QApplication>
#include <QTimer>
#include <QMessageBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QCheckBox>
#include <QProcess>
#include <QSpinBox>
#include <QtGlobal>

#include <cstdio>

#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttaudioanomalyscantask.h"
#include "data/ttstreampoint.h"
#include "extern/ttaudiorepairitem.h"
#include "avstream/ttavstream.h"
#include "gui/ttaudiorepairdialog.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static const QString kVideoFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264");
static const QString kAudioFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3");
// Same synthetic 5.1 fixture (and same builder) test_audiorepair and
// test_anomalyscan use - it is the only material here that actually contains
// an anomaly for the scanner to find.
static const QString kAnomalySample =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3");
static const QString kMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_anomaly_sample.sh");

static bool ensureAnomalySample()
{
    if (QFileInfo::exists(kAnomalySample)) return true;
    QDir().mkpath(QFileInfo(kAnomalySample).absolutePath());
    QProcess proc;
    proc.start(kMakeScript, {kAnomalySample});
    if (!proc.waitForStarted(5000)) return false;
    if (!proc.waitForFinished(180000)) return false;
    return proc.exitCode() == 0 && QFileInfo::exists(kAnomalySample);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // accept() warns in a modal box when it refuses a repair: close every box
    // and keep its text, so an unexpected refusal fails a check instead of
    // hanging the harness.
    QStringList boxes;
    QTimer boxDriver;
    QObject::connect(&boxDriver, &QTimer::timeout, [&]() {
        if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            boxes << mb->text();
            mb->done(QMessageBox::Ok);
        }
    });
    boxDriver.start(50);

    if (!QFileInfo::exists(kVideoFile) || !QFileInfo::exists(kAudioFile)) {
        fprintf(stderr, "missing fixture(s): %s / %s\n",
                qPrintable(kVideoFile), qPrintable(kAudioFile));
        return 2;
    }

    TTAVData avData;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&avData, &TTAVData::threadPoolExit, [&]() {
        done = true;
        loop.quit();
    });
    avData.openAVStreams(kVideoFile);
    if (!done) loop.exec();

    check(avData.avCount() == 1, "one AV item opened");
    if (avData.avCount() != 1) { printf("\nFAILED (fixture could not be opened)\n"); return 1; }

    TTAVItem* item = avData.avItemAt(0);
    check(item->audioCount() == 1, "one audio track opened");
    check(item->videoStream() != nullptr, "video stream present");
    if (item->audioCount() != 1 || !item->videoStream()) { printf("\nFAILED\n"); return 1; }

    const double fps = item->videoStream()->frameRate();
    check(fps > 0.0, QString("frame rate is positive (got %1)").arg(fps));

    // --- Case 1: prefill from a fresh AudioAnomaly marker (no existing repair) ---
    {
        const TTStreamPoint point(1250, StreamPointType::AudioAnomaly,
            QStringLiteral("Audio anomaly: C+LFE burst (track 1, LFE peak -19.7 dB)"),
            0.8f, 1.2f);

        TTAudioRepairDialog dlg(item, point, /*trackIndex=*/0, QList<int>(), nullptr);

        const int startMs = dlg.startSpinBoxForTest()->value();
        const int endMs = dlg.endSpinBoxForTest()->value();
        check(startMs == 50000, QString("prefill start = 50000 ms (got %1)").arg(startMs));
        check(endMs == 51200, QString("prefill end = 51200 ms (got %1)").arg(endMs));

        // channelMask bits: 0=FL 1=FR 2=C 3=LFE 4=SL 5=SR - default C+LFE
        static const bool expected[6] = { false, false, true, true, false, false };
        bool channelsOk = true;
        for (int ch = 0; ch < 6; ++ch) {
            if (dlg.channelCheckBoxForTest(ch)->isChecked() != expected[ch]) channelsOk = false;
        }
        check(channelsOk, "prefill channels = C+LFE only");

        // --- accept() with the ORIGINAL prefill values ---
        check(item->audioRepairList().isEmpty(), "no repair item before accept()");
        dlg.accept();
        QList<TTAudioRepairItem> repairs = item->audioRepairList();
        check(repairs.size() == 1, QString("exactly one repair item after accept() (got %1)").arg(repairs.size()));
        // ms/32 per the brief's ms->frame contract (48 kHz AC3 -> 32 ms/frame),
        // rounded to nearest - matches TTAudioRepairDialog::currentFrameFrom/To
        // (qRound(value/frameDurationMs)), not truncating integer division:
        // 50000/32 = 1562.5, which rounds to 1563.
        // The End spin box is the range's EXCLUSIVE end time while
        // TTAudioRepairItem::frameTo() is INCLUSIVE, hence the -1 (final
        // review I3): 51200 ms is the start of frame 1600, so the last
        // repaired frame is 1599.
        const qint64 expFrameFrom = qint64(qRound(50000.0 / 32.0));
        const qint64 expFrameTo   = qint64(qRound(51200.0 / 32.0)) - 1;
        if (repairs.size() == 1) {
            const TTAudioRepairItem& r = repairs.first();
            check(r.trackIndex() == 0, "repair trackIndex == 0");
            check(r.frameFrom() == expFrameFrom, QString("repair frameFrom == round(50000/32) = %1 (got %2)").arg(expFrameFrom).arg(r.frameFrom()));
            check(r.frameTo() == expFrameTo, QString("repair frameTo == round(51200/32)-1 = %1 (got %2)").arg(expFrameTo).arg(r.frameTo()));
            check(r.channelMask() == 0x0C, QString("repair channelMask == 0x0C (C+LFE) (got %1)").arg(r.channelMask()));
        }
    }

    // --- Case 2: changing the spinboxes before accept() changes the stored item ---
    {
        const TTStreamPoint point(1250, StreamPointType::AudioAnomaly,
            QStringLiteral("Audio anomaly: C+LFE burst (track 1, LFE peak -19.7 dB)"),
            0.8f, 1.2f);

        // The dialog must find Case 1's stored item (frameFrom 1563, AC3
        // 32 ms/frame -> 30016 ms) and edit it, not propose a fresh
        // marker-based default (which would show 50000 ms again).
        const int expectedEditStartMs = 1563 * 32;
        TTAudioRepairDialog dlg(item, point, /*trackIndex=*/0, QList<int>(), nullptr);
        const int prefillStart = dlg.startSpinBoxForTest()->value();
        check(prefillStart == expectedEditStartMs,
              QString("edit-mode prefill shows the existing item's range (%1 ms, got %2)")
                  .arg(expectedEditStartMs).arg(prefillStart));

        dlg.startSpinBoxForTest()->setValue(52000);
        dlg.endSpinBoxForTest()->setValue(52320);
        dlg.channelCheckBoxForTest(3)->setChecked(false); // uncheck LFE

        dlg.accept();
        QList<TTAudioRepairItem> repairs = item->audioRepairList();
        check(repairs.size() == 1, QString("still exactly one repair item (edit, not duplicate) (got %1)").arg(repairs.size()));
        if (repairs.size() == 1) {
            const TTAudioRepairItem& r = repairs.first();
            check(r.frameFrom() == 52000 / 32, QString("changed repair frameFrom == 52000/32 (got %1)").arg(r.frameFrom()));
            check(r.frameTo() == 52320 / 32 - 1, QString("changed repair frameTo == 52320/32-1 (got %1)").arg(r.frameTo()));
            check(r.channelMask() == 0x04, QString("changed repair channelMask == 0x04 (C only) (got %1)").arg(r.channelMask()));
        }
    }

    // --- Case 3: reject() must not touch the AVItem ---
    {
        item->clearAudioRepairs();
        const TTStreamPoint point(750, StreamPointType::AudioAnomaly,
            QStringLiteral("Audio anomaly: C+LFE burst (track 1, LFE peak -19.7 dB)"),
            0.8f, 1.2f);
        TTAudioRepairDialog dlg(item, point, /*trackIndex=*/0, QList<int>(), nullptr);
        dlg.startSpinBoxForTest()->setValue(1000);
        dlg.reject();
        check(item->audioRepairList().isEmpty(), "reject() leaves the AVItem's repair list untouched");
    }

    // --- Case 4 (review fix 1): extras-corrected ms prefill for a NEW
    // repair. Marker at video frame 1000 with 100 synthetic extra-frame
    // indices (0..99, all below 1000) must prefill using
    // (1000 - 100)/25 = 36.0s, NOT the naive 1000/25 = 40.0s a plain
    // frameIndex/frameRate computation would give - the same seconds-scale
    // gap the review flagged on real MPEG-2 material (273 extras / ~10.9s
    // on the corpus's "Benders" example).
    {
        item->clearAudioRepairs();
        QList<int> extras;
        for (int i = 0; i < 100; ++i) extras.append(i); // all < marker frame 1000

        const TTStreamPoint point(1000, StreamPointType::AudioAnomaly,
            QStringLiteral("Audio anomaly: C+LFE burst (track 1, LFE peak -19.7 dB)"),
            0.8f, 0.5f);

        TTAudioRepairDialog dlg(item, point, /*trackIndex=*/0, extras, nullptr);

        const int startMs = dlg.startSpinBoxForTest()->value();
        const int endMs = dlg.endSpinBoxForTest()->value();
        const int naiveStartMs = qRound(1000.0 / fps * 1000.0); // what it would be WITHOUT the fix
        check(startMs != naiveStartMs,
              QString("extras-corrected prefill differs from the naive (uncorrected) value (naive %1, got %2)")
                  .arg(naiveStartMs).arg(startMs));
        check(startMs == 36000, QString("extras-corrected prefill start = (1000-100)/25*1000 = 36000 ms (got %1)").arg(startMs));
        check(endMs == 36500, QString("extras-corrected prefill end = 36000 + 500 = 36500 ms (got %1)").arg(endMs));
    }

    // --- Case 5 (final review I3): full round trip
    // scanner finding -> TTStreamPoint -> dialog prefill -> TTAudioRepairItem.
    //
    // The scanner works in AC3 frames, the marker used to carry only a video
    // frame + a duration, and the dialog converted that back - three
    // quantizations deep (video-frame rounding at 40 ms against the 32 ms
    // audio grid, end-exclusive duration against an inclusive frameTo, and
    // the ms<->frame rounding in the dialog itself), which cost up to one
    // AC3 frame at each end. TTStreamPoint now carries the finding's own
    // range through; this case proves the round trip is exact, and the
    // second half proves the estimate-only fallback (older project files,
    // hand-placed markers) still lands within +/-1 frame.
    {
        item->clearAudioRepairs();

        if (!ensureAnomalySample()) {
            printf("\nFAILED (round trip: fixture %s could not be built)\n",
                   qPrintable(kAnomalySample));
            return 1;
        }

        QList<TTStreamPoint> points;
        TTAudioAnomalyScanTask task(kAnomalySample, /*trackIndex=*/0, /*fps=*/25.0,
                                    QList<int>(), QList<QPair<int,int>>());
        QObject::connect(&task, &TTAudioAnomalyScanTask::pointsDetected,
                         [&points](const QList<TTStreamPoint>& p) { points = p; });
        task.runSynchron();

        check(!points.isEmpty(),
              QString("round trip: the scan found at least one anomaly in %1 (got %2)")
                  .arg(kAnomalySample).arg(points.size()));
        if (points.isEmpty()) {
            printf("\nFAILED (round trip: no finding to carry through)\n");
            return 1;
        }

        // The finding's own AC3 frame numbers, taken from the pure
        // evaluate() path - the marker must carry exactly these.
        int decodeFailures = 0;
        const QVector<TTAudioAnomalyScanTask::FrameStat> stats =
            TTAudioAnomalyScanTask::collectFrameStats(kAnomalySample, &decodeFailures);
        TTSettings* cfg = TTSettings::instance();
        const QList<TTAudioAnomalyScanTask::Finding> findings =
            TTAudioAnomalyScanTask::evaluate(stats, cfg->anomalyLfeRmsDb(),
                                             cfg->anomalyCenterContrast(),
                                             cfg->anomalyLfeNullPercent(),
                                             cfg->anomalyLfeMinPeakDb());
        check(findings.size() == points.size(),
              QString("round trip: one marker per finding (%1 findings, %2 markers)")
                  .arg(findings.size()).arg(points.size()));
        if (findings.isEmpty()) {
            printf("\nFAILED (round trip: evaluate() produced no finding)\n");
            return 1;
        }

        const TTAudioAnomalyScanTask::Finding& f = findings.first();
        const TTStreamPoint& pt = points.first();
        check(pt.hasAudioFrameRange(), "round trip: marker carries an exact AC3 frame range");
        check(pt.audioFrameFrom() == f.frameFrom && pt.audioFrameTo() == f.frameTo,
              QString("round trip: marker range == finding range (%1-%2, got %3-%4)")
                  .arg(f.frameFrom).arg(f.frameTo).arg(pt.audioFrameFrom()).arg(pt.audioFrameTo()));

        // The finding's range belongs to the anomaly sample, so the dialog has
        // to work on that file: tux_test.ac3 switches layout inside this range
        // and accept() now refuses it (audio-repair.md H3).
        TTAudioType    sampleType(kAnomalySample);
        TTAudioStream* sampleStream = sampleType.createAudioStream();
        check(sampleStream != nullptr, "round trip: anomaly sample opened as an audio stream");
        if (!sampleStream) { printf("\nFAILED\n"); return 1; }
        sampleStream->createHeaderList();
        TTAVItem* sampleItem = new TTAVItem(nullptr);
        sampleItem->appendAudioEntry(sampleStream);

        // Marker -> dialog -> accept(): no tolerance, this must be exact.
        {
            TTAudioRepairDialog dlg(sampleItem, pt, /*trackIndex=*/0, QList<int>(), nullptr);
            const int startMs = dlg.startSpinBoxForTest()->value();
            const int endMs = dlg.endSpinBoxForTest()->value();
            check(startMs == qRound(f.frameFrom * 32.0),
                  QString("round trip: prefill start = frameFrom*32 = %1 ms (got %2)")
                      .arg(qRound(f.frameFrom * 32.0)).arg(startMs));
            check(endMs == qRound((f.frameTo + 1) * 32.0),
                  QString("round trip: prefill end = (frameTo+1)*32 = %1 ms (got %2)")
                      .arg(qRound((f.frameTo + 1) * 32.0)).arg(endMs));

            dlg.accept();
            const QList<TTAudioRepairItem> repairs = sampleItem->audioRepairList();
            check(repairs.size() == 1,
                  QString("round trip: one repair item written (got %1)").arg(repairs.size()));
            if (repairs.size() == 1) {
                const TTAudioRepairItem& r = repairs.first();
                check(r.frameFrom() == f.frameFrom && r.frameTo() == f.frameTo,
                      QString("round trip: repair item range == finding range exactly "
                              "(%1-%2, got %3-%4)")
                          .arg(f.frameFrom).arg(f.frameTo).arg(r.frameFrom()).arg(r.frameTo()));
            }
        }

        // Same marker with the exact range stripped (what an older project
        // file restores): the estimate path must still land within +/-1 AC3
        // frame of the finding.
        {
            sampleItem->clearAudioRepairs();
            const TTStreamPoint legacy(pt.frameIndex(), pt.type(), pt.description(),
                                       pt.confidence(), pt.duration());
            check(!legacy.hasAudioFrameRange(), "round trip: legacy marker has no exact range");

            TTAudioRepairDialog dlg(sampleItem, legacy, /*trackIndex=*/0, QList<int>(), nullptr);
            dlg.accept();
            const QList<TTAudioRepairItem> repairs = sampleItem->audioRepairList();
            check(repairs.size() == 1,
                  QString("round trip (legacy): one repair item written (got %1)").arg(repairs.size()));
            if (repairs.size() == 1) {
                const TTAudioRepairItem& r = repairs.first();
                const qint64 dFrom = qAbs(r.frameFrom() - f.frameFrom);
                const qint64 dTo   = qAbs(r.frameTo() - f.frameTo);
                check(dFrom <= 1 && dTo <= 1,
                      QString("round trip (legacy estimate): within +/-1 frame of the finding "
                              "(finding %1-%2, got %3-%4, delta %5/%6)")
                          .arg(f.frameFrom).arg(f.frameTo).arg(r.frameFrom()).arg(r.frameTo())
                          .arg(dFrom).arg(dTo));
            }
        }
        sampleItem->clearAudioRepairs();
    }

    // --- Case 6 (audio-repair.md H3): accept() builds the repair once and
    // refuses what the cut would refuse, with a warning, leaving the AVItem
    // untouched. Before, each of these was stored and failed only the cut.
    {
        item->clearAudioRepairs();
        auto only = [](TTAudioRepairDialog& dlg, quint8 mask) {
            for (int ch = 0; ch < 6; ++ch) dlg.channelCheckBoxForTest(ch)->setChecked(mask & (1u << ch));
        };
        auto refused = [&](TTAudioRepairDialog& dlg, const QString& reason, const QString& what) {
            boxes.clear();
            dlg.accept();
            check(item->audioRepairList().isEmpty() && boxes.size() == 1 && boxes.first().contains(reason),
                  QString("refused: %1 (%2 item(s), box: %3)")
                      .arg(what).arg(item->audioRepairList().size()).arg(boxes.join(" | ")));
        };

        // marker 500 = 20 s, frames 625-661: stereo block
        const TTStreamPoint stereo(500, StreamPointType::AudioAnomaly, QStringLiteral("stereo"), 0.8f, 1.2f);
        TTAudioRepairDialog dlgStereo(item, stereo, /*trackIndex=*/0, QList<int>(), nullptr);
        refused(dlgStereo, "beyond the stream", "C+LFE (the default) on a stereo block");
        only(dlgStereo, 0);
        refused(dlgStereo, "at least one channel", "no channel selected");
        only(dlgStereo, 0x01);
        boxes.clear();
        dlgStereo.accept();
        check(item->audioRepairList().size() == 1 && boxes.isEmpty(),
              QString("accepted: the left channel on the stereo block (%1 item(s), box: %2)")
                  .arg(item->audioRepairList().size()).arg(boxes.join(" | ")));
        item->clearAudioRepairs();

        // marker 750 = 30 s, frames 938-974: across the stereo -> 5.1 change at 939
        const TTStreamPoint across(750, StreamPointType::AudioAnomaly, QStringLiteral("across"), 0.8f, 1.2f);
        TTAudioRepairDialog dlgAcross(item, across, /*trackIndex=*/0, QList<int>(), nullptr);
        only(dlgAcross, 0x01);
        refused(dlgAcross, "frame size changed", "the left channel across the 938/939 layout change");
    }

    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
