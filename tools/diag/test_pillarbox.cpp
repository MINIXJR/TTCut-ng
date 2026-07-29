// Diagnostic harness for the stream-point pillarbox ("4:3 in 16:9") detection.
// Replays the exact pixel test from TTStreamPointVideoWorker::isPillarboxFrame
// on frames decoded through TTFFmpegWrapper, so a failing detection can be
// pinned to either the decode path or the worker plumbing around it.
//
// usage: test_pillarbox <es-file> [firstFrame] [lastFrame] [step] [threshold]
//
// firstFrame/lastFrame/the printed frame number are all DISPLAY positions —
// the same space decodeFrame() expects. The indexed path below walks display
// order and uses the wrapper's display-order map (TTDisplayOrderMap) to find
// each display position's decode-order frame type, so the number that is
// printed is always the exact number that was fed to decodeFrame(). Earlier
// this loop iterated w.frameIndex() (decode order) and passed that loop
// index straight to decodeFrame() (which takes a display position) — the
// printed number and the classified image came from different index spaces,
// which produced a wrong reference frame in this project's aspect-scan work.
//
// Build via `make test_pillarbox` in tools/diag (needs a root `make` first).
#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <cstdio>
#include <cstdlib>

#include "extern/ttffmpegwrapper.h"

extern "C" {
#include <libavutil/avutil.h>
}

// --- verbatim copy of the worker's pixel test -------------------------------
static bool isColumnBlack(const uint8_t* yPlane, int yStride, int col,
                          int y0, int y1, int threshold)
{
    int total = 0;
    int black = 0;
    for (int row = y0; row < y1; row += 2) {
        total++;
        if (yPlane[row * yStride + col] < threshold)
            black++;
    }
    if (total == 0) return false;
    return (float)black / total >= 0.90f;
}

static bool isPillarboxFrame(const uint8_t* yPlane, int yStride, int width,
                             int height, int threshold, float& barWidthPercent,
                             int& leftOut, int& rightOut)
{
    barWidthPercent = 0.0f;
    leftOut = rightOut = 0;
    if (!yPlane || width < 20 || height < 20) return false;

    int y0 = (int)(height * 0.30f);
    int y1 = (int)(height * 0.70f);
    int minBarWidth = width / 10;

    int leftBar = 0;
    for (int col = 0; col < width / 2; ++col) {
        if (isColumnBlack(yPlane, yStride, col, y0, y1, threshold)) leftBar++;
        else break;
    }
    int rightBar = 0;
    for (int col = width - 1; col >= width / 2; --col) {
        if (isColumnBlack(yPlane, yStride, col, y0, y1, threshold)) rightBar++;
        else break;
    }
    leftOut = leftBar;
    rightOut = rightBar;

    if (leftBar >= minBarWidth && rightBar >= minBarWidth) {
        barWidthPercent = (float)(leftBar + rightBar) / width * 100.0f;
        return true;
    }
    return false;
}
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <es-file> [firstFrame] [lastFrame] [step] [threshold]\n",
                argv[0]);
        return 2;
    }
    const QString file = argv[1];
    const int first = (argc > 2) ? atoi(argv[2]) : 0;
    int       last  = (argc > 3) ? atoi(argv[3]) : -1;
    const int step  = (argc > 4) ? atoi(argv[4]) : 1;
    const int thres = (argc > 5) ? atoi(argv[5]) : 20;

    // TTDIAG_NOINDEX=1 reproduces the worker's setup: openFile() only, no
    // buildFrameIndex()/buildGOPIndex(). Frame positions then have to be given
    // explicitly via firstFrame/lastFrame because the index is empty.
    const bool noIndex = qEnvironmentVariableIntValue("TTDIAG_NOINDEX") == 1;

    // TTDIAG_SEARCHMODE=1 enables the same fast path the search tasks use:
    // direct keyframe seek without DPB prefill (TTSearchTask::setupWorkers).
    const bool searchMode = qEnvironmentVariableIntValue("TTDIAG_SEARCHMODE") == 1;

    TTFFmpegWrapper w;
    if (searchMode) { w.setAnalysisMode(true); w.setSearchMode(true); }
    if (!w.openFile(file)) {
        fprintf(stderr, "openFile failed\n");
        return 1;
    }
    if (noIndex) {
        printf("NOINDEX mode: frames=%d (no buildFrameIndex)\n", w.frameCount());
        for (int i = first; i <= last; i += (step > 0 ? step : 1)) {
            QImage frame = w.decodeFrame(i);
            printf("%8d  %s\n", i,
                   frame.isNull() ? "DECODE-NULL" : "decoded");
        }
        return 0;
    }
    if (!w.buildFrameIndex()) {
        fprintf(stderr, "buildFrameIndex failed\n");
        return 1;
    }
    w.buildGOPIndex();

    const QList<TTFrameInfo>& idx = w.frameIndex();
    const TTDisplayOrderMap&  map = w.displayOrderMap();
    printf("frames=%d gops=%d\n", idx.size(), w.gopCount());

    int nI = 0;
    for (const auto& fi : idx)
        if (fi.frameType == AV_PICTURE_TYPE_I) nI++;
    printf("I-frames=%d\n", nI);

    // first/last/the loop variable are DISPLAY positions from here on (see
    // the header comment). displayCount() is the navigable (post-drop) frame
    // count; fall back to the raw decode-order size if no map was built.
    const int displayCount = map.isValid() ? map.displayCount() : idx.size();
    if (last < 0 || last >= displayCount) last = displayCount - 1;

    int seen = 0;
    for (int disp = first; disp <= last; ++disp) {
        const int decodeIdx = map.isValid() ? map.displayToDecode(disp) : disp;
        if (decodeIdx < 0 || decodeIdx >= idx.size()) continue;
        if (idx[decodeIdx].frameType != AV_PICTURE_TYPE_I) continue;
        if ((seen++ % step) != 0) continue;

        QImage frame = w.decodeFrame(disp);
        if (frame.isNull()) {
            printf("%8d I  DECODE-NULL\n", disp);
            continue;
        }
        QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
        float pct = 0.0f;
        int l = 0, r = 0;
        bool pb = isPillarboxFrame(gray.constBits(), gray.bytesPerLine(),
                                   gray.width(), gray.height(), thres, pct, l, r);
        printf("%8d I  %dx%d  left=%-4d right=%-4d min=%-4d %s\n",
               disp, gray.width(), gray.height(), l, r, gray.width() / 10,
               pb ? "PILLARBOX" : "-");
        fflush(stdout);
    }
    return 0;
}
