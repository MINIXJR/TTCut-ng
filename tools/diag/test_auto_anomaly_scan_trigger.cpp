// Regression harness for the auto-anomaly-scan wiring fix (user-acceptance
// bug, 2026-08-20): "after opening a video, no AudioAnomaly marker appears
// and there is no sign the audio track is being analyzed".
//
// TWO root causes were found (measured with fprintf trace + gate
// instrumentation on the real 1.6 GB audio-anomaly-LFE-center-burst DVB
// recording, which - because ttcut-audiofix flagged one corrupt MP2 range in
// its .info - is exactly the kind of repaired material that hits both):
//
//  1. TTCutMainWindow::maybeStartAutoAnomalyScan() (see its implementation
//     comment for the full trace) used to have only ONE trigger for a plain
//     video open - onAVDataReloaded(), reached from
//     TTAVData::onThreadPoolExit() on a fixed schedule (as soon as every
//     pool task reports done). TTAVData::onOpenVideoFinished() - the only
//     place that sets the current AV item for a plain open - can be blocked
//     for as long as a modal QMessageBox is on screen
//     (TTAVData::showExtraFrameClusterDialog()'s "Defective Frames
//     Detected", shown whenever the .info reports doubled-PTS clusters,
//     audio gaps, or corrupt ranges). The pool's exit() does not wait on
//     that dialog, so the one-shot trigger fires while the current item is
//     still null, and nothing re-checks the gate once the dialog is
//     dismissed and the item is finally set. Fixed by adding a second
//     trigger in onAVItemChanged() (also deferred by a zero-timer).
//
//  2. TTAVData::onOpenVideoFinished() also appends avItem to mpAVList AFTER
//     the point where it can get stuck in that same dialog. Qt's nested
//     event loop (QMessageBox::exec()) lets the ALREADY-QUEUED pool exit()
//     run to completion while the dialog is still up - so
//     TTAVData::onThreadPoolExit()'s "mark every item in mpAVList as having
//     finished its initial audio load" loop runs BEFORE avItem is even in
//     that list, permanently leaving TTAVItem::initialAudioLoadDone() false
//     for it (nothing else ever sets it) - which blocks
//     maybeStartAutoAnomalyScan()'s own gate regardless of fix 1. Fixed in
//     onOpenVideoFinished() itself: if the pool has already drained by the
//     time avItem is appended, the initial batch is trivially complete for
//     it too, so the flag is set right there.
//
// What this harness covers: the WIRING invariants across the three
// previously-measured scenarios, with the real main window, offscreen -
// exactly-one scan on a plain video open, exactly-one scan on a project
// without saved markers, and NO scan (restore instead) on a project that
// already carries AudioAnomaly markers. The tux test material used for this
// (no .info corruption entries) does not itself trigger the "Defective
// Frames Detected" dialog, so it does not exercise root cause 2's exact
// timing - but ANY modal QMessageBox that does pop up (from this or a
// future defect) is auto-dismissed by a driver timer so a headless run
// cannot silently stall on it instead of failing loudly.
//
// What it does NOT cover: reproducing root cause 2's dialog-stall timing
// itself needs material with a genuine .info corrupt/defect entry, which
// the tux corpus does not have. That evidence is the manual before/after
// run against the real DVB recording (see
// .superpowers/sdd/2026-08-19-audio-anomaly-repair/user-acceptance-fix-report.md).
//
// Usage:
//   test_auto_anomaly_scan_trigger video <video-file> <workdir>
//   test_auto_anomaly_scan_trigger project-clean <project.ttcut> <workdir>
//   test_auto_anomaly_scan_trigger project-markers <project.ttcut> <workdir>
//   test_auto_anomaly_scan_trigger project-abort-then-video <video-file> <workdir>
//
// project-markers expects a .ttcut that ALREADY carries a <StreamPoint>
// element with <Type>AudioAnomaly</Type> (top-level, after the Video
// element) - the harness does not synthesize one; the corpus recipe is
// documented alongside this file's CMakeLists entry.
//
// project-abort-then-video covers residuals R1 (2026-08-20 re-review,
// .superpowers/sdd/2026-08-19-audio-anomaly-repair/): TTCutMainWindow::
// openProjectFile() sets mProjectLoadInProgress = true and relied on
// onOpenProjectFileFinished() (success only) to release it again. A project
// read that ABORTS (TTAVData::onReadProjectFileAborted() - unreadable or
// corrupt .ttcut) never reaches that slot, so the flag stayed true for the
// rest of the session; maybeStartAutoAnomalyScan() checks it first at BOTH
// its call sites and silently refused to run again, with no error shown to
// the user. This mode synthesizes a deliberately corrupt .ttcut in
// <workdir>, feeds it to openProjectFile() (expect a clean abort, not a
// crash or hang), then opens the given valid <video-file> exactly like
// "video" mode and asserts the automatic scan still starts - proving the
// flag was released on the abort path, not just the success path.
//
// Prints PASS/FAIL per check and "ALL PASS"/"FAILED" at the end. Exit 0/1.
// Run with XDG_CONFIG_HOME pointing somewhere disposable and
// QT_QPA_PLATFORM=offscreen.
//
// Build: cmake --build build --target test_auto_anomaly_scan_trigger
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "data/ttstreampoint.h"
#include "data/ttstreampointmodel.h"
#include "gui/ttcutmainwindow.h"

