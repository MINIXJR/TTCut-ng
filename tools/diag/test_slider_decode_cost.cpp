// What does one slider step cost, and why does UHD lock the interface up?
//
// TODO.md, "H.265-UHD": moving the position slider leaves the application
// unresponsive for minutes. Measured on the live process at the time: 335 s of
// CPU in the GUI thread over five minutes, all 37 other threads at 0-2 ticks.
// So it computes rather than waits - but nobody has measured what a single
// step actually costs, and that number decides which fix is the right one.
//
// The path: TTCutMainWindow::onVideoSliderChanged() calls
// currentFrame->onGotoFrame() SYNCHRONOUSLY for every valueChanged the slider
// emits, and TTFFmpegWrapper::decodeFrame() always seeks (the sequential
// optimisation is deliberately off, see CLAUDE.md - identical DPB state across
// decoder instances). A drag produces dozens of those events and each one
// decodes from a keyframe.
//
// This probe measures decodeFrame() itself, in the two shapes a slider
// produces:
//   drag   - small forward steps, as while dragging the handle
//   jump   - positions far apart, as when clicking into the groove
// and reports the distribution, so "a drag of N events costs N x median" can
// be stated with a number instead of a feeling.
//
//   usage: test_slider_decode_cost <es-file> [steps] [dragStep]
//          DECODE_DEBUG=1         decode ONE frame with the wrapper's decoder
//                                 logging on (skip-loop tags, seek bytes) and
//                                 print the poc/keyframe neighbourhood - this
//                                 is what identified the EAGAIN packet drop
//          INDEX=stream|wrapper   how the frame index is obtained
//                                 (default stream - what the GUI's display path does)
//
// A third route used to exist: the quick-jump worker adopted the bare index
// list without the owner's stream metadata. On 06x03 display 3566 that route
// measured 72 675 ms against 13 ms for the routes below. It is gone — the
// index is a TTFrameIndexBundle now and cannot be handed over without its
// metadata (spec 2026-08-28-frame-index-bundle-design.md).
//
// The two index routes are NOT interchangeable and the difference dwarfs
// everything else on UHD material:
//   stream    TTH26xVideoStream::createHeaderList()/createIndexList() - the
//             native memory-mapped NALU parser - and the wrapper adopts the
//             result through provideFrameIndexTo(). This is what opening a
//             file in the GUI does.
//   wrapper   TTFrameIndexer::build(), a libav packet scan. Used by
//             the quick-jump worker (gui/ttquickjumpworker.cpp:69), the search
//             sub-decoders and the analysis wrappers whenever no built index
//             is handed to them.
//
// Build: cmake --build build --target test_slider_decode_cost
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "avstream/ttavtypes.h"
#include "avstream/tth26xvideostream.h"
#include "common/ttsettings.h"
#include "extern/ttffmpegwrapper.h"
#include "avstream/ttframeindexer.h"

