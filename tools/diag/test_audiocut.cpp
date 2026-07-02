// Diagnostic harness: drive TTFFmpegWrapper::cutAudioStream with explicit
// (start,end) second ranges on a raw audio ES and write the result, so the
// output can be cross-correlated against the source independently of the
// GUI / planAudioCut / mux stages. Forces logFFmpegDecoder for the
// per-segment skip/seek debug lines.
//
// Build: `make test_audiocut` in tools/diag (root `make` first).
// Usage: test_audiocut <in-audio> <out-audio> <start1_ms> <end1_ms> [start2_ms end2_ms ...]
// Ranges are integer MILLISECONDS — atof("1086.88") silently truncates to
// 1086 under a de_DE LC_NUMERIC locale (QCoreApplication applies the system
// locale), so no float parsing here.
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstdlib>
#include "common/ttsettings.h"
#include "extern/ttffmpegwrapper.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5 || (argc % 2) != 1) {
        fprintf(stderr, "usage: %s <in-audio> <out-audio> <start1> <end1> [start2 end2 ...]\n", argv[0]);
        return 2;
    }
    TTSettings::instance()->setLogFFmpegDecoder(true);
    TTSettings::instance()->setLogCutPipeline(true);

    QList<QPair<double,double>> keep;
    for (int i = 3; i + 1 < argc; i += 2)
        keep.append(qMakePair(atoll(argv[i]) / 1000.0, atoll(argv[i+1]) / 1000.0));

    for (const auto& s : keep)
        fprintf(stderr, "segment: %.4f .. %.4f\n", s.first, s.second);

    TTFFmpegWrapper w;
    bool ok = w.cutAudioStream(argv[1], argv[2], keep);
    fprintf(stderr, "cutAudioStream %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
