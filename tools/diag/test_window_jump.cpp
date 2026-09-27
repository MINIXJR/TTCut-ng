/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Reproduces a stream-point jump through the REAL display widget, headless.
//
//   usage: QT_QPA_PLATFORM=offscreen test_window_jump <es-file.m2v> <pos> [pos ...]
//
// tools/diag/test_mpeg2_seek already showed that navigation and decoder answer
// every position correctly. This harness goes one layer up: it drives
// TTMPEG2Window2 exactly as TTCurrentFrame::onGotoFrame does
// (moveToIndexPos -> showFrameAt) and then reads back the pixmap the widget
// actually put on screen. If the pixmap does not change while the position
// does, the fault is in the widget - which is what "the number jumps but the
// picture stays" looks like.
//
// Build via `make test_window_jump` in tools/diag.

#include <QApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QPixmap>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "mpeg2window/ttmpeg2window2.h"

static QByteArray pixmapSum(const QPixmap& pm)
{
    if (pm.isNull()) return QByteArray("<null pixmap>");
    const QImage img = pm.toImage().convertToFormat(QImage::Format_RGB32);
    return QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(img.constBits()),
                   int(img.sizeInBytes())),
        QCryptographicHash::Md5).toHex();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 3) {
        fprintf(stderr, "usage: %s <es-file.m2v> <pos> [pos ...]\n", argv[0]);
        return 2;
    }

    QFileInfo fi(QString::fromLocal8Bit(argv[1]));
    TTMpeg2VideoStream vs(fi);
    vs.createHeaderList();
    vs.createIndexList();
    if (!vs.headerList() || !vs.indexList()) {
        fprintf(stderr, "header/index list empty\n");
        return 1;
    }
    vs.indexList()->sortDisplayOrder();
    printf("frames=%d\n\n", vs.indexList()->count());

    // The application creates TWO of these on the same stream - one for
    // "current frame", one for "cut out" - and the log shows both running
    // openVideoStream(), so two TTMpeg2Decoder instances live on the same file
    // at the same time and are used alternately. A test that destroys the
    // second one before touching the first misses whatever they share.
    TTMPEG2Window2 window;
    window.resize(720, 576);
    window.openVideoStream(&vs);

    TTMPEG2Window2 second;
    second.resize(720, 576);
    second.openVideoStream(&vs);

    printf("%-9s %-9s %-34s %s\n", "request", "nav", "pixmap checksum", "note");

    QByteArray previous;
    for (int a = 2; a < argc; ++a) {
        const int want = atoi(argv[a]);
        const int nav  = vs.moveToIndexPos(want, 0);

        window.showFrameAt(nav);
        app.processEvents();

        const QByteArray sum = pixmapSum(window.pixmap());
        printf("%-9d %-9d %-34s %s\n", want, nav, sum.constData(),
               (sum == previous) ? "SAME PICTURE AS PREVIOUS" : "");
        previous = sum;
    }
    return 0;
}
