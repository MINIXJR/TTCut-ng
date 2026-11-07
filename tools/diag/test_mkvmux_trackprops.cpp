// Does the muxer write the track name and the track flags (Original,
// visually impaired, hearing impaired, forced) the user set, and only the
// ones that fit the track type? Reads the streams back with libav:
// `title` from the stream metadata, the flags from stream->disposition.
//
//   mux: audio        name + Original + visually + hearing impaired on audio
//   mux: subtitle     hearing impaired + forced on a subtitle, no name
//   mux: type filter  forced on audio, visually impaired on a subtitle ->
//                     neither written
//   mux: empty props  no props at all -> no title, audio only DEFAULT
//   mka: audio only   muxAudioOnly() with visually impaired
//
// The subtitle file is written by the harness (3 SRT cues).
//
//   usage: test_mkvmux_trackprops <video.264> <audio.ac3> <subtitle.srt> <fps> <workdir>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>
#include <cstdio>

#include "common/tttrackprops.h"
#include "extern/ttmkvmergeprovider.h"

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
}

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static const QString kAdName = QStringLiteral("Hörfilm & \"Kommentar\" <5.1>");

struct StreamInfo {
    bool    found = false;
    QString title;
    bool    hasTitle = false;
    int     disposition = 0;
};

// First stream of the given type in 'file'.
static StreamInfo readStream(const QString& file, AVMediaType type)
{
    StreamInfo si;
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, file.toUtf8().constData(), nullptr, nullptr) < 0) return si;
    avformat_find_stream_info(ctx, nullptr);
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream* st = ctx->streams[i];
        if (st->codecpar->codec_type != type) continue;
        si.found = true;
        si.disposition = st->disposition;
        const AVDictionaryEntry* t = av_dict_get(st->metadata, "title", nullptr, 0);
        if (t) { si.hasTitle = true; si.title = QString::fromUtf8(t->value); }
        break;
    }
    avformat_close_input(&ctx);
    return si;
}

static void dump(const char* tag, const StreamInfo& si)
{
    printf("  %s found=%d title=%s'%s' disp: default=%d original=%d visual=%d hearing=%d forced=%d\n",
           tag, si.found, si.hasTitle ? "" : "(none)", qPrintable(si.title),
           !!(si.disposition & AV_DISPOSITION_DEFAULT), !!(si.disposition & AV_DISPOSITION_ORIGINAL),
           !!(si.disposition & AV_DISPOSITION_VISUAL_IMPAIRED),
           !!(si.disposition & AV_DISPOSITION_HEARING_IMPAIRED), !!(si.disposition & AV_DISPOSITION_FORCED));
}

