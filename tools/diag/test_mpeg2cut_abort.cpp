// Abort harness for the MPEG-2 final cut's synchronous audio phase (Task 7 of
// the cut-abort plan), driven through the real TTAVData::onDoCut() -> pool
// chain -- no GUI, no main window. Mirrors tools/diag/test_h26xcut_abort.cpp
// (Task 6), reused here because the abort must be delivered exactly the same
// way the Cancel button delivers it.
//
// Usage: test_mpeg2cut_abort <video-m2v> <audio-mp2> <workdir> <phase> [cutIn cutOut cutIn cutOut ...]
//   phase = audio | video | mux | none | mplex | mplexabort | mplexearly
//           | mplexlate
//
// What it does, per invocation:
//   1. Opens the elementary streams the way TTOpenVideoTask/TTOpenAudioTask do
//      (TTVideoType/TTAudioType factory + createHeaderList + createIndexList +
//      sortDisplayOrder), builds a TTAVItem and a TTCutList.
//   2. Calls TTAVData::onDoCut() -- the same entry point the GUI and
//      --auto-cut use. For MPEG-2 it runs doCut's own branch: audio is cut
//      SYNCHRONOUSLY on the calling thread (qApp->processEvents() pumped from
//      inside cutAudioTracks' progress callback) and only THEN is the video
//      task handed to the pool.
//   3. Records every TTAVData::statusReport in emission order (direct
//      connection) plus every cutFinished().
//   4. For phase "audio": as soon as a Step message proves the audio phase is
//      genuinely under way (percent >= 5, i.e. after real packets have been
//      copied), posts onUserAbortRequest() to the GUI thread with a QUEUED
//      invocation -- exactly the route the Cancel button takes
//      (TTProgressBar::cancel -> TTAVData::onUserAbortRequest). The audio
//      phase's own qApp->processEvents() call is what lets the queued
//      invocation run before TTAudioCutter::cut's next per-packet abort poll.
//   5. For phase "video": arms on the Step between cut-list segments ("Cut 1
//      of N"), i.e. the pool's own pre-existing abort path (unchanged by
//      Task 7) -- exercised here only to prove TTCutVideoTask's fatal-log fix
//      (see 3.5 in the report) did not change its Canceled behavior.
//   5b. For phase "mux" (Task 8): arms on a "Muxing..." Step with percent >= 5,
//      i.e. after the mux has genuinely written packets. Since Task 8 the mux
//      is the operation's SECOND pool run (TTMuxTask), so this Step arrives on
//      a worker thread and the cancel travels the same queued GUI-thread route
//      the Cancel button uses.
//   5c. For phase "mplexabort": arms on the AddProcessLine that
//      TTMplexProvider::onProcStarted() emits, i.e. after QProcess has
//      reported the external mplex as genuinely started. The cancel therefore
//      has to travel the kill path in TTMplexProvider::mplexPart()'s wait
//      loop, not the "request arrived before proc->start()" shortcut, and the
//      earlier phases (audio, video pool run) have necessarily completed
//      first - which is exactly what the human tester had to do by hand.
//      mplex is NOT a pool task: it runs synchronously on the GUI thread
//      inside TTAVData::onCutFinished(), so the cancel is delivered through
//      TTAVData::mpMplexProvider, and the Canceled bracket is emitted by that
//      slot's own abort block (there is no pool run left to emit it).
//   5d. For phase "mplexearly": arms on the ShowProcessForm mplexPart() emits
//      BEFORE proc->start(). TTAVData::onStatusReport() pumps the event loop
//      right after re-emitting that report, so the cancel is already delivered
//      when mplexPart()'s pre-start check runs and no mplex process is ever
//      launched - the other reachable branch of the same abort, and
//      deterministic rather than a race (the pump sits between the emit and
//      the check).
//   6. After every abort run, the SAME cut is re-run without an abort in the
//      same process (the "restart after cancel" acceptance item). For the two
//      mplex phases that re-run is the "mplex" control, so the same
//      invocation also proves a non-aborted MPG cut still produces its .mpg.
//
// Assertions for phase "audio" (Task 7's own path):
//   - the arming message was reached, and audio progress stayed below 95% --
//     i.e. the abort landed mid-copy, not after the track had already
//     finished;
//   - exactly one Canceled bracket, text "Cut cancelled";
//   - NO Exit bracket, NO cutFinished();
//   - NO video-stage evidence at all: no "Cut %1 of %2" Step (TTCutVideoTask
//     never ran) and no Stage(StageVideo) announcement (emitted only right
//     before mpThreadTaskPool->init(), which sits after the abort check) --
//     together these prove the pool never started;
//   - the cut directory is EMPTY afterwards (every file the run created is
//     gone -- non-vacuity proven separately, see the report's cleanup-
//     disabled probe, not repeated by this binary).
//
// Assertions for phase "video": exactly one Canceled bracket, no Exit, no
// cutFinished(), and -- since Task 8 closed the pre-existing gap -- an EMPTY
// cut directory (the cut audio tracks and the partial video ES are removed by
// TTAVData::onCutAborted).
//
// Assertions for phase "mux" (Task 8's own path):
//   - the arming message was reached and mux progress stayed below 95%, i.e.
//     the abort landed while the mux was running, not after it finished;
//   - at least one video-cut Step was seen, i.e. it did not land before the
//     mux either -- the video pool run had completed;
//   - exactly one Canceled bracket, text "Cut cancelled";
//   - NO Exit bracket (in particular no stray "exiting thread pool" one from
//     the second pool run) and NO cutFinished();
//   - the cut directory is EMPTY afterwards: the partial .mkv, the chapter
//     file and the cut elementary streams that fed the mux are all gone.
//
// Assertions for phase "mplexabort":
//   - the arming message was reached, i.e. mplex really started;
//   - "Cancel requested - stopping the mplex process" was seen: the abort was
//     taken by the wait loop's poll and the child process was stopped there,
//     not shortcut before proc->start();
//   - at least one video-cut Step and audio progress that reached 100%, i.e.
//     the abort landed in the mux phase and not in an earlier one;
//   - exactly one Canceled bracket, text "Cut cancelled";
//   - NO Exit bracket and NO cutFinished();
//   - the cut directory is EMPTY afterwards: the partial .mpg (deleted by
//     TTMplexProvider) and the cut elementary streams that fed it (deleted by
//     onCutFinished()'s abort block) are all gone.
//
// Assertions for phase "mplexearly": the same bracket/cleanup set as
// "mplexabort", with the stop message asserted ABSENT - that is what
// distinguishes the pre-start branch from the wait-loop one.
//
// Assertions for phase "none"/"mplex": exactly one Init, one Exit ("Cut
// complete"), one cutFinished(), no Canceled, and a non-empty cut directory
// containing the expected products (video ES, audio ES, and the muxed .mkv
// resp. .mpg).
#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTimer>

