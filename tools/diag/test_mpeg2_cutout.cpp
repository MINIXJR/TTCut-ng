/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Diagnostic harness: drive TTMpeg2VideoStream::cut() directly to measure how
// many frames actually land in the output for a given (cutIn, cutOut) display
// range.
//
// Motivation (2026-07-10): getCutEndObject() walks BACKWARDS over display
// positions to find the last I/P frame (ipFramePos), then counts the B-frames
// that follow that I/P in the HEADER LIST (= bitstream / decode order) into
// bFrameCount. It finally does
//
//     if (bFrameCount > 0 && cutOutPos <= ipFramePos + bFrameCount)
//         cutParams->setCutOutIndex(cutOutPos);
//
// which adds a DISPLAY index to a count of bitstream-adjacent B-frames. In a
// classic IBBP GOP (display I0 B1 B2 P3 B4 B5 P6, bitstream I0 P3 B1 B2 P6 …)
// the B-frames following P3 in the bitstream are B1/B2 — i.e. frames that
// display BEFORE P3, not after it. Suppressing the cut-out tail re-encode on
// that basis would silently drop the frames between the last copied B-frame
// and the requested cut-out.
//
// This tool answers the question empirically: cut [cutIn..cutOut] and report
// how many frames the output really contains vs. how many were requested.
//
// Build: `make test_mpeg2_cutout` in tools/diag (run a root `make` first).
// Usage: test_mpeg2_cutout <file.m2v> <cutIn> <cutOut> <out.m2v>
//
// A correct engine writes (cutOut - cutIn + 1) frames.

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <cstdio>
#include <cstdlib>

#include "common/ttsettings.h"
#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttfilebuffer.h"
#include "data/ttcutparameter.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 5) {
        fprintf(stderr, "usage: %s <file.m2v> <cutIn> <cutOut> <out.m2v>\n", argv[0]);
        return 2;
    }

    const QString inFile  = QString::fromLocal8Bit(argv[1]);
    const int     cutIn   = atoi(argv[2]);
    const int     cutOut  = atoi(argv[3]);
    const QString outFile = QString::fromLocal8Bit(argv[4]);

    // encoderMode ON: allows arbitrary (B-frame) cut-out positions, which is
    // what isCutOutPoint() gates in the GUI. Default in TTSettings is true.
    TTSettings::instance()->setEncoderMode(true);

    QFileInfo fi(inFile);
    TTMpeg2VideoStream vs(fi);

    printf("== input: %s\n", qPrintable(inFile));
    vs.createHeaderList();
    vs.createIndexList();
    vs.indexList()->sortDisplayOrder();          // MPEG-2: list becomes display-ordered

    const int frames = vs.indexList()->count();
    printf("== index list: %d frames (display order)\n", frames);

    // Dump the frame types around the cut-out so the GOP shape is on record.
    printf("== display pos : type (1=I 2=P 3=B) : headerListIndex\n");
    const int lo = (cutIn > 2) ? cutIn - 2 : 0;
    const int hi = (cutOut + 4 < frames) ? cutOut + 4 : frames - 1;
    for (int i = lo; i <= hi; ++i) {
        printf("   %5d : %d%s : %d\n", i,
               vs.indexList()->pictureCodingType(i),
               (i == cutIn) ? "  <- cutIn" : ((i == cutOut) ? "  <- cutOut" : ""),
               vs.indexList()->headerListIndex(i));
    }

    TTFileBuffer tgt(outFile, QIODevice::WriteOnly);
    tgt.open();

    TTCutParameter cp(&tgt);
    cp.firstCall();
    cp.setCutInIndex(cutIn);
    cp.setCutOutIndex(cutOut);

    printf("== cut(%d, %d)\n", cutIn, cutOut);
    vs.cut(cutIn, cutOut, &cp);

    cp.lastCall();
    tgt.close();

    const int requested = cutOut - cutIn + 1;
    printf("== requested frames : %d\n", requested);
    printf("== numPicturesWritten (engine counter) : %d\n", cp.getNumPicturesWritten());
    printf("== cutOutIndex after cut()             : %d\n", cp.getCutOutIndex());
    printf("== output: %s  (count frames with ffprobe)\n", qPrintable(outFile));

    return 0;
}
