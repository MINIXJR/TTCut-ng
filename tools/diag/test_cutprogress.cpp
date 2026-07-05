// test_cutprogress — count TTESSmartCut::progressChanged emissions during a
// whole-video stream-copy cut, to locate why the open-GOP cut shows no progress.
// Prints emission count + first/last percent. Ground truth, not reasoning.
//
// Build (tools/diag/Makefile): make test_cutprogress
// Usage: test_cutprogress <es> <frameRate>
#include <QCoreApplication>
#include <QObject>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: %s <es> <frameRate>\n", argv[0]); return 2; }
    QString es = argv[1];
    double  fr = atof(argv[2]);

    QElapsedTimer t; t.start();
    TTESSmartCut sc;
    if (!sc.initialize(es, fr)) {
        fprintf(stderr, "initialize failed: %s\n", qPrintable(sc.lastError()));
        return 1;
    }
    const int frames = sc.frameCount();
    fprintf(stderr, "initialized (parse): %lld ms  frames=%d gops=%d\n",
            (long long)t.elapsed(), frames, sc.gopCount());

    int    count = 0;
    int    firstPct = -1, lastPct = -1;
    QObject::connect(&sc, &TTESSmartCut::progressChanged,
                     [&](int percent, const QString&) {
        if (firstPct < 0) firstPct = percent;
        lastPct = percent;
        ++count;
    });

    // Whole-video cut in DISPLAY positions [0, frames-1].
    QList<QPair<int,int>> keep;
    keep.append(qMakePair(0, frames - 1));

    QString out = "/usr/local/src/CLAUDE_TMP/TTCut-ng/cutprogress_out.h264";
    qint64 tCut = t.elapsed();
    bool ok = sc.smartCutFrames(out, keep);
    qint64 cutMs = t.elapsed() - tCut;

    fprintf(stderr, "smartCutFrames %s: %lld ms  err=%s\n",
            ok ? "OK" : "FAIL", (long long)cutMs, qPrintable(sc.lastError()));
    fprintf(stderr, "progressChanged emissions: %d  (first=%d%% last=%d%%)\n",
            count, firstPct, lastPct);
    return ok ? 0 : 1;
}
