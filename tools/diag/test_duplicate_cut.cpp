// Gate for a finding of the full map check (2026-10-11):
// TTCutTreeView::onEntryDuplicate() hands every selected row to
// TTAVData::copyCutEntry(), which appended the row's range to the CURRENT
// video (not to the row's own) through appendCutEntry() - and that one
// throws when the range is refused. Nobody caught on this route. Measured:
// a range the current video does not have ended the program in
// std::terminate, a range that fits landed on the wrong video.
//
// Two MPEG-2 fixtures of different length are opened, each gets a cut, both
// rows are selected with the shorter video current, then "Duplicate" runs.
//
//   usage: test_duplicate_cut <shorter-video-es> <longer-video-es> <direct|action>
//     direct  calls the slot and catches what leaves it
//     action  triggers the context-menu action inside the event loop
// Build via `cmake --build build --target test_duplicate_cut`.
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QMessageBox>
#include <QSlider>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

#include "common/ttexception.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttcuttreeview.h"
#include "gui/ttvideotreeview.h"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
  fflush(stdout);
}

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
}

void waitIdle(int timeoutMs)
{
  QElapsedTimer t; t.start();
  int quiet = 0;
  while (t.elapsed() < timeoutMs && quiet < 10) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
    quiet = (QThreadPool::globalInstance()->activeThreadCount() == 0) ? quiet + 1 : 0;
  }
}

QString rows(QTreeWidget* tree)
{
  QStringList out;
  for (int r = 0; r < tree->topLevelItemCount(); ++r)
    out << tree->topLevelItem(r)->text(0) + " " + tree->topLevelItem(r)->text(1) + "-" + tree->topLevelItem(r)->text(2);
  return out.join(" | ");
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 4) {
    fprintf(stderr, "usage: %s <shorter-video-es> <longer-video-es> <direct|action> [fits]\n", argv[0]);
    return 2;
  }
  const QString shortVideo = QString::fromUtf8(argv[1]);
  const QString longVideo  = QString::fromUtf8(argv[2]);
  const bool viaAction     = QString::fromUtf8(argv[3]) == "action";
  const bool fits          = argc > 4 && QString::fromUtf8(argv[4]) == "fits";

  TTCutMainWindow mainWnd;
  mainWnd.show();
  QApplication::processEvents();

  // Any message box is answered and its text kept.
  QStringList boxes;
  auto* boxTimer = new QTimer(qApp);
  QObject::connect(boxTimer, &QTimer::timeout, [&]() {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      boxes << box->text();
      box->accept();
    }
  });
  boxTimer->start(50);

  QTimer::singleShot(0, &mainWnd, [&]() {
    auto* videoView = mainWnd.findChild<TTVideoTreeView*>();
    auto* videoTree = videoView ? videoView->findChild<QTreeWidget*>() : nullptr;
    auto* cutView   = mainWnd.findChild<TTCutTreeView*>();
    auto* cutTree   = cutView ? cutView->findChild<QTreeWidget*>() : nullptr;
    if (!videoTree || !cutTree) { printf("FAIL: widgets not found\n"); failures++; qApp->exit(1); return; }

    mainWnd.onReadVideoStream(shortVideo); pump(500); waitIdle(60000);
    mainWnd.onReadVideoStream(longVideo);  pump(500); waitIdle(60000);
    if (videoTree->topLevelItemCount() != 2) {
      printf("FAIL: expected two videos in the list, got %d\n", videoTree->topLevelItemCount());
      failures++; qApp->exit(1); return;
    }

    auto frameCountOfCurrent = [&]() {
      int best = -1;
      for (QSlider* s : mainWnd.findChildren<QSlider*>()) best = qMax(best, s->maximum());
      return best + 1;
    };

    videoTree->setCurrentItem(videoTree->topLevelItem(1)); pump(300);
    const int longCount = frameCountOfCurrent();
    // "beyond": the last frames of the longer video, which the shorter one
    // does not have. "fits": a range both videos have.
    if (fits) mainWnd.onAppendCutEntry(200, 300);
    else      mainWnd.onAppendCutEntry(longCount - 21, longCount - 1);
    pump(200);

    videoTree->setCurrentItem(videoTree->topLevelItem(0)); pump(300);
    const int shortCount = frameCountOfCurrent();
    mainWnd.onAppendCutEntry(10, 100); pump(200);

    printf("observed: frames shorter=%d longer=%d; cut list before: %s\n",
           shortCount, longCount, qPrintable(rows(cutTree)));
    if (cutTree->topLevelItemCount() != 2 || longCount <= shortCount) {
      printf("FAIL: setup did not produce two cuts on videos of different length (boxes: %s)\n",
             qPrintable(boxes.join(" / ")));
      failures++; qApp->exit(1); return;
    }

    // Both rows selected, the shorter video current.
    // What Ctrl-click on the two rows leaves behind: both selected, the row
    // clicked last current - and its video the current one (itemClicked ->
    // onEntrySelected -> onCutSelectionChanged -> onChangeCurrentAVItem).
    videoTree->setCurrentItem(videoTree->topLevelItem(0)); pump(200);
    cutTree->setCurrentItem(cutTree->topLevelItem(1), 0, QItemSelectionModel::NoUpdate);
    for (int r = 0; r < 2; ++r) cutTree->topLevelItem(r)->setSelected(true);
    pump(100);
    printf("observed: selected rows=%d, current row=%d\n", int(cutTree->selectedItems().size()),
           cutTree->indexOfTopLevelItem(cutTree->currentItem()));
    fflush(stdout);

    bool escaped = false;
    QString escapedText;
    if (viaAction) {
      QAction* dup = nullptr;
      for (QAction* a : cutView->findChildren<QAction*>())
        if (a->text().contains("Duplicate", Qt::CaseInsensitive)) dup = a;
      if (!dup) { printf("FAIL: duplicate action not found\n"); failures++; qApp->exit(1); return; }
      printf("observed: triggering the duplicate action inside the event loop\n"); fflush(stdout);
      QTimer::singleShot(0, dup, &QAction::trigger);
      pump(500);
    } else {
      try {
        QMetaObject::invokeMethod(cutView, "onEntryDuplicate", Qt::DirectConnection);
      } catch (const TTException& ex) {
        escaped = true;
        escapedText = ex.getMessage();
      }
    }

    printf("observed: exception left the slot=%d (%s); cut list after: %s; boxes: %s\n",
           escaped ? 1 : 0, qPrintable(escapedText), qPrintable(rows(cutTree)), qPrintable(boxes.join(" / ")));

    // Every selected row is copied onto ITS OWN video: afterwards each video
    // has two rows, and they carry the same range.
    QHash<QString, QStringList> rangesByVideo;
    for (int r = 0; r < cutTree->topLevelItemCount(); ++r)
      rangesByVideo[cutTree->topLevelItem(r)->text(0)] << cutTree->topLevelItem(r)->text(1) + "-" + cutTree->topLevelItem(r)->text(2);
    bool ownVideo = (rangesByVideo.size() == 2);
    for (const QStringList& ranges : rangesByVideo)
      ownVideo = ownVideo && ranges.size() == 2 && ranges.at(0) == ranges.at(1);

    check(!escaped, "no exception leaves the duplicate slot");
    check(boxes.isEmpty(), "no refusal is reported");
    check(cutTree->topLevelItemCount() == 4, "both selected cuts were duplicated");
    check(ownVideo, "each copy lies on the video of its own row");
    qApp->exit(0);
  });

  app.exec();
  printf("%s\n", failures ? "DUPLICATE-CUT FAIL" : "DUPLICATE-CUT PASS");
  return failures ? 1 : 0;
}
