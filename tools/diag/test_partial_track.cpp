// Does a cut whose audio track fails still report success?
//
// TODO.md ("Teilfehlschläge beim Spurenschnitt"): TTAVData::cutAudioTracks()
// skips a failed track silently and reports it only through the size of the
// returned file list. Only the audio-only path compares that size against the
// requested track count (c7436a07); the MPEG-2 cut and TTH26xCutTask do not -
// a cut missing one of its audio tracks reports success, and a calibration
// factor for the remaining-time estimator is written on a wrong basis.
//
// This harness produces a REAL partial failure rather than a simulated one:
// the item carries two audio tracks, and the second one's file is deleted
// after its header list was built - TTFFmpegWrapper::cutAudioStream() then
// fails to open it, exactly as it would for a file lost mid-session.
//
// Two runs on one TTAVData (the cutsequence harness established that shape):
//   run 1  both tracks intact     -> control: must succeed, mux stage seen
//   run 2  second track sabotaged -> must FAIL: lastCutError names "1 of 2",
//                                    and the mux stage must never be reached
//
//   usage: test_partial_track <video-es> <audio-es> <workdir> [cutIn cutOut]
//
// Build via `cmake --build build --target test_partial_track`.
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
        fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir> [cutIn cutOut]\n", argv[0]);
        return 2;
    }
    const QString videoFile = QString::fromUtf8(argv[1]);
    const QString audioFile = QString::fromUtf8(argv[2]);
    const QString workDir   = QString::fromUtf8(argv[3]);
    QDir().mkpath(workDir);
    TTSettings::instance()->setCutDirPath(workDir);
    TTSettings::instance()->setTempDirPath(workDir);

    // Track 2 is a private copy so its later deletion cannot hurt the corpus.
    const QString track2File = QDir(workDir).absoluteFilePath(
        "track2." + QFileInfo(audioFile).suffix());
    QFile::remove(track2File);
    if (!QFile::copy(audioFile, track2File)) {
        fprintf(stderr, "cannot copy %s\n", qPrintable(track2File));
        return 2;
    }

    // Streams, opened the way the open tasks do (same as test_cut_outcome).
    TTVideoType    vType(videoFile);
    TTVideoStream* vStream = vType.createVideoStream();
    if (vStream == nullptr)               { fprintf(stderr, "no video stream\n"); return 2; }
    if (vStream->createHeaderList() <= 0) { fprintf(stderr, "createHeaderList\n"); return 2; }
    if (vStream->createIndexList()  <= 0) { fprintf(stderr, "createIndexList\n");  return 2; }
    if (vStream->indexList() != nullptr) vStream->indexList()->sortDisplayOrder();

    TTAVItem* avItem = new TTAVItem(vStream);
    TTSettings::instance()->setEncoderCodec(
        vStream->streamType() == TTAVTypes::h265_video ? 2 :
        vStream->streamType() == TTAVTypes::h264_video ? 1 : 0);

    for (const QString& af : { audioFile, track2File }) {
        TTAudioType    aType(af);
        TTAudioStream* aStream = aType.createAudioStream();
        if (aStream == nullptr) { fprintf(stderr, "no audio stream: %s\n", qPrintable(af)); return 2; }
        aStream->createHeaderList();
        avItem->appendAudioEntry(aStream);
    }
    printf("item carries %d audio tracks\n", avItem->audioCount());

    TTAVData avData;
    avData.setNonInteractive(true);

    // Observed per run: the error text at Exit time, and whether the mux
    // stage was ever announced.
    QString errorAtExit;
    bool    sawMuxStage  = false;
    bool    terminalSeen = false;
    QObject::connect(&avData,
        qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport),
        [&](TTThreadTask*, int state, const QString&, quint64 value) {
            if (state == StatusReportArgs::Stage
                && int(value) == StatusReportArgs::StageMux)
                sawMuxStage = true;
            if (state == StatusReportArgs::Exit || state == StatusReportArgs::Canceled) {
                if (state == StatusReportArgs::Exit)
                    errorAtExit = avData.lastCutError();
                if (!terminalSeen) {
                    terminalSeen = true;
                    QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
                }
            }
        });

    const int frameCount = vStream->frameCount();
    const int cutIn  = (argc > 4) ? QString(argv[4]).toInt() : frameCount / 6;
    const int cutOut = (argc > 5) ? QString(argv[5]).toInt() : (frameCount / 3) - 1;
    if (cutIn < 0 || cutOut >= frameCount || cutIn >= cutOut) {
        printf("FAIL: cut bounds %d..%d do not fit %d frames\n", cutIn, cutOut, frameCount);
        return 1;
    }
    printf("source: %d frames, cut %d..%d\n\n", frameCount, cutIn, cutOut);

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, qApp, &QCoreApplication::quit);
    watchdog.start(900000);

    auto runCut = [&](const char* label) {
        errorAtExit.clear();
        sawMuxStage  = false;
        terminalSeen = false;
        TTCutList cutList;
        cutList.append(avItem, cutIn, cutOut);
        const QString target = QDir(workDir).absoluteFilePath(QString(label));
        QElapsedTimer t; t.start();
        avData.onDoCut(target, &cutList, false);
        qApp->exec();
        printf("  [%s] ran %lld ms, lastCutError='%s', mux stage %s\n",
               label, (long long)t.elapsed(),
               qPrintable(errorAtExit.left(70)),
               sawMuxStage ? "seen" : "not seen");
    };

    // --- run 1: control - both tracks intact --------------------------------
    runCut("control");
    check(terminalSeen, "control run reached a terminal bracket");
    check(errorAtExit.isEmpty(), "control run reports no error");
    check(sawMuxStage, "control run reached the mux stage");

    // --- run 2: second track's file vanishes --------------------------------
    if (!QFile::remove(track2File)) {
        fprintf(stderr, "cannot remove %s\n", qPrintable(track2File));
        return 2;
    }
    printf("\ntrack 2 file deleted - cutting again\n");
    runCut("partial");
    check(terminalSeen, "partial run reached a terminal bracket");
    check(!errorAtExit.isEmpty(),
          "a cut missing an audio track reports an error");
    check(errorAtExit.contains("1") && errorAtExit.contains("2"),
          "the error names the count (1 of 2)");
    check(!sawMuxStage,
          "the mux stage is never reached when a track is missing");

    printf("\n%s\n", gFailures == 0 ? "ALL PASS" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
