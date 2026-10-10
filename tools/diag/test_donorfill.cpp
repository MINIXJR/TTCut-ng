// Diagnostic harness for the donor fill (extern/ttdonorfill.cpp and the
// "donor-fill" method of extern/ttaudiorepair.cpp; spec 2026-10-10).
//
//   test_donorfill
//     Self-test: the pure functions on synthetic PCM, then reader, build and
//     search on files made by make_donorfill_sample.sh (built if missing).
//     Exit 0 = all pass, 1 = failures.
//
//   test_donorfill --probe <ac3> <frameFrom> <frameTo> <donor> <expectedShift> <outPrefix>
//     The donor fill at a hole marker's frames of a real recording. Prints
//       STATUS <found|nohole|nosound|formatchange|unsupported|error> [detail]
//       HOLE <start> <end>            samples of the AC3 track, end exclusive
//       FIT <shift> <match> <gain>... shift in samples, donor minus AC3
//       RANGE <frameFrom> <frameTo>   the frames replaced
//     and writes <outPrefix>-original.ac3 / <outPrefix>-repariert.ac3
//     (+/-94 frames) to listen to. Exit 0 only when both were written.
//
// Build via `cmake --build build --target test_donorfill`.
#include <cmath>
#include <cstdio>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QVector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include "../../extern/ttaudiorepair.h"
#include "../../extern/ttaudiorepairitem.h"
#include "../../extern/ttdonorfill.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

// Deterministic white noise, -amp..amp.
static QVector<float> noise(int n, quint32 seed, float amp)
{
    QVector<float> v(n);
    quint32 s = seed;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        v[i] = amp * (float(s >> 8) / 8388608.0f - 1.0f);
    }
    return v;
}

static void testLocateHole()
{
    using TTDonorFill::HolePlacement;
    using TTDonorFill::locateHole;
    const int F = 1536, rate = 48000;
    const qint64 firstPos = 1000LL * F;                       // the planes hold frames 1000..1004
    const qint64 from = firstPos + F, to = firstPos + 4 * F;  // region: frames 1001..1003
    // Noise with silence in [a, b) (track samples); loud neighbours make the edges exact.
    auto holed = [&](quint32 seed, qint64 a, qint64 b) {
        QVector<float> v = noise(5 * F, seed, 0.3f);
        for (qint64 p = a; p < b; ++p) v[p - firstPos] = 0.0f;
        v[a - 1 - firstPos] = 0.25f;
        if (b - firstPos < v.size()) v[b - firstPos] = 0.25f;
        return v;
    };
    const qint64 h = firstPos + 2 * F + 300;
    {
        const HolePlacement r = locateHole({holed(1, h, h + 816)}, firstPos, from, to, rate);
        check(r.found && r.start == h && r.end == h + 816,
              QString("hole: found to the sample (%1..%2, expected %3..%4)").arg(r.start).arg(r.end).arg(h).arg(h + 816));
    }
    {
        const HolePlacement r = locateHole({holed(1, h, h + 816), holed(2, h + 5, h + 800)}, firstPos, from, to, rate);
        check(r.found && r.start == h && r.end == h + 816, "hole: two planes give the earliest start and the latest end");
    }
    {
        const HolePlacement r = locateHole({holed(1, h, h + 816), noise(5 * F, 2, 0.3f)}, firstPos, from, to, rate);
        check(r.found && r.start == h && r.end == h + 816, "hole: a plane without a hole does not hide the other's");
    }
    check(!locateHole({holed(1, h, h + 60)}, firstPos, from, to, rate).found, "hole: a run of 60 samples is none");
    check(!locateHole({holed(1, to - 400, to)}, firstPos, from, to, rate).found, "hole: a run touching the end of the region is none");
    check(!locateHole({holed(1, from, from + 400)}, firstPos, from, to, rate).found, "hole: a run touching the start of the region is none");
    check(!locateHole({noise(5 * F, 1, 0.3f)}, firstPos, from, to, rate).found, "hole: plain noise has none");
    check(!locateHole({}, firstPos, from, to, rate).found, "hole: no planes, no hole");
    check(!locateHole({noise(5 * F, 1, 0.3f), noise(4 * F, 2, 0.3f)}, firstPos, from, to, rate).found, "hole: planes of different length are refused");
    {
        // Review focus 2: the planes start with the track (firstPos 0, region from 0).
        QVector<float> v = noise(5 * F, 3, 0.3f);
        const qint64 a = 2 * F + 300;
        for (qint64 p = a; p < a + 816; ++p) v[p] = 0.0f;
        v[a - 1] = 0.25f; v[a + 816] = 0.25f;
        const HolePlacement r = locateHole({v}, 0, 0, 4 * F, rate);
        check(r.found && r.start == a && r.end == a + 816, "hole: found in planes that start with the track");
    }
}

