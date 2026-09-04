// Equivalence probe for the three directed searches (black frame, logo,
// scene change) that share TTSearchTask::runDirectedSearch(). Runs each
// search synchronously in both directions from a start frame, plus one
// scene-change search that aborts itself on the first progress report, and
// prints the found position, the abort flag and the number of progress
// reports. The output has no timing in it, so two builds can be diffed.
//
//   usage: test_directed_search <es-file> [startPos] [blackRatio] [sceneThreshold] [logoThreshold]
//
// The logo profile is built from the start frame's top-right corner with a
// manual ROI (H.264/H.265 only; MPEG-2 skips the logo search).
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QRect>
#include <QString>

#include "avstream/ttmpeg2videostream.h"
#include "avstream/tth264videostream.h"
#include "avstream/tth265videostream.h"
#include "avstream/tth26xvideostream.h"
#include "avstream/ttvideoindexlist.h"
#include "data/ttlogodetector.h"
#include "data/ttsearchtask_blackframe.h"
#include "data/ttsearchtask_logo.h"
#include "data/ttsearchtask_scenechange.h"
#include "extern/ttffmpegwrapper.h"
#include "avstream/ttframeindexer.h"

#include <cstdio>

namespace {

TTVideoStream* makeStream(const QFileInfo& fi)
{
  const QString suffix = fi.suffix().toLower();
  if (suffix == "m2v" || suffix == "mpv" || suffix == "mpeg2")
    return new TTMpeg2VideoStream(fi);
  if (suffix == "264" || suffix == "h264" || suffix == "avc")
    return new TTH264VideoStream(fi);
  if (suffix == "265" || suffix == "h265" || suffix == "hevc")
    return new TTH265VideoStream(fi);
  return nullptr;
}

// Runs one search task to completion and prints its outcome. abortOnProgress
// requests a user abort from inside the first progress report.
void runOne(const char* label, TTSearchTask* task, bool abortOnProgress)
{
  int foundPos = -2;
  bool aborted = false;
  int progressCount = 0;

  QObject::connect(task, &TTSearchTask::progress, [&](int) {
    progressCount++;
    if (abortOnProgress && progressCount == 1) task->onUserAbort();
  });
  QObject::connect(task, &TTSearchTask::found, [&](int pos, bool wasAborted) {
    foundPos = pos;
    aborted  = wasAborted;
  });

  task->runSynchron();
  printf("%-22s found=%d aborted=%d progress=%d\n", label, foundPos, aborted ? 1 : 0, progressCount);
  delete task;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);

  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file> [startPos] [blackRatio] [sceneThreshold] [logoThreshold]\n", argv[0]);
    return 2;
  }

  QFileInfo fi(QString::fromLocal8Bit(argv[1]));
  TTVideoStream* stream = fi.exists() ? makeStream(fi) : nullptr;
  if (stream == nullptr) {
    fprintf(stderr, "cannot open %s\n", qPrintable(fi.absoluteFilePath()));
    return 2;
  }

  stream->createHeaderList();
  stream->createIndexList();
  const int frames = stream->frameCount();
  if (stream->indexList() == nullptr || frames <= 0) {
    fprintf(stderr, "index list empty\n");
    return 1;
  }

  const int   startPos   = (argc > 2) ? QString(argv[2]).toInt()   : frames / 2;
  const float blackRatio = (argc > 3) ? QString(argv[3]).toFloat() : 0.98f;
  const float sceneThr   = (argc > 4) ? QString(argv[4]).toFloat() : 0.5f;
  const float logoThr    = (argc > 5) ? QString(argv[5]).toFloat() : 0.5f;

  TTFrameIndexBundle bundle;
  if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(stream))
    bundle = h26x->frameIndexBundle();

  printf("file=%s frames=%d start=%d\n", qPrintable(fi.fileName()), frames, startPos);

  const QString path = stream->filePath();
  const TTAVTypes::AVStreamType type = stream->streamType();
  TTVideoIndexList*  idx = stream->indexList();
  TTVideoHeaderList* hdr = stream->headerList();

  for (int dir : {1, -1}) {
    runOne(dir > 0 ? "black forward" : "black backward",
           new TTBlackFrameSearchTask(path, type, idx, hdr, startPos, dir, frames, blackRatio, bundle), false);
    runOne(dir > 0 ? "scene forward" : "scene backward",
           new TTSceneChangeSearchTask(path, type, idx, hdr, startPos, dir, frames, sceneThr, bundle), false);
  }
  runOne("scene abort",
         new TTSceneChangeSearchTask(path, type, idx, hdr, 0, 1, frames, 2.0f, bundle), true);

  if (type == TTAVTypes::mpeg2_demuxed_video) {
    printf("logo: skipped for MPEG-2\n");
  } else {
    TTFFmpegWrapper wrapper;
    wrapper.setAnalysisMode(true);
    QImage frame;
    if (wrapper.openFile(path)) {
      if (bundle.isEmpty()) {
        TTFrameIndexer ix;
        if (ix.build(path, -1, nullptr)) wrapper.setFrameIndex(ix.bundle());
      } else {
        wrapper.setFrameIndex(bundle);
      }
      frame = wrapper.decodeFrame(startPos);
    }
    if (frame.isNull()) {
      printf("logo: skipped (start frame not decodable)\n");
    } else {
      TTLogoDetector detector;
      detector.setROI(QRect(frame.width() - 240, 20, 200, 120));
      detector.addEdgeSample(frame);
      detector.finalizeProfile();
      for (int dir : {1, -1}) {
        runOne(dir > 0 ? "logo forward" : "logo backward",
               new TTLogoSearchTask(path, type, idx, hdr, startPos, dir, frames, &detector, logoThr, bundle), false);
      }
    }
  }

  delete stream;
  return 0;
}
