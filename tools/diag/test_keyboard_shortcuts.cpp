/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Keyboard shortcuts of the main window (2026-10-28): the desktop standard   */
/* keys on the menu actions, one mnemonic per menu entry, Space as a window   */
/* shortcut that text fields keep for themselves, the J/K/L playback shuttle, */
/* Ctrl+G for the time warp, Del in the cut list only, and a help dialog that */
/* lists what is bound.                                                       */
/*                                                                            */
/* The keys go through QTest, i.e. through ShortcutOverride and the shortcut  */
/* map like a real key press - a direct slot call would prove nothing about   */
/* which widget gets the key.                                                 */
/*                                                                            */
/*   usage: test_keyboard_shortcuts       (QT_QPA_PLATFORM=offscreen works)   */
/*   Build: cmake --build build --target test_keyboard_shortcuts              */
/*----------------------------------------------------------------------------*/

#include "../../gui/ttcutmainwindow.h"
#include "../../gui/ttcutframenavigation.h"
#include "../../gui/ttcuttreeview.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

static int failures = 0;

static void check(bool cond, const QString& what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", qPrintable(what));
  if (!cond) ++failures;
}

//! The mnemonic letter of a menu text ("Save Project &as..." -> 'A'), 0 if none.
static QChar mnemonicOf(const QString& text)
{
  for (int i = 0; i + 1 < text.size(); ++i) {
    if (text[i] != '&') continue;
    if (text[i + 1] == '&') { ++i; continue; }
    return text[i + 1].toUpper();
  }
  return QChar();
}

//! Every entry of the menu has a mnemonic, and no letter is used twice.
static void checkMnemonics(const QString& where, const QList<QAction*>& actions)
{
  QString used;
  for (const QAction* action : actions) {
    if (action->isSeparator() || action->text().isEmpty()) continue;   // hidden recent-file slots
    const QChar letter = mnemonicOf(action->text());
    check(!letter.isNull(), QString("%1: '%2' has a mnemonic").arg(where, action->text()));
    if (letter.isNull()) continue;
    check(!used.contains(letter), QString("%1: mnemonic %2 of '%3' is unique").arg(where, letter, action->text()));
    used += letter;
  }
}