static bool has(const StreamInfo& si, int flag) { return (si.disposition & flag) != 0; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 6) {
        fprintf(stderr, "usage: %s <video.264> <audio.ac3> <subtitle.srt> <fps> <workdir>\n", argv[0]);
        return 2;
    }
    const QString video    = QString::fromLocal8Bit(argv[1]);
    const QString audio    = QString::fromLocal8Bit(argv[2]);
    const QString subtitle = QString::fromLocal8Bit(argv[3]);
    const double  fps      = QString(argv[4]).toDouble();
    const QDir    work(QString::fromLocal8Bit(argv[5]));
    QDir().mkpath(work.absolutePath());

    {
        QFile f(subtitle);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            fprintf(stderr, "cannot write %s\n", qPrintable(subtitle));
            return 2;
        }
        QTextStream s(&f);
        for (int i = 1; i <= 3; ++i)
            s << i << "\n00:00:0" << i << ",000 --> 00:00:0" << i << ",800\nLine " << i << "\n\n";
    }

    TTMkvVideoOptions opts;
    opts.frameRate = fps;
    opts.codecId   = AV_CODEC_ID_H264;

    // mux: audio + mux: subtitle (one container)
    {
        printf("== mux: audio / subtitle\n");
        TTTrackProps a;
        a.name = kAdName;
        a.original = a.visualImpaired = a.hearingImpaired = true;
        TTTrackProps s;
        s.hearingImpaired = s.forced = true;
        const QString out = work.filePath("props.mkv");
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        p.setAudioLanguages({"deu"});
        p.setSubtitleLanguages({"deu"});
        p.setAudioTrackProps({a});
        p.setSubtitleTrackProps({s});
        check(p.mux(out, video, {audio}, {subtitle}), "mux: props.mkv written");
        const StreamInfo sa = readStream(out, AVMEDIA_TYPE_AUDIO);
        const StreamInfo ss = readStream(out, AVMEDIA_TYPE_SUBTITLE);
        dump("audio", sa);
        dump("subtitle", ss);
        check(sa.found && sa.title == kAdName, "mux: audio title (umlaut + XML specials)");
        check(has(sa, AV_DISPOSITION_ORIGINAL) && has(sa, AV_DISPOSITION_VISUAL_IMPAIRED) &&
              has(sa, AV_DISPOSITION_HEARING_IMPAIRED), "mux: audio Original + visually + hearing impaired");
        check(has(sa, AV_DISPOSITION_DEFAULT), "mux: audio keeps DEFAULT");
        check(ss.found && !ss.hasTitle, "mux: subtitle without a name has no title tag");
        check(has(ss, AV_DISPOSITION_HEARING_IMPAIRED) && has(ss, AV_DISPOSITION_FORCED),
              "mux: subtitle hearing impaired + forced");
    }

    // mux: type filter
    {
        printf("== mux: type filter\n");
        TTTrackProps a;
        a.forced = true;
        TTTrackProps s;
        s.visualImpaired = true;
        const QString out = work.filePath("filter.mkv");
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        p.setAudioTrackProps({a});
        p.setSubtitleTrackProps({s});
        check(p.mux(out, video, {audio}, {subtitle}), "mux: filter.mkv written");
        const StreamInfo sa = readStream(out, AVMEDIA_TYPE_AUDIO);
        const StreamInfo ss = readStream(out, AVMEDIA_TYPE_SUBTITLE);
        dump("audio", sa);
        dump("subtitle", ss);
        check(sa.found && !has(sa, AV_DISPOSITION_FORCED), "mux: type filter (no forced on audio)");
        check(ss.found && !has(ss, AV_DISPOSITION_VISUAL_IMPAIRED),
              "mux: type filter (no visually impaired on a subtitle)");
    }

    // mux: empty props
    {
        printf("== mux: empty props\n");
        const QString out = work.filePath("empty.mkv");
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        check(p.mux(out, video, {audio}, {subtitle}), "mux: empty.mkv written");
        const StreamInfo sa = readStream(out, AVMEDIA_TYPE_AUDIO);
        const StreamInfo ss = readStream(out, AVMEDIA_TYPE_SUBTITLE);
        dump("audio", sa);
        dump("subtitle", ss);
        check(sa.found && !sa.hasTitle && sa.disposition == AV_DISPOSITION_DEFAULT,
              "mux: empty props (audio: no title, only DEFAULT)");
        check(ss.found && !ss.hasTitle && ss.disposition == 0, "mux: empty props (subtitle: no title, no flag)");
    }

    // mka: audio only
    {
        printf("== mka: audio only\n");
        TTTrackProps a;
        a.visualImpaired = true;
        a.name = "AD";
        const QString out = work.filePath("audio.mka");
        TTMkvMergeProvider p;
        check(p.muxAudioOnly(out, {audio}, {"deu"}, {a}), "mka: audio.mka written");
        const StreamInfo sa = readStream(out, AVMEDIA_TYPE_AUDIO);
        dump("audio", sa);
        check(sa.found && has(sa, AV_DISPOSITION_VISUAL_IMPAIRED) && sa.title == "AD",
              "mka: audio only (visually impaired + title)");
    }

    printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
