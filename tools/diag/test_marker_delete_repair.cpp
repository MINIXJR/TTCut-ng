// Gate: deleting an audio-anomaly marker takes its planned repair along,
// after asking (docs/code-map/audio-repair.md H2, user decision 2026-09-25).
//
// The marker is display only, but it is the only handle on its repair.
// Deleting it left the repair invisible, still saved and still applied -
// measured: 0 markers after the delete, the saved project still with one
// <Repair>. In the real TTCutMainWindow, offscreen, on Tux H.264 + AC3:
//
//   1. Delete, answered No: marker and repair stay
//   2. Delete, answered Yes: both go (saved project: no <Repair>)
//   3. a hand-placed marker is deleted without a question
//   4. Delete all with a repair behind one marker, Yes: all markers and the
//      repair go
//
//   usage: test_marker_delete_repair <workdir>
//
// Build via `cmake --build build --target test_marker_delete_repair`.
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "common/ttsettings.h"
#include "data/ttstreampointmodel.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttstreampointwidget.h"

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

static int countIn(const QString& file, const QString& tag)
{
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) return -1;
  return QString::fromUtf8(f.readAll()).count(tag);
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
  QDir dir(QString::fromLocal8Bit(argv[1]));
  dir.removeRecursively();
  QDir().mkpath(dir.absoluteFilePath("tmp"));
  QFile::link("/usr/local/src/TTCut-ng/tools/testdata/tux_test.264", dir.absoluteFilePath("rep.264"));
  QFile::link("/usr/local/src/TTCut-ng/tools/testdata/tux_test.ac3", dir.absoluteFilePath("rep.ac3"));

  const QString project = dir.absoluteFilePath("rep.ttcut");
  {
    QFile f(project);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { check(false, "write project"); return 1; }
    QTextStream o(&f);
    o << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n  <Order>0</Order>\n"
      << "  <Name>" << dir.absoluteFilePath("rep.264") << "</Name>\n"
      << "  <Audio><Order>0</Order><Name>" << dir.absoluteFilePath("rep.ac3") << "</Name>\n"
      << "   <Repair><FrameFrom>130</FrameFrom><FrameTo>140</FrameTo><Channels>1</Channels>"
         "<Method>silence-fade</Method></Repair>\n"
      << "  </Audio>\n  <Cut><Order>0</Order><CutIn>100</CutIn><CutOut>900</CutOut></Cut>\n </Video>\n"
      << " <StreamPoint><Frame>104</Frame><Type>AudioAnomaly</Type><Description>anomaly (repair planned)"
         "</Description><Confidence>0.90</Confidence><Duration>0.35</Duration>"
         "<AudioFrameFrom>130</AudioFrameFrom><AudioFrameTo>140</AudioFrameTo></StreamPoint>\n"
      << " <StreamPoint><Frame>500</Frame><Type>ManualMarker</Type><Description>hand marker</Description>"
         "<Confidence>1.00</Confidence><Duration>0.00</Duration></StreamPoint>\n"
      << "</TTCut-Projectfile>\n";
  }

  // Answer every question box with `answer`, and count them. Click the
  // button: QMessageBox::question() reports the clicked button, done() alone
  // leaves it at NoButton.
  QMessageBox::StandardButton answer = QMessageBox::No;
  int questions = 0;
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, [&]() {
    if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      questions++;
      if (QAbstractButton* b = mb->button(answer)) b->click();
      else mb->done(answer);
    }
  });
  driver.start(50);

  TTCutMainWindow window;
  window.show();
  pump(300);
  TTSettings::instance()->setTempDirPath(dir.absoluteFilePath("tmp"));
  auto load = [&]() { window.openProjectFile(project); pump(5000); };
  auto save = [&](const QString& name) {
    const QString file = dir.absoluteFilePath(name);
    TTSettings::instance()->setProjectFileName(file);
    window.onFileSave();
    return file;
  };
  auto rowOf = [&](TTStreamPointModel* m, StreamPointType type) {
    for (int i = 0; i < m->rowCount(); i++) if (m->pointAt(i).type() == type) return i;
    return -1;
  };

  load();
  auto* model  = window.findChild<TTStreamPointModel*>();
  auto* widget = window.findChild<TTStreamPointWidget*>();
  if (!model || !widget) { check(false, "marker widgets found"); return 1; }
  check(model->rowCount() == 2, QString("two markers loaded (got %1)").arg(model->rowCount()));

  // 1. No
  answer = QMessageBox::No; questions = 0;
  emit widget->deleteRequested(rowOf(model, StreamPointType::AudioAnomaly));
  pump(300);
  QString f = save("no.ttcut");
  check(questions == 1, QString("1: the delete asks (%1 question)").arg(questions));
  check(model->rowCount() == 2 && countIn(f, "<Repair>") == 1, "1: answered No - marker and repair stay");

  // 2. Yes
  answer = QMessageBox::Yes; questions = 0;
  emit widget->deleteRequested(rowOf(model, StreamPointType::AudioAnomaly));
  pump(300);
  f = save("yes.ttcut");
  check(questions == 1, QString("2: the delete asks (%1 question)").arg(questions));
  check(model->rowCount() == 1 && countIn(f, "<Repair>") == 0,
        QString("2: answered Yes - marker and repair go (%1 marker, %2 <Repair>)")
            .arg(model->rowCount()).arg(countIn(f, "<Repair>")));

  // 3. hand-placed marker: no question
  questions = 0;
  emit widget->deleteRequested(rowOf(model, StreamPointType::ManualMarker));
  pump(300);
  check(questions == 0 && model->rowCount() == 0, "3: a marker without a repair goes without a question");

  // 4. Delete all, Yes
  load();
  check(model->rowCount() == 2, "4: project reloaded with both markers");
  answer = QMessageBox::Yes; questions = 0;
  emit widget->deleteAllRequested();
  pump(300);
  f = save("all.ttcut");
  check(questions == 1 && model->rowCount() == 0 && countIn(f, "<Repair>") == 0,
        QString("4: delete all asks once and removes markers and repair (%1 question, %2 marker, %3 <Repair>)")
            .arg(questions).arg(model->rowCount()).arg(countIn(f, "<Repair>")));

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
