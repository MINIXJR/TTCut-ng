// Diagnostic harness for extern/ttaudiorepair.cpp (Task 4 of the
// audio-anomaly-repair plan).
//
// Usage:
//   test_audiorepair
//     Self-test on synthetic material. Builds
//     /usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3 via
//     make_anomaly_sample.sh if it is missing, then builds the replacement
//     table for {track 0, frames 937-975, mask 12 (C+LFE)}, targetAcmod -1,
//     and checks: table size/structure (sync/acmod/CRC), LFE/C silence in
//     the decoded replacement frames, FL/FR closeness to the original
//     decode of the same frames (2-frame warm-up context matching the
//     encoder's own priming, discarded before comparison), and the
//     nonexistent-file error path, plus the acmod-change regression on
//     tools/diag/make_acmod_change_sample.sh's fixture. Exit 0 = ALL PASS,
//     1 = failures, 3 = NOT VERIFIED (a check could not run for lack of
//     material - explicitly not reported as a pass).
//
//   test_audiorepair <ac3> <from> <to> <mask> [out.ac3]
//     Builds the replacement table for an arbitrary file/range/mask
//     (targetAcmod -1) and reports success/failure + table stats. With
//     out.ac3 given, also writes a windowed copy (94-frame context margin
//     on each side, matching the Task 1 calibration spike's measurement
//     protocol) with [from,to] spliced from the replacement table and
//     everything else stream-copied verbatim -- for the real-material seam
//     measurement (Task 4 brief Step 6).
//
//   test_audiorepair --fade <ac3> <frameFrom> <frameTo> <lengthMs> <outPrefix>
//     The fade-out repair on a stop marker's frames: prints where the stop
//     search places it (STOP), the repaired frame range (RANGE) and the
//     largest sample step at the stop before and after (STEP), and writes
//     <outPrefix>-original.ac3 / <outPrefix>-repariert.ac3 (+/-94 frames).
//
// Build via `cmake --build build --target test_audiorepair`.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include "../../extern/ttaudiorepair.h"
#include "../../extern/ttaudiorepairitem.h"

static int gFailures = 0;
// Checks that could not run at all (missing material). Counted separately and
// reported at the end: a skipped check is NOT a passed check. Before the
// final review the acmod-change regression skipped on every machine without
// the corpus recording and the harness still printed "ALL PASS" - the guard
// for a Critical fix was effectively dead. Nothing skips today; this exists
// so the next added skip cannot hide the same way.
static int gSkipped = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

[[maybe_unused]] static void skipped(const QString& what)
{
    printf("SKIP: %s\n", qPrintable(what));
    gSkipped++;
}

// ---- CRC-16, ISO/IEC 11172-3 Annex A (x^16+x^15+x^2+1, 0x8005) ------------
// Bit-serial, MSB-first, no reflection. Same convention as
// tools/ttcut-audiofix/ttcut-audiofix.c (crc16_msb_bits) and ffmpeg's own
// AC3 decoder check (AV_CRC_16_ANSI, libavcodec/ac3dec.c).
static quint16 crc16MsbBits(quint16 crc, const uint8_t* d, size_t nbits)
{
    for (size_t i = 0; i < nbits; i++) {
        int bit = (d[i >> 3] >> (7 - (i & 7))) & 1;
        int outbit = (crc >> 15) & 1;
        crc = (quint16)(crc << 1);
        if (outbit ^ bit) crc ^= 0x8005;
    }
    return crc;
}
static quint16 crc16Msb(quint16 crc, const uint8_t* d, size_t nbytes)
{
    return crc16MsbBits(crc, d, nbytes * 8);
}

// ---- minimal AC3 packet-sequence decoder for verification -----------------
struct DecodedPCM {
    int channels = 0;
    int samplesPerFrame = 0;
    QVector<QVector<float>> ch; // ch[c] = concatenated samples across all decoded frames
};

// noDrc: decode without dynamic range compression, as TTAc3Reencoder does.
static bool decodeAc3Sequence(const QVector<QByteArray>& frames, DecodedPCM& out, QString& err, bool noDrc = false)
{
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
    if (ctx && noDrc) av_opt_set_double(ctx->priv_data, "drc_scale", 0.0, 0);
    if (!ctx || avcodec_open2(ctx, dec, nullptr) < 0) {
        err = QStringLiteral("could not open AC3 decoder");
        if (ctx) avcodec_free_context(&ctx);
        return false;
    }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool ok = true;
    for (const QByteArray& fb : frames) {
        av_packet_unref(pkt);
        pkt->data = reinterpret_cast<uint8_t*>(const_cast<char*>(fb.constData()));
        pkt->size = fb.size();
        if (avcodec_send_packet(ctx, pkt) < 0) {
            ok = false; err = QStringLiteral("send_packet failed"); break;
        }
        if (avcodec_receive_frame(ctx, frame) < 0) {
            ok = false; err = QStringLiteral("receive_frame failed"); break;
        }
        if (out.channels == 0) {
            out.channels = frame->ch_layout.nb_channels;
            out.samplesPerFrame = frame->nb_samples;
            out.ch.resize(out.channels);
        }
        for (int c = 0; c < out.channels; ++c) {
            const float* data = reinterpret_cast<const float*>(frame->data[c]);
            for (int n = 0; n < frame->nb_samples; ++n) out.ch[c].push_back(data[n]);
        }
        av_frame_unref(frame);
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return ok;
}

// Reads every AC3 frame's raw bytes from a file via avformat, indexed by
// sequential frame number (0-based, one AC3 frame per packet).
static bool readAllFrames(const QString& path, QVector<QByteArray>& frames, QString& err)
{
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        err = QStringLiteral("avformat_open_input failed");
        return false;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        err = QStringLiteral("avformat_find_stream_info failed");
        avformat_close_input(&fmtCtx);
        return false;
    }
    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = static_cast<int>(i);
            break;
        }
    }
    if (audioIdx < 0) {
        err = QStringLiteral("no audio stream");
        avformat_close_input(&fmtCtx);
        return false;
    }
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioIdx) {
            frames.append(QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmtCtx);
    return true;
}

