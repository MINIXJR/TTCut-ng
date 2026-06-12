// Temporary diagnostic: drive TTESSmartCut::smartCutFrames on a single short
// keep-range to trigger selectFramesNonPAFF and its STILLFRAME-DEBUG dump.
// Build via the one-off g++ command in the session notes; remove after diagnosis.
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        fprintf(stderr, "usage: %s <es> <out> <cutIn> <cutOut> [frameRate]\n", argv[0]);
        return 2;
    }
    QString es = argv[1];
    QString out = argv[2];
    int cutIn  = atoi(argv[3]);
    int cutOut = atoi(argv[4]);
    double fr  = (argc > 5) ? atof(argv[5]) : 25.0;

    TTESSmartCut sc;
    if (!sc.initialize(es, fr)) {
        fprintf(stderr, "initialize failed: %s\n", qPrintable(sc.lastError()));
        return 1;
    }
    fprintf(stderr, "initialized: frames=%d gops=%d fr=%.3f\n",
            sc.frameCount(), sc.gopCount(), sc.frameRate());

    QList<QPair<int,int>> keep;
    keep.append(qMakePair(cutIn, cutOut));
    bool ok = sc.smartCutFrames(out, keep);
    fprintf(stderr, "smartCutFrames %s err=%s\n",
            ok ? "OK" : "FAIL", qPrintable(sc.lastError()));
    for (const auto& r : sc.actualOutputFrameRanges())
        fprintf(stderr, "actualOutputRange: %d..%d\n", r.first, r.second);
    return ok ? 0 : 1;
}
