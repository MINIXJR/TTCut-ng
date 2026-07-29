/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Acceptance harness for TTAspectScanTask on MPEG-2 elementary streams (.m2v).
//
//   usage: test_aspectscan_mpeg2 <es-file.m2v> <sampleSeconds> [expectedCount]
//
// tools/diag/test_aspectscan.cpp builds its TTVideoIndexList from a
// TTFFmpegWrapper and always passes a null TTVideoHeaderList*. That is
// harmless for H.264/H.265 (TTSearchTask::setupWorkers only touches
// TTFFmpegWrapper for those stream types), but MPEG-2 goes through
// TTMpeg2Decoder, whose moveToFrameIndex() dereferences the header list
// unconditionally (data/ttsearchtask.cpp setupWorkers()/decodeFrameAt()) -
// a null header list segfaults. This harness instead builds header and
// index lists the same way the real application does: via
// TTMpeg2VideoStream::createHeaderList()/createIndexList() (exactly what
// TTOpenVideoTask runs for every opened MPEG-2 file), then passes the real
// TTVideoHeaderList* to TTAspectScanTask - the same construction as the
// production call site (gui/ttcutmainwindow.cpp, around the aspect-scan
// action).
//
// With expectedCount given, the run is a gate: exactly that many transitions
// must be reported (order-independent count, since the alternating direction
// pattern is checked visually/manually against source material once and
// then only the transition count is re-verified on every run).
//
// Build via `make test_aspectscan_mpeg2` in tools/diag (needs a root `make`
// first, so ../../obj/*.o exist).
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoheaderlist.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttavtypes.h"
#include "data/ttsearchtask_aspectscan.h"
#include "data/ttstreampoint.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <es-file.m2v> <sampleSeconds> [expectedCount]\n",
                argv[0]);
        return 2;
    }
    const QString file      = QString::fromLocal8Bit(argv[1]);
    const double  sampleS   = atof(argv[2]);
    const int     expectCnt = (argc > 3) ? atoi(argv[3]) : -1;

    if (QFileInfo(file).suffix().toLower() != "m2v") {
        fprintf(stderr, "warning: %s does not have a .m2v extension - "
                        "continuing anyway, this harness always uses the "
                        "MPEG-2 (TTMpeg2VideoStream) path\n", qPrintable(file));
    }

    QFileInfo fi(file);
    TTMpeg2VideoStream vs(fi);
    int headerCount = vs.createHeaderList();
    int indexCount  = vs.createIndexList();
    printf("headerCount=%d indexCount=%d\n", headerCount, indexCount);

    TTVideoHeaderList* headerList = vs.headerList();
    TTVideoIndexList*  indexList  = vs.indexList();
    if (!headerList || !indexList) {
        fprintf(stderr, "createHeaderList/createIndexList failed to populate lists\n");
        return 1;
    }
    indexList->sortDisplayOrder();
    printf("index entries (display order)=%d\n", indexList->count());
    printf("frameCount=%d frameRate=%f\n", vs.frameCount(), vs.frameRate());

    QList<TTStreamPoint> points;
    TTAspectScanTask task(vs.filePath(), TTAVTypes::mpeg2_demuxed_video,
                          indexList, headerList,
                          indexList->count(), vs.frameRate(), 20, sampleS,
                          QList<TTFrameInfo>());
    QObject::connect(&task, &TTAspectScanTask::pointsDetected,
                     [&points](const QList<TTStreamPoint>& p) { points = p; });
    task.runSynchron();

    for (const TTStreamPoint& p : points)
        printf("point: frame=%d desc=%s\n", p.frameIndex(), qPrintable(p.description()));
    printf("total points=%d\n", points.size());

    if (expectCnt < 0) return 0;

    if (points.size() != expectCnt) {
        printf("FAIL: expected %d transitions, got %d\n", expectCnt, points.size());
        return 1;
    }
    printf("PASS: %d transition(s) as expected\n", expectCnt);
    return 0;
}
