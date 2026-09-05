// Output gate for TTAudioCutter::cut across a refactor: drives every path of
// the packet loop on one AC3 file and prints what a byte-for-byte compare of
// two builds needs. Established for the cut() split (code audit 2026-09-04,
// batch E) - the existing harnesses cover only the plain stream copy.
//
// Usage: test_audiocutter_paths <in.ac3> <workdir>
//   A  plain stream copy of three keep ranges
//   B1 acmod normalisation towards stereo (targetAcmods all 2): on a
//      stereo/5.1/stereo source the 5.1 frames of the middle range are
//      re-encoded 5.1 -> stereo
//   B2 acmod normalisation towards 5.1 (all 7): stereo -> 5.1 in the
//      first and last range and the stereo tail of the middle one
//      (Mixed targets across ranges - {2, 2, 7} - crash inside swr_convert
//      in the pre-audit code: the resampler and the encoder are set up once
//      from the first re-encoded frame and never re-created when a later
//      range's input layout or target differs. Found 2026-09-05 while
//      establishing this gate; not part of the audit's refactor, see TODO.md.)
//   C  repair table: frames 30..34 replaced by the bytes of frame 40
//   D  abort as soon as progress passes 30 %
// Per run: result, lastError, output size, MD5, progress calls (count,
// first, last). The audio-only fixture of gate_ac3fix.sh (mixed.ac3:
// stereo448 + s51 + stereo448 + 4 junk bytes) exercises every branch.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <cstdio>
extern "C" {
#include <libavformat/avformat.h>
}
#include "extern/ttaudiocutter.h"

static QString md5(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return "-";
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

// Bytes of packet number n (0-based) of the first audio stream.
static QByteArray packetBytes(const QString& path, int n)
{
    QByteArray out;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return out;
    AVPacket* pkt = av_packet_alloc();
    int idx = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (idx++ == n) { out = QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size); av_packet_unref(pkt); break; }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return out;
}

struct Run {
    const char* tag;
    bool normalize = false;
    QList<int> targets = {};
    const TTAudioRepair::FrameTable* repair = nullptr;
    int abortAbove = -1;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: %s <in.ac3> <workdir>\n", argv[0]); return 2; }
    const QString in = argv[1];
    const QString work = argv[2];
    QDir().mkpath(work);

    const QList<QPair<double,double>> keep{{0.5, 5.0}, {9.0, 13.0}, {14.5, 19.0}};

    TTAudioRepair::FrameTable table;
    const QByteArray donor = packetBytes(in, 40);
    for (qint64 f = 30; f <= 34; ++f) table.insert(f, donor);
    printf("repair donor bytes=%d\n", int(donor.size()));

    const Run runs[] = {
        {"A-copy"},
        {"B1-acmod-to-stereo", true, {2, 2, 2}},
        {"B2-acmod-to-51", true, {7, 7, 7}},
        {"C-repair", false, {}, &table},
        {"D-abort", false, {}, nullptr, 30},
    };
    for (const Run& r : runs) {
        const QString out = work + "/" + r.tag + ".ac3";
        QFile::remove(out);
        QList<int> progress;
        int lastSeen = -1;
        TTAudioCutter cutter;
        const bool ok = cutter.cut(in, out, keep, r.normalize, r.targets,
            [&](int p) { progress.append(p); lastSeen = p; },
            [&]() { return r.abortAbove >= 0 && lastSeen > r.abortAbove; },
            r.repair);
        printf("%s: ok=%d error=\"%s\" size=%lld md5=%s progress=%d first=%d last=%d\n",
               r.tag, ok ? 1 : 0, qPrintable(cutter.lastError()),
               (long long)QFile(out).size(), qPrintable(md5(out)),
               int(progress.size()), progress.isEmpty() ? -1 : progress.first(),
               progress.isEmpty() ? -1 : progress.last());
    }
    return 0;
}
