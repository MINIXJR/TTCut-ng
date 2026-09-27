// Why does the indeterminate ("pulse") QProgressBar not animate in TTCut-ng -
// and which remedy actually restores the animation?
//
// Background: TTProgressBar switches to setRange(0, 0) after a stall. Reported
// from the GUI: static light/dark stripes, no movement, while the dialog's 1 s
// debug clock keeps counting - so the event loop is alive. test_pulse_animation
// showed that every plausible widget state (disabled parent, value == -1,
// re-enabled dialog) still animates. The difference left between that probe and
// the real application is gui/ttcutmain.cpp:
//
//     a.setStyleSheet(a.styleSheet() +
//                     "\nQGroupBox::title { subcontrol-position: top center; }");
//
// Setting ANY stylesheet on the QApplication wraps the platform style in
// QStyleSheetStyle for every widget, not just for QGroupBox.
//
// This probe does not ask anyone to watch a bar: it counts QEvent::Paint on the
// bar. An animating busy indicator repaints continuously (tens of paints per
// second); a static one paints once and then never again.
//
//   usage: test_pulse_stylesheet [seconds]          (default 5)
//          PULSE_MODE=none|sheet|chunk|real         (default none)
//          PULSE_GRAB=/path/to.png                  (screenshot for alignment)
//
// Modes:
//   none   no stylesheet                      - the reference: does it animate?
//   sheet  exactly what ttcutmain.cpp sets    - the reported defect
//   chunk  sheet + an explicit QProgressBar::chunk rule
//          - does styling the chunk bring the animation back?
//   real   the shipped fix: gui/TTCentredTitleStyle::install()
//          - measures production code, not a copy of it
//
// The window also holds a QGroupBox, so PULSE_GRAB can prove that "real"
// really centres the title - a fix that animates but loses the centring would
// be no fix.
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QObject>
#include <QProgressBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "gui/ttcentredtitlestyle.h"

#include <cstdio>

// Counts paint events on one widget. Nothing else - an event filter that
// swallowed or altered events would change what is being measured.
class PaintCounter : public QObject
{
public:
  int count = 0;

protected:
  bool eventFilter(QObject* obj, QEvent* ev) override
  {
    if (ev->type() == QEvent::Paint)
      ++count;
    return QObject::eventFilter(obj, ev);
  }
};

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);

  const QByteArray mode = qgetenv("PULSE_MODE").isEmpty() ? QByteArray("none")
                                                          : qgetenv("PULSE_MODE");

  QApplication app(argc, argv);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr, "REFUSING: offscreen has no style animation to observe.\n");
    return 2;
  }

  if (mode == "sheet") {
    // Byte-for-byte what gui/ttcutmain.cpp does.
    app.setStyleSheet(app.styleSheet() +
                      "\nQGroupBox::title { subcontrol-position: top center; }");
  } else if (mode == "chunk") {
    app.setStyleSheet(app.styleSheet() +
                      "\nQGroupBox::title { subcontrol-position: top center; }"
                      "\nQProgressBar { border: 1px solid grey; text-align: center; }"
                      "\nQProgressBar::chunk { background-color: #3daee9; width: 20px;"
                      " margin: 1px; }");
  } else if (mode == "real") {
    TTCentredTitleStyle::install();
    printf("  installed TTCentredTitleStyle, style now: '%s'\n",
           qPrintable(app.style()->objectName()));
  } else if (mode != "none") {
    fprintf(stderr, "unknown PULSE_MODE=%s\n", mode.constData());
    return 2;
  }

  const int secs = (argc > 1) ? QString(argv[1]).toInt() : 5;

  printf("mode: %-6s   style: %-10s platform: %s\n",
         mode.constData(), qPrintable(app.style()->objectName()),
         qPrintable(app.platformName()));

  auto* w = new QWidget;
  w->setWindowTitle(QString("pulse mode=%1").arg(QString(mode)));
  auto* lay = new QVBoxLayout(w);

  auto* box = new QGroupBox("Group title - centred?", w);
  auto* boxLay = new QVBoxLayout(box);
  boxLay->addWidget(new QLabel("title alignment check", box));
  lay->addWidget(box);

  auto* bar = new QProgressBar(w);
  bar->setRange(0, 0);                  // pulse mode, same as TTProgressBar
  lay->addWidget(bar);
  w->resize(420, 140);
  w->move(200, 200);

  PaintCounter counter;
  bar->installEventFilter(&counter);
  w->show();

  QElapsedTimer clock;
  clock.start();

  QTimer::singleShot(secs * 1000, qApp, [&] {
    const double rate = counter.count / double(clock.elapsed()) * 1000.0;
    printf("RESULT mode=%-6s %4d paints in %lld ms = %5.1f paints/s -> %s\n",
           mode.constData(), counter.count,
           static_cast<long long>(clock.elapsed()), rate,
           rate > 5.0 ? "ANIMATING" : "STATIC");

    const QByteArray grab = qgetenv("PULSE_GRAB");
    if (!grab.isEmpty()) {
      if (w->grab().save(QString::fromLocal8Bit(grab)))
        printf("  screenshot: %s\n", grab.constData());
      else
        printf("  screenshot FAILED: %s\n", grab.constData());
    }
    qApp->quit();
  });

  return app.exec();
}
