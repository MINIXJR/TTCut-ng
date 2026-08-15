// The one ingredient the doH264Cut use-after-free hunt never had in the
// process: the preview dialog.
//
// Background (TODO.md, docs/completed-work.md): the crash (core.500359) died
// in QAbstractProxyModelPrivate::_q_sourceModelDestroyed, called from inside
// TTAVData::doH264Cut. The proxy is named - a file dialog brings a
// KDirSortFilterProxyModel (KIO) into the process - and the mechanism is
// coherent: doH264Cut re-enters the event loop through the qApp->processEvents()
// in cutAudioTracks/cutSubtitleTracks, and that is where the deferred deletions
// from dialogs destroyed with plain `delete` are carried out.
//
// Four ASAN runs came back clean. What none of them contained, and the crash
// sequence did: TTCutPreview. Its constructor builds a TTMpvWrapper and puts
// mpv's render widget - a QOpenGLWidget - into the dialog, and
// TTCutMainWindow destroys the dialog with plain `delete` right before the cut
// (ttcutmainwindow.cpp:1155). A QOpenGLWidget has been the cause in this
// project once already (f87ea06c).
//
// This harness therefore runs, in one process and in the GUI's order:
//
//   file dialog (KIO proxy) -> delete
//   TTCutPreview            -> delete
//   real H.264 cut through TTAVData::onDoCut
//
// ASAN does not need the rare timing that produced the original crash: it
// reports the access to freed memory whenever the path is executed at all. The
// point is completeness of the path, not luck.
//
// NOT offscreen: without a platform theme there is no KIO and no proxy model,
// and mpv gets no GL context - the run would prove nothing and look like it
// did. The harness refuses rather than produce that.
//
//   usage: test_preview_then_cut <video-es> <audio-es> <workdir> [cutIn cutOut]
//          PREVIEW_EXEC=1   drive the dialog modally with exec() + a timed
//                           reject(), the way TTCutMainWindow does, instead of
//                           show(). Closer to the original, but exec() has
//                           hung on a KIO dialog before - hence not default.
//          PREVIEW_FULL=1   run the WHOLE preview the GUI runs, not just the
//                           dialog's constructor: doCutPreview() generates the
//                           clips, cutPreviewFinished carries the preview cut
//                           list into initPreview(), and mpv actually loads
//                           them. Implies PREVIEW_EXEC. This is the last known
//                           difference to the sequence that crashed.
//
// Build: cmake --build build --target test_preview_then_cut
// Run under ASAN for the finding that matters; put the ASAN binary somewhere
// else first (both builds write into tools/diag/ and overwrite each other).
#include <QAbstractProxyModel>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <cstdio>

#include "avstream/ttavstream.h"
#include "avstream/ttavtypes.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "gui/ttcutpreview.h"

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

