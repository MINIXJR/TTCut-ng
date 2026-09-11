// Builds the H.264/H.265 playback temp MKV the way
// TTCurrentFrame::buildPlaybackMuxParams() + TTPlaybackMuxTask do (default duration from the
// stream frame rate, PAFF flag + log2_max_frame_num, display-order PTS with
// dropped slots parked behind the last real slot, first audio track), so the
// file can be played headless with mpv/ffmpeg to reproduce decoder messages.
//
//   usage: repro_playback_mkv <video.es> <out.mkv> [audio.es] [--no-displaypts] [--no-paff]
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdio>
#include <cstring>
#include "avstream/tth264videostream.h"
#include "avstream/tth265videostream.h"
#include "avstream/tth26xvideostream.h"
#include "avstream/ttdisplayordermap.h"
#include "extern/ttmkvmergeprovider.h"

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 3) { fprintf(stderr, "usage: %s <video.es> <out.mkv> [audio.es] [--no-displaypts] [--no-paff]\n", argv[0]); return 2; }
  bool useDisplayPts = true, usePaff = true;
  QStringList audio;
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--no-displaypts")) useDisplayPts = false;
    else if (!strcmp(argv[i], "--no-paff")) usePaff = false;
    else audio << QString::fromLocal8Bit(argv[i]);
  }
  QFileInfo fi(QString::fromLocal8Bit(argv[1]));
  const QString suffix = fi.suffix().toLower();
  TTH26xVideoStream* stream = nullptr;
  if (suffix == "264" || suffix == "h264") stream = new TTH264VideoStream(fi);
  else if (suffix == "265" || suffix == "h265") stream = new TTH265VideoStream(fi);
  if (!stream) { fprintf(stderr, "unsupported suffix\n"); return 2; }
  stream->createHeaderList();
  stream->createIndexList();
  const double fr = stream->frameRate();
  printf("frames=%d frameRate=%.3f isPAFF=%d log2MaxFrameNum=%d\n",
         stream->frameCount(), fr, stream->isPAFF() ? 1 : 0, stream->paffLog2MaxFrameNum());
  if (fr <= 0) { fprintf(stderr, "no frame rate\n"); return 1; }

  TTMkvMergeProvider p;
  p.setDefaultDuration("0", QString("%1ns").arg(static_cast<int>(1000000000.0 / fr)));
  if (usePaff) p.setIsPAFF(stream->isPAFF(), stream->paffLog2MaxFrameNum());
  p.setVideoCodecId(TTMkvMergeProvider::videoCodecIdFor(stream->streamType()));

  bool hasDisplayPts = false;
  const TTDisplayOrderMap& dmap = stream->displayOrderMap();
  if (useDisplayPts && dmap.isValid() && dmap.count() > 0) {
    const int n = dmap.count();
    int maxReal = -1;
    for (int i = 0; i < n; ++i) maxReal = qMax(maxReal, dmap.decodeToDisplay(i));
    if (maxReal >= 0) {
      QVector<int> order; order.reserve(n);
      int nextDropped = maxReal + 1;
      for (int i = 0; i < n; ++i) { int d = dmap.decodeToDisplay(i); order.append(d >= 0 ? d : nextDropped++); }
      p.setVideoDisplayOrder(order);
      hasDisplayPts = true;
      printf("displayMap: count=%d maxReal=%d dropped=%d\n", n, maxReal, nextDropped - maxReal - 1);
    }
  }
  printf("displayPts=%d paffFlag=%d audio=%d\n", hasDisplayPts ? 1 : 0, usePaff ? 1 : 0, (int)audio.size());
  const bool ok = p.mux(QString::fromLocal8Bit(argv[2]), stream->filePath(), audio);
  printf("mux %s\n", ok ? "OK" : "FAIL");
  if (!ok) fprintf(stderr, "error: %s\n", qPrintable(p.lastError()));
  return ok ? 0 : 1;
}
