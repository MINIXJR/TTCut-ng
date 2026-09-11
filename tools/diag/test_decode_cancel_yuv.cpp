/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: decodeFrameYUV() - the frame-search decode - must honour the    */
/* cancel token inside its skip loop. Sibling of test_decode_cancel           */
/* (decodeFrame(), 2026-08-28): the frame search checks its abort flag only   */
/* between frames, so a cancel arriving during one decode was ignored until   */
/* that decode had run to completion. The loop's EOF-drain bound (also        */
/* mirrored from decodeFrame()) has no runtime probe: wrong metadata makes    */
/* the tags run AHEAD of the target on this path, so the target is always hit */
/* (with the wrong frame) - measured 117 ms, no drain, before the change.     */
/*                                                                            */
/* usage: test_decode_cancel_yuv <es-file> <display-index>                    */
/*                                                                            */
/* Cancel trigger is self-calibrating like test_decode_cancel: one warm-up    */
/* decode of the same target measures its cost, the cancel fires at 40 % of   */
/* it. decodeFrameYUV() has no frame cache, and a repeated target is never    */
/* the sequential case (that needs target-1), so both calls do the same work. */
/* The 200 ms latency check only bites on material whose decode costs more    */
/* than ~330 ms; the "reports no frame" check is the decisive one.            */
/*----------------------------------------------------------------------------*/
#include "../../extern/ttffmpegwrapper.h"
#include "../../avstream/ttframeindexer.h"

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

  TFrameInfo info;

  // Calibration: the uncancelled cost of exactly this decode.
  QElapsedTimer calib; calib.start();
  const bool calibOk = w.decodeFrameYUV(display, info);
  const qint64 calibMs = calib.elapsed();
  check(calibOk, "calibration decode (uncancelled) succeeds");

  const qint64 triggerMs = qMax<qint64>(1, calibMs * 2 / 5);
  printf("calibration: decodeFrameYUV(%d) took %lld ms uncancelled; firing cancel at %lld ms\n",
         display, static_cast<long long>(calibMs), static_cast<long long>(triggerMs));

  std::atomic<bool> cancel{false};
  w.setCancelToken(&cancel);
  std::thread trigger([&cancel, triggerMs]{
    std::this_thread::sleep_for(std::chrono::milliseconds(triggerMs));
    cancel = true;
  });

  QElapsedTimer t; t.start();
  const bool ok = w.decodeFrameYUV(display, info);
  const qint64 ms = t.elapsed();
  trigger.join();
  printf("decodeFrameYUV(%d) after cancel -> %s, %lld ms\n", display,
         ok ? "ok" : "failed", static_cast<long long>(ms));

  // ms includes the trigger delay; only the latency after the flag counts.
  check(ms - triggerMs < 200, "decode returned within 200 ms of the cancel");
  check(!ok, "cancelled decode reports no frame");

  cancel = false;
  w.setCancelToken(nullptr);
  check(w.decodeFrameYUV(display, info), "decoding works again after the token is cleared");

  return failures == 0 ? 0 : 1;
}
