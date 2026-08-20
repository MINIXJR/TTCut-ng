// Diagnostic harness for the Task 5 audio-anomaly-repair cut-path
// integration: TTFFmpegWrapper::cutAudioStream's repairTable lookup.
//
// Usage: test_audiorepair_cut
//   Self-test on synthetic material. Builds
//   /usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3 via
//   make_anomaly_sample.sh if it is missing, then:
//     1. Cuts segment 10.0-50.0s WITHOUT a repair table -> reference output A.
//     2. Cuts the same segment WITH a table for {track 0, frames 937-975,
//        mask 12 (C+LFE)} -> output B.
//     3. Checks: A and B are the same size; every output frame outside the
//        repair range is byte-identical between A and B (the source-frame
//        offset of output frame 0 is determined by byte-matching against the
//        source on the fixture's own frame-byte grid, NOT assumed as a
//        constant -- the cutAudioStream skip rule puts the segment's first
//        frame at source frame 313, not 312, for a 10.0s boundary); frames
//        inside the range decode to LFE ~ 0 in B; B as a whole is
//        structurally CRC-clean (every frame: sync 0B77, the source's own
//        CBR frame size read from the material, CRC residue 0) and carries
//        the same total frame count as A (no drop/duplicate).
//     4. Error/no-op path: a table whose item frames (5-6) fall outside every
//        keep window is built and passed in; the lookup never matches inside
//        the loop, so the output must be byte-identical to A (whole-file MD5
//        compare).
//     5. Segment-boundary-span path (Fix-Runde 1): a repair item whose frame
//        range straddles the boundary between two keep segments must abort
//        the TRACK via TTAVData::cutAudioTracks -- the target acmod is a
//        per-call scalar and cannot represent two different segment targets
//        for one item, so cutAudioTracks must never silently build a table
//        against the wrong one. Checked at the cutAudioTracks level (not
//        cutAudioStream in isolation), since the segment lookup lives there.
//   Prints "ALL PASS"/"FAILED" and exits 0/1 accordingly.
//
// Build via `cmake --build build --target test_audiorepair_cut`.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QVector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include "../../extern/ttffmpegwrapper.h"
#include "../../extern/ttaudiorepair.h"
#include "../../data/ttaudiorepairitem.h"
#include "../../avstream/ttavheader.h"
#include "../../avstream/ttavstream.h"
#include "../../avstream/ttavtypes.h"
#include "../../data/ttavdata.h"
#include "../../data/ttavlist.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

// ---- CRC-16, ISO/IEC 11172-3 Annex A (x^16+x^15+x^2+1, 0x8005) ------------
// Same convention as test_audiorepair.cpp / tools/ttcut-audiofix / ffmpeg's
// own AC3 decoder check.
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

// Reads every AC3 frame's raw bytes from a file via avformat, indexed by
// sequential frame number (0-based, one AC3 frame per packet). Mirrors
// test_audiorepair.cpp's helper of the same name.
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

// Minimal AC3 packet-sequence decoder, LFE-only extraction (this harness
// only needs to check the repaired range is silent on LFE).
static bool decodeLfe(const QVector<QByteArray>& frames, QVector<float>& lfeOut,
                       int& samplesPerFrame, QString& err)
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
    const int kLfeIndex = 3; // FL FR C LFE SL SR (ffmpeg 5.1(side) order)
    for (const QByteArray& fb : frames) {
        av_packet_unref(pkt);
        pkt->data = reinterpret_cast<uint8_t*>(const_cast<char*>(fb.constData()));
        pkt->size = fb.size();
        if (avcodec_send_packet(ctx, pkt) < 0) { ok = false; err = "send_packet failed"; break; }
        if (avcodec_receive_frame(ctx, frame) < 0) { ok = false; err = "receive_frame failed"; break; }
        if (frame->ch_layout.nb_channels < 6) { ok = false; err = "not 5.1"; av_frame_unref(frame); break; }
        samplesPerFrame = frame->nb_samples;
        const float* lfe = reinterpret_cast<const float*>(frame->data[kLfeIndex]);
        for (int n = 0; n < frame->nb_samples; ++n) lfeOut.push_back(lfe[n]);
        av_frame_unref(frame);
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return ok;
}