static void testFitDonor()
{
    using TTDonorFill::DonorFit;
    using TTDonorFill::fitDonor;
    const int rate = 48000, N = 60000;
    const qint64 hs = 30000, he = 30816;
    const QVector<float> base = noise(N, 7, 0.3f), base2 = noise(N, 8, 0.3f);
    // The donor carries the same sound `delay` samples later, scaled by factor.
    auto delayed = [&](const QVector<float>& b, int delay, float factor) {
        QVector<float> d(b.size(), 0.0f);
        for (int i = delay; i < b.size(); ++i) d[i] = factor * b[i - delay];
        return d;
    };
    auto withHole = [&](QVector<float> v) { for (qint64 i = hs; i < he; ++i) v[i] = 0.0f; return v; };
    const QVector<float> t = withHole(base), t2 = withHole(base2);
    {
        const DonorFit f = fitDonor({t}, 0, {delayed(base, 300, 0.5f)}, 0, hs, he, 0, rate);
        check(f.valid && f.shift == 300 && f.gains.size() == 1 && std::fabs(f.gains[0] - 2.0) < 1e-3 && f.match > 0.999,
              QString("fit: shift 300, gain 2 recovered (shift %1, gain %2, match %3)")
                  .arg(f.shift).arg(f.gains.value(0)).arg(f.match));
    }
    {
        const DonorFit f = fitDonor({t.mid(1000)}, 1000, {delayed(base, 300, 0.5f).mid(2000)}, 2000, hs, he, 280, rate);
        check(f.valid && f.shift == 300, QString("fit: plane offsets and a near expected shift give the same shift (%1)").arg(f.shift));
    }
    {
        const DonorFit f = fitDonor({t, t2}, 0, {delayed(base, 300, 0.5f), delayed(base2, 300, 0.25f)}, 0, hs, he, 0, rate);
        check(f.valid && f.shift == 300 && f.gains.size() == 2 && std::fabs(f.gains[0] - 2.0) < 1e-3 && std::fabs(f.gains[1] - 4.0) < 1e-3,
              QString("fit: two channels, one shift, a gain each (%1, %2)").arg(f.gains.value(0)).arg(f.gains.value(1)));
    }
    {
        const DonorFit f = fitDonor({t}, 0, {delayed(base, 300, 0.5f)}, 0, hs, he, 3000, rate);
        check(f.valid && f.match < 0.5, QString("fit: the true shift outside the search gives a poor match (%1)").arg(f.match));
    }
    {
        // Review focus 1.
        const DonorFit f = fitDonor({t}, 0, {QVector<float>(N, 0.0f)}, 0, hs, he, 120, rate);
        check(f.valid && f.shift == 120 && f.match == 0.0 && f.gains.value(0, 1.0) == 0.0,
              QString("fit: silent donor -> match 0, gain 0, the expected shift (shift %1, match %2)").arg(f.shift).arg(f.match));
    }
    {
        const DonorFit f = fitDonor({t}, 0, {delayed(base, 300, -0.5f)}, 0, hs, he, 0, rate);
        check(f.valid && f.match < 0.1 && f.gains.value(0, -1.0) >= 0.0 && f.gains.value(0, 1.0) < 0.2,
              QString("fit: an inverted donor gets no negative gain (match %1, gain %2)").arg(f.match).arg(f.gains.value(0)));
    }
    check(!fitDonor({t}, 0, {delayed(base, 300, 0.5f)}, 0, 5000, 5816, 0, rate).valid, "fit: a hole too close to the start is refused");
    check(!fitDonor({t}, 0, {delayed(base, 300, 0.5f).mid(0, 38000)}, 0, hs, he, 0, rate).valid, "fit: a donor too short for the search is refused");
    check(!fitDonor({t, t2}, 0, {delayed(base, 300, 0.5f)}, 0, hs, he, 0, rate).valid, "fit: plane counts must agree");
}

static void testWeightAndApply()
{
    using TTDonorFill::applyDonor;
    using TTDonorFill::donorWeight;
    const qint64 hs = 1000, he = 1816;
    const int xf = 96;
    check(donorWeight(hs - xf - 1, hs, he, xf) == 0.0 && donorWeight(he + xf, hs, he, xf) == 0.0, "weight: 0 outside hole and cross-fades");
    check(donorWeight(hs, hs, he, xf) == 1.0 && donorWeight(he - 1, hs, he, xf) == 1.0, "weight: 1 inside the hole");
    check(donorWeight(hs - xf, hs, he, xf) > 0.0 && donorWeight(hs - xf, hs, he, xf) < 0.01
          && donorWeight(hs - 1, hs, he, xf) > 0.99 && donorWeight(hs - 1, hs, he, xf) < 1.0, "weight: the ramp runs from next to 0 to next to 1");
    bool rising = true, mirrored = true;
    for (int k = 0; k < xf; ++k) {
        if (k > 0) rising = rising && donorWeight(hs - xf + k, hs, he, xf) > donorWeight(hs - xf + k - 1, hs, he, xf);
        mirrored = mirrored && std::fabs(donorWeight(hs - 1 - k, hs, he, xf) - donorWeight(he + k, hs, he, xf)) < 1e-12;
    }
    check(rising, "weight: the ramp before the hole rises");
    check(mirrored, "weight: the ramp behind the hole mirrors the one before");

    const QVector<float> original = noise(3000, 3, 0.3f), donor = noise(int(he - hs) + 2 * xf, 4, 0.2f);
    QVector<float> data = original;
    applyDonor(data.data(), int(data.size()), 0, donor, hs - xf, hs, he, xf, 1.5);
    bool outside = true, inside = true, between = true;
    for (qint64 p = 0; p < data.size(); ++p) {
        const float d = float(1.5 * donor.value(p - (hs - xf)));
        if (p < hs - xf || p >= he + xf) outside = outside && data[p] == original[p];
        else if (p >= hs && p < he)      inside = inside && data[p] == d;
        else between = between && data[p] >= qMin(original[p], d) - 1e-6f && data[p] <= qMax(original[p], d) + 1e-6f;
    }
    check(outside, "apply: samples outside hole and cross-fades are untouched");
    check(inside, "apply: inside the hole stands gain x donor");
    check(between, "apply: in the cross-fades the result lies between original and donor");

    // The same fill, applied to a buffer that starts in the middle (one "frame").
    QVector<float> part = original.mid(900, 200);
    applyDonor(part.data(), int(part.size()), 900, donor, hs - xf, hs, he, xf, 1.5);
    bool same = true;
    for (int n = 0; n < part.size(); ++n) same = same && part[n] == data[900 + n];
    check(same, "apply: a buffer starting at another position gets the same samples");
}

