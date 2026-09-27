// Gate for the cut overview bar above the video slider (audit run 16, map
// navigator-window.md, N1). The bar draws all current cuts in every
// paintEvent, so what it shows is the cut list as of its last Paint event. An
// event filter records the cut count at every Paint of the bar, in the real
// main window with a loaded project:
//   - after the project load the bar shows the project's cuts;
//   - a cut appended through the "add cut" signal is followed by a Paint
//     (it was not: the bar kept the old cuts until the slider moved);
//   - deleting a cut through the cut list is followed by a Paint.
//
//   usage: test_navigator_refresh <project.ttcut with one cut>
//
// Build via `cmake --build build --target test_navigator_refresh`.
#include <QApplication>
#include <QElapsedTimer>
#include <QSlider>
#include <QTreeWidget>

#include <clocale>
#include <cstdio>

#include "common/ttsettings.h"
#include "gui/ttcutframenavigation.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttcuttreeview.h"
#include "gui/ttnavigatordisplay.h"
#include "gui/ttstreamnavigator.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

static void spin(int ms)
{
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Cut count of the list at the bar's last Paint event.
struct PaintSpy : QObject
{
  QTreeWidget* tree = nullptr;
  int cutsAtLastPaint = -1;
  bool eventFilter(QObject*, QEvent* e) override
  {
    if (e->type() == QEvent::Paint) cutsAtLastPaint = tree->topLevelItemCount();
    return false;
  }
};

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start otherwise, as in main()
  app.setApplicationName("TTCut-ng");
  app.setOrganizationName("TTCut-ng");
  if (argc < 2) { fprintf(stderr, "usage: %s <project.ttcut>\n", argv[0]); return 2; }
  (void)TTSettings::instance();

  TTCutMainWindow w;
  w.show();
  spin(300);
  auto* tree = w.findChild<QTreeWidget*>("videoCutList");
  auto* nav  = w.findChild<TTStreamNavigator*>("streamNavigator");
  auto* list = w.findChild<TTCutTreeView*>("cutList");
  auto* fn   = w.findChild<TTCutFrameNavigation*>("navigation");
  auto* bar  = w.findChild<TTNavigatorDisplay*>();
  if (!tree || !nav || !list || !fn || !bar) { fprintf(stderr, "main window widgets not found\n"); return 2; }
  PaintSpy spy;
  spy.tree = tree;
  bar->installEventFilter(&spy);

  w.openProjectFile(QString::fromLocal8Bit(argv[1]));
  QElapsedTimer t;
  t.start();
  while (tree->topLevelItemCount() == 0 && t.elapsed() < 30000) spin(50);
  spin(1500);
  check(tree->topLevelItemCount() == 1 && spy.cutsAtLastPaint == 1,
        QString("after the project load the bar shows the project's cut - list %1, bar %2")
            .arg(tree->topLevelItemCount()).arg(spy.cutsAtLastPaint));

  const int last = nav->slider()->maximum();
  emit fn->addCutRange(last - 200, last);
  spin(1500);
  check(tree->topLevelItemCount() == 2 && spy.cutsAtLastPaint == 2,
        QString("an appended cut reaches the bar - list %1, bar %2")
            .arg(tree->topLevelItemCount()).arg(spy.cutsAtLastPaint));

  tree->clearSelection();
  tree->topLevelItem(1)->setSelected(true);
  list->onEntryDelete();
  spin(1500);
  check(tree->topLevelItemCount() == 1 && spy.cutsAtLastPaint == 1,
        QString("a deleted cut leaves the bar - list %1, bar %2")
            .arg(tree->topLevelItemCount()).arg(spy.cutsAtLastPaint));

  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
