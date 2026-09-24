// A project whose video file is missing must not stay half loaded.
//
// TODO.md (finding 2026-09-23, gate autocut_exit): TTAVData::
// endAbortedProjectLoad() sets the current AV item to nullptr, but
// TTCutMainWindow::onAVItemChanged() returns at once when that already is
// its current item - which it is when no video of the project ever opened.
// closeProject() then never ran: the AV item created for the project, with
// its cuts, stayed, and the project stayed "modified". Measured headless:
// quitting ran into the "Save changes before closing?" dialog and hung.
//
// Drives the real window offscreen: loads a project whose only video does
// not exist, waits for the load to end, then checks that the user is told
// (one "Project Not Loaded" dialog naming the project and the reason), the
// window title, the cut list and whether closing asks to save anything.
//
//   usage: test_project_missing_video <workdir>
// Build via `cmake --build build --target test_project_missing_video`.
#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <cstdio>

#include "gui/ttcutmainwindow.h"
#include "gui/ttcuttreeview.h"

namespace {
int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(20);
  }
}

// Rows of the cut list view, whatever its model.
int cutRows(TTCutMainWindow& wnd)
{
  auto* view = wnd.findChild<TTCutTreeView*>();
  if (!view) return -1;
  auto* tree = view->findChild<QTreeView*>();
  return (tree && tree->model()) ? tree->model()->rowCount() : -1;
}
} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
  const QDir work(QString::fromUtf8(argv[1]));
  QDir().mkpath(work.absolutePath());

  const QString project = work.filePath("missing_video.ttcut");
  {
    QFile f(project);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { fprintf(stderr, "cannot write project\n"); return 2; }
    QTextStream(&f) << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
                       " <Video>\n  <Order>0</Order>\n  <Name>" << work.filePath("does_not_exist.m2v") << "</Name>\n"
                       "  <Cut><Order>0</Order><CutIn>100</CutIn><CutOut>300</CutOut></Cut>\n"
                       " </Video>\n</TTCut-Projectfile>\n";
  }

  TTCutMainWindow wnd;
  wnd.show();
  QApplication::processEvents();

  // Any modal dialog on the way (the open failure's own message, a save
  // question) is counted and dismissed.
  int saveQuestions = 0, otherDialogs = 0;
  QString dialogText;
  QTimer dismiss;
  QObject::connect(&dismiss, &QTimer::timeout, [&]() {
    QWidget* modal = QApplication::activeModalWidget();
    if (!modal) return;
    if (auto* box = qobject_cast<QMessageBox*>(modal)) {
      if (QAbstractButton* d = box->button(QMessageBox::Discard)) { saveQuestions++; d->click(); return; }
      otherDialogs++;
      dialogText = box->text();
      box->close();
      return;
    }
    if (auto* dlg = qobject_cast<QDialog*>(modal)) { otherDialogs++; dlg->reject(); }
  });
  dismiss.start(50);

  const QString titleBefore = wnd.windowTitle();
  wnd.openProjectFile(project);
  pump(3000);

  const QString titleAfter = wnd.windowTitle();
  const int rows = cutRows(wnd);
  printf("title before: \"%s\"\ntitle after:  \"%s\"\ncut list rows: %d, other dialogs: %d\n",
         qPrintable(titleBefore), qPrintable(titleAfter), rows, otherDialogs);

  printf("dialog text: \"%s\"\n", qPrintable(dialogText));
  check(otherDialogs == 1, "one dialog tells the user the project was not loaded");
  check(dialogText.contains("missing_video.ttcut") && dialogText.contains("does_not_exist.m2v"),
        "the dialog names the project and the missing video");
  check(!titleAfter.endsWith("*"), "the failed project does not leave the window marked modified");
  check(rows == 0, "the failed project leaves no cut in the cut list");

  // Closing must have nothing to ask about.
  wnd.close();
  pump(500);
  printf("save questions on close: %d, window visible: %d\n", saveQuestions, wnd.isVisible() ? 1 : 0);
  check(saveQuestions == 0, "closing after the failed load asks nothing");

  printf("%s\n", failures == 0 ? "PASS: a project with a missing video leaves nothing behind"
                               : "FAIL: the failed project load leaves state behind");
  return failures == 0 ? 0 : 1;
}
