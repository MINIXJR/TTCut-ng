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

static bool decodeAc3Sequence(const QVector<QByteArray>& frames, DecodedPCM& out, QString& err)
{
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
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
    // the AC3 decoder's block-overlap reconstruction smears the fade-in
    // edge roughly 256 samples further into the frame than the fade
    // window itself (measured/confirmed in code review: the previous
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

int main(int argc, char** argv)
{
    if (argc == 1) {
        selfTest();
        testAcmodChangeRejected();
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
    if (argc == 5 || argc == 6) {
        return argMode(argc, argv);
    }
    fprintf(stderr, "usage: %s [<ac3> <from> <to> <mask> [out.ac3]]\n", argv[0]);
    return 2;
}
