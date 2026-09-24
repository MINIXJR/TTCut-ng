// Does the muxer admit it when an audio or subtitle input cannot be used?
//
// Code-audit run 7, hypothesis H3 (measured): TTMkvMergeProvider::mux()
// skipped an input it could not open with a qWarning and still returned true
// - three audio files in, one audio track out, no error. The final cuts had
// already checked the audio count on the CUT side, so nothing noticed a track
// that went missing in the container.
//
// Three runs on the same video + audio:
//   control  mux(video, {audio})                      -> true, 1 audio track
//   strict   mux(video, {audio, garbage, missing})    -> false, lastError
//            (default)                                   names both files,
//                                                        no output file
//   lenient  same inputs, setRequireAllInputs(false)  -> true, 1 audio track,
//                                                        droppedInputs() == 2
// plus muxAudioOnly() strict on {audio, garbage}      -> false
//
//   usage: test_mkvmux_inputs <video.264> <audio.ac3> <fps> <workdir>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <cstdio>

#include "extern/ttmkvmergeprovider.h"

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
}

static int fail(const char* what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

// Number of audio streams in a container, -1 when it cannot be opened.
static int audioTrackCount(const QString& file)
{
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, file.toUtf8().constData(), nullptr, nullptr) < 0) return -1;
    avformat_find_stream_info(ctx, nullptr);
    int n = 0;
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ++n;
    avformat_close_input(&ctx);
    return n;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        fprintf(stderr, "usage: %s <video.264> <audio.ac3> <fps> <workdir>\n", argv[0]);
        return 2;
    }
    const QString video = argv[1];
    const QString audio = argv[2];
    const double  fps   = QString(argv[3]).toDouble();
    const QDir    work(argv[4]);

    const QString garbage = work.filePath("garbage.ac3");
    const QString missing = work.filePath("missing.ac3");
    {
        QFile g(garbage);
        if (!g.open(QIODevice::WriteOnly)) return fail("cannot write garbage.ac3");
        QByteArray noise(64 * 1024, '\0');
        for (int i = 0; i < noise.size(); ++i) noise[i] = char((i * 7919) & 0xff);
        g.write(noise);
    }
    QFile::remove(missing);

    TTMkvVideoOptions opts;
    opts.frameRate = fps;
    opts.codecId   = AV_CODEC_ID_H264;

    // control
    {
        const QString out = work.filePath("control.mkv");
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        if (!p.mux(out, video, {audio})) return fail("control mux failed");
        if (audioTrackCount(out) != 1) return fail("control: not exactly one audio track");
        printf("control: ok, 1 audio track\n");
    }

    // strict (default)
    {
        const QString out = work.filePath("strict.mkv");
        QFile::remove(out);
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        if (p.mux(out, video, {audio, garbage, missing}))
            return fail("strict: mux returned true with two unusable audio inputs");
        const QString err = p.lastError();
        printf("strict: false, lastError: %s\n", qPrintable(err));
        if (!err.contains("garbage.ac3") || !err.contains("missing.ac3"))
            return fail("strict: lastError does not name both dropped files");
        if (p.droppedInputs().size() != 2) return fail("strict: droppedInputs() != 2");
        if (QFile::exists(out)) return fail("strict: an output file was left behind");
    }

    // lenient
    {
        const QString out = work.filePath("lenient.mkv");
        TTMkvMergeProvider p;
        p.setVideoOptions(opts);
        p.setRequireAllInputs(false);
        if (!p.mux(out, video, {audio, garbage, missing}))
            return fail("lenient: mux failed");
        if (p.droppedInputs().size() != 2) return fail("lenient: droppedInputs() != 2");
        if (audioTrackCount(out) != 1) return fail("lenient: not exactly one audio track");
        printf("lenient: true, 2 inputs reported dropped, 1 audio track\n");
    }

    // muxAudioOnly, strict
    {
        const QString out = work.filePath("strict.mka");
        QFile::remove(out);
        TTMkvMergeProvider p;
        if (p.muxAudioOnly(out, {audio, garbage}))
            return fail("muxAudioOnly: true with an unusable input");
        if (!p.lastError().contains("garbage.ac3")) return fail("muxAudioOnly: lastError does not name the file");
        if (QFile::exists(out)) return fail("muxAudioOnly: an output file was left behind");
        printf("muxAudioOnly strict: false, lastError: %s\n", qPrintable(p.lastError()));
    }

    printf("PASS: unusable mux inputs are reported (strict fails, lenient warns)\n");
    return 0;
}
