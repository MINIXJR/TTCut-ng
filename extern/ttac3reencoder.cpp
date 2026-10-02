/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttac3reencoder.h"
#include "../avstream/ttbitstream.h"

#include <QtGlobal>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// BSI syntax: ATSC A/52, section 5.3.2 and annex D (alternate bit stream
// syntax, bsid 6).
bool ttParseAc3FrameMeta(const uint8_t* data, int size, TTAc3FrameMeta* meta)
{
    if (!data || !meta || size < 8 || data[0] != 0x0B || data[1] != 0x77)
        return false;

    TTBitReader br(data + 5, size - 5);   // behind syncword, crc1, fscod/frmsizecod
    TTAc3FrameMeta m;
    m.bsid = int(br.bits(5));
    if (m.bsid > 8)
        return false;                     // 11..16 is E-AC3; 9, 10 are not AC3 either
    m.bsmod = int(br.bits(3));
    m.acmod = int(br.bits(3));
    if ((m.acmod & 1) && m.acmod != 1) m.cmixlev = int(br.bits(2));
    if (m.acmod & 4)                   m.surmixlev = int(br.bits(2));
    if (m.acmod == 2)                  m.dsurmod = int(br.bits(2));
    m.lfeon = br.flag();
    const int dialnorm = int(br.bits(5));
    m.dialnorm = dialnorm == 0 ? -31 : -dialnorm;   // code 0 is reserved and means -31
    if (br.flag()) br.skip(8);                      // compr
    if (br.flag()) br.skip(8);                      // langcod
    if (br.flag()) {                                // audprodie
        m.mixlevel = int(br.bits(5));
        m.roomtyp  = int(br.bits(2));
    }
    if (m.acmod == 0) {                             // 1+1: the second channel's set
        br.skip(5);
        if (br.flag()) br.skip(8);
        if (br.flag()) br.skip(8);
        if (br.flag()) br.skip(7);
    }
    m.copyright = int(br.bits(1));
    m.origbs    = int(br.bits(1));
    if (m.bsid == 6) {
        if (br.flag()) {                            // xbsi1e
            m.dmixmod       = int(br.bits(2));
            m.ltrtcmixlev   = int(br.bits(3));
            m.ltrtsurmixlev = int(br.bits(3));
            m.lorocmixlev   = int(br.bits(3));
            m.lorosurmixlev = int(br.bits(3));
        }
        if (br.flag()) {                            // xbsi2e
            m.dsurexmod    = int(br.bits(2));
            m.dheadphonmod = int(br.bits(2));
            m.adconvtyp    = int(br.bits(1));
            br.skip(9);                             // xbsi2, encinfo
        }
    }
    if (br.error())
        return false;
    *meta = m;
    return true;
}