static const QString kSampleFile =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/anomaly_sample.ac3");
static const QString kMakeScript =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_anomaly_sample.sh");
static const QString kOutA =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/test_audiorepair_cut_A.ac3");
static const QString kOutB =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/test_audiorepair_cut_B.ac3");
static const QString kOutC =
    QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/test_audiorepair_cut_C.ac3");

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

static QByteArray fileMd5(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return h.result();
}

// --- Fix-Runde 1, Important: a repair item whose frame range straddles the
// boundary between two keep segments must abort the track, never silently
// build its table against just one segment's targetAcmod. Item frames
// 937-975 -> time [29.984, 31.232); the two video-domain keep segments are
// split at 30.4s, well inside that range (>=0.15s margin on both sides --
// safely more than one audio frame (32 ms) away from either edge, so
// planAudioCut's frame-boundary snapping cannot pull the split back outside
// the item's range). Exercised at the TTAVData::cutAudioTracks level (not
// cutAudioStream in isolation), since the segment lookup lives there.
static void testSegmentBoundarySpan()
{
    if (!QFileInfo::exists(kSampleFile)) {
        check(false, "boundary-span test: fixture missing");
        return;
    }

    TTAudioType    aType(kSampleFile);
    TTAudioStream* aStream = aType.createAudioStream();
    if (!aStream) {
        check(false, "boundary-span test: could not create audio stream");
        return;
    }
    aStream->createHeaderList();

    TTAVItem* avItem = new TTAVItem(nullptr);
    avItem->appendAudioEntry(aStream);

    const quint8 kMask = 0b001100; // C + LFE
    TTAudioRepairItem item(0, 937, 975, kMask);
    avItem->appendAudioRepair(item);
    check(avItem->audioRepairList().size() == 1, "boundary-span test: repair item attached");

    const QList<QPair<double, double>> videoKeepList = {
        qMakePair(10.0, 30.4), qMakePair(30.4, 50.0)
    };

    const QString outFile = QDir(QFileInfo(kSampleFile).absolutePath())
                                 .absoluteFilePath("test_audiorepair_cut_span.ac3");
    QFile::remove(outFile);

    TTAVData avData;
    bool sawOnCut = false;
    bool cutOk = true;
    avData.cutAudioTracks(avItem, {0}, videoKeepList, false,
        [&](int, const QString&) { return outFile; },
        [&](int, const QString&, const QString&, bool ok) {
            sawOnCut = true;
            cutOk = ok;
        });

    check(sawOnCut, "boundary-span test: onCut was invoked");
    check(!cutOk, "boundary-span test: track reports ok==false (segment-span error caught)");
    check(!QFileInfo::exists(outFile), "boundary-span test: no output file was written");

    // Final review M14: the actionable reason must be retrievable by the
    // caller in user-facing wording, not only findable in the log file - that
    // is what the partial-failure dialog now shows.
    const QStringList reasons = avData.audioCutFailureReasons();
    check(reasons.size() == 1,
          QString("boundary-span test: exactly one user-facing failure reason (got %1)")
              .arg(reasons.size()));
    if (!reasons.isEmpty()) {
        check(reasons.first().contains("cut-segment boundary", Qt::CaseInsensitive),
              QString("boundary-span test: the reason names the cut-segment boundary "
                      "(got: %1)").arg(reasons.first()));
        check(reasons.first().contains("937") && reasons.first().contains("975"),
              QString("boundary-span test: the reason names the offending range 937-975 "
                      "(got: %1)").arg(reasons.first()));
    }
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    check(ensureSample(), "fixture: anomaly_sample.ac3 available (built if missing)");
    if (!QFileInfo::exists(kSampleFile)) {
        printf("\nFAILED (%d failures)\n", gFailures);
        return 1;
    }

    QList<QPair<double, double>> keep;
    keep.append(qMakePair(10.0, 50.0));

    const qint64 kFrom = 937, kTo = 975;
    const quint8 kMask = 0b001100; // C + LFE

    // --- Step 1: reference cut A (no repair table) --------------------------
    QFile::remove(kOutA);
    TTFFmpegWrapper ffA;
    bool okA = ffA.cutAudioStream(kSampleFile, kOutA, keep);
    check(okA, "reference cut A (no repair table) succeeded");
    if (!okA) { printf("\nFAILED (%d failures)\n", gFailures); return 1; }

    // --- Step 2: repair table + cut B ---------------------------------------
    TTAudioRepairItem item(0, kFrom, kTo, kMask);
    QString buildErr;
    TTAudioRepair::FrameTable table =
        TTAudioRepair::buildRepairTable(kSampleFile, item, -1, &buildErr);
    check(buildErr.isEmpty(), QString("buildRepairTable: no error (got: %1)").arg(buildErr));
    check(table.size() == (kTo - kFrom + 1), "repair table has the expected entry count");

    QFile::remove(kOutB);
    TTFFmpegWrapper ffB;
    bool okB = ffB.cutAudioStream(kSampleFile, kOutB, keep, false, QList<int>(),
                                   nullptr, {}, &table);
    check(okB, "repaired cut B (with repair table) succeeded");
    if (!okA || !okB) { printf("\nFAILED (%d failures)\n", gFailures); return 1; }

    // --- Step 3: A/B comparison ----------------------------------------------
    qint64 sizeA = QFileInfo(kOutA).size();
    qint64 sizeB = QFileInfo(kOutB).size();
    check(sizeA == sizeB, QString("A and B have equal size (A=%1 B=%2)").arg(sizeA).arg(sizeB));

    QVector<QByteArray> framesA, framesB, framesSrc;
    QString rdErr;
    check(readAllFrames(kOutA, framesA, rdErr), QString("read A frames (err: %1)").arg(rdErr));
    check(readAllFrames(kOutB, framesB, rdErr), QString("read B frames (err: %1)").arg(rdErr));
    check(readAllFrames(kSampleFile, framesSrc, rdErr), QString("read source frames (err: %1)").arg(rdErr));
    if (framesA.isEmpty() || framesB.isEmpty() || framesSrc.isEmpty()) {
        printf("\nFAILED (%d failures)\n", gFailures);
        return 1;
    }
    check(framesA.size() == framesB.size(),
          QString("A and B have equal frame count (A=%1 B=%2)").arg(framesA.size()).arg(framesB.size()));

    // Determine the source-frame offset of output frame 0 by byte-matching
    // against the source on the frame grid -- do NOT assume a constant
    // (the cutAudioStream skip rule for a 10.0s boundary lands on source
    // frame 313, not 312: frame 312 covers [9.984, 10.016), and its start
    // 9.984 < startTime - 0.001 = 9.999 is skipped).
    qint64 offset = -1;
    for (qint64 cand = 300; cand <= 320 && cand < framesSrc.size(); ++cand) {
        if (framesSrc[cand] == framesA[0]) { offset = cand; break; }
    }
    check(offset >= 0, QString("determined output-frame-0 source offset by byte match (got %1)").arg(offset));
    if (offset < 0) { printf("\nFAILED (%d failures)\n", gFailures); return 1; }
    printf("INFO: output frame 0 == source frame %lld (offset)\n", (long long)offset);

    const qint64 outFrom = kFrom - offset;
    const qint64 outTo = kTo - offset;
    printf("INFO: repair range in output coordinates: %lld..%lld\n", (long long)outFrom, (long long)outTo);

    bool outsideIdentical = true;
    qint64 firstMismatch = -1;
    for (qint64 j = 0; j < framesA.size(); ++j) {
        if (j >= outFrom && j <= outTo) continue; // inside repair range, expected to differ
        if (framesA[j] != framesB[j]) {
            outsideIdentical = false;
            if (firstMismatch < 0) firstMismatch = j;
        }
    }
    check(outsideIdentical,
          QString("frames outside the repair range are byte-identical between A and B%1")
              .arg(firstMismatch >= 0 ? QString(" (first mismatch at output frame %1)").arg(firstMismatch) : QString()));

    // Frames inside the range in B must differ from A (repair actually
    // happened) and decode to LFE ~ 0.
    bool insideDiffers = true;
    for (qint64 j = outFrom; j <= outTo && j < framesA.size(); ++j) {
        if (framesA[j] == framesB[j]) { insideDiffers = false; break; }
    }
    check(insideDiffers, "frames inside the repair range differ between A and B (repair applied)");

    const qint64 warmupStart = outFrom >= 2 ? outFrom - 2 : 0;
    const int warmupCount = static_cast<int>(outFrom - warmupStart);
    QVector<QByteArray> lfeSeq;
    for (qint64 j = warmupStart; j < outFrom; ++j) lfeSeq.append(framesB[j]);
    for (qint64 j = outFrom; j <= outTo && j < framesB.size(); ++j) lfeSeq.append(framesB[j]);

    QVector<float> lfe;
    int samplesPerFrame = 0;
    QString decErr;
    check(decodeLfe(lfeSeq, lfe, samplesPerFrame, decErr),
          QString("decode B's repair range + warm-up for LFE check (err: %1)").arg(decErr));
    if (!lfe.isEmpty() && samplesPerFrame > 0) {
        const int discard = warmupCount * samplesPerFrame;
        double lfeMaxAbs = 0.0;
        for (int n = discard; n < lfe.size(); ++n)
            lfeMaxAbs = std::max(lfeMaxAbs, (double)std::abs(lfe[n]));
        check(lfeMaxAbs < 1e-6, QString("LFE silent across the repaired range in B (max |x| = %1)").arg(lfeMaxAbs));
    }

    // B as a whole is structurally CRC-clean and carries the same frame
    // count as A (gapless: no dropped/duplicated frame).
    // Frame byte size read from the material, never assumed: it follows the
    // stream's bit rate (768 B at 192 kbit/s, 1536 B at 384 kbit/s - both
    // occur in this project's fixtures and corpus). Final review M9.
    const int frameBytes = framesA.isEmpty() ? 0 : framesA.first().size();
    check(frameBytes > 0, QString("A: frame byte size read from the material (%1 B)").arg(frameBytes));
    bool allSize1536 = true, allSync = true, allCrcOk = true;
    for (const QByteArray& f : framesB) {
        if (f.size() != frameBytes) allSize1536 = false;
        if (f.size() < 8 || (uint8_t)f[0] != 0x0B || (uint8_t)f[1] != 0x77) allSync = false;
        if (f.size() >= 4) {
            quint16 crc = crc16Msb(0, reinterpret_cast<const uint8_t*>(f.constData()) + 2, f.size() - 2);
            if (crc != 0) allCrcOk = false;
        } else {
            allCrcOk = false;
        }
    }
    check(allSize1536, QString("B: every frame is %1 bytes (the source's own CBR frame size)")
                           .arg(frameBytes));
    check(allSync, "B: every frame starts with sync 0B 77");
    check(allCrcOk, "B: every frame's CRC (poly 0x8005) is 0");
    check(framesB.size() == framesA.size(), "B: frame count matches A (gapless, no drop/duplicate)");

    // --- Step 4: no-op path (table entries outside every keep window) -------
    TTAudioRepairItem outsideItem(0, 5, 6, kMask); // t=0.16-0.224s, well before the 10.0s segment
    QString outsideErr;
    TTAudioRepair::FrameTable outsideTable =
        TTAudioRepair::buildRepairTable(kSampleFile, outsideItem, -1, &outsideErr);
    check(outsideErr.isEmpty(), QString("buildRepairTable (outside-window item): no error (got: %1)").arg(outsideErr));

    QFile::remove(kOutC);
    TTFFmpegWrapper ffC;
    bool okC = ffC.cutAudioStream(kSampleFile, kOutC, keep, false, QList<int>(),
                                   nullptr, {}, &outsideTable);
    check(okC, "cut C (table with no matching frames) succeeded");
    if (okC) {
        QByteArray md5A = fileMd5(kOutA);
        QByteArray md5C = fileMd5(kOutC);
        check(!md5A.isEmpty() && md5A == md5C,
              QString("C is byte-identical to A (lookup never matched, no error): md5A=%1 md5C=%2")
                  .arg(QString(md5A.toHex())).arg(QString(md5C.toHex())));
    }

    // --- Step 5: segment-boundary-span path (Fix-Runde 1) -------------------
    testSegmentBoundarySpan();

    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
