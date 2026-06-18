// Diagnostic + acceptance harness: drive TTESSmartCut::smartCutFrames on a
// single short keep-range and assert the selected start AU (Direction A).
// Build via `make test_stillframe` in tools/diag (run a root `make` first so
// the object files exist). Link line (kept in sync in the Makefile):
//   obj/ttessmartcut.o obj/moc_ttessmartcut.o obj/ttsettings.o
//   obj/moc_ttsettings.o obj/ttesinfo.o obj/ttnaluparser.o
//   obj/ttdisplayordermap.o obj/ttmessagelogger.o  + Qt5 + libav
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"
#include "../../avstream/ttnaluparser.h"

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

    // The selected start AU is the first element of the first actual output
    // range. Pre-fix the mixed-index walk reported 36388; the fix must report
    // the AU that DISPLAYS at the cut-in display position.
    int firstSelectedAU = sc.actualOutputFrameRanges().isEmpty()
        ? -1 : sc.actualOutputFrameRanges().first().first;

    // Acceptance (Direction A): cut-in display 36384 must select AU 36385
    // (the frame that DISPLAYS at 36384), NOT 36388 (old mixed-index walk).
    if (firstSelectedAU != 36385) {
        printf("FAIL: first selected AU = %d, expected 36385\n", firstSelectedAU);
        return 1;
    }
    printf("PASS: first selected AU = 36385\n");

    // Acceptance (cut-out frame-accuracy): the output must contain exactly
    // cutOut - cutIn + 1 displayed frames. Open the output file and verify
    // the frame count via TTNaluParser.
    TTNaluParser parser;
    if (!parser.openFile(out)) {
        printf("FAIL: could not open output file %s\n", qPrintable(out));
        return 1;
    }
    if (!parser.parseFile()) {
        printf("FAIL: could not parse output file %s\n", qPrintable(out));
        return 1;
    }

    int expected = cutOut - cutIn + 1;
    int actualCount = parser.accessUnitCount();
    if (actualCount != expected) {
        printf("FAIL: output has %d frames, expected %d (cutOut-cutIn+1)\n",
               actualCount, expected);
        return 1;
    }
    printf("PASS: output frame count = %d (== cutOut-cutIn+1)\n", actualCount);

    return ok ? 0 : 1;
}
