/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Measures the order domain of the marker positions reported by
// TTStreamPointVideoWorker::detectAspectChanges().
//
//   usage: test_streampoint_order <es-file.m2v> [maxRowsPrinted]
//
// The worker walks TTVideoHeaderList and counts picture_start_code headers,
// i.e. it counts in BITSTREAM (= decode) order. The number it puts into
// TTStreamPoint is consumed by TTCutMainWindow::onStreamPointJump, which hands
// it to onVideoSliderChanged - and for MPEG-2 the navigation index is a
// DISPLAY-order position (TTVideoIndexList::sortDisplayOrder() sorts by
// base_number + temporal_reference).
//
// For every sequence header the harness prints both numbers:
//   reported  - the worker's picture counter at that header
//   display   - the rank, in the display-sorted index list, of the first
//               picture that follows the header
// and their difference. A non-zero difference is the offset a user sees when
// double-clicking the marker.
//
// The run then puts the real worker to work on the same file and checks every
// marker it reports against the display position measured above. That part is
// the gate: exit code 1 if any reported marker sits somewhere else.
//
// Build via `make test_streampoint_order` in tools/diag (needs a root `make`
// first, so ../../obj/*.o exist).

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QString>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoheaderlist.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttmpeg2videoheader.h"
#include "avstream/ttavtypes.h"
#include "data/ttstreampoint.h"
#include "data/ttstreampoint_videoworker.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <es-file.m2v> [maxRowsPrinted]\n", argv[0]);
        return 2;
    }
    const QString file    = QString::fromLocal8Bit(argv[1]);
    const int     maxRows = (argc > 2) ? atoi(argv[2]) : 20;

    QFileInfo fi(file);
    TTMpeg2VideoStream vs(fi);
    const int headerCount = vs.createHeaderList();
    const int indexCount  = vs.createIndexList();
    printf("headerCount=%d indexCount=%d\n", headerCount, indexCount);

    TTVideoHeaderList* headerList = vs.headerList();
    TTVideoIndexList*  indexList  = vs.indexList();
    if (!headerList || !indexList) {
        fprintf(stderr, "createHeaderList/createIndexList failed to populate lists\n");
        return 1;
    }

    // Exactly what the application does after opening an MPEG-2 stream.
    indexList->sortDisplayOrder();

    // header-list index of a picture -> its rank in the display-sorted list,
    // which is the number every navigation path (slider, Goto, stream-point
    // jump) works with.
    QHash<int, int> headerIdxToDisplayRank;
    int rankVsValueMismatch = 0, firstRankMismatch = -1;
    for (int p = 0; p < indexList->count(); ++p) {
        headerIdxToDisplayRank.insert(indexList->headerListIndex(p), p);

        // Is the list position identical to the display_order VALUE
        // (base_number + temporal_reference)? If yes, a fix can compute the
        // position arithmetically; if not, only a lookup in the sorted list
        // gives the position the navigation really uses.
        if (indexList->videoIndexAt(p)->getDisplayOrder() != p) {
            if (firstRankMismatch < 0) firstRankMismatch = p;
            rankVsValueMismatch++;
        }
    }
    printf("index entries where rank != display_order value: %d of %d (first at %d)\n",
           rankVsValueMismatch, indexList->count(), firstRankMismatch);

    // Replay of TTStreamPointVideoWorker::detectAspectChanges(), plus the
    // display-order lookup the worker does not do.
    int pictureCount = 0;
    int prevAspect   = -1;
    int rows = 0, seqCount = 0, mismatches = 0, aspectChanges = 0, aspectMismatches = 0;
    QHash<int, int> deltaHistogram;
    QList<int>      expectedChangeDisplay;   // display position of every change

    for (int i = 0; i < headerList->size(); ++i) {
        TTVideoHeader* hdr = headerList->headerAt(i);
        if (!hdr) continue;

        if (hdr->headerType() == TTMpeg2VideoHeader::picture_start_code) {
            pictureCount++;
            continue;
        }

        if (hdr->headerType() != TTMpeg2VideoHeader::sequence_start_code)
            continue;

        seqCount++;
        const int aspect = ((TTSequenceHeader*)hdr)->aspectRatio();
        const bool isChange = (prevAspect >= 0 && aspect != prevAspect);
        prevAspect = aspect;
        if (isChange) aspectChanges++;

        // First picture that follows this sequence header.
        int nextPicHdr = -1;
        for (int j = i + 1; j < headerList->size(); ++j) {
            TTVideoHeader* h2 = headerList->headerAt(j);
            if (h2 && h2->headerType() == TTMpeg2VideoHeader::picture_start_code) {
                nextPicHdr = j;
                break;
            }
        }
        if (nextPicHdr < 0) continue;

        const int displayRank = headerIdxToDisplayRank.value(nextPicHdr, -1);
        if (displayRank < 0) continue;

        const int tref = ((TTPicturesHeader*)headerList->headerAt(nextPicHdr))->temporal_reference;
        const int type = ((TTPicturesHeader*)headerList->headerAt(nextPicHdr))->picture_coding_type;
        const int delta = displayRank - pictureCount;

        deltaHistogram[delta]++;
        if (isChange) expectedChangeDisplay.append(displayRank);
        if (delta != 0) {
            mismatches++;
            if (isChange) aspectMismatches++;
        }

        if (rows < maxRows || isChange || delta != 0) {
            printf("seq@hdr%-8d reported=%-7d display=%-7d delta=%-4d "
                   "tref=%-3d type=%d%s\n",
                   i, pictureCount, displayRank, delta, tref, type,
                   isChange ? "  <== aspect change" : "");
            rows++;
        }
    }

    printf("\nsequence headers=%d  aspect changes=%d\n", seqCount, aspectChanges);
    printf("headers whose reported position != display position: %d of %d\n",
           mismatches, seqCount);
    printf("aspect changes with a wrong position: %d of %d\n",
           aspectMismatches, aspectChanges);

    printf("delta histogram (display - reported):\n");
    QList<int> keys = deltaHistogram.keys();
    std::sort(keys.begin(), keys.end());
    for (int k : keys)
        printf("  delta %+d : %d\n", k, deltaHistogram.value(k));

    // ---------------------------------------------------------------------
    // Gate: the real worker on the same file. Every marker it reports has to
    // sit on the display position measured above.
    // ---------------------------------------------------------------------
    QList<TTStreamPoint> points;
    TTStreamPointVideoWorker worker(TTAVTypes::mpeg2_demuxed_video,
                                    headerList, indexList, vs.frameRate());
    QObject::connect(&worker, &TTStreamPointVideoWorker::pointsDetected,
                     [&points](const QList<TTStreamPoint>& p) { points = p; });
    worker.runSynchron();

    printf("\nworker reported %d marker(s)\n", points.size());

    if (points.size() != expectedChangeDisplay.size()) {
        printf("FAIL: %d aspect change(s) measured, worker reported %d marker(s)\n",
               expectedChangeDisplay.size(), points.size());
        return 1;
    }

    int wrong = 0;
    for (int i = 0; i < points.size(); ++i) {
        const int want = expectedChangeDisplay.at(i);
        const bool ok  = (points.at(i).frameIndex() == want);
        printf("  marker frame=%-8d expected=%-8d %-16s %s\n",
               points.at(i).frameIndex(), want,
               qPrintable(points.at(i).description()),
               ok ? "ok" : "WRONG POSITION");
        if (!ok) wrong++;
    }

    if (wrong > 0) {
        printf("FAIL: %d marker(s) not on the display position\n", wrong);
        return 1;
    }
    printf("PASS: all %d marker(s) on the measured display position\n",
           points.size());
    return 0;
}
