// Cross-operation abort harness: SEVERAL cut operations on ONE long-lived
// TTAVData, driven through the real TTAVData + TTThreadTaskPool chain -- no
// GUI, no main window.
//
// Why this exists next to the per-pipeline harnesses: every one of them
// (test_h26xcut_abort, test_mpeg2cut_abort, test_audioonlycut_abort,
// test_previewcut_abort) constructs a FRESH TTAVData per run, so their
// "restart after cancel" step restarts in the same process but against a
// brand-new object -- precisely the construction that HIDES a leftover flag
// or a surviving signal connection. The application owns exactly one TTAVData
// for the whole session (TTCutMainWindow), so the shared surface the abort
// work accumulated (mCutOperationActive, mMuxPoolRunActive, mSyncPhaseAbort,
// mCutProducedFiles and the connect/disconnect bookkeeping around the pool)
// had no runtime coverage across operations at all. This harness is that
// coverage.
//
// Usage: test_cutsequence_abort <h264-es> <h264-ac3> <m2v> <mp2> <workdir>
//
// The six operations, in order, all on the same TTAVData:
//   1. H.26x cut, cancelled during the video phase.
//   2. MPEG-2 MKV cut on the same object, run to COMPLETION. Afterwards the
//      harness probes that no `aborted -> onCutAborted` connection survived
//      the success path: onCutAborted() deletes every file in
//      mCutProducedFiles, so a connection left behind here would let a later
//      cancel of ANY pool operation reach a file-deleting slot. The probe is
//      QObject::disconnect()'s return value -- true means something was still
//      connected, i.e. finishMpeg2Cut() failed to drop it.
//   3. H.26x cut cancelled AFTER that successful MPEG-2 cut. Exactly one
//      Canceled: a duplicated connection would show up as two.
//   4. H.26x PREVIEW, cancelled. The preview must close the operation with
//      Canceled, not with the pool's own "exiting thread pool" Exit -- an
//      Exit forces the progress bar to 100% and makes the dialog report
//      "Finished after ..." for a run the user cancelled.
//   5. H.26x cut to completion after the cancelled preview: the cancelled
//      preview must not leave anything behind that spoils the next cut.
//   6. H.26x preview run to COMPLETION, then an H.26x cut cancelled. The
//      preview's own `aborted -> onCutPreviewAborted` connection has to be
//      dropped on its success path too, for the same reason as step 2 --
//      otherwise the later cancel emits the preview's Canceled bracket on
//      top of the cut's own, and the operation reports two closing brackets.
//   7. TWO streams opened through the real openAVStreams(), the FIRST made
//      current, then an H.26x cut cancelled. openAVStreams() arms
//      `aborted -> onOpenAVStreamsAborted` at every open and its only other
//      disconnect sits inside that slot, so the connection used to survive
//      every successful open for the whole session: the cancel then ran that
//      slot too, which sets the current AV item to the LAST one and emits
//      currentAVItemChanged() -- in the GUI a full stream switch performed
//      inside the cut's abort teardown. Needs TWO items: with a single one
//      the assignment is a no-op and the defect is invisible.
//
// Cancels are posted as QUEUED invocations of onUserAbortRequest() on the GUI
// thread -- exactly the route the Cancel button takes (TTProgressBar::cancel
// -> TTAVData::onUserAbortRequest), never from a worker thread.
//
// Non-vacuity: every cancelled step asserts it actually armed (a run that
// never reached its arming message fails instead of passing silently), and
// the completed steps assert their products exist, so "cut directory empty
// after a cancel" is evidence of something removed rather than of a run that
// never produced anything.
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
#include "common/ttthreadtaskpool.h"
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

