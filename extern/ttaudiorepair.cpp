// AC3 replacement-frame builder for audio anomaly repairs (Task 4 of the
// audio-anomaly-repair plan, see
// .superpowers/sdd/2026-08-19-audio-anomaly-repair/task-4-brief.md).
//
// Decodes the source frames named by a TTAudioRepairItem, silences the
// masked channels with 5 ms raised-cosine fades at the range boundaries
// (original value -> 0 at range start, 0 -> original value at range end;
// hard-zero in between), re-encodes on the source's sample rate/bit rate,
// and returns one ready-to-splice AC3 frame per source frame number.
//
// The method "fade-out" (spec 2026-10-05) multiplies every channel by
// fadeOutGain instead: a fade to 0 that ends at an abrupt stop. Both methods
// and the stop search (findStop) decode through the same walk, walkRange.
//
// The method "donor-fill" (spec 2026-10-10) cross-fades the filled channels
// to the sound of a second track over a hole; the search for hole, shift and
// gains is findDonorFill, the math is in ttdonorfill.cpp.
//
// Bitrate and frame size come from the repaired source frames themselves,
// never hardcoded and never from the stream header: 384 kbit/s@48 kHz is
// 1536 bytes/frame, 448 kbit/s is 1792, and one file can switch between
// them (e.g. stereo at 192 kbit/s, 5.1 at 384 kbit/s) - the stream-level
// bit rate is the FIRST frame's and made every repair in a block of another
// bit rate fail (audio-repair.md H8). The AC3 encoder ch_layout is taken from the DECODED frame
// (not codecpar) so this works even if avformat left codecpar's channel
// layout unpopulated for a raw elementary stream.
//
// Decoding, layout conversion, encoding and the frame alignment are
// TTAc3Reencoder's (ttac3reencoder.h): the AC3 encoder delays its input by
// 256 samples, and the re-encoder compensates for that, so the fades below
// land on the first and the last sample of the range. One replacement comes
// out per frame pushed -- a mismatch between requested and produced
// replacement-frame count is an implementation bug, not "encoder behavior".
//
// Abort contract (Task 4 review fix round, C1/I2/I3): a repair range MUST
// be uniform in source channel-mode (acmod/channel count) and source frame
// byte size (CBR bitrate). A channel-mode change inside the range would
// otherwise be silently upmixed/downmixed via swr against the FIRST
// frame's layout (corpus repro: source acmod 7...7 2...2 -> output stayed
// acmod 7 throughout, an unrepaired disturbance with no error reported). A
// frame-size change would desync the byte-for-byte splice the caller does
// against the source file. Both cases return an empty table + errorOut;
// callers already treat that as abort-the-cut (see FrameTable doc in the
// header). This module never silently upmixes, ignores an out-of-range
// channel mask bit, or truncates a short swr conversion.
#include "ttaudiorepair.h"
#include "ttac3reencoder.h"
#include "ttdonorfill.h"

#include <QScopeGuard>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cmath>
#include <functional>

