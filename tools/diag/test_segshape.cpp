// Diagnostic harness: drive TTESSmartCut::smartCutFrames with a MULTI-segment
// cut list to study whether the segment context (position/cut-out of a segment)
// changes the leading-frame (open-GOP B-frame) drop decision at a cut-in.
//
// Motivation (2026-06-23): the cut preview appeared to drop the leading
// reorder frames at a B-frame cut-in while the final cut kept them (station
// bumper leaked in). RESOLVED (2026-07-02) with this harness: the engine
// selects identically for both shapes — the output starts exactly at the
// requested display cut-in. The observed difference came from two .ttcut
// files: the preview ran on freshly set cut-ins (36386 = content start),
// the final cut on a pre-display-order-map-fix project whose stored cut-ins
// (36384) were bug-compensated 2-3 frames early. Kept as a regression tool:
// for any cut-in, actualOutputRange[i].first must equal the requested cut-in
// (display position) after Direction A.
//
// Build: `make test_segshape` in tools/diag (run a root `make` first).
// Usage:  test_segshape <es> <frameRate> <cutIn1> <cutOut1> [cutIn2 cutOut2 ...]
//   The segment of interest is identified by its cut-in; compare the printed
//   actualOutputRange.first for that cut-in across two invocations.
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include "common/ttsettings.h"
#include "extern/ttessmartcut.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5 || (argc % 2) != 1) {
        fprintf(stderr,
            "usage: %s <es> <frameRate> <cutIn1> <cutOut1> [cutIn2 cutOut2 ...]\n",
            argv[0]);
        return 2;
    }
    QString es = argv[1];
    double  fr = atof(argv[2]);

    // Force smart-cut logging so the "display X -> AU Y, streamCopyLimit,
    // Selected N frames" decision lines are emitted.
    TTSettings::instance()->setLogSmartCut(true);

    QList<QPair<int,int>> keep;
    for (int i = 3; i + 1 < argc; i += 2)
        keep.append(qMakePair(atoi(argv[i]), atoi(argv[i+1])));

    fprintf(stderr, "=== segments (%d) ===\n", keep.size());
    for (const auto& s : keep)
        fprintf(stderr, "  %d .. %d\n", s.first, s.second);

    TTESSmartCut sc;
    if (!sc.initialize(es, fr)) {
        fprintf(stderr, "initialize failed: %s\n", qPrintable(sc.lastError()));
        return 1;
    }
    fprintf(stderr, "initialized: frames=%d gops=%d fr=%.3f\n",
            sc.frameCount(), sc.gopCount(), sc.frameRate());

    // Write to a throwaway output (we only care about the decisions/ranges).
    QString out = "/usr/local/src/CLAUDE_TMP/TTCut-ng/segshape_out.h264";
    bool ok = sc.smartCutFrames(out, keep);
    fprintf(stderr, "smartCutFrames %s err=%s\n",
            ok ? "OK" : "FAIL", qPrintable(sc.lastError()));

    const auto ranges = sc.actualOutputFrameRanges();
    for (int i = 0; i < ranges.size(); ++i)
        fprintf(stderr, "actualOutputRange[%d] (cutIn %d): %d .. %d\n",
                i, (i < keep.size() ? keep[i].first : -1),
                ranges[i].first, ranges[i].second);

    return ok ? 0 : 1;
}
