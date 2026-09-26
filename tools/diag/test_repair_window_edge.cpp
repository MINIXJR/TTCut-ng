// Gate: a planned audio repair may reach over the edge of a keep window
// (docs/code-map/audio-repair.md H1, user decision 2026-09-25).
//
// A repair had to lie inside exactly one keep window. A preview window
// cutting through it made the preview's audio cut fail, so the clip came
// without audio (measured: preview_001.mkv with no audio stream, the reason
// only in the log); a cut edge through it failed the cut. Now the part inside
// the window is repaired - the cutter never writes the rest.
//
// Tux H.264 + AC3 (stereo, 192 kbit/s, 768 bytes per AC3 frame), repair
// 150-170 on the left channel, preview length 2 s:
//   1. one cut 100-900: the preview's start window (4.00-5.04 s, AC3 frames
//      125-157) cuts through the repair - preview_001.mkv must have audio
//   2. an audio cut of the cut 100-125 alone: it succeeds, and exactly the
//      written frames 150..157 differ from the source
//
//   usage: test_repair_window_edge <workdir>
//
// Build via `cmake --build build --target test_repair_window_edge`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "data/ttcutpreviewtask.h"
#include "extern/ttaudiorepairitem.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) gFailures++;
}

static QString audioStreams(const QString& file)
{
  QProcess p;
  p.start("ffprobe", {"-v", "error", "-select_streams", "a", "-show_entries", "stream=codec_name",
                      "-of", "csv=p=0", file});
  p.waitForFinished(30000);
  return QString::fromLatin1(p.readAllStandardOutput()).trimmed();
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
  const QString tmp = QString::fromLocal8Bit(argv[1]) + "/tmp";
  QDir(tmp).removeRecursively();
  QDir().mkpath(tmp);
  TTSettings::instance()->setTempDirPath(tmp);
  TTSettings::instance()->setCutPreviewSeconds(2);

  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, [&]() {
    if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) mb->done(QMessageBox::Ok);
  });
  driver.start(100);

  const QString video = "/usr/local/src/TTCut-ng/tools/testdata/tux_test.264";
  const QString audio = "/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3";
  TTAVData av;
  { QEventLoop l; QObject::connect(&av, &TTAVData::threadPoolExit, &l, &QEventLoop::quit);
    QTimer::singleShot(30000, &l, &QEventLoop::quit); av.openAVStreams(video); l.exec(); }
  if (av.avCount() != 1 || av.avItemAt(0)->audioCount() != 1) { check(false, "Tux video with its AC3 opened"); return 1; }
  TTAVItem* item = av.avItemAt(0);
  const qint64 repairFrom = 150, repairTo = 170;
  item->appendAudioRepair(TTAudioRepairItem(0, repairFrom, repairTo, 0x01));

  // --- 1. preview window through the repair --------------------------------
  av.appendCutEntry(item, 100, 900);
  TTCutList job;
  job.append(item, 100, 900);
  TTCutList* preview = nullptr;
  { QEventLoop l;
    QObject::connect(&av, &TTAVData::cutPreviewFinished, [&](TTCutList* p) { preview = p; l.quit(); });
    QTimer::singleShot(90000, &l, &QEventLoop::quit); av.doCutPreview(&job); l.exec(); }
  check(preview != nullptr, "preview built");
  if (!preview) return 1;
  const QString clip = TTCutPreviewTask::createPreviewFileName(1, "mkv");
  const QString streams = audioStreams(clip);
  check(streams == "ac3", QString("preview_001.mkv (window %1-%2) has its audio (got '%3')")
                              .arg(preview->at(0).cutInIndex()).arg(preview->at(0).cutOutIndex()).arg(streams));

  // --- 2. cut edge through the repair ----------------------------------------
  TTCutList edge;
  edge.append(item, 100, 125);
  const auto keep = av.buildVideoKeepList(&edge, item->videoStream()->frameRate());
  bool ok = false;
  QString outFile;
  av.cutAudioTracks(item, {0}, keep, false,
      [&](int, const QString& ext) { return tmp + "/edge." + ext; },
      [&](int, const QString& path, const QString&, bool o) { ok = o; outFile = path; });
  check(ok, "audio cut with a cut edge through the repair succeeds");

  QFile src(audio), out(outFile);
  if (!ok || !src.open(QIODevice::ReadOnly) || !out.open(QIODevice::ReadOnly)) return 1;
  const QByteArray s = src.readAll(), o = out.readAll();
  const int fb = 768;   // 192 kbit/s AC3 at 48 kHz
  // The cut starts at the keep window's first frame (32 ms grid). Aligning by
  // content would not do: the Tux tone is a steady sine, so many AC3 frames
  // are byte-identical.
  const int first = qRound(keep.first().first / 0.032);
  check(first < repairFrom && s.mid(first * fb, fb) == o.left(fb),
        QString("output starts with source frame %1 (keep window from %2 s)").arg(first).arg(keep.first().first));
  const int written = o.size() / fb;
  int differing = 0, misplaced = 0, missed = 0;
  for (int j = 0; j < written; j++) {
    const int k = first + j;
    const bool differs = o.mid(j * fb, fb) != s.mid(k * fb, fb);
    const bool inRepair = k >= repairFrom && k <= repairTo;
    if (differs) differing++;
    if (differs && !inRepair) misplaced++;
    if (!differs && inRepair) missed++;
  }
  printf("  written source frames %d-%d, %d replaced\n", first, first + written - 1, differing);
  check(differing > 0 && misplaced == 0 && missed == 0,
        QString("exactly the written frames inside the repair are replaced (%1 replaced, %2 outside, %3 missed)")
            .arg(differing).arg(misplaced).arg(missed));

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
