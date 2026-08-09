// Diagnostic gate: progress-callback contract of cutAudioStream.
// Cuts the given (start,end) ms ranges from a raw audio ES and validates the
// percent stream delivered by the callback: strictly increasing, final value
// 100, at most 101 invocations.
//
// Build: `cmake --build build --target diag`
// Usage: test_audioprogress <in-audio> <out-audio> <start1_ms> <end1_ms> [start2_ms end2_ms ...]
// Ranges are integer MILLISECONDS (same locale rationale as test_audiocut).
#include <QCoreApplication>
#include <QString>
#include <QList>
#include <cstdio>
#include <cstdlib>
#include "extern/ttffmpegwrapper.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5 || (argc % 2) != 1) {
        fprintf(stderr, "usage: %s <in-audio> <out-audio> <start1_ms> <end1_ms> [start2_ms end2_ms ...]\n", argv[0]);
        return 2;
    }

    QList<QPair<double,double>> keep;
    for (int i = 3; i + 1 < argc; i += 2)
        keep.append(qMakePair(atoll(argv[i]) / 1000.0, atoll(argv[i+1]) / 1000.0));

    QList<int> percents;
    TTFFmpegWrapper w;
    bool ok = w.cutAudioStream(argv[1], argv[2], keep, false, QList<int>(),
                               [&](int p) { percents.append(p); });
    fprintf(stderr, "cutAudioStream %s, %d callback calls\n",
            ok ? "OK" : "FAIL", (int)percents.size());
    if (!ok) return 1;

    bool strictlyIncreasing = true;
    for (int i = 1; i < percents.size(); ++i)
        if (percents[i] <= percents[i-1]) { strictlyIncreasing = false; break; }

    bool endsAt100 = !percents.isEmpty() && percents.last() == 100;
    bool bounded   = percents.size() <= 101;

    fprintf(stderr, "strictlyIncreasing=%d endsAt100=%d bounded=%d first=%d last=%d\n",
            strictlyIncreasing, endsAt100, bounded,
            percents.isEmpty() ? -1 : percents.first(),
            percents.isEmpty() ? -1 : percents.last());
    return (strictlyIncreasing && endsAt100 && bounded) ? 0 : 1;
}
