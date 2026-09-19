/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Gate for ttAnalyzeAspectWindow() (avstream/ttaspectwindow.cpp).
//
// The fixture comes from run-gates.sh make_aspect_m2v: open GOPs, 4:3 with a
// 16:9 run [A, B] whose display positions the generator computed from the
// bitstream itself (GOP base + temporal_reference). A is a leading
// B-picture, so the open-GOP rule - a leading B-picture belongs to the NEW
// sequence header although it is displayed before its I-picture - is part
// of every check.
//
//   usage: test_aspect_window <fixture.m2v> <A> <B> [01x03.m2v]
//
// With the fourth argument the real recording that motivated the feature is
// checked too: cut 23975..54686 starts on the last 4:3 picture, the 16:9
// programme begins at 23976.
#include <QCoreApplication>
#include <QFileInfo>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttaspectwindow.h"
#include "avstream/ttmpeg2videostream.h"
#include "avstream/ttvideoindexlist.h"

namespace {

int failures = 0;
void check(bool ok, const char* what, const TTAspectWindowInfo& r)
{
  printf("%s: %s  (main=%d in=%d out=%d inTarget=%d outTarget=%d)\n",
         ok ? "PASS" : "FAIL", what, r.mainAspect, r.cutInAspect,
         r.cutOutAspect, r.cutInTarget, r.cutOutTarget);
  if (!ok) failures++;
}

// Header and index list exactly as TTOpenVideoTask builds them.
void open(TTMpeg2VideoStream& vs)
{
  vs.createHeaderList();
  vs.createIndexList();
  vs.indexList()->sortDisplayOrder();
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 4) {
    fprintf(stderr, "usage: %s <fixture.m2v> <A> <B> [01x03.m2v]\n", argv[0]);
    return 2;
  }
  const int A = atoi(argv[2]);
  const int B = atoi(argv[3]);

  TTMpeg2VideoStream vs{QFileInfo(QString::fromLocal8Bit(argv[1]))};
  open(vs);
  printf("fixture: %d pictures, 16:9 run [%d, %d]\n", vs.frameCount(), A, B);
  if (B - A < 20 || A < 12 || B + 3 >= vs.frameCount()) {
    printf("FAIL: fixture shape unusable for the checks below\n");
    return 1;
  }
  printf("%s: first 16:9 picture is a B-picture (open GOP covered)\n",
         vs.frameType(A) == 3 ? "PASS" : "FAIL");
  if (vs.frameType(A) != 3) failures++;

  TTAspectWindowInfo r;

  r = ttAnalyzeAspectWindow(&vs, A + 5, B - 5);
  check(r.mainAspect == 3 && r.cutInTarget < 0 && r.cutOutTarget < 0,
        "window inside the 16:9 run: nothing to report", r);

  r = ttAnalyzeAspectWindow(&vs, A - 1, B - 5);
  check(r.mainAspect == 3 && r.cutInAspect == 2 && r.cutInTarget == A && r.cutOutTarget < 0,
        "one 4:3 picture at the cut-in: target is the first 16:9 picture", r);

  r = ttAnalyzeAspectWindow(&vs, A + 5, B + 1);
  check(r.mainAspect == 3 && r.cutOutAspect == 2 && r.cutOutTarget == B && r.cutInTarget < 0,
        "one 4:3 picture at the cut-out: target is the last 16:9 picture", r);

  r = ttAnalyzeAspectWindow(&vs, A - 2, B + 2);
  check(r.cutInTarget == A && r.cutOutTarget == B,
        "4:3 at both edges: both targets", r);

  r = ttAnalyzeAspectWindow(&vs, 0, A + 2);
  check(r.mainAspect == 2 && r.cutOutAspect == 3 && r.cutOutTarget == A - 1 && r.cutInTarget < 0,
        "4:3 majority ending in 16:9: cut-out target is the last 4:3 picture", r);

  r = ttAnalyzeAspectWindow(&vs, A - 10, A + 9);
  check(r.mainAspect == -1 && r.cutInTarget < 0 && r.cutOutTarget < 0
        && r.cutInAspect == 2 && r.cutOutAspect == 3,
        "10 x 4:3 + 10 x 16:9 is a tie: no majority, nothing reported", r);

  r = ttAnalyzeAspectWindow(nullptr, 0, 10);
  check(r.mainAspect == -1 && r.cutInAspect == -1, "null stream: empty result", r);

  r = ttAnalyzeAspectWindow(&vs, -5, 3);
  check(r.mainAspect == 2 && r.cutInAspect == 2 && r.cutInTarget < 0,
        "window starting before 0 is clamped", r);

  r = ttAnalyzeAspectWindow(&vs, 50, 40);
  check(r.mainAspect == -1 && r.cutInAspect == -1, "inverted window: empty result", r);

  if (argc > 4) {
    TTMpeg2VideoStream rec{QFileInfo(QString::fromLocal8Bit(argv[4]))};
    open(rec);
    r = ttAnalyzeAspectWindow(&rec, 23975, 54686);
    check(r.mainAspect == 3 && r.cutInAspect == 2 && r.cutInTarget == 23976 && r.cutOutTarget < 0,
          "01x03 cut 1: starts on the last 4:3 picture, target 23976", r);
  }

  printf("%s\n", failures ? "ASPECT-WINDOW FAIL" : "ASPECT-WINDOW PASS");
  return failures ? 1 : 0;
}
