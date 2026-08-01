/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: verify the PAFF raw->merged AU map recorded by                 */
/* mergePAFFFieldsInIndex. Opens an ES, builds the frame index, checks map    */
/* invariants, and prints counts plus the first dropped-RASL raw index (for   */
/* the RASL guard gate).                                                      */
/*                                                                            */
/* usage: test_rawmap <es-file> [expRaw expMerged] [--field-at N ...]         */
/*   expRaw/expMerged  asserted exactly when given                            */
/*   --field-at N      report whether MERGED index N is field-coded           */
/*                                                                            */
/* The field-coded positions in merged numbering (each hit is the top field   */
/* of a pair) used to be a separate tool (dump_fieldcoded, removed            */
/* 2026-07-31); it read the same frameIndex() this harness already builds.    */
/*----------------------------------------------------------------------------*/

#include "../../extern/ttffmpegwrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file> [expRaw expMerged] [--field-at N ...]\n", argv[0]);
    return 2;
  }

  // Split the tail into plain numbers (expected counts) and --field-at spots,
  // so the original two-argument form keeps working unchanged.
  std::vector<int> expected, fieldSpots;
  for (int a = 2; a < argc; ++a) {
    if (strcmp(argv[a], "--field-at") == 0) {
      while (a + 1 < argc && argv[a + 1][0] != '-') fieldSpots.push_back(atoi(argv[++a]));
    } else {
      expected.push_back(atoi(argv[a]));
    }
  }

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

  // Field-coded positions in MERGED numbering (top field of each pair).
  const QList<TTFrameInfo>& idx = w.frameIndex();
  int fieldCoded = 0;
  printf("field-coded merged positions (first 60):");
  for (int i = 0; i < idx.size(); ++i) {
    if (!idx[i].isFieldCoded) continue;
    if (fieldCoded < 60) printf(" %d", i);
    ++fieldCoded;
  }
  printf("\ntotal field-coded: %d\n", fieldCoded);
  for (int s : fieldSpots)
    printf("spot %d: %s\n", s,
           (s >= 0 && s < idx.size() && idx[s].isFieldCoded) ? "field-coded" : "not field-coded");

  if (expected.size() >= 2) {
    check(raw == expected[0], "expected raw count");
    check(merged == expected[1], "expected merged count");
  }
  return failures == 0 ? 0 : 1;
}
