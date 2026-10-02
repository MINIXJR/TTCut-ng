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
#include "extern/ttaudiocutter.h"
#include "extern/ttaudiorepair.h"

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

// ---- end-to-end helpers ------------------------------------------------------
// What a re-encode can carry: bsid only says whether extended fields exist;
// extended mix levels mean nothing without centre or surround channels; an
// absent xbsi2 field and "not indicated" are the same.
static TTAc3FrameMeta norm(TTAc3FrameMeta m)
{
    m.bsid = 0;
    if (m.acmod <= 2 || m.dmixmod < 0)
        m.dmixmod = m.ltrtcmixlev = m.ltrtsurmixlev = m.lorocmixlev = m.lorosurmixlev = -1;
    if (m.dsurexmod < 0 || m.acmod < 6) m.dsurexmod = 0;
    if (m.dheadphonmod < 0 || m.acmod != 2) m.dheadphonmod = 0;
    if (m.adconvtyp < 0) m.adconvtyp = 0;
    return m;
}

static double levelDb(const QVector<float>& v, qint64 start, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += double(v[start + i]) * v[start + i];
    return 10.0 * std::log10(s / n + 1e-20);
}

// Builds the repair table and splices it into a copy of the file's frames.
static bool repairInto(const QString& src, qint64 from, qint64 to, quint8 mask, int targetAcmod,
                       QVector<QByteArray>* frames, TTAudioRepair::FrameTable* table, QString* err)
{
    *table = TTAudioRepair::buildRepairTable(src, TTAudioRepairItem(0, from, to, mask), targetAcmod, err);
    if (!err->isEmpty() || table->size() != to - from + 1) return false;
    for (auto it = table->constBegin(); it != table->constEnd(); ++it)
        (*frames)[int(it.key())] = it.value();
    return true;
}

static double worstLevelDiff(const Pcm& o, qint64 oFrame, const Pcm& s, qint64 sFrame, int frames,
                             const QVector<QPair<int, int>>& channels)   // (output channel, source channel)
{
    double worst = 0.0;
    for (int f = 0; f < frames; ++f)
        for (const auto& c : channels)
            worst = qMax(worst, std::abs(levelDb(o.ch[c.first], (oFrame + f) * 1536, 1536) -
                                         levelDb(s.ch[c.second], (sFrame + f) * 1536, 1536)));
    return worst;
}

static Pcm gRef51, gRefSt;   // decodes of s51.ac3 and st.ac3

