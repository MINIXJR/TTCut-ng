// Acceptance harness for the "silence detection could not run" paths of
// TTStreamPointAudioWorker.
//
//   usage: test_silence_unavailable [file-without-audio-track]
//
// Before this, every one of the eight early exits in detectSilencePoints()
// returned an empty list in silence, and the caller then printed
// "Silence detection: 0 region(s) found" - indistinguishable from a track
// that genuinely holds no silence. This harness drives two of those exits for
// real (no such file; a file with no audio stream) and asserts that the pane
// is told why, and that the misleading "0 regions found" line is gone.
//
// Build via `cmake --build build --target test_silence_unavailable`.
#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <cstdio>

#include "common/istatusreporter.h"
#include "common/ttthreadtask.h"
#include "data/ttstreampoint_audioworker.h"
#include <algorithm>

static int gFailures = 0;

static void check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) gFailures++;
}

// Run the worker on one path and collect what it would put in the detail pane.
static QStringList runAndCollect(const QString& audioPath)
{
    QStringList lines;
    TTStreamPointAudioWorker worker(audioPath, 25.0f,
                                    true /*detectSilence*/, -30, 0.5f,
                                    false /*detectAudioChange*/, nullptr);
    QObject::connect(&worker, &TTThreadTask::statusReport,
                     [&lines](TTThreadTask*, int state, const QString& msg, quint64) {
                         if (state == StatusReportArgs::AddProcessLine)
                             lines.append(msg);
                     });
    worker.runSynchron();
    return lines;
}

static bool anyContains(const QStringList& lines, const char* needle)
{
    const QString n = QString::fromUtf8(needle);
    return std::any_of(lines.begin(), lines.end(),
                       [&](const QString& l) { return l.contains(n); });
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- Exit 1: the file does not exist at all ---
    {
        QStringList lines = runAndCollect("/nonexistent/no-such-audio.ac3");
        for (const QString& l : lines) printf("    | %s\n", qPrintable(l));

        check(anyContains(lines, "Silence detection skipped"),
              "missing file -> the pane is told the detection was skipped");
        check(anyContains(lines, "could not be opened"),
              "missing file -> the reason names the failure");
        check(!anyContains(lines, "region(s) found"),
              "missing file -> no misleading \"0 regions found\" line");
    }

    // --- Exit 3: a real file that carries no audio stream ---
    // Optional, since it needs material: pass any video-only elementary
    // stream. Skipped rather than failed when it is not there, so the harness
    // stays runnable on a bare checkout.
    if (argc > 1 && QFileInfo::exists(QString::fromUtf8(argv[1]))) {
        QStringList lines = runAndCollect(QString::fromUtf8(argv[1]));
        for (const QString& l : lines) printf("    | %s\n", qPrintable(l));

        check(anyContains(lines, "Silence detection skipped"),
              "video-only file -> the pane is told the detection was skipped");
        check(!anyContains(lines, "region(s) found"),
              "video-only file -> no misleading \"0 regions found\" line");
    } else {
        printf("SKIP: no video-only file given (argv[1]) - exit 3 not exercised\n");
    }

    printf("%s\n", gFailures == 0 ? "ALL PASS" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