// --- self-test on synthetic material ---------------------------------------
static const QString kSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3");
static const QString kMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_anomaly_sample.sh");

// Synthetic acmod-change fixture (final review I1), built on demand like
// kSampleFile above.
static const QString kAcmodSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/acmod_change_sample.ac3");
static const QString kAcmodMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_acmod_change_sample.sh");

static bool buildFixture(const QString& script, const QString& outFile)
{
    if (QFileInfo::exists(outFile)) return true;
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    QProcess proc;
    proc.start(script, {outFile});
    if (!proc.waitForStarted(5000)) return false;
    if (!proc.waitForFinished(180000)) return false;
    return proc.exitCode() == 0 && QFileInfo::exists(outFile);
}

static bool ensureSample()      { return buildFixture(kMakeScript, kSampleFile); }
static bool ensureAcmodSample() { return buildFixture(kAcmodMakeScript, kAcmodSampleFile); }

static void selfTest()
{
    check(ensureSample(), "fixture: anomaly_sample.ac3 available (built if missing)");
    if (!QFileInfo::exists(kSampleFile)) return;

    const qint64 kFrom = 937, kTo = 975;
    const quint8 kMask = 0b001100; // C + LFE
    const int kFL = 0, kFR = 1, kC = 2, kLFE = 3;

    TTAudioRepairItem item(0, kFrom, kTo, kMask);
    QString err;
    TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(kSampleFile, item, -1, &err);

    check(err.isEmpty(), QString("buildRepairTable: no error (got: %1)").arg(err));
    const qint64 expectedCount = kTo - kFrom + 1;
    check(table.size() == expectedCount,
          QString("table has %1 entries (expected %2)").arg(table.size()).arg(expectedCount));
    if (table.isEmpty()) return;

    // --- structural checks per entry ---------------------------------------
    bool allSize1536 = true, allSync = true, allAcmod7 = true, allCrcOk = true;
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        const QByteArray& f = it.value();
        if (f.size() != 1536) allSize1536 = false;
        if (f.size() < 8 || (uint8_t)f[0] != 0x0B || (uint8_t)f[1] != 0x77) allSync = false;
        if (f.size() >= 7) {
            int acmod = (((uint8_t)f[6]) >> 5) & 0x07;
            if (acmod != 7) allAcmod7 = false;
        } else {
            allAcmod7 = false;
        }
        if (f.size() >= 4) {
            quint16 crc = crc16Msb(0, reinterpret_cast<const uint8_t*>(f.constData()) + 2,
                                    f.size() - 2);
            if (crc != 0) allCrcOk = false;
        } else {
            allCrcOk = false;
        }
    }
    check(allSize1536, "every replacement frame is 1536 bytes (384 kbit/s @ 48 kHz)");
    check(allSync, "every replacement frame starts with sync 0B 77");
    check(allAcmod7, "every replacement frame has acmod == 7");
    check(allCrcOk, "every replacement frame's CRC (poly 0x8005) is 0");

    // --- read original frames (need warm-up context for a fair decode) -----
    QVector<QByteArray> allOriginal;
    QString readErr;
    check(readAllFrames(kSampleFile, allOriginal, readErr),
          QString("read original frames for comparison (err: %1)").arg(readErr));
    if (allOriginal.size() <= kTo) return;

    const qint64 warmupStart = kFrom >= 2 ? kFrom - 2 : 0;
    const int warmupCount = static_cast<int>(kFrom - warmupStart);

    QVector<QByteArray> origSeq;   // warm-up + original content frames
    QVector<QByteArray> repSeq;    // warm-up (same original bytes) + replacement frames
    for (qint64 f = warmupStart; f < kFrom; ++f) {
        origSeq.append(allOriginal[f]);
        repSeq.append(allOriginal[f]);
    }
    for (qint64 f = kFrom; f <= kTo; ++f) {
        origSeq.append(allOriginal[f]);
        repSeq.append(table.value(f));
    }

    DecodedPCM origPcm, repPcm;
    QString decErr;
    check(decodeAc3Sequence(origSeq, origPcm, decErr),
          QString("decode original warm-up+content sequence (err: %1)").arg(decErr));
    check(decodeAc3Sequence(repSeq, repPcm, decErr),
          QString("decode replacement warm-up+content sequence (err: %1)").arg(decErr));
    if (origPcm.channels < 6 || repPcm.channels < 6) {
        check(false, "decoded PCM has 6 channels (5.1)");
        return;
    }

    const int discard = warmupCount * origPcm.samplesPerFrame;
    const int nSamplesContent = static_cast<int>(expectedCount) * origPcm.samplesPerFrame;

    // LFE must be silent (masked, no fade content in this fixture's window).
    const int fadeLen = std::min(240, nSamplesContent); // 5 ms @ 48 kHz, matches implementation
    double lfeMaxAbs = 0.0;
    for (int n = 0; n < nSamplesContent; ++n) {
        float lfe = repPcm.ch[kLFE][discard + n];
        lfeMaxAbs = std::max(lfeMaxAbs, (double)std::abs(lfe));
    }
    check(lfeMaxAbs < 1e-6, QString("LFE silent across repair range (max |x| = %1)").arg(lfeMaxAbs));

    // C must be silent outside the fade windows -- but the naive
    // "everything past sample fadeLen" cut does NOT measure true silence:
    // until 2026-10-02 the replacement audio sat 256 samples late (the
    // AC3 encoder's delay, since compensated by TTAc3Reencoder), and the
    // decoder's block overlap still spreads an edge over neighbouring
    // samples (measured/confirmed in code review: the previous
    // "mid" check at n>=240 was actually still sampling the fade's own
    // decoded tail, landing at decoded samples 256-495; the region is
    // genuinely quiet only from about sample 512 onward, and clean from
    // frame 1 onward). Excluding a decoderSmear margin on BOTH sides of
    // the muted middle (symmetric, since the same block-overlap effect
    // can affect the fade-out edge too) isolates the part of the range
    // that is unambiguously "after the transition has fully decayed".
    const int decoderSmear = 256; // AC3 MDCT block-overlap smear, ~1 transform block
    const int trueMidStart = fadeLen + decoderSmear;
    const int trueMidEnd = nSamplesContent - fadeLen - decoderSmear; // exclusive
    double cMidMaxAbs = 0.0;
    bool haveMidSamples = trueMidStart < trueMidEnd;
    for (int n = trueMidStart; n < trueMidEnd; ++n) {
        float c = repPcm.ch[kC][discard + n];
        cMidMaxAbs = std::max(cMidMaxAbs, (double)std::abs(c));
    }
    check(haveMidSamples, "repair range wide enough to isolate a true (post-smear) mid region");
    check(!haveMidSamples || cMidMaxAbs < 0.01,
          QString("C silent in the true mid region (excl. %1-sample decoder smear past each fade; max |x| = %2)")
              .arg(decoderSmear).arg(cMidMaxAbs));

    // FL/FR vs original decode of the same frames (untouched channels).
    // Point-wise peak diff is NOT a valid fidelity metric here: this
    // fixture's FL/FR content is isolated sine tones, and an independent
    // AC3 encode/decode cycle measurably rotates a sine's phase even with
    // NO masking applied anywhere in the file (verified: an unmasked
    // identity round-trip of this same material shows peak diffs of
    // 0.534/FL, 0.338/FR -- essentially the same magnitude as the masked
    // repair case, i.e. the peak diff is dominated by phase, not by
    // muting-induced bit starvation). A phase-shifted sine of the same
    // frequency/amplitude has the SAME RMS and peak level as the
    // original; genuine signal loss or corruption changes the level
    // measurably. Level (RMS + peak amplitude) comparison is therefore
    // phase-robust where point-wise diff is not, and still catches the
    // failure mode point-wise diff was blind to (a fully erased channel:
    // amplitude 0.3 -> diff 0.30, which the old < 0.6 bound let through).
    double sumSqOrig[2] = {0.0, 0.0}, sumSqRep[2] = {0.0, 0.0};
    double peakOrig[2] = {0.0, 0.0}, peakRep[2] = {0.0, 0.0};
    int chSlot = 0;
    for (int ch : {kFL, kFR}) {
        for (int n = 0; n < nSamplesContent; ++n) {
            double o = origPcm.ch[ch][discard + n];
            double r = repPcm.ch[ch][discard + n];
            sumSqOrig[chSlot] += o * o;
            sumSqRep[chSlot] += r * r;
            peakOrig[chSlot] = std::max(peakOrig[chSlot], std::abs(o));
            peakRep[chSlot] = std::max(peakRep[chSlot], std::abs(r));
        }
        ++chSlot;
    }
    bool levelOk = true;
    QStringList levelDetails;
    for (int i = 0; i < 2; ++i) {
        double rmsOrig = std::sqrt(sumSqOrig[i] / nSamplesContent);
        double rmsRep = std::sqrt(sumSqRep[i] / nSamplesContent);
        double relRms = rmsOrig > 1e-9 ? std::abs(rmsOrig - rmsRep) / rmsOrig : std::abs(rmsRep);
        double relPeak = peakOrig[i] > 1e-9 ? std::abs(peakOrig[i] - peakRep[i]) / peakOrig[i]
                                             : std::abs(peakRep[i]);
        // RMS is the primary, tight discriminator (measured here: 0.05-0.06%,
        // i.e. two orders of magnitude below the 10% bound) -- it alone
        // already catches erasure/corruption (an erased channel drives
        // rmsRep to 0, relRms to 100%). Peak is a single-sample statistic
        // and naturally noisier even on an intact signal (measured here:
        // up to ~18%, a single-sample quantization/ringing overshoot near
        // the fixture's own transient, not a fidelity problem -- RMS for
        // the same channel stayed at 0.06%); its bound is loosened to 30%
        // so it still catches gross corruption without false-failing on
        // that natural variance.
        if (relRms >= 0.1 || relPeak >= 0.3) levelOk = false;
        levelDetails << QString("%1: rmsOrig=%2 rmsRep=%3 relRms=%4 peakOrig=%5 peakRep=%6 relPeak=%7")
                            .arg(i == 0 ? "FL" : "FR")
                            .arg(rmsOrig).arg(rmsRep).arg(relRms).arg(peakOrig[i]).arg(peakRep[i]).arg(relPeak);
    }
    check(levelOk, QString("FL/FR level (RMS+peak) close to original, phase-robust: %1")
                       .arg(levelDetails.join("; ")));

    // --- error path: nonexistent file -------------------------------------
    QString errMsg;
    TTAudioRepair::FrameTable emptyTable = TTAudioRepair::buildRepairTable(
        QStringLiteral("/nonexistent/path/does_not_exist.ac3"), item, -1, &errMsg);
    check(emptyTable.isEmpty(), "nonexistent file: table is empty");
    check(!errMsg.isEmpty(), "nonexistent file: errorOut is non-empty");

    // --- error path: range past the end of the file (final review M3) ------
    // Must be reported as what it is - a stale range against a shorter file -
    // and NOT as the "implementation bug" the count-mismatch branch reports.
    {
        QVector<QByteArray> allFrames;
        QString readErr;
        if (readAllFrames(kSampleFile, allFrames, readErr)) {
            const qint64 last = allFrames.size() - 1;
            TTAudioRepairItem beyond(0, last - 2, last + 20, kMask);
            QString eofErr;
            TTAudioRepair::FrameTable eofTable =
                TTAudioRepair::buildRepairTable(kSampleFile, beyond, -1, &eofErr);
            check(eofTable.isEmpty(), "range past EOF: table is empty");
            check(eofErr.contains("past the end", Qt::CaseInsensitive),
                  QString("range past EOF: error says the range reaches past the file's end "
                          "(got: %1)").arg(eofErr));
            check(!eofErr.contains("implementation bug", Qt::CaseInsensitive),
                  QString("range past EOF: error does NOT claim an implementation bug "
                          "(got: %1)").arg(eofErr));
        } else {
            check(false, QString("range past EOF: could not read the fixture (%1)").arg(readErr));
        }
    }
}

