// Acceptance harness for how a cut operation reports its outcome.
//
// The question this asks is narrow and specific: at the moment the Exit
// bracket arrives, does TTAVData::lastCutError() already hold the failure
// text? TTCutMainWindow::onStatusReport reads it exactly there to decide
// whether the run counts as regular - and a run that counts as regular feeds
// a calibration factor into the remaining-time estimator. Before this work
// the H.26x path emitted Exit three lines BEFORE assigning the field, so a
// failed cut arrived as a regular one (see TTAVData::onH26xCutFinished()).
//
// A failure is produced the same way it was first measured: a read-only
// output directory, which makes TTESSmartCut fail with "Cannot create output
// file".
//
//   usage: test_cut_outcome <video-es> <audio-es> <workdir> [cutIn cutOut ...]
//
// The cut bounds default to fractions of frameCount() rather than to fixed
// frame numbers: the previous hard-coded 500..1499 / 3000..3999 only fitted
// the 6000-frame 50fps H.264 source, and cut-out 3999 does not exist in the
// 3000-frame 25fps MPEG-2 source. On a 6000-frame source the derived default
// is arithmetically the old one, so existing H.264 results stay comparable.
//
// Build via `cmake --build build --target test_cut_outcome`.
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QString>
#include <QTimer>

#include <cstdio>

#include "avstream/ttavtypes.h"
#include "avstream/ttavstream.h"
#include "avstream/ttvideoindexlist.h"
#include "common/istatusreporter.h"
#include "common/ttsettings.h"
#include "common/ttthreadtask.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"

