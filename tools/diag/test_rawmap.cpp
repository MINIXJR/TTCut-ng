/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: verify the PAFF raw->merged AU map recorded by                 */
/* mergePAFFFieldsInIndex. Opens an ES, builds the frame index, checks map    */
/* invariants, and prints counts plus the first dropped-RASL raw index (for   */
/* the RASL guard gate). Optional argv[2]/argv[3]: expected raw/merged count. */
/*----------------------------------------------------------------------------*/

#include "../../extern/ttffmpegwrapper.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

int main(int argc, char** argv)
{
  if (argc < 2) { fprintf(stderr, "usage: %s <es-file> [expRaw expMerged]\n", argv[0]); return 2; }

  TTFFmpegWrapper w;
  if (!w.openFile(QString::fromLocal8Bit(argv[1]))) {
    fprintf(stderr, "open failed: %s\n", qPrintable(w.lastError())); return 2;
  }
  int vs = w.findBestVideoStream();
  if (vs < 0 || !w.buildFrameIndex(vs)) {
    fprintf(stderr, "index failed: %s\n", qPrintable(w.lastError())); return 2;
  }

  const int raw    = w.rawPacketCount();
  const int merged = w.frameCount();
  int collapsed = 0;
  for (int r = 0; r < raw; ++r)
    if (w.rawIsCollapsedField(r)) ++collapsed;
  printf("raw=%d merged=%d collapsed=%d paff=%d\n", raw, merged, collapsed, w.isPAFF() ? 1 : 0);

  check(raw >= merged, "raw >= merged");
  check(raw - collapsed == merged, "raw - collapsed == merged");
  check(w.rawToMergedIndex(-1) == -1 && w.rawToMergedIndex(raw) == -1,
        "out-of-range raw maps to -1");

  // Map invariants: monotone non-decreasing, collapsed maps to its top's index
  bool monotone = true, pairing = true;
  int prev = -1;
  for (int r = 0; r < raw; ++r) {
    int m = w.rawToMergedIndex(r);
    if (m < prev) monotone = false;
    if (w.rawIsCollapsedField(r) && r > 0 && m != w.rawToMergedIndex(r - 1))
      pairing = false;
    prev = m;
  }
  check(monotone, "map monotone non-decreasing");
  check(pairing, "collapsed field maps to its top's merged index");
  check(w.rawToMergedIndex(raw > 0 ? raw - 1 : 0) == merged - 1,
        "last raw maps to last merged");

  // First raw AU whose merged frame has no display slot (dropped RASL) —
  // consumed by the RASL guard gate.
  int firstNoDisp = -1;
  for (int r = 0; r < raw && firstNoDisp < 0; ++r) {
    int m = w.rawToMergedIndex(r);
    if (m >= 0 && w.displayOrderMap().decodeToDisplay(m) < 0) firstNoDisp = r;
  }
  printf("first_dropped_rasl_raw=%d\n", firstNoDisp);

  if (argc >= 4) {
    check(raw == atoi(argv[2]), "expected raw count");
    check(merged == atoi(argv[3]), "expected merged count");
  }
  return failures == 0 ? 0 : 1;
}
