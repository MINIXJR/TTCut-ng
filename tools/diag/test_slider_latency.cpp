// How long after a slider action does the picture change? Measured in the real
// main window with a loaded project, the slider driven the way a mouse drives
// it (TODO.md, "H.265-UHD: Schieber reagiert träge").
//
// "The picture changed" = the first Paint of the current-frame window
// (TTMPEG2Window2, a QLabel) whose pixmap cacheKey differs from the one shown
// before the action. A 10 ms heartbeat timer records how long the GUI thread
// could not run its event loop (the window cannot repaint in that time).
//
// Scenarios:
//   click  - triggerAction(SliderPageStepAdd), what a click into the groove
//            does (page step = TTSettings::stepSliderClick)
//   jump   - setValue() far away, slider not held
//   hold   - left mouse button pressed into the groove right of the handle,
//            held 3 s (the slider repeats its page step), released
//   wheel  - mouse wheel over the current-frame widget: single notches, and
//            five notches queued at once (they arrive while a decode runs)
//   drag   - setSliderDown(true), value raised every 20 ms for 3 s, then
//            setSliderDown(false) (emits sliderReleased)
//
//   usage: test_slider_latency <project.ttcut> [fastSlider 0|1] [pageStep] [gate]
//          defaults 1 and 40 (the user's settings on 2026-10-17)
//   gate:  one drag only, with a verdict: at least 6 new pictures while the
//          handle is held for 3 s (one per 500 ms). Before the drag preview
//          was paced (2026-10-17), the debounce restarted on every move and a
//          continuous drag showed none.
//   gatehold: one groove hold only, same verdict. The groove does not set
//          isSliderDown(), so the drag fix alone did not reach it.
//   gatewheel: five wheel notches queued at once must reach the target
//          with at most 3 reported positions. Before (2026-10-17) every
//          notch was decoded exactly, one after the other. FastSlider off:
//          the target is the exact frame. FastSlider on: the target is an
//          I-frame (forward the first at/after, backward the last at/before),
//          and three single notches each way must each report exactly that
//          I-frame - before 2026-10-18 a notch showed the keyframe, then the
//          exact frame. FastSlider off on cheap material (Tux H.264): three
//          single notches each way must each report exactly the exact frame.
//   gateclick: five page-step clicks and five jumps, median time to the new
//          picture at most 300 ms. Meant for FastSlider on and Tux H.265 4K,
//          where the exact decode of the I-frame took ~600-750 ms before the
//          fast keyframe decode served the slider (2026-10-18).
//
// Build via `cmake --build build --target test_slider_latency`.
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPixmap>
#include <QStyle>
#include <QSlider>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <clocale>
#include <unistd.h>
#include <cstdio>
#include <vector>

#include "common/ttsettings.h"
#include "gui/ttcurrentframe.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttstreamnavigator.h"
#include "mpeg2window/ttmpeg2window2.h"

