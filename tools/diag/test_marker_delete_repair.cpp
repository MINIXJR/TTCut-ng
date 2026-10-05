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
//   5. the context menu offers a new repair for an LFE marker and for a
//      marker without a kind, not for an abrupt stop - muting does not help
//      there (heard and measured 2026-10-05); a repair that already exists
//      behind a stop marker stays editable and removable
//   6. a context menu closed without a choice does nothing - it used to
//      open the repair dialog (user report 2026-10-05), on any marker
//   7. "Repair..." jumps the main window to the marker and opens the dialog;
//      the dialog has no "Go to frame" button (user decision 2026-10-05)
//
//   usage: test_marker_delete_repair <workdir>
//
// Build via `cmake --build build --target test_marker_delete_repair`.
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QListView>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "common/ttsettings.h"
#include "data/ttstreampointmodel.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttstreampointwidget.h"
#include "gui/ttaudiorepairdialog.h"

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

  // 5. what the context menu offers, by kind of anomaly marker
  {
    const QString project5 = dir.absoluteFilePath("kinds.ttcut");
    QFile f5(project5);
    if (!f5.open(QIODevice::WriteOnly | QIODevice::Truncate)) { check(false, "5: write project"); return 1; }
    QTextStream o(&f5);
    auto marker = [&](int frame, int from, const QString& kind) {
      o << " <StreamPoint><Frame>" << frame << "</Frame><Type>AudioAnomaly</Type><Description>anomaly"
           "</Description><Confidence>0.90</Confidence><Duration>0.10</Duration>"
        << "<AudioFrameFrom>" << from << "</AudioFrameFrom><AudioFrameTo>" << from + 2 << "</AudioFrameTo>"
        << (kind.isEmpty() ? QString() : "<AnomalyKind>" + kind + "</AnomalyKind>") << "</StreamPoint>\n";
    };
    o << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n  <Order>0</Order>\n"
      << "  <Name>" << dir.absoluteFilePath("rep.264") << "</Name>\n"
      << "  <Audio><Order>0</Order><Name>" << dir.absoluteFilePath("rep.ac3") << "</Name>\n"
      << "   <Repair><FrameFrom>130</FrameFrom><FrameTo>132</FrameTo><Channels>1</Channels>"
         "<Method>silence-fade</Method></Repair>\n"
      << "  </Audio>\n  <Cut><Order>0</Order><CutIn>100</CutIn><CutOut>900</CutOut></Cut>\n </Video>\n";
    marker(104, 130, "AbruptStop");   // stop marker with a repair behind it
    marker(200, 250, "AbruptStop");   // stop marker without
    marker(300, 375, "LfeBurst");
    marker(400, 500, QString());      // older project file: no kind
    o << " <StreamPoint><Frame>600</Frame><Type>ManualMarker</Type><Description>hand marker</Description>"
         "<Confidence>1.00</Confidence><Duration>0.00</Duration></StreamPoint>\n";
    o << "</TTCut-Projectfile>\n";
    f5.close();

    window.openProjectFile(project5);
    pump(5000);
    check(model->rowCount() == 5, QString("5: five markers loaded (got %1)").arg(model->rowCount()));
    auto rowAt = [&](int frame) {
      for (int i = 0; i < model->rowCount(); i++) if (model->pointAt(i).frameIndex() == frame) return i;
      return -1;
    };
    auto offers = [&](int frame, const QString& text) {
      return widget->contextMenuTextsForTest(rowAt(frame)).contains(text);
    };
    check(offers(104, "Edit repair...") && offers(104, "Remove repair") && !offers(104, "Repair..."),
          "5: stop marker with a repair: edit and remove, no new repair");
    check(!offers(200, "Repair...") && !offers(200, "Edit repair...") && offers(200, "Delete"),
          "5: stop marker without a repair: no repair action at all");
    check(offers(300, "Repair..."), "5: LFE marker: a new repair is offered");
    check(offers(400, "Repair..."), "5: anomaly marker without a kind: a new repair is offered");

    // 6. open the context menu on a marker and close it without choosing
    auto* list = widget->findChild<QListView*>();
    check(list != nullptr, "6: marker list view found");
    int menusClosed = 0, dialogsOpened = 0;
    QString choose;                 // text of the action to pick; empty = close the menu
    QStringList dialogButtons;      // push buttons of the last repair dialog seen
    QTimer menuDriver;
    QObject::connect(&menuDriver, &QTimer::timeout, [&]() {
      if (auto* dlg = qobject_cast<TTAudioRepairDialog*>(QApplication::activeModalWidget())) {
        dialogsOpened++;
        dialogButtons.clear();
        for (const QPushButton* b : dlg->findChildren<QPushButton*>()) dialogButtons << b->text();
        dlg->reject();
      } else if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
        menusClosed++;
        QAction* pick = nullptr;
        for (QAction* a : m->actions()) if (!choose.isEmpty() && a->text() == choose) pick = a;
        if (pick) {
          m->setActiveAction(pick);
          QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QApplication::sendEvent(m, &key);
        } else {
          m->close();
        }
      }
    });
    menuDriver.start(50);
    const int before = model->rowCount();
    for (int frame : {300, 200, 104, 600}) {
      if (!list) break;
      const int menusBefore = menusClosed, dialogsBefore = dialogsOpened;
      list->scrollTo(model->index(rowAt(frame), 0));
      const QPoint pos = list->visualRect(model->index(rowAt(frame), 0)).center();
      const bool hit = list->indexAt(pos).row() == rowAt(frame);
      QMetaObject::invokeMethod(widget, "onContextMenu", Qt::DirectConnection, Q_ARG(QPoint, pos));
      pump(300);
      check(hit && menusClosed == menusBefore + 1,
            QString("6: marker %1: the menu opened on its row and was closed without a choice").arg(frame));
      check(dialogsOpened == dialogsBefore,
            QString("6: marker %1: closing the menu opens no repair dialog").arg(frame));
    }
    check(model->rowCount() == before, "6: closing the menu changes no marker");

    // 7. choose "Repair..." on the LFE marker
    QList<int> jumps;
    QObject::connect(widget, &TTStreamPointWidget::jumpToFrame, [&](int frame) { jumps << frame; });
    if (list) {
      choose = QStringLiteral("Repair...");
      const int dialogsBefore = dialogsOpened;
      list->scrollTo(model->index(rowAt(300), 0));
      const QPoint pos = list->visualRect(model->index(rowAt(300), 0)).center();
      QMetaObject::invokeMethod(widget, "onContextMenu", Qt::DirectConnection, Q_ARG(QPoint, pos));
      pump(300);
      check(dialogsOpened == dialogsBefore + 1, "7: \"Repair...\" opens the repair dialog");
      check(jumps == QList<int>{300},
            QString("7: opening the dialog jumps to the marker's frame, once (%1 jump(s))").arg(jumps.size()));
      check(!dialogButtons.isEmpty() && !dialogButtons.contains("Go to frame"),
            "7: the dialog has no \"Go to frame\" button: " + dialogButtons.join(" | "));
    }
    menuDriver.stop();
  }

  printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