void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) qApp->processEvents();
}

} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    fprintf(stderr, "REFUSING: offscreen loads no platform theme (no KIO, no proxy)\n"
                    "and gives mpv no GL context - the run would prove nothing.\n");
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
  TTMessageLogger::getInstance()->setLogFilePath(
      QDir(workDir).absoluteFilePath("preview_then_cut.log"));

  printf("platform: %s\n", qPrintable(qApp->platformName()));

  // A stand-in for the main window: TTCutPreview is constructed with the main
  // window as parent, and the parent is what keeps a destroyed child's
  // deferred deletions company in the event loop.
  QWidget mainStandIn;
  mainStandIn.resize(400, 300);
  mainStandIn.setWindowTitle("preview_then_cut: main window stand-in");
  mainStandIn.show();
  pump(200);

  // ---- Phase 1: the file dialog, created and destroyed as TTCut does -------
  {
    QFileDialog* dlg = new QFileDialog(&mainStandIn, "probe", QDir::homePath());
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);
    dlg->show();
    pump(800);

    const QStringList proxies = proxyModelsInTree(dlg);
    printf("phase 1 - proxy models while the dialog was open: %s\n",
           proxies.isEmpty() ? "NONE" : qPrintable(proxies.join(", ")));
    if (proxies.isEmpty())
      printf("  WARNING: no proxy model - this run cannot show the suspected\n"
             "  interaction, whatever the rest does.\n");

    dlg->close();
    delete dlg;                     // plain delete, as TTCutMainWindow does
  }
  pump(200);

  // ---- Phase 2: the material, built before the preview because the preview
  // ---- needs the very cut list the cut will use ----------------------------
  TTVideoType    vType(videoFile);
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
    fprintf(stderr, "FAIL: cut bounds %d..%d do not fit %d frames\n",
            cutIn, cutOut, frameCount);
    return 1;
  }
  printf("phase 2 - source: %d frames at %.3f fps, cut %d..%d\n",
         frameCount, vStream->frameRate(), cutIn, cutOut);

  TTAVData avData;
  avData.setNonInteractive(true);
  TTCutList cutList;
  cutList.append(avItem, cutIn, cutOut);

  std::atomic<int>  cutFinishedCount { 0 };
  std::atomic<bool> terminalSeen { false };
  // cutFinished, NOT threadPoolExit: the pool drains its queue while the cut
  // is still running on this path (measured; see test_dialog_then_cut).
  auto scheduleQuit = [&terminalSeen] {
    if (terminalSeen.exchange(true)) return;
    QTimer::singleShot(3000, qApp, &QCoreApplication::quit);
  };
  QObject::connect(&avData, &TTAVData::cutFinished, [&cutFinishedCount, scheduleQuit] {
    cutFinishedCount++;
    scheduleQuit();
  });

  // ---- Phase 3: the preview dialog - the piece that was always missing -----
  {
    const bool full = !qgetenv("PREVIEW_FULL").isEmpty();
    const bool modal = full || !qgetenv("PREVIEW_EXEC").isEmpty();

    TTCutList* previewCutList = nullptr;
    if (full) {
      // What the GUI does first: generate the preview clips. The dialog is
      // only created once cutPreviewFinished delivers the preview cut list.
      QEventLoop previewLoop;
      QObject::connect(&avData, &TTAVData::cutPreviewFinished,
                       [&previewCutList, &previewLoop](TTCutList* list) {
                         previewCutList = list;
                         previewLoop.quit();
                       });
      QTimer previewGuard;
      previewGuard.setSingleShot(true);
      QObject::connect(&previewGuard, &QTimer::timeout, [&previewLoop] {
        printf("  WATCHDOG: cutPreviewFinished never arrived\n");
        previewLoop.quit();
      });
      previewGuard.start(600000);

      printf("phase 3 - generating preview clips (doCutPreview)\n");
      QElapsedTimer pt; pt.start();
      avData.doCutPreview(&cutList);
      previewLoop.exec();
      previewGuard.stop();
      printf("phase 3 - preview clips after %lld ms, cut list %s\n",
             (long long)pt.elapsed(), previewCutList ? "delivered" : "MISSING");
    }

    TTCutPreview* preview = new TTCutPreview(&mainStandIn);
    printf("phase 3 - TTCutPreview constructed (TTMpvWrapper + render widget)\n");
    if (previewCutList != nullptr) {
      preview->initPreview(previewCutList, &cutList, &avData, false, false);
      printf("phase 3 - initPreview done (mpv loads the clips)\n");
    }

    if (modal) {
      QTimer::singleShot(full ? 4000 : 1500, preview, &QDialog::reject);

      // The watchdog must not outlive the dialog. The first version used
      // QTimer::singleShot with a captured raw pointer, and when exec()
      // returned normally the timer still fired seconds later - inside
      // doH264Cut's processEvents() - and called close() on the deleted
      // dialog. ASAN caught that as a SEGV whose backtrace looks deceptively
      // like the defect being hunted (processEvents, doH264Cut right below).
      // A QPointer plus an owned timer that is stopped on return keeps the
      // harness from manufacturing its own finding.
      QPointer<TTCutPreview> guard(preview);
      QTimer previewWatchdog;
      previewWatchdog.setSingleShot(true);
      QObject::connect(&previewWatchdog, &QTimer::timeout, [&guard] {
        if (guard) {
          printf("  WATCHDOG: exec() did not return, forcing close\n");
          guard->close();
        }
      });
      previewWatchdog.start(full ? 30000 : 8000);

      preview->exec();
      previewWatchdog.stop();
      printf("phase 3 - exec() returned\n");
    } else {
      preview->show();
      pump(1500);
      preview->close();
    }

    delete preview;                 // ttcutmainwindow.cpp:1155
    printf("phase 3 - preview deleted\n");
  }
  pump(300);

  // ---- Phase 4: the real cut, whose processEvents() collects the deferred --
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
  watchdog.start(1800000);

  QElapsedTimer timer; timer.start();
  avData.onDoCut(QDir(workDir).absoluteFilePath("preview_then_cut.mkv"), &cutList, false);
  qApp->exec();

  printf("\ncut ran %lld ms, cutFinished fired %d time(s)\n",
         (long long)timer.elapsed(), cutFinishedCount.load());
  const QStringList left = proxyModelsInTree(nullptr);
  printf("proxy models still reachable at the end: %s\n",
         left.isEmpty() ? "none" : qPrintable(left.join(", ")));
  printf("\nNo crash in this run. Under ASAN that is a statement about this\n"
         "shape; without it, much less.\n");
  return 0;
}
