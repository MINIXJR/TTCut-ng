// Gate for a finding of the full map check (2026-10-11):
// TTCutMainWindow::onFileSave() built the QFileInfo it hands to
// writeProjectFile BEFORE it appended ".ttcut" to a chosen name without
// extension. Measured: the first save wrote the file under the name as
// typed while TTSettings::projectFileName and the window title said
// ".ttcut", and the second save wrote a second file.
//
// Drives the real window offscreen, opens a video, answers the save dialog
// with a name WITHOUT extension and looks at what is on disk - after the
// first save and after a second one (which no longer asks for a name).
//
//   usage: test_save_suffix <video-es> <empty-dir>
// Build via `cmake --build build --target test_save_suffix`.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QThread>
#include <QTimer>
#include <cstdio>

#include "common/ttsettings.h"
#include "gui/ttcutmainwindow.h"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

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
    fprintf(stderr, "usage: %s <video-es> <empty-dir>\n", argv[0]);
    return 2;
  }
  const QString video = QString::fromUtf8(argv[1]);
  const QString dir   = QDir(QString::fromUtf8(argv[2])).absolutePath();
  const QString typed = dir + "/noext";

  TTCutMainWindow mainWnd;
  mainWnd.show();
  QApplication::processEvents();

  int fileDialogsSeen = 0;

  QTimer::singleShot(0, &mainWnd, [&]() {
    mainWnd.onReadVideoStream(video);
    if (!waitForDirty(mainWnd, 60000)) {
      printf("FAIL: the opened video did not mark the project modified\n");
      failures++;
      qApp->exit(1);
      return;
    }

    // Answers the save dialog with a name that has no extension.
    auto* timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, [&]() {
      auto* dlg = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      // The name goes into the dialog's own input field, and the dialog is
      // accepted only on a later tick that still finds it there: right after
      // it opens, the dialog applies its initial selection, and a name set
      // before that is overwritten (measured: 5 of 12 runs saved under the
      // proposed name instead of the typed one).
      auto* edit = dlg->findChild<QLineEdit*>("fileNameEdit");
      if (!edit) { printf("FAIL: the file dialog has no input field\n"); failures++; dlg->reject(); return; }
      if (edit->text() != typed) { edit->setText(typed); return; }
      if (dlg->property("ttAccepted").toBool()) return;
      dlg->setProperty("ttAccepted", true);
      fileDialogsSeen++;
      QMetaObject::invokeMethod(dlg, "accept", Qt::QueuedConnection);
    });
    timer->start(50);

    const bool first = mainWnd.onFileSave();
    printf("observed after save 1: returned=%d dialogs=%d projectFileName=%s title=%s\n",
           first ? 1 : 0, fileDialogsSeen,
           qPrintable(TTSettings::instance()->projectFileName()), qPrintable(mainWnd.windowTitle()));
    printf("observed after save 1: on disk: %s\n", qPrintable(QDir(dir).entryList(QDir::Files).join(", ")));
    const bool namedFileExists = QFileInfo::exists(TTSettings::instance()->projectFileName());
    const bool typedFileExists = QFileInfo::exists(typed);

    const bool second = mainWnd.onFileSave();
    printf("observed after save 2: returned=%d dialogs=%d on disk: %s\n",
           second ? 1 : 0, fileDialogsSeen, qPrintable(QDir(dir).entryList(QDir::Files).join(", ")));

    check(first, "the first save reports a written file");
    check(fileDialogsSeen == 1, "the save dialog came up once");
    check(TTSettings::instance()->projectFileName() == typed + ".ttcut", "projectFileName carries .ttcut");
    check(namedFileExists, "the file named by projectFileName exists after the first save");
    check(!typedFileExists, "no file without extension was written");
    check(QDir(dir).entryList(QDir::Files).size() == 1, "two saves leave one project file");
    timer->stop();
    qApp->exit(0);
  });

  app.exec();
  printf("%s\n", failures ? "SAVE-SUFFIX FAIL" : "SAVE-SUFFIX PASS");
  return failures ? 1 : 0;
}
