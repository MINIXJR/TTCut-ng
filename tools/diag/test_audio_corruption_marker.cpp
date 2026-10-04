// Gate for the "Audio data error" markers built from a .info file
// (audio_N_corrupt_ranges, reported by ttcut-audiofix at demux time).
//
// The marker has to name the damaged track in a way the user can find in the
// audio list. A track number cannot do that: the .info lists the tracks in
// demux order, the audio list sorts AC3 first and can be reordered by hand.
// With MP2 before AC3 in the .info, "track 2" meant the AC3 track while row 2
// of the list was the MP2 track. The marker therefore carries the file name,
// which is what the list shows in its first column.
//
//   test_audio_corruption_marker                      the gate (mini .info)
//   test_audio_corruption_marker <file.info> [x.qm]    print the markers of a
//                                                      real .info, translated
//                                                      when a .qm is given
// Build via `cmake --build build --target test_audio_corruption_marker`.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTranslator>
#include <cstdio>

#include "avstream/ttesinfo.h"
#include "data/ttavdata.h"

namespace {

int failures = 0;
void check(bool ok, const char* what, const QString& detail = QString())
{
  printf("%s: %s%s\n", ok ? "PASS" : "FAIL", what,
         detail.isEmpty() ? "" : qPrintable("  (" + detail + ")"));
  if (!ok) failures++;
}

}  // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc > 1) {
    QTranslator translator;
    if (argc > 2 && translator.load(argv[2])) app.installTranslator(&translator);
    TTESInfo real;
    if (!real.load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    const QList<TTStreamPoint> markers = TTAVData::audioCorruptionPoints(real, 0);
    for (const TTStreamPoint& p : markers)
      printf("frame %d: %s\n", p.frameIndex(), qPrintable(p.description()));
    printf("%d marker(s)\n", int(markers.size()));
    return 0;
  }

  const QString dir = "/usr/local/src/CLAUDE_TMP/TTCut-ng/audio_corruption_marker";
  QDir().mkpath(dir);
  const QString path = dir + "/mini.info";

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    fprintf(stderr, "cannot write %s\n", qPrintable(path));
    return 1;
  }
  QTextStream out(&f);
  out <<
    "# TTCut Elementary Stream Info File\n"
    "\n"
    "[audio]\n"
    "count=2\n"
    "audio_0_file=rec_deu.mp2\n"
    "audio_0_codec=mp2\n"
    "audio_1_file=rec_deu.ac3\n"
    "audio_1_codec=ac3\n"
    "audio_1_corrupt_ranges=151322-151322,160000-160040\n";
  out.flush();
  f.close();

  TTESInfo info;
  check(info.load(path), "the .info loads");

  int frames = 0;
  const QList<TTStreamPoint> points = TTAVData::audioCorruptionPoints(info, 50, &frames);
  check(points.size() == 2, "one marker per range", QString::number(points.size()));
  check(frames == 42, "frames counted over both ranges", QString::number(frames));
  if (points.size() == 2) {
    const QString desc = points[0].description();
    check(points[0].frameIndex() == 151272, "the marker stands the offset before the range",
          QString::number(points[0].frameIndex()));
    check(points[0].type() == StreamPointType::Error, "the marker is an error marker");
    check(desc.contains("151322"), "the text names the range", desc);
    // The range can be removed junk or a frame with a bad checksum, and the
    // .info does not say which - the name must not promise an audible defect.
    check(desc.startsWith("Audio data error: "), "the text calls it a data error", desc);
    check(desc.contains("rec_deu.ac3"), "the text names the damaged track by its file", desc);
    check(!desc.contains("rec_deu.mp2"), "the text does not name the intact track", desc);
    check(!desc.contains("track", Qt::CaseInsensitive), "the text carries no track number", desc);
    check(points[1].description().contains("160000") && points[1].description().contains("160040") &&
          points[1].description().contains("rec_deu.ac3"),
          "the second range names the same file", points[1].description());
  }

  printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
  return failures == 0 ? 0 : 1;
}