namespace {

const int kFrameSamples = 1536;   // one AC3 frame
const int kEncoderDelay = 256;    // the AC3 encoder's overlap (its initial_padding)

QString avErr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

bool fail(QString* error, const QString& msg)
{
    if (error) *error = msg;
    return false;
}

// Mix levels as ffmpeg's AC3 encoder expects them (it matches floats against
// its tables with a tolerance of 0.01). Index = code in the bit stream.
const double kCmixlev[3]   = {0.7071067811865476, 0.5946035575013605, 0.5};
const double kSurmixlev[3] = {0.7071067811865476, 0.5, 0.0};
const double kExtmixlev[8] = {1.4142135623730951, 1.189207115002721, 1.0, 0.8408964152537145,
                              0.7071067811865476, 0.5946035575013605, 0.5, 0.0};

bool hasCenter(int acmod)   { return (acmod & 1) && acmod != 1; }
bool hasSurround(int acmod) { return (acmod & 4) != 0; }

// What decides whether two consecutive frames can share one encoder.
struct RunKey {
    int            targetAcmod = -1;
    qint64         bitRate = 0;
    quint64        sourceLayout = 0;
    TTAc3FrameMeta meta;
};

bool sameRun(const RunKey& a, const RunKey& b)
{
    const TTAc3FrameMeta &m = a.meta, &n = b.meta;
    return a.targetAcmod == b.targetAcmod && a.bitRate == b.bitRate && a.sourceLayout == b.sourceLayout &&
           m.bsmod == n.bsmod && m.acmod == n.acmod && m.cmixlev == n.cmixlev && m.surmixlev == n.surmixlev &&
           m.dsurmod == n.dsurmod && m.dialnorm == n.dialnorm && m.mixlevel == n.mixlevel &&
           m.roomtyp == n.roomtyp && m.copyright == n.copyright && m.origbs == n.origbs &&
           m.dmixmod == n.dmixmod && m.ltrtcmixlev == n.ltrtcmixlev && m.ltrtsurmixlev == n.ltrtsurmixlev &&
           m.lorocmixlev == n.lorocmixlev && m.lorosurmixlev == n.lorosurmixlev &&
           m.dsurexmod == n.dsurexmod && m.dheadphonmod == n.dheadphonmod && m.adconvtyp == n.adconvtyp;
}

// Gives the encoder the header fields of a source frame. A field is set only
// when it means something in the source frame's own layout (a stereo frame
// can carry extended mix levels that describe nothing) and its code is not
// reserved; everything else keeps the encoder's default.
// The options are set as numbers, not through an option dictionary: option
// strings are parsed with the C library's strtod, and "0.707" fails under a
// numeric locale with a decimal comma (the encoder then does not open).
void setEncoderOptions(AVCodecContext* enc, const TTAc3FrameMeta& m)
{
    void* o = enc->priv_data;
    av_opt_set_int(o, "dialnorm", qBound(-31, m.dialnorm, -1), 0);
    av_opt_set_int(o, "copyright", m.copyright, 0);
    av_opt_set_int(o, "original", m.origbs, 0);
    if (hasCenter(m.acmod) && m.cmixlev >= 0 && m.cmixlev <= 2)
        av_opt_set_double(o, "center_mixlev", kCmixlev[m.cmixlev], 0);
    if (hasSurround(m.acmod) && m.surmixlev >= 0 && m.surmixlev <= 2)
        av_opt_set_double(o, "surround_mixlev", kSurmixlev[m.surmixlev], 0);
    if (m.acmod == 2 && m.dsurmod >= 0 && m.dsurmod <= 2)
        av_opt_set_int(o, "dsur_mode", m.dsurmod, 0);
    if (m.mixlevel >= 0) {
        av_opt_set_int(o, "mixing_level", 80 + m.mixlevel, 0);
        av_opt_set_int(o, "room_type", m.roomtyp >= 0 && m.roomtyp <= 2 ? m.roomtyp : 0, 0);
    }
    if (m.acmod > 2 && m.dmixmod >= 0) {
        av_opt_set_int(o, "dmix_mode", m.dmixmod, 0);
        if (hasCenter(m.acmod)) {
            av_opt_set_double(o, "ltrt_cmixlev", kExtmixlev[m.ltrtcmixlev & 7], 0);
            av_opt_set_double(o, "loro_cmixlev", kExtmixlev[m.lorocmixlev & 7], 0);
        }
        if (hasSurround(m.acmod)) {              // codes 0..2 are reserved for surround
            if (m.ltrtsurmixlev >= 3) av_opt_set_double(o, "ltrt_surmixlev", kExtmixlev[m.ltrtsurmixlev & 7], 0);
            if (m.lorosurmixlev >= 3) av_opt_set_double(o, "loro_surmixlev", kExtmixlev[m.lorosurmixlev & 7], 0);
        }
    }
    if (m.dsurexmod >= 0) {
        if (m.acmod >= 6) av_opt_set_int(o, "dsurex_mode", m.dsurexmod, 0);
        if (m.acmod == 2) av_opt_set_int(o, "dheadphone_mode", qMin(m.dheadphonmod, 2), 0);
        av_opt_set_int(o, "ad_conv_type", m.adconvtyp, 0);
    }
}

} // namespace

struct TTAc3Reencoder::Private
{
    // decoder side
    AVCodecParameters* par = nullptr;       // own copy, to reopen the decoder
    AVCodecContext*    dec = nullptr;
    AVFrame*           frame = nullptr;     // last decoded frame, source layout
    AVPacket*          prevPkt = nullptr;   // last pushed packet while it was not decoded
    bool               warm = false;        // the packet before the next one went through the decoder

    // the current run
    bool               runActive = false;
    RunKey             key;
    AVCodecContext*    enc = nullptr;
    AVChannelLayout    target {};           // the encoder's layout
    SwrContext*        swr = nullptr;
    AVChannelLayout    swrIn {};            // input layout the resampler was set up for
    AVFrame*           conv = nullptr;      // a frame converted to the target layout
    AVAudioFifo*       fifo = nullptr;      // encoder input, target layout
    AVFrame*           encFrame = nullptr;  // 1536 samples handed to the encoder
    AVPacket*          encPkt = nullptr;
    bool               discardNext = false; // the run's first packet is priming
    QList<qint64>      pendingTags;
    int64_t            encPts = 0;

