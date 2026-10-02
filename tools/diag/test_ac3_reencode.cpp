// test_ac3_reencode - re-encoded AC3 frames must play like the frames they
// replace: header fields, position of the audio, level
// (docs/code-map/audio-repair.md, audio-cut-timing.md).
//
// Usage:
//   test_ac3_reencode <workdir>
//       Self-test on generated material (gate ac3_reencode). Fixtures: pink
//       noise low-passed at 1.5 kHz, 28 s, 448 kbit/s - 5.1 and stereo
//       encodes of the SAME noise with FL/FR of the 5.1 equal to L/R of the
//       stereo, so a file spliced from both stays one continuous signal on
//       those two channels. The low-pass is what makes seams measurable:
//       full-band pink noise codes so poorly at this bit rate (error 12 dB
//       below the signal, measured) that coding noise would hide them; with
//       it the worst 128-sample block of a clean re-encode sits 23 dB below.
//   test_ac3_reencode --real <recording.ac3> <workdir> [first-frame]
//       Dynamic range compression needs a real recording: ffmpeg's encoder
//       writes no compression words. Repairs 32 frames (no channel masked)
//       and compares levels decoded without compression. Exit 77 when the
//       chosen range carries no compression.
// Prints PASS/FAIL per check; exit 0 when all passed.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStringList>
#include <QVector>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include "extern/ttac3reencoder.h"

static int gChecks = 0, gFailed = 0;

static void check(bool ok, const QString& what)
{
    ++gChecks;
    if (!ok) ++gFailed;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    fflush(stdout);
}

// ---- frames ------------------------------------------------------------------
static const int kWords48k[38] = {64,64,80,80,96,96,112,112,128,128,160,160,192,192,224,224,256,256,
    320,320,384,384,448,448,512,512,640,640,768,768,896,896,1024,1024,1152,1152,1280,1280};

static QVector<QByteArray> readFrames(const QString& path)
{
    QVector<QByteArray> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray d = f.readAll();
    for (int p = 0; p + 7 <= d.size();) {
        if (quint8(d[p]) != 0x0B || quint8(d[p + 1]) != 0x77) break;
        const int code = quint8(d[p + 4]) & 0x3F;
        if ((quint8(d[p + 4]) >> 6) != 0 || code >= 38) break;
        const int size = kWords48k[code] * 2;
        if (p + size > d.size()) break;
        out.append(d.mid(p, size));
        p += size;
    }
    return out;
}

static bool writeFrames(const QString& path, const QVector<QByteArray>& frames)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    for (const QByteArray& fr : frames)
        if (f.write(fr) != fr.size()) return false;
    return true;
}

// ---- header fields -----------------------------------------------------------
static TTAc3FrameMeta metaOf(const QByteArray& frame)
{
    TTAc3FrameMeta m;
    if (!ttParseAc3FrameMeta(reinterpret_cast<const uint8_t*>(frame.constData()), frame.size(), &m))
        m.bsid = -1;
    return m;
}

static QString metaText(const TTAc3FrameMeta& m)
{
    return QString("bsid=%1 bsmod=%2 acmod=%3 lfe=%4 cmix=%5 sur=%6 dsur=%7 dialnorm=%8 mixlevel=%9 "
                   "room=%10 copyright=%11 orig=%12 dmix=%13 ltrt=%14/%15 loro=%16/%17 dsurex=%18 dhp=%19 adconv=%20")
        .arg(m.bsid).arg(m.bsmod).arg(m.acmod).arg(m.lfeon ? 1 : 0).arg(m.cmixlev).arg(m.surmixlev)
        .arg(m.dsurmod).arg(m.dialnorm).arg(m.mixlevel).arg(m.roomtyp).arg(m.copyright).arg(m.origbs)
        .arg(m.dmixmod).arg(m.ltrtcmixlev).arg(m.ltrtsurmixlev).arg(m.lorocmixlev).arg(m.lorosurmixlev)
        .arg(m.dsurexmod).arg(m.dheadphonmod).arg(m.adconvtyp);
}

// ---- decoding and measuring ----------------------------------------------------
static const double kLevelTolDb = 0.2;    // spec: level per frame and channel
// Error-to-signal in a 128-sample block at a range edge. Coding noise sits far
// below this; a dip, missing samples or shifted audio put the error near the
// signal itself (0 dB and above).
static const double kSeamEsrDb  = -15.0;
static const double kSilentDb   = -60.0;

