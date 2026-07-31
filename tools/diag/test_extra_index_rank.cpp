/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic harness (TODO "Latent: mExtraIndices speichert Stream-Ordnung",  */
/* 2026-07-31): does the field-pair extra index actually drift against the     */
/* display position it is compared with?                                      */
/*                                                                            */
/* TTMpeg2VideoStream::createIndexList() appends the running picture counter   */
/* (bitstream order) to mExtraIndices. TTOpenVideoTask then calls             */
/* sortDisplayOrder(), after which the same list is consumed as if it held    */
/* DISPLAY positions -- TTAVData::countExtraFramesBefore() compares it with    */
/* cutInIndex()/cutOutIndex(), TTCurrentFrame with currentIndex().            */
/*                                                                            */
/* This harness measures both quantities directly:                            */
/*   (1) per extra: display rank minus stored stream position                 */
/*   (2) per display position p: the audio-correction error, i.e. the         */
/*       difference between what countExtraFramesBefore(p) returns today and   */
/*       what it would return if the list held display ranks.                 */
/* Identity across the sort is the header-list index, which is unique per     */
/* picture and untouched by sorting.                                          */
/*----------------------------------------------------------------------------*/

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QString>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"
#include "avstream/ttavheader.h"

// Mirror of TTAVData::countExtraFramesBefore(): how many entries of a sorted
// list are strictly below frameIndex.
static int countBefore(const QList<int>& sorted, int frameIndex)
{
  int lo = 0, hi = sorted.size();
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (sorted[mid] < frameIndex) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.m2v> [max_examples]\n", argv[0]);
    return 2;
  }
  const int maxExamples = (argc > 2) ? atoi(argv[2]) : 12;

  QFileInfo fi(QString::fromLocal8Bit(argv[1]));
  TTMpeg2VideoStream vs(fi);
  vs.createHeaderList();
  vs.createIndexList();

  TTVideoIndexList* il = vs.indexList();
  const QList<int> extras = vs.extraIndices();
  const int n = il->count();

  printf("== file                      : %s\n", qPrintable(fi.fileName()));
  printf("== pictures (index entries)  : %d\n", n);
  printf("== extras (2nd field)        : %d\n", extras.size());
  if (extras.isEmpty()) {
    printf("\nNo field pairs in this stream -- nothing to measure.\n");
    return 0;
  }

  // Identity before the sort: stream position -> header-list index.
  QList<int> hdrByStreamPos;
  hdrByStreamPos.reserve(n);
  for (int i = 0; i < n; ++i)
    hdrByStreamPos.append(il->videoIndexAt(i)->getHeaderListIndex());

  // Sort exactly as TTOpenVideoTask does, then map back.
  il->sortDisplayOrder();
  QHash<int, int> rankByHdr;
  rankByHdr.reserve(n);
  for (int p = 0; p < n; ++p)
    rankByHdr.insert(il->videoIndexAt(p)->getHeaderListIndex(), p);

  // (1) Per-extra drift.
  QList<int> extraRanks;
  extraRanks.reserve(extras.size());
  int nonZero = 0, minD = 0, maxD = 0;
  long long sumAbs = 0;
  printf("\n== (1) per extra: stored stream position vs. display rank\n");
  printf("   %8s %8s %7s   (first %d)\n", "stream", "rank", "delta", maxExamples);
  for (int i = 0; i < extras.size(); ++i) {
    const int e = extras[i];
    const int rank = rankByHdr.value(hdrByStreamPos[e], -1);
    extraRanks.append(rank);
    const int d = rank - e;
    if (d != 0) ++nonZero;
    if (i == 0 || d < minD) minD = d;
    if (i == 0 || d > maxD) maxD = d;
    sumAbs += (d < 0 ? -d : d);
    if (i < maxExamples)
      printf("   %8d %8d %+7d\n", e, rank, d);
  }
  printf("   ...\n");
  printf("   drift != 0 : %d of %d extras\n", nonZero, extras.size());
  printf("   delta range: %+d .. %+d, mean |delta| = %.2f\n",
         minD, maxD, double(sumAbs) / double(extras.size()));

  // (2) Consumer-level error.
  //
  // Reference list: the two fields of a pair land on two consecutive display
  // slots and show ONE frame, so the duplicate slot is the HIGHER of the two
  // ranks -- at p = higherRank the slots before p still hold one displayed
  // frame, at p = higherRank+1 they hold a duplicate. Taking the parser's
  // "second field" instead would be ambiguous: compareFunc only orders by
  // display order, both fields carry the same one, and std::sort is not
  // stable, so the parser's second field can end up on either slot.
  QList<int> ranksSorted;
  ranksSorted.reserve(extras.size());
  for (int i = 0; i < extras.size(); ++i) {
    const int e = extras[i];
    const int rankSecond = extraRanks[i];
    const int rankFirst  = (e >= 1) ? rankByHdr.value(hdrByStreamPos[e - 1], -1)
                                    : rankSecond;
    ranksSorted.append(qMax(rankFirst, rankSecond));
  }
  std::sort(ranksSorted.begin(), ranksSorted.end());

  // Equal countBefore() for every p is the same statement as "both lists are
  // the same multiset", so say it directly -- that is the property the three
  // counting consumers actually depend on.
  int multisetDiffs = 0;
  for (int i = 0; i < extras.size(); ++i)
    if (extras[i] != ranksSorted[i]) ++multisetDiffs;
  printf("\n== multiset check: stored stream positions vs. duplicate display slots\n");
  printf("   differing entries after sorting both: %d of %d%s\n",
         multisetDiffs, extras.size(),
         multisetDiffs == 0 ? "  => identical multiset" : "  => NOT identical");

  int worstErr = 0, worstPos = -1, positionsWrong = 0;
  QHash<int, int> errHist;
  QList<QPair<int, int> > wrongPositions;   // (position, error)
  for (int p = 0; p <= n; ++p) {
    const int today   = countBefore(extras, p);       // what the code returns
    const int correct = countBefore(ranksSorted, p);  // what it should return
    const int err = today - correct;
    errHist[err] += 1;
    if (err != 0) {
      ++positionsWrong;
      wrongPositions.append(qMakePair(p, err));
      if (qAbs(err) > qAbs(worstErr)) { worstErr = err; worstPos = p; }
    }
  }

  printf("\n== (2) audio-correction error over all %d display positions\n", n + 1);
  printf("   positions with a wrong extra count : %d (%.2f %%)\n",
         positionsWrong, 100.0 * positionsWrong / double(n + 1));
  printf("   worst error : %+d frames at display position %d\n", worstErr, worstPos);
  QList<int> keys = errHist.keys();
  std::sort(keys.begin(), keys.end());
  printf("   error histogram (frames -> positions):\n");
  for (int k : keys)
    printf("     %+3d : %d\n", k, errHist[k]);

  // A cut point only picks up the error if it lands on one of these positions,
  // so list them -- together with how far the nearest field pair is away.
  if (!wrongPositions.isEmpty()) {
    printf("   affected display positions (pos:error, nearest extra rank):\n     ");
    for (int i = 0; i < wrongPositions.size(); ++i) {
      const int p = wrongPositions[i].first;
      int nearest = -1, bestDist = -1;
      for (int r : ranksSorted) {
        const int d = qAbs(r - p);
        if (bestDist < 0 || d < bestDist) { bestDist = d; nearest = r; }
      }
      printf("%d:%+d(%d,d=%d) ", p, wrongPositions[i].second, nearest, bestDist);
      if ((i + 1) % 4 == 0) printf("\n     ");
    }
    printf("\n");
  }

  // (3) Where the two fields of a pair actually land after sorting. The parser
  // marks the second field; its partner is the preceding picture in stream
  // order. Both carry the same display order, so std::sort (not stable) may
  // place them in either order -- what matters here is whether they stay
  // adjacent, i.e. whether the pair still occupies two consecutive slots.
  int nonAdjacent = 0;
  for (int i = 0; i < extras.size(); ++i) {
    const int e = extras[i];
    if (e < 1) continue;
    const int rankSecond = extraRanks[i];
    const int rankFirst  = rankByHdr.value(hdrByStreamPos[e - 1], -1);
    if (qAbs(rankSecond - rankFirst) != 1) ++nonAdjacent;
  }
  printf("\n== (3) field pairs not adjacent after sortDisplayOrder(): %d of %d\n",
         nonAdjacent, extras.size());

  return 0;
}
