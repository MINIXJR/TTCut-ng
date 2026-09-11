/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Diagnostic: the three directed-search decodes - isFrameBlack() (black      */
/* frame), buildHistogram() (scene change) and decodeFrame() (logo) - must    */
/* honour the cancel token inside a running decode. TTSearchTask checks its   */
/* abort flag only between batches, so the latency of a cancel used to be one */
/* full decode per sub-decoder. Sibling of test_decode_cancel and             */
/* test_decode_cancel_yuv, same self-calibrating trigger (40 % of one         */
/* uncancelled decode of the same target).                                    */
/*                                                                            */
/* usage: test_search_cancel <es-file> <frame-index>                          */
/*        (frame-index is the raw index isFrameBlack/buildHistogram take)     */
/*----------------------------------------------------------------------------*/
#include "../../extern/ttffmpegwrapper.h"
#include "../../avstream/ttframeindexer.h"

#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

static int failures = 0;
static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

// Runs one decode kind: calibrate, cancel at 40 %, check latency and that it
// works again. `decode` returns true when a frame was produced.
static void probe(const char* name, TTFFmpegWrapper& w, const std::function<bool()>& decode)
{
  printf("--- %s\n", name);
  w.setCancelToken(nullptr);
  QElapsedTimer calib; calib.start();
  const bool calibOk = decode();
  const qint64 calibMs = calib.elapsed();
  check(calibOk, "calibration decode (uncancelled) succeeds");
  const qint64 triggerMs = qMax<qint64>(1, calibMs * 2 / 5);
  printf("    calibration %lld ms, cancel at %lld ms\n",
         static_cast<long long>(calibMs), static_cast<long long>(triggerMs));

  std::atomic<bool> cancel{false};
  w.setCancelToken(&cancel);
  std::thread trigger([&cancel, triggerMs]{
    std::this_thread::sleep_for(std::chrono::milliseconds(triggerMs));
    cancel = true;
  });
  QElapsedTimer t; t.start();
  const bool ok = decode();
  const qint64 ms = t.elapsed();
  trigger.join();
  printf("    after cancel -> %s, %lld ms\n", ok ? "frame" : "no frame", static_cast<long long>(ms));
  check(ms - triggerMs < 200, "decode returned within 200 ms of the cancel");
  check(!ok, "cancelled decode reports no frame");

  cancel = false;
  w.setCancelToken(nullptr);
  check(decode(), "decoding works again after the token is cleared");
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: %s <es-file> <frame-index>\n", argv[0]);
    return 2;
  }
  const QString file = QString::fromLocal8Bit(argv[1]);
  const int frame    = atoi(argv[2]);

  TTFFmpegWrapper owner;
  if (!owner.openFile(file)) { fprintf(stderr, "owner open failed\n"); return 2; }
  int vs = owner.findBestVideoStream();
  TTFrameIndexer ix;
  if (vs < 0 || !ix.build(file, vs, nullptr)) { fprintf(stderr, "owner index failed\n"); return 2; }
  owner.setFrameIndex(ix.bundle());

  // Same configuration as TTSearchTask::setupWorkers().
  TTFFmpegWrapper w;
  w.setAnalysisMode(true);
  w.setSearchMode(true);
  if (!w.openFile(file)) { fprintf(stderr, "open failed\n"); return 2; }
  w.setFrameIndex(owner.frameIndexBundle());

  // isFrameBlack() answers false both for "not black" and "not decoded"; a
  // cancelled call must at least not take the full decode time, and the
  // histogram/decodeFrame probes carry the "no frame" check.
  int hist[256]; int total = 0;
  probe("buildHistogram (scene change)", w, [&] {
    total = 0; return w.buildHistogram(frame, hist, total) && total > 0; });
  probe("decodeFrame (logo)", w, [&] {
    w.clearFrameCache(); return !w.decodeFrame(frame).isNull(); });

  // isFrameBlack: timing only (see above).
  {
    printf("--- isFrameBlack (black frame), timing only\n");
    w.setCancelToken(nullptr);
    QElapsedTimer calib; calib.start();
    w.isFrameBlack(frame, 16, 0.98f);
    const qint64 calibMs = calib.elapsed();
    const qint64 triggerMs = qMax<qint64>(1, calibMs * 2 / 5);
    printf("    calibration %lld ms, cancel at %lld ms\n",
           static_cast<long long>(calibMs), static_cast<long long>(triggerMs));
    std::atomic<bool> cancel{false};
    w.setCancelToken(&cancel);
    std::thread trigger([&cancel, triggerMs]{
      std::this_thread::sleep_for(std::chrono::milliseconds(triggerMs));
      cancel = true;
    });
    QElapsedTimer t; t.start();
    w.isFrameBlack(frame, 16, 0.98f);
    const qint64 ms = t.elapsed();
    trigger.join();
    printf("    after cancel -> %lld ms\n", static_cast<long long>(ms));
    check(ms - triggerMs < 200, "isFrameBlack returned within 200 ms of the cancel");
    // No "no frame" signal here, so also require an early return: well
    // under one uncancelled decode (a full run would take ~calibMs).
    check(ms < calibMs * 3 / 4, "isFrameBlack returned well before a full decode");
    w.setCancelToken(nullptr);
  }

  printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
