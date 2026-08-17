/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Regression harness for the cut-list button overlap (2026-08-17): the       */
/* TTCutTreeView declared minimumSize 16x104 while its five action buttons    */
/* need 160 px, and the cutListTab pinned its height to 160..200 - shrinking  */
/* the window pushed the bottom button out of the widget. The fix removes     */
/* the hand-written minimums so Qt derives them from the layouts.             */
/*                                                                            */
/* The check instantiates the REAL main window, resizes it to something far   */
/* below any sane minimum (Qt clamps resize() to minimumSize, exactly what    */
/* the window manager enforces), and then requires every cut-list action      */
/* button to sit fully inside the cut-list widget without pairwise overlap.  */
/*                                                                            */
/*   usage: test_cutlist_minsize          (QT_QPA_PLATFORM=offscreen works)   */
/*   Build: cmake --build build --target test_cutlist_minsize                 */
/*----------------------------------------------------------------------------*/

#include "../../gui/ttcutmainwindow.h"

#include <QApplication>
#include <QPushButton>
#include <QTabWidget>
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  TTCutMainWindow w;
  w.show();
  QApplication::processEvents();

  // Force far below minimum; Qt clamps to minimumSize like a WM would.
  w.resize(400, 300);
  QApplication::processEvents();
  printf("main window after resize(400,300): %dx%d (minimumSizeHint %dx%d)\n",
         w.width(), w.height(),
         w.minimumSizeHint().width(), w.minimumSizeHint().height());

  QWidget* cutList = w.findChild<QWidget*>("cutList");
  check(cutList != nullptr, "cut-list widget found");
  if (!cutList) return 1;

  const int listH = cutList->height();
  printf("cut-list widget height: %d\n", listH);

  const char* names[] = {"pbPreview", "pbCutAudioVideo", "pbCutSelected",
                         "pbCutAudio", "pbCutAudioSelected"};
  int prevBottom = -1;
  for (const char* name : names) {
    auto* b = cutList->findChild<QPushButton*>(name);
    check(b != nullptr, name);
    if (!b) continue;
    // Geometry relative to the cut-list widget (buttons live in a nested
    // layout directly on it, so geometry() already is in its coordinates).
    const QRect g = b->geometry();
    printf("  %-20s y=%3d..%3d h=%2d\n", name, g.top(), g.bottom(), g.height());
    check(g.top() > prevBottom, "no overlap with previous button");
    check(g.bottom() < listH, "fully inside the cut-list widget");
    prevBottom = g.bottom();
  }

  // Scan a window-size grid: the defect may only appear at intermediate
  // sizes (layout distribution), not at the clamped minimum.
  const int minW = w.width(), minH = w.height();
  int scanFailures = 0;
  for (int dw : {0, 200, 400, 800}) {
    for (int dh = 0; dh <= 400; dh += 10) {
      w.resize(minW + dw, minH + dh);
      QApplication::processEvents();
      int prev = -1;
      const int lh = cutList->height();
      for (const char* name : names) {
        auto* b = cutList->findChild<QPushButton*>(name);
        const QRect g = b->geometry();
        if (g.top() <= prev || g.bottom() >= lh) {
          printf("SCAN-FAIL at %dx%d: %s y=%d..%d listH=%d\n",
                 minW + dw, minH + dh, name, g.top(), g.bottom(), lh);
          ++scanFailures;
          break;
        }
        prev = g.bottom();
      }
      if (scanFailures > 5) break;
    }
    if (scanFailures > 5) break;
  }
  failures += scanFailures;

  printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
  return failures ? 1 : 0;
}