#include <atomic>
#include <cstdio>

#include "avstream/ttavtypes.h"
#include "avstream/ttavstream.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"

namespace {

struct Event {
  int     state = 0;
  QString msg;
  quint64 value;
};

class Recorder
{
public:
  void add(int state, const QString& msg, quint64 value)
  {
    QMutexLocker lock(&mMutex);
    mEvents.append({ state, msg, value });
  }
  QList<Event> events()
  {
    QMutexLocker lock(&mMutex);
    return mEvents;
  }
private:
  QMutex       mMutex;
  QList<Event> mEvents;
};

int fail(const QString& what)
{
  fprintf(stderr, "FAIL: %s\n", qPrintable(what));
  return 1;
}

QStringList dirEntries(const QString& dir)
{
  return QDir(dir).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}

void clearDir(const QString& dir)
{
  QDir d(dir);
  d.mkpath(".");
  for (const QString& f : dirEntries(dir)) d.remove(f);
}

const char* stateName(int s)
{
  switch (s) {
    case StatusReportArgs::Init:      return "Init";
    case StatusReportArgs::Start:     return "Start";
    case StatusReportArgs::Step:      return "Step";
    case StatusReportArgs::Finished:  return "Finished";
    case StatusReportArgs::Exit:      return "Exit";
    case StatusReportArgs::Canceled:  return "Canceled";
    case StatusReportArgs::Error:     return "Error";
    case StatusReportArgs::Stage:     return "Stage";
    default:                          return "other";
  }
}

// One complete cut run. Returns 0 on success, non-zero on a failed assertion.
int runCut(TTAVItem* avItem, const QString& workDir, const QString& target,
           const QList<QPair<int,int>>& cuts, const QString& phase)
{
  clearDir(workDir);
  TTSettings::instance()->setCutDirPath(workDir);
  // Deterministic products regardless of this machine's persisted GUI
  // settings (TTSettings::instance() loads real QSettings on construction).
  // Phase "mplex" is the MPG container control run (no abort); "mplexabort"
  // is the same MPG path with a cancel delivered while the external mplex
  // process is running.
  const bool useMplex = (phase == "mplex" || phase == "mplexabort" ||
                         phase == "mplexearly" || phase == "mplexlate");
  TTSettings::instance()->setWorkingOutputContainer(useMplex ? 0 : 1);
  if (useMplex) {
    TTSettings::instance()->setWorkingMuxMode(0);   // run mplex, not a script
    // TTMplexProvider::createOutputFilePath() honours muxOutputPath() before
    // the video file's own directory. Without this the .mpg lands in whatever
    // this machine's persisted GUI setting points at (measured: it did).
    TTSettings::instance()->setMuxOutputPath(workDir);
  }
  // Keep the cut ES for inspection by default. TTCUT_ABORT_TEST_DELETE_ES=1
  // switches the "delete elementary streams after a successful mux" option on
  // instead - that step moved out of the mux itself and into
  // TTAVData::onMpeg2MuxFinished() when the mux became a pool task, so it
  // needs its own control run.
  const bool deleteES = (qgetenv("TTCUT_ABORT_TEST_DELETE_ES") == "1");
  TTSettings::instance()->setWorkingMuxDeleteES(deleteES);

  TTAVData avData;
  avData.setNonInteractive(true);          // no modal burst dialog

  // Populate TTAVData's OWN cut list member (not a throwaway local one): the
  // MPEG-2 branch of onCutFinished() reads mpCutList directly (video stream /
  // frame rate for the MKV mux setup), same as the real GUI and --auto-cut
  // paths do (TTCutMainWindow::runAutoCutMode() cuts via
  // mpAVData->cutList()). A local TTCutList passed only as onDoCut()'s
  // parameter would leave that member empty and crash onCutFinished() on the
  // very first at(0) -- the H.26x task doesn't have this dependency, so its
  // harness gets away with a local list.
  TTCutList* cutList = avData.cutList();
  for (const auto& c : cuts) cutList->append(avItem, c.first, c.second);

  Recorder rec;
  // The .mpg TTMplexProvider will write (muxOutputPath is workDir, see above).
  // Read by the mplexabort arming condition so the cleanup assertion is about
  // a file that really existed.
  const QString mpgPath =
      QFileInfo(QDir(workDir), QFileInfo(target).completeBaseName() + ".mpg").absoluteFilePath();
  qint64 mpgSizeAtArm = -1;
  // Event index at which the queued cancel actually EXECUTED on the GUI
  // thread. The gap between arming and this is what the mplexlate phase is
  // about, and it is not observable any other way.
  std::atomic<int> cancelDeliveredAt { -1 };
  std::atomic<int> armedAt { -1 };

  std::atomic<bool> armed { false };
  std::atomic<int>  cutStepMsgs { 0 };
  std::atomic<int>  cutFinishedCount { 0 };
  std::atomic<int>  avReloadCount { 0 };
  std::atomic<bool> terminalSeen { false };

  static const QRegularExpression videoCutStepRe("^Cut (\\d+) of (\\d+)$");

  auto scheduleQuit = [&terminalSeen]() {
    if (terminalSeen.exchange(true)) return;
    // Give trailing signals (a second bracket, cutFinished, deleteLater) two
    // seconds to arrive -- their ABSENCE is what most assertions test, so
    // quitting immediately would make those assertions vacuous.
    QMetaObject::invokeMethod(qApp, [] { QTimer::singleShot(2000, qApp, &QCoreApplication::quit); },
                              Qt::QueuedConnection);
  };

  QObject::connect(&avData, &TTAVData::cutFinished,
                   [&cutFinishedCount] { cutFinishedCount++; });

  // avDataReloaded() is what makes both tree views rebuild. It is emitted per
  // POOL RUN by TTAVData::onThreadPoolExit(), and since Task 8 an MPEG-2 MKV
  // cut has two of them - counted here so the "once per cut operation"
  // behaviour cannot regress unnoticed.
  QObject::connect(&avData, &TTAVData::avDataReloaded,
                   [&avReloadCount] { avReloadCount++; });

  QObject::connect(&avData,
      qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
      &avData,
      [&](TTThreadTask*, int state, const QString& msg, quint64 value) {
        rec.add(state, msg, value);

        if (state == StatusReportArgs::Canceled || state == StatusReportArgs::Exit)
          scheduleQuit();

        if (state == StatusReportArgs::Step && videoCutStepRe.match(msg).hasMatch())
          cutStepMsgs++;

        if (phase == "none" || phase == "mplex" || armed.load()) return;

        bool arm = false;
        if (phase == "mplexabort") {
          // Arm on mplex's OWN output, and only once the .mpg it is writing
          // actually has bytes in it. Arming on "Starting mplex process"
          // (which TTMplexProvider emits as soon as QProcess reports the child
          // started) was enough to prove the process was running, but it let
          // the cancel land before mplex had created its output at all - and
          // then "cut directory empty afterwards" is true for a file that
          // never existed. Review called that out as a vacuous assertion.
          // mpgSizeAtArm > 0 is asserted below, so the cleanup check is now
          // about a partial file that demonstrably existed.
          if (state == StatusReportArgs::AddProcessLine && msg.contains("INFO:")) {
            const qint64 sz = QFileInfo(mpgPath).size();
            if (sz > 0) { mpgSizeAtArm = sz; arm = true; }
          }
        } else if (phase == "mplexlate") {
          // The window the seed in onCutFinished() exists for: the request has
          // been RECORDED (mSyncPhaseAbort set) but no pool task is left to
          // carry it and the provider does not exist yet. TTCutVideoTask polls
          // isAborted() at the top of each cut-list iteration, so a cancel
          // arriving during the final iteration is never polled again, and
          // TTThreadTask::run() emits finished() regardless of mIsAborted -
          // the pool reports a normal finish and the request has nobody left
          // to reach.
          //
          // Arming on the Stage(StageMux) that onCutFinished() emits as its
          // FIRST statement puts the harness exactly in that state, and the
          // call below is DIRECT, not queued, for a reason: arming on the last
          // "Cut N of N" Step instead reproduces it only sometimes (measured
          // over 10 runs: the queued call is delivered either at the arm point
          // or not until after the mux has finished, and only the former
          // reaches this window) - a flaky probe of a deterministic defect.
          // A direct call is also the more faithful one here: the real Cancel
          // button reaches this slot through a DIRECT connection
          // (TTProgressBar::cancel -> TTAVData::onUserAbortRequest, both in the
          // GUI thread), so the button runs it synchronously too. The queued
          // hop in the other phases exists only because they arm from a
          // callback that originates on a worker thread.
          if (state == StatusReportArgs::Stage && value == StatusReportArgs::StageMux) {
            if (armed.exchange(true)) return;
            fprintf(stderr, "  arm at [Stage] StageMux (direct call)\n");
            armedAt.store((int)rec.events().size());
            cancelDeliveredAt.store((int)rec.events().size());
            avData.onUserAbortRequest();
          }
          return;
        } else if (phase == "mplexearly") {
          // The other reachable window: mplexPart() emits ShowProcessForm
          // BEFORE proc->start(), and TTAVData::onStatusReport() pumps the
          // event loop right after re-emitting it - so a cancel posted here
          // is already delivered when mplexPart()'s pre-start check runs, and
          // no mplex process is ever launched. Deterministic, not a race:
          // the pump sits between the emit and the check.
          // Matched on the message, not just the state: the MPEG-2 re-encoder
          // emits ShowProcessForm too (during the video phase), and arming on
          // that aborted the wrong phase - measured, videoCutSteps went to 0.
          if (state == StatusReportArgs::ShowProcessForm && msg == "Starting mplex")
            arm = true;
        } else if (state != StatusReportArgs::Step) {
          return;
        } else if (phase == "audio") {
          if (msg.contains("Cutting audio track") && value >= 5) arm = true;
        } else if (phase == "video") {
          // Between-segments poll (TTCutVideoTask::operation()'s isAborted()
          // check runs at the TOP of the next loop iteration): arm on the
          // first completed segment, before the second one starts.
          if (videoCutStepRe.match(msg).hasMatch()) arm = true;
        } else if (phase == "mux") {
          // TTMkvMergeProvider emits one progressChanged per changed percent;
          // >= 5 proves packets have really been written before the cancel is
          // posted (its checkAbort() poll sits at the top of the write loop).
          if (msg == "Muxing..." && value >= 5) arm = true;
        }

        if (!arm) return;
        if (armed.exchange(true)) return;
        fprintf(stderr, "  arm at [%s] \"%s\" %llu\n",
                stateName(state), qPrintable(msg), (unsigned long long)value);
        armedAt.store((int)rec.events().size());
        // Queued: the cancel must run on the GUI thread, like the real one.
        QMetaObject::invokeMethod(&avData, "onUserAbortRequest", Qt::QueuedConnection);
        // Posted right after it from the same thread, so FIFO delivery makes
        // this run immediately AFTER the slot: it records when the cancel was
        // really executed, not when it was posted.
        QMetaObject::invokeMethod(&avData,
            [&rec, &cancelDeliveredAt] { cancelDeliveredAt.store((int)rec.events().size()); },
            Qt::QueuedConnection);
      },
      Qt::DirectConnection);

  // Emergency brake only -- a run that needs this has already failed.
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(900000);

  QElapsedTimer timer;
  timer.start();
  avData.onDoCut(target, cutList, false);
  qApp->exec();
  const qint64 elapsed = timer.elapsed();

  // ---- evaluation -----------------------------------------------------
  const QList<Event> events = rec.events();
  int  initCount = 0, exitCount = 0, cancelCount = 0, stageVideoCount = 0;
  QString exitMsg, cancelMsg;
  bool sawAudioMsg = false;
  quint64 maxAudioPercent = 0;
  bool sawMuxMsg = false;
  quint64 maxMuxPercent = 0;
  // Proof that the cancel was taken by the wait loop's poll and stopped a
  // RUNNING mplex, rather than by mplexPart()'s pre-start check.
  bool sawMplexStopMsg = false;

  for (const Event& e : events) {
    if (e.state == StatusReportArgs::AddProcessLine &&
        e.msg == "Cancel requested - stopping the mplex process")
      sawMplexStopMsg = true;
    if (e.state == StatusReportArgs::Init)     initCount++;
    if (e.state == StatusReportArgs::Exit)   { exitCount++;   exitMsg   = e.msg; }
    if (e.state == StatusReportArgs::Canceled) { cancelCount++; cancelMsg = e.msg; }
    if (e.state == StatusReportArgs::Stage && e.value == StatusReportArgs::StageVideo)
      stageVideoCount++;
    if (e.state != StatusReportArgs::Step) continue;
    if (e.msg.contains("Cutting audio track")) {
      sawAudioMsg = true;
      maxAudioPercent = qMax(maxAudioPercent, e.value);
    }
    if (e.msg == "Muxing...") {
      sawMuxMsg = true;
      maxMuxPercent = qMax(maxMuxPercent, e.value);
    }
  }

  printf("  phase=%-5s events=%d  init=%d exit=%d cancel=%d cutFinished=%d "
         "videoCutSteps=%d stageVideo=%d avReload=%d  %lld ms\n",
         qPrintable(phase), (int)events.size(), initCount, exitCount, cancelCount,
         cutFinishedCount.load(), cutStepMsgs.load(), stageVideoCount,
         avReloadCount.load(), (long long)elapsed);
  printf("  audioMsg=%d(max %llu%%)  muxMsg=%d(max %llu%%)  mplexStopMsg=%d  mpgAtArm=%lld B\n",
         sawAudioMsg ? 1 : 0, (unsigned long long)maxAudioPercent,
         sawMuxMsg ? 1 : 0, (unsigned long long)maxMuxPercent,
         sawMplexStopMsg ? 1 : 0, (long long)mpgSizeAtArm);
  printf("  armedAtEvent=%d  cancelDeliveredAtEvent=%d\n",
         armedAt.load(), cancelDeliveredAt.load());
  const QStringList left = dirEntries(workDir);
  printf("  cut dir after run: %s\n",
         left.isEmpty() ? "(empty)" : qPrintable(left.join(", ")));
  for (const QString& f : left)
    printf("    %-48s %lld B\n", qPrintable(f),
           (long long)QFileInfo(QDir(workDir), f).size());

  if (phase == "none" || phase == "mplex") {
    if (initCount != 1)   return fail("control: expected exactly one Init");
    if (exitCount != 1)   return fail(QString("control: expected exactly one Exit, got %1").arg(exitCount));
    if (exitMsg != "Cut complete")
      return fail(QString("control: unexpected Exit text \"%1\"").arg(exitMsg));
    if (cancelCount != 0) return fail("control: unexpected Canceled");
    if (cutFinishedCount.load() != 1)
      return fail(QString("control: expected one cutFinished(), got %1").arg(cutFinishedCount.load()));
    // One reload per cut OPERATION, not per pool run (the MKV cut has two).
    if (avReloadCount.load() != 1)
      return fail(QString("control: expected one avDataReloaded(), got %1").arg(avReloadCount.load()));
    if (left.isEmpty())   return fail("control: cut directory is empty - nothing was produced");
    if (phase == "none") {
      bool sawMkv = false;
      for (const QString& f : left) if (f.endsWith(".mkv")) sawMkv = true;
      if (!sawMkv) return fail("control: no .mkv produced");
      // With the option on, the muxed .mkv must be the ONLY thing left.
      if (deleteES && left.size() != 1)
        return fail(QString("control: deleteES left more than the .mkv: %1").arg(left.join(", ")));
    } else {
      bool sawMpg = false;
      for (const QString& f : left) if (f.endsWith(".mpg")) sawMpg = true;
      if (!sawMpg) return fail("mplex: no .mpg produced");
    }
    return 0;
  }

  if (!armed.load())     return fail(phase + ": never reached the arming message");
  // Exactly one opening bracket, on the abort paths too. This is what catches
  // a regression of the mCutOperationActive re-arm in onCutFinished(): without
  // it the second pool run emits the pool's own "starting thread pool" Init
  // and the count goes to 2.
  if (initCount != 1)
    return fail(QString("%1: expected exactly one Init, got %2").arg(phase).arg(initCount));
  if (cancelCount != 1)  return fail(QString("%1: expected exactly one Canceled, got %2").arg(phase).arg(cancelCount));
  if (cancelMsg != "Cut cancelled")
    return fail(QString("%1: unexpected Canceled text \"%2\"").arg(phase, cancelMsg));
  if (exitCount != 0)
    return fail(QString("%1: an Exit bracket followed the cancel (\"%2\")").arg(phase, exitMsg));
  if (cutFinishedCount.load() != 0)
    return fail(QString("%1: cutFinished() was emitted on a cancelled run").arg(phase));

  // Reload expectation per abort phase: the audio phase aborts before the pool
  // ever runs, so nothing reloads; the video and mux phases have exactly one
  // pool run reaching onThreadPoolExit's reload (the mux run skips it).
  {
    const int expectedReloads = (phase == "audio") ? 0 : 1;
    if (avReloadCount.load() != expectedReloads)
      return fail(QString("%1: expected %2 avDataReloaded(), got %3")
                      .arg(phase).arg(expectedReloads).arg(avReloadCount.load()));
  }

  if (phase == "audio") {
    if (!sawAudioMsg)   return fail("audio: no audio progress observed");
    if (maxAudioPercent >= 95)
      return fail(QString("audio: progress reached %1%% - the track was cut to the end")
                      .arg(maxAudioPercent));
    if (cutStepMsgs.load() != 0)
      return fail("audio: a video-cut Step was observed - the pool started");
    if (stageVideoCount != 0)
      return fail("audio: Stage(StageVideo) was announced - the pool phase began");
    if (!left.isEmpty())
      return fail(QString("audio: files left behind: %1").arg(left.join(", ")));
  } else if (phase == "video") {
    if (cutStepMsgs.load() < 1)
      return fail("video: no video-cut Step observed - never reached the arm point");
    // Since Task 8: onCutAborted() deletes what the run created, so the
    // video-phase abort has to leave an empty directory too.
    if (!left.isEmpty())
      return fail(QString("video: files left behind: %1").arg(left.join(", ")));
  } else if (phase == "mux") {
    if (!sawMuxMsg) return fail("mux: no mux progress observed");
    if (maxMuxPercent >= 95)
      return fail(QString("mux: progress reached %1%% - the mux had already finished")
                      .arg(maxMuxPercent));
    if (cutStepMsgs.load() < 1)
      return fail("mux: no video-cut Step - the abort landed before the mux phase");
    if (!left.isEmpty())
      return fail(QString("mux: files left behind: %1").arg(left.join(", ")));
  } else if (phase == "mplexabort") {
    // The abort has to land in the mux phase, i.e. after both earlier phases
    // completed - the audio track was cut to the end and every cut-list entry
    // produced its video Step.
    if (!sawAudioMsg || maxAudioPercent < 100)
      return fail(QString("mplexabort: audio phase did not complete (max %1%%)")
                      .arg(maxAudioPercent));
    if (cutStepMsgs.load() < 1)
      return fail("mplexabort: no video-cut Step - the abort landed before the mux phase");
    // Distinguishes the kill path from mplexPart()'s pre-start check: only
    // the wait loop's poll emits this line, and it emits it before stopping
    // the child.
    if (!sawMplexStopMsg)
      return fail("mplexabort: mplex was never stopped from the wait loop");
    // Non-vacuity of the cleanup check: the .mpg had real content when the
    // cancel was posted, so its absence below is a removal and not an absence
    // that was always going to hold.
    if (mpgSizeAtArm <= 0)
      return fail("mplexabort: the .mpg had no bytes when the abort was armed");
    if (!left.isEmpty())
      return fail(QString("mplexabort: files left behind: %1").arg(left.join(", ")));
  } else if (phase == "mplexlate") {
    // The abort must land in the mux phase: both earlier phases completed.
    if (!sawAudioMsg || maxAudioPercent < 100)
      return fail(QString("mplexlate: audio phase did not complete (max %1%%)")
                      .arg(maxAudioPercent));
    if (cutStepMsgs.load() < 1)
      return fail("mplexlate: no video-cut Step - the abort landed before the mux phase");
    // The request was recorded before the provider existed, so the seed in
    // onCutFinished() is what has to carry it into mplexPart()'s pre-start
    // check - no process may be started, hence no wait-loop stop message.
    if (sawMplexStopMsg)
      return fail("mplexlate: mplex was started - the seed did not carry the request");
    if (!left.isEmpty())
      return fail(QString("mplexlate: files left behind: %1").arg(left.join(", ")));
  } else if (phase == "mplexearly") {
    if (!sawAudioMsg || maxAudioPercent < 100)
      return fail(QString("mplexearly: audio phase did not complete (max %1%%)")
                      .arg(maxAudioPercent));
    if (cutStepMsgs.load() < 1)
      return fail("mplexearly: no video-cut Step - the abort landed before the mux phase");
    // The complement of the mplexabort assertion: this cancel must be taken
    // by the pre-start check, so the wait loop's stop message must NOT appear.
    if (sawMplexStopMsg)
      return fail("mplexearly: the process was started after all - wrong branch");
    if (!left.isEmpty())
      return fail(QString("mplexearly: files left behind: %1").arg(left.join(", ")));
  }

  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);   // keep stdout interleaved with qDebug()'s stderr
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  if (argc < 5) {
    fprintf(stderr, "usage: %s <video-m2v> <audio-mp2> <workdir> "
                    "<audio|video|mux|none|mplex|mplexabort|mplexearly|mplexlate> "
                    "[cutIn cutOut cutIn cutOut ...]\n", argv[0]);
    return 2;
  }
  const QString videoFile = argv[1];
  const QString audioFile = argv[2];
  const QString workDir   = argv[3];
  const QString phase     = argv[4];