namespace {

QElapsedTimer gClock;

void spin(int ms)
{
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Every Paint of the frame window that shows a different pixmap than the
// previous one.
struct PictureSpy : QObject
{
  TTMPEG2Window2* win = nullptr;
  qint64 lastKey = 0;
  std::vector<qint64> changes;   // ms on gClock
  bool eventFilter(QObject*, QEvent* e) override
  {
    if (e->type() == QEvent::Paint) {
      const qint64 key = win->pixmap().cacheKey();
      if (key != lastKey) { lastKey = key; changes.push_back(gClock.elapsed()); }
    }
    return false;
  }
};

// Gaps between heartbeat ticks = time the GUI thread was busy.
struct Heartbeat
{
  QTimer timer;
  qint64 last = -1;
  std::vector<qint64> gaps;
  void start()
  {
    timer.setInterval(10);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [this] {
      const qint64 now = gClock.elapsed();
      if (last >= 0) gaps.push_back(now - last);
      last = now;
    });
    timer.start();
  }
  // The first gap counts from the reset, so a decode right after it is seen.
  void reset() { gaps.clear(); last = gClock.elapsed(); }
  qint64 maxGap() const { return gaps.empty() ? 0 : *std::max_element(gaps.begin(), gaps.end()); }
  qint64 frozen() const   // time beyond the 10 ms beat in gaps over 50 ms
  {
    qint64 sum = 0;
    for (qint64 g : gaps) if (g > 50) sum += g - 10;
    return sum;
  }
};

// Wait until the picture changes after `since` (or timeout), then settle.
qint64 waitForChange(PictureSpy& spy, size_t before, qint64 since, int timeoutMs)
{
  QElapsedTimer t;
  t.start();
  while (spy.changes.size() == before && t.elapsed() < timeoutMs) spin(5);
  if (spy.changes.size() == before) return -1;
  return spy.changes[before] - since;
}

void printStats(const char* what, std::vector<qint64> v)
{
  if (v.empty()) { printf("  %-28s n=0\n", what); return; }
  std::sort(v.begin(), v.end());
  printf("  %-28s n=%zu  min %lld  median %lld  max %lld ms\n", what, v.size(),
         (long long)v.front(), (long long)v[v.size() / 2], (long long)v.back());
}

} // namespace

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start otherwise, as in main()
  app.setApplicationName("TTCut-ng");
  app.setOrganizationName("TTCut-ng");
  if (argc < 2) {
    fprintf(stderr, "usage: %s <project.ttcut> [fastSlider 0|1] [pageStep]\n", argv[0]);
    return 2;
  }
  const bool fast = (argc > 2) ? QString(argv[2]).toInt() != 0 : true;
  const int  page = (argc > 3) ? QString(argv[3]).toInt() : 40;
  const QString mode = (argc > 4) ? QString(argv[4]) : QString();
  const bool gateDrag = (mode == "gate");
  const bool gateHold = (mode == "gatehold");
  const bool gateWheel = (mode == "gatewheel");
  const bool gateClick = (mode == "gateclick");
  const bool gate = gateDrag || gateHold || gateWheel || gateClick;
  // Measurement only: notches at a steady pace, as when turning the wheel
  // notch by notch. Runs nothing else.
  const bool wheelSteady = (mode == "wheelsteady");
  // The last frame, as the End key shows it (TTCurrentFrame::onMoveToEnd).
  // A watchdog ends the process if the decode does not come back.
  const bool gateEnd = (mode == "gateend");
  // Behind the last I-frame with FastSlider on: the End key with the keyboard
  // focus on the slider (the slider handles it itself, SliderToMaximum) and a
  // wheel notch from the I-frame before the last one must both land on the
  // last I-frame. Before 2026-10-18 neither moved: there is no I-frame
  // at/after their target.
  const bool gateSliderEnd = (mode == "gatesliderend");
  TTSettings::instance()->setFastSlider(fast);
  TTSettings::instance()->setStepSliderClick(page);

  TTCutMainWindow w;
  w.resize(1600, 1000);
  w.show();
  spin(300);
  auto* nav = w.findChild<TTStreamNavigator*>("streamNavigator");
  auto* cur = w.findChild<TTCurrentFrame*>("currentFrame");
  if (!nav || !cur || !cur->videoWindow()) { fprintf(stderr, "main window widgets not found\n"); return 2; }
  QSlider* slider = qobject_cast<QSlider*>(nav->slider());
  if (!slider) { fprintf(stderr, "slider not found\n"); return 2; }

  gClock.start();
  PictureSpy spy;
  spy.win = cur->videoWindow();
  spy.win->installEventFilter(&spy);

  // Position the current-frame widget reports for what it shows.
  int shownPos = -1;
  std::vector<std::pair<qint64, int>> shownLog;   // every reported position
  // SLIDER_TRACE=1: every slider value and reported position, as they come
  // (finds the step a hang happens at).
  const bool trace = qEnvironmentVariableIntValue("SLIDER_TRACE") == 1;
  QObject::connect(cur, &TTCurrentFrame::newFramePosition, [&](int p) {
    shownPos = p;
    shownLog.push_back({ gClock.elapsed(), p });
    if (trace) fprintf(stderr, "trace %lld shown %d\n", (long long)gClock.elapsed(), p);
  });
  if (trace)
    QObject::connect(slider, &QAbstractSlider::valueChanged, [&](int v) {
      fprintf(stderr, "trace %lld value %d down %d\n", (long long)gClock.elapsed(), v,
              slider->isSliderDown());
    });

  w.openProjectFile(QString::fromLocal8Bit(argv[1]));
  QElapsedTimer t;
  t.start();
  while ((slider->maximum() <= 0 || spy.changes.empty()) && t.elapsed() < 120000) spin(50);
  if (slider->maximum() <= 0 || spy.changes.empty()) { fprintf(stderr, "project did not load\n"); return 2; }
  printf("style %s\n", qPrintable(QApplication::style()->name()));
  printf("loaded in %lld ms, %d frames, fastSlider=%d pageStep=%d\n",
         (long long)t.elapsed(), slider->maximum() + 1, fast, page);
  spin(3000);

  Heartbeat beat;
  beat.start();
  const int max = slider->maximum();

  if (gateSliderEnd) {
    // Oracle first, on the frame widget's own navigation: the last I-frame
    // and the one before it.
    cur->onMoveToEnd();
    cur->onPrevIFrame();
    int lastI = shownPos;
    cur->onNextIFrame();             // the last frame may itself be an I-frame
    if (shownPos != lastI && shownPos == max) lastI = max;
    cur->onGotoFrame(lastI, 0);
    cur->onPrevIFrame();
    const int prevI = shownPos;

    slider->setValue(max / 2);
    spin(2000);
    const int from = shownPos;
    slider->setFocus();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_End, Qt::NoModifier);
    QApplication::sendEvent(slider, &press);
    QApplication::sendEvent(slider, &release);
    spin(3000);
    printf("slider End: from %d, slider %d, shown %d, last I-frame %d, last frame %d\n",
           from, slider->value(), shownPos, lastI, max);
    const bool endOk = shownPos == lastI;

    cur->onGotoFrame(prevI, 0);
    spin(500);
    const QPointF p(cur->width() / 2, cur->height() / 2);
    QWheelEvent notch(p, cur->mapToGlobal(p), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(cur, &notch);
    spin(1500);
    printf("wheel forward from %d: shown %d\n", prevI, shownPos);
    const bool wheelOk = shownPos == lastI;

    if (!endOk) printf("FAIL: the End key on the slider did not reach the last I-frame %d\n", lastI);
    if (!wheelOk) printf("FAIL: the wheel notch did not reach the last I-frame %d\n", lastI);
    if (!endOk || !wheelOk) return 1;
    printf("PASS\n");
    return 0;
  }
  if (gateEnd) {
    std::atomic<bool> back{false};
    std::thread watchdog([&] {
      for (int k = 0; k < 200 && !back; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!back) {
        printf("FAIL: showing the last frame %d did not come back within 20 s\n", max);
        fflush(stdout);
        _exit(1);
      }
    });
    const qint64 t0 = gClock.elapsed();
    cur->onMoveToEnd();
    back = true;
    watchdog.join();
    printf("end: shown %d of %d after %lld ms\n", shownPos, max, (long long)(gClock.elapsed() - t0));
    if (shownPos != max) { printf("FAIL: shown %d, expected the last frame %d\n", shownPos, max); return 1; }
    printf("PASS\n");
    return 0;
  }
  const int wheelStep = TTSettings::instance()->stepMouseWheel();

  // Where a wheel notch from `from` should land, taken through the widget's
  // own navigation (it moves the stream; the caller goes back afterwards).
  // FastSlider on: the I-frame at/after resp. at/before the target. Asked
  // only after the notches: it decodes the target, and the frame cache would
  // then hand the wheel a picture it never decoded.
  auto notchTarget = [&](int from, bool forward) {
    const int t = forward ? from + wheelStep : from - wheelStep;
    if (!fast) return t;
    if (forward) { cur->onGotoFrame(t, 1); return shownPos; }
    cur->onGotoFrame(t + 1, 0);
    cur->onPrevIFrame();
    return shownPos;
  };
  auto chainedTarget = [&](int from, int notches) {
    const int back = shownPos;
    int t = from;
    for (int k = 0; k < notches; ++k) t = notchTarget(t, true);
    cur->onGotoFrame(back, 0);
    spin(300);
    return t;
  };
  // Time (on gClock) of the first report of `pos` in log entries [first, end).
  auto reportedAt = [&](size_t first, size_t end, int pos) -> qint64 {
    for (size_t k = first; k < end; ++k)
      if (shownLog[k].second == pos) return shownLog[k].first;
    return -1;
  };

  if (wheelSteady) {
    int round = 0;
    for (int interval : { 250, 400, 700 }) {
      // Three separate places, each with room for the eight notches.
      const int room = max - 8 * wheelStep - 10;
      slider->setValue(room * ++round / 4);
      spin(3000);
      const int from = shownPos;
      const int notches = 8;
      const size_t before = spy.changes.size();
      const size_t logBefore = shownLog.size();
      beat.reset();
      const qint64 t0 = gClock.elapsed();
      // A thread posts the notches at fixed wall times: the window system
      // queues wheel events while the GUI thread is busy decoding.
      std::atomic<bool> posted{false};
      std::thread poster([&] {
        for (int k = 0; k < notches; ++k) {
          if (k) std::this_thread::sleep_for(std::chrono::milliseconds(interval));
          const QPointF p(cur->width() / 2, cur->height() / 2);
          QCoreApplication::postEvent(cur, new QWheelEvent(p, p, QPoint(), QPoint(0, -120),
              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false));
        }
        posted = true;
      });
      // The GUI thread keeps running its event loop meanwhile; a join here
      // would hold it and queue every notch.
      while (!posted) spin(5);
      poster.join();
      // Until the reports stop (a frozen window reports nothing either).
      size_t seen = 0;
      while (gClock.elapsed() - t0 < 60000) {
        seen = shownLog.size();
        spin(2500);
        if (shownLog.size() == seen) break;
      }
      const size_t pictures = spy.changes.size() - before;
      const qint64 frozen = beat.frozen(), longest = beat.maxGap();
      const size_t logEnd = shownLog.size();
      const int target = chainedTarget(from, notches);
      const qint64 at = reportedAt(logBefore, logEnd, target);
      const qint64 reached = (at < 0) ? -1 : at - t0;
      QString steps;
      for (size_t k = logBefore; k < logEnd; ++k)
        steps += QString(" %1@%2").arg(shownLog[k].second - from).arg(shownLog[k].first - t0);
      printf("wheel steady %d ms x %d: last notch at %d ms, target after %lld ms "
             "(%lld ms after the last notch), %zu picture(s), GUI frozen %lld ms, longest %lld ms\n"
             "  shown (offset@ms):%s\n",
             interval, notches, interval * (notches - 1), (long long)reached,
             (long long)(reached - interval * (notches - 1)), pictures,
             (long long)frozen, (long long)longest, qPrintable(steps));
    }
    printf("MEASURED (no verdict)\n");
    return 0;
  }

  // --- click: page step ------------------------------------------------------
  std::vector<qint64> clickJumpLat;   // gateclick: both scenarios
  if (!gate || gateClick) {
    std::vector<qint64> lat, freeze;
    slider->setValue(max / 5);
    waitForChange(spy, spy.changes.size(), gClock.elapsed(), 15000);
    spin(1000);
    for (int i = 0; i < (gate ? 5 : 10); ++i) {
      beat.reset();
      const size_t before = spy.changes.size();
      const qint64 t0 = gClock.elapsed();
      slider->triggerAction(QAbstractSlider::SliderPageStepAdd);
      const qint64 l = waitForChange(spy, before, t0, 15000);
      spin(800);
      lat.push_back(l);
      clickJumpLat.push_back(l);
      freeze.push_back(beat.maxGap());
      printf("  click %2d -> %d: picture after %lld ms, %zu picture(s), longest freeze %lld ms\n",
             i + 1, slider->value(), (long long)l, spy.changes.size() - before, (long long)beat.maxGap());
    }
    printf("click (page step %d):\n", page);
    printStats("picture changed after", lat);
    printStats("longest GUI freeze", freeze);
  }

  // --- jump: far away, not held ------------------------------------------------
  if (!gate || gateClick) {
    std::vector<qint64> lat, freeze;
    for (int i = 0; i < (gate ? 5 : 10); ++i) {
      beat.reset();
      const int target = int((qint64(max) * ((i * 37) % 97 + 1)) / 99);
      const size_t before = spy.changes.size();
      const qint64 t0 = gClock.elapsed();
      slider->setValue(target);
      const qint64 l = waitForChange(spy, before, t0, 15000);
      spin(800);
      lat.push_back(l);
      clickJumpLat.push_back(l);
      freeze.push_back(beat.maxGap());
      printf("  jump  %2d -> %d: picture after %lld ms, %zu picture(s), longest freeze %lld ms\n",
             i + 1, target, (long long)l, spy.changes.size() - before, (long long)beat.maxGap());
    }
    printf("jump:\n");
    printStats("picture changed after", lat);
    printStats("longest GUI freeze", freeze);
  }

  // --- mouse click: press into the groove, release after 100 ms ----------------
  // A human click lasts longer than the 50 ms timer; it must still land where
  // a click lands (shown = what moveToIndexPos(value, fastSlider) gives), not
  // on the keyframe preview of a hold.
  if (!gate) {
    std::vector<qint64> lat;
    for (int i = 0; i < 5; ++i) {
      slider->setValue(max / 2 + i * (max / 20));
      spin(3000);
      const int x = int(double(slider->value()) / max * slider->width()) + slider->width() / 8;
      const QPointF p(x, slider->height() / 2);
      const int startVal = slider->value();
      const size_t before = spy.changes.size();
      QMouseEvent press(QEvent::MouseButtonPress, p, slider->mapToGlobal(p),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, p, slider->mapToGlobal(p),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      const qint64 t0 = gClock.elapsed();
      QApplication::sendEvent(slider, &press);
      spin(100);
      QApplication::sendEvent(slider, &release);
      const qint64 l = waitForChange(spy, before, t0, 5000);
      spin(1500);
      lat.push_back(l);
      printf("  mouse click %d: value %d -> %d, picture after %lld ms, %zu picture(s), shown %d\n",
             i + 1, startVal, slider->value(), (long long)l, spy.changes.size() - before, shownPos);
    }
    printf("mouse click (100 ms):\n");
    printStats("picture changed after", lat);
  }

  // --- hold: press into the groove, hold 3 s, release -------------------------
  size_t picturesHold = 0;
  if (!gate || gateHold) for (int round = 0; round < (gate ? 1 : 2); ++round) {
    slider->setValue(max / 3 + round * (max / 5));
    spin(3000);
    // An eighth of the groove right of the handle: far enough that the
    // repeated page steps do not reach the mouse within 3 s.
    const int x = int(double(slider->value()) / max * slider->width()) + slider->width() / 8;
    const QPointF p(x, slider->height() / 2);
    const int startVal = slider->value();
    beat.reset();
    const size_t before = spy.changes.size();
    QMouseEvent press(QEvent::MouseButtonPress, p, slider->mapToGlobal(p),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    const qint64 t0 = gClock.elapsed();
    QApplication::sendEvent(slider, &press);
    while (gClock.elapsed() - t0 < 3000) spin(5);
    const int heldVal = slider->value();
    std::vector<qint64> during(spy.changes.begin() + before, spy.changes.end());
    picturesHold = during.size();
    std::vector<qint64> intervals;
    for (size_t k = 1; k < during.size(); ++k) intervals.push_back(during[k] - during[k - 1]);
    printf("hold %d: value %d -> %d in 3 s, %zu new pictures while held (first after %lld ms), "
           "GUI frozen %lld ms in total, longest %lld ms\n",
           round + 1, startVal, heldVal, during.size(),
           during.empty() ? -1LL : (long long)(during.front() - t0),
           (long long)beat.frozen(), (long long)beat.maxGap());
    printStats("  interval between pictures", intervals);
    beat.reset();
    const size_t beforeRel = spy.changes.size();
    QMouseEvent release(QEvent::MouseButtonRelease, p, slider->mapToGlobal(p),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    const qint64 tRel = gClock.elapsed();
    QApplication::sendEvent(slider, &release);
    const qint64 l = waitForChange(spy, beforeRel, tRel, 5000);
    spin(1500);
    printf("  release: picture after %lld ms, %zu picture(s), longest freeze %lld ms, slider %d, shown %d\n",
           (long long)l, spy.changes.size() - beforeRel, (long long)beat.maxGap(), slider->value(), shownPos);
  }

  // --- wheel over the current frame ------------------------------------------
  int wheelReports = -1, wheelFinal = -1, wheelTarget = -2;
  int wheelSingleFails = 0;
  if (!gate || gateWheel) {
    auto wheel = [&](bool post, bool forward = true) {
      const QPointF p(cur->width() / 2, cur->height() / 2);
      // angleDelta y < 0: towards the user = forward in the stream
      auto* e = new QWheelEvent(p, cur->mapToGlobal(p), QPoint(), QPoint(0, forward ? -120 : 120),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      if (post) QCoreApplication::postEvent(cur, e);
      else { QApplication::sendEvent(cur, e); delete e; }
    };
    std::vector<qint64> lat;
    slider->setValue(max / 2);
    spin(3000);
    for (int i = 0; i < (gate ? 0 : 5); ++i) {
      beat.reset();
      const size_t before = spy.changes.size();
      const int from = shownPos;
      const size_t logBefore = shownLog.size();
      const qint64 t0 = gClock.elapsed();
      wheel(true);
      const qint64 l = waitForChange(spy, before, t0, 5000);
      spin(2500);
      lat.push_back(l);
      QString steps;
      for (size_t k = logBefore; k < shownLog.size(); ++k)
        steps += QString(" %1@%2ms").arg(shownLog[k].second).arg(shownLog[k].first - t0);
      QString pics;
      for (size_t k = before; k < spy.changes.size(); ++k)
        pics += QString(" %1ms").arg(spy.changes[k] - t0);
      printf("  wheel notch %d: %d -> %d, first picture after %lld ms, longest freeze %lld ms; "
             "positions:%s; pictures:%s\n",
             i + 1, from, shownPos, (long long)l, (long long)beat.maxGap(),
             qPrintable(steps), qPrintable(pics));
    }
    if (!gate) {
      printf("wheel, single notch:\n");
      printStats("picture changed after", lat);
    }
    // One notch = one report: FastSlider on the expected I-frame, off the
    // exact frame (cheap material only: on UHD a notch previews first).
    for (int i = 0; i < (gateWheel ? 6 : 0); ++i) {
      const bool forward = i < 3;
      spin(500);
      const int from = shownPos;
      const size_t logBefore = shownLog.size();
      const qint64 t0 = gClock.elapsed();
      wheel(true, forward);
      spin(1500);
      QString steps;
      for (size_t k = logBefore; k < shownLog.size(); ++k)
        steps += QString(" %1@%2ms").arg(shownLog[k].second).arg(shownLog[k].first - t0);
      const bool once = shownLog.size() == logBefore + 1;
      const int got = shownPos;
      const int expected = notchTarget(from, forward);
      cur->onGotoFrame(got, 0);
      const bool ok = once && got == expected;
      if (!ok) ++wheelSingleFails;
      printf("  wheel notch %s %d: %d -> %d, expected %d; positions:%s%s\n",
             forward ? "forward" : "back", i % 3 + 1, from, got, expected,
             qPrintable(steps), ok ? "" : "  <- wrong");
    }
    for (int round = 0; round < (gate ? 1 : 2); ++round) {
      spin(1500);
      beat.reset();
      const size_t before = spy.changes.size();
      const int from = shownPos;
      const size_t logBefore = shownLog.size();
      const qint64 t0 = gClock.elapsed();
      for (int k = 0; k < 5; ++k) wheel(true);
      size_t seen = 0;
      while (gClock.elapsed() - t0 < 30000) {
        seen = shownLog.size();
        spin(1500);
        if (shownLog.size() == seen) break;
      }
      wheelReports = int(shownLog.size() - logBefore);
      wheelFinal   = shownPos;
      const size_t pictures = spy.changes.size() - before;
      const qint64 frozen = beat.frozen(), longest = beat.maxGap();
      const size_t logEnd = shownLog.size();
      const int target = chainedTarget(from, 5);
      const qint64 at = reportedAt(logBefore, logEnd, target);
      const qint64 reached = (at < 0) ? -1 : at - t0;
      wheelTarget  = target;
      QString steps;
      for (size_t k = logBefore; k < logEnd; ++k)
        steps += QString(" %1@%2ms").arg(shownLog[k].second).arg(shownLog[k].first - t0);
      printf("wheel, 5 notches at once %d: %d -> %d (target %d) reached after %lld ms, %zu picture(s), "
             "GUI frozen %lld ms in total, longest %lld ms; positions shown:%s\n",
             round + 1, from, wheelFinal, target, (long long)reached, pictures,
             (long long)frozen, (long long)longest, qPrintable(steps));
    }
  }

  // --- drag: hold, move for 3 s, release ---------------------------------------
  size_t picturesHeld = 0;
  if (!gate || gateDrag) for (int round = 0; round < (gate ? 1 : 3); ++round) {
    slider->setValue(max / 4 + round * (max / 6));
    spin(3000);
    beat.reset();
    const size_t before = spy.changes.size();
    slider->setSliderDown(true);
    const qint64 t0 = gClock.elapsed();
    int moves = 0;
    QTimer mover;
    mover.setInterval(20);
    mover.setTimerType(Qt::PreciseTimer);
    // Absolute targets, as a mouse gives them: a preview that snaps the
    // handle to its keyframe does not slow the drag down.
    const int dragStart = slider->value();
    QObject::connect(&mover, &QTimer::timeout, [&] {
      ++moves;
      slider->setValue(dragStart + moves * 25);
    });
    mover.start();
    while (gClock.elapsed() - t0 < 3000) spin(5);
    mover.stop();
    const qint64 tStop = gClock.elapsed();
    std::vector<qint64> during(spy.changes.begin() + before, spy.changes.end());
    std::vector<qint64> intervals;
    for (size_t k = 1; k < during.size(); ++k) intervals.push_back(during[k] - during[k - 1]);
    picturesHeld = during.size();
    const qint64 dragFreeze = beat.maxGap();
    const qint64 dragFrozen = beat.frozen();

    // hold still for 300 ms, then release
    const size_t beforeStill = spy.changes.size();
    beat.reset();
    spin(300);
    printf("drag %d: still 300 ms: %zu picture(s) %s, longest freeze %lld ms, slider %d, shown %d\n",
           round + 1, spy.changes.size() - beforeStill,
           spy.changes.size() > beforeStill
               ? qPrintable(QString("(first after %1 ms)").arg(spy.changes[beforeStill] - tStop)) : "",
           (long long)beat.maxGap(), slider->value(), shownPos);
    beat.reset();
    const size_t beforeRel = spy.changes.size();
    const qint64 tRel = gClock.elapsed();
    slider->setSliderDown(false);
    const qint64 l = waitForChange(spy, beforeRel, tRel, 5000);
    spin(1500);
    printf("drag %d: %d moves in %lld ms, %zu new pictures while held, GUI frozen %lld ms in total, longest %lld ms\n",
           round + 1, moves, (long long)(tStop - t0), during.size(), (long long)dragFrozen, (long long)dragFreeze);
    printStats("  interval between pictures", intervals);
    printf("  release (after 300 ms still): picture after %lld ms, %zu picture(s), longest freeze %lld ms, slider %d, shown %d\n",
           (long long)l, spy.changes.size() - beforeRel, (long long)beat.maxGap(), slider->value(), shownPos);
  }

  if (gateClick) {
    std::vector<qint64> v = clickJumpLat;
    std::sort(v.begin(), v.end());
    if (v.empty() || v.front() < 0) {
      printf("FAIL: a click or jump showed no new picture\n");
      return 1;
    }
    const qint64 median = v[v.size() / 2];
    if (median > 300) {
      printf("FAIL: median %lld ms to the new picture after a click or jump, expected at most 300\n",
             (long long)median);
      return 1;
    }
    printf("PASS (median %lld ms)\n", (long long)median);
    return 0;
  }
  if (gateWheel) {
    if (wheelSingleFails > 0) {
      printf("FAIL: %d of 6 single notches did not report exactly the expected %s\n",
             wheelSingleFails, fast ? "I-frame" : "frame");
      return 1;
    }
    if (wheelFinal != wheelTarget) {
      printf("FAIL: wheel ended at %d, expected %d\n", wheelFinal, wheelTarget);
      return 1;
    }
    if (wheelReports > 3) {
      printf("FAIL: %d positions reported for five queued notches, expected at most 3\n", wheelReports);
      return 1;
    }
    printf("PASS\n");
    return 0;
  }
  if (gate) {
    const size_t pictures = gateDrag ? picturesHeld : picturesHold;
    if (pictures < 6) {
      printf("FAIL: %zu new pictures during a 3 s %s, expected at least 6\n", pictures,
             gateDrag ? "drag" : "groove hold");
      return 1;
    }
    printf("PASS\n");
    return 0;
  }
  printf("MEASURED (no verdict)\n");
  return 0;
}
