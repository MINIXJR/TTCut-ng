/*----------------------------------------------------------------------------*/
/* HEVC seam machinery — see tthevcseam.h. Port of the validated PoC          */
/* (CLAUDE_TMP/TTCut-ng/hevc_rasl/poc/hevc_seam_poc.py, 2026-07-21).          */
/*----------------------------------------------------------------------------*/

#include "tthevcseam.h"

#include <QtGlobal>

#include <algorithm>

// ---------------------------------------------------------------- bit reader
namespace {

class THevcBitReader
{
public:
    explicit THevcBitReader(const QByteArray& data)
        : mData(reinterpret_cast<const quint8*>(data.constData()))
        , mSizeBits(data.size() * 8), mPos(0), mError(false) {}

    bool error() const { return mError; }
    int  pos() const { return mPos; }
    int  sizeBits() const { return mSizeBits; }

    int bit()
    {
        if (mPos >= mSizeBits) { mError = true; return 0; }
        int b = (mData[mPos >> 3] >> (7 - (mPos & 7))) & 1;
        ++mPos;
        return b;
    }
    quint32 bits(int n)
    {
        quint32 v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | bit();
        return v;
    }
    quint32 ue()
    {
        int zeros = 0;
        while (!mError && bit() == 0) {
            if (++zeros > 31) { mError = true; return 0; }
        }
        quint32 suffix = (zeros > 0) ? bits(zeros) : 0;
        return ((1u << zeros) - 1) + suffix;
    }
    qint32 se()
    {
        quint32 k = ue();
        return (k & 1) ? static_cast<qint32>((k + 1) / 2)
                       : -static_cast<qint32>(k / 2);
    }
    void skip(int n) { mPos += n; if (mPos > mSizeBits) mError = true; }

private:
    const quint8* mData;
    int  mSizeBits;
    int  mPos;
    bool mError;
};

class THevcBitWriter
{
public:
    void bit(int b)
    {
        if ((mNumBits & 7) == 0) mBytes.append(char(0));
        if (b) mBytes[mBytes.size() - 1] =
            char(quint8(mBytes.at(mBytes.size() - 1)) | (1 << (7 - (mNumBits & 7))));
        ++mNumBits;
    }
    void bits(quint32 v, int n)
    {
        for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1);
    }
    void ue(quint32 v)
    {
        quint32 vp1 = v + 1;
        int nb = 0;
        for (quint32 t = vp1; t; t >>= 1) ++nb;
        bits(0, nb - 1);
        bits(vp1, nb);
    }
    void se(qint32 v)
    {
        ue(v > 0 ? quint32(2 * v - 1) : quint32(-2 * v));
    }
    void alignOneZeros()          // rbsp stop bit + zero padding
    {
        bit(1);
        while (mNumBits & 7) bit(0);
    }
    int numBits() const { return mNumBits; }
    QByteArray data() const { return mBytes; }

private:
    QByteArray mBytes;
    int mNumBits = 0;
};

} // namespace

