// Does the per-track delay follow the mkvmerge convention (positive = track
// plays LATER) on both delay consumers of the cut pipeline?
//
// TODO.md ("Untertitel-Delay editierbar machen"): the subtitle delay is new,
// and the audio delay's sign was flipped to the mkvmerge convention in the
// same change (it was never documented and happened to be inverse). This
// harness pins the convention at runtime for both:
//
//   1. TTAVData::cutSubtitleTracks() — a subtitle at source 10.0s cut with
//      keep window 9.5..12.5 must land at output 0.5s for delay 0, at 1.0s
//      for +500 ms (shown later), and at 0.0s (clamped) for -500 ms.
//   2. TTAVData::planAudioCut() — the audio source window for a keep segment
//      starting at 10.0s must start ~10.0s for delay 0, ~9.5s for +500 ms
//      (audio plays later = window shifts earlier), ~10.5s for -500 ms.
//
//   usage: test_subtitle_delay <audio-es> <workdir>
//
// Build via `cmake --build build --target test_subtitle_delay`.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

#include <cmath>
#include <cstdio>

#include "avstream/ttavheader.h"
#include "avstream/ttavstream.h"
#include "avstream/ttavtypes.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

// First "HH:MM:SS,mmm --> HH:MM:SS,mmm" line of the cut SRT, as start/end ms.
static bool firstEntryTimes(const QString& path, int& startMs, int& endMs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    static const QRegularExpression re(
        "^(\\d{2}):(\\d{2}):(\\d{2}),(\\d{3}) --> (\\d{2}):(\\d{2}):(\\d{2}),(\\d{3})");
    while (!in.atEnd()) {
        QRegularExpressionMatch m = re.match(in.readLine());
        if (!m.hasMatch()) continue;
        startMs = ((m.captured(1).toInt() * 60 + m.captured(2).toInt()) * 60
                   + m.captured(3).toInt()) * 1000 + m.captured(4).toInt();
        endMs   = ((m.captured(5).toInt() * 60 + m.captured(6).toInt()) * 60
                   + m.captured(7).toInt()) * 1000 + m.captured(8).toInt();
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    if (argc < 3) {
        fprintf(stderr, "usage: %s <audio-es> <workdir>\n", argv[0]);
        return 2;
    }
    const QString audioFile = QString::fromUtf8(argv[1]);
    const QString workDir   = QString::fromUtf8(argv[2]);
    QDir().mkpath(workDir);

    // --- Source SRT: one entry inside the keep window, one far outside ---
    const QString srcSrt = QDir(workDir).absoluteFilePath("delay_src.srt");
    {
        QFile f(srcSrt);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fprintf(stderr, "cannot write %s\n", qPrintable(srcSrt));
            return 2;
        }
        f.write("1\r\n00:00:10,000 --> 00:00:11,000\r\nInside\r\n\r\n"
                "2\r\n00:00:30,000 --> 00:00:31,000\r\nOutside\r\n\r\n");
    }

    TTSubtitleType    sType(srcSrt);
    TTSubtitleStream* sStream = sType.createSubtitleStream();
    if (sStream == nullptr) { fprintf(stderr, "no subtitle stream\n"); return 2; }
    sStream->createHeaderList();

    // cutSubtitleTracks never touches the video stream, so the item can go
    // without one here (the GUI always has one).
    TTAVItem* avItem = new TTAVItem(nullptr);
    avItem->appendSubtitleEntry(sStream);

    TTAVData avData;
    const QList<QPair<double, double>> keepList = { qMakePair(9.5, 12.5) };

    struct Case { int delayMs; int expectStartMs; int expectEndMs; };
    // delay 0:    window 9.5..12.5, entry 10.0..11.0     -> 500..1500
    // delay +500: window 9.0..12.0, entry shown later    -> 1000..2000
    // delay -500: window 10.0..13.0, entry start clamped -> 0..1000
    const QList<Case> srtCases = { {0, 500, 1500}, {500, 1000, 2000}, {-500, 0, 1000} };

    for (const Case& c : srtCases) {
        avItem->onSubtitleDelayChanged(0, c.delayMs);
        const QString outSrt = QDir(workDir).absoluteFilePath(
            QString("delay_%1.srt").arg(c.delayMs));
        QFile::remove(outSrt);

        bool cutOk = false;
        avData.cutSubtitleTracks(avItem, {0}, keepList,
            [&](int) { return outSrt; },
            [&](int, const QString&, const QString&, bool ok) { cutOk = ok; });

        int startMs = -1, endMs = -1;
        bool parsed = cutOk && firstEntryTimes(outSrt, startMs, endMs);
        check(parsed && startMs == c.expectStartMs && endMs == c.expectEndMs,
              QString("SRT delay %1 ms -> entry %2..%3 ms (expected %4..%5)")
                  .arg(c.delayMs).arg(startMs).arg(endMs)
                  .arg(c.expectStartMs).arg(c.expectEndMs));
    }

    // --- planAudioCut: source window shifts EARLIER for positive delay ---
    TTAudioType    aType(audioFile);
    TTAudioStream* aStream = aType.createAudioStream();
    if (aStream == nullptr) { fprintf(stderr, "no audio stream\n"); return 2; }
    aStream->createHeaderList();

    const QList<QPair<double, double>> videoKeep = { qMakePair(10.0, 20.0) };
    const double frameSec = aStream->headerAt(0)->frame_time / 1000.0;

    struct ACase { int delayMs; double expectStartSec; };
    const QList<ACase> audioCases = { {0, 10.0}, {500, 9.5}, {-500, 10.5} };
    for (const ACase& c : audioCases) {
        TTAVData::AudioCutPlan plan = avData.planAudioCut(aStream, videoKeep, c.delayMs);
        bool ok = !plan.keepList.isEmpty()
               && std::fabs(plan.keepList[0].first - c.expectStartSec) <= frameSec;
        check(ok, QString("audio delay %1 ms -> window start %2 s (expected %3 +/- %4)")
                      .arg(c.delayMs)
                      .arg(plan.keepList.isEmpty() ? -1.0 : plan.keepList[0].first)
                      .arg(c.expectStartSec).arg(frameSec));
    }

    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