    bool openDecoder(QString* error);
    bool decode(const AVPacket* pkt, QString* error);
    bool openRun(const RunKey& k, QString* error);
    bool toTarget(const AVFrame* in, AVFrame** out, QString* error);
    bool drain(QList<Replacement>* out, QString* error);
    bool closeRun(const AVFrame* lookahead, QList<Replacement>* out, QString* error);
    void dropRun();
};

bool TTAc3Reencoder::Private::openDecoder(QString* error)
{
    avcodec_free_context(&dec);
    warm = false;
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec || avcodec_parameters_to_context(dec, par) < 0) {
        avcodec_free_context(&dec);
        return fail(error, QStringLiteral("could not set up the AC3 decoder"));
    }
    // Without the stream's dynamic range compression: the encoder cannot
    // write compression words, so compression applied here would be baked
    // into the replacement. avcodec_flush_buffers resets this option
    // (ac3_decode_flush clears the decoder's context) - this class never
    // flushes and reopens the decoder instead.
    av_opt_set_double(dec->priv_data, "drc_scale", 0.0, 0);
    const int ret = avcodec_open2(dec, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&dec);   // push() then reports "not open"
        return fail(error, QString("could not open the AC3 decoder: %1").arg(avErr(ret)));
    }
    return true;
}

bool TTAc3Reencoder::Private::decode(const AVPacket* pkt, QString* error)
{
    int ret = avcodec_send_packet(dec, pkt);
    if (ret < 0)
        return fail(error, QString("AC3 decode send_packet failed: %1").arg(avErr(ret)));
    av_frame_unref(frame);
    ret = avcodec_receive_frame(dec, frame);
    if (ret < 0)
        return fail(error, QString("AC3 decode produced no frame: %1").arg(avErr(ret)));
    if (frame->format != AV_SAMPLE_FMT_FLTP || frame->nb_samples != kFrameSamples)
        return fail(error, QStringLiteral("AC3 decoder produced an unexpected sample format"));
    warm = true;
    return true;
}

bool TTAc3Reencoder::Private::openRun(const RunKey& k, QString* error)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
    if (!codec)
        return fail(error, QStringLiteral("AC3 encoder not available"));
    enc = avcodec_alloc_context3(codec);
    if (!enc)
        return fail(error, QStringLiteral("avcodec_alloc_context3 failed for the AC3 encoder"));

    av_channel_layout_uninit(&target);
    if (k.targetAcmod < 0) {
        av_channel_layout_copy(&target, &frame->ch_layout);
    } else if (k.targetAcmod == 7 || k.targetAcmod == 6) {
        const AVChannelLayout layout51 = AV_CHANNEL_LAYOUT_5POINT1;
        av_channel_layout_copy(&target, &layout51);
    } else {
        const AVChannelLayout layoutStereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&target, &layoutStereo);
    }
    const int channels = target.nb_channels;
    enc->sample_rate = frame->sample_rate;
    enc->bit_rate    = k.bitRate;
    enc->time_base   = AVRational{1, frame->sample_rate};
    enc->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&enc->ch_layout, &target);
    // bsmod 5..7 are only valid for one channel (7 with more channels is
    // karaoke, which the encoder numbers differently): not carried then.
    if (k.meta.bsmod <= 4 || channels == 1)
        enc->audio_service_type = static_cast<AVAudioServiceType>(k.meta.bsmod);

    setEncoderOptions(enc, k.meta);
    const int ret = avcodec_open2(enc, codec, nullptr);
    if (ret < 0)
        return fail(error, QString("could not open the AC3 encoder: %1").arg(avErr(ret)));
    if (enc->frame_size != kFrameSamples)
        return fail(error, QStringLiteral("AC3 encoder frame size is not 1536 samples"));

    fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, channels, 2 * kFrameSamples);
    encFrame = av_frame_alloc();
    if (!fifo || !encFrame)
        return fail(error, QStringLiteral("out of memory setting up the AC3 encoder input"));
    encFrame->format      = AV_SAMPLE_FMT_FLTP;
    encFrame->sample_rate = enc->sample_rate;
    encFrame->nb_samples  = kFrameSamples;
    av_channel_layout_copy(&encFrame->ch_layout, &target);
    if (av_frame_get_buffer(encFrame, 0) < 0)
        return fail(error, QStringLiteral("av_frame_get_buffer failed for the AC3 encoder input"));

    // Priming: the encoder puts the first 256 samples of an input frame into
    // the last transform block of the packet before. 1280 zero samples in
    // front move the run's audio 256 samples ahead, so that packet k holds
    // exactly the run's frame k - 1; packet 0 is thrown away.
    const int priming = kFrameSamples - kEncoderDelay;
    av_samples_set_silence(encFrame->extended_data, 0, priming, channels, AV_SAMPLE_FMT_FLTP);
    if (av_audio_fifo_write(fifo, reinterpret_cast<void**>(encFrame->extended_data), priming) < priming)
        return fail(error, QStringLiteral("audio fifo write failed"));

    discardNext = true;
    pendingTags.clear();
    encPts = 0;
    key = k;
    runActive = true;
    return true;
}

