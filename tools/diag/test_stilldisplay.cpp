// Acceptance: decodeFrame(N) must deliver the output frame whose decode-order
// AU == displayOrderMap().displayToDecode(N) (the true display position N).
// Pre-fix this FAILS at B-frame positions (decodeFrame delivers the display-rank
// frame N-seekKeyframe, off by the local B-frame reorder amount).
// Usage: test_stilldisplay <es> <N> [N2 ...]
#include <QCoreApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include "extern/ttffmpegwrapper.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: %s <es> <N> [N2 ...]\n", argv[0]); return 2; }
    TTFFmpegWrapper::initializeFFmpeg();
    TTFFmpegWrapper w;
    if (!w.openFile(argv[1])) { fprintf(stderr, "openFile failed\n"); return 1; }
    int vs = w.findBestVideoStream();
    if (!w.buildFrameIndex(vs)) { fprintf(stderr, "buildFrameIndex failed\n"); return 1; }

    int rc = 0;
    for (int a = 2; a < argc; ++a) {
        int N = atoi(argv[a]);
        QImage img = w.decodeFrame(N);
        int delivered = w.frameAt(N).deliveredDecodeIndex;
        int expected  = w.displayOrderMap().isValid()
                      ? w.displayOrderMap().displayToDecode(N) : N;
        bool ok = (!img.isNull() && delivered == expected);
        printf("%s: N=%d delivered=%d expected(displayToDecode)=%d\n",
               ok ? "PASS" : "FAIL", N, delivered, expected);
        if (!ok) rc = 1;
    }
    return rc;
}
