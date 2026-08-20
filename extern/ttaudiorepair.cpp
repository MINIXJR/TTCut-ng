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
// Bitrate and frame size are read from the opened stream, never hardcoded:
// 384 kbit/s@48 kHz is 1536 bytes/frame, but corpus material also exists at
// 448 kbit/s (1792 bytes/frame) -- see CLAUDE.md calibration note in the
// Task 4 brief. The AC3 encoder ch_layout is taken from the DECODED frame
// (not codecpar) so this works even if avformat left codecpar's channel
// layout unpopulated for a raw elementary stream.
//
// Frame-exact 1:1 decode/encode (no encoder priming/delay) was verified in
// the Task 1 calibration spike (repair_prototype.py) -- a mismatch between
// requested and produced replacement-frame count is therefore treated as an
// implementation bug here, not tolerated as "encoder behavior".
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

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
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
    int ret = avformat_open_input(&fmtCtx, audioFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        return fail(QString("Could not open %1: %2").arg(audioFile, avErr(ret)));
    }
    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        QString msg = QString("Could not find stream info: %1").arg(avErr(ret));
        avformat_close_input(&fmtCtx);
        return fail(msg);
    }

    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = static_cast<int>(i);
            break;
        }
    }
    if (audioIdx < 0) {
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("no audio stream found"));
    }
    AVStream* inStream = fmtCtx->streams[audioIdx];
    AVCodecParameters* cp = inStream->codecpar;
    if (cp->sample_rate <= 0) {
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("invalid sample rate"));
    }
    // Bit rate from the source, never hardcoded/faked (384k/448k both occur
    // in the corpus): an unknown bit rate would silently produce
    // wrong-sized replacement frames that can't splice byte-for-byte back
    // into the source, so it is a hard error, not a 384k guess (I2).
    if (cp->bit_rate <= 0) {
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("could not determine source bit rate"));
    }
    const int64_t bitRate = cp->bit_rate;

    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* decCtx = dec ? avcodec_alloc_context3(dec) : nullptr;
    if (!decCtx ||
        avcodec_parameters_to_context(decCtx, cp) < 0 ||
        avcodec_open2(decCtx, dec, nullptr) < 0) {
        if (decCtx) avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("could not open AC3 decoder"));
    }

    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_AC3);
    if (!enc) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("AC3 encoder not available"));
    }

    AVCodecContext* encCtx = nullptr;   // created lazily from the first decoded frame
    SwrContext* swrCtx = nullptr;
    AVFrame* convFrame = nullptr;
    AVChannelLayout sourceRefLayout = {}; // established at the first in-range frame

    const int fadeLen = fadeLenSamples(cp->sample_rate);
    // 2-frame decoder warm-up before frameFrom (AC3 has no DPB, but the
    // synthesis filterbank carries overlap state across frames).
    const qint64 warmupStart = item.frameFrom() >= 2 ? item.frameFrom() - 2 : 0;
    const qint64 expectedCount = item.frameTo() - item.frameFrom() + 1;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    // Allocation failure is an out-of-memory condition, not something the
    // loop below could survive: pkt/frame are dereferenced unconditionally on
    // the first iteration. Report it as the error the caller must abort on
    // rather than crashing on a null pointer (final review M10).
    if (!pkt || !frame) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return fail(QStringLiteral("out of memory allocating the AC3 packet/frame buffers"));
    }
    FrameTable table;
    qint64 frameIdx = -1;
    qint64 sourceFrameSize = -1; // CBR byte size, captured from the first touched packet
    bool failed = false;
    QString errMsg;

    while (!failed && av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }
        ++frameIdx;
        if (frameIdx < warmupStart) { av_packet_unref(pkt); continue; }
        if (frameIdx > item.frameTo()) { av_packet_unref(pkt); break; }

        // Splice invariant (I2): every source frame touched (warm-up and
        // content) must be the same CBR byte size, or the byte-for-byte
        // splice the caller does against the source file desyncs.
        if (sourceFrameSize < 0) {
            sourceFrameSize = pkt->size;
        } else if (pkt->size != sourceFrameSize) {
            failed = true;
            errMsg = QString("source frame size changed within the repair range at frame %1 "
                              "(%2 vs %3 bytes) -- not a constant-bitrate region")
                         .arg(frameIdx).arg(pkt->size).arg(sourceFrameSize);
            av_packet_unref(pkt);
            break;
        }

        int sendRet = avcodec_send_packet(decCtx, pkt);
        av_packet_unref(pkt);
        if (sendRet < 0) {
            failed = true;
            errMsg = QString("AC3 decode send_packet failed at frame %1: %2")
                         .arg(frameIdx).arg(avErr(sendRet));
            break;
        }
        int decRet = avcodec_receive_frame(decCtx, frame);
        if (decRet < 0) {
            failed = true;
            errMsg = QString("AC3 decode produced no frame at %1: %2")
                         .arg(frameIdx).arg(avErr(decRet));
            break;
        }

        if (frameIdx < item.frameFrom()) {
            av_frame_unref(frame);
            continue; // decoder warm-up only, discarded from the table
        }
        if (frame->format != AV_SAMPLE_FMT_FLTP) {
            failed = true;
            errMsg = QStringLiteral("AC3 decoder produced an unexpected sample format");
            av_frame_unref(frame);
            break;
        }

        // Channel-mode consistency (C1): the repair range must be uniform
        // in source channel layout. Established from the first in-range
        // frame; any later frame that differs (acmod/channel-count change,
        // e.g. a mid-range switch to a commercial break's 2.0 track) aborts
        // the whole table rather than being silently up/downmixed against
        // the first frame's layout. The channel mask is validated against
        // the SAME first-frame channel count: a bit referring to a channel
        // that does not exist in this stream (e.g. LFE/bit 3 on a 2-channel
        // stream) is an error, not a silently-skipped no-op.
        if (frameIdx == item.frameFrom()) {
            av_channel_layout_copy(&sourceRefLayout, &frame->ch_layout);
            const int nCh = frame->ch_layout.nb_channels;
            const quint8 validMask = (nCh >= 8) ? quint8(0xFF) : quint8((1u << nCh) - 1);
            if (item.channelMask() & ~validMask) {
                failed = true;
                errMsg = QString("channel mask 0x%1 references channel(s) beyond the "
                                  "stream's %2 channel(s) at frame %3")
                             .arg(item.channelMask(), 0, 16).arg(nCh).arg(frameIdx);
                av_frame_unref(frame);
                break;
            }
        } else if (av_channel_layout_compare(&frame->ch_layout, &sourceRefLayout) != 0) {
            failed = true;
            errMsg = QString("repair range spans a channel-mode change at frame %1")
                         .arg(frameIdx);
            av_frame_unref(frame);
            break;
        }

        if (!encCtx) {
            encCtx = avcodec_alloc_context3(enc);
            if (!encCtx) {
                failed = true;
                errMsg = QStringLiteral("avcodec_alloc_context3 failed for AC3 encoder");
                av_frame_unref(frame);
                break;
            }
            encCtx->sample_rate = frame->sample_rate;
            encCtx->bit_rate = bitRate;
            encCtx->time_base = AVRational{1, frame->sample_rate};
            encCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
            if (targetAcmod < 0) {
                // Keep the source channel layout, read from the decoded
                // frame (robust even if codecpar left ch_layout unpopulated
                // for a raw AC3 elementary stream).
                av_channel_layout_copy(&encCtx->ch_layout, &frame->ch_layout);
            } else if (targetAcmod == 7 || targetAcmod == 6) {
                AVChannelLayout layout51 = AV_CHANNEL_LAYOUT_5POINT1;
                av_channel_layout_copy(&encCtx->ch_layout, &layout51);
            } else {
                AVChannelLayout layoutStereo = AV_CHANNEL_LAYOUT_STEREO;
                av_channel_layout_copy(&encCtx->ch_layout, &layoutStereo);
            }
            if (avcodec_open2(encCtx, enc, nullptr) < 0) {
                failed = true;
                errMsg = QStringLiteral("could not open AC3 encoder");
                av_frame_unref(frame);
                break;
            }
        }

        applyMaskAndFade(frame, item.channelMask(), fadeLen,
                          frameIdx == item.frameFrom(), frameIdx == item.frameTo());

        AVFrame* encInput = frame;
        const bool sameLayout =
            av_channel_layout_compare(&frame->ch_layout, &encCtx->ch_layout) == 0;
        if (!sameLayout) {
            if (!swrCtx) {
                int swrRet = swr_alloc_set_opts2(&swrCtx,
                    &encCtx->ch_layout, encCtx->sample_fmt, encCtx->sample_rate,
                    &frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate,
                    0, nullptr);
                if (swrRet < 0 || !swrCtx || swr_init(swrCtx) < 0) {
                    failed = true;
                    errMsg = QStringLiteral("swr init failed for target acmod conversion");
                    av_frame_unref(frame);
                    break;
                }
                convFrame = av_frame_alloc();
            }
            av_frame_unref(convFrame);
            av_channel_layout_copy(&convFrame->ch_layout, &encCtx->ch_layout);
            convFrame->format = encCtx->sample_fmt;
            convFrame->sample_rate = encCtx->sample_rate;
            convFrame->nb_samples = frame->nb_samples;
            if (av_frame_get_buffer(convFrame, 0) < 0) {
                failed = true;
                errMsg = QStringLiteral("av_frame_get_buffer failed for swr output");
                av_frame_unref(frame);
                break;
            }
            // I3: swr_convert's return is the number of samples actually
            // produced (or a negative AVERROR) -- a short conversion left
            // uninitialized tail samples in convFrame that must not be fed
            // to the encoder silently.
            int swrOut = swr_convert(swrCtx, convFrame->data, convFrame->nb_samples,
                                      (const uint8_t**)frame->data, frame->nb_samples);
            if (swrOut < 0) {
                failed = true;
                errMsg = QString("swr_convert failed at frame %1: %2")
                             .arg(frameIdx).arg(avErr(swrOut));
                av_frame_unref(frame);
                break;
            }
            if (swrOut != convFrame->nb_samples) {
                failed = true;
                errMsg = QString("swr_convert produced %1 samples at frame %2, expected %3")
                             .arg(swrOut).arg(frameIdx).arg(convFrame->nb_samples);
                av_frame_unref(frame);
                break;
            }
            encInput = convFrame;
        }
        encInput->pts = frameIdx;

        int sendRet2 = avcodec_send_frame(encCtx, encInput);
        if (sendRet2 < 0) {
            failed = true;
            errMsg = QString("AC3 encode send_frame failed at %1: %2")
                         .arg(frameIdx).arg(avErr(sendRet2));
            av_frame_unref(frame);
            break;
        }
        AVPacket* encPkt = av_packet_alloc();
        int recvRet = avcodec_receive_packet(encCtx, encPkt);
        if (recvRet < 0) {
            failed = true;
            errMsg = QString("AC3 encode produced no packet at %1: %2")
                         .arg(frameIdx).arg(avErr(recvRet));
            av_packet_free(&encPkt);
            av_frame_unref(frame);
            break;
        }
        // I2 splice invariant: the replacement frame's byte size must match
        // the source's CBR frame size exactly, or the caller's byte-offset
        // splice into the source file corrupts the stream.
        if (encPkt->size != sourceFrameSize) {
            failed = true;
            errMsg = QString("encoded replacement frame size mismatch at frame %1: "
                              "got %2 bytes, source frames are %3 bytes")
                         .arg(frameIdx).arg(encPkt->size).arg(sourceFrameSize);
            av_packet_free(&encPkt);
            av_frame_unref(frame);
            break;
        }
        table.insert(frameIdx, QByteArray(reinterpret_cast<const char*>(encPkt->data),
                                           encPkt->size));
        av_packet_free(&encPkt);
        av_frame_unref(frame);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_channel_layout_uninit(&sourceRefLayout);
    if (convFrame) av_frame_free(&convFrame);
    if (swrCtx) swr_free(&swrCtx);
    if (encCtx) avcodec_free_context(&encCtx);
    avcodec_free_context(&decCtx);
    avformat_close_input(&fmtCtx);

    if (failed) {
        return fail(errMsg);
    }
    if (table.size() != expectedCount) {
        // Two very different causes, and calling both an implementation bug
        // sent the reader hunting in the encoder (final review M3):
        //
        // a) The file simply ends before frameTo - a repair range saved
        //    against a longer AC3 (recording re-demuxed/replaced). frameIdx
        //    holds the last frame number the demuxer delivered, so
        //    frameIdx < frameTo means we ran into EOF, not a logic error.
        // b) Anything else: the range was fully read but produced too few
        //    replacement frames, which AC3 encoding (frame-exact, no
        //    priming/delay) cannot do - that IS an implementation bug.
        if (frameIdx < item.frameTo()) {
            return fail(QString("repair range %1-%2 reaches past the end of the audio file "
                                 "(it holds %3 frames) -- the recording was probably "
                                 "re-demuxed or replaced after the project was saved")
                            .arg(item.frameFrom()).arg(item.frameTo()).arg(frameIdx + 1));
        }
        return fail(QString("replacement-frame count mismatch: expected %1, got %2 "
                             "(implementation bug -- AC3 encode is frame-exact, no priming/delay)")
                        .arg(expectedCount).arg(table.size()));
    }
    return table;
}

} // namespace TTAudioRepair