bool TTAc3Reencoder::Private::toTarget(const AVFrame* in, AVFrame** out, QString* error)
{
    if (av_channel_layout_compare(&in->ch_layout, &target) == 0) {
        *out = const_cast<AVFrame*>(in);
        return true;
    }
    if (swr && av_channel_layout_compare(&swrIn, &in->ch_layout) != 0) {
        swr_free(&swr);
        av_channel_layout_uninit(&swrIn);
    }
    if (!swr) {
        const int ret = swr_alloc_set_opts2(&swr, &target, AV_SAMPLE_FMT_FLTP, in->sample_rate,
                                            &in->ch_layout, AV_SAMPLE_FMT_FLTP, in->sample_rate, 0, nullptr);
        if (ret < 0 || !swr || swr_init(swr) < 0)
            return fail(error, QStringLiteral("swr init failed for the channel layout conversion"));
        av_channel_layout_copy(&swrIn, &in->ch_layout);
    }
    if (!conv) conv = av_frame_alloc();
    if (!conv)
        return fail(error, QStringLiteral("out of memory"));
    av_frame_unref(conv);
    av_channel_layout_copy(&conv->ch_layout, &target);
    conv->format      = AV_SAMPLE_FMT_FLTP;
    conv->sample_rate = in->sample_rate;
    conv->nb_samples  = in->nb_samples;
    if (av_frame_get_buffer(conv, 0) < 0)
        return fail(error, QStringLiteral("av_frame_get_buffer failed for the conversion output"));
    const int n = swr_convert(swr, conv->data, conv->nb_samples,
                              const_cast<const uint8_t**>(in->data), in->nb_samples);
    if (n != in->nb_samples)
        return fail(error, QString("swr_convert produced %1 of %2 samples").arg(n).arg(in->nb_samples));
    *out = conv;
    return true;
}

bool TTAc3Reencoder::Private::drain(QList<Replacement>* out, QString* error)
{
    while (av_audio_fifo_size(fifo) >= kFrameSamples) {
        if (av_frame_make_writable(encFrame) < 0 ||
            av_audio_fifo_read(fifo, reinterpret_cast<void**>(encFrame->extended_data), kFrameSamples) < kFrameSamples)
            return fail(error, QStringLiteral("audio fifo read failed"));
        encFrame->nb_samples = kFrameSamples;
        encFrame->pts = encPts;
        encPts += kFrameSamples;
        int ret = avcodec_send_frame(enc, encFrame);
        if (ret < 0)
            return fail(error, QString("AC3 encode send_frame failed: %1").arg(avErr(ret)));
        ret = avcodec_receive_packet(enc, encPkt);
        if (ret < 0)
            return fail(error, QString("AC3 encode produced no packet: %1").arg(avErr(ret)));
        if (discardNext) {
            discardNext = false;
        } else if (pendingTags.isEmpty()) {
            av_packet_unref(encPkt);
            return fail(error, QStringLiteral("AC3 re-encode produced a frame nobody asked for"));
        } else {
            out->append(Replacement{pendingTags.takeFirst(),
                                    QByteArray(reinterpret_cast<const char*>(encPkt->data), encPkt->size)});
        }
        av_packet_unref(encPkt);
    }
    return true;
}

// The first 256 samples behind a run complete its last frame; lookahead is
// the decoded frame that follows, or null when nothing follows (silence).
bool TTAc3Reencoder::Private::closeRun(const AVFrame* lookahead, QList<Replacement>* out, QString* error)
{
    bool ok = true;
    AVFrame* pcm = nullptr;
    if (lookahead && toTarget(lookahead, &pcm, nullptr)) {
        ok = av_audio_fifo_write(fifo, reinterpret_cast<void**>(pcm->extended_data), kEncoderDelay) == kEncoderDelay;
    } else {
        ok = av_frame_make_writable(encFrame) >= 0;
        if (ok) {
            av_samples_set_silence(encFrame->extended_data, 0, kEncoderDelay, target.nb_channels, AV_SAMPLE_FMT_FLTP);
            ok = av_audio_fifo_write(fifo, reinterpret_cast<void**>(encFrame->extended_data), kEncoderDelay) == kEncoderDelay;
        }
    }
    if (!ok)
        fail(error, QStringLiteral("audio fifo write failed"));
    ok = ok && drain(out, error);
    if (ok && !pendingTags.isEmpty())
        ok = fail(error, QStringLiteral("AC3 re-encode left a frame without a replacement"));
    dropRun();
    return ok;
}

