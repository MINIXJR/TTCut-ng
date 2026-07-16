/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic harness (Defekt 2 investigation, 2026-07-16):                   */
/* dump the MPEG-2 field-picture extra-index positions and the frame types    */
/* around them, in BOTH stream order and display order, so we can locate the  */
/* field region and see how the double-index leaks into navigation vs. cut.   */
/*----------------------------------------------------------------------------*/

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <cstdio>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttavheader.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <file.m2v> [context]\n", argv[0]); return 2; }

    QFileInfo fi(QString::fromLocal8Bit(argv[1]));
    const int ctx = (argc > 2) ? atoi(argv[2]) : 8;

    TTMpeg2VideoStream vs(fi);
    vs.createHeaderList();
    vs.createIndexList();

    TTVideoIndexList* il = vs.indexList();
    const QList<int>& extras = vs.extraIndices();
    printf("== raw index entries (pictures) : %d\n", il->count());
    printf("== extra (2nd-field) indices    : %d\n", extras.size());
    printf("== implied real frames          : %d\n", il->count() - extras.size());

    if (!extras.isEmpty()) {
        printf("== first extra pic_num=%d  last extra pic_num=%d\n",
               extras.first(), extras.last());
        int gaps = 0;
        for (int i = 1; i < extras.size(); ++i)
            if (extras[i] != extras[i-1] + 2) gaps++;   // field pairs step by 2 in pic_num
        printf("== extra-index step!=2 transitions : %d (0 => single contiguous field-run)\n", gaps);
    }

    // Frame types in STREAM order (index list before sort == pic_num order)
    // around the first extra.
    if (!extras.isEmpty()) {
        int e = extras.first();
        int lo = (e - ctx > 0) ? e - ctx : 0;
        int hi = e + ctx;
        printf("\n== STREAM-order (pic_num) around first extra %d :\n", e);
        printf("   pic_num : type(1=I 2=P 3=B) : displayOrder : headerListIdx : EXTRA?\n");
        for (int i = lo; i <= hi && i < il->count(); ++i) {
            TTVideoIndex* vi = il->videoIndexAt(i);
            bool isExtra = extras.contains(i);
            printf("   %7d : %d : %d : %d %s\n", i,
                   vi ? vi->getPictureCodingType() : -1,
                   vi ? vi->getDisplayOrder() : -1,
                   vi ? vi->getHeaderListIndex() : -1,
                   isExtra ? "<-- EXTRA (2nd field)" : "");
        }
    }

    // Now sort into display order (what navigation/cut use) and dump the same
    // region, this time by display position.
    il->sortDisplayOrder();
    if (!extras.isEmpty()) {
        int e = extras.first();
        printf("\n== DISPLAY-order around display pos ~%d (first-extra region) :\n", e);
        printf("   dispPos : type : displayOrder : headerListIdx\n");
        int lo = (e - ctx > 0) ? e - ctx : 0;
        int hi = e + ctx;
        for (int i = lo; i <= hi && i < il->count(); ++i) {
            TTVideoIndex* vi = il->videoIndexAt(i);
            printf("   %7d : %d : %d : %d\n", i,
                   vi ? vi->getPictureCodingType() : -1,
                   vi ? vi->getDisplayOrder() : -1,
                   vi ? vi->getHeaderListIndex() : -1);
        }
    }
    return 0;
}
