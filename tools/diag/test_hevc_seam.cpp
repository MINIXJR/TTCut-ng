// Diagnostic harness for extern/tthevcseam.cpp.
// Usage:
//   test_hevc_seam sps <file.265>            - parse & print first SPS
//   test_hevc_seam pps <file.265>            - parse & print all unique PPS
//   test_hevc_seam ppsid <file.265> <newid>  - patch first PPS id, re-parse
//   test_hevc_seam roundtrip <cut.265>       - slice parse+rebuild identity
//                                              for all pre-seam encoder slices
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <QByteArray>
#include <QFile>
#include <QPair>
#include <QVector>

#include "../../extern/tthevcseam.h"

// Minimal annex-b NAL scan (start offset incl. start code, end exclusive).
struct Nal { qint64 sc; qint64 pl; qint64 end; int type; };

static QVector<Nal> scanNals(const QByteArray& buf)
{
    QVector<Nal> nals;
    QVector<QPair<qint64, qint64>> starts;   // (sc, payload)
    for (qint64 i = 0; i + 3 < buf.size(); ) {
        if (buf.at(i) == 0 && buf.at(i + 1) == 0) {
            if (buf.at(i + 2) == 1) { starts.append({i, i + 3}); i += 3; continue; }
            if (buf.at(i + 2) == 0 && i + 3 < buf.size() && buf.at(i + 3) == 1) {
                starts.append({i, i + 4}); i += 4; continue;
            }
        }
        ++i;
    }
    for (int k = 0; k < starts.size(); ++k) {
        Nal n;
        n.sc = starts[k].first;
        n.pl = starts[k].second;
        n.end = (k + 1 < starts.size()) ? starts[k + 1].first : buf.size();
        n.type = (quint8(buf.at(n.pl)) >> 1) & 0x3F;
        nals.append(n);
    }
    return nals;
}

static void printSps(const THevcSpsSeamInfo& s)
{
    printf("valid=%d reason='%s'\n", s.valid, qPrintable(s.invalidReason));
    printf("spsId=%d subLayersMinus1=%d chroma=%d %dx%d bitDepth=%d/%d\n",
           s.spsId, s.maxSubLayersMinus1, s.chromaFormatIdc,
           s.picWidth, s.picHeight, s.bitDepthLuma, s.bitDepthChroma);
    printf("log2MaxPocLsb=%d dpbMinus1=%d\n",
           s.log2MaxPocLsb, s.maxDecPicBufferingMinus1);
    printf("cb=%d+%d tb=%d+%d tuDepth=%d/%d\n",
           s.log2MinCbSizeMinus3, s.log2DiffMaxMinCbSize,
           s.log2MinTbSizeMinus2, s.log2DiffMaxMinTbSize,
           s.tuDepthInter, s.tuDepthIntra);
    printf("scaling=%d flat16=%d amp=%d sao=%d pcm=%d\n",
           s.scalingListEnabled, s.scalingListFlat16, s.ampEnabled,
           s.saoEnabled, s.pcmEnabled);
    printf("numStRps=%d longTerm=%d tmvp=%d strongIntra=%d\n",
           s.numShortTermRefPicSets, s.longTermRefPicsPresent,
           s.temporalMvpEnabled, s.strongIntraSmoothing);
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: see header\n"); return 2; }
    QFile f(argv[2]);
    if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "open failed\n"); return 2; }
    // 64 MB head is enough for parameter sets + first GOPs in all corpora
    QByteArray buf = f.read(64 * 1024 * 1024);
    QVector<Nal> nals = scanNals(buf);

    if (!strcmp(argv[1], "sps")) {
        for (const Nal& n : nals)
            if (n.type == 33) {
                printSps(parseHevcSpsSeamInfo(buf.mid(n.sc, n.end - n.sc)));
                return 0;
            }
        fprintf(stderr, "no SPS found\n");
        return 1;
    }
    if (!strcmp(argv[1], "pps")) {
        QVector<QByteArray> seen;
        for (const Nal& n : nals) {
            if (n.type != 34) continue;
            QByteArray raw = buf.mid(n.sc, n.end - n.sc);
            if (seen.contains(raw)) continue;
            seen.append(raw);
            THevcPpsSeamInfo p = parseHevcPpsSeamInfo(raw);
            printf("ppsId=%d valid=%d reason='%s' extraBits=%d wp=%d wbp=%d "
                   "wpp=%d deblockCtrl=%d listsMod=%d chromaQp=%d\n",
                   p.ppsId, p.valid, qPrintable(p.invalidReason),
                   p.numExtraSliceHeaderBits, p.weightedPred, p.weightedBipred,
                   p.entropyCodingSync, p.deblockingControlPresent,
                   p.listsModificationPresent, p.sliceChromaQpOffsetsPresent);
        }
        return 0;
    }
    if (!strcmp(argv[1], "ppsid")) {
        if (argc < 4) { fprintf(stderr, "need newid\n"); return 2; }
        for (const Nal& n : nals) {
            if (n.type != 34) continue;
            QByteArray patched =
                patchHevcPpsId(buf.mid(n.sc, n.end - n.sc), atoi(argv[3]));
            if (patched.isEmpty()) { printf("PATCH FAILED\n"); return 1; }
            THevcPpsSeamInfo p = parseHevcPpsSeamInfo(patched);
            printf("patched ppsId=%d valid=%d\n", p.ppsId, p.valid);
            return (p.valid && p.ppsId == atoi(argv[3])) ? 0 : 1;
        }
        fprintf(stderr, "no PPS\n");
        return 1;
    }
    if (!strcmp(argv[1], "roundtrip")) {
        THevcSpsSeamInfo encSps; THevcPpsSeamInfo encPps;
        bool haveSets = false;
        int tested = 0;
        for (const Nal& n : nals) {
            if (n.type == 33 && !haveSets)
                encSps = parseHevcSpsSeamInfo(buf.mid(n.sc, n.end - n.sc));
            if (n.type == 34 && !haveSets) {
                encPps = parseHevcPpsSeamInfo(buf.mid(n.sc, n.end - n.sc));
                haveSets = encSps.valid && encPps.valid;
            }
            if (n.type == 37) break;              // EOB = seam
            if (haveSets && tested > 0 && (n.type == 32 || n.type == 33))
                break;                            // param-set re-send = seam
            bool isSlice = (n.type <= 9) || (n.type >= 16 && n.type <= 21);
            if (!haveSets || !isSlice) continue;
            QByteArray raw = buf.mid(n.sc, n.end - n.sc);
            THevcSliceHeader h = parseHevcSliceHeader(raw, encSps, encPps);
            if (!h.ok) {
                printf("slice %d PARSE FAIL: %s\n", tested,
                       qPrintable(h.error));
                return 1;
            }
            QByteArray rebuilt = buildHevcSliceHeader(
                h, encSps, encPps, encSps.log2MaxPocLsb, h.ppsId);
            QByteArray orig = ttHevcDeescape(raw.mid(ttHevcStartCodeLen(raw)));
            if (rebuilt != orig) {
                printf("slice %d ROUNDTRIP MISMATCH (%d vs %d bytes)\n",
                       tested, rebuilt.size(), orig.size());
                return 1;
            }
            ++tested;
        }
        printf("roundtrip OK: %d slices byte-identical\n", tested);
        return tested > 0 ? 0 : 1;
    }
    fprintf(stderr, "unknown subcommand\n");
    return 2;
}