// --- C1 regression: a repair range that spans a source channel-mode
// (acmod) change must abort with an error, never silently up/downmix the
// changed frames against the range's first-frame layout.
//
// The case was established on the real 02x06 corpus recording, which
// switches acmod 7/lfeon (5.1) -> acmod 2 (2.0 stereo) at frame 64057 (ad
// break). That file is not in the repository and no longer exists on this
// machine, so the test silently SKIPped while the harness still printed
// ALL PASS - i.e. the Task-4 Critical had no live regression guard at all
// (final review I1). It now runs on a synthetic fixture built by
// tools/diag/make_acmod_change_sample.sh (5.1 section + 2.0 section, both
// 384 kbit/s so the FRAME SIZE stays constant across the transition and
// the channel-mode check is what fires, not the CBR check).
//
// The transition frame is not hardcoded: it is read out of the fixture by
// scanning the acmod field of each frame header, so a re-built fixture with
// a different section length keeps working.
static void testAcmodChangeRejected()
{
    if (!ensureAcmodSample()) {
        check(false, QString("fixture: %1 could not be built (see %2)")
                         .arg(kAcmodSampleFile, kAcmodMakeScript));
        return;
    }

    QVector<QByteArray> frames;
    QString readErr;
    if (!readAllFrames(kAcmodSampleFile, frames, readErr)) {
        check(false, QString("acmod fixture: frames readable (%1)").arg(readErr));
        return;
    }
    check(frames.size() > 10, QString("acmod fixture: %1 frames read").arg(frames.size()));
    if (frames.size() <= 10) return;

    // acmod lives in the 3 most significant bits of byte 6 of an AC3 sync
    // frame (syncword 2, crc1 2, fscod+frmsizecod 1, bsid+bsmod 1).
    auto acmodOf = [](const QByteArray& f) -> int {
        if (f.size() < 7) return -1;
        return (static_cast<uint8_t>(f[6]) >> 5) & 0x07;
    };
    const int firstAcmod = acmodOf(frames[0]);
    qint64 transition = -1;
    bool sizeConstant = true;
    for (int i = 1; i < frames.size(); ++i) {
        if (frames[i].size() != frames[0].size()) sizeConstant = false;
        if (transition < 0 && acmodOf(frames[i]) != firstAcmod) transition = i;
    }
    check(sizeConstant,
          QString("acmod fixture: every frame is %1 bytes (CBR check must not fire first)")
              .arg(frames[0].size()));
    check(transition > 2,
          QString("acmod fixture: channel-mode transition found at frame %1").arg(transition));
    if (transition <= 2 || !sizeConstant) return;

    const qint64 kFrom = transition - 3, kTo = transition + 3;
    const quint8 kMask = 0b001100; // C + LFE, valid for the range's first (5.1) frame

    TTAudioRepairItem item(0, kFrom, kTo, kMask);
    QString err;
    TTAudioRepair::FrameTable table =
        TTAudioRepair::buildRepairTable(kAcmodSampleFile, item, -1, &err);

    check(table.isEmpty(), "acmod change in range: table is empty");
    check(!err.isEmpty(), QString("acmod change in range: errorOut is non-empty (got: %1)").arg(err));
    check(err.contains("channel-mode change", Qt::CaseInsensitive),
          QString("acmod change in range: error names the channel-mode change (got: %1)").arg(err));
    check(err.contains(QString::number(transition)),
          QString("acmod change in range: error names the exact transition frame %1 (got: %2)")
              .arg(transition).arg(err));

    // Counter-check: the SAME file, a range entirely inside the 5.1 section,
    // must build fine - otherwise the assertion above would also pass on a
    // buildRepairTable that simply rejects this fixture wholesale.
    TTAudioRepairItem inside(0, 10, 16, kMask);
    QString insideErr;
    TTAudioRepair::FrameTable insideTable =
        TTAudioRepair::buildRepairTable(kAcmodSampleFile, inside, -1, &insideErr);
    check(insideErr.isEmpty(),
          QString("acmod fixture: a range inside the 5.1 section still builds (got: %1)").arg(insideErr));
    check(insideTable.size() == 7,
          QString("acmod fixture: that range yields 7 replacement frames (got %1)").arg(insideTable.size()));
}