// ---- R1: repair in the source layout, mask C+LFE ------------------------------
static void caseRepairSameLayout()
{
    const qint64 from = 100, to = 131;
    QVector<QByteArray> frames = readFrames(W("s51.ac3"));
    const QVector<QByteArray> source = frames;
    TTAudioRepair::FrameTable table;
    QString err;
    const bool built1 = repairInto(W("s51.ac3"), from, to, 0x0C, -1, &frames, &table, &err);
    check(built1, "R1 table built" + (built1 ? QString() : ": " + err));
    if (table.isEmpty()) return;
    writeFrames(W("r1.ac3"), frames);
    Pcm o;
    check(decodeFile(W("r1.ac3"), -1, &o), "R1 decoded");
    const int lag = lagSamples(o.ch[0], (from + 8) * 1536, gRef51.ch[0], (from + 8) * 1536);
    check(lag == 0, QString("R1 lag of the re-encoded audio: %1 samples").arg(lag));
    const double lev = worstLevelDiff(o, from + 1, gRef51, from + 1, int(to - from - 1), {{0, 0}, {1, 1}, {4, 4}, {5, 5}});
    check(lev <= kLevelTolDb, QString("R1 level of the unmasked channels: worst difference %1 dB").arg(lev, 0, 'f', 2));
    const double in = worstBlockEsr(o.ch[0], from * 1536, gRef51.ch[0], from * 1536, -2, 3);
    const double ex = worstBlockEsr(o.ch[0], (to + 1) * 1536, gRef51.ch[0], (to + 1) * 1536, -2, 3);
    check(in < kSeamEsrDb && ex < kSeamEsrDb,
          QString("R1 range edges, FL: error-to-signal %1 / %2 dB").arg(in, 0, 'f', 1).arg(ex, 0, 'f', 1));

    // Masked channel FC: 240-sample fades at the first and the last sample of the range.
    const qint64 a = from * 1536, e = (to + 1) * 1536;
    // Relative to the source at the same place: transform coding spreads a
    // little noise around a fade, so "silent" means 20 dB down, not digital zero.
    const double fadeOutStart = levelDb(o.ch[2], a, 48) - levelDb(gRef51.ch[2], a, 48);
    const double downAfter    = levelDb(o.ch[2], a + 256, 224) - levelDb(gRef51.ch[2], a + 256, 224);
    const double downBefore   = levelDb(o.ch[2], e - 480, 224) - levelDb(gRef51.ch[2], e - 480, 224);
    const double fadeInEnd    = levelDb(o.ch[2], e - 48, 48) - levelDb(gRef51.ch[2], e - 48, 48);
    check(fadeOutStart > -2.0 && downAfter < -20.0,
          QString("R1 fade-out starts with the range: first 48 samples %1 dB, samples 256..480 %2 dB against the source")
              .arg(fadeOutStart, 0, 'f', 1).arg(downAfter, 0, 'f', 1));
    check(fadeInEnd > -2.0 && downBefore < -20.0,
          QString("R1 fade-in ends with the range: last 48 samples %1 dB, samples -480..-256 %2 dB against the source")
              .arg(fadeInEnd, 0, 'f', 1).arg(downBefore, 0, 'f', 1));

    bool same = true;
    for (qint64 i = from; i <= to; ++i)
        same = same && metaText(metaOf(frames[int(i)])) == metaText(metaOf(source[int(i)]));
    check(same, "R1 header fields equal the source frames': " + metaText(metaOf(frames[int(from)])));
}

// ---- R2, R3: repair inside a normalised segment ------------------------------
static void caseRepairConverted()
{
    // R2: 5.1 source, target stereo
    {
        QVector<QByteArray> frames = readFrames(W("s51.ac3"));
        TTAudioRepair::FrameTable table;
        QString err;
        const bool built2 = repairInto(W("s51.ac3"), 100, 131, 0x08, 2, &frames, &table, &err);
        check(built2, "R2 table built" + (built2 ? QString() : ": " + err));
        if (!table.isEmpty()) {
            const TTAc3FrameMeta m = metaOf(table.value(110));
            check(m.acmod == 2 && m.dialnorm == -23 && m.copyright == 1 && m.origbs == 0 && m.mixlevel == 20,
                  "R2 stereo replacement carries the source's layout-independent fields: " + metaText(m));
            writeFrames(W("r2.ac3"), frames.mid(100, 32));
            Pcm o;
            check(decodeFile(W("r2.ac3"), -1, &o), "R2 decoded");
            const int lag = lagSamples(o.ch[0], 8 * 1536, gRef51.ch[0], 108 * 1536);
            check(lag == 0, QString("R2 lag: %1 samples").arg(lag));
        }
    }
    // R3: stereo source, target 5.1, FL masked
    {
        QVector<QByteArray> frames = readFrames(W("st.ac3"));
        TTAudioRepair::FrameTable table;
        QString err;
        const bool built3 = repairInto(W("st.ac3"), 100, 131, 0x01, 7, &frames, &table, &err);
        check(built3, "R3 table built" + (built3 ? QString() : ": " + err));
        if (!table.isEmpty()) {
            const TTAc3FrameMeta m = metaOf(table.value(110));
            check(m.acmod == 7 && m.lfeon && m.dialnorm == -20 && m.copyright == 1,
                  "R3 5.1 replacement carries the source's layout-independent fields: " + metaText(m));
            writeFrames(W("r3.ac3"), frames.mid(100, 32));
            Pcm o;
            check(decodeFile(W("r3.ac3"), -1, &o), "R3 decoded");
            const int lag = lagSamples(o.ch[1], 8 * 1536, gRefSt.ch[1], 108 * 1536);
            const double lev = worstLevelDiff(o, 2, gRefSt, 102, 28, {{1, 1}});
            check(lag == 0 && lev <= kLevelTolDb,
                  QString("R3 FR against R: lag %1 samples, worst level difference %2 dB").arg(lag).arg(lev, 0, 'f', 2));
            check(levelDb(o.ch[0], 4 * 1536, 24 * 1536) < kSilentDb, "R3 masked FL is silent");
        }
    }
}