// ---- files made by make_donorfill_sample.sh --------------------------------
static const QString kDir = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/donorfill_sample");
static const QString kMakeScript = QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_donorfill_sample.sh");
static QString fx(const char* name) { return kDir + QLatin1Char('/') + QLatin1String(name); }

static bool ensureFixture()
{
    if (QFileInfo::exists(fx("unrelated.mp2"))) return true; // the script's last output
    QProcess proc;
    proc.start(kMakeScript, {kDir});
    return proc.waitForStarted(5000) && proc.waitForFinished(300000) && proc.exitCode() == 0;
}

// Byte position of the first packet libavformat delivers; -1 on error.
static qint64 firstPacketPos(const QString& path)
{
    AVFormatContext* fmt = nullptr;
    int idx = -1;
    QString err;
    if (!TTAudioRepair::openFirstAudioStream(path, &fmt, &idx, &err)) return -1;
    AVPacket* pkt = av_packet_alloc();
    qint64 pos = -1;
    while (pos < 0 && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx) pos = pkt->pos;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return pos;
}

// A copy of an MP2 file whose first frame header differs from the second in
// the "original" bit: libavformat then starts with the second frame.
static QString headerSkipCopy(const QString& mp2)
{
    QFile in(mp2);
    if (!in.open(QIODevice::ReadOnly)) return QString();
    QByteArray bytes = in.readAll();
    bytes[3] = char(bytes[3] ^ 0x04);
    const QString out = kDir + QStringLiteral("/donor51_300_hdr.mp2");
    QFile o(out);
    if (!o.open(QIODevice::WriteOnly | QIODevice::Truncate) || o.write(bytes) != bytes.size()) return QString();
    return out;
}

static double energyOf(const QVector<float>& v)
{
    double e = 0.0;
    for (float x : v) e += double(x) * x;
    return e;
}

static double maxDiff(const QVector<float>& a, const QVector<float>& b)
{
    if (a.size() != b.size()) return 1e9;
    double m = 0.0;
    for (qsizetype i = 0; i < a.size(); ++i) m = qMax(m, double(std::fabs(a[i] - b[i])));
    return m;
}

static void testReader()
{
    using TTDonorFill::DonorPcm;
    using TTDonorFill::readDonorRange;
    const QString mp2 = fx("donor51_300.mp2");
    const DonorPcm a = readDonorRange(mp2, 100000, 5000);
    check(a.ok && a.planes.size() == 2 && a.planes[0].size() == 5000 && a.planes[1].size() == 5000 && a.sampleRate == 48000,
          QString("reader: MP2 range read as two planes at 48 kHz (%1)").arg(a.error));
    check(a.ok && energyOf(a.planes[0]) > 1.0 && energyOf(a.planes[1]) > 1.0, "reader: the range carries sound");
    const DonorPcm b = readDonorRange(mp2, 102000, 1000);
    check(a.ok && b.ok && maxDiff(a.planes[0].mid(2000, 1000), b.planes[0]) < 1e-4,
          QString("reader: a later start gives the same samples (max diff %1)")
              .arg(a.ok && b.ok ? maxDiff(a.planes[0].mid(2000, 1000), b.planes[0]) : -1.0));
    const DonorPcm head = readDonorRange(mp2, 0, 3000);
    check(head.ok && head.planes[0].size() == 3000, QString("reader: the start of the track can be read (%1)").arg(head.error));

    // Review focus 5.
    const QString hdr = headerSkipCopy(mp2);
    check(!hdr.isEmpty() && firstPacketPos(mp2) == 0 && firstPacketPos(hdr) > 0,
          QString("fixture: libav skips the first frame of the header-skip donor (first packet at %1)").arg(firstPacketPos(hdr)));
    const DonorPcm c = readDonorRange(hdr, 100000, 5000);
    check(a.ok && c.ok && maxDiff(a.planes[0], c.planes[0]) < 1e-6 && maxDiff(a.planes[1], c.planes[1]) < 1e-6,
          QString("reader: header-skip donor gives the same samples at the same positions (%1)").arg(c.error));

    const DonorPcm ac3 = readDonorRange(fx("donor51_300.ac3"), 100000, 5000);
    check(ac3.ok && ac3.planes.size() == 2 && ac3.sampleRate == 48000 && energyOf(ac3.planes[0]) > 1.0,
          QString("reader: AC3 2.0 donor read (%1)").arg(ac3.error));

    // Review focus 3.
    const DonorPcm before = readDonorRange(mp2, -10, 100);
    check(!before.ok && before.noSound && !before.error.isEmpty(), "reader: a range before the start is 'no sound'");
    const DonorPcm past = readDonorRange(mp2, 959000, 5000);
    check(!past.ok && past.noSound && !past.error.isEmpty(), QString("reader: a range past the end is 'no sound' (%1)").arg(past.error));
    const DonorPcm missing = readDonorRange(kDir + QStringLiteral("/does-not-exist.mp2"), 0, 100);
    check(!missing.ok && !missing.noSound && !missing.error.isEmpty(), "reader: a missing file is an error, not 'no sound'");
    const DonorPcm six = readDonorRange(fx("hole51.ac3"), 100000, 100);
    check(!six.ok && six.noSound, QString("reader: a 5.1 track is 'no sound' as a donor (%1)").arg(six.error));
    check(!readDonorRange(mp2, 100000, 0).ok, "reader: an empty range is refused");
}

