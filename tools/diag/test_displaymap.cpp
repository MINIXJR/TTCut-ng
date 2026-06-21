// Temporary diagnostic: authoritative global display-index -> AU map via the
// real display path (TTFFmpegWrapper::decodeFrame fills deliveredDecodeIndex).
// Remove after diagnosis.
#include <QCoreApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include "extern/ttffmpegwrapper.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) { fprintf(stderr, "usage: %s <es> <lo> <hi>\n", argv[0]); return 2; }
    TTFFmpegWrapper::initializeFFmpeg();
    TTFFmpegWrapper w;
    if (!w.openFile(argv[1])) { fprintf(stderr, "openFile failed\n"); return 1; }
    int vs = w.findBestVideoStream();
    if (!w.buildFrameIndex(vs)) { fprintf(stderr, "buildFrameIndex failed\n"); return 1; }
    fprintf(stderr, "frames=%d\n", w.frameCount());
    int lo = atoi(argv[2]), hi = atoi(argv[3]);
    bool save = (argc > 4 && QString(argv[4]) == "save");
    QString outDir = (argc > 5) ? argv[5] : ".";
    for (int n = lo; n <= hi; ++n) {
        QImage img = w.decodeFrame(n);        // lazily fills deliveredDecodeIndex
        TTFrameInfo fi = w.frameAt(n);
        fprintf(stderr, "display %6d -> AU(delivered)=%6d  isKeyframe=%d frameType=%d null=%d\n",
                n, fi.deliveredDecodeIndex, fi.isKeyframe ? 1 : 0, fi.frameType, img.isNull() ? 1 : 0);
        if (save && !img.isNull())
            img.save(QString("%1/dm_%2.png").arg(outDir).arg(n));
    }
    return 0;
}
