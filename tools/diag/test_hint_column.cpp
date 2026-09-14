// Gate for the cut list's hint column (column 5) on AC3 format changes.
//
// The column carries two producers: burstHint() (a warning icon) and
// acmodHint() (an info icon), joined as "<burst> + <acmod>" when both speak.
// acmodHint compares the AC3 channel mode at the cut-in and at the cut-out
// against the majority mode inside the range, which is why a range needs to
// straddle a format change to say anything at all.
//
// This gate covers what a visual check was meant to cover: "AC3 start" when
// only the beginning differs, "AC3 end" when only the end does, "AC3
// start+end" when both do, and an empty cell for a range that lies inside
// one format. The tooltip has to name the two modes.
//
// The audio is the same synthetic file gate_acmod_majority builds: 8 s
// stereo (2/0), 4 s 5.1 (3/2), 8 s stereo. Video is any tux H.264 fixture -
// only its frame rate matters, since the hint converts frame positions to
// seconds with it.
//
//   usage: test_hint_column <video.264> <mixed.ac3> <workdir>
// Build via `cmake --build build --target test_hint_column`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

#include "avstream/ttavstream.h"
#include "data/ttavdata.h"
#include "gui/ttcuttreeview.h"

namespace {

int failures = 0;
void check(bool ok, const char* what, const QString& detail = QString())
{
  printf("%s: %s%s\n", ok ? "PASS" : "FAIL", what,
         detail.isEmpty() ? "" : qPrintable("  (" + detail + ")"));
  if (!ok) failures++;
}

// The tree's column-5 text of row i.
QString hintOf(QTreeWidget* tree, int row)
{
  QTreeWidgetItem* it = tree->topLevelItem(row);
  return it ? it->text(5) : QStringLiteral("<no row>");
}
QString tipOf(QTreeWidget* tree, int row)
{
  QTreeWidgetItem* it = tree->topLevelItem(row);
  return it ? it->toolTip(5) : QString();
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 4) {
    fprintf(stderr, "usage: %s <video.264> <mixed.ac3> <workdir>\n", argv[0]);
    return 2;
  }
  // Absolute: a relative <Name> in a project file resolves against the
  // project's directory, not the current one.
  const QString video = QFileInfo(QString::fromUtf8(argv[1])).absoluteFilePath();
  const QString audio = QFileInfo(QString::fromUtf8(argv[2])).absoluteFilePath();
  const QDir    work(QString::fromUtf8(argv[3]));
  QDir().mkpath(work.absolutePath());

  const QString prj = work.absoluteFilePath("hint.ttcut");
  {
    QFile f(prj);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    f.write(QString("<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
                    " <Video>\n  <Order>0</Order>\n  <Name>%1</Name>\n"
                    "  <Audio>\n   <Order>0</Order>\n   <Name>%2</Name>\n   <Language>deu</Language>\n  </Audio>\n"
                    " </Video>\n</TTCut-Projectfile>\n").arg(video, audio).toUtf8());
  }

  TTAVData avData;
  avData.setNonInteractive(true);
  QEventLoop loop;
  bool loaded = false;
  QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                   [&](const QString&) { loaded = true; loop.quit(); });
  QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                   [&]() { loop.quit(); });
  QTimer::singleShot(120000, &loop, &QEventLoop::quit);
  avData.readProjectFile(QFileInfo(prj));
  if (!loaded) loop.exec();
  check(loaded && avData.avCount() == 1, "project with video and AC3 loaded");
  if (!loaded || avData.avCount() == 0) {
    printf("HINT-COLUMN FAIL\n");
    return 1;
  }

  TTAVItem* item = avData.avItemAt(0);
  TTVideoStream* vs = item->videoStream();
  const double fr = vs ? vs->frameRate() : 0.0;
  check(fr > 0, "video frame rate known", QString::number(fr));
  if (fr <= 0) { printf("HINT-COLUMN FAIL\n"); return 1; }

  TTCutTreeView view(nullptr);
  view.setAVData(&avData);
  QTreeWidget* tree = view.findChild<QTreeWidget*>();
  check(tree != nullptr, "the view has its tree widget");
  if (!tree) { printf("HINT-COLUMN FAIL\n"); return 1; }

  // Ranges in seconds against the 8 s stereo / 4 s 5.1 / 8 s stereo file.
  auto f = [fr](double sec) { return int(sec * fr); };
  item->appendCutEntry(f(1),   f(5));    // 0: inside the first stereo block
  item->appendCutEntry(f(7),   f(11));   // 1: starts stereo, majority 5.1
  item->appendCutEntry(f(9),   f(13));   // 2: majority 5.1, ends stereo
  item->appendCutEntry(f(7.5), f(12.5)); // 3: both ends stereo, majority 5.1
  app.processEvents();

  check(tree->topLevelItemCount() == 4, "four rows in the cut list",
        QString::number(tree->topLevelItemCount()));

  check(hintOf(tree, 0).isEmpty(), "no hint for a range inside one format",
        hintOf(tree, 0));
  check(hintOf(tree, 1) == QStringLiteral("AC3 start"),
        "a range that starts in the other format says 'AC3 start'", hintOf(tree, 1));
  check(hintOf(tree, 2) == QStringLiteral("AC3 end"),
        "a range that ends in the other format says 'AC3 end'", hintOf(tree, 2));
  check(hintOf(tree, 3) == QStringLiteral("AC3 start+end"),
        "a range differing at both ends says 'AC3 start+end'", hintOf(tree, 3));

  check(tipOf(tree, 1).contains("2/0") && tipOf(tree, 1).contains("3/2"),
        "the tooltip names both channel modes", tipOf(tree, 1).replace('\n', " | "));

  printf("%s\n", failures ? "HINT-COLUMN FAIL" : "HINT-COLUMN PASS");
  return failures ? 1 : 0;
}
