// Dumps every value TTESInfo exposes for one .info file, one field per
// line, so the parser can be diffed between two builds.
//
//   usage: test_esinfo_dump <file.info>
#include <QCoreApplication>
#include <QString>

#include "avstream/ttesinfo.h"

#include <cstdio>

static void range(const char* tag, const TTESRange& r)
{
  printf("%s %d-%d ms=%d\n", tag, r.start, r.end, r.ms);
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <file.info>\n", argv[0]); return 2; }

  TTESInfo info;
  const bool ok = info.load(QString::fromLocal8Bit(argv[1]));
  printf("loaded=%d error=\"%s\"\n", ok ? 1 : 0, qPrintable(info.lastError()));
  printf("video file=\"%s\" codec=\"%s\" %dx%d fps=%d/%d (%.5f) start_pts=%.3f filler=%d saved=%lld\n",
         qPrintable(info.videoFile()), qPrintable(info.videoCodec()), info.videoWidth(), info.videoHeight(),
         info.frameRateNum(), info.frameRateDen(), info.frameRate(), info.startPts(),
         info.fillerStripped() ? 1 : 0, static_cast<long long>(info.fillerSavedBytes()));
  printf("audio tracks=%d\n", info.audioTrackCount());
  for (int i = 0; i < info.audioTrackCount(); ++i) {
    const TTAudioTrackInfo t = info.audioTrack(i);
    printf("audio[%d] file=\"%s\" codec=\"%s\" lang=\"%s\" first_pts=%.3f trimmed=%d silence=%d removed=%d corrupt=%d\n",
           i, qPrintable(t.file), qPrintable(t.codec), qPrintable(t.language), t.firstPts,
           t.trimmedMs, t.silenceMs, t.removedMs, static_cast<int>(t.corruptRanges.size()));
    for (const TTESRange& r : t.corruptRanges) range("  audio_corrupt", r);
    printf("  silenceMs()=%d removedMs()=%d\n", info.audioSilenceMs(i), info.audioRemovedMs(i));
  }
  printf("markers=%d has=%d\n", info.markerCount(), info.hasMarkers() ? 1 : 0);
  for (const TTMarkerInfo& m : info.markers())
    printf("  marker ts=\"%s\" frame=%d type=\"%s\" verified=%d ms=%d frame@25=%d\n",
           qPrintable(m.timestamp), m.frame, qPrintable(m.type), m.verified ? 1 : 0,
           m.toMilliseconds(), m.toFrame(25.0));
  printf("timing has=%d first_video=%.3f first_audio=%.3f av_offset=%d\n",
         info.hasTimingInfo() ? 1 : 0, info.firstVideoPts(), info.firstAudioPts(), info.avOffsetMs());
  printf("warnings has=%d decode_errors=%d recommend_projectx=%d regions=%d\n",
         info.hasWarnings() ? 1 : 0, info.decodeErrors(), info.recommendProjectX() ? 1 : 0,
         static_cast<int>(info.decodeErrorRegions().size()));
  for (const TTDecodeErrorRegion& r : info.decodeErrorRegions())
    printf("  region frame=%d time=\"%s\" count=%d\n", r.frame, qPrintable(r.time), r.errorCount);
  printf("es_total_aus=%d doubled=%d gap_frames=%d\n", info.esTotalAus(),
         static_cast<int>(info.esDoubledPtsAus().size()), info.audioGapFrameCount());
  for (int v : info.esDoubledPtsAus()) printf("  doubled %d\n", v);
  for (int v : info.audioGapFrames()) printf("  gap %d\n", v);
  for (const TTESRange& r : info.esMissingRanges()) range("missing", r);
  for (const TTESRange& r : info.corruptFrameRanges()) range("corrupt", r);
  printf("source=\"%s\"\n", qPrintable(info.sourceFile()));
  return 0;
}