// -------------------------------------------------------------- EPB handling
QByteArray ttHevcDeescape(const QByteArray& nalData)
{
    QByteArray out;
    out.reserve(nalData.size());
    int zeros = 0;
    for (int i = 0; i < nalData.size(); ++i) {
        quint8 b = quint8(nalData.at(i));
        if (zeros >= 2 && b == 3) { zeros = 0; continue; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

QByteArray ttHevcEscape(const QByteArray& rbsp)
{
    QByteArray out;
    out.reserve(rbsp.size() + 8);
    int zeros = 0;
    for (int i = 0; i < rbsp.size(); ++i) {
        quint8 b = quint8(rbsp.at(i));
        if (zeros >= 2 && b <= 3) { out.append(char(3)); zeros = 0; }
        out.append(char(b));
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

int ttHevcStartCodeLen(const QByteArray& nal)
{
    if (nal.size() >= 4 && nal.at(0) == 0 && nal.at(1) == 0
        && nal.at(2) == 0 && nal.at(3) == 1) return 4;
    if (nal.size() >= 3 && nal.at(0) == 0 && nal.at(1) == 0
        && nal.at(2) == 1) return 3;
    return 0;
}

// ------------------------------------------------------------------ SPS parse
// Scaling list data walk with flat-16 tracking (H.265 7.3.4).
// A list is "flat 16" when every coefficient (and the DC coef for
// sizeId >= 2) decodes to 16 — numerically identical to scaling disabled.
// pred_matrix_id_delta references copy earlier lists, inheriting flatness;
// delta == 0 references the DEFAULT list, which is NOT flat -> not flat16.
static void parseScalingListData(THevcBitReader& r, bool* allFlat16)
{
    *allFlat16 = true;
    for (int sizeId = 0; sizeId < 4; ++sizeId) {
        for (int matrixId = 0; matrixId < 6;
             matrixId += (sizeId == 3) ? 3 : 1) {
            int predMode = r.bit();
            if (!predMode) {
                quint32 delta = r.ue();
                if (delta == 0)          // copies DEFAULT list (non-flat)
                    *allFlat16 = false;
                // delta > 0 copies an earlier parsed list: flatness inherited,
                // tracked implicitly via *allFlat16 over all explicit lists.
            } else {
                int coefNum = qMin(64, 1 << (4 + (sizeId << 1)));
                int nextCoef = 8;
                if (sizeId > 1) {
                    qint32 dcMinus8 = r.se();
                    if (dcMinus8 != 8) *allFlat16 = false;   // DC must be 16
                    nextCoef = dcMinus8 + 8;
                }
                for (int i = 0; i < coefNum; ++i) {
                    qint32 d = r.se();
                    nextCoef = (nextCoef + d + 256) % 256;
                    if (nextCoef != 16) *allFlat16 = false;
                }
            }
        }
    }
}

THevcSpsSeamInfo parseHevcSpsSeamInfo(const QByteArray& spsNal)
{
    THevcSpsSeamInfo info;
    int sc = ttHevcStartCodeLen(spsNal);
    QByteArray rbsp = ttHevcDeescape(spsNal.mid(sc));
    THevcBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 33) {
        info.invalidReason = QStringLiteral("not an SPS NAL");
        return info;
    }
    r.bits(4);                                   // sps_video_parameter_set_id
    info.maxSubLayersMinus1 = int(r.bits(3));
    r.bit();                                     // temporal_id_nesting
    if (info.maxSubLayersMinus1 != 0) {
        info.invalidReason = QStringLiteral("sub-layers unsupported");
        return info;
    }
    // profile_tier_level(1, 0): 2+1+5+32+4x1+43+1 = 88 bits general + 8 level
    r.skip(88 + 8);

    info.spsId = int(r.ue());
    info.chromaFormatIdc = int(r.ue());
    if (info.chromaFormatIdc == 3) r.bit();      // separate_colour_plane
    info.picWidth  = int(r.ue());
    info.picHeight = int(r.ue());
    if (r.bit()) {                               // conformance_window
        r.ue(); r.ue(); r.ue(); r.ue();
    }
    info.bitDepthLuma   = int(r.ue()) + 8;
    info.bitDepthChroma = int(r.ue()) + 8;
    info.log2MaxPocLsb  = int(r.ue()) + 4;
    int subLayerOrdering = r.bit();
    // maxSubLayersMinus1 == 0: exactly one dpb/reorder/latency triple either way
    Q_UNUSED(subLayerOrdering);
    info.maxDecPicBufferingMinus1 = int(r.ue());
    r.ue();                                      // max_num_reorder_pics
    r.ue();                                      // max_latency_increase_plus1
    info.log2MinCbSizeMinus3  = int(r.ue());
    info.log2DiffMaxMinCbSize = int(r.ue());
    info.log2MinTbSizeMinus2  = int(r.ue());
    info.log2DiffMaxMinTbSize = int(r.ue());
    info.tuDepthInter = int(r.ue());
    info.tuDepthIntra = int(r.ue());
    info.scalingListEnabled = r.bit();
    if (info.scalingListEnabled) {
        info.scalingListDataPresent = r.bit();
        if (info.scalingListDataPresent) {
            bool flat = true;
            parseScalingListData(r, &flat);
            info.scalingListFlat16 = flat;
        } else {
            // Default lists active — NOT flat.
            info.scalingListFlat16 = false;
        }
    }
    info.ampEnabled = r.bit();
    info.saoEnabled = r.bit();
    info.pcmEnabled = r.bit();
    if (info.pcmEnabled) {
        info.invalidReason = QStringLiteral("PCM unsupported");
        return info;
    }
    info.numShortTermRefPicSets = int(r.ue());
    if (info.numShortTermRefPicSets != 0) {
        // st_ref_pic_set parsing in the SPS is not implemented; preflight
        // requires 0 anyway (all measured corpora).
        info.invalidReason = QStringLiteral("SPS RPS sets unsupported");
        return info;
    }
    info.longTermRefPicsPresent = r.bit();
    if (info.longTermRefPicsPresent) {
        info.invalidReason = QStringLiteral("long-term ref pics unsupported");
        return info;
    }
    info.temporalMvpEnabled = r.bit();
    info.strongIntraSmoothing = r.bit();
    // VUI and extensions are irrelevant for the seam — stop here.

    if (r.error()) {
        info.invalidReason = QStringLiteral("bitstream overrun");
        return info;
    }
    info.valid = true;
    return info;
}

// ------------------------------------------------------------------ PPS parse
THevcPpsSeamInfo parseHevcPpsSeamInfo(const QByteArray& ppsNal)
{
    THevcPpsSeamInfo info;
    int sc = ttHevcStartCodeLen(ppsNal);
    QByteArray rbsp = ttHevcDeescape(ppsNal.mid(sc));
    THevcBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 34) {
        info.invalidReason = QStringLiteral("not a PPS NAL");
        return info;
    }
    info.ppsId = int(r.ue());
    info.spsId = int(r.ue());
    info.dependentSliceSegments = r.bit();
    info.outputFlagPresent = r.bit();
    info.numExtraSliceHeaderBits = int(r.bits(3));
    info.signDataHiding = r.bit();
    info.cabacInitPresent = r.bit();
    info.numRefIdxL0DefaultMinus1 = int(r.ue());
    info.numRefIdxL1DefaultMinus1 = int(r.ue());
    r.se();                                      // init_qp_minus26
    r.bit();                                     // constrained_intra_pred
    r.bit();                                     // transform_skip_enabled
    if (r.bit())                                 // cu_qp_delta_enabled
        r.ue();                                  // diff_cu_qp_delta_depth
    r.se();                                      // pps_cb_qp_offset
    r.se();                                      // pps_cr_qp_offset
    info.sliceChromaQpOffsetsPresent = r.bit();
    info.weightedPred = r.bit();
    info.weightedBipred = r.bit();
    r.bit();                                     // transquant_bypass_enabled
    info.tilesEnabled = r.bit();
    info.entropyCodingSync = r.bit();
    if (info.tilesEnabled) {
        info.invalidReason = QStringLiteral("tiles unsupported");
        return info;
    }
    info.ppsLoopFilterAcrossSlices = r.bit();
    info.deblockingControlPresent = r.bit();
    if (info.deblockingControlPresent) {
        r.bit();                                 // deblocking_filter_override_enabled
        if (r.bit() == 0) {                      // pps_deblocking_filter_disabled
            r.se();                              // pps_beta_offset_div2
            r.se();                              // pps_tc_offset_div2
        }
    }
    if (r.bit()) {                               // pps_scaling_list_data_present
        bool flatIgnored = true;
        parseScalingListData(r, &flatIgnored);   // walk to stay in sync
    }
    info.listsModificationPresent = r.bit();
    r.ue();                                      // log2_parallel_merge_level_minus2
    info.sliceHeaderExtension = r.bit();
    // pps_extension_present + trailing: not needed.

    if (r.error()) {
        info.invalidReason = QStringLiteral("bitstream overrun");
        return info;
    }
    info.valid = true;
    return info;
}

// -------------------------------------------------------------- pps_id patch
QByteArray patchHevcPpsId(const QByteArray& ppsNalWithStartCode, int newPpsId)
{
    int sc = ttHevcStartCodeLen(ppsNalWithStartCode);
    if (sc == 0 || newPpsId < 1 || newPpsId > 63)
        return QByteArray();
    QByteArray rbsp = ttHevcDeescape(ppsNalWithStartCode.mid(sc));
    THevcBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 34) return QByteArray();
    if (r.bit() != 1) return QByteArray();       // pps_id must be ue(0) = '1'

    // Locate the rbsp stop bit (last set bit) so trailing alignment can be
    // rebuilt after the shift.
    int totalBits = rbsp.size() * 8;
    int last1 = totalBits - 1;
    const quint8* d = reinterpret_cast<const quint8*>(rbsp.constData());
    while (last1 > r.pos() && ((d[last1 >> 3] >> (7 - (last1 & 7))) & 1) == 0)
        --last1;
    if (last1 <= r.pos()) return QByteArray();

    THevcBitWriter w;
    w.bits(hdr, 16);
    w.ue(quint32(newPpsId));
    for (int p = r.pos(); p < last1; ++p)
        w.bit((d[p >> 3] >> (7 - (p & 7))) & 1);
    w.alignOneZeros();                           // stop bit + padding

    return ppsNalWithStartCode.left(sc) + ttHevcEscape(w.data());
}

// --------------------------------------------------------------- slice header
bool isIrapNal(int t)  { return t >= 16 && t <= 23; }
bool isIdrNal(int t)   { return t == 19 || t == 20; }

THevcSliceHeader parseHevcSliceHeader(const QByteArray& nalWithSc,
                                      const THevcSpsSeamInfo& sps,
                                      const THevcPpsSeamInfo& pps)
{
    THevcSliceHeader h;
    int sc = ttHevcStartCodeLen(nalWithSc);
    QByteArray rbsp = ttHevcDeescape(nalWithSc.mid(sc));
    THevcBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    h.nalType = int((hdr >> 9) & 0x3F);
    h.nuhRest = hdr & 0x1FF;

    auto fail = [&h](const char* why) {
        h.ok = false; h.error = QString::fromLatin1(why); return h;
    };

    if (r.bit() != 1) return fail("first_slice_segment_in_pic_flag != 1");
    if (isIrapNal(h.nalType)) h.noOutputPrior = r.bit();
    h.ppsId = int(r.ue());
    if (h.ppsId != 0) return fail("encoder pps_id != 0");
    for (int i = 0; i < pps.numExtraSliceHeaderBits; ++i) r.bit();
    h.sliceType = int(r.ue());
    if (h.sliceType == 0) return fail("B slice in encoder output");
    if (h.sliceType > 2) return fail("bad slice_type");

    if (!isIdrNal(h.nalType)) {
        h.pocLsb = int(r.bits(sps.log2MaxPocLsb));
        if (r.bit() != 0) return fail("st_rps_sps_flag != 0");
        quint32 nneg = r.ue(), npos = r.ue();
        if (nneg > 16 || npos > 16) return fail("RPS too large");
        for (quint32 i = 0; i < nneg; ++i) {
            THevcRpsEntry e; e.deltaPoc = int(r.ue()) + 1; e.used = r.bit();
            h.rpsNeg.append(e);
        }
        for (quint32 i = 0; i < npos; ++i) {
            THevcRpsEntry e; e.deltaPoc = int(r.ue()) + 1; e.used = r.bit();
            h.rpsPos.append(e);
        }
        if (sps.temporalMvpEnabled) h.tmvp = r.bit();
    }
    if (sps.saoEnabled) { h.saoLuma = r.bit(); h.saoChroma = r.bit(); }

    if (h.sliceType == 1) {                       // P
        h.numRefIdxOverride = r.bit();
        int nActive = pps.numRefIdxL0DefaultMinus1 + 1;
        if (h.numRefIdxOverride) {
            h.l0ActiveMinus1 = int(r.ue());
            nActive = h.l0ActiveMinus1 + 1;
        }
        if (pps.listsModificationPresent)
            return fail("lists_modification unsupported");
        if (h.tmvp && nActive > 1) {
            h.hasCollocatedRefIdx = true;
            h.collocatedRefIdx = r.ue();
        }
        if (pps.weightedPred) {
            h.lumaLog2Denom = r.ue();
            if (sps.chromaFormatIdc != 0) h.deltaChromaDenom = r.se();
            for (int i = 0; i < nActive; ++i)
                h.lumaWeightFlag.append(r.bit());
            for (int i = 0; i < nActive; ++i) {
                if (h.lumaWeightFlag.at(i)) {
                    qint32 wgt = r.se(), off = r.se();
                    h.lumaWeights.append({wgt, off});
                }
            }
            if (sps.chromaFormatIdc != 0) {
                for (int i = 0; i < nActive; ++i)
                    h.chromaWeightFlag.append(r.bit());
                for (int i = 0; i < nActive; ++i) {
                    if (h.chromaWeightFlag.at(i)) {
                        QVector<qint32> v;
                        for (int k = 0; k < 4; ++k) v.append(r.se());
                        h.chromaWeights.append(v);
                    }
                }
            }
        }
        h.fiveMinusMaxMergeCand = r.ue();
    }
    h.qpDelta = r.se();
    if (pps.sliceChromaQpOffsetsPresent) { r.se(); r.se(); }
    if (pps.deblockingControlPresent)
        return fail("deblocking control in encoder PPS unsupported");
    // loop_filter_across present iff pps flag && (sao used || deblocking on).
    // x265: deblocking on (not disabled), so present iff pps flag set.
    if (pps.ppsLoopFilterAcrossSlices) {
        h.hasLoopFilterAcross = true;
        h.loopFilterAcross = r.bit();
    }
    if (pps.tilesEnabled || pps.entropyCodingSync) {
        h.numEntryPoints = r.ue();
        if (h.numEntryPoints > 0) {
            h.offsetLenMinus1 = r.ue();
            for (quint32 i = 0; i < h.numEntryPoints; ++i)
                h.entryPointOffsets.append(r.bits(int(h.offsetLenMinus1) + 1));
        }
    }
    if (pps.sliceHeaderExtension)
        return fail("slice header extension unsupported");

    if (r.bit() != 1) return fail("alignment stop bit missing");
    while (r.pos() & 7) {
        if (r.bit() != 0) return fail("alignment zero bit not zero");
    }
    if (r.error()) return fail("bitstream overrun");
    h.sliceData = rbsp.mid(r.pos() / 8);
    h.ok = true;
    return h;
}

QByteArray buildHevcSliceHeader(const THevcSliceHeader& h,
                                const THevcSpsSeamInfo& sps,
                                const THevcPpsSeamInfo& pps,
                                int writePocBits, int writePpsId)
{
    THevcBitWriter w;
    w.bit(0);                                    // forbidden_zero
    w.bits(quint32(h.nalType), 6);
    w.bits(h.nuhRest, 9);
    w.bit(1);                                    // first_slice
    if (isIrapNal(h.nalType)) w.bit(h.noOutputPrior);
    w.ue(quint32(writePpsId));
    // numExtraSliceHeaderBits is 0 for x265 PPS (asserted in the preflight)
    w.ue(quint32(h.sliceType));
    if (!isIdrNal(h.nalType)) {
        w.bits(quint32(h.pocLsb), writePocBits);
        w.bit(0);                                // st_rps_sps_flag
        w.ue(quint32(h.rpsNeg.size()));
        w.ue(quint32(h.rpsPos.size()));
        for (const THevcRpsEntry& e : h.rpsNeg) {
            w.ue(quint32(e.deltaPoc - 1)); w.bit(e.used);
        }
        for (const THevcRpsEntry& e : h.rpsPos) {
            w.ue(quint32(e.deltaPoc - 1)); w.bit(e.used);
        }
        if (sps.temporalMvpEnabled) w.bit(h.tmvp);
    }
    if (sps.saoEnabled) { w.bit(h.saoLuma); w.bit(h.saoChroma); }
    if (h.sliceType == 1) {
        w.bit(h.numRefIdxOverride);
        if (h.numRefIdxOverride) w.ue(quint32(h.l0ActiveMinus1));
        if (h.hasCollocatedRefIdx) w.ue(h.collocatedRefIdx);
        if (pps.weightedPred) {
            w.ue(h.lumaLog2Denom);
            if (sps.chromaFormatIdc != 0) w.se(h.deltaChromaDenom);
            for (int f : h.lumaWeightFlag) w.bit(f);
            for (const auto& lw : h.lumaWeights) { w.se(lw.first); w.se(lw.second); }
            if (sps.chromaFormatIdc != 0) {
                for (int f : h.chromaWeightFlag) w.bit(f);
                for (const auto& cw : h.chromaWeights)
                    for (qint32 v : cw) w.se(v);
            }
        }
        w.ue(h.fiveMinusMaxMergeCand);
    }
    w.se(h.qpDelta);
    if (h.hasLoopFilterAcross) w.bit(h.loopFilterAcross);
    if (pps.tilesEnabled || pps.entropyCodingSync) {
        w.ue(h.numEntryPoints);
        if (h.numEntryPoints > 0) {
            w.ue(h.offsetLenMinus1);
            for (quint32 off : h.entryPointOffsets)
                w.bits(off, int(h.offsetLenMinus1) + 1);
        }
    }
    w.alignOneZeros();
    return w.data() + h.sliceData;
}

// ------------------------------------------------------------- CRA RPS probe
bool parseHevcCraRpsInfo(const QByteArray& auData, int srcPocBits,
                         const QVector<int>& ppsExtraBitsById,
                         int* craPoc, QVector<int>* retainPocs,
                         QString* errorReason)
{
    // Find the first CRA slice NAL (type 21) inside the AU data.
    int sc = 0, type = 0;
    for (int i = 0; (i = ttHevcNextNal(auData, i, &sc, &type)) >= 0; i += sc + 1) {
        if (type == 21) {
            QByteArray rbsp = ttHevcDeescape(auData.mid(i + sc));
            THevcBitReader r(rbsp);
            r.bits(16);
            if (r.bit() != 1) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA not first_slice");
                return false;
            }
            r.bit();                              // no_output_of_prior_pics
            int ppsId = int(r.ue());
            int extraBits = (ppsId >= 0 && ppsId < ppsExtraBitsById.size())
                ? ppsExtraBitsById.at(ppsId) : -1;
            if (extraBits < 0) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA references unknown PPS %1").arg(ppsId);
                return false;
            }
            for (int k = 0; k < extraBits; ++k) r.bit();
            int sliceType = int(r.ue());
            if (sliceType != 2) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA slice_type != I");
                return false;
            }
            *craPoc = int(r.bits(srcPocBits));
            if (r.bit() != 0) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA uses SPS RPS set");
                return false;
            }
            quint32 nneg = r.ue(), npos = r.ue();
            if (nneg > 16 || npos > 16 || r.error()) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA RPS parse error");
                return false;
            }
            retainPocs->clear();
            int p = *craPoc;
            for (quint32 k = 0; k < nneg; ++k) {
                p -= int(r.ue()) + 1;
                r.bit();                          // used flag (irrelevant)
                retainPocs->append(p);
            }
            if (r.error()) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA RPS parse overrun");
                return false;
            }
            std::sort(retainPocs->begin(), retainPocs->end());
            return true;
        }
    }
    if (errorReason) *errorReason = QStringLiteral("no CRA slice in AU");
    return false;
}

