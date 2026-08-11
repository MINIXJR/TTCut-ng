// Audio abort harness. Usage: test_audiocut_abort <in.ac3>
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
#include "extern/ttffmpegwrapper.h"

static int fail(const char* what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <in.ac3>\n", argv[0]); return 2; }
    const QString out = "/usr/local/src/CLAUDE_TMP/TTCut-ng/cut-abort/abort_audio_out.ac3";
    QList<QPair<double,double>> keep{{0.0, 30.0}};

    // (a) abort immediately: predicate true from the start
    {
        TTFFmpegWrapper ff;
        if (ff.cutAudioStream(argv[1], out, keep, false, {}, {}, []{ return true; }))
            return fail("(a) succeeded despite abort predicate");
        QFile::remove(out);
    }
    // (b) no predicate: normal run still works
    {
        TTFFmpegWrapper ff;
        if (!ff.cutAudioStream(argv[1], out, keep, false, {}, {}))
            return fail("(b) clean run failed");
        if (QFile(out).size() == 0) return fail("(b) empty output");
        QFile::remove(out);
    }
    printf("PASS\n");
    return 0;
}
