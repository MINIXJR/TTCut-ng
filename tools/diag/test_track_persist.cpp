// Do track languages, delays and positions survive the ways a track can
// enter the list? Probe for audit run 8, hypotheses H1 and H2 of
// docs/code-map/track-management.md.
//
//   H1  A plain open (TTAVData::openAVStreams) parks the .info audio
//       languages under (item, discovery index), but the discovered tracks
//       arrive with order -1. Staged: an audio file without a language
//       suffix and an .info that names it with language "ita".
//       Correct: the track's language is "ita".
//   H2  Discovered subtitles keep order -1, and serializeAVDataItem writes
//       item.order() as <Order>. Staged: two SRTs discovered next to the
//       video, each given its own language and delay, saved, reloaded.
//       Correct: every <Order> is distinct, and after the reload each file
//       carries its own language and delay again, in the saved order.
//   H2b A project written before the fix carries <Order>-1 on every
//       subtitle. Correct: each section's language and delay still reach
//       its own file, and the file order is kept.
//
// Every value is printed before it is checked, so a FAIL shows what the
// code actually did.
//
//   usage: test_track_persist <workdir>
//
// Build via `cmake --build build --target test_track_persist`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutprojectdata.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static const QString kVideoFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264");
static const QString kAudioFile =
    QStringLiteral("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3");

static bool writeText(const QString& path, const QString& text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream(&f) << text;
    return true;
}

static QString srt(int entries)
{
    QString s;
    for (int i = 1; i <= entries; ++i)
        s += QString("%1\n00:00:%2,000 --> 00:00:%3,000\nLine %1\n\n")
                 .arg(i).arg(2 * i, 2, 10, QChar('0')).arg(2 * i + 1, 2, 10, QChar('0'));
    return s;
}

// A fresh directory with the video (and, if wanted, the audio) linked under
// the base name "trk", so openAVStreams discovers only what we put there.
static QString stage(const QString& workDir, const QString& name, bool withAudio)
{
    QDir(workDir).mkpath(name);
    QDir dir(QDir(workDir).absoluteFilePath(name));
    for (const QString& e : dir.entryList(QDir::Files)) dir.remove(e);
    QFile::link(kVideoFile, dir.absoluteFilePath("trk.264"));
    if (withAudio) QFile::link(kAudioFile, dir.absoluteFilePath("trk.ac3"));
    return dir.absolutePath();
}

static bool openPlain(TTAVData& av, const QString& video)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&av, &TTAVData::threadPoolExit, [&]() { done = true; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    av.openAVStreams(video);
    if (!done) loop.exec();
    return done;
}

static bool openProject(TTAVData& av, const QString& project)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&av, &TTAVData::readProjectFileFinished,
                     [&](const QString&) { done = true; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    av.readProjectFile(QFileInfo(project));
    if (!done) loop.exec();
    return done;
}

static void dumpSubtitles(const char* tag, TTAVItem* item)
{
    for (int i = 0; i < item->subtitleCount(); ++i) {
        const TTSubtitleItem s = item->subtitleListItemAt(i);
        printf("  %s subtitle[%d] %s lang=%s delay=%d order=%d\n", tag, i,
               qPrintable(QFileInfo(s.getFileName()).fileName()),
               qPrintable(s.getLanguage()), s.getDelayMs(), s.order());
    }
}

// --- H1 --------------------------------------------------------------------
static void testInfoLanguage(const QString& workDir)
{
    printf("== H1: .info audio language on a plain open\n");
    const QString dir = stage(workDir, "h1", true);
    writeText(QDir(dir).absoluteFilePath("trk.info"),
              "[video]\nfile=trk.264\ncodec=h264\nwidth=720\nheight=576\nframe_rate=25/1\n"
              "start_pts=0.000000\n\n[audio]\ncount=1\naudio_0_file=trk.ac3\naudio_0_codec=ac3\n"
              "audio_0_lang=ita\naudio_0_first_pts=0.000000\naudio_0_trimmed_ms=0\n");

    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "H1: open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) {
        check(false, "H1: one item with one audio track");
        return;
    }
    const TTAudioItem a = av.avItemAt(0)->audioListItemAt(0);
    printf("  audio[0] %s lang=%s order=%d\n", qPrintable(QFileInfo(a.getFileName()).fileName()),
           qPrintable(a.getLanguage()), a.order());
    check(a.getLanguage() == "ita", "H1: audio language taken from .info (ita)");
}