// Every AC3 frame of a file, in order.
static QVector<QByteArray> readFrames(const QString& path)
{
    QVector<QByteArray> frames;
    AVFormatContext* fmt = nullptr;
    int idx = -1;
    QString err;
    if (!TTAudioRepair::openFirstAudioStream(path, &fmt, &idx, &err)) return frames;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx) frames.append(QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size));
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return frames;
}

// Decodes AC3 frames as TTAc3Reencoder does (no dynamic range compression);
// plane sample i is track sample i when the frames start with frame 0.
static TTDonorFill::Planes decodeAc3(const QVector<QByteArray>& frames)
{
    TTDonorFill::Planes out;
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
    if (!ctx) return out;
    av_opt_set_double(ctx->priv_data, "drc_scale", 0.0, 0);
    if (avcodec_open2(ctx, dec, nullptr) < 0) { avcodec_free_context(&ctx); return out; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    for (const QByteArray& fb : frames) {
        pkt->data = reinterpret_cast<uint8_t*>(const_cast<char*>(fb.constData()));
        pkt->size = int(fb.size());
        if (avcodec_send_packet(ctx, pkt) < 0 || avcodec_receive_frame(ctx, frame) < 0) { out.clear(); break; }
        if (out.isEmpty()) out.resize(frame->ch_layout.nb_channels);
        for (int c = 0; c < out.size(); ++c) {
            const float* d = reinterpret_cast<const float*>(frame->extended_data[c]);
            for (int n = 0; n < frame->nb_samples; ++n) out[c].append(d[n]);
        }
        av_frame_unref(frame);
    }
    pkt->data = nullptr;
    pkt->size = 0;
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return out;
}

// How many dB the repaired sound is closer to the clean one than silence, over [a, b).
static double closerThanSilence(const QVector<float>& repaired, const QVector<float>& clean, qint64 a, qint64 b)
{
    double sig = 0.0, err = 0.0;
    for (qint64 i = a; i < b; ++i) {
        sig += double(clean[i]) * clean[i];
        const double e = double(clean[i]) - repaired[i];
        err += e * e;
    }
    return 10.0 * std::log10(qMax(sig, 1e-30) / qMax(err, 1e-30));
}

// Builds the table for item, splices it into the holed file and compares the
// filled channels with the file without holes. Thresholds: measured before
// the re-encode 11.7..15.0 dB over the hole and 11.8..15.5 dB over its last
// 200 samples (2026-10-10, fixture_check.py); a stereo fill taken from the
// other channel measures -3 dB.
static void checkFill(const QString& what, const QString& hole, const QString& clean, const QString& donor,
                      const TTAudioRepairItem& item)
{
    QString err;
    const TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(hole, item, -1, &err, donor);
    check(err.isEmpty() && table.size() == item.frameTo() - item.frameFrom() + 1,
          QString("%1: table of %2 frame(s) built (%3)").arg(what).arg(table.size()).arg(err));
    if (!err.isEmpty() || table.isEmpty()) return;

    QVector<QByteArray> frames = readFrames(hole);
    const TTDonorFill::Planes before = decodeAc3(frames);
    bool sizes = true;
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        sizes = sizes && it.key() >= item.frameFrom() && it.key() <= item.frameTo() && it.value().size() == frames[it.key()].size();
        frames[it.key()] = it.value();
    }
    check(sizes, QString("%1: the replacements are the item's frames and keep the frame size").arg(what));
    const TTDonorFill::Planes after = decodeAc3(frames), ref = decodeAc3(readFrames(clean));
    const QVector<int> planes = item.channelMask() == TTAudioRepair::kDonorFillMaskCentre ? QVector<int>{2} : QVector<int>{0, 1};
    if (after.size() != ref.size() || after.isEmpty() || before.size() != ref.size()) { check(false, what + ": decode of the result"); return; }
    for (int p : planes) {
        const double was = closerThanSilence(before[p], ref[p], item.holeStart(), item.holeEnd());
        const double all = closerThanSilence(after[p], ref[p], item.holeStart(), item.holeEnd());
        const double tail = closerThanSilence(after[p], ref[p], item.holeEnd() - 200, item.holeEnd());
        check(was < 1.0, QString("%1, plane %2: the hole is a hole before the repair (%3 dB)").arg(what).arg(p).arg(was, 0, 'f', 1));
        check(all >= 9.0, QString("%1, plane %2: the fill is %3 dB closer to the clean sound than silence (>= 9)").arg(what).arg(p).arg(all, 0, 'f', 1));
        check(tail >= 9.0, QString("%1, plane %2: also over the last 200 samples of the hole, %3 dB (>= 9)").arg(what).arg(p).arg(tail, 0, 'f', 1));
    }
}

