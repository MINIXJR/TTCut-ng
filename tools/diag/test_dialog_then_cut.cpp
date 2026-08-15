// Combines the two halves of the doH264Cut use-after-free hunt into one run.
//
// Background: the crash (core.500359) died in
// QAbstractProxyModelPrivate::_q_sourceModelDestroyed, called from inside
// TTAVData::doH264Cut. Two separate probes have since been run, each covering
// one half of the suspected mechanism, and each came back clean:
//
//   tools/diag/test_filedialog_proxy   - the dialog half. Establishes that a
//       file dialog here brings a KDirSortFilterProxyModel (KIO) into the
//       process, i.e. exactly the class of object the crash operated on. 20
//       open/close/delete cycles under ASAN: no report.
//
//   --auto-cut on the original 03x01 material - the cut half. Full H.264
//       Smart Cut of the file identified from the core's strings, under ASAN:
//       no report, output complete (2810 s, the projected length exactly).
//       But --auto-cut loads its project directly: no open dialog, no preview
//       dialog, no cut dialog. That is precisely the difference to the
//       sequence that crashed.
//
// So neither half faults alone. This harness puts them in ONE process, in the
// order the GUI produces them:
//
//   file dialog opened and destroyed  ->  real H.264 cut through
//   TTAVData::onDoCut, whose doH264Cut re-enters the event loop via
//   cutAudioTracks'/cutSubtitleTracks' qApp->processEvents()
//
// That re-entry is where deferred deletions from the dialog teardown are
// carried out, and where the frames missing from the crash backtrace belong.
// If the mechanism is real, this is the shape that should show it.
//
// NOT offscreen, unlike the other cut harnesses: QT_QPA_PLATFORM=offscreen
// loads no platform theme, so no KIO, so no proxy model - the run would be
// the cut half again under a different name. The harness refuses to start
// there rather than produce a result that looks like evidence.
//
//   usage: test_dialog_then_cut <video-es> <audio-es> <workdir> [cutIn cutOut]
//
// Build: cmake --build build --target test_dialog_then_cut
// Run under ASAN for the finding that matters.
#include <QAbstractProxyModel>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QTimer>
#include <QWidget>

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

