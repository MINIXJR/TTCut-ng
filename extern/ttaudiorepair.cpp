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

#include <QScopeGuard>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cmath>

namespace TTAudioRepair {

namespace {

QString avErr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

// 5 ms raised-cosine fade length in samples, generic over sample rate
// (240 samples at 48 kHz, the corpus rate -- but never hardcoded).
int fadeLenSamples(int sampleRate)
{
    int n = static_cast<int>(std::lround(0.005 * sampleRate));
    return n < 1 ? 1 : n;
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

} // namespace

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

FrameTable buildRepairTable(const QString& audioFile,
                             const TTAudioRepairItem& item,
                             int targetAcmod,
                             QString* errorOut)
{
    if (errorOut) errorOut->clear();
    auto fail = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        return FrameTable();
    };

    if (item.frameFrom() < 0 || item.frameTo() < item.frameFrom()) {
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

    const int fadeLen = fadeLenSamples(cp->sample_rate);
    // Decoder warm-up before frameFrom (the synthesis filterbank carries
    // overlap state across frames); one frame behind frameTo supplies the
    // 256 samples that complete the last replacement.
    const qint64 warmupStart = item.frameFrom() >= 2 ? item.frameFrom() - 2 : 0;
    const qint64 expectedCount = item.frameTo() - item.frameFrom() + 1;

    pkt = av_packet_alloc();
    if (!pkt)
        return fail(QStringLiteral("out of memory allocating the AC3 packet buffer"));
    FrameTable table;
    qint64 frameIdx = -1;
    qint64 sourceFrameSize = -1; // CBR byte size, captured from the first touched packet

    // Channel-mode consistency (C1) and the mask, checked on the decoded
    // frame: the repair range must be uniform in source channel layout, and
    // a mask bit must refer to a channel the stream has. Then mask and fades.
    const TTAc3Reencoder::PcmEdit edit = [&](AVFrame* frame, QString* error) {
        if (frameIdx == item.frameFrom()) {
            av_channel_layout_copy(&sourceRefLayout, &frame->ch_layout);
            const int nCh = frame->ch_layout.nb_channels;
            const quint8 validMask = (nCh >= 8) ? quint8(0xFF) : quint8((1u << nCh) - 1);
            if (item.channelMask() & ~validMask) {
                *error = QString("channel mask 0x%1 references channel(s) beyond the "
                                 "stream's %2 channel(s) at frame %3")
                             .arg(item.channelMask(), 0, 16).arg(nCh).arg(frameIdx);
                return false;
            }
        } else if (av_channel_layout_compare(&frame->ch_layout, &sourceRefLayout) != 0) {
            *error = QString("repair range spans a channel-mode change at frame %1").arg(frameIdx);
            return false;
        }
        applyMaskAndFade(frame, item.channelMask(), fadeLen,
                         frameIdx == item.frameFrom(), frameIdx == item.frameTo());
        return true;
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

        if (frameIdx > item.frameTo()) {
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
            av_packet_unref(pkt);
            return fail(msg);
        }

        const bool inRange = frameIdx >= item.frameFrom();
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
        if (frameIdx < item.frameTo()) {
            return fail(QString("repair range %1-%2 reaches past the end of the audio file "
                                 "(it holds %3 frames) -- the recording was probably "
                                 "re-demuxed or replaced after the project was saved")
                            .arg(item.frameFrom()).arg(item.frameTo()).arg(frameIdx + 1));
        }
        return fail(QString("replacement-frame count mismatch: expected %1, got %2 "
                             "(implementation bug)")
                        .arg(expectedCount).arg(table.size()));
    }
    return table;
}

} // namespace TTAudioRepair