namespace {

struct Stats
{
  double minMs = 0, medMs = 0, maxMs = 0, totalMs = 0;
};

Stats summarise(std::vector<double> v)
{
  Stats s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.minMs = v.front();
  s.maxMs = v.back();
  s.medMs = v[v.size() / 2];
  for (double d : v) s.totalMs += d;
  return s;
}

void report(const char* shape, const Stats& s, int n)
{
  printf("  %-6s n=%2d   min %7.1f ms   median %7.1f ms   max %7.1f ms   sum %7.1f ms\n",
         shape, n, s.minMs, s.medMs, s.maxMs, s.totalMs);
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);

  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file> [steps] [dragStep]\n", argv[0]);
    return 2;
  }
  const QString file  = QString::fromLocal8Bit(argv[1]);
  const int steps     = (argc > 2) ? QString(argv[2]).toInt() : 20;
  const int dragStep  = (argc > 3) ? QString(argv[3]).toInt() : 25;

  if (!QFileInfo::exists(file)) {
    fprintf(stderr, "no such file: %s\n", qPrintable(file));
    return 2;
  }

  const QByteArray indexMode = qgetenv("INDEX");
  const bool viaStream   = (indexMode != "wrapper");

  TTFFmpegWrapper wrapper;
  QElapsedTimer t;

  t.start();
  if (!wrapper.openFile(file)) { fprintf(stderr, "FAIL: openFile\n"); return 1; }
  const qint64 openMs = t.elapsed();

  qint64 parseMs = 0, indexMs = 0;
  TTVideoStream* stream = nullptr;
  if (viaStream) {
    TTVideoType vType(file);          // takes the path, not a QFileInfo
    stream = vType.createVideoStream();
    if (stream == nullptr) { fprintf(stderr, "FAIL: no video stream class\n"); return 1; }
    // Timed separately and printed as they finish: on UHD material one of the
    // two runs for many minutes, and a probe that prints only at the end
    // cannot say which.
    t.restart();
    const int aus = stream->createHeaderList();
    const qint64 headerMs = t.elapsed();
    printf("  createHeaderList : %lld ms (%d access units)\n", (long long)headerMs, aus);
    if (aus <= 0) { fprintf(stderr, "FAIL: createHeaderList\n"); return 1; }

    t.restart();
    const int idx = stream->createIndexList();
    const qint64 idxMs = t.elapsed();
    printf("  createIndexList  : %lld ms (%d entries)\n", (long long)idxMs, idx);
    if (idx <= 0) { fprintf(stderr, "FAIL: createIndexList\n"); return 1; }
    parseMs = headerMs + idxMs;

    t.restart();
    bool adopted = false;
    if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(stream)) {
      adopted = h26x->provideFrameIndexTo(&wrapper);
    }
    indexMs = t.elapsed();
    if (!adopted) {
      fprintf(stderr, "FAIL: the stream would not hand over its index - rerun\n"
                      "      with INDEX=wrapper to measure the scan instead\n");
      return 1;
    }
  } else {
    t.restart();
    TTFrameIndexer ix;
    if (!ix.build(file, -1, nullptr)) { fprintf(stderr, "FAIL: frame index build\n"); return 1; }
    wrapper.setFrameIndex(ix.bundle());
    indexMs = t.elapsed();
  }

  const int frames = wrapper.frameCount();
  printf("file        : %s\n", qPrintable(QFileInfo(file).fileName()));
  printf("index route : %s\n", viaStream ? "stream (native NALU parser + adopt)"
                                          : "wrapper (libav packet scan)");
  printf("frames      : %d\n", frames);
  printf("open        : %lld ms   parse: %lld ms   index: %lld ms\n",
         (long long)openMs, (long long)parseMs, (long long)indexMs);
  if (frames < steps * dragStep + 10) {
    fprintf(stderr, "FAIL: %d frames are too few for %d steps of %d\n",
            frames, steps, dragStep);
    return 1;
  }

  // The adopted index is only as good as what it carries: seekToFrame() walks
  // BACK from the target until it finds isKeyframe, and for elementary streams
  // it seeks to that frame's fileOffset. An index without keyframe flags (or
  // without offsets) degrades every decodeFrame() into "decode from byte 0" -
  // frame 4620 of a UHD stream then means 4620 4K decodes for one displayed
  // image. Print what the index actually contains before measuring anything.
  {
    const QList<TTFrameInfo>& idx = wrapper.frameIndex();
    int keyframes = 0, validOffsets = 0;
    for (const TTFrameInfo& fi : idx) {
      if (fi.isKeyframe)      keyframes++;
      if (fi.fileOffset >= 0) validOffsets++;
    }
    printf("index check : %d keyframes, %d/%d valid fileOffsets\n",
           keyframes, validOffsets, int(idx.size()));
    if (keyframes == 0 || validOffsets == 0)
      printf("  ^^ EVERY seek degrades to a decode from the start of the file.\n");
  }

  // FRAME=<n> aims every shape at one specific DISPLAY position instead of the
  // quarter mark - needed to re-measure a frame that failed in the GUI.
  const int base = qgetenv("FRAME").isEmpty()
                     ? frames / 4
                     : QString::fromLatin1(qgetenv("FRAME")).toInt();

  if (!qgetenv("DECODE_DEBUG").isEmpty()) {
    TTSettings::instance()->setLogFFmpegDecoder(true);
    printf("map around display %d:\n", base);
    for (int i = base - 3; i <= base + 3; ++i)
      printf("  display %d -> targetAU (see decode below); index[%d]: poc=%d key=%d idr=%d\n",
             i, i, wrapper.frameIndex()[i].poc,
             wrapper.frameIndex()[i].isKeyframe ? 1 : 0,
             wrapper.frameIndex()[i].isIDR ? 1 : 0);
    t.restart();
    QImage img = wrapper.decodeFrame(base);
    printf("single decode: %lld ms, %s\n", (long long)t.elapsed(),
           img.isNull() ? "NULL" : "ok");
    return 0;
  }

  // A first decode warms whatever caches exist; it is not part of either shape.
  t.restart();
  QImage warm = wrapper.decodeFrame(base);
  printf("first decode: %lld ms  (%dx%d)\n\n",
         (long long)t.elapsed(), warm.width(), warm.height());

  // --- drag: small forward steps, the handle being pulled ------------------
  std::vector<double> drag;
  for (int i = 1; i <= steps; ++i) {
    const int pos = base + i * dragStep;
    t.restart();
    QImage img = wrapper.decodeFrame(pos);
    drag.push_back(double(t.nsecsElapsed()) / 1e6);
    if (img.isNull()) printf("  WARNING: null image at %d\n", pos);
  }

  // --- jump: far apart, clicking into the groove ---------------------------
  std::vector<double> jump;
  for (int i = 1; i <= steps; ++i) {
    // Spread across the file without a random generator, so runs compare.
    const int pos = (frames / (steps + 1)) * i;
    t.restart();
    QImage img = wrapper.decodeFrame(pos);
    jump.push_back(double(t.nsecsElapsed()) / 1e6);
    if (img.isNull()) printf("  WARNING: null image at %d\n", pos);
  }

  // --- preview: the drag path - nearest keyframe, no DPB prefill ----------
  std::vector<double> preview;
  for (int i = 1; i <= steps; ++i) {
    const int pos = (frames / (steps + 1)) * i + 7;   // deliberately off-key
    int shown = -1;
    t.restart();
    QImage img = wrapper.decodeNearestKeyframe(pos, &shown);
    preview.push_back(double(t.nsecsElapsed()) / 1e6);
    if (img.isNull()) printf("  WARNING: preview null at %d\n", pos);
  }

  const Stats ds = summarise(drag);
  const Stats js = summarise(jump);
  const Stats ps = summarise(preview);
  printf("decodeFrame() cost per slider event:\n");
  report("drag", ds, steps);
  report("jump", js, steps);
  printf("decodeNearestKeyframe() - the while-dragging path:\n");
  report("prev", ps, steps);

  // The number that matters for the interface: a drag is not one event.
  printf("\nA drag emitting 50 valueChanged events costs about %.1f s of GUI\n"
         "thread time at the median above - during which the window cannot\n"
         "repaint, because onVideoSliderChanged() decodes synchronously.\n",
         50.0 * ds.medMs / 1000.0);

  wrapper.closeFile();
  delete stream;
  return 0;
}
