// Gate for ttMpvLoadfileCommand(): the CLI-form per-file options the wrapper
// hands to loadfile ("--sub-file=...") must survive mpv's options-list
// parsing whatever the file name contains. Established 2026-09-05 after a
// recording named "03x11_-_Da_glaub_i,_haben_wir_..." lost its subtitles in
// main-window playback: the list is comma-separated, mpv cut the path at the
// comma ("Error parsing option _haben_wir_..._deu.srt,pause (option not
// found)") and dropped every option after it, --pause=yes included.
//
// Part 1 checks the pure transform; part 2 feeds its result to a real libmpv
// instance (vo=null, ao=null) on a file with a comma, a space, an umlaut, a
// double quote and a bracket in its path and expects the subtitle track and
// the pause state to arrive. Prints PASS/FAIL per check.
//
// Usage: test_mpv_loadfile_args <video> <subtitle-with-awkward-name.srt>
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <clocale>
#include <cstdio>
#include <mpv/client.h>
#include "gui/ttmpvlibbackend.h"

static int fails = 0;
static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start under a comma-decimal locale
    if (argc < 3) { fprintf(stderr, "usage: %s <video> <subtitle.srt>\n", argv[0]); return 2; }
    const QString video = argv[1];
    const QString srt   = argv[2];

    // --- Part 1: pure transform -------------------------------------------
    QStringList plain = ttMpvLoadfileCommand({"loadfile", "/x/y.mkv"});
    check(plain == QStringList({"loadfile", "/x/y.mkv"}), "loadfile without options unchanged");
    QStringList other = ttMpvLoadfileCommand({"seek", "10", "absolute"});
    check(other == QStringList({"seek", "10", "absolute"}), "non-loadfile command unchanged");

    const QString awkward = QString::fromUtf8("/tmp/k\xc3\xb6mma, t\xc3\xa9st/a,b\"c]d_deu.srt");
    QStringList out = ttMpvLoadfileCommand({"loadfile", "/x/y.mkv", "--start=10.5",
                                            "--sub-file=" + awkward, "--pause=yes"});
    check(out.size() == 5, "options folded into one positional argument");
    check(out.size() >= 4 && out[2] == "replace" && out[3] == "0", "flag and index inserted");
    const QString opts = out.size() == 5 ? out[4] : QString();
    // Every value length-prefixed (mpv's %len% quoting, UTF-8 bytes), so the
    // list parser never splits inside a value.
    check(opts.startsWith("start=%4%10.5,"), "start value length-prefixed");
    check(opts.contains(QString("sub-file=%") + QString::number(awkward.toUtf8().size()) + "%" + awkward),
          "sub-file value length-prefixed with its UTF-8 byte length");
    check(opts.endsWith(",pause=%3%yes"), "pause value length-prefixed");

    // --- Part 2: live libmpv -----------------------------------------------
    check(QFile::exists(video) && QFile::exists(srt), "fixture files exist");
    mpv_handle* m = mpv_create();
    mpv_set_option_string(m, "vo", "null");
    mpv_set_option_string(m, "ao", "null");
    mpv_set_option_string(m, "idle", "yes");
    mpv_set_option_string(m, "terminal", "no");
    mpv_request_log_messages(m, "error");
    check(mpv_initialize(m) >= 0, "libmpv initialised");

    QStringList live = ttMpvLoadfileCommand({"loadfile", video, "--start=1.0",
                                             "--sub-file=" + srt, "--pause=yes"});
    QList<QByteArray> utf8;
    for (const QString& s : live) utf8.append(s.toUtf8());
    QVector<const char*> cargv;
    for (const QByteArray& b : utf8) cargv.append(b.constData());
    cargv.append(nullptr);
    int rc = mpv_command(m, cargv.data());
    check(rc >= 0, "loadfile accepted");

    bool loaded = false, errorSeen = false;
    QString tracks, sid, pause;
    for (int i = 0; i < 300 && !loaded; ++i) {
        mpv_event* ev = mpv_wait_event(m, 0.1);
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* lm = static_cast<mpv_event_log_message*>(ev->data);
            QString t = QString::fromUtf8(lm->text);
            if (!t.contains("mmco")) { printf("  mpv: %s", lm->text); errorSeen = true; }
        } else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            char* a = mpv_get_property_string(m, "track-list/count");
            char* b = mpv_get_property_string(m, "sid");
            char* c = mpv_get_property_string(m, "pause");
            tracks = a ? a : ""; sid = b ? b : ""; pause = c ? c : "";
            mpv_free(a); mpv_free(b); mpv_free(c);
            loaded = true;
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            break;
        }
    }
    mpv_terminate_destroy(m);
    check(loaded, "file loaded");
    check(!errorSeen, "no mpv error message while loading");
    check(tracks == "3" && sid == "1", "subtitle track present and selected (tracks=3, sid=1)");
    check(pause == "yes", "pause option after the subtitle path still applied");

    printf(fails ? "FAILED (%d)\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