// --- argument-driven mode: build a table for an arbitrary file/range/mask,
// optionally write a windowed splice copy for the real-material measurement.
// audio-repair.md H8: the replacement frames were encoded at the stream's
// bit rate - the FIRST frame's - so in a file that switches bit rate every
// repair outside the first frame's block failed ("encoded replacement frame
// size mismatch"). tux_test.ac3 alternates stereo at 192 kbit/s (768-byte
// frames: 0-938, 1878-2972) and 5.1 at 384 kbit/s (1536-byte frames:
// 939-1877, 2973-3754). A repair in each block must build, with frames of
// that block's own size.
static void testMixedBitRate()
{
    const QString file = QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3");
    if (!QFileInfo::exists(file)) {
        check(false, "mixed bit rate: tools/testdata/tux_test.ac3 present");
        return;
    }
    struct Case { qint64 from, to; quint8 mask; int frameBytes; const char* what; };
    const Case cases[] = {
        { 625,  661, 0x01,  768, "stereo block (192 kbit/s), left" },
        {1563, 1599, 0x0C, 1536, "5.1 block (384 kbit/s), C+LFE" },
        {3000, 3030, 0x0C, 1536, "second 5.1 block, C+LFE" },
    };
    for (const Case& c : cases) {
        QString err;
        const TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(
            file, TTAudioRepairItem(0, c.from, c.to, c.mask), -1, &err);
        bool sizesOk = !table.isEmpty();
        for (const QByteArray& frame : table) sizesOk = sizesOk && frame.size() == c.frameBytes;
        check(err.isEmpty() && table.size() == c.to - c.from + 1 && sizesOk,
              QString("mixed bit rate: %1, frames %2-%3 -> %4 frames of %5 bytes (error: %6)")
                  .arg(c.what).arg(c.from).arg(c.to).arg(table.size()).arg(c.frameBytes)
                  .arg(err.isEmpty() ? QStringLiteral("-") : err));
    }
}

