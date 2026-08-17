/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Benchmark for the playback temp-MKV mux (2026-08-17): reproduces the       */
/* TTMkvMergeProvider::mux call from TTCurrentFrame::createTempMkvForPlayback */
/* to measure where the wall-clock goes. libav alone reads+parses the same   */
/* file at >1.5 GB/s; the in-app mux ran at ~22 MB/s.                        */
/*                                                                            */
/*   usage: bench_playback_mux <video.es> <out.mkv> [audio.es] [fps]          */
/*----------------------------------------------------------------------------*/

#include "../../extern/ttmkvmergeprovider.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>
#include <cstdio>

extern "C" {
#include <libavcodec/codec_id.h>
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        fprintf(stderr, "usage: %s <video.es> <out.mkv> [audio.es] [fps]\n", argv[0]);
        return 2;
    }
    const QString video = argv[1];
    const QString out   = argv[2];
    QStringList audio;
    if (argc > 3 && argv[3][0] != '\0') audio << QString(argv[3]);
    const double fps = (argc > 4) ? atof(argv[4]) : 50.0;

    TTMkvMergeProvider p;
    p.setDefaultDuration("0", QString("%1ns").arg((int)(1000000000.0 / fps)));
    p.setVideoCodecId(AV_CODEC_ID_H264);

    QElapsedTimer t;
    t.start();
    const bool ok = p.mux(out, video, audio);
    const double secs = t.elapsed() / 1000.0;
    const double mb = QFileInfo(video).size() / 1048576.0;
    printf("mux %s: %.1f s  (%.0f MB/s)\n", ok ? "OK" : "FAIL", secs, mb / secs);
    if (!ok) fprintf(stderr, "error: %s\n", qPrintable(p.lastError()));
    return ok ? 0 : 1;
}