// ---- R4, R5, R6, L1: ranges at the file's ends, reserved codes, locale --------
static void caseRepairEdges()
{
    {   // R4: the range ends with the last frame
        QVector<QByteArray> frames = readFrames(W("s51.ac3"));
        const qint64 last = frames.size() - 1;
        TTAudioRepair::FrameTable table;
        QString err;
        const bool built4 = repairInto(W("s51.ac3"), last - 34, last, 0x08, -1, &frames, &table, &err);
        check(built4, "R4 table built" + (built4 ? QString() : ": " + err));
        if (!table.isEmpty()) {
            writeFrames(W("r4.ac3"), frames);
            Pcm o;
            decodeFile(W("r4.ac3"), -1, &o);
            const int lag = lagSamples(o.ch[0], (last - 26) * 1536, gRef51.ch[0], (last - 26) * 1536);
            const double tail = esrDb(o.ch[0], last * 1536, gRef51.ch[0], last * 1536, 1280);
            check(lag == 0 && tail < kSeamEsrDb,
                  QString("R4 range to the end of the file: lag %1, last frame error-to-signal %2 dB").arg(lag).arg(tail, 0, 'f', 1));
        }
    }
    {   // R5: the range starts at frame 0
        QVector<QByteArray> frames = readFrames(W("s51.ac3"));
        TTAudioRepair::FrameTable table;
        QString err;
        const bool built5 = repairInto(W("s51.ac3"), 0, 5, 0x08, -1, &frames, &table, &err);
        check(built5, "R5 table built" + (built5 ? QString() : ": " + err));
        if (!table.isEmpty()) {
            writeFrames(W("r5.ac3"), frames);
            Pcm o;
            decodeFile(W("r5.ac3"), -1, &o);
            const double head = esrDb(o.ch[0], 256, gRef51.ch[0], 256, 6 * 1536 - 256);
            check(head < kSeamEsrDb, QString("R5 range from frame 0: error-to-signal %1 dB").arg(head, 0, 'f', 1));
        }
    }
    {   // R6: cmixlev 3 is reserved - the encoder default (1) is used, the build does not fail
        QVector<QByteArray> frames = readFrames(W("s51.ac3"));
        for (int i = 298; i <= 305; ++i) frames[i][6] = char(quint8(frames[i][6]) | 0x18);
        writeFrames(W("res.ac3"), frames);
        TTAudioRepair::FrameTable table;
        QString err;
        const bool built6 = repairInto(W("res.ac3"), 300, 303, 0x08, -1, &frames, &table, &err);
        check(built6, "R6 table built" + (built6 ? QString() : ": " + err));
        if (!table.isEmpty()) {
            const TTAc3FrameMeta m = metaOf(table.value(301));
            check(m.cmixlev == 1 && m.dialnorm == -23, "R6 reserved cmixlev falls back to the default: " + metaText(m));
        }
    }
    {   // L1: German numeric locale
        if (!setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
            printf("NOTE: L1 not run - locale de_DE.UTF-8 is not installed\n");
        } else {
            QVector<QByteArray> frames = readFrames(W("s51.ac3"));
            const QVector<QByteArray> source = frames;
            TTAudioRepair::FrameTable table;
            QString err;
            const bool ok = repairInto(W("s51.ac3"), 400, 403, 0x08, -1, &frames, &table, &err);
            setlocale(LC_NUMERIC, "C");
            check(ok && metaText(metaOf(frames[401])) == metaText(metaOf(source[401])),
                  "L1 header fields under a German numeric locale: " + (ok ? metaText(metaOf(frames[401])) : err));
        }
    }
}

