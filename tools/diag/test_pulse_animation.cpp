// Does an indeterminate QProgressBar animate here - and does it still animate
// when its parent window is disabled?
//
// Background: TTProgressBar switches the bar to setRange(0, 0) after 5 s
// without a Step ("pulse mode") to show that work continues without knowing
// how far along it is. Reported from the GUI: the bar shows static light/dark
// blue stripes and never moves, while the dialog's own 1 s debug clock keeps
// counting - so the event loop is alive and only the style animation is
// missing.
//
// The suspicion this probe tests: TTCutMainWindow disables itself for the
// duration of an operation (setEnabled(false) in the Init branch), and a
// disabled widget is painted in its disabled state, which for most styles
// means no animation. Measured during a real run: parentEnabled=0 while the
// bar was visible.
//
// Four bars side by side, all in setRange(0, 0):
//   1. in an enabled window
//   2. in a window that is disabled right after showing
//   3. in a dialog whose PARENT window is disabled - the exact shape TTCut
//      produces (TTProgressBar is constructed with the main window as parent)
//
// Watch which ones move. Whatever the answer, it decides where the fix goes:
// if (1) moves and (3) does not, it is ours - the dialog must not inherit the
// disabled state. If none moves, the style does not animate indeterminate bars
// at all and the pulse idea needs a different mechanism (e.g. a timer that
// walks the value by hand).
//
// Not offscreen: without a real platform theme there is no style animation to
// observe in the first place.
//
//   usage: test_pulse_animation [seconds]     (default 20)
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QWidget>

#include <cstdio>

static QProgressBar* pulseBar(QWidget* parent, const QString& caption)
{
  auto* lay = new QVBoxLayout(parent);
  lay->addWidget(new QLabel(caption, parent));
  auto* bar = new QProgressBar(parent);
  bar->setRange(0, 0);            // indeterminate == TTProgressBar's pulse mode
  lay->addWidget(bar);
  return bar;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr, "REFUSING: offscreen has no style animation to observe.\n");
    return 2;
  }
  const int secs = (argc > 1) ? QString(argv[1]).toInt() : 20;
  printf("style: %s   platform: %s\n",
         qPrintable(app.style()->objectName()), qPrintable(app.platformName()));

  // 1 - enabled window
  auto* w1 = new QWidget;
  w1->setWindowTitle("1: enabled window");
  pulseBar(w1, "should animate");
  w1->resize(420, 90);
  w1->move(80, 80);
  w1->show();

  // 2 - window disabled after show
  auto* w2 = new QWidget;
  w2->setWindowTitle("2: window itself disabled");
  pulseBar(w2, "disabled window");
  w2->resize(420, 90);
  w2->move(80, 220);
  w2->show();
  w2->setEnabled(false);

  // 3 - dialog whose PARENT is disabled: what TTCut does
  auto* parent = new QWidget;
  parent->setWindowTitle("3: parent (disabled) - like TTCutMainWindow");
  parent->resize(420, 90);
  parent->move(80, 360);
  parent->show();
  // TTCut's ORDER matters and is reproduced here: the dialog is constructed
  // in the Init branch, the main window is disabled immediately after, and
  // only a later Start actually shows the dialog.
  auto* dlg = new QDialog(parent);          // same construction as TTProgressBar
  dlg->setWindowTitle("3: dialog, parent disabled");
  QProgressBar* bar3 = pulseBar(dlg, "parent disabled - TTCut's shape");
  auto* btn = new QPushButton("Cancel (clickable?)", dlg);
  dlg->layout()->addWidget(btn);
  QObject::connect(btn, &QPushButton::clicked,
                   [] { printf("  >>> BUTTON CLICKED - dialog is interactive\n"); });
  dlg->resize(420, 130);
  dlg->move(560, 360);
  parent->setEnabled(false);                // exactly what the Init branch does
  dlg->setModal(false);
  dlg->show();                              // shown AFTER the parent was disabled

  // 4 - dialog with a DISABLED parent, but explicitly re-enabled: what
  // TTProgressBar ends up in, because resetForNewOperation() calls
  // this->setEnabled(true) on itself.
  auto* parent2 = new QWidget;
  parent2->setWindowTitle("4: parent (disabled)");
  parent2->resize(420, 60);
  parent2->move(80, 500);
  parent2->show();
  auto* dlg2 = new QDialog(parent2);
  dlg2->setWindowTitle("4: dialog re-enabled, parent disabled");
  QProgressBar* bar4 = pulseBar(dlg2, "re-enabled - does it animate?");
  dlg2->resize(420, 90);
  dlg2->move(560, 500);
  parent2->setEnabled(false);
  dlg2->show();
  dlg2->setEnabled(true);        // like resetForNewOperation()

  // 5 - the state TTCut is actually in: range 0..0 AND value == -1, which is
  // what reset() leaves behind (value = minimum - 1). Measured in a real run:
  // indet=1 barEnabled=1 visible=1 value=-1, and still no movement - while
  // case 1 and 4 above do move. The only difference left is this value.
  auto* w5 = new QWidget;
  w5->setWindowTitle("5: range 0..0 AND value=-1 (after reset) - TTCut's real state");
  auto* bar5 = pulseBar(w5, "value=-1 after reset()");
  bar5->reset();                 // value becomes minimum-1 == -1
  bar5->setRange(0, 0);
  w5->resize(420, 90);
  w5->move(560, 220);
  w5->show();

  // 6 - same, but value forced to 0 afterwards: the candidate fix
  auto* w6 = new QWidget;
  w6->setWindowTitle("6: range 0..0, value forced to 0");
  auto* bar6 = pulseBar(w6, "value=0");
  bar6->setRange(0, 0);
  bar6->setValue(0);
  w6->resize(420, 90);
  w6->move(560, 80);
  w6->show();

  QTimer report;
  QObject::connect(&report, &QTimer::timeout, [dlg, dlg2, bar5, bar6] {
    printf("  (3) dlg=%d   (4) dlg=%d   (5) value=%d   (6) value=%d\n",
           dlg->isEnabled(), dlg2->isEnabled(), bar5->value(), bar6->value());
  });
  report.start(2000);

  QTimer::singleShot(secs * 1000, qApp, &QCoreApplication::quit);
  printf("\nWatch the four bars for %d s. Which ones move?\n\n", secs);
  return app.exec();
}
