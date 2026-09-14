// Gate for the asynchronous playback mux (TTPlaybackMuxTask, 2026-09-11).
//
// Before: TTCurrentFrame::onPlayVideo() muxed the H.264/H.265 playback MKV
// synchronously on the GUI thread - no progress, no cancel, "not responding"
// for the whole mux. Now the mux runs on a worker behind a cancellable
// QProgressDialog and playback continues in onPlaybackMuxFinished().
//
// The harness drives the REAL main window: loads a project, presses Play
// and checks, in this order,
//   1. cancel:   the progress dialog appears, Cancel ends the mux, the
//                partial file is gone, Play is enabled again;
//   2. complete: the mux runs to the end while a 20 ms timer keeps ticking
//                (the GUI thread stays responsive), one temp MKV exists;
//   3. cache:    the next Play shows no dialog (cached MKV reused);
//   4. detach:   with the cache removed, Play starts a mux and a stream
//                switch mid-mux (onAVDataChanged(nullptr)) leaves no file.
//
// Needs a source whose mux takes several seconds: the harness clicks Cancel
// 400 ms after Play, and a mux that is done by then leaves nothing to cancel
// - the run then fails three checks for a reason that has nothing to do with
// the code under test (measured 2026-09-14: 75 MB muxes in ~150 ms, 1.3 GB
// in ~900 ms, 3.1 GB in ~2.3 s, which is the first size that works).
// Without broadcast material, concatenating a tux fixture is enough - an
// elementary stream stays valid when appended to itself:
//   for i in $(seq 1 100); do cat tools/test-videos/cache/\
//       tux_h264_1080p_progressive_test.264; done > /tmp-of-project/big.264
//   (same for the .ac3, then a .ttcut naming both)
// Runs offscreen: mpv gets no GL context there, so playback itself is not
// asserted - only the mux, the dialog, the responsiveness and the files.
// Point XDG_CONFIG_HOME somewhere disposable; the run writes settings.
//
//   usage: test_playback_mux_async <project.ttcut> <workdir>
//
// Build: cmake --build build --target test_playback_mux_async
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <clocale>
#include <cstdio>

#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "gui/ttcurrentframe.h"
#include "gui/ttcutmainwindow.h"
#include "gui/ttmpvwrapper.h"

namespace {

int failures = 0;
void expect(const char* name, bool ok, const QString& detail = QString())
{
  if (ok) printf("PASS  %s%s\n", name, detail.isEmpty() ? "" : qPrintable("  (" + detail + ")"));
  else  { failures++; printf("FAIL  %s%s\n", name, detail.isEmpty() ? "" : qPrintable("  (" + detail + ")")); }
}

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) qApp->processEvents();
}

QProgressDialog* visibleMuxDialog(QWidget* root)
{
  for (auto* d : root->findChildren<QProgressDialog*>())
    if (d->isVisible()) return d;
  return nullptr;
}

// Waits up to ms for the predicate; returns the elapsed time or -1.
template <typename F> qint64 waitFor(int ms, F&& pred)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) {
    qApp->processEvents();
    if (pred()) return t.elapsed();
  }
  return -1;
}

QStringList playbackFiles(const QString& dir)
{
  return QDir(dir).entryList({"ttcut-ng_playback_*.mkv"}, QDir::Files);
}

// The user's cancel: the dialog's own button. QProgressDialog::cancel() would
// only hide the dialog - it does not emit canceled(), so the task would never
// hear about it (this harness proved that the hard way).
void clickCancel(QProgressDialog* dlg)
{
  auto* button = dlg->findChild<QPushButton*>();
  if (button) button->click();
  else dlg->close();   // closeEvent() emits canceled() too
}