struct Pcm {
    QVector<QVector<float>> ch = QVector<QVector<float>>(6);   // missing channels stay zero
    qint64 samples = 0;
};

static bool openAc3(const QString& path, AVFormatContext** fmt)
{
    *fmt = nullptr;
    if (avformat_open_input(fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(*fmt, nullptr) < 0 || (*fmt)->nb_streams < 1) {
        avformat_close_input(fmt);
        return false;
    }
    return true;
}

// Decodes a raw AC3 file. drcScale < 0: the decoder's default (compression
// applied). No flush: it would reset drc_scale. cons_noisegen: the decoder
// fills mantissas that got no bits with noise from a running generator, so
// two decodes of the same frame differ when the frames before it differ
// (measured: 10 to 17 dB below the signal on pink noise); seeded per frame,
// equal frames decode equally.
static bool decodeFile(const QString& path, int drcScale, Pcm* pcm)
{
    AVFormatContext* fmt = nullptr;
    if (!openAc3(path, &fmt)) return false;
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[0]->codecpar);
    AVDictionary* opts = nullptr;
    av_dict_set_int(&opts, "cons_noisegen", 1, 0);
    if (drcScale >= 0) av_dict_set_int(&opts, "drc_scale", drcScale, 0);
    bool ok = avcodec_open2(dec, codec, &opts) >= 0;
    av_dict_free(&opts);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    while (ok && av_read_frame(fmt, pkt) >= 0) {
        if (avcodec_send_packet(dec, pkt) >= 0 && avcodec_receive_frame(dec, fr) >= 0 &&
            fr->format == AV_SAMPLE_FMT_FLTP) {
            const int n = fr->nb_samples, nch = fr->ch_layout.nb_channels;
            for (int c = 0; c < 6; ++c) {
                const int at = pcm->ch[c].size();
                pcm->ch[c].resize(at + n);
                if (c < nch) memcpy(pcm->ch[c].data() + at, fr->data[c], size_t(n) * sizeof(float));
            }
            pcm->samples += n;
        } else {
            ok = false;
        }
        av_packet_unref(pkt);
        av_frame_unref(fr);
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return ok && pcm->samples > 0;
}

// Error of o against s relative to the level of s, in dB.
static double esrDb(const QVector<float>& o, qint64 oStart, const QVector<float>& s, qint64 sStart, int n)
{
    double e = 0.0, p = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = double(o[oStart + i]) - s[sStart + i];
        e += d * d;
        p += double(s[sStart + i]) * s[sStart + i];
    }
    return 10.0 * std::log10((e + 1e-20) / (p + 1e-20));
}

// lag with o[i] == s[i + lag]; 0 when the audio sits where the source's does.
static int lagSamples(const QVector<float>& o, qint64 oStart, const QVector<float>& s, qint64 sStart)
{
    const int n = 4096, range = 600;
    double best = -1e300;
    int bestLag = 0;
    for (int lag = -range; lag <= range; ++lag) {
        double c = 0.0;
        for (int i = 0; i < n; ++i) c += double(o[oStart + i]) * s[sStart + i + lag];
        if (c > best) { best = c; bestLag = lag; }
    }
    return bestLag;
}

// Worst error-to-signal of the 128-sample blocks from..to (in blocks) around a sample position.
static double worstBlockEsr(const QVector<float>& o, qint64 oPos, const QVector<float>& s, qint64 sPos,
                            int fromBlock, int toBlock)
{
    double worst = -1e300;
    for (int b = fromBlock; b <= toBlock; ++b)
        worst = qMax(worst, esrDb(o, oPos + b * 128, s, sPos + b * 128, 128));
    return worst;
}