void TTAc3Reencoder::Private::dropRun()
{
    avcodec_free_context(&enc);
    swr_free(&swr);
    av_channel_layout_uninit(&swrIn);
    av_channel_layout_uninit(&target);
    av_frame_free(&conv);
    av_frame_free(&encFrame);
    if (fifo) { av_audio_fifo_free(fifo); fifo = nullptr; }
    pendingTags.clear();
    runActive = false;
}

TTAc3Reencoder::TTAc3Reencoder() : d(new Private) {}

TTAc3Reencoder::~TTAc3Reencoder()
{
    d->dropRun();
    av_packet_free(&d->encPkt);
    av_packet_free(&d->prevPkt);
    av_frame_free(&d->frame);
    avcodec_free_context(&d->dec);
    avcodec_parameters_free(&d->par);
    delete d;
}

bool TTAc3Reencoder::open(const AVCodecParameters* par, QString* error)
{
    d->par     = avcodec_parameters_alloc();
    d->frame   = av_frame_alloc();
    d->prevPkt = av_packet_alloc();
    d->encPkt  = av_packet_alloc();
    if (!d->par || !d->frame || !d->prevPkt || !d->encPkt || avcodec_parameters_copy(d->par, par) < 0)
        return fail(error, QStringLiteral("out of memory setting up the AC3 re-encoder"));
    return d->openDecoder(error);
}

bool TTAc3Reencoder::push(const AVPacket* pkt, bool reencode, const Request& req, qint64 tag,
                          const PcmEdit& edit, QList<Replacement>* out, QString* error)
{
    if (!d->dec)
        return fail(error, QStringLiteral("AC3 re-encoder is not open"));

    if (!reencode) {
        if (d->runActive) {
            // The frame behind a run. If it does not decode, silence completes the run.
            const bool decoded = d->decode(pkt, nullptr);
            return d->closeRun(decoded ? d->frame : nullptr, out, error);
        }
        av_packet_unref(d->prevPkt);
        if (av_packet_ref(d->prevPkt, pkt) < 0)
            return fail(error, QStringLiteral("out of memory"));
        d->warm = false;
        return true;
    }

    RunKey key;
    if (!ttParseAc3FrameMeta(pkt->data, pkt->size, &key.meta))
        return fail(error, QStringLiteral("not an AC3 frame header"));

    // Warm-up: the first 256 samples of a frame need the overlap of the
    // frame before it. That frame's own first 256 samples do not matter.
    if (!d->warm && d->prevPkt->size > 0)
        d->decode(d->prevPkt, nullptr);
    av_packet_unref(d->prevPkt);
    if (!d->decode(pkt, error))
        return false;

    if (edit && !edit(d->frame, error))
        return false;

    key.targetAcmod  = req.targetAcmod;
    key.bitRate      = req.bitRate > 0 ? req.bitRate
                                       : qint64(pkt->size) * 8 * d->frame->sample_rate / kFrameSamples;
    key.sourceLayout = d->frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
                           ? quint64(d->frame->ch_layout.u.mask) : quint64(d->frame->ch_layout.nb_channels);

    // Another encoder configuration ends the run; this frame is its lookahead.
    if (d->runActive && !sameRun(d->key, key) && !d->closeRun(d->frame, out, error))
        return false;
    if (!d->runActive && !d->openRun(key, error)) {
        d->dropRun();
        return false;
    }

    AVFrame* pcm = nullptr;
    if (!d->toTarget(d->frame, &pcm, error))
        return false;
    if (av_audio_fifo_write(d->fifo, reinterpret_cast<void**>(pcm->extended_data), kFrameSamples) < kFrameSamples)
        return fail(error, QStringLiteral("audio fifo write failed"));
    d->pendingTags.append(tag);
    return d->drain(out, error);
}

bool TTAc3Reencoder::finish(QList<Replacement>* out, QString* error)
{
    return d->runActive ? d->closeRun(nullptr, out, error) : true;
}

void TTAc3Reencoder::reset()
{
    d->dropRun();
    if (d->prevPkt) av_packet_unref(d->prevPkt);
    if (d->par) d->openDecoder(nullptr);   // a fresh decoder instead of a flush, see openDecoder()
}