// Play is a Play/Stop toggle. Whatever playback the previous phase started
// (offscreen mpv does start) is stopped here, and the phase waits for the
// player to report it, so the next click means Play again.
void ensureStopped(TTCurrentFrame* frame, QPushButton* play)
{
  auto* player = frame->findChild<TTMpvWrapper*>();
  if (!player) return;
  pump(1500);
  if (player->isPlaying()) play->click();
  waitFor(15000, [&] { return !player->isPlaying(); });
  pump(500);
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // as gui/ttcutmain.cpp: libmpv insists on it
  if (argc < 3) {
    fprintf(stderr, "usage: %s <project.ttcut> <workdir>\n", argv[0]);
    return 2;
  }
  const QString project = argv[1];
  const QString workDir = QDir(argv[2]).absolutePath();
  QDir().mkpath(workDir);
  TTMessageLogger::getInstance()->setLogFilePath(QDir(workDir).absoluteFilePath("playback_mux_async.log"));
  TTSettings::instance()->setTempDirPath(workDir);
  for (const QString& f : playbackFiles(workDir)) QFile::remove(QDir(workDir).absoluteFilePath(f));

  TTCutMainWindow window;
  window.show();
  window.setWindowTitle("PROBE test_playback_mux_async - running, please leave open");
  pump(500);

  // ---- load the project the way the GUI does, wait until the window is ready
  window.openProjectFile(project);
  {
    QTreeWidget* tree = nullptr;
    waitFor(120000, [&] {
      if (!tree) tree = window.findChild<QTreeWidget*>("videoCutList");
      return tree != nullptr;
    });
    qint64 enabledSince = -1;
    QElapsedTimer t; t.start();
    while (t.elapsed() < 600000) {
      qApp->processEvents();
      if (window.isEnabled()) {
        if (enabledSince < 0) enabledSince = t.elapsed();
        else if (t.elapsed() - enabledSince > 2000) break;
      } else enabledSince = -1;
    }
    printf("project loaded, window ready after %lld ms\n", (long long)t.elapsed());
  }

  auto* frame = window.findChild<TTCurrentFrame*>("currentFrame");
  auto* play  = frame ? frame->findChild<QPushButton*>("pbPlayVideo") : nullptr;
  if (!frame || !play) { fprintf(stderr, "FAIL: currentFrame/pbPlayVideo not found\n"); return 1; }
  expect("play button enabled before the first play", play->isEnabled());

  // Responsiveness meter: a 20 ms timer on the GUI thread. A blocked thread
  // delivers none of its ticks; a free one nearly all of them.
  int ticks = 0;
  QTimer meter;
  meter.setInterval(20);
  QObject::connect(&meter, &QTimer::timeout, [&] { ticks++; });
  meter.start();

  // ---- 1. cancel --------------------------------------------------------
  play->click();
  qint64 shown = waitFor(5000, [&] { return visibleMuxDialog(&window) != nullptr; });
  expect("1 progress dialog shown", shown >= 0, QString("after %1 ms").arg(shown));
  expect("1 play button disabled during mux", !play->isEnabled());
  pump(400);
  if (auto* dlg = visibleMuxDialog(&window)) {
    printf("      dialog value at cancel: %d %%\n", dlg->value());
    clickCancel(dlg);   // canceled() -> TTPlaybackMuxTask::onUserAbort()
  }
  qint64 gone = waitFor(15000, [&] { return visibleMuxDialog(&window) == nullptr; });
  expect("1 dialog closed after cancel", gone >= 0, QString("after %1 ms").arg(gone));
  qint64 cleaned = waitFor(5000, [&] { return playbackFiles(workDir).isEmpty(); });
  expect("1 no temp MKV left after cancel", cleaned >= 0, QString("after %1 ms").arg(cleaned));
  // The dialog hides itself on the click; the worker notices the abort at its
  // next packet and the task's aborted() re-enables Play a moment later.
  qint64 enabled = waitFor(15000, [&] { return play->isEnabled(); });
  expect("1 play button enabled again", enabled >= 0, QString("after %1 ms").arg(enabled));

  // ---- 2. complete ------------------------------------------------------
  ticks = 0;
  QElapsedTimer muxClock; muxClock.start();
  play->click();
  shown = waitFor(5000, [&] { return visibleMuxDialog(&window) != nullptr; });
  expect("2 progress dialog shown", shown >= 0);
  int lastValue = -1;
  gone = waitFor(600000, [&] {
    if (auto* d = visibleMuxDialog(&window)) { lastValue = d->value(); return false; }
    return true;
  });
  const qint64 muxMs = muxClock.elapsed();
  expect("2 dialog closed after completion", gone >= 0, QString("mux %1 ms, last value %2 %").arg(muxMs).arg(lastValue));
  expect("2 progress advanced", lastValue > 0, QString("%1 %").arg(lastValue));
  const int expectedTicks = static_cast<int>(muxMs / 20);
  expect("2 GUI thread responsive during mux", ticks >= expectedTicks / 2,
         QString("%1 of ~%2 ticks").arg(ticks).arg(expectedTicks));
  const QStringList after = playbackFiles(workDir);
  expect("2 exactly one temp MKV", after.size() == 1, after.join(", "));
  if (after.size() == 1)
    expect("2 temp MKV not empty", QFileInfo(QDir(workDir).absoluteFilePath(after[0])).size() > 0);
  ensureStopped(frame, play);

  // ---- 3. cache ---------------------------------------------------------
  play->click();
  shown = waitFor(1500, [&] { return visibleMuxDialog(&window) != nullptr; });
  expect("3 no dialog on cached replay", shown < 0);
  expect("3 still exactly one temp MKV", playbackFiles(workDir).size() == 1);
  ensureStopped(frame, play);

  // ---- 4. detach --------------------------------------------------------
  for (const QString& f : playbackFiles(workDir)) QFile::remove(QDir(workDir).absoluteFilePath(f));
  play->click();
  shown = waitFor(5000, [&] { return visibleMuxDialog(&window) != nullptr; });
  expect("4 mux restarted after cache removal", shown >= 0);
  pump(400);
  frame->onAVDataChanged(nullptr);   // stream switch mid-mux
  expect("4 dialog gone right after the switch", visibleMuxDialog(&window) == nullptr);
  cleaned = waitFor(15000, [&] { return playbackFiles(workDir).isEmpty(); });
  expect("4 no temp MKV left after detach", cleaned >= 0, QString("after %1 ms").arg(cleaned));
  pump(500);

  meter.stop();
  printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
