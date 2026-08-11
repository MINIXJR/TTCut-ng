// Abort harness for the H.264/H.265 final cut (TTH26xCutTask), driven through
// the real TTAVData + TTThreadTaskPool chain -- no GUI, no main window.
//
// Usage: test_h26xcut_abort <video-es> <audio-es> <workdir> <phase> [cutIn cutOut cutIn cutOut]
//   phase = video | audio | mux | none
//
// What it does, per invocation:
//   1. Opens the elementary streams the way TTOpenVideoTask/TTOpenAudioTask do
//      (TTVideoType/TTAudioType factory + createHeaderList + createIndexList +
//      sortDisplayOrder), builds a TTAVItem and a two-cut TTCutList.
//   2. Calls TTAVData::onDoCut() -- the same entry point the GUI and
//      --auto-cut use. It routes to doH264Cut(), which starts TTH26xCutTask on
//      the pool and returns; the harness then enters the event loop.
//   3. Records EVERY TTAVData::statusReport in emission order (direct
//      connection, so worker-thread emissions are seen where they happen) plus
//      every cutFinished().
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
//   - NO Exit bracket at all -- neither onH26xCutFinished's ("H.264/H.265
//     cutting complete") nor onThreadPoolExit's "exiting thread pool". The
//     latter's absence is what proves mCutOperationActive was still set when
//     onThreadPoolExit ran, i.e. that its else-branch (the reset) is the one
//     that executed;
//   - NO cutFinished();
//   - the cut directory is EMPTY afterwards (every file the run created is
//     gone -- the control run shows what a completed run leaves there, so the
//     check is not vacuous);
//   - phase-specific evidence that the abort landed mid-phase, see below.
//
// Mid-phase evidence (this is the part that can silently go vacuous, so each
// case states what would have been observed had the abort landed late):
//   video: armed on the 3rd "...segment..." message of TTESSmartCut. A
//          completed smartCutFrames() ALWAYS emits "Cut complete" at 100 --
//          its absence therefore proves the run stopped inside the video
//          phase, not at the poll point behind it.
//   audio: armed on the first "Cutting audio track" message with percent >= 5,
//          i.e. after cutAudioStream has already copied packets. A completed
//          audio cut reaches ~100 percent and is followed by the mux stage --
//          the harness requires the maximum observed audio percent to stay
//          below 95 AND no "Muxing" message at all, which together exclude
//          "aborted at the post-audio poll".
//   mux:   armed on the first "Muxing..." message with percent >= 2. There is
//          NO poll point behind a successful mux (deliberately, see
//          operation()), so a Canceled outcome after the mux has started can
//          only come from TTMkvMergeProvider's own poll. The maximum observed
//          mux percent is printed as corroboration.
//   none:  control run, no abort. Requires exactly one Init, one Exit
//          ("H.264/H.265 cutting complete"), one cutFinished(), no Canceled,
//          and a non-empty cut directory.
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
  void clear()
  {
    QMutexLocker lock(&mMutex);
    mEvents.clear();
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

  TTAVData avData;
  avData.setNonInteractive(true);          // no modal burst dialog

  TTCutList cutList;
  for (const auto& c : cuts) cutList.append(avItem, c.first, c.second);

  Recorder rec;
  std::atomic<bool> armed { false };
  std::atomic<int>  segMsgs { 0 };
  std::atomic<int>  cutFinishedCount { 0 };
  std::atomic<bool> terminalSeen { false };

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

        if (state == StatusReportArgs::Canceled || state == StatusReportArgs::Exit)
          scheduleQuit();

        if (phase == "none" || armed.load()) return;
        if (state != StatusReportArgs::Step) return;

        bool arm = false;
        if (phase == "video") {
          // 3rd segment message: several more segments/chunks follow, so the
          // abort cannot coincide with the end of the phase.
          if (msg.contains("segment") && ++segMsgs >= 3) arm = true;
        } else if (phase == "audio") {
          if (msg.contains("Cutting audio track") && value >= 5) arm = true;
        } else if (phase == "mux") {
          if (msg.contains("Muxing") && value >= 2) arm = true;
        }

        if (!arm) return;
        if (armed.exchange(true)) return;
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
  avData.onDoCut(target, &cutList, false);
  qApp->exec();
  const qint64 elapsed = timer.elapsed();

  // ---- evaluation -----------------------------------------------------
  const QList<Event> events = rec.events();
  int  initCount = 0, exitCount = 0, cancelCount = 0;
  QString exitMsg, cancelMsg;
  bool sawCutComplete = false, sawAudioMsg = false, sawMuxMsg = false;
  quint64 maxAudioPercent = 0, maxMuxPercent = 0;

  for (const Event& e : events) {
    if (e.state == StatusReportArgs::Init)     initCount++;
    if (e.state == StatusReportArgs::Exit)   { exitCount++;   exitMsg   = e.msg; }
    if (e.state == StatusReportArgs::Canceled) { cancelCount++; cancelMsg = e.msg; }
    if (e.state != StatusReportArgs::Step) continue;
    if (e.msg.contains("Cut complete"))        sawCutComplete = true;
    if (e.msg.contains("Cutting audio track")) {
      sawAudioMsg = true;
      maxAudioPercent = qMax(maxAudioPercent, e.value);
    }
    if (e.msg.contains("Muxing")) {
      sawMuxMsg = true;
      maxMuxPercent = qMax(maxMuxPercent, e.value);
    }
  }

  printf("  phase=%-5s events=%d  init=%d exit=%d cancel=%d cutFinished=%d  %lld ms\n",
         qPrintable(phase), (int)events.size(), initCount, exitCount, cancelCount,
         cutFinishedCount.load(), (long long)elapsed);
  printf("  cutComplete=%d audioMsg=%d(max %llu%%) muxMsg=%d(max %llu%%)\n",
         sawCutComplete ? 1 : 0, sawAudioMsg ? 1 : 0,
         (unsigned long long)maxAudioPercent, sawMuxMsg ? 1 : 0,
         (unsigned long long)maxMuxPercent);
  const QStringList left = dirEntries(workDir);
  printf("  cut dir after run: %s\n",
         left.isEmpty() ? "(empty)" : qPrintable(left.join(", ")));

  if (phase == "none") {
    if (initCount != 1)   return fail("control: expected exactly one Init");
    if (exitCount != 1)   return fail(QString("control: expected exactly one Exit, got %1").arg(exitCount));
    if (exitMsg != "H.264/H.265 cutting complete")
      return fail(QString("control: unexpected Exit text \"%1\"").arg(exitMsg));
    if (cancelCount != 0) return fail("control: unexpected Canceled");
    if (cutFinishedCount.load() != 1)
      return fail(QString("control: expected one cutFinished(), got %1").arg(cutFinishedCount.load()));
    if (left.isEmpty())   return fail("control: cut directory is empty - nothing was produced");
    if (!left.contains(QFileInfo(target).completeBaseName() + ".mkv"))
      return fail("control: no .mkv produced");
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

  if (phase == "video") {
    if (sawCutComplete) return fail("video: Smart Cut ran to completion (abort landed behind the phase)");
    if (sawAudioMsg)    return fail("video: the audio phase started despite the abort");
  } else if (phase == "audio") {
    if (!sawAudioMsg)   return fail("audio: no audio progress observed");
    if (maxAudioPercent >= 95)
      return fail(QString("audio: progress reached %1%% - the track was cut to the end")
                      .arg(maxAudioPercent));
    if (sawMuxMsg)      return fail("audio: the mux phase started despite the abort");
  } else if (phase == "mux") {
    if (!sawMuxMsg)     return fail("mux: no mux progress observed");
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

  if (phase != "video" && phase != "audio" && phase != "mux" && phase != "none")
    return fail("unknown phase (use video|audio|mux|none)");

  // Open the streams exactly like TTOpenVideoTask / TTOpenAudioTask do.
  TTVideoType   vType(videoFile);
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr) return fail("could not create the video stream");
  if (vStream->createHeaderList() <= 0) return fail("createHeaderList failed");
  if (vStream->createIndexList()  <= 0) return fail("createIndexList failed");
  if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

  TTAVItem* avItem = new TTAVItem(vStream);

  // Same codec-dependent encoder setup that TTCutMainWindow::runAutoCutMode()
  // does. Without it the transient encoder values keep their MPEG-2 defaults
  // and the re-encoded GOPs come out at a different CRF than in a real run.
  TTSettings::instance()->setEncoderCodec(
      vStream->streamType() == TTAVTypes::h265_video ? 2 :
      vStream->streamType() == TTAVTypes::h264_video ? 1 : 0);

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
    // leave nothing behind that stops the next one (brief step 4.3).
    printf("== restart after cancel ==\n");
    rc = runCut(avItem, workDir, target, cuts, "none");
    if (rc != 0) return rc;
  }

  printf("PASS\n");
  return 0;
}