// --- H2 --------------------------------------------------------------------
static void testSubtitleRoundTrip(const QString& workDir)
{
    printf("== H2: subtitle language/delay/order across save + reload\n");
    const QString dir = stage(workDir, "h2", false);
    writeText(QDir(dir).absoluteFilePath("trk_deu.srt"), srt(2));
    writeText(QDir(dir).absoluteFilePath("trk_eng.srt"), srt(3));

    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "H2: open finished");
    if (av.avCount() != 1) { check(false, "H2: one AV item"); return; }
    TTAVItem* item = av.avItemAt(0);
    dumpSubtitles("opened", item);
    check(item->subtitleCount() == 2, "H2: two subtitles discovered");
    if (item->subtitleCount() != 2) return;

    // Language and delay per FILE, as a user would set them in the view.
    QMap<QString, QPair<QString, int>> want;
    want["trk_deu.srt"] = qMakePair(QString("fra"), 111);
    want["trk_eng.srt"] = qMakePair(QString("ita"), 222);
    for (int i = 0; i < item->subtitleCount(); ++i) {
        const QString f = QFileInfo(item->subtitleListItemAt(i).getFileName()).fileName();
        item->onSubtitleLanguageChanged(i, want[f].first);
        item->onSubtitleDelayChanged(i, want[f].second);
    }
    // Put the eng file first, the way the up/down buttons would.
    if (QFileInfo(item->subtitleListItemAt(0).getFileName()).fileName() != "trk_eng.srt")
        item->onSwapSubtitleItems(0, 1);
    dumpSubtitles("edited", item);

    const QString project = QDir(dir).absoluteFilePath("h2.ttcut");
    av.writeProjectFile(QFileInfo(project), {}, TTLogoProjectData());
    QFile raw(project);
    QString xml;
    if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) xml = QString::fromUtf8(raw.readAll());
    QStringList orders;
    QRegularExpression subRe("<Subtitle>\\s*<Order>(-?\\d+)</Order>\\s*<Name>[^<]*/([^/<]+)</Name>");
    for (auto it = subRe.globalMatch(xml); it.hasNext();) {
        auto m = it.next();
        printf("  saved <Subtitle> Order=%s Name=%s\n", qPrintable(m.captured(1)), qPrintable(m.captured(2)));
        orders << m.captured(1);
    }
    check(orders.size() == 2, "H2: two <Subtitle> sections written");
    check(orders.size() == 2 && orders[0] != orders[1], "H2: the two <Order> values differ");

    TTAVData re;
    check(openProject(re, project), "H2: reload finished");
    if (re.avCount() != 1) { check(false, "H2: reload has one AV item"); return; }
    TTAVItem* back = re.avItemAt(0);
    dumpSubtitles("reloaded", back);
    check(back->subtitleCount() == 2, "H2: reload has two subtitles");
    if (back->subtitleCount() != 2) return;
    for (int i = 0; i < back->subtitleCount(); ++i) {
        const TTSubtitleItem s = back->subtitleListItemAt(i);
        const QString f = QFileInfo(s.getFileName()).fileName();
        check(s.getLanguage() == want[f].first,
              QString("H2: %1 keeps language %2").arg(f, want[f].first));
        check(s.getDelayMs() == want[f].second,
              QString("H2: %1 keeps delay %2").arg(f).arg(want[f].second));
    }
    check(QFileInfo(back->subtitleListItemAt(0).getFileName()).fileName() == "trk_eng.srt",
          "H2: saved order restored (trk_eng.srt first)");
}

// --- H2b -------------------------------------------------------------------
static void testLegacySubtitleOrder(const QString& workDir)
{
    printf("== H2b: project with <Order>-1 on every subtitle (written before the fix)\n");
    const QString dir = stage(workDir, "h2b", false);
    writeText(QDir(dir).absoluteFilePath("trk_deu.srt"), srt(2));
    writeText(QDir(dir).absoluteFilePath("trk_eng.srt"), srt(3));
    const QString project = QDir(dir).absoluteFilePath("legacy.ttcut");
    writeText(project, QString(
        "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
        " <Video>\n  <Order>0</Order>\n  <Name>%1</Name>\n"
        "  <Subtitle><Order>-1</Order><Name>%2</Name><Language>ita</Language><Delay>222</Delay></Subtitle>\n"
        "  <Subtitle><Order>-1</Order><Name>%3</Name><Language>fra</Language><Delay>111</Delay></Subtitle>\n"
        " </Video>\n</TTCut-Projectfile>\n")
        .arg(QDir(dir).absoluteFilePath("trk.264"), QDir(dir).absoluteFilePath("trk_eng.srt"),
             QDir(dir).absoluteFilePath("trk_deu.srt")));

    TTAVData av;
    check(openProject(av, project), "H2b: load finished");
    if (av.avCount() != 1) { check(false, "H2b: one AV item"); return; }
    TTAVItem* item = av.avItemAt(0);
    dumpSubtitles("loaded", item);
    check(item->subtitleCount() == 2, "H2b: two subtitles");
    if (item->subtitleCount() != 2) return;
    for (int i = 0; i < 2; ++i) {
        const TTSubtitleItem s = item->subtitleListItemAt(i);
        const QString f = QFileInfo(s.getFileName()).fileName();
        const QString lang = f == "trk_eng.srt" ? "ita" : "fra";
        const int delay    = f == "trk_eng.srt" ? 222 : 111;
        check(s.getLanguage() == lang && s.getDelayMs() == delay,
              QString("H2b: %1 has %2/%3").arg(f, lang).arg(delay));
    }
    check(QFileInfo(item->subtitleListItemAt(0).getFileName()).fileName() == "trk_eng.srt",
          "H2b: file order kept (trk_eng.srt first)");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
        return 2;
    }
    const QString workDir = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(workDir);

    testInfoLanguage(workDir);
    testSubtitleRoundTrip(workDir);
    testLegacySubtitleOrder(workDir);

    printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