namespace {

int gFailures = 0;

void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) gFailures++;
}

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) qApp->processEvents(QEventLoop::AllEvents, 20);
}

// Dismisses any visible QMessageBox by clicking its Ok button (falls back to
// close()). Both dialogs this harness can encounter
// (showExtraFrameClusterDialog's "Defective Frames Detected" and
// openAVStreams's "Stream Integrity Warning") offer a QMessageBox::Ok
// button alongside an AcceptRole "Import as Stream Points" one; clicking Ok
// exercises the plain "do not import" path, which is what a user declining
// the import would do too - the scan-trigger wiring does not depend on that
// choice either way.
void dismissModalDialogs()
{
  // QMessageBox ONLY - deliberately NOT a catch-all for QDialog. TTProgressBar
  // (gui/ttprogressbar.h) is ALSO a QDialog and is legitimately visible while
  // the pool is loading; calling reject() on it cancels the load (measured:
  // triggers TTThreadTaskPool's abort path mid-OpenVideoTask, which then
  // races onOpenVideoFinished's teardown into a segfault in
  // TTCutMainWindow::onAVItemChanged - exactly the kind of stall/crash this
  // driver exists to avoid, not cause).
  for (QWidget* w : qApp->topLevelWidgets()) {
    if (!w->isVisible()) continue;
    auto* mb = qobject_cast<QMessageBox*>(w);
    if (!mb) continue;
    printf("dismissing QMessageBox: %s\n", qPrintable(mb->windowTitle()));
    if (QAbstractButton* ok = mb->button(QMessageBox::Ok)) {
      ok->click();
    } else {
      mb->close();
    }
  }
}

// Number of AudioAnomaly-typed rows currently in the stream point model,
// found via the (only) QListView the main window owns while a video is
// open - TTStreamPointWidget's marker list.
int audioAnomalyMarkerCount(QMainWindow& mainWnd)
{
  // Several QListViews can exist by the time a project is loaded (e.g. a
  // QCompleter popup) - the one this harness wants is identified by its
  // MODEL's type, not by being "the first" or "the only" one.
  TTStreamPointModel* model = nullptr;
  for (QListView* view : mainWnd.findChildren<QListView*>()) {
    if (auto* m = qobject_cast<TTStreamPointModel*>(view->model())) {
      model = m;
      break;
    }
  }
  if (!model) return -1;
  int count = 0;
  for (int i = 0; i < model->rowCount(); i++) {
    if (model->pointAt(i).type() == StreamPointType::AudioAnomaly) count++;
  }
  return count;
}

