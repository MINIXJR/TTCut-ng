// Gate for the cut list's drift column after a cut preview (audit run 9,
// docs/code-map/cut-preview.md H2), against the real TTCutMainWindow,
// offscreen.
//
// The column shows the cumulative A/V drift after each cut over ALL cuts of
// the project (user decision 2026-09-25). A preview of one cut with its
// neighbours computed the drift over those three cuts only and wrote the
// values into rows 0..2 - measured: row 3's preview filled rows 0-2, rows 3
// and 4 stayed empty.
//
// Five cuts on the Tux H.264 + AC3 fixture. Correct: after previewing row 2
// with its neighbours every row carries a value, and each equals the value
// after a preview of all rows.
//
//   usage: test_preview_drift_rows <workdir>
//
// Build via `cmake --build build --target test_preview_drift_rows`.
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>

#include "common/ttsettings.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttcuttreeview.h"

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

// Enabled and staying enabled for two seconds = the operation has finished
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

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
  QDir dir(QString::fromLocal8Bit(argv[1]));
  QDir().mkpath(dir.absoluteFilePath("tmp"));
  for (const QString& e : dir.entryList(QDir::Files)) dir.remove(e);
  QFile::link("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264", dir.absoluteFilePath("trk.264"));
  QFile::link("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3", dir.absoluteFilePath("trk_deu.ac3"));

  const QString project = dir.absoluteFilePath("five.ttcut");
  {
    QFile f(project);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { check(false, "write project"); return 1; }
    QTextStream out(&f);
    out << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n"
        << "  <Order>0</Order>\n  <Name>" << dir.absoluteFilePath("trk.264") << "</Name>\n"
        << "  <Audio><Order>0</Order><Name>" << dir.absoluteFilePath("trk_deu.ac3") << "</Name></Audio>\n";
    const int cuts[5][2] = {{0, 100}, {200, 300}, {400, 500}, {600, 700}, {800, 900}};
    for (int i = 0; i < 5; ++i)
      out << "  <Cut><Order>" << i << "</Order><CutIn>" << cuts[i][0] << "</CutIn><CutOut>"
          << cuts[i][1] << "</CutOut></Cut>\n";
    out << " </Video>\n</TTCut-Projectfile>\n";
  }

  // Close the preview dialog as soon as it is up.
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, [&]() {
    if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dlg->reject();
  });
  driver.start(200);

  TTCutMainWindow window;
  window.show();
  pump(300);
  TTSettings::instance()->setTempDirPath(dir.absoluteFilePath("tmp"));
  TTSettings::instance()->setCutPreviewSeconds(2);
  window.openProjectFile(project);
  waitReady(window, 30000);

  auto* view = window.findChild<TTCutTreeView*>("cutList");
  auto* tree = window.findChild<QTreeWidget*>("videoCutList");
  if (!view || !tree) { check(false, "cut list widgets found"); return 1; }
  check(tree->topLevelItemCount() == 5, QString("five cuts loaded (got %1)").arg(tree->topLevelItemCount()));
  if (tree->topLevelItemCount() != 5) return 1;

  auto column = [&]() {
    QStringList v;
    for (int i = 0; i < tree->topLevelItemCount(); i++) v << tree->topLevelItem(i)->text(4);
    return v;
  };
  auto previewSelection = [&](const QList<int>& rows) {
    tree->clearSelection();
    tree->setCurrentItem(tree->topLevelItem(rows.first()));
    for (int r : rows) tree->topLevelItem(r)->setSelected(true);
    view->onEntryPreview();
    waitReady(window, 60000);
    pump(500);
    return column();
  };

  const QStringList neighbours = previewSelection({2});
  printf("  row 2 with neighbours: %s\n", qPrintable(neighbours.join(" | ")));
  const QStringList all = previewSelection({0, 1, 2, 3, 4});
  printf("  all rows:              %s\n", qPrintable(all.join(" | ")));

  bool filled = true;
  for (const QString& v : neighbours) filled &= v.endsWith(" ms");
  check(filled, "H2: every row has a drift value after a neighbour preview");
  check(neighbours == all, "H2: each row's value equals the value of a preview of all cuts");

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
