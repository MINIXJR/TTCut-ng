// Is an I-frame shown by the fast keyframe decode the same picture as the
// exact decode?
//
// With FastSlider on, the slider and the mouse wheel only go to I-frames and
// show them with TTFFmpegWrapper::decodeNearestKeyframe(), which seeks to the
// keyframe itself without the DPB prefill from the previous GOP. The exact
// path (decodeFrame) prefills. For the I-frame itself that must not matter -
// it is intra-coded - but the window then shows what a cut would see only if
// both pictures are identical, so it is checked rather than assumed
// (spec 2026-10-17-slider-drag-preview-design.md, addendum 2026-10-18 (2)).
//
// Two wrappers adopt the stream's own index (the GUI's route), each with its
// own frame cache, so neither hands the other a cached picture. About 20
// I-frames spread over the stream are decoded both ways.
//
//   usage: test_keyframe_preview_identity <es-file> [count]
//   exit:  0 identical, 1 a picture differs or a decode failed, 2 usage
//
// Build via `cmake --build build --target test_keyframe_preview_identity`.
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>

#include <cstdio>
#include <vector>

#include "avstream/ttavtypes.h"
#include "avstream/tth26xvideostream.h"
#include "avstream/ttvideoindexlist.h"
#include "extern/ttffmpegwrapper.h"

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file> [count]\n", argv[0]);
    return 2;
  }
  const QString file = QString::fromLocal8Bit(argv[1]);
  const int count    = (argc > 2) ? QString(argv[2]).toInt() : 20;
  if (!QFileInfo::exists(file)) { fprintf(stderr, "no such file: %s\n", qPrintable(file)); return 2; }

  TTVideoType vType(file);
  TTVideoStream* stream = vType.createVideoStream();
  auto* h26x = dynamic_cast<TTH26xVideoStream*>(stream);
  if (h26x == nullptr) { fprintf(stderr, "FAIL: not an H.264/H.265 stream\n"); return 1; }
  if (stream->createHeaderList() <= 0 || stream->createIndexList() <= 0) {
    fprintf(stderr, "FAIL: header/index list\n");
    return 1;
  }
  // As TTOpenVideoTask does: the list is built in stream order, the GUI
  // navigates it in display order.
  stream->indexList()->sortDisplayOrder();
  const TTFrameIndexBundle bundle = h26x->frameIndexBundle();
  if (bundle.isEmpty()) { fprintf(stderr, "FAIL: no frame index bundle\n"); return 1; }

  TTFFmpegWrapper fast, exact;
  if (!fast.openFile(file) || !exact.openFile(file)) { fprintf(stderr, "FAIL: openFile\n"); return 1; }
  fast.setFrameIndex(bundle);
  exact.setFrameIndex(bundle);

  // I-frames as the slider and the wheel find them: the index list, type 1.
  TTVideoIndexList* index = stream->indexList();
  std::vector<int> iframes;
  for (int p = index->moveToIndexPos(0, 1); p >= 0; p = index->moveToNextIndexPos(p, 1))
    iframes.push_back(p);
  if (iframes.empty()) { fprintf(stderr, "FAIL: no I-frames in the index list\n"); return 1; }

  std::vector<int> picks;
  const int n = qMin(count, int(iframes.size()));
  for (int k = 0; k < n; ++k) picks.push_back(iframes[size_t(k) * iframes.size() / size_t(n)]);

  printf("file    : %s\n", qPrintable(QFileInfo(file).fileName()));
  printf("frames  : %d, I-frames %zu, checked %zu\n", stream->frameCount(), iframes.size(), picks.size());
  int bad = 0;
  for (int pos : picks) {
    int shown = -1;
    const QImage a = fast.decodeNearestKeyframe(pos, &shown);
    const QImage b = exact.decodeFrame(pos);
    const char* what = nullptr;
    if (a.isNull() || b.isNull()) what = "decode failed";
    else if (shown != pos)        what = "fast decode shows another position";
    else if (a != b)              what = "pictures differ";
    if (what) {
      ++bad;
      printf("  I-frame %6d: %s (shown %d, fast %s, exact %s)\n", pos, what, shown,
             a.isNull() ? "null" : "ok", b.isNull() ? "null" : "ok");
    }
  }
  delete stream;
  if (bad) { printf("FAIL: %d of %zu I-frames\n", bad, picks.size()); return 1; }
  printf("PASS: %zu I-frames identical\n", picks.size());
  return 0;
}
