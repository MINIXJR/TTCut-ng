// Gate for finding C3 of code-audit run 5: the menu's Exit must honour what
// the close handler decides.
//
// TTCutMainWindow::onFileExit() used to call close() and then qApp->quit()
// unconditionally. Measured on Qt 6.10.2, that had two effects - the second
// one is the costly one:
//   Cancel:   quit() runs closeEvent on the open windows itself (measured
//             with a 15-line probe: closeEvents goes 1 -> 2, the loop keeps
//             running), so the unsaved-changes question came up TWICE and
//             the application stayed. Not the "quits despite Cancel" the
//             code reading suggested, but a duplicate question.
//   Save:     closeEvent called onFileSave() without looking at the result.
//             A cancelled file dialog (or a failed write) saved nothing, the
//             handler accepted the close anyway, and the application ended
//             with the changes gone.
//
// Drives the real window offscreen with a real project loaded (that is what
// arms the dirty flag, through TTAVData::avItemAppended), answers the modal
// dialogs from a timer and checks whether the application survives:
//
//   cancel    Cancel in the unsaved-changes dialog -> window stays, no quit
//   discard   Discard -> window closes, application quits
//   savefail  Save, then cancel the file dialog -> window stays, no quit
//
//   usage: test_exit_cancel <video-es> <case>
// Build via `cmake --build build --target test_exit_cancel`.
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstring>

#include "gui/ttcutmainwindow.h"

namespace {

int  failures = 0;
bool reachedEnd = false;       // set by the body when the application survived

void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// Answers whatever modal dialog comes up: the unsaved-changes question with
// the button this case wants, any file dialog with Cancel.
void armDialogAnswers(QMessageBox::StandardButton answer, int* questionsSeen, int* fileDialogsSeen)
{
  auto* timer = new QTimer(qApp);
  QObject::connect(timer, &QTimer::timeout, [answer, questionsSeen, fileDialogsSeen]() {
    QWidget* modal = QApplication::activeModalWidget();
    if (!modal) return;
    if (auto* box = qobject_cast<QMessageBox*>(modal)) {
      if (QAbstractButton* b = box->button(answer)) { (*questionsSeen)++; b->click(); }
      return;
    }
    if (auto* dlg = qobject_cast<QFileDialog*>(modal)) { (*fileDialogsSeen)++; dlg->reject(); return; }
    if (auto* dlg = qobject_cast<QDialog*>(modal)) dlg->reject();
  });
  timer->start(50);
}

// Pumps until the window title carries the "modified" marker: opening a
// video appends an AV item, which is one of the eight signals that mark the
// project dirty - and only a dirty project is asked about on close.
bool waitForDirty(TTCutMainWindow& wnd, int timeoutMs)
{
  QElapsedTimer t; t.start();
  while (!wnd.windowTitle().endsWith("*") && t.elapsed() < timeoutMs) {
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(20);
  }
  return wnd.windowTitle().endsWith("*");
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 3) {
    fprintf(stderr, "usage: %s <video-es> <cancel|discard|savefail>\n", argv[0]);
    return 2;
  }
  const QString video = QString::fromUtf8(argv[1]);
  const QString mode  = QString::fromUtf8(argv[2]);
  const bool expectQuit = (mode == "discard");

  TTCutMainWindow mainWnd;
  mainWnd.show();
  QApplication::processEvents();

  int questionsSeen = 0, fileDialogsSeen = 0;

  QTimer::singleShot(0, &mainWnd, [&]() {
    mainWnd.onReadVideoStream(video);
    if (!waitForDirty(mainWnd, 60000)) {
      printf("FAIL: the opened video did not mark the project modified\n");
      failures++;
      reachedEnd = true;
      qApp->exit(1);
      return;
    }

    if (mode == "cancel")        armDialogAnswers(QMessageBox::Cancel,  &questionsSeen, &fileDialogsSeen);
    else if (mode == "discard")  armDialogAnswers(QMessageBox::Discard, &questionsSeen, &fileDialogsSeen);
    else                         armDialogAnswers(QMessageBox::Save,    &questionsSeen, &fileDialogsSeen);

    mainWnd.onFileExit();

    // Still here: the application did not quit. Give a pending quit one more
    // turn of the loop before saying so.
    QTimer::singleShot(400, [&]() {
      reachedEnd = true;
      check(questionsSeen == 1, "the unsaved-changes dialog was answered once");
      if (mode == "savefail")
        check(fileDialogsSeen == 1, "the save dialog came up and was cancelled");
      check(mainWnd.isVisible(), "the window is still open");
      qApp->exit(0);
    });
  });

  app.exec();

  printf("observed: questions=%d fileDialogs=%d reachedEnd=%d visible=%d\n",
         questionsSeen, fileDialogsSeen, reachedEnd ? 1 : 0, mainWnd.isVisible() ? 1 : 0);

  if (expectQuit) {
    check(!reachedEnd, "Discard closes the window and ends the application");
  } else {
    check(reachedEnd, "the application is still running after the close was refused");
  }

  printf("%s\n", failures ? "EXIT-CANCEL FAIL" : "EXIT-CANCEL PASS");
  return failures ? 1 : 0;
}
