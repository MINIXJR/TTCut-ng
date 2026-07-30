/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Drives the MPEG-2 still-image path the way the GUI does, for a list of
// positions, and reports what comes back.
//
//   usage: test_mpeg2_seek [--second-decoder] <es-file.m2v> <pos> [pos ...]
//
// --second-decoder creates a second TTMpeg2Decoder on the same file before
// stepping through the positions, and runs a few frames through it. That is
// what a stream-point analysis does while the still-image window already holds
// one: TTSearchTask::setupWorkers builds its own decoder for MPEG-2. If the
// display decoder returns stale pictures afterwards, the two instances share
// state they must not share.
//
// Same construction as the application: TTMpeg2VideoStream builds header and
// index list, sortDisplayOrder() is applied, and TTMpeg2Decoder is created
// from both - exactly what TTMPEG2Window2::openVideoFile does. Each position
// then goes through moveToIndexPos() and moveToFrameIndex(), the two calls
// behind TTCurrentFrame::onGotoFrame.
//
// For every position it prints the index the navigation resolved to, the
// decoder's answer, and a checksum plus mean brightness of the returned
// picture. Two positions that produce the same checksum returned the same
// picture - which is what a "the image does not change" report looks like
// from the inside.
//
// Build via `make test_mpeg2_seek` in tools/diag (needs a root `make` first).

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoheaderlist.h"
#include "avstream/ttvideoindexlist.h"
#include "mpeg2decoder/ttmpeg2decoder.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int arg = 1;
    bool secondDecoder = false;
    int  fast = 0;
    while (arg < argc) {
        const QString a = QString::fromLatin1(argv[arg]);
        if (a == QLatin1String("--second-decoder")) { secondDecoder = true; ++arg; }
        // --fast mirrors TTSettings::fastSlider(): TTCutMainWindow::
        // onVideoSliderChanged then calls onGotoFrame(pos, 1), and that 1 is a
        // FRAME TYPE - moveToIndexPos looks for the next I frame from pos
        // instead of going to pos itself.
        else if (a == QLatin1String("--fast"))      { fast = 1; ++arg; }
        else break;
    }
    if (argc - arg < 2) {
        fprintf(stderr, "usage: %s [--second-decoder] <es-file.m2v> <pos> [pos ...]\n", argv[0]);
        return 2;
    }

    QFileInfo fi(QString::fromLocal8Bit(argv[arg++]));
    TTMpeg2VideoStream vs(fi);
    vs.createHeaderList();
    vs.createIndexList();

    TTVideoHeaderList* headerList = vs.headerList();
    TTVideoIndexList*  indexList  = vs.indexList();
    if (!headerList || !indexList) {
        fprintf(stderr, "header/index list empty\n");
        return 1;
    }
    indexList->sortDisplayOrder();
    printf("frames=%d\n\n", indexList->count());

    // The display window's decoder, created first - as in the application,
    // where TTMPEG2Window2::openVideoFile runs when the stream is opened.
    TTMpeg2Decoder decoder(vs.filePath(), indexList, headerList);

    if (secondDecoder) {
        printf("creating a second decoder on the same file "
               "(what the analysis does) and running frames through it...\n");
        TTMpeg2Decoder analysis(vs.filePath(), indexList, headerList);
        for (int p = 100; p <= 400; p += 100) {
            try { analysis.moveToFrameIndex(p); } catch (TTMpeg2DecoderException&) {}
        }
        printf("second decoder destroyed again\n\n");
    }

    printf("%-9s %-9s %-9s %-10s %-34s %s\n",
           "request", "nav", "decoder", "brightness", "checksum", "note");

    QByteArray previous;
    for (int a = arg; a < argc; ++a) {
        const int want = atoi(argv[a]);
        const int nav  = vs.moveToIndexPos(want, fast);

        int decoded = -1;
        try {
            decoded = decoder.moveToFrameIndex(nav);
        } catch (TTMpeg2DecoderException& ex) {
            printf("%-9d %-9d %-9s %-10s %-34s %s\n",
                   want, nav, "-", "-", "-", qPrintable(ex.message()));
            continue;
        }

        // The decoder hands back the RGB32 buffer through the Y pointer;
        // TTMPEG2Window2 wraps exactly that into its QImage.
        TFrameInfo* info = decoder.getFrameInfo();
        if (!info || !info->Y) {
            printf("%-9d %-9d %-9d %-10s %-34s %s\n",
                   want, nav, decoded, "-", "-", "no frame buffer");
            continue;
        }

        const int bytes = info->width * info->height * 4;
        QByteArray pic(reinterpret_cast<const char*>(info->Y), bytes);
        const QByteArray sum = QCryptographicHash::hash(pic, QCryptographicHash::Md5).toHex();

        double mean = 0.0;
        for (int i = 0; i < bytes; i += 4)      // RGB32: B G R x
            mean += (pic[i] & 0xFF) + (pic[i + 1] & 0xFF) + (pic[i + 2] & 0xFF);
        mean /= double(bytes / 4) * 3.0;

        printf("%-9d %-9d %-9d %-10.1f %-34s %s\n",
               want, nav, decoded, mean, sum.constData(),
               (sum == previous) ? "SAME PICTURE AS PREVIOUS" : "");
        previous = sum;
    }
    return 0;
}
