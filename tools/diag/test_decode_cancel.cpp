/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: a decode in flight must abandon its skip loop when the owner    */
/* sets the cancel flag. The quick-jump worker checks its abort flag only      */
/* BETWEEN frames; while decodeFrame() was running, closing the dialog blocked */
/* the GUI thread in ~TTThreadTaskPool's waitForDone() for as long as the      */
/* decode took. Usage: test_decode_cancel <es-file> <display-index>            */
/*                                                                              */
/* Self-calibrating trigger: a hard-coded millisecond delay tied to one        */
/* corpus file is a false-negative risk on different material or hardware —    */
/* if the decode is faster than the delay, the cancel fires after the decode   */
/* already finished, and a CORRECT binary prints FAIL. Instead we measure one  */
/* warm-up decode of the SAME target frame to learn its steady-state cost on   */
/* this machine, then fire the cancel at a fraction of that measured time —    */
/* comfortably before the decode would finish on its own, whatever that time   */
/* turns out to be.                                                            */
/*----------------------------------------------------------------------------*/

#include "../../extern/ttffmpegwrapper.h"
#include "../../extern/ttframeindexer.h"

#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

static int failures = 0;
static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: %s <es-file> <display-index>\n", argv[0]);
    return 2;
  }
  const QString file = QString::fromLocal8Bit(argv[1]);
  const int display  = atoi(argv[2]);

  TTFFmpegWrapper owner;
  if (!owner.openFile(file)) { fprintf(stderr, "owner open failed\n"); return 2; }
  int vs = owner.findBestVideoStream();
  TTFrameIndexer ix;
  if (vs < 0 || !ix.build(file, vs, nullptr)) { fprintf(stderr, "owner index failed\n"); return 2; }
  owner.setFrameIndex(ix.bundle());

  TTFFmpegWrapper w;
  if (!w.openFile(file)) { fprintf(stderr, "open failed\n"); return 2; }
  w.setFrameIndex(owner.frameIndexBundle());

  // Calibration: decodeFrame() always seeks (no sequential-decode shortcut,
  // see CLAUDE.md), so timing the very same target frame twice — once here,
  // uncancelled, once below, cancelled — measures the same work both times.
  // Run it once, uncancelled, to learn how long THIS frame takes on THIS
  // machine right now (also warms the OS page cache / one-time decoder
  // context costs, so the timed call below is the steady-state case).
  QElapsedTimer calib; calib.start();
  QImage calibImg = w.decodeFrame(display);
  const qint64 calibMs = calib.elapsed();
  check(!calibImg.isNull(), "calibration decode (uncancelled) succeeds");

  // The calibration call cached the result — clear it so the timed,
  // cancelled call below actually re-decodes instead of returning from the
  // LRU cache in ~0 ms (which would give the cancel nothing to interrupt).
  w.clearFrameCache();

  // Fire the cancel at 40% of the measured decode time: comfortably inside
  // the window the decode is expected to still be running, on whatever
  // material and hardware this happens to run on. Floor of 1 ms guards
  // against a calibration read of 0 (timer resolution / a trivially cheap
  // frame) collapsing the delay to zero.
  const qint64 triggerMs = qMax<qint64>(1, calibMs * 2 / 5);
  printf("calibration: decodeFrame(%d) took %lld ms uncancelled; firing cancel at %lld ms\n",
         display, static_cast<long long>(calibMs), static_cast<long long>(triggerMs));

  std::atomic<bool> cancel{false};
  w.setCancelToken(&cancel);

  std::thread trigger([&cancel, triggerMs]{
    std::this_thread::sleep_for(std::chrono::milliseconds(triggerMs));
    cancel = true;
  });

  QElapsedTimer t; t.start();
  QImage img = w.decodeFrame(display);
  const qint64 ms = t.elapsed();
  trigger.join();

  printf("decodeFrame(%d) after cancel -> %s, %lld ms\n", display,
         img.isNull() ? "null" : "image", static_cast<long long>(ms));

  // `ms` is timed from BEFORE decodeFrame() starts, i.e. it already includes
  // the triggerMs the trigger thread spends sleeping before it sets the
  // cancel flag. Subtract triggerMs so the check measures only the latency
  // from "cancel flag set" to "decodeFrame() returns" — the thing this
  // harness actually cares about. Do NOT simplify this back to `ms < 200`:
  // triggerMs scales with the calibrated decode cost (40% of it), so on
  // material where a single decode costs ~330 ms or more, triggerMs alone
  // exceeds 200 ms and a correct binary would print FAIL. Both operands are
  // qint64 (signed), so the subtraction cannot wrap even if it were briefly
  // negative.
  check(ms - triggerMs < 200, "decode returned within 200 ms of the cancel");
  check(img.isNull(), "cancelled decode returns no image");

  // A cancel is not a failure: no neighbour attempt may run.
  cancel = false;
  w.setCancelToken(nullptr);
  // Clear again so this final decode is a real one, not another cache hit —
  // otherwise "works again" would trivially pass regardless of correctness.
  w.clearFrameCache();
  QImage again = w.decodeFrame(display);
  check(!again.isNull(), "decoding works again after the token is cleared");

  return failures == 0 ? 0 : 1;
}