//! Bracket counts of one operation, as the progress dialog sees them.
struct Brackets {
  int     init   = 0;
  int     exit   = 0;
  int     cancel = 0;
  QString exitMsg;
  QString cancelMsg;
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

Recorder          gRec;
std::atomic<int>  gCutFinished    { 0 };
std::atomic<int>  gAvReload       { 0 };
std::atomic<int>  gPreviewDone    { 0 };
std::atomic<int>  gCurrentChanged { 0 };
TTAVItem*         gLastCurrent    = nullptr;
std::atomic<bool> gArmed          { false };
std::atomic<bool> gTerminal       { false };
QString           gPhase;                   //!< none | video | preview

//! Reset the per-operation recorders. gPhase drives the arming rule in the
//! statusReport lambda: "none" never arms, "video" arms on the Smart Cut's
//! segment messages, "preview" on the preview task's per-clip message.
void beginStep(const QString& phase)
{
  gRec.clear();
  gCutFinished    = 0;
  gAvReload       = 0;
  gPreviewDone    = 0;
  gCurrentChanged = 0;
  gLastCurrent    = nullptr;
  gArmed          = false;
  gTerminal       = false;
  gPhase          = phase;
}

Brackets tally()
{
  Brackets b;
  for (const Event& e : gRec.events()) {
    if (e.state == StatusReportArgs::Init)     b.init++;
    if (e.state == StatusReportArgs::Exit)   { b.exit++;   b.exitMsg   = e.msg; }
    if (e.state == StatusReportArgs::Canceled) { b.cancel++; b.cancelMsg = e.msg; }
  }
  return b;
}

void printBrackets(const Brackets& b, const QStringList& left)
{
  printf("  init=%d exit=%d cancel=%d cutFinished=%d avReload=%d previewFinished=%d\n",
         b.init, b.exit, b.cancel, gCutFinished.load(), gAvReload.load(),
         gPreviewDone.load());
  printf("  exitMsg=\"%s\" cancelMsg=\"%s\"\n",
         qPrintable(b.exitMsg), qPrintable(b.cancelMsg));
  printf("  dir: %s\n", left.isEmpty() ? "(empty)" : qPrintable(left.join(", ")));
}

//! One final-cut operation through TTAVData::onDoCut(), the entry point the
//! GUI and --auto-cut use. phase "none" runs it to completion, "video"
//! cancels it once the Smart Cut is inside the video phase.
int runCut(TTAVData& avData, TTAVItem* avItem, const QString& workDir,
           const QString& target, const QList<QPair<int,int>>& cuts,
           const QString& phase, const QString& label)
{
  clearDir(workDir);
  beginStep(phase);

  TTCutList* cutList = avData.cutList();
  cutList->clear();
  for (const auto& c : cuts) cutList->append(avItem, c.first, c.second);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(600000);

  printf("== %s ==\n", qPrintable(label));
  avData.onDoCut(target, cutList, false);
  qApp->exec();

  const Brackets    b    = tally();
  const QStringList left = dirEntries(workDir);
  printBrackets(b, left);

  if (phase == "none") {
    if (b.init != 1)   return fail(label + ": expected one Init, got " + QString::number(b.init));
    if (b.exit != 1)   return fail(label + ": expected one Exit, got " + QString::number(b.exit));
    if (b.cancel != 0) return fail(label + ": unexpected Canceled");
    if (gCutFinished.load() != 1) return fail(label + ": expected one cutFinished()");
    if (gAvReload.load() != 1)
      return fail(label + ": expected one avDataReloaded(), got " + QString::number(gAvReload.load()));
    if (left.isEmpty()) return fail(label + ": nothing produced");
  } else {
    if (!gArmed.load()) return fail(label + ": never armed");
    if (b.cancel != 1)  return fail(label + ": expected exactly one Canceled, got " + QString::number(b.cancel));
    if (b.exit != 0)    return fail(label + ": an Exit followed the cancel (\"" + b.exitMsg + "\")");
    if (gCutFinished.load() != 0) return fail(label + ": cutFinished() on a cancelled run");
    if (!left.isEmpty()) return fail(label + ": files left behind: " + left.join(", "));
  }
  return 0;
}

//! One preview operation through TTAVData::doCutPreview(). phase "none" runs
//! it to completion, "preview" cancels it inside the clip loop.
int runPreview(TTAVData& avData, TTAVItem* avItem, const QString& tempDir,
               const QList<QPair<int,int>>& cuts, const QString& phase,
               const QString& label)
{
  clearDir(tempDir);
  beginStep(phase);

  TTCutList* cutList = avData.cutList();
  cutList->clear();
  for (const auto& c : cuts) cutList->append(avItem, c.first, c.second);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(600000);

  printf("== %s ==\n", qPrintable(label));
  avData.doCutPreview(cutList);
  qApp->exec();

  const Brackets    b    = tally();
  const QStringList left = dirEntries(tempDir);
  printBrackets(b, left);
  for (const Event& e : gRec.events())
    printf("    [%d] \"%s\" %llu\n", e.state, qPrintable(e.msg),
           (unsigned long long)e.value);

  if (phase == "none") {
    if (gPreviewDone.load() != 1) return fail(label + ": cutPreviewFinished did not fire exactly once");
    if (b.cancel != 0) return fail(label + ": unexpected Canceled on a completed preview");
    if (b.exit != 1)   return fail(label + ": expected one Exit, got " + QString::number(b.exit));
    if (!left.contains("preview_001.mkv")) return fail(label + ": preview_001.mkv missing");
  } else {
    if (!gArmed.load()) return fail(label + ": never armed");
    if (gPreviewDone.load() != 0) return fail(label + ": cutPreviewFinished fired despite the cancel");
    // The point of step 4: a cancelled preview must close with the
    // operation's own Canceled bracket. Before this was fixed the run ended
    // with the pool's Exit "exiting thread pool", which drives
    // TTProgressBar into setTotalProgress(100) and makes TTCutMainWindow
    // report "Finished after ..." for a cancelled operation.
    if (b.cancel != 1)
      return fail(label + ": expected exactly one Canceled, got " + QString::number(b.cancel));
    if (b.exit != 0)
      return fail(label + ": an Exit closed the cancelled preview (\"" + b.exitMsg + "\")");
    if (b.init != 1)
      return fail(label + ": expected one Init, got " + QString::number(b.init));
  }
  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  if (argc < 6) {
    fprintf(stderr, "usage: %s <h264-es> <h264-ac3> <m2v> <mp2> <workdir>\n", argv[0]);
    return 2;
  }
  const QString h264File = argv[1];
  const QString ac3File  = argv[2];
  const QString m2vFile  = argv[3];
  const QString mp2File  = argv[4];
  const QString workDir  = argv[5];

  const QString cutDir  = QDir(workDir).absoluteFilePath("cut");
  const QString tempDir = QDir(workDir).absoluteFilePath("temp");
  clearDir(cutDir);
  clearDir(tempDir);

  TTSettings::instance()->setCutDirPath(cutDir);
  TTSettings::instance()->setTempDirPath(tempDir);
  TTSettings::instance()->setWorkingOutputContainer(1);   // MKV
  TTSettings::instance()->setWorkingMuxDeleteES(false);
  // Short preview windows: the harness cares about brackets and connections,
  // not about preview length, and step 6 runs one preview to completion.
  TTSettings::instance()->setCutPreviewSeconds(10);

  // Open the streams exactly like TTOpenVideoTask / TTOpenAudioTask do (same
  // setup as the per-pipeline harnesses).
  auto openItem = [](const QString& video, const QString& audio) -> TTAVItem* {
    TTVideoType    vType(video);
    TTVideoStream* vStream = vType.createVideoStream();
    if (vStream == nullptr) return nullptr;
    if (vStream->createHeaderList() <= 0) return nullptr;
    if (vStream->createIndexList()  <= 0) return nullptr;
    if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

    TTAVItem*      item   = new TTAVItem(vStream);
    TTAudioType    aType(audio);
    TTAudioStream* aStream = aType.createAudioStream();
    if (aStream == nullptr) return nullptr;
    aStream->createHeaderList();
    item->appendAudioEntry(aStream);
    return item;
  };

  TTAVItem* h264Item = openItem(h264File, ac3File);
  if (h264Item == nullptr) return fail("could not open the H.264 streams");
  TTAVItem* mpeg2Item = openItem(m2vFile, mp2File);
  if (mpeg2Item == nullptr) return fail("could not open the MPEG-2 streams");

  // ONE long-lived TTAVData, like TTCutMainWindow has. Everything below runs
  // against this single object -- that is the whole point of this harness.
  TTAVData avData;
  avData.setNonInteractive(true);

  QObject::connect(&avData, &TTAVData::cutFinished,        [] { gCutFinished++; });
  QObject::connect(&avData, &TTAVData::avDataReloaded,     [] { gAvReload++; });
  QObject::connect(&avData, &TTAVData::cutPreviewFinished, [](TTCutList*) { gPreviewDone++; });
  QObject::connect(&avData, &TTAVData::currentAVItemChanged,
                   [](TTAVItem* item) { gCurrentChanged++; gLastCurrent = item; });

  QObject::connect(&avData,
      qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
      &avData,
      [&avData](TTThreadTask*, int state, const QString& msg, quint64 value) {
        gRec.add(state, msg, value);
        // Every operation ends with exactly one closing bracket; give the
        // trailing signals and deferred deletes a moment to settle before
        // the loop quits and the step inspects final state.
        if (state == StatusReportArgs::Canceled || state == StatusReportArgs::Exit) {
          if (!gTerminal.exchange(true))
            QMetaObject::invokeMethod(qApp, [] {
                QTimer::singleShot(2500, qApp, &QCoreApplication::quit); },
                Qt::QueuedConnection);
        }
        if (gPhase == "none" || gArmed.load()) return;
        if (state != StatusReportArgs::Step) return;
        bool arm = false;
        if (gPhase == "video"   && msg.contains("segment"))               arm = true;
        if (gPhase == "preview" && msg.contains("Creating preview clip")) arm = true;
        if (!arm) return;
        if (gArmed.exchange(true)) return;
        fprintf(stderr, "  arm at \"%s\" %llu\n", qPrintable(msg),
                (unsigned long long)value);
        // Queued invocation on the GUI thread: the Cancel button's own route.
        QMetaObject::invokeMethod(&avData, "onUserAbortRequest", Qt::QueuedConnection);
      },
      Qt::DirectConnection);

  const QString h264Target =
      QFileInfo(QDir(cutDir), QFileInfo(h264File).completeBaseName()).absoluteFilePath();
  const QString mpeg2Target =
      QFileInfo(QDir(cutDir), QFileInfo(m2vFile).completeBaseName()).absoluteFilePath();

  QList<QPair<int,int>> h264Cuts;  h264Cuts  << qMakePair(500, 1499)  << qMakePair(3000, 3999);
  QList<QPair<int,int>> mpeg2Cuts; mpeg2Cuts << qMakePair(100, 899)   << qMakePair(1300, 2099);
  QList<QPair<int,int>> onePreviewCut; onePreviewCut << qMakePair(1000, 2000);

  TTSettings::instance()->setEncoderCodec(1);   // H.264
  int rc = runCut(avData, h264Item, cutDir, h264Target, h264Cuts, "video",
                  "1. H.26x cut, cancelled during the video phase");
  if (rc) return rc;

  TTSettings::instance()->setEncoderCodec(0);   // MPEG-2
  rc = runCut(avData, mpeg2Item, cutDir, mpeg2Target, mpeg2Cuts, "none",
              "2. MPEG-2 MKV cut on the SAME TTAVData, run to completion");
  if (rc) return rc;

  // The MPEG-2 success path must leave no `aborted -> onCutAborted`
  // connection behind: that slot deletes every path in mCutProducedFiles, so
  // a surviving connection would put a file-deleting slot on the abort of
  // every LATER pool operation (a cancelled preview, a cancelled stream
  // open, a cancelled frame search). disconnect() returns true only if it
  // actually removed something.
  if (QObject::disconnect(avData.threadTaskPool(), &TTThreadTaskPool::aborted,
                          &avData, &TTAVData::onCutAborted))
    return fail("2: the MPEG-2 success path left an `aborted -> onCutAborted` "
                "connection behind (onCutAborted deletes files)");
  printf("  no stale `aborted -> onCutAborted` connection after the successful cut\n");

  TTSettings::instance()->setEncoderCodec(1);
  rc = runCut(avData, h264Item, cutDir, h264Target, h264Cuts, "video",
              "3. H.26x cut cancelled AFTER the successful MPEG-2 cut");
  if (rc) return rc;

  rc = runPreview(avData, h264Item, tempDir, h264Cuts, "preview",
                  "4. H.26x preview, cancelled");
  if (rc) return rc;

  rc = runCut(avData, h264Item, cutDir, h264Target, h264Cuts, "none",
              "5. H.26x cut to completion AFTER the cancelled preview");
  if (rc) return rc;

  rc = runPreview(avData, h264Item, tempDir, onePreviewCut, "none",
                  "6a. H.26x preview run to completion");
  if (rc) return rc;

  // A completed preview must drop its own `aborted -> onCutPreviewAborted`
  // connection as well; otherwise the next cancelled operation emits the
  // preview's Canceled bracket on top of its own and the run reports two
  // closing brackets. Checked behaviourally rather than with a disconnect()
  // probe, because probing would remove the very connection under test.
  rc = runCut(avData, h264Item, cutDir, h264Target, h264Cuts, "video",
              "6b. H.26x cut cancelled AFTER the completed preview");
  if (rc) return rc;

  // Step 7 needs streams opened through the REAL entry point: only
  // openAVStreams() arms `aborted -> onOpenAVStreamsAborted`, and only items
  // it produced live in mpAVList, which is what that slot walks. The items
  // steps 1-6 use are built by hand, exactly like the per-pipeline harnesses
  // do, and are deliberately NOT in the list.
  printf("== 7. two streams opened through openAVStreams(), then a cancelled "
         "H.26x cut ==\n");
  auto openThroughAVData = [&avData](const QString& path, int expectCount) {
    avData.openAVStreams(path);
    QElapsedTimer t;  t.start();
    while (avData.avCount() < expectCount && t.elapsed() < 180000)
      QApplication::processEvents(QEventLoop::AllEvents, 50);
    // Let the trailing queued signals (audio/subtitle tasks, pool exit) settle
    // before the next open re-arms the connection under test.
    QElapsedTimer settle;  settle.start();
    while (settle.elapsed() < 3000)
      QApplication::processEvents(QEventLoop::AllEvents, 50);
  };
  openThroughAVData(h264File, 1);
  openThroughAVData(m2vFile,  2);
  printf("  avCount=%d after two openAVStreams() calls\n", avData.avCount());
  if (avData.avCount() < 2)
    return fail("7: could not open two videos through openAVStreams()");

  TTAVItem* openedFirst = avData.avItemAt(0);
  TTAVItem* openedLast  = avData.avItemAt(avData.avCount() - 1);
  avData.onChangeCurrentAVItem(openedFirst);
  QApplication::processEvents();

  TTSettings::instance()->setEncoderCodec(1);
  rc = runCut(avData, openedFirst, cutDir, h264Target, h264Cuts, "video",
              "7. H.26x cut on the first opened stream, cancelled");
  if (rc) return rc;

  printf("  currentAVItemChanged during the cancel: %d (emitted == last? %s)\n",
         gCurrentChanged.load(), (gLastCurrent == openedLast) ? "yes" : "no");
  if (gCurrentChanged.load() != 0)
    return fail(QString("7: the cancel reached onOpenAVStreamsAborted() - "
                        "currentAVItemChanged fired %1 time(s)%2, i.e. a full "
                        "GUI stream switch inside the cut's abort teardown")
                .arg(gCurrentChanged.load())
                .arg(gLastCurrent == openedLast ? " carrying the LAST item" : ""));
  printf("  no stale `aborted -> onOpenAVStreamsAborted` connection after the "
         "stream opens\n");

  printf("PASS\n");
  return 0;
}