namespace TTAudioRepair {

namespace {

QString avErr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

// Silences the masked channels of one AV_SAMPLE_FMT_FLTP frame in place.
// isFirst applies a fade from the original value down to 0 across the first
// fadeLen samples; isLast applies a fade from 0 back up to the original
// value across the last fadeLen samples. Samples outside an active fade
// window (including the whole frame for a frame that is neither first nor
// last) are hard-set to 0. Matches the calibrated Task 1 spike prototype
// (repair_prototype.py: silence_with_fades()) sample for sample.
void applyMaskAndFade(AVFrame* frame, quint8 channelMask, int fadeLen,
                       bool isFirst, bool isLast)
{
    const int nCh = frame->ch_layout.nb_channels;
    const int nSamples = frame->nb_samples;
    const int fl = std::min(fadeLen, nSamples);
    for (int ch = 0; ch < nCh && ch < 6; ++ch) {
        if (!(channelMask & (1u << ch))) continue;
        float* data = reinterpret_cast<float*>(frame->data[ch]);
        for (int n = 0; n < nSamples; ++n) {
            double gain;
            if (isFirst && n < fl) {
                gain = 0.5 * (1.0 + std::cos(M_PI * n / fl));       // 1 -> 0
            } else if (isLast && n >= nSamples - fl) {
                int m = n - (nSamples - fl);
                gain = 0.5 * (1.0 - std::cos(M_PI * m / fl));       // 0 -> 1
            } else {
                gain = 0.0;
            }
            data[n] = static_cast<float>(data[n] * gain);
        }
    }
}

// Stop search (spec 2026-10-05, section 3). Measured on one real stop and
// the generated fixtures.
constexpr double kStopSearchMinDropDb  = 20.0;   // a boundary below this is no stop
constexpr double kStopSearchLevelDb    = 12.0;   // "sound" = above the level before minus this
constexpr double kStopSearchJumpFactor = 20.0;   // a jump = this many times the usual step
constexpr double kStopSearchJumpWindow = 0.015;  // s before the end of the sound
constexpr double kStopSearchJumpRef    = 0.020;  // s of steps the "usual step" is the median of
constexpr int    kBlock                = 256;

// Called for every frame of the walked range, in order, on its decoded PCM.
using FrameEdit = std::function<bool(qint64 frameIdx, AVFrame* frame, int sampleRate, QString* error)>;

} // namespace

// 5 ms raised-cosine fade length in samples, generic over sample rate
// (240 samples at 48 kHz, the corpus rate -- but never hardcoded).
int fadeLenSamples(int sampleRate)
{
    int n = static_cast<int>(std::lround(0.005 * sampleRate));
    return n < 1 ? 1 : n;
}

bool openFirstAudioStream(const QString& audioFile, AVFormatContext** fmtCtx, int* audioIdx, QString* error)
{
    *fmtCtx = nullptr;
    *audioIdx = -1;
    int ret = avformat_open_input(fmtCtx, audioFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        if (error) *error = QString("Could not open %1: %2").arg(audioFile, avErr(ret));
        return false;
    }
    ret = avformat_find_stream_info(*fmtCtx, nullptr);
    if (ret < 0) {
        if (error) *error = QString("Could not find stream info: %1").arg(avErr(ret));
        avformat_close_input(fmtCtx);
        return false;
    }
    for (unsigned i = 0; i < (*fmtCtx)->nb_streams; ++i) {
        if ((*fmtCtx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            *audioIdx = static_cast<int>(i);
            break;
        }
    }
    if (*audioIdx < 0) {
        if (error) *error = QStringLiteral("no audio stream found");
        avformat_close_input(fmtCtx);
        return false;
    }
    return true;
}

namespace {

// The sample rate of the file's first audio stream; 0 with *error set.
int sampleRateOf(const QString& audioFile, QString* error)
{
    AVFormatContext* fmtCtx = nullptr;
    int audioIdx = -1;
    if (!openFirstAudioStream(audioFile, &fmtCtx, &audioIdx, error)) return 0;
    const int rate = fmtCtx->streams[audioIdx]->codecpar->sample_rate;
    avformat_close_input(&fmtCtx);
    if (rate <= 0) { *error = QStringLiteral("invalid sample rate"); return 0; }
    return rate;
}

// The planes a donor fill writes for this layout: left and right of a 2/0
// stream, the centre of a 3/2 stream (with or without LFE). Empty: neither.
QVector<int> donorFillPlanes(const AVChannelLayout& layout)
{
    if (layout.order != AV_CHANNEL_ORDER_NATIVE) return {};
    if (layout.u.mask == AV_CH_LAYOUT_STEREO) return {0, 1};
    const uint64_t mainChannels = layout.u.mask & ~uint64_t(AV_CH_LOW_FREQUENCY);
    if (mainChannels != AV_CH_LAYOUT_5POINT0 && mainChannels != AV_CH_LAYOUT_5POINT0_BACK) return {};
    const int centre = av_channel_layout_index_from_channel(&layout, AV_CHAN_FRONT_CENTER);
    return centre >= 0 ? QVector<int>{centre} : QVector<int>{};
}

quint8 maskOfPlanes(const QVector<int>& planes)
{
    quint8 mask = 0;
    for (int p : planes) mask |= quint8(1u << p);
    return mask;
}

// The donor's sound per filled channel: its mid (L+R)/2 for the centre, L
// and R for a stereo track.
QVector<QVector<float>> donorSoundFor(quint8 channelMask, const TTDonorFill::Planes& donor)
{
    if (channelMask != kDonorFillMaskCentre) return donor;
    QVector<float> mid(donor[0].size());
    for (qsizetype i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (donor[0][i] + donor[1][i]);
    return {mid};
}

// Decodes frames frameFrom..frameTo (with warm-up before and one frame of
// lookahead behind), hands the PCM of each to frameEdit and returns the
// re-encoded frames. The range must be uniform in channel layout and frame
// size (abort contract above). *formatChanged is set when the walk fails
// because channel layout or frame size change inside the range.
FrameTable walkRange(const QString& audioFile, qint64 frameFrom, qint64 frameTo,
                     int targetAcmod, const FrameEdit& frameEdit, QString* errorOut,
                     bool* formatChanged = nullptr)
{
    if (errorOut) errorOut->clear();
    if (formatChanged) *formatChanged = false;
    auto fail = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        return FrameTable();
    };

    if (frameFrom < 0 || frameTo < frameFrom) {
        return fail(QStringLiteral("invalid frame range"));
    }

    AVFormatContext* fmtCtx = nullptr;
    AVPacket*        pkt    = nullptr;
    AVChannelLayout  sourceRefLayout = {};  // established at the first in-range frame
    const auto releaseAll = qScopeGuard([&]() {
        av_packet_free(&pkt);
        av_channel_layout_uninit(&sourceRefLayout);
        avformat_close_input(&fmtCtx);
    });

    int audioIdx = -1;
    QString openError;
    if (!openFirstAudioStream(audioFile, &fmtCtx, &audioIdx, &openError))
        return fail(openError);
    AVCodecParameters* cp = fmtCtx->streams[audioIdx]->codecpar;
    if (cp->sample_rate <= 0)
        return fail(QStringLiteral("invalid sample rate"));

    TTAc3Reencoder reencoder;
    QString reError;
    if (!reencoder.open(cp, &reError))
        return fail(reError);

    // Decoder warm-up before frameFrom (the synthesis filterbank carries
    // overlap state across frames); one frame behind frameTo supplies the
    // 256 samples that complete the last replacement.
    const qint64 warmupStart = frameFrom >= 2 ? frameFrom - 2 : 0;
    const qint64 expectedCount = frameTo - frameFrom + 1;

    pkt = av_packet_alloc();
    if (!pkt)
        return fail(QStringLiteral("out of memory allocating the AC3 packet buffer"));
    FrameTable table;
    qint64 frameIdx = -1;
    qint64 sourceFrameSize = -1; // CBR byte size, captured from the first touched packet

    // Channel-mode consistency (C1), checked on the decoded frame: the
    // range must be uniform in source channel layout. Then the caller's edit.
    const TTAc3Reencoder::PcmEdit edit = [&](AVFrame* frame, QString* error) {
        if (frameIdx == frameFrom) {
            av_channel_layout_copy(&sourceRefLayout, &frame->ch_layout);
        } else if (av_channel_layout_compare(&frame->ch_layout, &sourceRefLayout) != 0) {
            *error = QString("repair range spans a channel-mode change at frame %1").arg(frameIdx);
            if (formatChanged) *formatChanged = true;
            return false;
        }
        return frameEdit(frameIdx, frame, cp->sample_rate, error);
    };

    // I2 splice invariant: a replacement must have the source's CBR frame
    // size, or the caller's byte-offset splice corrupts the stream.
    QList<TTAc3Reencoder::Replacement> finished;
    auto takeFinished = [&](QString* error) {
        for (const TTAc3Reencoder::Replacement& r : finished) {
            if (r.bytes.size() != sourceFrameSize) {
                *error = QString("encoded replacement frame size mismatch at frame %1: "
                                 "got %2 bytes, source frames are %3 bytes")
                             .arg(r.tag).arg(r.bytes.size()).arg(sourceFrameSize);
                return false;
            }
            table.insert(r.tag, r.bytes);
        }
        finished.clear();
        return true;
    };

    TTAc3Reencoder::Request request;
    request.targetAcmod = targetAcmod;
    bool sawFrameBehind = false;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        ++frameIdx;
        if (frameIdx < warmupStart) { av_packet_unref(pkt); continue; }

        if (frameIdx > frameTo) {
            // The frame behind the range: lookahead only, exempt from the
            // uniformity checks below.
            const bool ok = reencoder.push(pkt, false, request, frameIdx, {}, &finished, &reError);
            av_packet_unref(pkt);
            if (!ok) return fail(reError);
            sawFrameBehind = true;
            break;
        }

        // Splice invariant (I2): every source frame touched (warm-up and
        // content) must be the same CBR byte size.
        if (sourceFrameSize < 0) {
            sourceFrameSize = pkt->size;
            // The bit rate of the repaired frames, from their size: an AC3
            // frame carries 1536 samples (1536 B -> 384 kbit/s at 48 kHz).
            request.bitRate = sourceFrameSize * 8 * cp->sample_rate / 1536;
        } else if (pkt->size != sourceFrameSize) {
            const QString msg = QString("source frame size changed within the repair range at frame %1 "
                                        "(%2 vs %3 bytes) -- not a constant-bitrate region")
                                    .arg(frameIdx).arg(pkt->size).arg(sourceFrameSize);
            if (formatChanged) *formatChanged = true;
            av_packet_unref(pkt);
            return fail(msg);
        }

        const bool inRange = frameIdx >= frameFrom;
        const bool ok = reencoder.push(pkt, inRange, request, frameIdx,
                                       inRange ? edit : TTAc3Reencoder::PcmEdit(), &finished, &reError);
        av_packet_unref(pkt);
        if (!ok)
            return fail(reError.contains("frame") ? reError
                                                  : QString("%1 (frame %2)").arg(reError).arg(frameIdx));
        if (!takeFinished(&reError)) return fail(reError);
    }
    // The file ended with the range: silence completes the last frame.
    if (!sawFrameBehind && !reencoder.finish(&finished, &reError))
        return fail(reError);
    if (!takeFinished(&reError)) return fail(reError);

    if (table.size() != expectedCount) {
        // Two very different causes, and calling both an implementation bug
        // sent the reader hunting in the encoder (final review M3):
        //
        // a) The file simply ends before frameTo - a repair range saved
        //    against a longer AC3 (recording re-demuxed/replaced). frameIdx
        //    holds the last frame number the demuxer delivered, so
        //    frameIdx < frameTo means we ran into EOF, not a logic error.
        // b) Anything else: the range was fully read but produced too few
        //    replacement frames - that IS an implementation bug.
        if (frameIdx < frameTo) {
            return fail(QString("repair range %1-%2 reaches past the end of the audio file "
                                 "(it holds %3 frames) -- the recording was probably "
                                 "re-demuxed or replaced after the project was saved")
                            .arg(frameFrom).arg(frameTo).arg(frameIdx + 1));
        }
        return fail(QString("replacement-frame count mismatch: expected %1, got %2 "
                             "(implementation bug)")
                        .arg(expectedCount).arg(table.size()));
    }
    return table;
}

// The PCM of frames first..last, decoded as buildRepairTable decodes them.
struct Ac3Pcm {
    TTDonorFill::Planes planes;     // every channel
    QVector<int> fillPlanes;        // donorFillPlanes() of the layout
    int rate = 0;
};

bool readAc3Planes(const QString& audioFile, qint64 first, qint64 last, Ac3Pcm* out, bool* formatChanged, QString* error)
{
    *out = Ac3Pcm();
    const FrameEdit read = [&](qint64, AVFrame* f, int sampleRate, QString*) {
        if (out->planes.isEmpty()) {
            out->planes.resize(f->ch_layout.nb_channels);
            out->fillPlanes = donorFillPlanes(f->ch_layout);
            out->rate = sampleRate;
        }
        for (int ch = 0; ch < f->ch_layout.nb_channels && ch < out->planes.size(); ++ch) {
            const float* d = reinterpret_cast<const float*>(f->extended_data[ch]);
            for (int n = 0; n < f->nb_samples; ++n) out->planes[ch].append(d[n]);
        }
        return true;
    };
    walkRange(audioFile, first, last, -1, read, error, formatChanged);
    return error->isEmpty();
}

} // namespace

double fadeOutGain(qint64 pos, qint64 fadeEnd, int fadeLen, int silenceLen, int fadeInLen)
{
    const qint64 fadeStart = fadeEnd - fadeLen, silenceEnd = fadeEnd + silenceLen;
    if (pos < fadeStart)  return 1.0;
    if (pos < fadeEnd)    return 0.5 * (1.0 + std::cos(M_PI * double(pos - fadeStart) / fadeLen));
    if (pos < silenceEnd) return 0.0;
    if (pos < silenceEnd + fadeInLen)
        return 0.5 * (1.0 - std::cos(M_PI * double(pos - silenceEnd) / fadeInLen));
    return 1.0;
}

TTAudioRepairItem makeFadeOutItem(int track, quint8 channelMask, qint64 fadeEnd,
                                  int fadeLen, int silenceLen, int sampleRate)
{
    const qint64 first = fadeEnd - fadeLen;
    const qint64 last  = fadeEnd + silenceLen + fadeLenSamples(sampleRate) - 1;
    TTAudioRepairItem item(track, first >= 0 ? first / kAc3FrameSamples : -1,
                           last / kAc3FrameSamples, channelMask);
    item.setFadeOut(fadeEnd, fadeLen, silenceLen);
    return item;
}

TTAudioRepairItem makeDonorFillItem(int track, quint8 channelMask, int donorTrack, qint64 holeStart, qint64 holeEnd,
                                    qint64 donorShift, const QVector<double>& gains, double match, int sampleRate)
{
    const int crossFade = TTDonorFill::crossFadeSamples(sampleRate);
    const qint64 first = holeStart - crossFade, last = holeEnd + crossFade - 1;
    TTAudioRepairItem item(track, first >= 0 ? first / kAc3FrameSamples : -1, last / kAc3FrameSamples, channelMask);
    item.setDonorFill(donorTrack, holeStart, holeEnd, donorShift, gains, match);
    return item;
}

QString donorFillProblem(const TTAudioRepairItem& item, int sampleRate)
{
    if (!item.isDonorFill()) return QStringLiteral("not a donor-fill repair");
    const quint8 mask = item.channelMask();
    if (mask != kDonorFillMaskCentre && mask != kDonorFillMaskStereo)
        return QString("donor fill: channel mask 0x%1 is neither the centre nor left+right").arg(mask, 0, 16);
    const QVector<double> gains = item.gains();
    if (gains.size() != (mask == kDonorFillMaskStereo ? 2 : 1))
        return QString("donor fill: %1 gain value(s) for %2 filled channel(s)")
            .arg(gains.size()).arg(mask == kDonorFillMaskStereo ? 2 : 1);
    for (double g : gains)
        if (!std::isfinite(g) || g < 0.0 || g > kDonorFillMaxGain)
            return QString("donor fill: gain %1 is outside 0..%2").arg(g).arg(kDonorFillMaxGain);
    // No track holds a week of samples: a position or shift beyond that is
    // refused before anything is computed from it.
    const qint64 limit = qint64(sampleRate > 0 ? sampleRate : 48000) * 86400 * 7;
    if (item.holeStart() < 0 || item.holeStart() > limit || item.holeEnd() < 0 || item.holeEnd() > limit)
        return QString("donor fill: a hole at samples %1-%2 is not possible").arg(item.holeStart()).arg(item.holeEnd());
    if (item.donorShift() < -limit || item.donorShift() > limit)
        return QString("donor fill: a shift of %1 samples is not possible").arg(item.donorShift());
    // A hole is found inside five frames (the marker's three and one on each side).
    if (item.holeEnd() <= item.holeStart() || item.holeEnd() - item.holeStart() > 5 * kAc3FrameSamples)
        return QString("donor fill: a hole of %1 samples is not possible").arg(item.holeEnd() - item.holeStart());
    const TTAudioRepairItem want = makeDonorFillItem(item.trackIndex(), mask, item.donorTrack(), item.holeStart(),
                                                     item.holeEnd(), item.donorShift(), gains, item.match(), sampleRate);
    if (want.frameFrom() < 0)
        return QStringLiteral("donor fill: the hole lies before the beginning of the track");
    if (want.frameFrom() != item.frameFrom() || want.frameTo() != item.frameTo())
        return QString("donor-fill values do not fit the frame range %1-%2 (they need %3-%4)")
            .arg(item.frameFrom()).arg(item.frameTo()).arg(want.frameFrom()).arg(want.frameTo());
    return QString();
}

StopPlacement locateStop(const QVector<QVector<float>>& planes, qint64 firstPos,
                         qint64 from, qint64 to, int sampleRate)
{
    StopPlacement r;
    if (planes.isEmpty() || sampleRate <= 0) return r;
    const qint64 n = planes[0].size();
    const qint64 lo = qMax<qint64>(0, from - firstPos);

    QVector<double> cs(n + 1, 0.0);                     // prefix sums of the power of all planes
    for (qint64 i = 0; i < n; ++i) {
        double p = 0.0;
        for (const QVector<float>& pl : planes) p += double(pl[i]) * pl[i];
        cs[i + 1] = cs[i] + p;
    }
    auto mean = [&](qint64 a, qint64 b) { return (cs[b] - cs[a]) / double(b - a); };
    auto toDb = [](double p) { return p > 1e-18 ? 10.0 * std::log10(p) : -180.0; };

    // 1. The block boundary with the largest drop, on the scan's own grid.
    double bestDrop = -1e9;
    qint64 b = -1;
    for (qint64 pos = ((from + kBlock - 1) / kBlock) * kBlock; pos < to; pos += kBlock) {
        const qint64 i = pos - firstPos;
        if (i - 4 * kBlock < 0 || i + 2 * kBlock > n) continue;
        const double drop = toDb(mean(i - 3 * kBlock, i)) - toDb(mean(i, i + 2 * kBlock));
        if (drop > bestDrop) { bestDrop = drop; b = i; }
    }
    if (b < 0 || bestDrop < kStopSearchMinDropDb) return r;

    // 2. Walk back from the quiet side to where the sound ends.
    const double rmsBefore = std::sqrt(mean(b - 4 * kBlock, b - kBlock) / planes.size());
    const float  threshold = float(rmsBefore * std::pow(10.0, -kStopSearchLevelDb / 20.0));
    auto magnitude = [&](qint64 i) { float m = 0.0f; for (const QVector<float>& pl : planes) m = qMax(m, std::fabs(pl[i])); return m; };
    qint64 d = qMin(n, b + kBlock);
    while (d > lo && magnitude(d - 1) <= threshold) --d;

    // 3. The earliest jump shortly before it.
    auto step = [&](qint64 i) { float s = 0.0f; for (const QVector<float>& pl : planes) s = qMax(s, std::fabs(pl[i] - pl[i - 1])); return s; };
    const qint64 window = qint64(kStopSearchJumpWindow * sampleRate);
    const qint64 ref    = qint64(kStopSearchJumpRef * sampleRate);
    qint64 first = d;
    QVector<float> usual;
    for (qint64 q = qMax(d - window, ref + 1); q < d; ++q) {
        usual.resize(0);
        for (qint64 k = q - ref; k < q; ++k) usual.append(step(k));
        std::nth_element(usual.begin(), usual.begin() + usual.size() / 2, usual.end());
        if (step(q) >= kStopSearchJumpFactor * qMax(usual[usual.size() / 2], 1e-9f)) { first = q; break; }
    }

    r.found    = true;
    r.soundEnd = d + firstPos;
    r.fadeEnd  = first + firstPos;
    r.silence  = int(d - first) + sampleRate / 1000;
    return r;
}

FrameTable buildRepairTable(const QString& audioFile, const TTAudioRepairItem& item,
                            int targetAcmod, QString* errorOut, const QString& donorFile)
{
    auto fail = [&](const QString& msg) { if (errorOut) *errorOut = msg; return FrameTable(); };
    if (errorOut) errorOut->clear();

    const bool fadeOut = item.isFadeOut();
    const bool donorFill = item.isDonorFill();
    if (!fadeOut && !donorFill && item.method() != QLatin1String(TTAudioRepairItem::kMethodSilenceFade))
        return fail(QString("unknown repair method '%1'").arg(item.method()));
    if (fadeOut) {
        if (item.fadeLength() <= 0 || item.silenceLength() <= 0)
            return fail(QStringLiteral("invalid fade-out values"));
        if (item.fadeEnd() - item.fadeLength() < 0)
            return fail(QStringLiteral("the fade-out would start before the beginning of the track"));
    }

    // Donor fill: the donor's sound for the hole and its cross-fades, one
    // vector per filled channel, read before the walk.
    QVector<QVector<float>> donorSound;
    const QVector<double> gains = item.gains();
    int crossFade = 0, donorRate = 0;
    if (donorFill) {
        if (donorFile.isEmpty()) return fail(QStringLiteral("donor fill: no donor track given"));
        QString rateError;
        const int rate = sampleRateOf(audioFile, &rateError);
        if (rate <= 0) return fail(rateError);
        const QString problem = donorFillProblem(item, rate);
        if (!problem.isEmpty()) return fail(problem);
        crossFade = TTDonorFill::crossFadeSamples(rate);
        const TTDonorFill::DonorPcm donor = TTDonorFill::readDonorRange(
            donorFile, item.holeStart() - crossFade + item.donorShift(),
            item.holeEnd() - item.holeStart() + 2 * crossFade);
        if (!donor.ok) return fail(QString("donor fill: %1").arg(donor.error));
        donorRate = donor.sampleRate;
        donorSound = donorSoundFor(item.channelMask(), donor.planes);
    }

    const FrameEdit edit = [&](qint64 frameIdx, AVFrame* frame, int sampleRate, QString* error) {
        const int fadeLen = fadeLenSamples(sampleRate);
        if (donorFill) {
            const QVector<int> planes = donorFillPlanes(frame->ch_layout);
            if (frameIdx == item.frameFrom()) {
                if (planes.isEmpty() || maskOfPlanes(planes) != item.channelMask()) {
                    *error = QString("donor fill: channel mask 0x%1 does not fit the channel layout at frame %2")
                                 .arg(item.channelMask(), 0, 16).arg(frameIdx);
                    return false;
                }
                if (sampleRate != donorRate) {
                    *error = QString("donor fill: the donor's sample rate %1 differs from the track's %2")
                                 .arg(donorRate).arg(sampleRate);
                    return false;
                }
            }
            for (int k = 0; k < planes.size() && k < donorSound.size(); ++k)
                TTDonorFill::applyDonor(reinterpret_cast<float*>(frame->extended_data[planes[k]]), frame->nb_samples,
                                        frameIdx * kAc3FrameSamples, donorSound[k], item.holeStart() - crossFade,
                                        item.holeStart(), item.holeEnd(), crossFade, gains[k]);
            return true;
        }
        if (fadeOut) {
            if (frameIdx == item.frameFrom()) {
                const TTAudioRepairItem want = makeFadeOutItem(item.trackIndex(), item.channelMask(), item.fadeEnd(),
                                                               item.fadeLength(), item.silenceLength(), sampleRate);
                if (want.frameFrom() != item.frameFrom() || want.frameTo() != item.frameTo()) {
                    *error = QString("fade-out values do not fit the frame range %1-%2 (they need %3-%4)")
                                 .arg(item.frameFrom()).arg(item.frameTo()).arg(want.frameFrom()).arg(want.frameTo());
                    return false;
                }
            }
            const qint64 pos0 = frameIdx * kAc3FrameSamples;
            for (int ch = 0; ch < frame->ch_layout.nb_channels; ++ch) {
                float* data = reinterpret_cast<float*>(frame->extended_data[ch]);
                for (int n = 0; n < frame->nb_samples; ++n)
                    data[n] = float(data[n] * fadeOutGain(pos0 + n, item.fadeEnd(), item.fadeLength(),
                                                          item.silenceLength(), fadeLen));
            }
            return true;
        }
        // A mask bit must refer to a channel the stream has.
        if (frameIdx == item.frameFrom()) {
            const int nCh = frame->ch_layout.nb_channels;
            const quint8 validMask = (nCh >= 8) ? quint8(0xFF) : quint8((1u << nCh) - 1);
            if (item.channelMask() & ~validMask) {
                *error = QString("channel mask 0x%1 references channel(s) beyond the "
                                 "stream's %2 channel(s) at frame %3")
                             .arg(item.channelMask(), 0, 16).arg(nCh).arg(frameIdx);
                return false;
            }
        }
        applyMaskAndFade(frame, item.channelMask(), fadeLen,
                         frameIdx == item.frameFrom(), frameIdx == item.frameTo());
        return true;
    };
    return walkRange(audioFile, item.frameFrom(), item.frameTo(), targetAcmod, edit, errorOut);
}

StopPlacement findStop(const QString& audioFile, qint64 frameFrom, qint64 frameTo, QString* errorOut)
{
    if (errorOut) errorOut->clear();
    const qint64 first = qMax<qint64>(0, frameFrom - 1);
    QVector<QVector<float>> planes;
    int rate = 0;
    const FrameEdit read = [&](qint64, AVFrame* f, int sampleRate, QString*) {
        rate = sampleRate;
        const int lfe = av_channel_layout_index_from_channel(&f->ch_layout, AV_CHAN_LOW_FREQUENCY);
        int p = 0;
        for (int ch = 0; ch < f->ch_layout.nb_channels; ++ch) {
            if (ch == lfe) continue;
            if (planes.size() <= p) planes.resize(p + 1);
            const float* d = reinterpret_cast<const float*>(f->extended_data[ch]);
            for (int n = 0; n < f->nb_samples; ++n) planes[p].append(d[n]);
            ++p;
        }
        return true;
    };
    // One frame behind the marker's frames when the track has it.
    QString err;
    walkRange(audioFile, first, frameTo + 1, -1, read, &err);
    if (!err.isEmpty()) {
        planes.clear();
        walkRange(audioFile, first, frameTo, -1, read, &err);
    }
    if (!err.isEmpty()) { if (errorOut) *errorOut = err; return StopPlacement(); }
    return locateStop(planes, first * kAc3FrameSamples, frameFrom * kAc3FrameSamples,
                      (frameTo + 1) * kAc3FrameSamples, rate);
}

DonorFillSearch findDonorFill(const QString& audioFile, int track, qint64 frameFrom, qint64 frameTo,
                              const QString& donorFile, int donorTrack, qint64 expectedShift)
{
    DonorFillSearch r;
    auto done = [&](DonorFillStatus status, const QString& detail = QString()) {
        r.status = status;
        r.error = detail;
        return r;
    };
    if (frameFrom < 0 || frameTo < frameFrom) return done(DonorFillStatus::Error, QStringLiteral("invalid frame range"));

    // 1. The hole: in the marker's frames and one frame on each side. The
    //    planes reach one frame further (the search looks four blocks back);
    //    at the end of the track there may be fewer frames behind.
    const qint64 first = qMax<qint64>(0, frameFrom - 2);
    Ac3Pcm pcm;
    QString err;
    bool formatChanged = false;
    qint64 last = -1;
    for (qint64 tryLast = frameTo + 2; tryLast >= frameTo && last < 0; --tryLast) {
        if (readAc3Planes(audioFile, first, tryLast, &pcm, &formatChanged, &err)) last = tryLast;
        else if (formatChanged) return done(DonorFillStatus::FormatChange, err);
    }
    if (last < 0) return done(DonorFillStatus::Error, err);
    if (pcm.fillPlanes.isEmpty()) return done(DonorFillStatus::UnsupportedLayout);

    TTDonorFill::Planes target;
    for (int p : pcm.fillPlanes) target.append(pcm.planes[p]);
    const TTDonorFill::HolePlacement hole = TTDonorFill::locateHole(
        target, first * kAc3FrameSamples,
        qMax(first, frameFrom - 1) * kAc3FrameSamples, qMin(last + 1, frameTo + 2) * kAc3FrameSamples, pcm.rate);
    if (!hole.found) return done(DonorFillStatus::NoHole);

    // 2. The sound next to the hole, in both tracks.
    const int rate = pcm.rate;
    const qint64 reach = TTDonorFill::guardSamples(rate) + TTDonorFill::compareSamples(rate);
    const qint64 search = TTDonorFill::searchSamples(rate);
    const qint64 cmpFrom = hole.start - reach, cmpTo = hole.end + reach;        // [cmpFrom, cmpTo)
    if (cmpFrom < 0) return done(DonorFillStatus::NoSound, QStringLiteral("the hole lies too close to the start of the track"));
    const qint64 cmpFirst = cmpFrom / kAc3FrameSamples, cmpLast = (cmpTo - 1) / kAc3FrameSamples;
    Ac3Pcm wide;
    if (!readAc3Planes(audioFile, cmpFirst, cmpLast, &wide, &formatChanged, &err))
        return done(formatChanged ? DonorFillStatus::FormatChange : DonorFillStatus::NoSound, err);
    target.clear();
    for (int p : wide.fillPlanes) target.append(wide.planes[p]);
    const quint8 mask = maskOfPlanes(wide.fillPlanes);

    const qint64 donorFrom = cmpFrom + expectedShift - search;
    const TTDonorFill::DonorPcm donor = TTDonorFill::readDonorRange(donorFile, donorFrom, cmpTo - cmpFrom + 2 * search);
    if (!donor.ok) return done(donor.noSound ? DonorFillStatus::NoSound : DonorFillStatus::DonorUnreadable, donor.error);
    if (donor.sampleRate != rate)
        return done(DonorFillStatus::Error, QString("the donor's sample rate %1 differs from the track's %2").arg(donor.sampleRate).arg(rate));

    // 3. Shift and gains.
    const TTDonorFill::DonorFit fit = TTDonorFill::fitDonor(target, cmpFirst * kAc3FrameSamples,
                                                            donorSoundFor(mask, donor.planes), donorFrom,
                                                            hole.start, hole.end, expectedShift, rate);
    if (!fit.valid) return done(DonorFillStatus::NoSound, QStringLiteral("too little sound next to the hole"));

    r.item = makeDonorFillItem(track, mask, donorTrack, hole.start, hole.end, fit.shift, fit.gains, fit.match, rate);
    return done(DonorFillStatus::Found);
}

} // namespace TTAudioRepair
