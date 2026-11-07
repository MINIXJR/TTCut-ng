// Per-track name and MKV flags (Original, visually impaired, hearing
// impaired, forced): the value type, the item plumbing, the prefill on open,
// the project file and the track list columns.
// Plan docs/superpowers/plans/2026-11-06-mkv-track-flags.md.
//
// Every value is printed before it is checked, so a FAIL shows what the
// code actually did.
//
//   usage: test_track_props <workdir>
//
// Build via `cmake --build build --target test_track_props`.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

#include "common/tttrackprops.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "common/ttsettings.h"
#include "data/ttcutprojectdata.h"
#include "gui/ttaudiotreeview.h"
#include "gui/ttsubtitletreeview.h"

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
static const QString kAdName = QStringLiteral("Hörfilm & \"Kommentar\" <5.1>");

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

// A fresh directory with the video linked as "trk.264", each audio name
// linked to the Tux AC3 and each subtitle name written as a small SRT.
static QString stage(const QString& workDir, const QString& name,
                     const QStringList& audios, const QStringList& subtitles = {})
{
    QDir(workDir).mkpath(name);
    QDir dir(QDir(workDir).absoluteFilePath(name));
    for (const QString& e : dir.entryList(QDir::Files)) dir.remove(e);
    QFile::link(kVideoFile, dir.absoluteFilePath("trk.264"));
    for (const QString& a : audios) QFile::link(kAudioFile, dir.absoluteFilePath(a));
    for (const QString& s : subtitles) writeText(dir.absoluteFilePath(s), srt(3));
    return dir.absolutePath();
}