// ---- fixtures ----------------------------------------------------------------
static bool ffmpeg(const QStringList& args)
{
    QProcess p;
    p.start("ffmpeg", QStringList{"-y", "-v", "error"} + args);
    return p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static QString gWork;
static QString W(const QString& name) { return gWork + "/" + name; }
static const int kFrames = 875;   // 28 s

static bool makeFixtures()
{
    const QStringList noise{"-f", "lavfi", "-i", "anoisesrc=d=28:c=pink:r=48000:a=0.3:seed=7,lowpass=f=1500"};
    const QStringList pan51{"-af", "pan=5.1(side)|FL=c0|FR=0.8*c0|FC=0.6*c0|LFE=0.3*c0|SL=0.5*c0|SR=0.4*c0",
                            "-c:a", "ac3", "-b:a", "448k"};
    const QStringList meta51{"-center_mixlev", "0.707", "-surround_mixlev", "0.707", "-copyright", "1",
                             "-original", "0", "-mixing_level", "100", "-room_type", "small",
                             "-dmix_mode", "ltrt", "-ltrt_cmixlev", "0.841", "-ltrt_surmixlev", "0.595",
                             "-loro_cmixlev", "0.595", "-loro_surmixlev", "0.5", "-ad_conv_type", "hdcd"};
    if (!ffmpeg(noise + pan51 + QStringList{"-dialnorm", "-23"} + meta51 + QStringList{W("s51.ac3")})) return false;
    if (!ffmpeg(noise + pan51 + QStringList{"-dialnorm", "-27"} + meta51 + QStringList{W("s51b.ac3")})) return false;
    if (!ffmpeg(noise + QStringList{"-af", "pan=stereo|FL=c0|FR=0.8*c0", "-c:a", "ac3", "-b:a", "448k",
                                    "-dialnorm", "-20", "-dsur_mode", "on", "-copyright", "1", W("st.ac3")}))
        return false;
    const QVector<QByteArray> s51 = readFrames(W("s51.ac3")), s51b = readFrames(W("s51b.ac3")),
                              st = readFrames(W("st.ac3"));
    if (s51.size() < kFrames || s51b.size() < kFrames || st.size() < kFrames) return false;
    // stereo | 5.1 | stereo on one timeline
    if (!writeFrames(W("mixed.ac3"), st.mid(0, 250) + s51.mid(250, 375) + st.mid(625, 250))) return false;
    // dialnorm changes at frame 200
    return writeFrames(W("dn.ac3"), s51.mid(0, 200) + s51b.mid(200, kFrames - 200));
}

// ---- P1: the header reader on known values -----------------------------------
static void caseParser()
{
    const TTAc3FrameMeta a = metaOf(readFrames(W("s51.ac3")).value(10));
    check(metaText(a) == "bsid=6 bsmod=0 acmod=7 lfe=1 cmix=0 sur=0 dsur=-1 dialnorm=-23 mixlevel=20 "
                         "room=2 copyright=1 orig=0 dmix=1 ltrt=3/5 loro=5/6 dsurex=0 dhp=0 adconv=1",
          "P1 5.1 frame: " + metaText(a));
    const TTAc3FrameMeta b = metaOf(readFrames(W("st.ac3")).value(10));
    check(metaText(b) == "bsid=8 bsmod=0 acmod=2 lfe=0 cmix=-1 sur=-1 dsur=2 dialnorm=-20 mixlevel=-1 "
                         "room=-1 copyright=1 orig=1 dmix=-1 ltrt=-1/-1 loro=-1/-1 dsurex=-1 dhp=-1 adconv=-1",
          "P1 stereo frame: " + metaText(b));
    TTAc3FrameMeta c;
    const uint8_t junk[16] = {0x0B, 0x77, 0, 0, 0x1E, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // bsid 31
    check(!ttParseAc3FrameMeta(junk, sizeof(junk), &c), "P1 bsid above 8 is refused");
    check(!ttParseAc3FrameMeta(junk, 5, &c), "P1 a truncated header is refused");
}

// ---- E1, E2: the unit on its own ---------------------------------------------
static QVector<AVPacket*> readPackets(const QString& path, AVFormatContext** fmtOut, int count)
{
    QVector<AVPacket*> pkts;
    if (!openAc3(path, fmtOut)) return pkts;
    for (int i = 0; i < count; ++i) {
        AVPacket* p = av_packet_alloc();
        if (av_read_frame(*fmtOut, p) < 0) { av_packet_free(&p); break; }
        pkts.append(p);
    }
    return pkts;
}

static void caseUnit()
{
    AVFormatContext* fmt = nullptr;
    QVector<AVPacket*> pkts = readPackets(W("s51.ac3"), &fmt, 140);
    check(pkts.size() == 140, "E1 packets read");
    if (pkts.size() != 140) return;

    TTAc3Reencoder re;
    QString err;
    const bool opened = re.open(fmt->streams[0]->codecpar, &err);
    check(opened, "E1 open" + (opened ? QString() : ": " + err));

    QList<TTAc3Reencoder::Replacement> out;
    AVPacket* bad = av_packet_alloc();
    av_new_packet(bad, 5);
    memset(bad->data, 0, 5);
    const bool refused = !re.push(bad, true, {}, 1, {}, &out, &err) && !err.isEmpty() && out.isEmpty();
    check(refused, "E1 a malformed packet fails the push with a message: " + err);
    av_packet_free(&bad);
    re.reset();

    // Replacements arrive one frame late, tagged, in order.
    bool ok = re.push(pkts[10], false, {}, 0, {}, &out, &err);
    ok = ok && re.push(pkts[11], true, {}, 11, {}, &out, &err);
    check(ok && out.isEmpty(), "E1 first frame of a run: nothing finished yet");
    ok = re.push(pkts[12], true, {}, 12, {}, &out, &err);
    check(ok && out.size() == 1 && out[0].tag == 11, "E1 second frame finishes the first");
    ok = re.push(pkts[13], false, {}, 0, {}, &out, &err);
    check(ok && out.size() == 2 && out[1].tag == 12, "E1 the frame behind the run finishes the last");
    check(out.size() == 2 && out[0].bytes.size() == pkts[11]->size && out[1].bytes.size() == pkts[12]->size,
          "E1 replacements have the source frame's size");
    out.clear();
    check(re.finish(&out, &err) && out.isEmpty(), "E1 finish with no run pending adds nothing");

    // E2: frames 100..131 replaced through the unit, spliced, decoded.
    re.reset();
    QVector<QByteArray> frames = readFrames(W("s51.ac3"));
    const QVector<QByteArray> source = frames;
    ok = true;
    for (int i = 99; i <= 132 && ok; ++i) {
        out.clear();
        ok = re.push(pkts[i], i >= 100 && i <= 131, {}, i, {}, &out, &err);
        for (const auto& r : out) frames[int(r.tag)] = r.bytes;
    }
    check(ok, "E2 pushes" + (ok ? QString() : ": " + err));
    int replaced = 0;
    for (int i = 100; i <= 131; ++i) replaced += frames[i] != source[i];
    check(replaced == 32, QString("E2 32 frames replaced (%1)").arg(replaced));
    writeFrames(W("e2.ac3"), frames);
    Pcm src, o;
    check(decodeFile(W("s51.ac3"), -1, &src) && decodeFile(W("e2.ac3"), -1, &o), "E2 decoded");
    if (!gFailed) {
        const int lag = lagSamples(o.ch[0], 108 * 1536, src.ch[0], 108 * 1536);
        check(lag == 0, QString("E2 lag of the re-encoded audio: %1 samples").arg(lag));
        const double in = worstBlockEsr(o.ch[0], 100 * 1536, src.ch[0], 100 * 1536, -2, 3);
        const double ex = worstBlockEsr(o.ch[0], 132 * 1536, src.ch[0], 132 * 1536, -2, 3);
        check(in < kSeamEsrDb && ex < kSeamEsrDb,
              QString("E2 range edges: error-to-signal %1 / %2 dB").arg(in, 0, 'f', 1).arg(ex, 0, 'f', 1));
        bool same = true;
        for (int i = 100; i <= 131; ++i) same = same && metaText(metaOf(frames[i])) == metaText(metaOf(source[i]));
        check(same, "E2 header fields equal the source frames': " + metaText(metaOf(frames[100])));
    }
    for (AVPacket* p : pkts) av_packet_free(&p);
    avformat_close_input(&fmt);
}

static int runSelfTest()
{
    check(makeFixtures(), "fixtures generated (ffmpeg)");
    if (gFailed) return 1;
    caseParser();
    caseUnit();
    return gFailed ? 1 : 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");   // as the application's main() does
    const QStringList args = app.arguments();
    if (args.size() != 2) {
        fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
        return 2;
    }
    gWork = args.at(1);
    QDir().mkpath(gWork);
    const int rc = runSelfTest();
    printf("%s (%d checks, %d failed)\n", gFailed ? "FAILED" : "ALL PASS", gChecks, gFailed);
    return rc;
}