QStringList proxyModelsInTree(QObject* extraRoot)
{
  QStringList found;
  QList<QObject*> roots;
  for (QWidget* w : qApp->topLevelWidgets()) roots.append(w);
  if (extraRoot) roots.append(extraRoot);
  for (QObject* root : roots) {
    for (QObject* child : root->findChildren<QObject*>())
      if (auto* p = qobject_cast<QAbstractProxyModel*>(child))
        found << p->metaObject()->className();
  }
  found.removeDuplicates();
  return found;
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr, "REFUSING: offscreen loads no platform theme, so no KIO and "
                    "no proxy model - this would only re-run the cut half.\n");
    return 2;
  }
  if (argc < 4) {
    fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir> [cutIn cutOut]\n", argv[0]);
    return 2;
  }
  const QString videoFile = argv[1];
  const QString audioFile = argv[2];
  const QString workDir   = argv[3];
  const int argCutIn  = (argc > 4) ? QString(argv[4]).toInt() : -1;
  const int argCutOut = (argc > 5) ? QString(argv[5]).toInt() : -1;

  QDir().mkpath(workDir);
  TTSettings::instance()->setCutDirPath(workDir);
  TTSettings::instance()->setTempDirPath(workDir);
  TTMessageLogger::getInstance()->setLogFilePath(QDir(workDir).absoluteFilePath("dialog_then_cut.log"));

  printf("platform: %s\n", qPrintable(qApp->platformName()));

  // ---- Phase 1: the dialog, created and destroyed the way TTCut does -------
  {
    QFileDialog* dlg = new QFileDialog(nullptr, "probe", QDir::homePath());
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);
    dlg->show();
    QElapsedTimer t; t.start();
    while (t.elapsed() < 800) qApp->processEvents();

    const QStringList proxies = proxyModelsInTree(dlg);
    printf("proxy models while the dialog was open: %s\n",
           proxies.isEmpty() ? "NONE" : qPrintable(proxies.join(", ")));
    if (proxies.isEmpty())
      printf("  WARNING: no proxy model - this run cannot show the suspected\n"
             "  interaction, whatever the cut does.\n");

    dlg->close();
    // Plain delete, as TTCutMainWindow does to the cut dialog right before
    // onDoCut. Whatever the teardown hands to deleteLater() survives this and
    // is collected during the cut below.
    delete dlg;
  }

  // ---- Phase 2: a real cut, immediately after ------------------------------
  TTVideoType   vType(videoFile);
  TTVideoStream* vStream = vType.createVideoStream();
  if (vStream == nullptr) { fprintf(stderr, "FAIL: no video stream\n"); return 1; }
  if (vStream->createHeaderList() <= 0) { fprintf(stderr, "FAIL: createHeaderList\n"); return 1; }
  if (vStream->createIndexList()  <= 0) { fprintf(stderr, "FAIL: createIndexList\n"); return 1; }
  if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

  TTAVItem* avItem = new TTAVItem(vStream);
  TTSettings::instance()->setEncoderCodec(
      vStream->streamType() == TTAVTypes::h265_video ? 2 :
      vStream->streamType() == TTAVTypes::h264_video ? 1 : 0);

  TTAudioType    aType(audioFile);
  TTAudioStream* aStream = aType.createAudioStream();
  if (aStream == nullptr) { fprintf(stderr, "FAIL: no audio stream\n"); return 1; }
  aStream->createHeaderList();
  avItem->appendAudioEntry(aStream);

  const int frameCount = vStream->frameCount();
  const int cutIn  = (argCutIn  >= 0) ? argCutIn  : frameCount / 6;
  const int cutOut = (argCutOut >= 0) ? argCutOut : (frameCount * 2) / 3;
  if (cutIn < 0 || cutOut >= frameCount || cutIn >= cutOut) {
    fprintf(stderr, "FAIL: cut bounds %d..%d do not fit %d frames\n", cutIn, cutOut, frameCount);
    return 1;
  }
  printf("source: %d frames at %.3f fps, cut %d..%d\n",
         frameCount, vStream->frameRate(), cutIn, cutOut);

  TTAVData avData;
  avData.setNonInteractive(true);
  TTCutList cutList;
  cutList.append(avItem, cutIn, cutOut);

  std::atomic<int>  cutFinishedCount { 0 };
  std::atomic<bool> terminalSeen { false };
  // Wait for cutFinished, NOT for threadPoolExit. Measured the hard way: the
  // pool emits exit() as soon as its queue drains, which on this path happens
  // while the cut is still running - the first version of this harness left
  // the event loop after 901 ms, printed "cutFinished fired 0 time(s)", and
  // its process kept writing the output for another minute. Every number it
  // printed was about a cut that had not happened yet.
  auto scheduleQuit = [&terminalSeen] {
    if (terminalSeen.exchange(true)) return;
    // Trailing signals and deferred deletes get a moment before the loop
    // ends - the deferred deletes are the whole point of this harness.
    QTimer::singleShot(3000, qApp, &QCoreApplication::quit);
  };
  QObject::connect(&avData, &TTAVData::cutFinished, [&cutFinishedCount, scheduleQuit] {
    cutFinishedCount++;
    scheduleQuit();
  });

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(1800000);

  QElapsedTimer timer; timer.start();
  avData.onDoCut(QDir(workDir).absoluteFilePath("dialog_then_cut.mkv"), &cutList, false);
  qApp->exec();

  printf("\ncut ran %lld ms, cutFinished fired %d time(s)\n",
         (long long)timer.elapsed(), cutFinishedCount.load());
  printf("proxy models still reachable at the end: %s\n",
         proxyModelsInTree(nullptr).isEmpty() ? "none"
                                              : qPrintable(proxyModelsInTree(nullptr).join(", ")));
  printf("\nNo crash in this run. Under ASAN that is a real (if narrow)\n"
         "statement about this shape; without it, much less.\n");
  return 0;
}
