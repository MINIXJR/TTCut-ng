// Compares ttProbeVideo() with what TTFFmpegWrapper reports for the same file:
// codec type, best video stream index and the TTStreamInfo fields the stream
// classes read (width, height, frameRate, bitRate, profile, level, codecId).
//   usage: test_probe_video <file>
#include <QCoreApplication>
#include <QString>
#include <cstdio>

#include "avstream/ttavutil.h"
#include "extern/ttffmpegwrapper.h"

static int failures = 0;
static void expectEq(const char* name, long long got, long long expected)
{
  if (got == expected) { printf("PASS  %s = %lld\n", name, got); return; }
  failures++;
  printf("FAIL  %s: got %lld, expected %lld\n", name, got, expected);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <file>\n", argv[0]); return 2; }
  const QString path = QString::fromLocal8Bit(argv[1]);

  TTVideoProbe p; QString err;
  const bool probed = ttProbeVideo(path, &p, &err);

  TTFFmpegWrapper w;
  const bool opened = w.openFile(path);
  expectEq("open result", probed ? 1 : 0, opened ? 1 : 0);
  if (!opened) { printf("wrapper: %s\nprobe: %s\n", qPrintable(w.lastError()), qPrintable(err)); return failures ? 1 : 0; }

  const int vs = w.findBestVideoStream();
  expectEq("videoStreamIndex", p.videoStreamIndex, vs);
  expectEq("codecType", p.codecType, w.detectVideoCodec());
  const TTStreamInfo si = w.getStreamInfo(vs);
  expectEq("codecId",   p.info.codecId, si.codecId);
  expectEq("width",     p.info.width,   si.width);
  expectEq("height",    p.info.height,  si.height);
  expectEq("frameRate*1000", (long long)(p.info.frameRate * 1000), (long long)(si.frameRate * 1000));
  expectEq("bitRate",   p.info.bitRate, si.bitRate);
  expectEq("profile",   p.info.profile, si.profile);
  expectEq("level",     p.info.level,   si.level);
  printf("codec=%s\n", qPrintable(ttCodecTypeToString(p.codecType)));
  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURES\n", failures);
  return failures ? 1 : 0;
}