// ---- H1: a header field changes inside the range ------------------------------
static void caseHeaderChange()
{
    QVector<QByteArray> frames = readFrames(W("dn.ac3"));
    TTAudioRepair::FrameTable table;
    QString err;
    const bool built7 = repairInto(W("dn.ac3"), 190, 210, 0x08, -1, &frames, &table, &err);
    check(built7, "H1 table built" + (built7 ? QString() : ": " + err));
    if (table.isEmpty()) return;
    check(metaOf(table.value(195)).dialnorm == -23 && metaOf(table.value(205)).dialnorm == -27,
          QString("H1 dialnorm follows the source: %1 / %2").arg(metaOf(table.value(195)).dialnorm).arg(metaOf(table.value(205)).dialnorm));
    writeFrames(W("h1.ac3"), frames);
    Pcm o, src;
    decodeFile(W("h1.ac3"), -1, &o);
    decodeFile(W("dn.ac3"), -1, &src);
    const double seam = worstBlockEsr(o.ch[0], 200 * 1536, src.ch[0], 200 * 1536, -2, 2);
    check(seam < kSeamEsrDb, QString("H1 no seam where the encoder changes: %1 dB").arg(seam, 0, 'f', 1));
}

// ---- N1..N3, A1: acmod normalisation through TTAudioCutter --------------------
static void caseCutter()
{
    const QVector<QByteArray> source = readFrames(W("mixed.ac3"));
    const double total = kFrames * 0.032;

    // Fixture sanity: the stereo L and the 5.1 FL are the same signal.
    const double fix = esrDb(gRefSt.ch[0], 300 * 1536, gRef51.ch[0], 300 * 1536, 10 * 1536);
    check(fix < kSeamEsrDb, QString("fixture: stereo L equals 5.1 FL within coding noise (%1 dB)").arg(fix, 0, 'f', 1));

    {   // N1: target 5.1 - a run from the file's start and a run to its end
        TTAudioCutter cutter;
        const bool ok = cutter.cut(W("mixed.ac3"), W("n1.ac3"), {{0.0, total}}, true, {7});
        const QVector<QByteArray> out = readFrames(W("n1.ac3"));
        check(ok && out.size() == kFrames, QString("N1 cut: %1 frames (%2)").arg(out.size()).arg(cutter.lastError()));
        if (out.size() == kFrames) {
            bool copied = true, heads = true;
            for (int i = 250; i < 625; ++i) copied = copied && out[i] == source[i];
            for (int i : {0, 100, 249, 625, 700, 874}) {
                const TTAc3FrameMeta m = metaOf(out[i]);
                heads = heads && m.acmod == 7 && m.lfeon && m.dialnorm == -20 && m.copyright == 1 && m.origbs == 1;
            }
            check(copied, "N1 the 5.1 frames are copied byte for byte");
            check(heads, "N1 re-encoded frames carry the stereo source's fields: " + metaText(metaOf(out[100])));
            Pcm o;
            check(decodeFile(W("n1.ac3"), -1, &o), "N1 decoded");
            const int lagA = lagSamples(o.ch[0], 8 * 1536, gRefSt.ch[0], 8 * 1536);
            const int lagB = lagSamples(o.ch[0], 700 * 1536, gRefSt.ch[0], 700 * 1536);
            check(lagA == 0 && lagB == 0, QString("N1 lag: %1 / %2 samples").arg(lagA).arg(lagB));
            const double lev = qMax(worstLevelDiff(o, 2, gRefSt, 2, 246, {{0, 0}, {1, 1}}),
                                    worstLevelDiff(o, 627, gRefSt, 627, 246, {{0, 0}, {1, 1}}));
            check(lev <= kLevelTolDb, QString("N1 FL/FR against L/R: worst level difference %1 dB").arg(lev, 0, 'f', 2));
            const double s1 = worstBlockEsr(o.ch[0], 250 * 1536, gRef51.ch[0], 250 * 1536, -3, 2);
            const double s2 = worstBlockEsr(o.ch[0], 625 * 1536, gRef51.ch[0], 625 * 1536, -3, 2);
            check(s1 < kSeamEsrDb && s2 < kSeamEsrDb,
                  QString("N1 seams run/copy and copy/run: %1 / %2 dB").arg(s1, 0, 'f', 1).arg(s2, 0, 'f', 1));
            const double tail = esrDb(o.ch[0], 874 * 1536, gRefSt.ch[0], 874 * 1536, 1280);
            check(tail < kSeamEsrDb, QString("N1 last frame of the file: %1 dB").arg(tail, 0, 'f', 1));
        }
    }
    {   // N2: target stereo - a run in mid-segment
        TTAudioCutter cutter;
        const bool ok = cutter.cut(W("mixed.ac3"), W("n2.ac3"), {{0.0, total}}, true, {2});
        const QVector<QByteArray> out = readFrames(W("n2.ac3"));
        check(ok && out.size() == kFrames, QString("N2 cut: %1 frames").arg(out.size()));
        if (out.size() == kFrames) {
            bool copied = true;
            for (int i = 0; i < kFrames; ++i)
                if (i < 250 || i >= 625) copied = copied && out[i] == source[i];
            const TTAc3FrameMeta m = metaOf(out[400]);
            check(copied, "N2 the stereo frames are copied byte for byte");
            check(m.acmod == 2 && m.dialnorm == -23 && m.copyright == 1 && m.origbs == 0,
                  "N2 re-encoded frames carry the 5.1 source's fields: " + metaText(m));
            Pcm o;
            decodeFile(W("n2.ac3"), -1, &o);
            const int lag = lagSamples(o.ch[0], 300 * 1536, gRef51.ch[0], 300 * 1536);
            check(lag == 0, QString("N2 lag: %1 samples").arg(lag));
        }
    }
    {   // N3: a run that starts and ends with its segment (stereo frames 50..149)
        TTAudioCutter cutter;
        const bool ok = cutter.cut(W("mixed.ac3"), W("n3.ac3"), {{1.6, 4.8}}, true, {7});
        const QVector<QByteArray> out = readFrames(W("n3.ac3"));
        check(ok && out.size() == 100, QString("N3 cut: %1 frames").arg(out.size()));
        if (out.size() == 100) {
            Pcm o;
            decodeFile(W("n3.ac3"), -1, &o);
            const int lag = lagSamples(o.ch[0], 8 * 1536, gRefSt.ch[0], 58 * 1536);
            const double head = esrDb(o.ch[0], 256, gRefSt.ch[0], 50 * 1536 + 256, 1280);
            const double tail = esrDb(o.ch[0], 99 * 1536, gRefSt.ch[0], 149 * 1536, 1536);
            check(lag == 0 && head < kSeamEsrDb && tail < kSeamEsrDb,
                  QString("N3 lag %1, first frame %2 dB, last frame %3 dB").arg(lag).arg(head, 0, 'f', 1).arg(tail, 0, 'f', 1));
        }
    }
    {   // A1: abort while a frame is held back
        TTAudioCutter cutter;
        int polls = 0;
        const bool ok = cutter.cut(W("mixed.ac3"), W("a1.ac3"), {{0.0, total}}, true, {7}, nullptr,
                                   [&]() { return ++polls > 40; });
        check(!ok && cutter.lastError().contains("aborted"), "A1 abort inside a run: " + cutter.lastError());
    }
}