static const QString kStopSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/stop_sample_5.1.ac3");
static const QString kStopStereoFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/stop_sample_stereo.ac3");
static const QString kStopMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_stop_sample.sh");

static bool ensureStopSample(const QString& file, const QString& mode)
{
    if (QFileInfo::exists(file)) return true;
    QProcess proc;
    proc.start(kStopMakeScript, {file, mode});
    return proc.waitForStarted(5000) && proc.waitForFinished(180000) && proc.exitCode() == 0;
}

// The gain curve of the fade-out repair, as a function of the track sample.
static void testFadeOutGain()
{
    using TTAudioRepair::fadeOutGain;
    const qint64 E = 10000; const int L = 960, S = 48, I = 240;
    check(fadeOutGain(E - L - 1, E, L, S, I) == 1.0 && fadeOutGain(E - L, E, L, S, I) == 1.0, "gain: 1 up to the start of the fade");
    check(std::fabs(fadeOutGain(E - L / 2, E, L, S, I) - 0.5) < 1e-9, "gain: 0.5 in the middle of the fade");
    check(fadeOutGain(E - 1, E, L, S, I) < 1e-5, "gain: next to 0 at the last sample of the fade");
    check(fadeOutGain(E, E, L, S, I) == 0.0 && fadeOutGain(E + S - 1, E, L, S, I) == 0.0, "gain: 0 through the silence");
    check(fadeOutGain(E + S, E, L, S, I) == 0.0, "gain: the fade-in starts at 0");
    check(std::fabs(fadeOutGain(E + S + I / 2, E, L, S, I) - 0.5) < 1e-9, "gain: 0.5 in the middle of the fade-in");
    check(fadeOutGain(E + S + I, E, L, S, I) == 1.0, "gain: 1 again behind the fade-in");
    bool mono = true;
    for (qint64 p = E - L; p < E; ++p)         mono = mono && fadeOutGain(p + 1, E, L, S, I) <= fadeOutGain(p, E, L, S, I);
    for (qint64 p = E + S; p < E + S + I; ++p) mono = mono && fadeOutGain(p + 1, E, L, S, I) >= fadeOutGain(p, E, L, S, I);
    check(mono, "gain: monotonic in both fades");

    // the frame range of an item: every frame the curve touches
    const TTAudioRepairItem a = TTAudioRepair::makeFadeOutItem(2, 0x3F, 46536704, 960, 522, 48000);
    check(a.isFadeOut() && a.trackIndex() == 2 && a.channelMask() == 0x3F && a.fadeEnd() == 46536704
              && a.fadeLength() == 960 && a.silenceLength() == 522,
          "makeFadeOutItem: method and values");
    check(a.frameFrom() == 30296 && a.frameTo() == 30297,
          QString("makeFadeOutItem: frames 30296-30297 (got %1-%2)").arg(a.frameFrom()).arg(a.frameTo()));
    check(TTAudioRepair::makeFadeOutItem(0, 0x3F, 500, 960, 48, 48000).frameFrom() < 0,
          "makeFadeOutItem: a fade before the start of the track gives no valid range");
}

