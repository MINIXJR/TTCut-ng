// Abort harness for the audio-only cut (TTAudioOnlyCutTask), driven through
// the real TTAVData + TTThreadTaskPool chain -- no GUI, no main window.
// Mirrors tools/diag/test_h26xcut_abort.cpp (Task 6/9), reused here because
// the abort must be delivered exactly the same way the Cancel button
// delivers it.
//
// Usage: test_audioonlycut_abort <video-es> <audio-es> <workdir> <phase> [cutIn cutOut cutIn cutOut]
//   phase = audio | mux | none
//
// What it does, per invocation:
//   1. Opens the elementary streams the way TTOpenVideoTask/TTOpenAudioTask do
//      (TTVideoType/TTAudioType factory + createHeaderList + createIndexList +
//      sortDisplayOrder), builds a TTAVItem and a two-cut TTCutList.
//   2. Calls TTAVData::onDoCut(target, cutList, /*audioOnly=*/true) -- the
//      same entry point the GUI and --auto-cut use with the "audio only"
//      checkbox on. It routes to doAudioOnlyCut(), which starts
//      TTAudioOnlyCutTask on the pool and returns; the harness then enters
//      the event loop.
//   3. Records EVERY TTAVData::statusReport in emission order (direct
//      connection, so worker-thread emissions are seen where they happen)
//      plus every cutFinished().
//   4. For an abort phase: as soon as a message proves the phase is really
//      under way, it posts onUserAbortRequest() to the GUI thread with a
//      QUEUED invocation -- exactly the route the Cancel button takes
//      (TTProgressBar::cancel -> TTAVData::onUserAbortRequest). Never called
//      from the worker thread: TTThreadTask::abort() pumps the event loop and
//      the pool queue is GUI-thread state.
//   5. After the aborted run it re-runs the SAME cut without an abort in the
//      same process (the "restart after cancel" acceptance item) and checks
//      that the products appear.
//
// Assertions, per abort run:
//   - exactly one Canceled bracket, text "Cut cancelled";
//   - NO Exit bracket at all;
//   - NO cutFinished();
//   - the cut directory is EMPTY afterwards (every file the run created is
//     gone -- the control run shows what a completed run leaves there, so
//     the check is not vacuous; a separate cleanup-disabled probe, not run by
//     this binary, corroborates it further, see the report);
//   - phase-specific evidence that the abort landed mid-phase, see below.
//
// Mid-phase evidence:
//   audio: armed on the first "Cutting audio track" message with percent >=
//          5, i.e. after TTAudioCutter::cut has already copied packets. A
//          completed audio cut reaches ~100 percent and is followed by the
//          MKA mux stage -- the harness requires the maximum observed audio
//          percent to stay below 95 AND no "Muxing" message at all, which
//          together exclude "aborted at the post-audio poll".
//   mux:   the working format is forced to AOF_OriginalMKA for this phase.
//          muxAudioOnly() (unlike the H.26x/MPEG-2 video mux) reports no
//          per-packet progress -- there is exactly one static "Muxing audio
//          tracks into MKA..." Step at value 0 before the call. The harness
//          arms on that single message and relies on TTMkvMergeProvider's
//          own per-packet checkAbort() poll (see ttmkvmergeprovider.cpp) to
//          land the cancel inside the write loop rather than before or after
//          it: the audio material kept by the default cut ranges is tens of
//          seconds long, so the copy loop runs measurably longer than the
//          cross-thread queued delivery of the cancel. The elapsed time
//          between the arm and the Canceled bracket is printed for
//          corroboration, and the report documents the size-based,
//          cleanup-disabled probe that supplies the non-vacuity evidence
//          percent progress would otherwise have given.
//   none:  control run, no abort. Requires exactly one Init, one Exit
//          ("Audio cut complete"), one cutFinished(), no Canceled, and a
//          non-empty cut directory.
#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <atomic>
#include <cstdio>

#include "avstream/ttavtypes.h"
#include "avstream/ttavstream.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttcut.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"

namespace {

struct Event {
  int     state;
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
  // "mux" needs the MKA path so its single mux Step actually fires; the other
  // phases use plain ES output (the default working format).
  TTSettings::instance()->setWorkingAudioOnlyFormat(
      phase == "mux" ? TTCut::AOF_OriginalMKA : TTCut::AOF_OriginalES);

  TTAVData avData;
  avData.setNonInteractive(true);          // no modal burst dialog

  TTCutList cutList;
  for (const auto& c : cuts) cutList.append(avItem, c.first, c.second);

  Recorder rec;
  std::atomic<bool>  armed { false };
  std::atomic<int>   cutFinishedCount { 0 };
  std::atomic<bool>  terminalSeen { false };
  QElapsedTimer      armTimer;
  std::atomic<qint64> armToCancelMs { -1 };

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

  QObject::connect(&avData,
      qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
      &avData,
      [&](TTThreadTask*, int state, const QString& msg, quint64 value) {
        rec.add(state, msg, value);

        if (state == StatusReportArgs::Canceled) {
          if (armed.load()) armToCancelMs.store(armTimer.elapsed());
          scheduleQuit();
        }
        if (state == StatusReportArgs::Exit)
          scheduleQuit();

        if (phase == "none" || armed.load()) return;
        if (state != StatusReportArgs::Step) return;

        bool arm = false;
        if (phase == "audio") {
          if (msg.contains("Cutting audio track") && value >= 5) arm = true;
        } else if (phase == "mux") {
          if (msg.contains("Muxing audio tracks into MKA")) arm = true;
        }

        if (!arm) return;
        if (armed.exchange(true)) return;
        armTimer.start();
        fprintf(stderr, "  arm at [%s] \"%s\" %llu\n",
                stateName(state), qPrintable(msg), (unsigned long long)value);
        // Queued: the cancel must run on the GUI thread, like the real one.
        QMetaObject::invokeMethod(&avData, "onUserAbortRequest", Qt::QueuedConnection);
      },
      Qt::DirectConnection);