// -------------------------------------------------------- encoder packet fix
QByteArray rewriteHevcEncoderPacket(const QByteArray& packetData,
                                    const THevcSliceRewriteCtx& ctx,
                                    int packetIndex,
                                    QString* errorReason)
{
    auto fail = [errorReason](const QString& why) {
        if (errorReason) *errorReason = why;
        return QByteArray();
    };
    if (!ctx.encHeadersParsed || ctx.pocBase < 0 || ctx.srcPocBits <= 0
        || ctx.encPpsId < 1)
        return fail(QStringLiteral("rewrite context incomplete"));

    const int pocMax = 1 << ctx.srcPocBits;
    QByteArray out;
    out.reserve(packetData.size() + 32);

    int i = 0;
    while (i + 3 < packetData.size()) {
        int sc = 0;
        if (packetData.at(i) == 0 && packetData.at(i + 1) == 0) {
            if (packetData.at(i + 2) == 1) sc = 3;
            else if (i + 3 < packetData.size() && packetData.at(i + 2) == 0
                     && packetData.at(i + 3) == 1) sc = 4;
        }
        if (sc == 0) { return fail(QStringLiteral("packet NAL scan lost sync")); }
        // Find next start code (NAL end)
        int end = packetData.size();
        for (int j = i + sc + 1; j + 2 < packetData.size(); ++j) {
            if (packetData.at(j) == 0 && packetData.at(j + 1) == 0
                && (packetData.at(j + 2) == 1
                    || (j + 3 < packetData.size() && packetData.at(j + 2) == 0
                        && packetData.at(j + 3) == 1))) {
                end = j;
                break;
            }
        }
        QByteArray nal = packetData.mid(i, end - i);
        int type = (quint8(packetData.at(i + sc)) >> 1) & 0x3F;

        if (type == 32 || type == 33) {
            // Drop encoder VPS/SPS — the source sets rule the stream.
        } else if (type == 34) {
            QByteArray patched = patchHevcPpsId(nal, ctx.encPpsId);
            if (patched.isEmpty())
                return fail(QStringLiteral("encoder PPS id patch failed"));
            out += patched;
        } else if (type == 39 || type == 40 || type == 35) {
            out += nal;                           // SEI / AUD verbatim
        } else if ((type <= 9) || (type >= 16 && type <= 21)) {
            THevcSliceHeader h =
                parseHevcSliceHeader(nal, ctx.encSps, ctx.encPps);
            if (!h.ok)
                return fail(QStringLiteral("slice parse: %1").arg(h.error));

            const int poc = (ctx.pocBase + packetIndex) % pocMax;
            if (isIdrNal(h.nalType)) {
                if (packetIndex != 0)
                    return fail(QStringLiteral("unexpected mid-segment IDR"));
                // Demotion IDR -> CRA: type 21, insert poc + empty RPS + tmvp.
                h.nalType = 21;
                h.pocLsb = poc;
                h.rpsNeg.clear();
                h.rpsPos.clear();
                h.tmvp = ctx.encSps.temporalMvpEnabled ? 1 : 0;
            } else if (h.nalType == 0 || h.nalType == 1) {
                if (h.sliceType != 1)
                    return fail(QStringLiteral("non-P trail slice"));
                // POC anchoring: uniform shift keeps original deltas valid.
                // Retain extension: keep the used list untouched (CABAC
                // ref_idx conformance), append retain POCs as used=0.
                QVector<int> absUsed;
                int p = poc;
                for (const THevcRpsEntry& e : h.rpsNeg) {
                    p -= e.deltaPoc;
                    absUsed.append(p);
                }
                QVector<QPair<int, int>> merged;   // (absPoc, used) desc
                for (int k = 0; k < absUsed.size(); ++k)
                    merged.append({absUsed.at(k), h.rpsNeg.at(k).used});
                for (int rp : ctx.retainPocs) {
                    if (rp >= poc) continue;               // not yet decoded
                    if (rp < ctx.pocBase) continue;        // outside window
                    if (absUsed.contains(rp)) continue;    // already listed
                    merged.append({rp, 0});
                }
                std::sort(merged.begin(), merged.end(),
                          [](const QPair<int, int>& a, const QPair<int, int>& b)
                          { return a.first > b.first; });
                h.rpsNeg.clear();
                int prev = poc;
                for (const auto& m : merged) {
                    THevcRpsEntry e;
                    e.deltaPoc = prev - m.first;
                    e.used = m.second;
                    if (e.deltaPoc < 1)
                        return fail(QStringLiteral("retain merge delta < 1"));
                    h.rpsNeg.append(e);
                    prev = m.first;
                }
                h.pocLsb = poc;
            } else {
                return fail(QStringLiteral("unexpected slice NAL type %1")
                            .arg(h.nalType));
            }

            QByteArray rebuilt = buildHevcSliceHeader(
                h, ctx.encSps, ctx.encPps, ctx.srcPocBits, ctx.encPpsId);
            out += nal.left(sc);                  // original start code
            out += ttHevcEscape(rebuilt);
        } else {
            return fail(QStringLiteral("unexpected NAL type %1").arg(type));
        }
        i = end;
    }
    return out;
}

int ttHevcNextNal(const QByteArray& data, int from, int* scLen, int* nalType)
{
    for (int i = from; i + 4 < data.size(); ++i) {
        int sc = 0;
        if (data.at(i) == 0 && data.at(i + 1) == 0) {
            if (data.at(i + 2) == 1) sc = 3;
            else if (data.at(i + 2) == 0 && data.at(i + 3) == 1) sc = 4;
        }
        if (sc == 0) continue;
        *scLen   = sc;
        *nalType = (quint8(data.at(i + sc)) >> 1) & 0x3F;
        return i;
    }
    return -1;
}