// The stop search on plain sample data (no codec): two planes, sin and cos,
// so that the magnitude never passes through zero.
static QVector<QVector<float>> tone(int n, double hz, double amp = 0.3)
{
    QVector<QVector<float>> p(2, QVector<float>(n));
    for (int i = 0; i < n; ++i) {
        p[0][i] = float(amp * std::sin(2 * M_PI * hz * i / 48000.0));
        p[1][i] = float(amp * std::cos(2 * M_PI * hz * i / 48000.0));
    }
    return p;
}
static void testLocateStop()
{
    using TTAudioRepair::locateStop;
    const qint64 first = 100000; const int n = 1536 * 6;
    const qint64 from = first + 1536, to = first + 1536 * 4;
    {   // L1: hard cut at local sample 4000
        auto p = tone(n, 100.0);
        for (int c = 0; c < 2; ++c) for (int i = 4000; i < n; ++i) p[c][i] = 0.0f;
        const auto r = locateStop(p, first, from, to, 48000);
        check(r.found && r.soundEnd == first + 4000 && r.fadeEnd == first + 4000 && r.silence == 48,
              QString("L1 hard cut: found to the sample (found %1, soundEnd %2, fadeEnd %3, silence %4)")
                  .arg(r.found).arg(r.soundEnd - first).arg(r.fadeEnd - first).arg(r.silence));
    }
    {   // L2: a dropout of ten samples 400 samples before the cut: its first jump ends the fade-out
        auto p = tone(n, 100.0);
        for (int c = 0; c < 2; ++c) {
            for (int i = 3600; i < 3610; ++i) p[c][i] = 0.0f;
            for (int i = 4000; i < n; ++i) p[c][i] = 0.0f;
        }
        const auto r = locateStop(p, first, from, to, 48000);
        check(r.found && r.soundEnd == first + 4000 && r.fadeEnd == first + 3600 && r.silence == 448,
              QString("L2 jump before the cut: fadeEnd at the jump (soundEnd %1, fadeEnd %2, silence %3)")
                  .arg(r.soundEnd - first).arg(r.fadeEnd - first).arg(r.silence));
    }
    {   // L3: a steady tone
        check(!locateStop(tone(n, 100.0), first, from, to, 48000).found, "L3 steady tone: nothing found");
    }
    {   // L4: natural decay, 60 dB over 100 ms
        auto p = tone(n, 100.0);
        for (int c = 0; c < 2; ++c) for (int i = 3000; i < n; ++i)
            p[c][i] *= float(std::pow(10.0, -3.0 * qMin(1.0, (i - 3000) / 4800.0)));
        check(!locateStop(p, first, from, to, 48000).found, "L4 natural decay: nothing found");
    }
    {   // L5: nothing to look at
        check(!locateStop(QVector<QVector<float>>(), first, from, to, 48000).found, "L5 no planes: nothing found");
        check(!locateStop(tone(600, 100.0), first, first, first + 600, 48000).found, "L5 too short: nothing found");
    }
}

