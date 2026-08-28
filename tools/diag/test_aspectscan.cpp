// Acceptance harness for TTAspectScanTask on real elementary streams.
//
//   usage: test_aspectscan <es-file> <frameRate> <sampleSeconds> [expectedFrame]
//
// Builds a TTVideoIndexList the same way TTH26xVideoStream::createIndexList
// does (display rank from the wrapper's display-order map, dropped leading
// pictures skipped), runs the task synchronously and prints every stream point.
// With expectedFrame given, the run is a gate: exactly one transition at that
// display position.
//
// Stream type is derived from the file extension (.264/.h264 -> h264_video,
// .265/.h265 -> h265_video). This harness always passes a null
// TTVideoHeaderList* to TTAspectScanTask, which is harmless for H.264/H.265
// (that path only ever touches TTFFmpegWrapper), but MPEG-2 goes through
// TTMpeg2Decoder, which dereferences the header list unconditionally and
// would segfault - so .m2v input is refused here rather than crashing.
// Use tools/diag/test_aspectscan_mpeg2 for MPEG-2 material; it builds a real
// header list via TTMpeg2VideoStream, the same way the GUI does.
//
// Build via `make test_aspectscan` in tools/diag.
#include <QCoreApplication>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <cstdio>
#include <cstdlib>

#include "data/ttsearchtask_aspectscan.h"
#include "data/ttstreampoint.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttavheader.h"
#include "avstream/ttavtypes.h"
#include "extern/ttffmpegwrapper.h"
#include "common/ttthreadtask.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}

static int codingTypeOf(int avPictureType)
{
    if (avPictureType == AV_PICTURE_TYPE_I) return 1;
    if (avPictureType == AV_PICTURE_TYPE_P) return 2;
    return 3;
}

static bool streamTypeFromExtension(const QString& file, TTAVTypes::AVStreamType& outType)
{
    const QString ext = QFileInfo(file).suffix().toLower();
    if (ext == "264" || ext == "h264") { outType = TTAVTypes::h264_video; return true; }
    if (ext == "265" || ext == "h265") { outType = TTAVTypes::h265_video; return true; }
    return false;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        fprintf(stderr, "usage: %s <es-file> <frameRate> <sampleSeconds> [expectedFrame]\n",
                argv[0]);
        return 2;
    }
    const QString file    = argv[1];
    const float   fps     = atof(argv[2]);
    const double  sampleS = atof(argv[3]);
    const int     expect  = (argc > 4) ? atoi(argv[4]) : -1;

    if (QFileInfo(file).suffix().toLower() == "m2v") {
        fprintf(stderr,
                "test_aspectscan does not support MPEG-2 (.m2v) input: it always "
                "passes a null TTVideoHeaderList*, and TTMpeg2Decoder dereferences "
                "that unconditionally (segfault). Use "
                "tools/diag/test_aspectscan_mpeg2 instead - it builds a real "
                "header list via TTMpeg2VideoStream.\n");
        return 2;
    }

    TTAVTypes::AVStreamType streamType;
    if (!streamTypeFromExtension(file, streamType)) {
        fprintf(stderr, "unrecognized elementary stream extension for %s\n", qPrintable(file));
        return 2;
    }

    TTFFmpegWrapper w;
    if (!w.openFile(file) || !w.buildFrameIndex()) {
        fprintf(stderr, "open/buildFrameIndex failed\n");
        return 1;
    }
    w.buildGOPIndex();

    // Same construction as TTH26xVideoStream::createIndexList.
    const QList<TTFrameInfo>& frames = w.frameIndex();
    const TTDisplayOrderMap&  map    = w.displayOrderMap();
    TTVideoIndexList indexList;
    for (int i = 0; i < frames.size(); ++i) {
        const int disp = map.isValid() ? map.decodeToDisplay(i) : i;
        if (disp < 0) continue;
        TTVideoIndex* vi = new TTVideoIndex();
        vi->setDisplayOrder(disp);
        vi->setHeaderListIndex(i);
        vi->setPictureCodingType(codingTypeOf(frames[i].frameType));
        indexList.add(vi);
    }
    indexList.sortDisplayOrder();
    printf("index entries=%d\n", indexList.count());

    QList<TTStreamPoint> points;
    TTAspectScanTask task(file, streamType, &indexList, nullptr,
                          indexList.count(), fps, 20, sampleS, w.frameIndexBundle());
    QObject::connect(&task, &TTAspectScanTask::pointsDetected,
                     [&points](const QList<TTStreamPoint>& p) { points = p; });
    // Record all status messages: the step sequence is the invariant
    // against which detail output is tested (only AddProcessLine may be added).
    // Format: <state>|<value>|<msg>
    QObject::connect(&task, &TTThreadTask::statusReport,
                     [](TTThreadTask*, int state, const QString& msg, quint64 value) {
                         printf("STATUS|%d|%llu|%s\n", state,
                                (unsigned long long)value, qPrintable(msg));
                     });
    task.runSynchron();

    for (const TTStreamPoint& p : points)
        printf("point: frame=%d desc=%s\n", p.frameIndex(), qPrintable(p.description()));

    if (expect < 0) return 0;

    if (points.size() != 1) {
        printf("FAIL: expected exactly 1 transition, got %d\n", points.size());
        return 1;
    }
    if (points.first().frameIndex() != expect) {
        printf("FAIL: transition at %d, expected %d\n", points.first().frameIndex(), expect);
        return 1;
    }
    printf("PASS: single transition at frame %d\n", expect);
    return 0;
}
