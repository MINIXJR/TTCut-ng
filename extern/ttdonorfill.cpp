// Donor fill: hole search, donor fit and cross-fade (pure), see ttdonorfill.h.
// The rules and their constants were measured on the two holes of one
// recording and on 1658 pretended holes (spec 2026-10-10, "Evidence").
#include "ttdonorfill.h"
#include "ttaudiorepair.h"

#include <QScopeGuard>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <cmath>

namespace TTDonorFill {

namespace {

constexpr int    kBlock           = 256;    // the anomaly scan's grid
constexpr double kHoleThresholdDb = 30.0;   // "in the hole": this far below the level before it

int msToSamples(double ms, int sampleRate)
{
    const int n = int(std::lround(ms * sampleRate / 1000.0));
    return n < 1 ? 1 : n;
}

double toDb(double p) { return p > 1e-18 ? 10.0 * std::log10(p) : -180.0; }

// The hole of one plane inside [lo, hi) (indices into the plane); *a < 0: none.
void holeOfPlane(const QVector<float>& x, qint64 firstPos, qint64 lo, qint64 hi, int minLen, qint64* a, qint64* b)
{
    *a = *b = -1;
    const qint64 n = x.size();
    QVector<double> cs(n + 1, 0.0);                     // prefix sums of the power
    for (qint64 i = 0; i < n; ++i) cs[i + 1] = cs[i] + double(x[i]) * x[i];
    auto mean = [&](qint64 p, qint64 q) { return (cs[q] - cs[p]) / double(q - p); };

    // 1. The block boundary with the largest drop, on the scan's grid.
    double bestDrop = -1e9;
    qint64 best = -1;
    const qint64 firstGrid = ((lo + firstPos + kBlock - 1) / kBlock) * kBlock - firstPos;
    for (qint64 i = firstGrid; i < hi; i += kBlock) {
        if (i - 4 * kBlock < 0 || i + 2 * kBlock > n) continue;
        const double drop = toDb(mean(i - 3 * kBlock, i)) - toDb(mean(i, i + 2 * kBlock));
        if (drop > bestDrop) { bestDrop = drop; best = i; }
    }
    if (best < 0) return;

    // 2. Quiet: at or below the level before that boundary minus kHoleThresholdDb.
    const double threshold = std::sqrt(mean(best - 4 * kBlock, best - kBlock)) * std::pow(10.0, -kHoleThresholdDb / 20.0);

    // 3. The longest quiet run.
    qint64 runStart = -1, bestStart = -1, bestLen = 0;
    for (qint64 i = lo; i <= hi; ++i) {
        if (i < hi && std::fabs(x[i]) <= threshold) {
            if (runStart < 0) runStart = i;
            continue;
        }
        if (runStart >= 0 && i - runStart > bestLen) { bestStart = runStart; bestLen = i - runStart; }
        runStart = -1;
    }
    // Too short, or not closed inside the region.
    if (bestLen < minLen || bestStart == lo || bestStart + bestLen == hi) return;
    *a = bestStart;
    *b = bestStart + bestLen;
}

constexpr int kWarmupFrames = 2;            // decoded before the range: the synthesis filter's history

QString avErr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

// Sample n of channel ch as float; false for a sample format the decoders
// in use do not produce.
bool sampleValue(const AVFrame* f, int ch, int n, float* out)
{
    const int nb = f->ch_layout.nb_channels;
    switch (f->format) {
    case AV_SAMPLE_FMT_FLTP: *out = reinterpret_cast<const float*>(f->extended_data[ch])[n]; return true;
    case AV_SAMPLE_FMT_S16P: *out = reinterpret_cast<const int16_t*>(f->extended_data[ch])[n] / 32768.0f; return true;
    case AV_SAMPLE_FMT_FLT:  *out = reinterpret_cast<const float*>(f->extended_data[0])[n * nb + ch]; return true;
    case AV_SAMPLE_FMT_S16:  *out = reinterpret_cast<const int16_t*>(f->extended_data[0])[n * nb + ch] / 32768.0f; return true;
    default: return false;
    }
}

} // namespace

int crossFadeSamples(int sampleRate) { return msToSamples(2.0, sampleRate); }
int guardSamples(int sampleRate)     { return msToSamples(5.0, sampleRate); }
int compareSamples(int sampleRate)   { return msToSamples(150.0, sampleRate); }
int searchSamples(int sampleRate)    { return msToSamples(50.0, sampleRate); }
int minHoleSamples(int sampleRate)   { return msToSamples(2.0, sampleRate); }

HolePlacement locateHole(const Planes& planes, qint64 firstPos, qint64 from, qint64 to, int sampleRate)
{
    HolePlacement r;
    if (planes.isEmpty() || sampleRate <= 0 || firstPos < 0) return r;
    const qint64 n = planes[0].size();
    for (const QVector<float>& p : planes)
        if (p.size() != n) return r;
    const qint64 lo = qMax<qint64>(0, from - firstPos), hi = qMin(n, to - firstPos);
    if (hi <= lo) return r;

    for (const QVector<float>& p : planes) {
        qint64 a = -1, b = -1;
        holeOfPlane(p, firstPos, lo, hi, minHoleSamples(sampleRate), &a, &b);
        if (a < 0) continue;
        r.start = r.found ? qMin(r.start, a + firstPos) : a + firstPos;
        r.end   = r.found ? qMax(r.end, b + firstPos) : b + firstPos;
        r.found = true;
    }
    return r;
}

DonorFit fitDonor(const Planes& target, qint64 targetPos, const Planes& donor, qint64 donorPos,
                  qint64 holeStart, qint64 holeEnd, qint64 expectedShift, int sampleRate)
{
    DonorFit r;
    if (target.isEmpty() || target.size() != donor.size() || sampleRate <= 0 || holeEnd <= holeStart) return r;
    const qint64 G = guardSamples(sampleRate), W = compareSamples(sampleRate), S = searchSamples(sampleRate);
    const qint64 tn = target[0].size(), dn = donor[0].size();
    for (const QVector<float>& p : target) if (p.size() != tn) return r;
    for (const QVector<float>& p : donor)  if (p.size() != dn) return r;

    // The compared sound: W samples before the hole and W behind it, G away from its edges.
    const qint64 win[2] = { holeStart - G - W, holeEnd + G };
    if (win[0] - targetPos < 0 || win[1] + W - targetPos > tn) return r;
    if (win[0] + expectedShift - S - donorPos < 0 || win[1] + W + expectedShift + S - donorPos > dn) return r;

    const int nShift = int(2 * S + 1), nCh = int(target.size());
    QVector<double> cross(nCh * nShift, 0.0), energy(nCh * nShift, 0.0);
    double targetEnergy = 0.0;
    for (int c = 0; c < nCh; ++c) {
        const float* t = target[c].constData();
        const float* d = donor[c].constData();
        for (int w = 0; w < 2; ++w) {
            const qint64 t0 = win[w] - targetPos;
            for (qint64 i = 0; i < W; ++i) targetEnergy += double(t[t0 + i]) * t[t0 + i];
            for (int k = 0; k < nShift; ++k) {
                const qint64 d0 = win[w] + expectedShift - S + k - donorPos;
                double x = 0.0, e = 0.0;
                for (qint64 i = 0; i < W; ++i) {
                    x += double(t[t0 + i]) * d[d0 + i];
                    e += double(d[d0 + i]) * d[d0 + i];
                }
                cross[c * nShift + k] += x;
                energy[c * nShift + k] += e;
            }
        }
    }

    // The best correlation over all planes; among equals the shift nearest the expected one.
    int best = int(S);
    double bestCc = -2.0;
    for (int k = 0; k < nShift; ++k) {
        double x = 0.0, e = 0.0;
        for (int c = 0; c < nCh; ++c) { x += cross[c * nShift + k]; e += energy[c * nShift + k]; }
        const double den = std::sqrt(e * targetEnergy);
        const double cc = den > 0.0 ? x / den : 0.0;
        if (cc > bestCc || (cc == bestCc && std::abs(k - int(S)) < std::abs(best - int(S)))) { bestCc = cc; best = k; }
    }

    r.valid = true;
    r.shift = expectedShift - S + best;
    r.match = qBound(0.0, bestCc, 1.0);
    for (int c = 0; c < nCh; ++c) {
        const double e = energy[c * nShift + best];
        r.gains.append(r.match > 0.0 && e > 0.0 ? qMax(0.0, cross[c * nShift + best] / e) : 0.0);
    }
    return r;
}

double donorWeight(qint64 pos, qint64 holeStart, qint64 holeEnd, int crossFade)
{
    if (pos < holeStart - crossFade || pos >= holeEnd + crossFade) return 0.0;
    if (pos < holeStart) return 0.5 - 0.5 * std::cos(M_PI * (double(pos - (holeStart - crossFade)) + 0.5) / crossFade);
    if (pos < holeEnd)   return 1.0;
    return 0.5 + 0.5 * std::cos(M_PI * (double(pos - holeEnd) + 0.5) / crossFade);
}

void applyDonor(float* data, int count, qint64 pos0, const QVector<float>& donor, qint64 fillFirst,
                qint64 holeStart, qint64 holeEnd, int crossFade, double gain)
{
    for (int n = 0; n < count; ++n) {
        const qint64 pos = pos0 + n;
        const double w = donorWeight(pos, holeStart, holeEnd, crossFade);
        const qint64 i = pos - fillFirst;
        if (w <= 0.0 || i < 0 || i >= donor.size()) continue;
        data[n] = float((1.0 - w) * data[n] + w * gain * donor[i]);
    }
}

DonorPcm readDonorRange(const QString& donorFile, qint64 firstPos, qint64 count)
{
    DonorPcm r;
    auto fail = [&](const QString& msg, bool noSound = false) {
        r.ok = false; r.noSound = noSound; r.planes.clear(); r.error = msg;
        return r;
    };
    if (count <= 0) return fail(QStringLiteral("empty donor range"));
    if (firstPos < 0)
        return fail(QStringLiteral("the donor range starts before the beginning of the donor track"), true);

    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext*  dec = nullptr;
    AVPacket*        pkt = nullptr;
    AVFrame*         frame = nullptr;
    const auto releaseAll = qScopeGuard([&]() {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmtCtx);
    });

