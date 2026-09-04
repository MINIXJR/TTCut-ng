// A cancelled MPEG-2 cut must not poison the NEXT one.
//
// Reported from the GUI (2026-08-15): after cancelling a preview, the next
// click on "preview" produced nothing, and only the one after that worked.
//
// Cause: TTCutVideoTask::onUserAbort() and TTCutTask::onUserAbort() push the
// cancel into the cut stream (mpCutStream->setAbort(true)). That stream is NOT
// per-operation - TTCutVideoTask::operation() takes it from
// cutItem.avDataItem()->videoStream(), the long-lived object the display
// widgets share. Every mAbort check clears the flag as it throws
// (ttmpeg2videostream.cpp:121, 244, 568), so the flag only disappears if a
// check is still reached. Cancel a preview after the stream has finished its
// work - easy, its phases last about a second - and the flag stays set on an
// object that outlives the operation. The next cut then aborts itself midway,
// clearing the flag, so the third attempt works: exactly the "only the second
// click does something" pattern.
//
// This harness does not race the clock. It sets the flag directly, the way a
// previous cancelled run would have left it, and then runs a perfectly normal
// cut:
//
//   vStream->setAbort(true)   ->   cut   ->   must complete
//
// Before the fix the cut dies with TTAbortException and cutFinished() never
// arrives. Non-vacuous by construction: the same run without the setAbort()
// call must pass, which the second phase below checks.
//
//   usage: test_stale_abort <video-m2v> <audio-mp2> <workdir>
#include <QApplication>
#include <QDir>
#include <QTimer>

#include <atomic>
#include <cstdio>

#include "avstream/ttavtypes.h"
#include "avstream/ttavstream.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttsettings.h"
#include "common/ttmessagelogger.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"

namespace {

int gFailures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) gFailures++;
}

// One cut run. Returns true if cutFinished() arrived.
bool runCut(TTAVItem* avItem, const QString& target,
            int cutIn, int cutOut, bool poisonFlagFirst)
{
  TTAVData avData;
  avData.setNonInteractive(true);

  TTCutList cutList;
  cutList.append(avItem, cutIn, cutOut);

  std::atomic<bool> finished { false };
  std::atomic<bool> terminalSeen { false };
  QObject::connect(&avData, &TTAVData::cutFinished, [&finished] { finished = true; });
  // Terminate on the operation's closing bracket, whichever it is. Waiting for
  // threadPoolExit alone would hang on the cancel path: onCutAborted() arms
  // mCutOperationActive so onThreadPoolExit() suppresses that very Exit.
  QObject::connect(&avData,
      qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
      &avData,
      [&terminalSeen](TTThreadTask*, int state, const QString&, quint64) {
        if (state != StatusReportArgs::Exit && state != StatusReportArgs::Canceled) return;
        if (terminalSeen.exchange(true)) return;
        QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
      },
      Qt::DirectConnection);

  if (poisonFlagFirst) {
    // Exactly what a cancelled run leaves behind when its cancel lands after
    // the stream is done: the flag set on the shared, long-lived stream.
    avItem->videoStream()->setAbort(true);
    printf("  (abort flag left set on the shared video stream, as a cancelled run would)\n");
  }

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(600000);

  avData.onDoCut(target, &cutList, false);
  qApp->exec();
  return finished.load();
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  if (argc < 4) {
    fprintf(stderr, "usage: %s <video-m2v> <audio-mp2> <workdir>\n", argv[0]);
    return 2;
  }
  const QString videoFile = argv[1];
  const QString audioFile = argv[2];
  const QString workDir   = argv[3];

  QDir().mkpath(workDir);
  TTSettings::instance()->setCutDirPath(workDir);
  TTSettings::instance()->setTempDirPath(workDir);
  TTMessageLogger::getInstance()->setLogFilePath(
      QDir(workDir).absoluteFilePath("stale_abort.log"));

  TTVideoType    vType(videoFile);
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr) { fprintf(stderr, "FAIL: no video stream\n"); return 1; }
  if (vStream->createHeaderList() <= 0) { fprintf(stderr, "FAIL: createHeaderList\n"); return 1; }
  if (vStream->createIndexList()  <= 0) { fprintf(stderr, "FAIL: createIndexList\n"); return 1; }
  if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

  TTAVItem* avItem = new TTAVItem(vStream);
  TTSettings::instance()->setEncoderCodec(0);   // MPEG-2

  TTAudioType    aType(audioFile);
  TTAudioStream* aStream = aType.createAudioStream();
  if (aStream == nullptr) { fprintf(stderr, "FAIL: no audio stream\n"); return 1; }
  aStream->createHeaderList();
  avItem->appendAudioEntry(aStream);

  const int frameCount = vStream->frameCount();
  const int cutIn  = frameCount / 6;
  const int cutOut = frameCount / 3;
  printf("source: %d frames, cut %d..%d\n", frameCount, cutIn, cutOut);

  // Phase 1 - control: no stale flag, the cut must simply work. Without this
  // the phase below could "pass" on material that cannot be cut at all.
  printf("\n=== control: clean stream ===\n");
  const bool cleanOk = runCut(avItem, QDir(workDir).absoluteFilePath("clean"),
                              cutIn, cutOut, false);
  check(cleanOk, "a normal cut completes (control - proves the setup can cut)");

  // Phase 2 - the reported defect.
  printf("\n=== with a stale abort flag from a previous cancelled run ===\n");
  const bool staleOk = runCut(avItem, QDir(workDir).absoluteFilePath("stale"),
                              cutIn, cutOut, true);
  check(staleOk, "a cut still completes after a previous run left the abort flag set");

  printf("\n%s\n", gFailures == 0 ? "PASS" : "FAILURES");
  return gFailures == 0 ? 0 : 1;
}
