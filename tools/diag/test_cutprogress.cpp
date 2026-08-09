// test_cutprogress — count TTESSmartCut::progressChanged emissions during a
// smart cut, to locate why the open-GOP cut shows no progress and to gate
// time-weighted, encode-pass-covering progress reporting.
// Prints emission count + first/last percent. Ground truth, not reasoning.
//
// Build: cmake --build build --target test_cutprogress
// Usage: test_cutprogress <es> <frameRate> [cutInDisplay cutOutDisplay ...]
#include <QCoreApplication>
#include <QObject>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>
#include "extern/ttessmartcut.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: %s <es> <frameRate>\n", argv[0]); return 2; }
    QString es = argv[1];
    double  fr = atof(argv[2]);

    QElapsedTimer t; t.start();
    TTESSmartCut sc;
    if (!sc.initialize(es, fr)) {
        fprintf(stderr, "initialize failed: %s\n", qPrintable(sc.lastError()));
        return 1;
    }
    const int frames = sc.frameCount();
    fprintf(stderr, "initialized (parse): %lld ms  frames=%d gops=%d\n",
            (long long)t.elapsed(), frames, sc.gopCount());

    // Optional: display-order cut ranges as pairs; default whole video.
    QList<QPair<int,int>> keep;
    if (argc >= 5) {
        for (int i = 3; i + 1 < argc; i += 2)
            keep.append(qMakePair(atoi(argv[i]), atoi(argv[i+1])));
    } else {
        keep.append(qMakePair(0, frames - 1));
    }

    int  count = 0, encodeCount = 0;
    int  firstPct = -1, lastPct = -1;
    bool monotone = true;
    QObject::connect(&sc, &TTESSmartCut::progressChanged,
                     [&](int percent, const QString& msg) {
        if (firstPct < 0) firstPct = percent;
        if (percent < lastPct) monotone = false;
        lastPct = percent;
        ++count;
        if (msg.startsWith(QStringLiteral("Encoding")))
            ++encodeCount;
    });

    QString out = "/usr/local/src/CLAUDE_TMP/TTCut-ng/cutprogress_out.h264";
    qint64 tCut = t.elapsed();
    bool ok = sc.smartCutFrames(out, keep);
    qint64 cutMs = t.elapsed() - tCut;

    fprintf(stderr, "smartCutFrames %s: %lld ms  err=%s\n",
            ok ? "OK" : "FAIL", (long long)cutMs, qPrintable(sc.lastError()));

    int reenc = sc.framesReencoded();
    // Gate: >=1 Emission je 10 encodierte Frames, Prozentfolge monoton,
    // Ende bei 100.
    bool encodeGate = (reenc < 10) || (encodeCount >= reenc / 10);
    bool endGate    = (lastPct == 100);
    fprintf(stderr, "emissions=%d encodeEmissions=%d reencoded=%d copied=%d "
                    "monotone=%d first=%d last=%d\n",
            count, encodeCount, reenc, sc.framesStreamCopied(),
            monotone ? 1 : 0, firstPct, lastPct);
    if (reenc < 10)
        fprintf(stderr, "NOTE: <10 re-encoded frames - encode gate inconclusive\n");
    return (ok && encodeGate && monotone && endGate) ? 0 : 1;
}