    int audioIdx = -1;
    QString openError;
    if (!TTAudioRepair::openFirstAudioStream(donorFile, &fmtCtx, &audioIdx, &openError))
        return fail(openError);
    const AVCodecParameters* cp = fmtCtx->streams[audioIdx]->codecpar;
    int frameSamples = 0;
    if (cp->codec_id == AV_CODEC_ID_MP2)      frameSamples = 1152;
    else if (cp->codec_id == AV_CODEC_ID_AC3) frameSamples = 1536;
    else return fail(QString("donor codec '%1' is not supported (MP2 or AC3)").arg(avcodec_get_name(cp->codec_id)));

    const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
    dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec || avcodec_parameters_to_context(dec, cp) < 0)
        return fail(QStringLiteral("could not set up the donor decoder"));
    // As TTAc3Reencoder decodes the repaired track: without dynamic range compression.
    if (cp->codec_id == AV_CODEC_ID_AC3) av_opt_set_double(dec->priv_data, "drc_scale", 0.0, 0);
    if (avcodec_open2(dec, codec, nullptr) < 0)
        return fail(QStringLiteral("could not open the donor decoder"));
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) return fail(QStringLiteral("out of memory reading the donor track"));

    const qint64 lastFrame = (firstPos + count - 1) / frameSamples;
    const qint64 warmupStart = qMax<qint64>(0, firstPos / frameSamples - kWarmupFrames);
    r.planes = Planes(2, QVector<float>(count, 0.0f));
    qint64 filled = 0;
    qint64 frameIdx = -1;          // number of the frame in the file
    bool   firstPacket = true;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        if (firstPacket) {
            firstPacket = false;
            // Frames libavformat skipped at the start of the file still count.
            if (pkt->pos > 0) {
                if (pkt->size <= 0 || pkt->pos % pkt->size != 0) {
                    av_packet_unref(pkt);
                    return fail(QStringLiteral("the donor track does not start on a frame"));
                }
                frameIdx = pkt->pos / pkt->size - 1;
            }
        }
        ++frameIdx;
        if (frameIdx < warmupStart) { av_packet_unref(pkt); continue; }
        if (frameIdx > lastFrame)   { av_packet_unref(pkt); break; }

        int ret = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (ret >= 0) ret = avcodec_receive_frame(dec, frame);   // one frame per packet, MP2 and AC3
        if (ret < 0) return fail(QString("donor decode failed at frame %1: %2").arg(frameIdx).arg(avErr(ret)));

        if (frame->ch_layout.nb_channels != 2)
            return fail(QString("the donor track is not two-channel at frame %1").arg(frameIdx), true);
        if (frame->nb_samples != frameSamples)
            return fail(QString("donor frame %1 holds %2 samples, expected %3").arg(frameIdx).arg(frame->nb_samples).arg(frameSamples));
        if (r.sampleRate == 0) r.sampleRate = frame->sample_rate;
        else if (frame->sample_rate != r.sampleRate)
            return fail(QString("the donor's sample rate changes at frame %1").arg(frameIdx), true);

        const qint64 pos0 = frameIdx * frameSamples;
        const qint64 a = qMax(pos0, firstPos), b = qMin(pos0 + frameSamples, firstPos + count);
        for (qint64 p = a; p < b; ++p) {
            for (int c = 0; c < 2; ++c) {
                if (!sampleValue(frame, c, int(p - pos0), &r.planes[c][p - firstPos]))
                    return fail(QStringLiteral("unsupported sample format in the donor track"));
            }
        }
        if (b > a) filled += b - a;
        av_frame_unref(frame);
    }
    if (filled != count)
        return fail(QString("the donor track has no sound for samples %1-%2").arg(firstPos).arg(firstPos + count - 1), true);
    r.ok = true;
    return r;
}

} // namespace TTDonorFill
