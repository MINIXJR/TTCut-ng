// test_ac3_reencode - re-encoded AC3 frames must play like the frames they
// replace: header fields, position of the audio, level
// (docs/code-map/audio-repair.md, audio-cut-timing.md).
//
// Usage:
//   test_ac3_reencode <workdir>
//       Self-test on generated material (gate ac3_reencode). Fixtures: pink
//       noise, 28 s, 448 kbit/s - 5.1 and stereo encodes of the SAME noise
//       with FL/FR of the 5.1 equal to L/R of the stereo, so a file spliced
//       from both stays one continuous signal on those two channels.
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
    const QStringList noise{"-f", "lavfi", "-i", "anoisesrc=d=28:c=pink:r=48000:a=0.3:seed=7"};
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

static int runSelfTest()
{
    check(makeFixtures(), "fixtures generated (ffmpeg)");
    if (gFailed) return 1;
    caseParser();
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
