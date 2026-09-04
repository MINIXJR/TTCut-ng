/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: an index-adopting wrapper must inherit the owner's stream      */
/* metadata along with the index. Wrapper A builds the index (SPS parse +     */
/* packet scan run there); wrapper B adopts the bundle. Before the bundle     */
/* existed, the quick-jump worker adopted the bare list, B's decode-order     */
/* tagging counted PAFF field packets as frames, and decodeFrame() on a       */
/* field-pair target drained the file to EOF: 72 675 ms measured on 06x03,    */
/* display 3566, against 13 ms on the metadata-carrying path.                 */
/*                                                                            */
/* usage: test_index_bundle_adopt <es-file> <display-index> [expected-AU]     */
/*        BREAK_METADATA=1  adopt the index with deliberately wrong metadata; */
/*                          the decode MUST fail, but quickly (bound B).      */
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
  const bool breakMetadata = !qgetenv("BREAK_METADATA").isEmpty();

  TTFFmpegWrapper owner;
  if (!owner.openFile(file)) { fprintf(stderr, "owner open failed\n"); return 2; }
  int vs = owner.findBestVideoStream();
  TTFrameIndexer ix;
  if (vs < 0 || !ix.build(file, vs, nullptr)) { fprintf(stderr, "owner index failed\n"); return 2; }
  owner.setFrameIndex(ix.bundle());
  check(owner.isPAFF(), "owner detected PAFF");

  TTFrameIndexBundle bundle = owner.frameIndexBundle();
  check(bundle.index.size() == owner.frameIndex().size(), "bundle carries the full index");
  check(bundle.isPAFF == owner.isPAFF(), "bundle carries isPAFF");
  check(bundle.frameMbsOnlyFlag == owner.h264FrameMbsOnlyFlag(),
        "bundle carries frame_mbs_only_flag");
  check(bundle.log2MaxFrameNum == owner.h264Log2MaxFrameNum(),
        "bundle carries log2_max_frame_num");

  if (breakMetadata) {
    // The pre-fix quick-jump state: index without the owner's measurements.
    bundle.isPAFF           = false;
    bundle.frameMbsOnlyFlag = true;
    bundle.log2MaxFrameNum  = 4;
  }

  TTFFmpegWrapper adopter;
  if (!adopter.openFile(file)) { fprintf(stderr, "adopter open failed\n"); return 2; }
  adopter.setFrameIndex(bundle);

  if (!breakMetadata) {
    check(adopter.isPAFF(), "adopter inherited PAFF state");
    check(!adopter.h264FrameMbsOnlyFlag(), "adopter inherited frame_mbs_only_flag=0");
  }

  if (expectAU < 0) {
    expectAU = adopter.displayOrderMap().displayToDecode(display);
    printf("expected AU derived from display map: %d\n", expectAU);
  }

  QElapsedTimer t; t.start();
  QImage img = adopter.decodeFrame(display);
  const qint64 ms = t.elapsed();
  printf("decodeFrame(%d) -> %s, %lld ms\n", display, img.isNull() ? "null" : "image",
         static_cast<long long>(ms));

  if (breakMetadata) {
    // Bound B: wrong metadata must still fail fast instead of draining to EOF.
    check(ms < 1000, "wrong metadata fails in under 1 s (bound B holds)");
  } else {
    check(!img.isNull(), "decodeFrame returned an image");
    const QList<TTFrameInfo>& idx = adopter.frameIndex();
    bool delivered = display >= 0 && display < idx.size() &&
                     idx[display].deliveredDecodeIndex == expectAU;
    check(delivered, "deliveredDecodeIndex == expected target AU (no fallback)");
    check(ms < 100, "delivery under 100 ms");
  }

  return failures == 0 ? 0 : 1;
}
