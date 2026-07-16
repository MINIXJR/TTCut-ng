// Diagnostic harness (EOS + Non-IDR seam investigation, 2026-07-16): drive
// TTESSmartCut::smartCutFrames on one keep-range with smart-cut logging
// enabled, so the branch pick (standard vs SPS unification), the boundary-
// crossing extension, and the frameNumDelta bridge are visible on stderr.
// Unlike test_stillframe this has no material-specific acceptance check.
//
// Build: `make test_smartcut_seam` in tools/diag (run a root `make` first).
// Usage: test_smartcut_seam <es> <out> <cutIn> <cutOut> [frameRate]
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"
#include "common/ttsettings.h"

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

    TTSettings::instance()->setLogSmartCut(true);

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
