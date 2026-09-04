// Acceptance (Befund D, still-frame aspect): TTFFmpegWrapper must report the
// stream's sample aspect ratio. Decodes frame 0 first (the still-frame path
// always decodes before showing), then prints sampleAspectRatio().
// Usage: test_sar <es> [expected_sar]
//   With expected_sar: PASS/FAIL check (tolerance 0.001), exit code 0/1.
#include <QCoreApplication>
#include <QImage>
#include <cstdio>
#include <cmath>
#include "extern/ttffmpegwrapper.h"
#include "avstream/ttframeindexer.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <es> [expected_sar]\n", argv[0]); return 2; }

    TTFFmpegWrapper::initializeFFmpeg();
    TTFFmpegWrapper w;
    if (!w.openFile(argv[1])) { fprintf(stderr, "openFile failed\n"); return 1; }
    TTFrameIndexer ix;
    if (!ix.build(argv[1], w.findBestVideoStream(), nullptr)) {
        fprintf(stderr, "frame index build failed: %s\n", qPrintable(ix.lastError()));
        return 1;
    }
    w.setFrameIndex(ix.bundle());

    QImage img = w.decodeFrame(0);
    double sar = w.sampleAspectRatio();
    printf("frame0: %dx%d sar=%.6f\n", img.width(), img.height(), sar);

    if (argc > 2) {
        // QByteArray::toDouble parses C-locale, immune to LC_NUMERIC comma
        double expected = QByteArray(argv[2]).toDouble();
        bool ok = std::fabs(sar - expected) <= 0.001;
        printf("%s: sar=%.6f expected=%.6f\n", ok ? "PASS" : "FAIL", sar, expected);
        return ok ? 0 : 1;
    }
    return 0;
}