// ---- --real: dynamic range compression on a recording -------------------------
static int runReal(const QString& file, qint64 from)
{
    const QVector<QByteArray> all = readFrames(file);
    if (from < 0) {
        for (qint64 i = 1000; i + 80 < all.size() && from < 0; i += 500) {
            bool uniform = true;
            for (qint64 k = i - 16; k < i + 64; ++k)
                uniform = uniform && all[int(k)].size() == all[int(i)].size() &&
                          metaOf(all[int(k)]).acmod == metaOf(all[int(i)]).acmod;
            if (uniform) from = i;
        }
    }
    check(from >= 16 && from + 64 < all.size(), QString("real: range found at frame %1 of %2").arg(from).arg(all.size()));
    if (gFailed) return 1;
    const qint64 to = from + 31;

    QVector<QByteArray> frames = all;
    TTAudioRepair::FrameTable table;
    QString err;
    const bool built8 = repairInto(file, from, to, 0x00, -1, &frames, &table, &err);
    check(built8, "real: table built (no channel masked)" + (built8 ? QString() : ": " + err));
    if (gFailed) return 1;
    const qint64 a = from - 16;
    writeFrames(W("real_src.ac3"), all.mid(int(a), 96));
    writeFrames(W("real_out.ac3"), frames.mid(int(a), 96));

    Pcm s0, s1, o0;
    check(decodeFile(W("real_src.ac3"), 0, &s0) && decodeFile(W("real_src.ac3"), 1, &s1) &&
          decodeFile(W("real_out.ac3"), 0, &o0), "real: decoded");
    if (gFailed) return 1;

    const TTAc3FrameMeta m = metaOf(all[int(from)]);
    QVector<QPair<int, int>> channels;
    const int nch = m.acmod == 7 ? 6 : 2;
    for (int c = 0; c < nch; ++c)
        if (!(nch == 6 && c == 3)) channels.append({c, c});   // the LFE is band-limited by the encoder

    const double gain = worstLevelDiff(s1, 17, s0, 17, 30, {{0, 0}});
    printf("material: %s\nmaterial: compression changes the level by up to %.2f dB in this range\n",
           qPrintable(metaText(m)), gain);
    if (gain < 0.3) {
        printf("SKIP: no dynamic range compression in frames %lld..%lld - nothing to prove here\n",
               (long long)from, (long long)to);
        return 77;
    }
    const int lag = lagSamples(o0.ch[0], 24 * 1536, s0.ch[0], 24 * 1536);
    check(lag == 0, QString("real: lag %1 samples").arg(lag));
    const double lev = worstLevelDiff(o0, 17, s0, 17, 30, channels);
    check(lev <= kLevelTolDb,
          QString("real: level decoded without compression, worst difference %1 dB").arg(lev, 0, 'f', 2));
    bool same = true;
    for (qint64 i = from; i <= to; ++i)
        same = same && metaText(norm(metaOf(frames[int(i)]))) == metaText(norm(metaOf(all[int(i)])));
    check(same, "real: header fields equal the source's: " + metaText(metaOf(frames[int(from)])));
    return gFailed ? 1 : 0;
}

static int runSelfTest()
{
    check(makeFixtures(), "fixtures generated (ffmpeg)");
    if (gFailed) return 1;
    caseParser();
    caseUnit();
    check(decodeFile(W("s51.ac3"), -1, &gRef51) && decodeFile(W("st.ac3"), -1, &gRefSt), "references decoded");
    if (gFailed) return 1;
    caseRepairSameLayout();
    caseRepairConverted();
    caseRepairEdges();
    caseHeaderChange();
    caseCutter();
    return gFailed ? 1 : 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");   // as the application's main() does
    const QStringList args = app.arguments();
    const bool real = args.size() >= 4 && args.at(1) == "--real";
    if (!real && args.size() != 2) {
        fprintf(stderr, "usage: %s <workdir>\n       %s --real <recording.ac3> <workdir> [first-frame]\n",
                argv[0], argv[0]);
        return 2;
    }
    gWork = real ? args.at(3) : args.at(1);
    QDir().mkpath(gWork);
    const int rc = real ? runReal(args.at(2), args.size() > 4 ? args.at(4).toLongLong() : -1) : runSelfTest();
    printf("%s (%d checks, %d failed)\n", gFailed ? "FAILED" : "ALL PASS", gChecks, gFailed);
    return rc;
}