  QList<QPair<int,int>> cuts;
  for (int i = 5; i + 1 < argc; i += 2)
    cuts.append(qMakePair(QString(argv[i]).toInt(), QString(argv[i+1]).toInt()));
  if (cuts.isEmpty()) cuts << qMakePair(100, 899) << qMakePair(1300, 2099) << qMakePair(2300, 2799);

  if (phase != "audio" && phase != "video" && phase != "mux" &&
      phase != "none"  && phase != "mplex" && phase != "mplexabort" &&
      phase != "mplexearly" && phase != "mplexlate")
    return fail("unknown phase (use audio|video|mux|none|mplex|mplexabort|mplexearly|mplexlate)");

  // Open the streams exactly like TTOpenVideoTask / TTOpenAudioTask do.
  TTVideoType   vType(videoFile);
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr) return fail("could not create the video stream");
  if (vStream->createHeaderList() <= 0) return fail("createHeaderList failed");
  if (vStream->createIndexList()  <= 0) return fail("createIndexList failed");
  if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

  TTAVItem* avItem = new TTAVItem(vStream);

  TTAudioType    aType(audioFile);
  TTAudioStream* aStream = aType.createAudioStream();
  if (aStream == nullptr) return fail("could not create the audio stream");
  aStream->createHeaderList();
  avItem->appendAudioEntry(aStream);

  // Isolated log file so a grep for ERROR/WARN/fatal after an abort run
  // reflects only this run, not whatever else has ever logged on this
  // machine (~/.cache/ttcut-ng/logfile.log is the real GUI's persistent log).
  // Deliberately a SIBLING of workDir, not inside it: workDir is also the cut
  // directory, and runCut()'s "directory empty after abort" assertion would
  // otherwise see the log file itself as a leftover product.
  TTMessageLogger::getInstance()->setLogFilePath(
      QFileInfo(QDir(workDir), "../ttcut_harness.log").absoluteFilePath());

  const QString target =
      QFileInfo(QDir(workDir), QFileInfo(videoFile).completeBaseName()).absoluteFilePath();

  printf("== %s: abort during the %s phase ==\n",
         qPrintable(QFileInfo(videoFile).fileName()), qPrintable(phase));
  int rc = runCut(avItem, workDir, target, cuts, phase);
  if (rc != 0) return rc;

  // Print this run's log BEFORE the restart's clearDir() wipes workDir (the
  // log itself lives one level up, see setLogFilePath() above, so it
  // survives -- but the point in time to read it is still "right after this
  // run", before the restart run's own log entries get appended).
  {
    QFile logFile(QFileInfo(QDir(workDir), "../ttcut_harness.log").absoluteFilePath());
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      printf("---- log for this run (%s) ----\n%s---- end log ----\n",
             qPrintable(phase), logFile.readAll().constData());
    }
  }

  if (phase != "none" && phase != "mplex") {
    // Restart the very same cut in the same process: a cancelled run must
    // leave nothing behind that stops the next one. The restart uses the same
    // output container the aborted run used, so the mplex abort's control run
    // is the MPG path - which doubles as the "a non-aborted MPG cut still
    // produces its .mpg" check.
    printf("== restart after cancel ==\n");
    rc = runCut(avItem, workDir, target, cuts,
                phase.startsWith("mplex") ? "mplex" : "none");
    if (rc != 0) return rc;
  }

  printf("PASS\n");
  return 0;
}