static int gFailures = 0;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) gFailures++;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 4) {
        fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir>\n", argv[0]);
        return 2;
    }
    const QString videoFile = QString::fromUtf8(argv[1]);
    const QString audioFile = QString::fromUtf8(argv[2]);
    const QString workDir   = QString::fromUtf8(argv[3]);

    // A directory we can create but not write into - this is what makes the
    // cut fail for a real reason instead of a simulated one.
    const QString roDir = workDir + "/readonly-out";
    QDir().mkpath(roDir);
    if (!QFile::setPermissions(roDir,
            QFile::ReadOwner | QFile::ExeOwner)) {
        fprintf(stderr, "cannot make %s read-only\n", qPrintable(roDir));
        return 2;
    }
    // Restore permissions on every exit path (including the early returns
    // below), so the workdir can always be cleaned up.
    auto permGuard = qScopeGuard([&] {
        QFile::setPermissions(roDir,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    });

    TTAVData avData;
    avData.setNonInteractive(true);

    // What the Exit handler saw, captured at the moment it arrived.
    bool    sawExit          = false;
    QString errorAtExitTime;
    QString bracketTextAtExitTime;

    QObject::connect(&avData,
        qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
        [&](TTThreadTask*, int state, const QString& message, quint64) {
            if (state == StatusReportArgs::Exit) {
                sawExit               = true;
                errorAtExitTime       = avData.lastCutError();
                bracketTextAtExitTime = message;
            }
        });

    // Open the streams exactly like TTOpenVideoTask / TTOpenAudioTask do -
    // same sequence as tools/diag/test_h26xcut_abort.cpp.
    TTVideoType    vType(videoFile);
    TTVideoStream* vStream = vType.createVideoStream();
    if (vStream == nullptr)             { fprintf(stderr, "no video stream\n");     return 2; }
    if (vStream->createHeaderList() <= 0) { fprintf(stderr, "createHeaderList\n"); return 2; }
    if (vStream->createIndexList()  <= 0) { fprintf(stderr, "createIndexList\n");  return 2; }
    if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

    TTAVItem* avItem = new TTAVItem(vStream);

    // Codec-dependent encoder setup, as TTCutMainWindow::runAutoCutMode() does.
    TTSettings::instance()->setEncoderCodec(
        vStream->streamType() == TTAVTypes::h265_video ? 2 :
        vStream->streamType() == TTAVTypes::h264_video ? 1 : 0);

    TTAudioType    aType(audioFile);
    TTAudioStream* aStream = aType.createAudioStream();
    if (aStream == nullptr) { fprintf(stderr, "no audio stream\n"); return 2; }
    aStream->createHeaderList();
    avItem->appendAudioEntry(aStream);

    // Target inside the read-only directory: this is what makes the cut fail.
    const QString target =
        QFileInfo(QDir(roDir), QFileInfo(videoFile).completeBaseName()).absoluteFilePath();

    // Start the cut the same way test_h26xcut_abort's runCut() does. Read that
    // function and use the identical entry point and cut-list construction -
    // do not invent a second way.
    TTSettings::instance()->setCutDirPath(roDir);

    TTCutList cutList;
    // Two cuts, derived from the stream length (see the usage note above).
    // On 6000 frames this is exactly 500..1499 and 3000..3999.
    const int frameCount = vStream->frameCount();
    QList<QPair<int,int>> cuts;
    for (int i = 4; i + 1 < argc; i += 2)
        cuts.append(qMakePair(QString(argv[i]).toInt(), QString(argv[i+1]).toInt()));
    if (cuts.isEmpty())
        cuts << qMakePair(frameCount / 12, frameCount / 4 - 1)
             << qMakePair(frameCount / 2,  (frameCount * 2) / 3 - 1);
    for (const auto& c : cuts) {
        if (c.first < 0 || c.second >= frameCount || c.first >= c.second) {
            printf("FAIL: cut bounds %d..%d do not fit a %d-frame stream\n",
                   c.first, c.second, frameCount);
            return 1;
        }
        cutList.append(avItem, c.first, c.second);
    }
    printf("source: %d frames at %.3f fps, %d cut(s)\n",
           frameCount, vStream->frameRate(), (int)cuts.count());

    // Quit once a terminal bracket (Exit or Canceled) has arrived. Give
    // trailing signals a couple of seconds before quitting, same rationale as
    // test_h26xcut_abort's scheduleQuit(): quitting immediately would risk
    // missing the very assignment this harness is trying to catch.
    bool terminalSeen = false;
    QObject::connect(&avData,
        qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
        [&](TTThreadTask*, int state, const QString&, quint64) {
            if (state != StatusReportArgs::Exit && state != StatusReportArgs::Canceled) return;
            if (terminalSeen) return;
            terminalSeen = true;
            QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
        });

    // Emergency brake only - a run that needs this has already failed.
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
    watchdog.start(900000);

    QElapsedTimer timer;
    timer.start();
    avData.onDoCut(target, &cutList, false);
    qApp->exec();
    printf("  cut run took %lld ms\n", (long long)timer.elapsed());

    check(sawExit, "a failing cut still reports an Exit bracket");
    check(!errorAtExitTime.isEmpty(),
          "lastCutError() is already filled when Exit arrives");

    // Not just filled, but filled with the RIGHT text. TTH26xCutTask keeps
    // two separate wordings (data/tth26xcuttask.h:64-68): the short bracket
    // text ("Cutting failed", shown in the progress window) and the longer
    // error-dialog text ("Cutting failed: Cannot create output file: ...",
    // shown in TTCutMainWindow's "Cutting Failed" message box). Before this
    // check was added, mLastCutError held the SHORT text - identical to the
    // bracket text, missing the actual reason. It must instead differ from
    // the bracket text and carry the read-only-directory path that made this
    // run fail, which only the long text has.
    check(errorAtExitTime != bracketTextAtExitTime,
          "lastCutError() differs from the closing bracket's short text");
    // Checked by the failing PATH, not by one engine's wording: the H.26x
    // path fails inside TTESSmartCut ("Cannot create output file: <path>"),
    // the MPEG-2 path inside TTFileBuffer::directWrite ("Could not write N
    // bytes to <path>: Permission denied"). Both must name the directory this
    // harness made read-only - that is what "the detailed reason" means here,
    // and pinning the check to one engine's phrasing only ever tested the
    // engine the harness happened to be run with.
    check(errorAtExitTime.contains(roDir),
          "lastCutError() carries the detailed reason (with the failing path), not just the short bracket text");

    printf("%s\n", gFailures == 0 ? "ALL PASS" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