static void testBuild()
{
    using TTAudioRepair::buildRepairTable;
    using TTAudioRepair::donorFillProblem;
    using TTAudioRepair::makeDonorFillItem;
    const quint8 C = TTAudioRepair::kDonorFillMaskCentre, LR = TTAudioRepair::kDonorFillMaskStereo;
    const QString hole51 = fx("hole51.ac3"), clean51 = fx("clean51.ac3"), mp2 = fx("donor51_300.mp2");

    // Values measured on the files (2026-10-10, fixture_check.py): hole, shift, gain.
    const TTAudioRepairItem a = makeDonorFillItem(0, C, 1, 240243, 240721, 525, {1.374}, 0.977, 48000);
    check(a.isDonorFill() && a.frameFrom() == 156 && a.frameTo() == 156 && a.donorTrack() == 1 && a.channelMask() == C
          && a.holeStart() == 240243 && a.holeEnd() == 240721 && a.donorShift() == 525 && a.gains() == QVector<double>{1.374} && a.match() == 0.977,
          "item: makeDonorFillItem keeps the values, hole A lies in frame 156");
    const TTAudioRepairItem b = makeDonorFillItem(0, C, 1, 383852, 384141, 525, {1.377}, 0.976, 48000);
    check(b.frameFrom() == 249 && b.frameTo() == 250, "item: hole B touches frames 249-250");
    const TTAudioRepairItem c = makeDonorFillItem(0, C, 1, 527749, 528075, 525, {1.380}, 0.978, 48000);
    check(c.frameFrom() == 343 && c.frameTo() == 343 && 344 * 1536 - (c.holeEnd() + 96) < 256,
          "item: the fill of hole C ends in the last 256 samples of frame 343");
    check(makeDonorFillItem(0, C, 1, 50, 500, 0, {1.0}, 1.0, 48000).frameFrom() == -1, "item: a fill starting before the track has frameFrom -1");
    check(donorFillProblem(a, 48000).isEmpty(), QString("item: sound values have no problem (%1)").arg(donorFillProblem(a, 48000)));

    checkFill("5.1 + MP2, hole A", hole51, clean51, mp2, a);
    checkFill("5.1 + MP2, hole B (across a frame boundary)", hole51, clean51, mp2, b);
    checkFill("5.1 + MP2, hole C (ends late in its frame)", hole51, clean51, mp2, c);
    checkFill("5.1 + AC3 2.0 donor, hole A", hole51, clean51, fx("donor51_300.ac3"),
              makeDonorFillItem(0, C, 1, 240243, 240721, 300, {1.383}, 0.977, 48000));
    checkFill("stereo + MP2, hole A", fx("hole20.ac3"), fx("clean20.ac3"), fx("donor20_300.mp2"),
              makeDonorFillItem(0, LR, 1, 240257, 240627, 525, {0.973, 0.971}, 0.973, 48000));

    // Review focus 4 and 3: what must be refused, with the reason named.
    auto refuses = [&](const QString& what, const TTAudioRepairItem& item, const QString& donor, const QString& needle) {
        QString err;
        const TTAudioRepair::FrameTable t = buildRepairTable(hole51, item, -1, &err, donor);
        check(t.isEmpty() && err.contains(needle, Qt::CaseInsensitive), QString("build: refuses %1 (%2)").arg(what, err));
    };
    auto edited = [&](quint8 mask, qint64 hs, qint64 he, const QVector<double>& gains, qint64 f0, qint64 f1) {
        TTAudioRepairItem item(0, f0, f1, mask);
        item.setDonorFill(1, hs, he, 525, gains, 0.9);
        return item;
    };
    refuses("a missing donor file name", a, QString(), "no donor track");
    refuses("an unreadable donor file", a, kDir + "/does-not-exist.mp2", "donor fill");
    refuses("a gain too many", edited(C, 240250, 240723, {1.0, 1.0}, 156, 156), mp2, "gain");
    refuses("a gain that is not a number", edited(C, 240250, 240723, {std::nan("")}, 156, 156), mp2, "gain");
    refuses("a gain above 100", edited(C, 240250, 240723, {101.0}, 156, 156), mp2, "gain");
    refuses("a negative gain", edited(C, 240250, 240723, {-0.5}, 156, 156), mp2, "gain");
    refuses("a mask that is neither centre nor left+right", edited(0x08, 240250, 240723, {1.0}, 156, 156), mp2, "channel mask");
    refuses("an empty hole", edited(C, 240723, 240723, {1.0}, 156, 156), mp2, "hole");
    refuses("a hole of minutes", edited(C, 240250, 240250 + 48000 * 120, {1.0}, 156, 3906), mp2, "hole");
    refuses("a frame range that does not fit", edited(C, 240250, 240723, {1.0}, 150, 156), mp2, "frame range");
    refuses("left+right on a 5.1 track", edited(LR, 240250, 240723, {1.0, 1.0}, 156, 156), mp2, "channel layout");
    refuses("a shift before the donor's start", makeDonorFillItem(0, C, 1, 240250, 240723, -400000, {1.0}, 0.9, 48000), mp2, "donor fill");
    refuses("a shift past the donor's end", makeDonorFillItem(0, C, 1, 240250, 240723, 800000, {1.0}, 0.9, 48000), mp2, "donor fill");
    refuses("a 5.1 track as donor", a, hole51, "donor fill");
    // Values no track can have must be refused before anything is computed from them.
    refuses("a shift beyond any track", makeDonorFillItem(0, C, 1, 240243, 240721, 9000000000000000000LL, {1.0}, 0.9, 48000), mp2, "shift");
    refuses("a shift far before any track", makeDonorFillItem(0, C, 1, 240243, 240721, -9000000000000000000LL, {1.0}, 0.9, 48000), mp2, "shift");
    refuses("a hole position beyond any track", edited(C, 9000000000000000000LL, 9000000000000000400LL, {1.0}, 156, 156), mp2, "hole");
    refuses("a hole position before the track", edited(C, -9000000000000000000LL, -8999999999999999600LL, {1.0}, 156, 156), mp2, "hole");
}

