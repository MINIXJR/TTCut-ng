// Prints the frame rate TTCut-ng reads from an MPEG-2 elementary stream.
//
// TTSequenceHeader::frameRateValue() knew only frame_rate_code 2, 3 and 5
// (24, 25, 30 fps) until 2026-09-24; every other code - 23.976, 29.97, 50,
// 59.94, 60 - came back as 25 fps (29.97 even without a log line), and the
// whole timeline ran on it: a 720p50 cut of 10 s became a 20 s MKV at half
// speed with the audio cut from the wrong place.
//
// Opens the stream the way TTOpenVideoTask does and prints
// "frameRate=<value>" for gate_mpeg2_framerate.sh to compare.
//
//   usage: test_mpeg2_framerate <file.m2v>
#include <QCoreApplication>
#include <cstdio>

#include "avstream/ttavstream.h"
#include "avstream/ttavtypes.h"

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <file.m2v>\n", argv[0]); return 2; }

  TTVideoType    vType(QString::fromUtf8(argv[1]));
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr)               { fprintf(stderr, "no video stream\n"); return 1; }
  if (vStream->createHeaderList() <= 0) { fprintf(stderr, "createHeaderList failed\n"); return 1; }

  printf("frameRate=%.6f\n", double(vStream->frameRate()));
  return 0;
}
