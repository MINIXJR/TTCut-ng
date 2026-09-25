// Track changes seen from the main window (audit run 8,
// docs/code-map/track-management.md H4 and H6), against the real
// TTCutMainWindow, offscreen:
//
//   H4  An audio file added by hand is checked against the video length
//       once it has arrived. The old check ran right after queuing the open
//       and compared the previous last track. Staged: video (1:00) with a
//       2:00 track, then a 5 s file added by hand. Correct: one
//       "Length Mismatch" warning, about the 5 s file.
//   H6  The still-frame overlay shows subtitle track 0 after the tracks
//       were swapped and after the track it showed was removed.
//
//   usage: test_track_gui <workdir>
//
// Build via `cmake --build build --target test_track_gui`.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>

#include "avstream/ttavstream.h"
#include "gui/ttcurrentframe.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttsubtitletreeview.h"
#include "mpeg2window/ttmpeg2window2.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) gFailures++;
}

static void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) qApp->processEvents();
}

// Enabled and staying enabled for two seconds = loading has finished
// (criterion from test_mainwindow_then_cut).
static void waitReady(QWidget& w, int timeoutMs)
{
  QElapsedTimer t; t.start();
  qint64 since = -1;
  while (t.elapsed() < timeoutMs) {
    qApp->processEvents();
    if (w.isEnabled()) { if (since < 0) since = t.elapsed(); else if (t.elapsed() - since > 2000) return; }
    else since = -1;
  }
}

static bool writeFile(const QString& path, const QByteArray& data)
{
  QFile f(path);
  return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }

  const QString ac3Source = "/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3";
  QDir dir(QString::fromLocal8Bit(argv[1]));
  QDir().mkpath(dir.absolutePath());
  for (const QString& e : dir.entryList(QDir::Files)) dir.remove(e);
  QFile::link("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264", dir.absoluteFilePath("trk.264"));
  QFile::link(ac3Source, dir.absoluteFilePath("trk_deu.ac3"));
  writeFile(dir.absoluteFilePath("trk_a.srt"), "1\n00:00:00,000 --> 00:10:00,000\nAAA\n\n");
  writeFile(dir.absoluteFilePath("trk_b.srt"), "1\n00:00:00,000 --> 00:10:00,000\nBBB\n\n");

  // 5 s of AC3: tux_test.ac3 is 192 kbit/s, 768 bytes per 32 ms frame.
  QFile src(ac3Source);
  if (!src.open(QIODevice::ReadOnly)) { check(false, "read tux_test.ac3"); return 1; }
  const QString shortAc3 = QDir(dir.absoluteFilePath("extra")).absoluteFilePath("short.ac3");
  QDir().mkpath(dir.absoluteFilePath("extra"));
  writeFile(shortAc3, src.read(768 * 157));

  QStringList boxes;
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, [&]() {
    if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      boxes << mb->windowTitle() + ": " + mb->text().replace('\n', ' ');
      mb->done(QMessageBox::Ok);
    }
  });
  driver.start(100);

  TTCutMainWindow window;
  window.show();
  pump(300);
  window.onReadVideoStream(dir.absoluteFilePath("trk.264"));
  waitReady(window, 30000);
  printf("  boxes while opening: %s\n", qPrintable(boxes.isEmpty() ? QString("(none)") : boxes.join(" | ")));
  boxes.clear();

  // --- H6 ------------------------------------------------------------------
  auto* subtitleView = window.findChild<TTSubtitleTreeView*>("subtitleFileList");
  auto* subtitleTree = window.findChild<QTreeWidget*>("subtitleListView");
  auto* frame        = window.findChild<TTCurrentFrame*>("currentFrame");
  if (!subtitleView || !subtitleTree || !frame) { check(false, "H6: widgets found"); return 1; }
  check(subtitleTree->topLevelItemCount() == 2, "H6: two subtitles listed");
  if (subtitleTree->topLevelItemCount() != 2) return 1;

  auto overlayFile = [&]() {
    TTSubtitleStream* s = frame->videoWindow()->subtitleStream();
    return s ? QFileInfo(s->filePath()).fileName() : QString("(none)");
  };
  auto row0 = [&]() {
    return subtitleTree->topLevelItemCount() ? QFileInfo(subtitleTree->topLevelItem(0)->text(0)).fileName()
                                             : QString("(none)");
  };
  printf("  start : row0=%s overlay=%s\n", qPrintable(row0()), qPrintable(overlayFile()));
  check(overlayFile() == row0(), "H6: overlay shows track 0 after loading");

  emit subtitleView->swapItems(0, 1);
  pump(300);
  printf("  swap  : row0=%s overlay=%s\n", qPrintable(row0()), qPrintable(overlayFile()));
  check(overlayFile() == row0(), "H6: overlay follows track 0 after a swap");

  // Remove the track the overlay showed before the swap (now row 1).
  emit subtitleView->removeItem(1);
  pump(300);
  printf("  remove: row0=%s overlay=%s\n", qPrintable(row0()), qPrintable(overlayFile()));
  check(overlayFile() == row0(), "H6: overlay follows track 0 after the shown track was removed");

  emit subtitleView->removeItem(0);
  pump(300);
  printf("  empty : overlay=%s\n", qPrintable(overlayFile()));
  check(overlayFile() == "(none)", "H6: no overlay once the last subtitle is removed");

  // --- H4 ------------------------------------------------------------------
  window.onReadAudioStream(shortAc3);
  waitReady(window, 20000);
  pump(500);
  printf("  boxes after adding short.ac3: %s\n", qPrintable(boxes.isEmpty() ? QString("(none)") : boxes.join(" | ")));
  check(boxes.size() == 1, QString("H4: exactly one warning (got %1)").arg(boxes.size()));
  // TTCut measures the 157 frames as 00:00:04.992; the 2:00 track already
  // loaded would read 00:02:00.128.
  check(boxes.size() == 1 && boxes[0].contains("Length Mismatch") &&
        QRegularExpression("Audio: 00:00:0\\d").match(boxes[0]).hasMatch(),
        "H4: the warning is about the 5 s file, not the 2:00 track");

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
