// Diagnostic harness (defect A measurement, 2026-07-20): smart-cut one
// keep-range, then mux the cut ES into MKV exactly like the app pipeline
// (TTAVData::onCutFinished): setDefaultDuration + setVideoCodecId +
// setVideoDisplayOrder(smartCut.outputDisplayOrder()). Video-only mux —
// enough to measure whether container PTS stay content-true at the seam.
//
// Build: `make test_mkvmux` in tools/diag (run a root `make` first).
// Usage: test_mkvmux <es> <out.mkv> <cutIn> <cutOut> [frameRate]
#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"
#include "extern/ttmkvmergeprovider.h"
#include "common/ttsettings.h"

extern "C" {
#include <libavcodec/codec_id.h>
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        fprintf(stderr, "usage: %s <es> <out.mkv> <cutIn> <cutOut> [frameRate]\n", argv[0]);
        return 2;
    }
    QString es = argv[1];
    QString outMkv = argv[2];
    int cutIn  = atoi(argv[3]);
    int cutOut = atoi(argv[4]);
    double fr  = (argc > 5) ? atof(argv[5]) : 25.0;

    TTSettings::instance()->setLogSmartCut(true);
    TTSettings::instance()->setLogMkvMux(true);

    TTESSmartCut sc;
    if (!sc.initialize(es, fr)) {
        fprintf(stderr, "initialize failed: %s\n", qPrintable(sc.lastError()));
        return 1;
    }

    const bool isHevc = es.endsWith(".265", Qt::CaseInsensitive)
                     || es.endsWith(".h265", Qt::CaseInsensitive)
                     || es.endsWith(".hevc", Qt::CaseInsensitive);
    QString cutEs = outMkv + (isHevc ? ".cut.265" : ".cut.264");
    QList<QPair<int,int>> keep;
    keep.append(qMakePair(cutIn, cutOut));
    if (!sc.smartCutFrames(cutEs, keep)) {
        fprintf(stderr, "smartCutFrames FAIL: %s\n", qPrintable(sc.lastError()));
        return 1;
    }

    const QVector<int> order = sc.outputDisplayOrder();
    fprintf(stderr, "outputDisplayOrder: %d entries%s\n", order.size(),
            order.isEmpty() ? " (EMPTY -> legacy linear PTS)" : "");
    // Dump the order entries around the copy seam for offline analysis.
    for (int i = 0; i < order.size(); ++i)
        fprintf(stdout, "order[%d]=%d\n", i, order[i]);

    TTMkvMergeProvider mkv;
    int frameDurationNs = (int)(1000000000.0 / fr);
    mkv.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
    mkv.setIsPAFF(false);
    mkv.setVideoCodecId(isHevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    mkv.setVideoDisplayOrder(order);
    bool ok = mkv.mux(outMkv, cutEs, QStringList());
    fprintf(stderr, "mux %s err=%s\n", ok ? "OK" : "FAIL", qPrintable(mkv.lastError()));
    return ok ? 0 : 1;
}