// Stop search and fade-out build on the decoded fixtures.
static void testFadeOutBuild()
{
    for (const QString& mode : {QStringLiteral("5.1"), QStringLiteral("stereo")}) {
        const QString file = mode == "5.1" ? kStopSampleFile : kStopStereoFile;
        const int nCh = mode == "5.1" ? 6 : 2;
        check(ensureStopSample(file, mode), "fixture stop_sample " + mode + " available");
        if (!QFileInfo::exists(file)) continue;

        QString err;
        const TTAudioRepair::StopPlacement sp = TTAudioRepair::findStop(file, 312, 314, &err);
        check(err.isEmpty() && sp.found, QString("%1: stop found in frames 312-314 (%2)").arg(mode, err));
        // Hard cut at sample 480144; the AC3 codec smears it over one block
        // (measured with ffmpeg: the decoded sound ends at 480401), no jump.
        check(sp.soundEnd >= 480144 && sp.soundEnd <= 480144 + 320,
              QString("%1: sound ends within 320 samples behind the cut (got %2)").arg(mode).arg(sp.soundEnd));
        check(sp.fadeEnd == sp.soundEnd && sp.silence == 48,
              QString("%1: no jump - the fade-out ends where the sound ends, silence 1 ms (fadeEnd %2, silence %3)")
                  .arg(mode).arg(sp.fadeEnd).arg(sp.silence));
        check(!TTAudioRepair::findStop(file, 937, 939, &err).found, mode + ": natural decay (frames 937-939): nothing found");
        // Review focus 1: the last frames of the track (silent, nothing behind them)
        QVector<QByteArray> src;
        check(readAllFrames(file, src, err), mode + ": source frames read");
        const qint64 lastFrame = src.size() - 1;
        check(!TTAudioRepair::findStop(file, lastFrame - 2, lastFrame, &err).found,
              mode + ": last three frames of the track: nothing found, no failure");
        if (!sp.found || src.isEmpty()) continue;

        const int len = 960;
        const TTAudioRepairItem item = TTAudioRepair::makeFadeOutItem(0, quint8((1u << nCh) - 1), sp.fadeEnd, len, sp.silence, 48000);
        const TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(file, item, -1, &err);
        check(err.isEmpty() && table.size() == item.frameTo() - item.frameFrom() + 1,
              QString("%1: fade-out table built for frames %2-%3 (%4)").arg(mode).arg(item.frameFrom()).arg(item.frameTo()).arg(err));
        if (!err.isEmpty()) continue;

        const qint64 w0 = item.frameFrom() - 4, w1 = item.frameTo() + 2;
        QVector<QByteArray> a, b;
        for (qint64 f = w0; f <= w1; ++f) { a << src[f]; b << (table.contains(f) ? table.value(f) : src[f]); }
        DecodedPCM pa, pb;
        check(decodeAc3Sequence(a, pa, err) && decodeAc3Sequence(b, pb, err), mode + ": source and repaired decoded");
        auto idx = [&](qint64 pos) { return pos - w0 * 1536; };
        double maxBefore = 0.0, maxSilence = 0.0, rmsA = 0.0, rmsB = 0.0;
        for (int c = 0; c < pa.channels && c < pb.channels; ++c) {
            for (qint64 p = sp.fadeEnd - len - 600; p < sp.fadeEnd - len; ++p)
                maxBefore = qMax(maxBefore, double(std::fabs(pa.ch[c][idx(p)] - pb.ch[c][idx(p)])));
            for (qint64 p = sp.fadeEnd; p < sp.fadeEnd + sp.silence; ++p)
                maxSilence = qMax(maxSilence, double(std::fabs(pb.ch[c][idx(p)])));
            for (qint64 p = sp.fadeEnd - len / 2 - 48; p < sp.fadeEnd - len / 2 + 48; ++p) {
                rmsA += double(pa.ch[c][idx(p)]) * pa.ch[c][idx(p)];
                rmsB += double(pb.ch[c][idx(p)]) * pb.ch[c][idx(p)];
            }
        }
        check(maxBefore < 0.02, QString("%1: unchanged before the fade (largest difference %2)").arg(mode).arg(maxBefore));
        check(maxSilence < 0.01, QString("%1: silent from the end of the fade-out (largest sample %2)").arg(mode).arg(maxSilence));
        const double ratio = rmsA > 0 ? std::sqrt(rmsB / rmsA) : -1.0;
        check(ratio > 0.35 && ratio < 0.65, QString("%1: half level in the middle of the fade (ratio %2)").arg(mode).arg(ratio));
    }

    if (!QFileInfo::exists(kStopSampleFile)) return;
    QString err;
    TTAudioRepair::buildRepairTable(kStopSampleFile, TTAudioRepair::makeFadeOutItem(0, 0x3F, 500, 960, 48, 48000), -1, &err);
    check(err.contains("before the beginning"), "fade before the start of the track is refused: " + err);
    TTAudioRepairItem wrong(0, 312, 313, 0x3F);
    wrong.setFadeOut(480401, 960, 48);                       // needs 312-312
    TTAudioRepair::buildRepairTable(kStopSampleFile, wrong, -1, &err);
    check(err.contains("do not fit"), "fade values that do not fit the frame range are refused: " + err);
    TTAudioRepair::buildRepairTable(kStopSampleFile, TTAudioRepairItem(0, 312, 312, 0x3F, QStringLiteral("nonsense")), -1, &err);
    check(err.contains("unknown repair method"), "an unknown method is refused: " + err);
}

static int argMode(int argc, char** argv)
{
    const QString ac3Path = QString::fromUtf8(argv[1]);
    bool okFrom = false, okTo = false, okMask = false;
    const qint64 from = QString::fromUtf8(argv[2]).toLongLong(&okFrom);
    const qint64 to = QString::fromUtf8(argv[3]).toLongLong(&okTo);
    const int mask = QString::fromUtf8(argv[4]).toInt(&okMask);
    if (!okFrom || !okTo || !okMask) {
        fprintf(stderr, "invalid <from>/<to>/<mask> arguments\n");
        return 2;
    }
    const QString outPath = argc >= 6 ? QString::fromUtf8(argv[5]) : QString();

    printf("Building repair table for %s frames %lld-%lld mask %d ...\n",
           qPrintable(ac3Path), (long long)from, (long long)to, mask);

    TTAudioRepairItem item(0, from, to, static_cast<quint8>(mask));
    QString err;
    TTAudioRepair::FrameTable table =
        TTAudioRepair::buildRepairTable(ac3Path, item, -1, &err);

    if (!err.isEmpty()) {
        printf("FAIL: buildRepairTable error: %s\n", qPrintable(err));
        return 1;
    }
    const qint64 expectedCount = to - from + 1;
    printf("table entries: %lld (expected %lld)\n", (long long)table.size(), (long long)expectedCount);
    qint64 minSize = -1, maxSize = -1;
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        qint64 sz = it.value().size();
        if (minSize < 0 || sz < minSize) minSize = sz;
        if (sz > maxSize) maxSize = sz;
    }
    printf("frame byte size range: %lld..%lld\n", (long long)minSize, (long long)maxSize);

    if (table.size() != expectedCount) {
        printf("FAIL: entry count mismatch\n");
        return 1;
    }
    printf("OK: table built successfully\n");

    if (!outPath.isEmpty()) {
        // Windowed splice copy: 94-frame context margin on each side,
        // matching the Task 1 calibration spike's measurement protocol
        // (repair_prototype.py: context_frames_measure=94), so the real-
        // material seam measurement can reuse that exact method.
        const qint64 margin = 94;
        QVector<QByteArray> allFrames;
        QString readErr;
        if (!readAllFrames(ac3Path, allFrames, readErr)) {
            printf("FAIL: could not read source frames for window copy: %s\n", qPrintable(readErr));
            return 1;
        }
        const qint64 winStart = std::max<qint64>(0, from - margin);
        const qint64 winEnd = std::min<qint64>(allFrames.size() - 1, to + margin);

        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            printf("FAIL: could not open %s for writing\n", qPrintable(outPath));
            return 1;
        }
        for (qint64 f = winStart; f <= winEnd; ++f) {
            const QByteArray& bytes = (f >= from && f <= to) ? table.value(f) : allFrames[f];
            out.write(bytes);
        }
        out.close();
        printf("window copy written: %s (frames %lld-%lld, splice %lld-%lld)\n",
               qPrintable(outPath), (long long)winStart, (long long)winEnd,
               (long long)from, (long long)to);
    }
    return 0;
}