int countLogOccurrences(const QString& logPath, const QString& needle)
{
  QFile f(logPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
  QTextStream in(&f);
  int count = 0;
  while (!in.atEnd()) {
    if (in.readLine().contains(needle)) count++;
  }
  return count;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);

  if (argc < 4) {
    fprintf(stderr,
      "usage: %s <video|project-clean|project-markers|project-abort-then-video> "
      "<file> <workdir> [settle-cap-sec]\n", argv[0]);
    return 2;
  }
  const QString mode    = argv[1];
  const QString path    = argv[2];
  const QString workDir = argv[3];
  // Default covers the tux corpus; real DVB material with a long AC3 track
  // (e.g. the audio-anomaly-LFE-center-burst corpus, ~66 minutes of 5.1)
  // needs more wall time for the scan itself to finish, not just start.
  const int settleCapSec = (argc >= 5) ? QString(argv[4]).toInt() : 30;

  // TTSettings::load() (triggered by TTCutMainWindow's constructor, below)
  // unconditionally re-applies TTMessageLogger::setLogFilePath() from the
  // (empty, unless the user customized it) LogFile\LogFilePath setting,
  // which falls back to QStandardPaths::GenericCacheLocation +
  // "/ttcut-ng/logfile.log" - i.e. it overwrites any path set beforehand.
  // Setting our own path is therefore pointless; instead XDG_CACHE_HOME MUST
  // be isolated so that fallback does not land in the real user cache (a
  // harness run without it once wrote a real "Audio anomaly scan started
  // automatically" entry into ~/.cache/ttcut-ng/logfile.log - harmless
  // content, but not this harness's business to touch).
  if (!qEnvironmentVariableIsSet("XDG_CACHE_HOME")) {
    fprintf(stderr, "XDG_CACHE_HOME must be set to a disposable directory - "
                     "otherwise this harness would write into the real "
                     "user cache (~/.cache/ttcut-ng/logfile.log).\n");
    return 2;
  }

  QDir().mkpath(workDir);

  printf("mode: %s   file: %s\n", qPrintable(mode), qPrintable(path));
  printf("platform: %s   config home: %s   cache home: %s\n",
         qPrintable(qApp->platformName()), qgetenv("XDG_CONFIG_HOME").constData(),
         qgetenv("XDG_CACHE_HOME").constData());

  if (!TTSettings::instance()->audioAnomalyScanEnabled()) {
    fprintf(stderr, "StreamPoints\\AudioAnomalyScanEnabled is off in this "
                     "XDG_CONFIG_HOME - the harness needs it on.\n");
    return 2;
  }

  TTCutMainWindow mainWnd;

  // Same fallback TTMessageLogger::defaultLogPath() computes, evaluated
  // AFTER TTSettings::load() (above) has had its say.
  QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
  if (cacheDir.isEmpty()) cacheDir = QDir::tempPath();
  const QString logPath = cacheDir + "/ttcut-ng/logfile.log";
  printf("log file: %s\n", qPrintable(logPath));

  // Dialog-dismiss driver: runs for the whole harness lifetime so a modal
  // warning cannot stall the load chain (see file header).
  QTimer dismissTimer;
  QObject::connect(&dismissTimer, &QTimer::timeout, [] { dismissModalDialogs(); });
  dismissTimer.start(50);

  if (mode == "video") {
    mainWnd.onReadVideoStream(path);
  } else if (mode == "project-clean" || mode == "project-markers") {
    mainWnd.openProjectFile(path);
  } else if (mode == "project-abort-then-video") {
    // Deliberately corrupt: readXml()'s QDomDocument::setContent() fails on
    // this, TTCutProjectData::readXml() throws TTDataFormatException,
    // TTAVData::readProjectFile()'s catch(const TTException&) runs
    // onReadProjectFileAborted() - the abort path R1 fixes.
    const QString brokenProject =
        QDir(workDir).absoluteFilePath("residuals_r1_broken.ttcut");
    QFile bf(brokenProject);
    if (!bf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      fprintf(stderr, "could not write corrupt fixture %s\n", qPrintable(brokenProject));
      return 2;
    }
    QTextStream(&bf) << "this is not a TTCut-Projectfile, deliberately "
                         "corrupt for the R1 abort-path regression check\n";
    bf.close();

    printf("R1: opening deliberately corrupt project %s (expect a clean "
           "abort, not a crash or hang)\n", qPrintable(brokenProject));
    mainWnd.openProjectFile(brokenProject);
    pump(1000); // abort path is synchronous; a little slack for the signal/slot chain to settle

    printf("R1: opening the valid video after the aborted project load\n");
    mainWnd.onReadVideoStream(path);
  } else {
    fprintf(stderr, "unknown mode: %s\n", qPrintable(mode));
    return 2;
  }

  // Settle: pump for settleCapSec. Deliberately NOT "until the log stops
  // growing" - TTAudioAnomalyScanTask reports its progress via statusReport
  // (to the GUI progress bar), not via TTMessageLogger, so the log goes
  // quiet the instant the scan STARTS and stays quiet for its entire run;
  // "no new log line for 1s" was indistinguishable from "already finished"
  // and broke out long before a real scan on a 66-minute 5.1 AC3 track (the
  // audio-anomaly-LFE-center-burst corpus) was actually done - measured:
  // the harness reported 0 markers on that material even though the scan
  // (confirmed still running via a much longer manual wait) later found
  // one. settleCapSec must be sized for the material under test; the
  // default covers the tux corpus, real DVB material needs the 5th
  // argument.
  QElapsedTimer total; total.start();
  const qint64 settleCapMs = qint64(settleCapSec) * 1000;
  while (total.elapsed() < settleCapMs) {
    pump(100);
  }

  const int scanStartedCount = countLogOccurrences(logPath,
      "Audio anomaly scan started automatically after loading");
  const int markerCount = audioAnomalyMarkerCount(mainWnd);

  printf("scan-started log lines: %d   AudioAnomaly markers in model: %d\n",
         scanStartedCount, markerCount);

  if (mode == "video") {
    check(scanStartedCount == 1, "plain video open: exactly one automatic scan");
  } else if (mode == "project-clean") {
    check(scanStartedCount == 1, "project without saved markers: exactly one automatic scan");
  } else if (mode == "project-markers") {
    check(scanStartedCount == 0, "project with saved markers: zero automatic scans");
    check(markerCount > 0, "project with saved markers: markers were restored, not lost");
  } else { // project-abort-then-video
    check(scanStartedCount == 1,
          "R1: auto scan still starts after an aborted project load "
          "(mProjectLoadInProgress released on the abort path, not just success)");
  }

  printf(gFailures == 0 ? "ALL PASS\n" : "FAILED\n");
  return gFailures == 0 ? 0 : 1;
}
