/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: TTH26xVideoStream::findIDRBefore returns the last TRUE IDR     */
/* (NAL scan, TTFrameInfo::isIDR) at or before a display position, and the    */
/* cut-out preview window built on it never starts behind its end.           */
/* Before audit run 12 it returned the last libav key picture in DECODE      */
/* order: a recovery point or CRA, and for an open-GOP leading picture the    */
/* key picture displayed after it (cut [46, 49] -> window [50, 49]).          */
/* Usage: test_h26x_idr_before <es-file> [...]                                */
/*----------------------------------------------------------------------------*/

#include "../../avstream/tth264videostream.h"
#include "../../avstream/tth265videostream.h"
#include "../../avstream/ttvideoindexlist.h"
#include "../../data/ttpreviewclip.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <cstdio>

static int failures = 0;
static void check(bool cond, const QString& what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", qPrintable(what));
  if (!cond) ++failures;
}

static void testFile(const QString& path)
{
  QFileInfo fi(path);
  const QString suffix = fi.suffix().toLower();
  TTH26xVideoStream* s = nullptr;
  if (suffix == "264" || suffix == "h264") s = new TTH264VideoStream(fi);
  else if (suffix == "265" || suffix == "h265") s = new TTH265VideoStream(fi);
  if (s == nullptr) { check(false, path + ": unsupported suffix"); return; }
  if (s->createHeaderList() <= 0 || s->createIndexList() <= 0) {
    check(false, path + ": open"); delete s; return;
  }
  s->indexList()->sortDisplayOrder();

  const TTFrameIndexBundle b = s->frameIndexBundle();
  const int n = s->frameCount();
  auto isIdrAt = [&](int disp) {
    const int dec = s->displayToDecodeIndex(disp);
    return dec >= 0 && dec < b.index.size() && b.index[dec].isIDR;
  };

  int idrs = 0, wrong = 0, firstWrong = -1, got = 0, want = 0, expected = -1;
  for (int p = 0; p < n; ++p) {
    if (isIdrAt(p)) { expected = p; ++idrs; }
    const int r = s->findIDRBefore(p);
    if (r != expected) {
      if (firstWrong < 0) { firstWrong = p; got = r; want = expected; }
      ++wrong;
    }
  }
  printf("%s: %d display positions, %d IDR\n", qPrintable(fi.fileName()), n, idrs);
  check(idrs > 0, fi.fileName() + ": at least one IDR");
  check(wrong == 0, fi.fileName() + QString(": findIDRBefore = last IDR <= position "
        "(%1 wrong, first at %2: got %3, want %4)").arg(wrong).arg(firstWrong).arg(got).arg(want));

  int inverted = 0, firstInverted = -1;
  for (int p = 0; p + 3 < n; ++p) {
    const QPair<int, int> w = ttPreviewCutOutWindow(s, p, p + 3, 125);
    if (w.first > w.second) { if (firstInverted < 0) firstInverted = p; ++inverted; }
  }
  check(inverted == 0, fi.fileName() + QString(": cut-out window of a 4-frame cut never "
        "starts behind its end (%1 inverted, first cut-in %2)").arg(inverted).arg(firstInverted));
  delete s;
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <es-file> [...]\n", argv[0]); return 2; }
  for (int i = 1; i < argc; ++i) testFile(QString::fromLocal8Bit(argv[i]));
  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