static const QString kAcmodSampleFile = QStringLiteral("/usr/local/src/CLAUDE_TMP/TTCut-ng/acmod_change_sample.ac3");
static const QString kAcmodMakeScript = QStringLiteral("/usr/local/src/TTCut-ng/tools/diag/make_acmod_change_sample.sh");

static const char* statusName(TTAudioRepair::DonorFillStatus s)
{
    switch (s) {
    case TTAudioRepair::DonorFillStatus::Found:             return "found";
    case TTAudioRepair::DonorFillStatus::NoHole:            return "nohole";
    case TTAudioRepair::DonorFillStatus::NoSound:           return "nosound";
    case TTAudioRepair::DonorFillStatus::FormatChange:      return "formatchange";
    case TTAudioRepair::DonorFillStatus::UnsupportedLayout: return "unsupported";
    case TTAudioRepair::DonorFillStatus::DonorUnreadable:   return "donorunreadable";
    case TTAudioRepair::DonorFillStatus::Error:             return "error";
    }
    return "?";
}

static void testSearch()
{
    using TTAudioRepair::DonorFillSearch;
    using TTAudioRepair::DonorFillStatus;
    using TTAudioRepair::findDonorFill;
    const QString hole51 = fx("hole51.ac3"), clean51 = fx("clean51.ac3"), mp2 = fx("donor51_300.mp2");
    auto describe = [](const DonorFillSearch& s) {
        return QString("%1, hole %2..%3, frames %4-%5, shift %6, match %7, gain %8 %9")
            .arg(statusName(s.status)).arg(s.item.holeStart()).arg(s.item.holeEnd()).arg(s.item.frameFrom()).arg(s.item.frameTo())
            .arg(s.item.donorShift()).arg(s.item.match(), 0, 'f', 3).arg(s.item.gains().value(0), 0, 'f', 3).arg(s.error);
    };
    // The decoded hole lies inside [lo, hi) and the quiet part found is at
    // least 200 samples long (measured: 289..478 of the 816; the encoder
    // smears the edges).
    auto holeIn = [](const DonorFillSearch& s, qint64 lo, qint64 hi) {
        return s.status == DonorFillStatus::Found && s.item.holeStart() >= lo - 48 && s.item.holeEnd() <= hi + 48
            && s.item.holeEnd() - s.item.holeStart() >= 200;
    };

    const DonorFillSearch a = findDonorFill(hole51, 0, 155, 157, mp2, 1, 0);
    check(holeIn(a, 240000, 240816) && a.item.frameFrom() == 156 && a.item.frameTo() == 156
          && a.item.channelMask() == TTAudioRepair::kDonorFillMaskCentre && a.item.donorTrack() == 1 && a.item.trackIndex() == 0,
          "search: hole A found in frame 156, centre, donor track kept - " + describe(a));
    check(a.status == DonorFillStatus::Found && a.item.match() >= 0.95 && a.item.gains().size() == 1
          && a.item.gains()[0] > 1.2 && a.item.gains()[0] < 1.6,
          "search: hole A fits the donor (match >= 0.95, gain 1.2..1.6; measured 0.977, 1.374)");
    check(TTAudioRepair::donorFillProblem(a.item, 48000).isEmpty(), "search: the item it returns has sound values");

    const DonorFillSearch a700 = findDonorFill(hole51, 0, 155, 157, fx("donor51_700.mp2"), 1, 0);
    check(a700.status == DonorFillStatus::Found && a700.item.donorShift() - a.item.donorShift() == 400,
          QString("search: a donor 400 samples later gives a shift 400 larger (%1 vs %2)").arg(a700.item.donorShift()).arg(a.item.donorShift()));
    // Review focus 5.
    const DonorFillSearch hdr = findDonorFill(hole51, 0, 155, 157, kDir + "/donor51_300_hdr.mp2", 1, 0);
    check(hdr.status == DonorFillStatus::Found && hdr.item.donorShift() == a.item.donorShift(),
          QString("search: header-skip donor gives the same shift (%1 vs %2)").arg(hdr.item.donorShift()).arg(a.item.donorShift()));
    const DonorFillSearch ac3 = findDonorFill(hole51, 0, 155, 157, fx("donor51_300.ac3"), 1, 0);
    check(ac3.status == DonorFillStatus::Found && ac3.item.donorShift() == 300 && ac3.item.match() >= 0.95,
          "search: AC3 2.0 donor, delayed by 300 samples, gives shift 300 - " + describe(ac3));

    const DonorFillSearch b = findDonorFill(hole51, 0, 248, 250, mp2, 1, 0);
    check(holeIn(b, 383600, 384416) && b.item.frameFrom() == 249 && b.item.frameTo() == 250 && b.item.donorShift() == a.item.donorShift(),
          "search: hole B lies across frames 249-250 - " + describe(b));
    const DonorFillSearch c = findDonorFill(hole51, 0, 342, 344, mp2, 1, 0);
    check(holeIn(c, 527484, 528300) && c.item.frameFrom() == 343 && c.item.frameTo() == 343 && 344 * 1536 - (c.item.holeEnd() + 96) < 256,
          "search: the fill of hole C ends in the last 256 samples of frame 343 - " + describe(c));
    const DonorFillSearch st = findDonorFill(fx("hole20.ac3"), 0, 155, 157, fx("donor20_300.mp2"), 1, 0);
    check(holeIn(st, 240000, 240816) && st.item.channelMask() == TTAudioRepair::kDonorFillMaskStereo && st.item.gains().size() == 2
          && st.item.gains()[0] > 0.9 && st.item.gains()[0] < 1.1 && st.item.gains()[1] > 0.9 && st.item.gains()[1] < 1.1 && st.item.match() >= 0.95,
          "search: stereo hole, left+right, two gains 0.9..1.1 (measured 0.973, 0.971) - " + describe(st));

    // Search and build together.
    if (a.status == DonorFillStatus::Found)  checkFill("search + build, 5.1 hole A", hole51, clean51, mp2, a.item);
    if (b.status == DonorFillStatus::Found)  checkFill("search + build, 5.1 hole B", hole51, clean51, mp2, b.item);
    if (c.status == DonorFillStatus::Found)  checkFill("search + build, 5.1 hole C", hole51, clean51, mp2, c.item);
    if (st.status == DonorFillStatus::Found) checkFill("search + build, stereo hole A", fx("hole20.ac3"), fx("clean20.ac3"), fx("donor20_300.mp2"), st.item);

    // What is not a fill.
    auto statusIs = [&](const QString& what, const DonorFillSearch& s, DonorFillStatus want) {
        check(s.status == want, QString("search: %1 -> %2 (got %3 %4)").arg(what, statusName(want), statusName(s.status), s.error));
    };
    statusIs("no hole in the frames", findDonorFill(clean51, 0, 155, 157, fx("donor51_300.mp2"), 1, 0), DonorFillStatus::NoHole);
    // Review focus 2.
    statusIs("first frames of the track", findDonorFill(hole51, 0, 0, 2, mp2, 1, 0), DonorFillStatus::NoHole);
    const qint64 lastFrame = readFrames(hole51).size() - 1;
    statusIs("last frames of the track", findDonorFill(hole51, 0, lastFrame - 2, lastFrame, mp2, 1, 0), DonorFillStatus::NoHole);
    // Review focus 3: a donor that ends at 4.8 s, the hole lies at 5.0 s.
    {
        QFile in(mp2);
        QFile out(kDir + "/donor51_short.mp2");
        const bool written = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                             && out.write(in.read(576 * 200)) == 576 * 200;
        out.close();
        check(written, "fixture: short donor written");
        statusIs("a donor that ends before the hole", findDonorFill(hole51, 0, 155, 157, out.fileName(), 1, 0), DonorFillStatus::NoSound);
    }
    statusIs("a missing donor file", findDonorFill(hole51, 0, 155, 157, kDir + "/does-not-exist.mp2", 1, 0), DonorFillStatus::DonorUnreadable);
    {
        // A donor with 100 bytes of junk before its first frame: libavformat
        // starts in the middle of nowhere, the frame count cannot be trusted.
        QFile in(mp2);
        QFile out(kDir + "/donor51_junk.mp2");
        const bool written = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                             && out.write(QByteArray(100, '\0')) == 100 && out.write(in.readAll()) > 0;
        out.close();
        check(written, "fixture: donor with junk before the first frame written");
        statusIs("a donor that does not start on a frame", findDonorFill(hole51, 0, 155, 157, out.fileName(), 1, 0), DonorFillStatus::DonorUnreadable);
    }
    statusIs("a missing AC3 file", findDonorFill(kDir + "/does-not-exist.ac3", 0, 155, 157, mp2, 1, 0), DonorFillStatus::Error);
    statusIs("an invalid frame range", findDonorFill(hole51, 0, 157, 155, mp2, 1, 0), DonorFillStatus::Error);
    statusIs("a mono track", findDonorFill(fx("mono.ac3"), 0, 155, 157, mp2, 1, 0), DonorFillStatus::UnsupportedLayout);
    {
        bool have = QFileInfo::exists(kAcmodSampleFile);
        if (!have) {
            QProcess proc;
            proc.start(kAcmodMakeScript, {kAcmodSampleFile});
            have = proc.waitForStarted(5000) && proc.waitForFinished(180000) && proc.exitCode() == 0;
        }
        const QVector<QByteArray> frames = have ? readFrames(kAcmodSampleFile) : QVector<QByteArray>();
        qint64 transition = -1;
        for (qsizetype i = 1; i < frames.size() && transition < 0; ++i)
            if ((quint8(frames[i][6]) >> 5) != (quint8(frames[0][6]) >> 5)) transition = i;
        check(transition > 2, QString("fixture: channel-mode change found at frame %1").arg(transition));
        if (transition > 2)
            statusIs("a channel-mode change in the frames", findDonorFill(kAcmodSampleFile, 0, transition - 1, transition + 1, mp2, 1, 0),
                     DonorFillStatus::FormatChange);
    }
    {
        const DonorFillSearch other = findDonorFill(hole51, 0, 155, 157, fx("unrelated.mp2"), 1, 0);
        check(other.status == DonorFillStatus::Found && other.item.match() < 0.80,
              "search: a donor with other sound is found as a poor match (measured 0.05) - " + describe(other));
    }
    {
        const DonorFillSearch far = findDonorFill(hole51, 0, 155, 157, mp2, 1, 5000);
        check(far.status == DonorFillStatus::Found && far.item.match() < 0.80,
              "search: the true shift outside +/-50 ms is found as a poor match (the dialog's hint) - " + describe(far));
    }
}