//! Focus `target` and press `key` on it the way the keyboard would.
static void pressOn(QWidget* target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
  target->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  QTest::keyClick(target, key, mods);
  QApplication::processEvents();
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  TTCutMainWindow w;
  w.show();
  w.activateWindow();
  check(QTest::qWaitForWindowActive(&w), "main window becomes active");

  auto* navigation = w.findChild<TTCutFrameNavigation*>("navigation");
  auto* cutList    = w.findChild<TTCutTreeView*>("cutList");
  check(navigation && cutList, "navigation and cut-list widgets found");
  if (!navigation || !cutList) return 1;
  // No stream is open: enable the controls as an open stream would, so the
  // keys reach their handlers (they are ignored while the controls are off).
  navigation->controlEnabled(true);
  cutList->controlEnabled(true);
  QApplication::processEvents();

  // --- desktop standard keys on the menu actions ---------------------------
  const struct { const char* action; QKeySequence::StandardKey key; } standard[] = {
    {"actionFileNew", QKeySequence::New},       {"actionFileOpen", QKeySequence::Open},
    {"actionFileSave", QKeySequence::Save},     {"actionFileSaveAs", QKeySequence::SaveAs},
    {"actionExit", QKeySequence::Quit},         {"actionSettings", QKeySequence::Preferences},
    {"actionKeyboardShortcuts", QKeySequence::HelpContents}};
  for (const auto& s : standard) {
    const QAction* action = w.findChild<QAction*>(s.action);
    check(action && action->shortcuts() == QKeySequence::keyBindings(s.key),
          QString("%1 carries the platform binding (%2)").arg(s.action,
              action ? action->shortcut().toString() : QString("missing")));
  }
  // Ctrl+S is the binding of every platform theme, offscreen included - the
  // comparison above would also hold for two empty lists.
  const QAction* saveAction = w.findChild<QAction*>("actionFileSave");
  check(saveAction && saveAction->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_S)),
        "Save Project is bound to Ctrl+S");

  // --- mnemonics -------------------------------------------------------------
  QList<QAction*> menus = w.menuBar()->actions();
  checkMnemonics("menu bar", menus);
  for (QAction* menuAction : menus)
    if (QMenu* menu = menuAction->menu())
      checkMnemonics(menu->title().remove('&'), menu->actions());

  // --- Space: window shortcut, text fields keep it ---------------------------
  QShortcut* space = nullptr;
  for (QShortcut* s : w.findChildren<QShortcut*>())
    if (s->key() == QKeySequence(Qt::Key_Space)) space = s;
  check(space != nullptr, "Space shortcut exists on the main window");
  if (!space) return 1;
  QSignalSpy spaceSpy(space, &QShortcut::activated);

  auto* button = navigation->findChild<QPushButton*>("pbPrevIFrame");
  check(button && button->isEnabled(), "navigation button available");
  if (button) {
    QSignalSpy clicked(button, &QAbstractButton::clicked);
    pressOn(button, Qt::Key_Space);
    check(spaceSpy.count() == 1, "Space on a focused button plays");
    check(clicked.count() == 0, "Space on a focused button does not click it");
  }

  auto* spinBox = navigation->findChild<QDoubleSpinBox*>();
  check(spinBox != nullptr, "threshold spin box found");
  if (spinBox) {
    spaceSpy.clear();
    pressOn(spinBox, Qt::Key_Space);
    check(spaceSpy.count() == 0, "Space in a spin box stays with the spin box");
  }

  // A text field in the window (the audio delay box only exists with audio)
  auto* lineEdit = new QLineEdit(w.centralWidget());
  lineEdit->show();
  spaceSpy.clear();
  pressOn(lineEdit, Qt::Key_Space);
  check(spaceSpy.count() == 0 && lineEdit->text() == " ", "Space in a line edit types a blank");
  delete lineEdit;

  auto* cutTree = cutList->findChild<QTreeWidget*>("videoCutList");
  check(cutTree != nullptr, "cut-list tree found");
  if (cutTree) {
    spaceSpy.clear();
    pressOn(cutTree, Qt::Key_Space);
    check(spaceSpy.count() == 1, "Space in the cut list plays");
  }

  // --- J/K/L, Ctrl+G, no j/k frame step any more ----------------------------
  QSignalSpy backward(navigation, &TTCutFrameNavigation::shuttleBackward);
  QSignalSpy stop(navigation, &TTCutFrameNavigation::shuttleStop);
  QSignalSpy forward(navigation, &TTCutFrameNavigation::shuttleForward);
  QSignalSpy nextFrame(navigation, &TTCutFrameNavigation::nextBFrame);
  QSignalSpy prevFrame(navigation, &TTCutFrameNavigation::prevBFrame);
  QSignalSpy quickJump(navigation, &TTCutFrameNavigation::openQuickJump);
  QSignalSpy home(navigation, &TTCutFrameNavigation::moveToHome);
  pressOn(&w, Qt::Key_J);
  pressOn(&w, Qt::Key_K);
  pressOn(&w, Qt::Key_L);
  check(backward.count() == 1 && stop.count() == 1 && forward.count() == 1,
        QString("J/K/L reach the shuttle (%1/%2/%3)").arg(backward.count()).arg(stop.count()).arg(forward.count()));
  check(nextFrame.count() == 0 && prevFrame.count() == 0, "J/K no longer step frames");
  pressOn(&w, Qt::Key_L, Qt::ControlModifier);
  check(forward.count() == 1, "Ctrl+L is not the shuttle");
  pressOn(&w, Qt::Key_G, Qt::ControlModifier);
  check(quickJump.count() == 1 && home.count() == 0, "Ctrl+G opens the time warp, not the first frame");

  // A list with the focus must not swallow the letter keys (type-ahead
  // search), but keeps the other letters for its search.
  // L and K: their targets are safe without an open stream (I would decode)
  int views = 0;
  for (QAbstractItemView* view : w.findChildren<QAbstractItemView*>()) {
    if (view->window() != &w || !view->isEnabled()) continue;
    ++views;
    const int forwardBefore = forward.count(), stopBefore = stop.count();
    pressOn(view, Qt::Key_L);
    pressOn(view, Qt::Key_K);
    check(forward.count() == forwardBefore + 1 && stop.count() == stopBefore + 1,
          QString("L and K reach the navigation while %1 has the focus").arg(view->objectName().isEmpty() ? view->metaObject()->className()
                                                 : view->objectName()));
  }
  check(views >= 4, QString("all lists of the window checked (%1)").arg(views));
  if (cutTree) {
    bool reachedList = false;
    struct Probe : QObject {
      bool* hit;
      explicit Probe(bool* h) : hit(h) {}
      bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::KeyPress) *hit = true;
        return false;
      }
    } probe(&reachedList);
    // The filter installed last runs first: reinstall the main window's
    // filter after the probe, so the probe only sees what it lets through.
    cutTree->removeEventFilter(&w);
    cutTree->installEventFilter(&probe);
    cutTree->installEventFilter(&w);
    pressOn(cutTree, Qt::Key_X);
    const bool xReached = reachedList;
    reachedList = false;
    pressOn(cutTree, Qt::Key_L);
    const bool lReached = reachedList;
    cutTree->removeEventFilter(&probe);
    check(xReached, "other letters still reach the list for its search");
    check(!lReached, "L does not reach the list");
  }

  // --- Del: only in the cut list ---------------------------------------------
  QAction* deleteAction = nullptr;
  if (cutTree)
    for (QAction* a : cutTree->actions())
      if (a->shortcuts().contains(QKeySequence(Qt::Key_Delete))) deleteAction = a;
  check(deleteAction && deleteAction->shortcutContext() == Qt::WidgetShortcut,
        "Del is a widget shortcut of the cut list");
  if (deleteAction && button) {
    QSignalSpy deleted(deleteAction, &QAction::triggered);
    pressOn(cutTree, Qt::Key_Delete);
    check(deleted.count() == 1, "Del in the cut list deletes");
    pressOn(button, Qt::Key_Delete);
    check(deleted.count() == 1, "Del elsewhere does not delete a cut");
  }

  // --- help dialog lists what is bound ----------------------------------------
  QString helpText;
  QTimer::singleShot(0, [&helpText]() {
    for (QWidget* top : QApplication::topLevelWidgets())
      if (auto* box = qobject_cast<QMessageBox*>(top); box && box->isVisible()) {
        helpText = box->text();
        box->done(0);
      }
  });
  w.findChild<QAction*>("actionKeyboardShortcuts")->trigger();
  check(!helpText.isEmpty(), "help dialog shown");
  const auto native = [](QKeyCombination c) {
    return QKeySequence(c).toString(QKeySequence::NativeText).toHtmlEscaped();
  };
  for (QKeyCombination c : {QKeyCombination(Qt::CTRL | Qt::Key_S), QKeyCombination(Qt::Key_Space),
                            QKeyCombination(Qt::Key_L), QKeyCombination(Qt::SHIFT | Qt::Key_G),
                            QKeyCombination(Qt::CTRL | Qt::Key_G), QKeyCombination(Qt::Key_Delete)})
    // a key of its own, or the second of a "first / second" pair
    check(helpText.contains("<b>" + native(c) + "</b>") || helpText.contains(" / " + native(c) + "</b>"),
          QString("help lists %1").arg(native(c)));
  check(!helpText.contains("j / k"), "help no longer lists j / k");

  printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
  return failures ? 1 : 0;
}