  // Emergency brake only -- a run that needs this has already failed.
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(900000);

  QElapsedTimer timer;
  timer.start();
  avData.onDoCut(target, &cutList, /*audioOnly=*/true);
  qApp->exec();
  const qint64 elapsed = timer.elapsed();

  // ---- evaluation -----------------------------------------------------
  const QList<Event> events = rec.events();
  int  initCount = 0, exitCount = 0, cancelCount = 0;
  QString exitMsg, cancelMsg;
  bool sawAudioMsg = false, sawMuxMsg = false;
  quint64 maxAudioPercent = 0;

  for (const Event& e : events) {
    if (e.state == StatusReportArgs::Init)     initCount++;
    if (e.state == StatusReportArgs::Exit)   { exitCount++;   exitMsg   = e.msg; }
    if (e.state == StatusReportArgs::Canceled) { cancelCount++; cancelMsg = e.msg; }
    if (e.state != StatusReportArgs::Step) continue;
    if (e.msg.contains("Cutting audio track")) {
      sawAudioMsg = true;
      maxAudioPercent = qMax(maxAudioPercent, e.value);
    }
    if (e.msg.contains("Muxing audio tracks into MKA")) sawMuxMsg = true;
  }

  printf("  phase=%-5s events=%d  init=%d exit=%d cancel=%d cutFinished=%d  %lld ms\n",
         qPrintable(phase), (int)events.size(), initCount, exitCount, cancelCount,
         cutFinishedCount.load(), (long long)elapsed);
  printf("  audioMsg=%d(max %llu%%) muxMsg=%d  armToCancel=%lld ms\n",
         sawAudioMsg ? 1 : 0, (unsigned long long)maxAudioPercent, sawMuxMsg ? 1 : 0,
         (long long)armToCancelMs.load());
  const QStringList left = dirEntries(workDir);
  printf("  cut dir after run: %s\n",
         left.isEmpty() ? "(empty)" : qPrintable(left.join(", ")));

  if (phase == "none") {
    if (initCount != 1)   return fail("control: expected exactly one Init");
    if (exitCount != 1)   return fail(QString("control: expected exactly one Exit, got %1").arg(exitCount));
    if (exitMsg != "Audio cut complete")
      return fail(QString("control: unexpected Exit text \"%1\"").arg(exitMsg));
    if (cancelCount != 0) return fail("control: unexpected Canceled");
    if (cutFinishedCount.load() != 1)
      return fail(QString("control: expected one cutFinished(), got %1").arg(cutFinishedCount.load()));
    if (left.isEmpty())   return fail("control: cut directory is empty - nothing was produced");
    return 0;
  }

  if (!armed.load())     return fail(phase + ": never reached the arming message");
  if (cancelCount != 1)  return fail(QString("%1: expected exactly one Canceled, got %2").arg(phase).arg(cancelCount));
  if (cancelMsg != "Cut cancelled")
    return fail(QString("%1: unexpected Canceled text \"%2\"").arg(phase, cancelMsg));
  if (exitCount != 0)
    return fail(QString("%1: an Exit bracket followed the cancel (\"%2\")").arg(phase, exitMsg));
  if (cutFinishedCount.load() != 0)
    return fail(QString("%1: cutFinished() was emitted on a cancelled run").arg(phase));
  if (!left.isEmpty())
    return fail(QString("%1: files left behind: %2").arg(phase, left.join(", ")));

  if (phase == "audio") {
    if (!sawAudioMsg)   return fail("audio: no audio progress observed");
    if (maxAudioPercent >= 95)
      return fail(QString("audio: progress reached %1%% - the track was cut to the end")
                      .arg(maxAudioPercent));
    if (sawMuxMsg)      return fail("audio: the mux phase started despite the abort");
  } else if (phase == "mux") {
    if (!sawMuxMsg)     return fail("mux: no mux Step observed - never reached the arm point");
  }

  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  if (argc < 5) {
    fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir> <phase> "
                    "[cutIn cutOut cutIn cutOut]\n", argv[0]);
    return 2;
  }
  const QString videoFile = argv[1];
  const QString audioFile = argv[2];
  const QString workDir   = argv[3];
  const QString phase     = argv[4];

  QList<QPair<int,int>> cuts;
  for (int i = 5; i + 1 < argc; i += 2)
    cuts.append(qMakePair(QString(argv[i]).toInt(), QString(argv[i+1]).toInt()));
  if (cuts.isEmpty()) cuts << qMakePair(500, 1499) << qMakePair(3000, 3999);

  if (phase != "audio" && phase != "mux" && phase != "none")
    return fail("unknown phase (use audio|mux|none)");

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

  const QString target =
      QFileInfo(QDir(workDir), QFileInfo(videoFile).completeBaseName()).absoluteFilePath();

  printf("== %s: abort during the %s phase ==\n",
         qPrintable(QFileInfo(videoFile).fileName()), qPrintable(phase));
  int rc = runCut(avItem, workDir, target, cuts, phase);
  if (rc != 0) return rc;

  if (phase != "none") {
    // Restart the very same cut in the same process: a cancelled run must
    // leave nothing behind that stops the next one (brief step 3).
    printf("== restart after cancel ==\n");
    rc = runCut(avItem, workDir, target, cuts, "none");
    if (rc != 0) return rc;
  }

  printf("PASS\n");
  return 0;
}