static int probeMode(char** argv)
{
    const QString ac3 = QString::fromUtf8(argv[2]), donor = QString::fromUtf8(argv[5]), prefix = QString::fromUtf8(argv[7]);
    const qint64 from = QString::fromUtf8(argv[3]).toLongLong(), to = QString::fromUtf8(argv[4]).toLongLong();
    const qint64 expected = QString::fromUtf8(argv[6]).toLongLong();

    const TTAudioRepair::DonorFillSearch s = TTAudioRepair::findDonorFill(ac3, 0, from, to, donor, 1, expected);
    printf("STATUS %s %s\n", statusName(s.status), qPrintable(s.error));
    if (s.status != TTAudioRepair::DonorFillStatus::Found) return 1;
    printf("HOLE %lld %lld\n", (long long)s.item.holeStart(), (long long)s.item.holeEnd());
    printf("FIT %lld %.4f", (long long)s.item.donorShift(), s.item.match());
    for (double g : s.item.gains()) printf(" %.4f", g);
    printf("\nRANGE %lld %lld\n", (long long)s.item.frameFrom(), (long long)s.item.frameTo());

    QString err;
    const TTAudioRepair::FrameTable table = TTAudioRepair::buildRepairTable(ac3, s.item, -1, &err, donor);
    if (!err.isEmpty()) { printf("FAIL: %s\n", qPrintable(err)); return 1; }
    const QVector<QByteArray> all = readFrames(ac3);
    if (all.isEmpty()) { printf("FAIL: could not read %s\n", qPrintable(ac3)); return 1; }
    const qint64 w0 = qMax<qint64>(0, s.item.frameFrom() - 94), w1 = qMin<qint64>(all.size() - 1, s.item.frameTo() + 94);
    QFile original(prefix + "-original.ac3"), repaired(prefix + "-repariert.ac3");
    if (!original.open(QIODevice::WriteOnly | QIODevice::Truncate) || !repaired.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        printf("FAIL: could not write %s-*.ac3\n", qPrintable(prefix));
        return 1;
    }
    for (qint64 f = w0; f <= w1; ++f) {
        original.write(all[f]);
        repaired.write(table.contains(f) ? table.value(f) : all[f]);
    }
    printf("written: %s-original.ac3, %s-repariert.ac3 (frames %lld-%lld)\n",
           qPrintable(prefix), qPrintable(prefix), (long long)w0, (long long)w1);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && QString::fromUtf8(argv[1]) == QLatin1String("--probe")) {
        if (argc != 8) { fprintf(stderr, "usage: test_donorfill --probe <ac3> <frameFrom> <frameTo> <donor> <expectedShift> <outPrefix>\n"); return 2; }
        return probeMode(argv);
    }
    testLocateHole();
    testFitDonor();
    testWeightAndApply();
    const bool fixture = ensureFixture();
    check(fixture, "fixture: donorfill_sample available (built if missing)");
    if (fixture) {
        testReader();
        testBuild();
        testSearch();
    }
    printf("%s (%d failure(s))\n", gFailures ? "FAILED" : "ALL PASS", gFailures);
    return gFailures ? 1 : 0;
}
