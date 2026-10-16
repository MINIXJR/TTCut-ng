// Gate for the logo area (TODO.md, reading findings of the full map check
// 2026-10-11, "Logo-Auswahl 4x4"). The main window reported "Logo profile
// created" whenever it had decoded a frame, whether or not TTLogoDetector
// accepted the area - a 4x4 area or one outside the picture left the search
// buttons on and a search click did nothing, without a word. In the real main
// window with the Tux H.264 project:
//   - an area is reported as a profile exactly when the detector built one,
//     and the logo search then starts;
//   - a rejected area says so, leaves the search buttons off and a search
//     click starts nothing;
//   - a drag in the picture emits only areas the detector accepts, at any
//     window size, and the area it emits becomes a profile.
//
//   usage: test_logo_roi <project.ttcut, 1920x1080 video>
//
// Build via `cmake --build build --target test_logo_roi`.
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>

#include <clocale>
#include <cstdio>

#include "common/ttsettings.h"
#include "data/ttlogodetector.h"
#include "gui/ttcurrentframe.h"
#include "gui/ttcutmainwindow.h"
#include "mpeg2window/ttmpeg2window2.h"

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

static void sendMouse(QWidget* w, QEvent::Type type, const QPoint& p)
{
  const Qt::MouseButtons held = (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent ev(type, QPointF(p), QPointF(w->mapToGlobal(p)), Qt::LeftButton, held, Qt::NoModifier);
  QApplication::sendEvent(w, &ev);
}

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
  auto* prevLogo = w.findChild<QPushButton*>("pbPrevLogo");
  auto* current  = w.findChild<TTCurrentFrame*>();
  if (!prevLogo || !current) { fprintf(stderr, "main window widgets not found\n"); return 2; }
  TTMPEG2Window2* video = current->videoWindow();

  w.openProjectFile(QString::fromLocal8Bit(argv[1]));
  // Ready = the window is enabled again and stays so (it is disabled while
  // the project loads, see test_mainwindow_then_cut).
  {
    QElapsedTimer t;
    t.start();
    qint64 since = -1;
    while (t.elapsed() < 120000) {
      spin(50);
      if (w.isEnabled()) { if (since < 0) since = t.elapsed(); else if (t.elapsed() - since > 2000) break; }
      else since = -1;
    }
  }

  // ---- the main window's verdict on an area --------------------------------
  struct Case { QRect roi; bool profile; };
  const int m = TTLogoDetector::kMinRoiSide;
  const QList<Case> cases = {
    { QRect(1600, 40, 200, 120), true  },
    { QRect(1600, 40, m, m),     true  },
    { QRect(1600, 40, m - 1, m - 1), false },
    { QRect(5000, 5000, 50, 50), false },   // e.g. a project saved for another resolution
  };
  for (const Case& c : cases) {
    w.onCancelLogoROI();
    w.onLogoROISelected(c.roi);
    const QString area = QString("%1,%2 %3x%4").arg(c.roi.x()).arg(c.roi.y())
                             .arg(c.roi.width()).arg(c.roi.height());
    const QString status = w.statusBar()->currentMessage();
    const bool created = status.startsWith("Logo profile created");
    check(created == c.profile && prevLogo->isEnabled() == c.profile,
          QString("area %1: status \"%2\", search buttons %3 - expected %4")
              .arg(area, status, prevLogo->isEnabled() ? "on" : "off",
                   c.profile ? "profile, buttons on" : "no profile, buttons off"));

    w.statusBar()->clearMessage();
    w.onSearchLogo(0, 1, 0.5f);
    const bool started = w.statusBar()->currentMessage().startsWith("Searching logo change");
    check(started == c.profile,
          QString("area %1: search click %2").arg(area, started ? "starts a search" : "starts nothing"));
    w.onAbortLogoSearch();
    spin(2000);
  }
  w.onCancelLogoROI();

  // ---- the drag in the picture ---------------------------------------------
  // One widget pixel is several picture pixels, so which picture sizes a drag
  // can produce depends on the window size. For a range of window widths:
  // grow the drag from a click until the widget emits; what it emits must be
  // usable and become a profile. At some of these widths the old limit (4)
  // was hit and a 4x4 area emitted (red run 2026-10-16).
  QList<QRect> emitted;
  QObject::connect(video, &TTMPEG2Window2::logoROISelected,
                   [&emitted](QRect r) { emitted.append(r); });
  QStringList seen;
  bool allUsable = true, allProfiles = true;
  for (int width = 800; width <= 2400; width += 100) {
    w.onCancelLogoROI();
    w.resize(width, 1000);
    spin(300);
    emitted.clear();
    const QPoint origin = video->rect().center();
    video->setLogoSelectionMode(true);
    for (int d = 0; d <= 40 && emitted.isEmpty(); ++d) {
      sendMouse(video, QEvent::MouseButtonPress, origin);
      sendMouse(video, QEvent::MouseMove, origin + QPoint(d, d));
      sendMouse(video, QEvent::MouseButtonRelease, origin + QPoint(d, d));
    }
    spin(500);
    if (emitted.size() != 1) { allUsable = false; seen << QString("%1:none").arg(width); continue; }
    const QRect r = emitted.first();
    seen << QString("%1:%2x%3").arg(width).arg(r.width()).arg(r.height());
    if (r.width() < m || r.height() < m) allUsable = false;
    if (!w.statusBar()->currentMessage().startsWith("Logo profile created") || !prevLogo->isEnabled())
      allProfiles = false;
  }
  printf("window width : first emitted area  %s\n", qPrintable(seen.join("  ")));
  check(allUsable, "at every window width the first emitted area has both sides >= kMinRoiSide");
  check(allProfiles, "at every window width the emitted area becomes a profile");

  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