static QString infoText(const QString& audioLines, const QString& subtitleLines = {})
{
    return "[video]\nfile=trk.264\ncodec=h264\nwidth=720\nheight=576\nframe_rate=25/1\n"
           "start_pts=0.000000\n\n" + audioLines + "\n" + subtitleLines;
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

static QString fileOf(const TTAudioItem& a)    { return QFileInfo(a.getFileName()).fileName(); }

static int audioIndexOf(TTAVItem* item, const QString& file)
{
    for (int i = 0; i < item->audioCount(); ++i)
        if (fileOf(item->audioListItemAt(i)) == file) return i;
    return -1;
}

static void dumpProps(const char* tag, int i, const QString& file, const QString& lang,
                      const TTTrackProps& p)
{
    printf("  %s[%d] %s lang=%s name='%s' flags='%s' viPre=%d hiPre=%d langPre=%d\n", tag, i,
           qPrintable(file), qPrintable(lang), qPrintable(p.name), qPrintable(p.flagsToken()),
           int(p.visualImpairedPrefill), int(p.hearingImpairedPrefill), int(p.languagePrefill));
}

static void dumpAudio(TTAVItem* item)
{
    for (int i = 0; i < item->audioCount(); ++i) {
        const TTAudioItem a = item->audioListItemAt(i);
        dumpProps("audio", i, fileOf(a), a.getLanguage(), a.trackProps());
    }
}

// --- model -----------------------------------------------------------------
static void testModelValueType()
{
    printf("== model: value type\n");
    TTTrackProps all;
    all.original = all.visualImpaired = all.hearingImpaired = all.forced = true;
    printf("  all flags token '%s'\n", qPrintable(all.flagsToken()));
    check(all.flagsToken() == "original,visual-impaired,hearing-impaired,forced",
          "model: token round trip (all four, fixed order)");
    TTTrackProps none;
    check(none.flagsToken().isEmpty(), "model: no flag -> empty token");
    TTTrackProps p = all;
    p.setFlagsFromToken("forced,bogus");
    printf("  after 'forced,bogus' token '%s'\n", qPrintable(p.flagsToken()));
    check(p.forced && !p.original && !p.visualImpaired && !p.hearingImpaired,
          "model: token round trip (setFlagsFromToken replaces, ignores unknown)");
    TTTrackProps q;
    q.setFlagsFromToken(all.flagsToken());
    check(q.flagsToken() == all.flagsToken(), "model: token round trip (parse of written token)");

    TTTrackProps mis;
    mis.prefillFromAudioLanguage("mis");
    check(mis.visualImpaired && mis.visualImpairedPrefill == TTTrackPrefill::LanguageMis &&
          mis.languagePrefill == TTTrackPrefill::LanguageMis,
          "model: mis prefill (visually impaired + both prefill reasons)");
    check(!mis.hearingImpaired && mis.hearingImpairedPrefill == TTTrackPrefill::None,
          "model: mis prefill leaves hearing impaired alone");
    TTTrackProps qks;
    qks.prefillFromAudioLanguage("qks");
    check(qks.hearingImpaired && qks.hearingImpairedPrefill == TTTrackPrefill::LanguageQks &&
          qks.languagePrefill == TTTrackPrefill::LanguageQks,
          "model: qks prefill (hearing impaired + both prefill reasons)");
    check(!qks.visualImpaired && !qks.original && !qks.forced &&
          qks.visualImpairedPrefill == TTTrackPrefill::None,
          "model: qks prefill sets no other flag");
    for (const char* lang : {"mul", "deu", "qaa"}) {
        TTTrackProps o;
        o.prefillFromAudioLanguage(lang);
        check(o.flagsToken().isEmpty() && o.visualImpairedPrefill == TTTrackPrefill::None &&
              o.hearingImpairedPrefill == TTTrackPrefill::None &&
              o.languagePrefill == TTTrackPrefill::None,
              QString("model: no prefill for %1").arg(lang));
    }
}

static void testModelItemSlots(const QString& workDir)
{
    printf("== model: item slots\n");
    const QString dir = stage(workDir, "slots", {"trk_deu.ac3"}, {"trk_deu.srt"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "model: item slots open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1 ||
        av.avItemAt(0)->subtitleCount() != 1) {
        check(false, "model: item slots one audio + one subtitle");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    item->onAudioNameChanged(0, kAdName);
    item->onAudioFlagChanged(0, TTTrackFlag::Original, true);
    item->onSubtitleFlagChanged(0, TTTrackFlag::Forced, true);
    item->onSubtitleNameChanged(0, "UT");
    // Out-of-range indices are ignored (no crash, nothing changed).
    item->onAudioNameChanged(5, "x");
    item->onAudioFlagChanged(-1, TTTrackFlag::Forced, true);
    item->onSubtitleNameChanged(5, "x");
    item->onSubtitleFlagChanged(5, TTTrackFlag::Forced, true);

    const TTTrackProps a = item->audioListItemAt(0).trackProps();
    const TTTrackProps s = item->subtitleListItemAt(0).trackProps();
    dumpProps("audio", 0, fileOf(item->audioListItemAt(0)), "", a);
    dumpProps("subtitle", 0, "", "", s);
    check(a.name == kAdName && a.original && !a.forced, "model: item slots audio name + Original");
    check(s.name == "UT" && s.forced && !s.original, "model: item slots subtitle name + Forced");

    // A name is stored without surrounding blanks: blanks only = no name.
    item->onAudioNameChanged(0, "   ");
    item->onSubtitleNameChanged(0, "  UT DE ");
    printf("  trimmed: audio='%s' subtitle='%s'\n", qPrintable(item->audioListItemAt(0).trackProps().name),
           qPrintable(item->subtitleListItemAt(0).trackProps().name));
    check(item->audioListItemAt(0).trackProps().name.isEmpty(), "model: blank audio name stored as no name");
    check(item->subtitleListItemAt(0).trackProps().name == "UT DE", "model: subtitle name stored trimmed");
}

static void testModelSwapKeepsProps(const QString& workDir)
{
    printf("== model: swap keeps props\n");
    const QString dir = stage(workDir, "swap", {"trk_deu.ac3", "trk_eng.ac3"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "model: swap open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2) {
        check(false, "model: swap two audio tracks");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    const int deu = audioIndexOf(item, "trk_deu.ac3");
    item->onAudioNameChanged(deu, "A");
    item->onSwapAudioItems(0, 1);
    dumpAudio(item);
    const int after = audioIndexOf(item, "trk_deu.ac3");
    check(after >= 0 && after != deu, "model: swap moved the deu track");
    check(after >= 0 && item->audioListItemAt(after).trackProps().name == "A",
          "model: swap keeps props (deu track still named A)");
    check(item->audioListItemAt(1 - after).trackProps().name.isEmpty(),
          "model: swap keeps props (other track unnamed)");
}

// --- prefill on open --------------------------------------------------------
static void checkNeverForcedOriginal(TTAVItem* item, const char* stageName)
{
    bool ok = true;
    for (int i = 0; i < item->audioCount(); ++i) {
        const TTTrackProps p = item->audioListItemAt(i).trackProps();
        ok = ok && !p.forced && !p.original;
    }
    for (int i = 0; i < item->subtitleCount(); ++i) {
        const TTTrackProps p = item->subtitleListItemAt(i).trackProps();
        ok = ok && !p.forced && !p.original;
    }
    check(ok, QString("prefill: never forced/original (%1)").arg(stageName));
}

static void testPrefillMisAudio(const QString& workDir)
{
    printf("== prefill: mis audio\n");
    const QString dir = stage(workDir, "pre_mis", {"trk_deu.ac3", "trk_mis.ac3"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "prefill: mis audio open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2) {
        check(false, "prefill: mis audio two tracks");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    dumpAudio(item);
    const TTAudioItem mis = item->audioListItemAt(audioIndexOf(item, "trk_mis.ac3"));
    const TTAudioItem deu = item->audioListItemAt(audioIndexOf(item, "trk_deu.ac3"));
    check(mis.trackProps().visualImpaired &&
          mis.trackProps().visualImpairedPrefill == TTTrackPrefill::LanguageMis &&
          mis.trackProps().languagePrefill == TTTrackPrefill::LanguageMis,
          "prefill: mis audio -> visually impaired (reason LanguageMis)");
    check(mis.getLanguage() == "deu", "prefill: mis audio -> language deu");
    check(!deu.trackProps().visualImpaired && deu.getLanguage() == "deu" &&
          deu.trackProps().languagePrefill == TTTrackPrefill::None,
          "prefill: deu audio untouched");
    checkNeverForcedOriginal(item, "mis audio");

    // The prefill runs once: a later language change neither sets nor clears it.
    item->onAudioLanguageChanged(audioIndexOf(item, "trk_mis.ac3"), "eng");
    item->onAudioLanguageChanged(audioIndexOf(item, "trk_deu.ac3"), "mis");
    dumpAudio(item);
    const TTAudioItem misAfter = item->audioListItemAt(audioIndexOf(item, "trk_mis.ac3"));
    const TTAudioItem deuAfter = item->audioListItemAt(audioIndexOf(item, "trk_deu.ac3"));
    check(misAfter.getLanguage() == "eng" && misAfter.trackProps().visualImpaired &&
          misAfter.trackProps().visualImpairedPrefill == TTTrackPrefill::LanguageMis &&
          misAfter.trackProps().languagePrefill == TTTrackPrefill::LanguageMis,
          "prefill: language change to eng keeps the mis prefill");
    check(deuAfter.getLanguage() == "mis" && !deuAfter.trackProps().visualImpaired &&
          deuAfter.trackProps().languagePrefill == TTTrackPrefill::None,
          "prefill: language change to mis does not prefill");
}

static void testPrefillMisFromInfo(const QString& workDir)
{
    printf("== prefill: mis from .info\n");
    const QString dir = stage(workDir, "pre_info", {"trk.ac3"});
    writeText(QDir(dir).absoluteFilePath("trk.info"),
              infoText("[audio]\ncount=1\naudio_0_file=trk.ac3\naudio_0_codec=ac3\naudio_0_lang=mis\n"));
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "prefill: mis from .info open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) {
        check(false, "prefill: mis from .info one track");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    dumpAudio(item);
    const TTAudioItem a = item->audioListItemAt(0);
    check(a.trackProps().visualImpaired && a.getLanguage() == "deu",
          "prefill: mis from .info -> visually impaired + deu");
    checkNeverForcedOriginal(item, "mis from .info");
}

static void testPrefillQksAudio(const QString& workDir)
{
    printf("== prefill: qks audio\n");
    const QString dir = stage(workDir, "pre_qks", {"trk_deu.ac3", "trk_qks.ac3"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "prefill: qks audio open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2) {
        check(false, "prefill: qks audio two tracks");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    dumpAudio(item);
    const TTAudioItem qks = item->audioListItemAt(audioIndexOf(item, "trk_qks.ac3"));
    const TTAudioItem deu = item->audioListItemAt(audioIndexOf(item, "trk_deu.ac3"));
    check(qks.trackProps().hearingImpaired &&
          qks.trackProps().hearingImpairedPrefill == TTTrackPrefill::LanguageQks &&
          qks.trackProps().languagePrefill == TTTrackPrefill::LanguageQks,
          "prefill: qks audio -> hearing impaired (reason LanguageQks)");
    check(!qks.trackProps().visualImpaired, "prefill: qks audio not visually impaired");
    check(qks.getLanguage() == "deu", "prefill: qks audio -> language deu");
    check(!deu.trackProps().hearingImpaired && deu.getLanguage() == "deu" &&
          deu.trackProps().languagePrefill == TTTrackPrefill::None,
          "prefill: deu audio untouched (qks case)");
    checkNeverForcedOriginal(item, "qks audio");
}

// Between equal ranks the arrival order of the parallel open tasks decides
// (TTAudioList::append), which the test cannot steer. So the other track is
// "eng", second in the preference list: after the prefill turned the code
// into "deu" (rank 0), only the sort rule (rank INT_MAX) puts the prefilled
// track behind a rank-1 track.
static void testPrefillSortsBehind(const QString& workDir)
{
    for (const char* code : {"mis", "qks"}) {
        printf("== prefill: %s track sorts behind\n", code);
        TTSettings* settings = TTSettings::instance();
        const QStringList savedPrefs = settings->audioLanguagePreference();
        settings->setAudioLanguagePreference({"deu", "eng"});

        const QString dir = stage(workDir, QString("pre_sort_%1").arg(code), {"trk_a.ac3", "trk_b.ac3"});
        writeText(QDir(dir).absoluteFilePath("trk.info"),
                  infoText(QString("[audio]\ncount=2\naudio_0_file=trk_a.ac3\naudio_0_codec=ac3\naudio_0_lang=%1\n"
                                   "audio_1_file=trk_b.ac3\naudio_1_codec=ac3\naudio_1_lang=eng\n").arg(code)));
        TTAVData av;
        const bool opened = openPlain(av, QDir(dir).absoluteFilePath("trk.264"));
        settings->setAudioLanguagePreference(savedPrefs);
        check(opened, QString("prefill: sort %1 open finished").arg(code));
        if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2) {
            check(false, QString("prefill: sort %1 two tracks").arg(code));
            continue;
        }
        TTAVItem* item = av.avItemAt(0);
        dumpAudio(item);
        check(fileOf(item->audioListItemAt(0)) == "trk_b.ac3",
              QString("prefill: %1 track sorts behind (trk_b.ac3 at position 0)").arg(code));
        check(item->audioListItemAt(0).getLanguage() == "eng" &&
              item->audioListItemAt(1).getLanguage() == "deu",
              QString("prefill: sort %1 languages eng, deu").arg(code));
    }
}

static void testPrefillProjectLanguageWins(const QString& workDir)
{
    printf("== prefill: project language wins\n");
    for (const char* lang : {"mis", "deu"}) {
        const QString dir = stage(workDir, QString("pre_proj_%1").arg(lang), {"trk_mis.ac3"});
        const QString project = QDir(dir).absoluteFilePath("p.ttcut");
        writeText(project, QString(
            "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
            " <Video>\n  <Order>0</Order>\n  <Name>%1</Name>\n"
            "  <Audio><Order>0</Order><Name>%2</Name><Language>%3</Language></Audio>\n"
            " </Video>\n</TTCut-Projectfile>\n")
            .arg(QDir(dir).absoluteFilePath("trk.264"), QDir(dir).absoluteFilePath("trk_mis.ac3"), lang));
        TTAVData av;
        check(openProject(av, project), QString("prefill: project (%1) load finished").arg(lang));
        if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) {
            check(false, "prefill: project one track");
            continue;
        }
        dumpAudio(av.avItemAt(0));
        check(av.avItemAt(0)->audioListItemAt(0).getLanguage() == lang,
              QString("prefill: project language wins (%1 stays %1)").arg(lang));
    }
}

static void testPrefillSubtitleFromInfo(const QString& workDir)
{
    printf("== prefill: subtitle from .info\n");
    const QString dir = stage(workDir, "pre_sub", {}, {"trk_deu.srt", "trk_eng.srt"});
    writeText(QDir(dir).absoluteFilePath("trk.info"),
              infoText("", "[subtitles]\ncount=2\nsub_0_file=trk_deu.srt\nsub_0_hearing_impaired=1\n"
                           "sub_1_file=trk_eng.srt\n"));
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "prefill: subtitle open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->subtitleCount() != 2) {
        check(false, "prefill: subtitle two tracks");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    for (int i = 0; i < item->subtitleCount(); ++i) {
        const TTSubtitleItem s = item->subtitleListItemAt(i);
        const QString f = QFileInfo(s.getFileName()).fileName();
        dumpProps("subtitle", i, f, s.getLanguage(), s.trackProps());
        const TTTrackProps p = s.trackProps();
        if (f == "trk_deu.srt")
            check(p.hearingImpaired && p.hearingImpairedPrefill == TTTrackPrefill::RecordingHearingImpaired &&
                  !p.forced && !p.original,
                  "prefill: subtitle from .info -> hearing impaired, not forced/original");
        else
            check(!p.hearingImpaired && p.hearingImpairedPrefill == TTTrackPrefill::None,
                  "prefill: subtitle without the key -> not hearing impaired");
    }
    checkNeverForcedOriginal(item, "subtitle from .info");
}

// --- project file ------------------------------------------------------------
static QString readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
}

static void testProjectRoundTrip(const QString& workDir)
{
    printf("== project: round trip\n");
    const QString dir = stage(workDir, "proj_rt", {"trk_mis.ac3"}, {"trk_deu.srt"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "project: round trip open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1 || av.avItemAt(0)->subtitleCount() != 1) {
        check(false, "project: round trip one audio + one subtitle");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    item->onAudioNameChanged(0, kAdName);
    item->onAudioFlagChanged(0, TTTrackFlag::Original, true);
    item->onSubtitleFlagChanged(0, TTTrackFlag::Forced, true);
    const TTTrackProps wantA = item->audioListItemAt(0).trackProps();
    const TTTrackProps wantS = item->subtitleListItemAt(0).trackProps();
    dumpProps("saved audio", 0, "", item->audioListItemAt(0).getLanguage(), wantA);
    dumpProps("saved subtitle", 0, "", "", wantS);

    const QString project = QDir(dir).absoluteFilePath("rt.ttcut");
    av.writeProjectFile(QFileInfo(project), {}, TTLogoProjectData());
    TTAVData re;
    check(openProject(re, project), "project: round trip reload finished");
    if (re.avCount() != 1 || re.avItemAt(0)->audioCount() != 1 || re.avItemAt(0)->subtitleCount() != 1) {
        check(false, "project: round trip reload one audio + one subtitle");
        return;
    }
    const TTTrackProps a = re.avItemAt(0)->audioListItemAt(0).trackProps();
    const TTTrackProps s = re.avItemAt(0)->subtitleListItemAt(0).trackProps();
    dumpProps("loaded audio", 0, "", re.avItemAt(0)->audioListItemAt(0).getLanguage(), a);
    dumpProps("loaded subtitle", 0, "", "", s);
    check(a.name == kAdName, "project: round trip audio name (umlaut + XML specials)");
    check(a.flagsToken() == "original,visual-impaired", "project: round trip audio flags (Original + Visually impaired)");
    check(s.name.isEmpty() && s.flagsToken() == "forced", "project: round trip subtitle flags (Forced)");
    check(re.avItemAt(0)->audioListItemAt(0).getLanguage() == "deu", "project: round trip audio language deu");
}

static void testProjectEmptyFlagsClearPrefill(const QString& workDir)
{
    printf("== project: empty TrackFlags clears prefill\n");
    const QString dir = stage(workDir, "proj_clear", {"trk_mis.ac3"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "project: clear open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) { check(false, "project: clear one audio"); return; }
    av.avItemAt(0)->onAudioFlagChanged(0, TTTrackFlag::VisualImpaired, false);
    const QString project = QDir(dir).absoluteFilePath("clear.ttcut");
    av.writeProjectFile(QFileInfo(project), {}, TTLogoProjectData());
    const QString xml = readFile(project);
    const bool hasEmpty = xml.contains("<TrackFlags/>") || xml.contains("<TrackFlags></TrackFlags>");
    printf("  xml has empty TrackFlags: %d, has TrackName: %d\n", hasEmpty, int(xml.contains("<TrackName")));
    check(hasEmpty, "project: empty TrackFlags element written");
    check(!xml.contains("<TrackName"), "project: no TrackName for an empty name");

    TTAVData re;
    check(openProject(re, project), "project: clear reload finished");
    if (re.avCount() != 1 || re.avItemAt(0)->audioCount() != 1) { check(false, "project: clear reload one audio"); return; }
    const TTTrackProps a = re.avItemAt(0)->audioListItemAt(0).trackProps();
    dumpProps("loaded audio", 0, "", re.avItemAt(0)->audioListItemAt(0).getLanguage(), a);
    check(!a.visualImpaired, "project: empty TrackFlags clears prefill (Visually impaired stays off)");
}

static void testProjectLegacyKeepsPrefill(const QString& workDir)
{
    printf("== project: legacy project keeps prefill\n");
    const QString dir = stage(workDir, "proj_legacy", {"trk_mis.ac3"});
    const QString project = QDir(dir).absoluteFilePath("legacy.ttcut");
    writeText(project, QString(
        "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
        " <Video>\n  <Order>0</Order>\n  <Name>%1</Name>\n"
        "  <Audio><Order>0</Order><Name>%2</Name><Language>mis</Language></Audio>\n"
        " </Video>\n</TTCut-Projectfile>\n")
        .arg(QDir(dir).absoluteFilePath("trk.264"), QDir(dir).absoluteFilePath("trk_mis.ac3")));
    TTAVData av;
    check(openProject(av, project), "project: legacy load finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) { check(false, "project: legacy one audio"); return; }
    const TTTrackProps a = av.avItemAt(0)->audioListItemAt(0).trackProps();
    dumpProps("loaded audio", 0, "", av.avItemAt(0)->audioListItemAt(0).getLanguage(), a);
    check(a.visualImpaired && a.name.isEmpty(), "project: legacy project keeps prefill (Visually impaired on, no name)");
}

// --- track list views ---------------------------------------------------------
static QStringList headerTexts(QTreeWidget* tree)
{
    QStringList h;
    for (int c = 0; c < tree->columnCount(); ++c) h << tree->headerItem()->text(c);
    return h;
}

template <typename W>
static W* cell(QTreeWidget* tree, int row, int column)
{
    QTreeWidgetItem* it = tree->topLevelItem(row);
    return it ? qobject_cast<W*>(tree->itemWidget(it, column)) : nullptr;
}

static void testViews(const QString& workDir)
{
    printf("== view: track lists\n");
    const QString dir = stage(workDir, "view", {"trk_deu.ac3", "trk_mis.ac3"}, {"trk_deu.srt"});
    writeText(QDir(dir).absoluteFilePath("trk.info"),
              infoText("", "[subtitles]\ncount=1\nsub_0_file=trk_deu.srt\nsub_0_hearing_impaired=1\n"));
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "view: open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2 || av.avItemAt(0)->subtitleCount() != 1) {
        check(false, "view: two audio + one subtitle");
        return;
    }
    TTAVItem* item = av.avItemAt(0);

    TTAudioTreeView audioView;
    TTSubtitleTreeView subView;
    audioView.onAVDataChanged(item);
    subView.onAVDataChanged(item);
    QTreeWidget* at = audioView.findChild<QTreeWidget*>();
    QTreeWidget* st = subView.findChild<QTreeWidget*>();
    if (!at || !st) { check(false, "view: tree widgets found"); return; }

    const QStringList ah = headerTexts(at), sh = headerTexts(st);
    printf("  audio header: %s\n  subtitle header: %s\n", qPrintable(ah.join(" | ")), qPrintable(sh.join(" | ")));
    check(ah.mid(8, 4) == QStringList({"Name", "Original", "Visually impaired", "Hearing impaired"}),
          "view: columns (audio 8-11)");
    check(sh.mid(4, 4) == QStringList({"Name", "Original", "Hearing impaired", "Forced"}),
          "view: columns (subtitle 4-7)");
    check(!ah.contains("Forced") && !sh.contains("Visually impaired"), "view: columns (no Forced in audio, no Visually impaired in subtitle)");

    const int misRow = audioIndexOf(item, "trk_mis.ac3");
    const int deuRow = audioIndexOf(item, "trk_deu.ac3");
    QCheckBox* misVi = cell<QCheckBox>(at, misRow, 10);
    QCheckBox* deuVi = cell<QCheckBox>(at, deuRow, 10);
    QComboBox* misLang = cell<QComboBox>(at, misRow, 7);
    QComboBox* deuLang = cell<QComboBox>(at, deuRow, 7);
    if (!misVi || !deuVi || !misLang || !deuLang) { check(false, "view: audio row widgets present"); return; }
    printf("  mis row: vi=%d tip='%s' lang=%s langTip='%s'\n", misVi->isChecked(), qPrintable(misVi->toolTip()),
           qPrintable(misLang->currentData().toString()), qPrintable(misLang->toolTip()));
    printf("  deu row: vi=%d tip='%s' langTip='%s'\n", deuVi->isChecked(), qPrintable(deuVi->toolTip()),
           qPrintable(deuLang->toolTip()));
    check(misVi->isChecked() && misVi->toolTip() == "Prefilled from language code \"mis\"",
          "view: prefill shown (mis row Visually impaired ticked, tooltip)");
    check(misLang->currentData().toString() == "deu" &&
          misLang->toolTip() == "Prefilled: the recording marks this track \"mis\" (audio description)",
          "view: prefill shown (mis row language deu, tooltip)");
    check(!deuVi->isChecked() && deuVi->toolTip().isEmpty() && deuLang->toolTip().isEmpty(),
          "view: prefill shown (deu row unticked, no tooltips)");

    QCheckBox* subHi = cell<QCheckBox>(st, 0, 6);
    QCheckBox* subForced = cell<QCheckBox>(st, 0, 7);
    if (!subHi || !subForced) { check(false, "view: subtitle row widgets present"); return; }
    printf("  subtitle row: hi=%d tip='%s' forced=%d\n", subHi->isChecked(), qPrintable(subHi->toolTip()),
           subForced->isChecked());
    check(subHi->isChecked() &&
          subHi->toolTip() == "Prefilled from the recording: the broadcaster flags this subtitle for the hard of hearing"
          && !subForced->isChecked(), "view: prefill shown (subtitle Hearing impaired, not Forced)");

    misVi->setChecked(false);
    check(!item->audioListItemAt(misRow).trackProps().visualImpaired, "view: toggle reaches model");
    subForced->setChecked(true);
    check(item->subtitleListItemAt(0).trackProps().forced, "view: toggle reaches model (subtitle Forced)");

    QLineEdit* name0 = cell<QLineEdit>(at, 0, 8);
    if (!name0) { check(false, "view: name editor present"); return; }
    name0->setText("AD");          // setText alone emits no textEdited
    emit name0->textEdited("AD");  // what typing emits, before any focus change
    check(item->audioListItemAt(0).trackProps().name == "AD", "view: name reaches model without focus change");

    const QString movedFile = fileOf(item->audioListItemAt(0));
    at->setCurrentItem(at->topLevelItem(0));
    audioView.onItemDown();
    const int moved = audioIndexOf(item, movedFile);
    QLineEdit* movedName = cell<QLineEdit>(at, moved, 8);
    QLineEdit* otherName = cell<QLineEdit>(at, 1 - moved, 8);
    printf("  after down: %s at row %d, name '%s'; other row name '%s'\n", qPrintable(movedFile), moved,
           movedName ? qPrintable(movedName->text()) : "-", otherName ? qPrintable(otherName->text()) : "-");
    check(moved == 1 && movedName && movedName->text() == "AD" && otherName && otherName->text().isEmpty(),
          "view: swap keeps props");

    // Optional look at the lists: TRACK_PROPS_SHOT=<dir> saves both as PNG.
    const QString shotDir = qEnvironmentVariable("TRACK_PROPS_SHOT");
    if (!shotDir.isEmpty()) {
        for (QWidget* w : {static_cast<QWidget*>(&audioView), static_cast<QWidget*>(&subView)}) {
            w->resize(1500, 160);
            w->show();
        }
        QApplication::processEvents();
        audioView.grab().save(QDir(shotDir).absoluteFilePath("audio-list.png"));
        subView.grab().save(QDir(shotDir).absoluteFilePath("subtitle-list.png"));
    }
}

// flagChanged carries a TTTrackFlag; a queued connection must be able to
// copy it (Qt 6 takes the type from moc, no qRegisterMetaType needed).
static void testQueuedFlagSignal()
{
    printf("== view: flagChanged over a queued connection\n");
    TTAudioTreeView view;
    QObject receiver;
    bool got = false;
    QObject::connect(&view, &TTTrackTreeView::flagChanged, &receiver,
                     [&got](int row, TTTrackFlag flag, bool on) {
                         got = row == 2 && flag == TTTrackFlag::Forced && on;
                     }, Qt::QueuedConnection);
    emit view.flagChanged(2, TTTrackFlag::Forced, true);
    QCoreApplication::processEvents();
    check(got, "view: flagChanged arrives over a queued connection");
}

static void testViewQks(const QString& workDir)
{
    printf("== view: qks row\n");
    const QString dir = stage(workDir, "view_qks", {"trk_deu.ac3", "trk_qks.ac3"});
    TTAVData av;
    check(openPlain(av, QDir(dir).absoluteFilePath("trk.264")), "view qks: open finished");
    if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 2) {
        check(false, "view qks: two audio tracks");
        return;
    }
    TTAVItem* item = av.avItemAt(0);
    TTAudioTreeView audioView;
    audioView.onAVDataChanged(item);
    QTreeWidget* at = audioView.findChild<QTreeWidget*>();
    if (!at) { check(false, "view qks: tree widget found"); return; }
    const int qksRow = audioIndexOf(item, "trk_qks.ac3");
    QCheckBox* hi = cell<QCheckBox>(at, qksRow, 11);
    QCheckBox* vi = cell<QCheckBox>(at, qksRow, 10);
    QComboBox* lang = cell<QComboBox>(at, qksRow, 7);
    if (!hi || !vi || !lang) { check(false, "view qks: row widgets present"); return; }
    printf("  qks row: hi=%d tip='%s' vi=%d lang=%s langTip='%s'\n", hi->isChecked(), qPrintable(hi->toolTip()),
           vi->isChecked(), qPrintable(lang->currentData().toString()), qPrintable(lang->toolTip()));
    check(hi->isChecked() && hi->toolTip() == "Prefilled from language code \"qks\"",
          "view qks: Hearing impaired ticked, tooltip");
    check(!vi->isChecked() && vi->toolTip().isEmpty(), "view qks: Visually impaired unticked");
    check(lang->currentData().toString() == "deu" &&
          lang->toolTip() == "Prefilled: the recording marks this track \"qks\" (clear speech)",
          "view qks: language deu, tooltip");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // TRACK_PROPS_QM=<file.qm>: show the lists translated (for the optional
    // TRACK_PROPS_SHOT pictures; the checks above compare English texts).
    QTranslator translator;
    const QString qm = qEnvironmentVariable("TRACK_PROPS_QM");
    if (!qm.isEmpty() && translator.load(qm)) app.installTranslator(&translator);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
        return 2;
    }
    const QString workDir = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(workDir);

    testModelValueType();
    testModelItemSlots(workDir);
    testModelSwapKeepsProps(workDir);
    testPrefillMisAudio(workDir);
    testPrefillMisFromInfo(workDir);
    testPrefillQksAudio(workDir);
    testPrefillSortsBehind(workDir);
    testPrefillProjectLanguageWins(workDir);
    testPrefillSubtitleFromInfo(workDir);
    testProjectRoundTrip(workDir);
    testProjectEmptyFlagsClearPrefill(workDir);
    testProjectLegacyKeepsPrefill(workDir);
    testViews(workDir);
    testViewQks(workDir);
    testQueuedFlagSignal();

    printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