// test_audiorepair --fade <ac3> <frameFrom> <frameTo> <lengthMs> <outPrefix>
// The fade-out repair on a stop marker's frames of a real recording: where the
// search places it, the largest step at the stop before and after, and both
// versions as AC3 (+/-94 frames) to listen to.
static int fadeMode(char** argv)
{
    const QString ac3 = QString::fromUtf8(argv[2]);
    const qint64 from = QString::fromUtf8(argv[3]).toLongLong(), to = QString::fromUtf8(argv[4]).toLongLong();
    const int lenMs = QString::fromUtf8(argv[5]).toInt();
    const QString prefix = QString::fromUtf8(argv[6]);
    QString err;
    const TTAudioRepair::StopPlacement sp = TTAudioRepair::findStop(ac3, from, to, &err);
    printf("STOP found=%d fadeEnd=%lld soundEnd=%lld silence=%d %s\n", int(sp.found),
           (long long)sp.fadeEnd, (long long)sp.soundEnd, sp.silence, qPrintable(err));
    if (!sp.found) return 1;
    const TTAudioRepairItem item = TTAudioRepair::makeFadeOutItem(0, 0x3F, sp.fadeEnd, lenMs * 48, sp.silence, 48000);
    const TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(ac3, item, -1, &err);
    if (!err.isEmpty()) { printf("FAIL: %s\n", qPrintable(err)); return 1; }
    printf("RANGE %lld %lld\n", (long long)item.frameFrom(), (long long)item.frameTo());

    QVector<QByteArray> all;
    if (!readAllFrames(ac3, all, err)) { printf("FAIL: %s\n", qPrintable(err)); return 1; }
    const qint64 w0 = qMax<qint64>(0, item.frameFrom() - 94), w1 = qMin<qint64>(all.size() - 1, item.frameTo() + 94);
    QVector<QByteArray> a, b;
    for (qint64 f = w0; f <= w1; ++f) { a << all[f]; b << (table.contains(f) ? table.value(f) : all[f]); }
    auto write = [](const QString& name, const QVector<QByteArray>& frames) {
        QFile o(name);
        if (!o.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        for (const QByteArray& x : frames) o.write(x);
        return true;
    };
    if (!write(prefix + "-original.ac3", a) || !write(prefix + "-repariert.ac3", b)) {
        printf("FAIL: could not write %s-*.ac3\n", qPrintable(prefix));
        return 1;
    }
    DecodedPCM pa, pb;
    if (!decodeAc3Sequence(a, pa, err, true) || !decodeAc3Sequence(b, pb, err, true)) {
        printf("FAIL: %s\n", qPrintable(err));
        return 1;
    }
    auto maxStep = [&](const DecodedPCM& p) {
        float m = 0.0f;
        for (int c = 0; c < p.channels; ++c) {
            if (p.channels == 6 && c == 3) continue;
            for (qint64 pos = sp.fadeEnd - 48; pos <= sp.soundEnd + 48; ++pos) {
                const qint64 i = pos - w0 * 1536;
                m = qMax(m, std::fabs(p.ch[c][i] - p.ch[c][i - 1]));
            }
        }
        return m;
    };
    printf("STEP original=%s repaired=%s\n", qPrintable(QString::number(maxStep(pa), 'f', 4)),
           qPrintable(QString::number(maxStep(pb), 'f', 4)));
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 1) {
        selfTest();
        testAcmodChangeRejected();
        testMixedBitRate();
        testFadeOutGain();
        testLocateStop();
        testFadeOutBuild();
        if (gFailures > 0) {
            printf("\nFAILED (%d failures, %d skipped)\n", gFailures, gSkipped);
            return 1;
        }
        if (gSkipped > 0) {
            // Deliberately NOT "ALL PASS": part of the suite did not run, so
            // the suite proves less than it looks like it does. Distinct exit
            // code so a caller can tell "broken" from "not fully checked".
            printf("\nNOT VERIFIED: %d check(s) skipped, 0 failures - "
                   "material missing, the suite is incomplete\n", gSkipped);
            return 3;
        }
        printf("\nALL PASS (0 failures)\n");
        return 0;
    }
    if (argc == 7 && QString::fromUtf8(argv[1]) == "--fade") return fadeMode(argv);
    if (argc == 5 || argc == 6) {
        return argMode(argc, argv);
    }
    fprintf(stderr, "usage: %s [<ac3> <from> <to> <mask> [out.ac3]]\n"
                    "       %s --fade <ac3> <frameFrom> <frameTo> <lengthMs> <outPrefix>\n", argv[0], argv[0]);
    return 2;
}
