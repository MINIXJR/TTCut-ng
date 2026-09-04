/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: verify that an index-ADOPTING wrapper decodes PAFF field-pair  */
/* targets (Befund B). Wrapper A builds the frame index (owner); wrapper B    */
/* opens the same file and adopts index + stream metadata exactly like        */
/* every adopter of TTH26xVideoStream::frameIndexBundle() does. Pre-fix, B's  */
/* decode-order tagging counted field packets as frames, so decodeFrame() on  */
/* a field-pair display target drained the file to EOF (2x53 s) and fell back */
/* to a neighbor frame (deliveredDecodeIndex left unset — the distinguishing  */
/* signal). Usage: test_adopt_paff <es-file> <display-index> [expected-AU]    */
/* Without expected-AU the target is derived from the display-order map       */
/* (an explicit value additionally cross-checks the map).                     */
/*----------------------------------------------------------------------------*/

#include "../../extern/ttffmpegwrapper.h"
#include "../../avstream/ttframeindexer.h"

#include <QElapsedTimer>

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
  if (argc < 3) {
    fprintf(stderr, "usage: %s <es-file> <display-index> [expected-AU]\n", argv[0]);
    return 2;
  }
  const QString file = QString::fromLocal8Bit(argv[1]);
  const int display  = atoi(argv[2]);
  int expectAU = (argc >= 4) ? atoi(argv[3]) : -1;

  // Owner: builds the index (SPS parse + packet scan run here).
  TTFFmpegWrapper owner;
  if (!owner.openFile(file)) { fprintf(stderr, "owner open failed\n"); return 2; }
  int vs = owner.findBestVideoStream();
  TTFrameIndexer ix;
  if (vs < 0 || !ix.build(file, vs, nullptr)) { fprintf(stderr, "owner index failed\n"); return 2; }
  owner.setFrameIndex(ix.bundle());
  check(owner.isPAFF(), "owner detected PAFF");

  // Adopter: same file, index + metadata adopted (frameIndexBundle adoption sequence).
  TTFFmpegWrapper adopter;
  if (!adopter.openFile(file)) { fprintf(stderr, "adopter open failed\n"); return 2; }
  adopter.setFrameIndex(owner.frameIndexBundle());
  check(adopter.isPAFF(), "adopter inherited PAFF state");
  check(!adopter.h264FrameMbsOnlyFlag(), "adopter inherited frame_mbs_only_flag=0");

  if (expectAU < 0) {
    expectAU = adopter.displayOrderMap().displayToDecode(display);
    printf("expected AU derived from display map: %d\n", expectAU);
  }

  QElapsedTimer t; t.start();
  QImage img = adopter.decodeFrame(display);
  const qint64 ms = t.elapsed();
  printf("decodeFrame(%d) -> %s, %lld ms\n", display, img.isNull() ? "null" : "image",
         static_cast<long long>(ms));

  check(!img.isNull(), "decodeFrame returned an image");
  // The fallback path returns a NEIGHBOR image but never sets
  // deliveredDecodeIndex for the requested display position — so this is the
  // proof of real delivery, not fallback.
  const QList<TTFrameInfo>& idx = adopter.frameIndex();
  bool delivered = display >= 0 && display < idx.size() &&
                   idx[display].deliveredDecodeIndex == expectAU;
  check(delivered, "deliveredDecodeIndex == expected target AU (no fallback)");
  check(ms < 30000, "delivery under 30 s (no EOF drain)");

  return failures == 0 ? 0 : 1;
}
