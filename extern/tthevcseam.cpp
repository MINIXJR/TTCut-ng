/*----------------------------------------------------------------------------*/
/* HEVC seam machinery — see tthevcseam.h. Port of the validated PoC          */
/* (CLAUDE_TMP/TTCut-ng/hevc_rasl/poc/hevc_seam_poc.py, 2026-07-21).          */
/*----------------------------------------------------------------------------*/

#include "tthevcseam.h"
#include "../avstream/ttannexb.h"
#include "../avstream/ttbitstream.h"

#include <QtGlobal>

#include <algorithm>

// -------------------------------------------------------------- start codes
// ------------------------------------------------------------------ SPS parse
// Scaling list data walk with flat-16 tracking (H.265 7.3.4).
// A list is "flat 16" when every coefficient (and the DC coef for
// sizeId >= 2) decodes to 16 — numerically identical to scaling disabled.
// pred_matrix_id_delta references copy earlier lists, inheriting flatness;
// delta == 0 references the DEFAULT list, which is NOT flat -> not flat16.
static void parseScalingListData(TTBitReader& r, bool* allFlat16)
{
    *allFlat16 = true;
    for (int sizeId = 0; sizeId < 4; ++sizeId) {
        for (int matrixId = 0; matrixId < 6;
             matrixId += (sizeId == 3) ? 3 : 1) {
            int predMode = r.bits(1);
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
    int sc = ttStartCodeLength(spsNal);
    QByteArray rbsp = ttRbspFromNal(spsNal.mid(sc));
    TTBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 33) {
        info.invalidReason = QStringLiteral("not an SPS NAL");
        return info;
    }
    r.bits(4);                                   // sps_video_parameter_set_id
    info.maxSubLayersMinus1 = int(r.bits(3));
    r.bits(1);                                     // temporal_id_nesting
    if (info.maxSubLayersMinus1 != 0) {
        info.invalidReason = QStringLiteral("sub-layers unsupported");
        return info;
    }
    // profile_tier_level(1, 0): 2+1+5+32+4x1+43+1 = 88 bits general + 8 level
    r.skip(88 + 8);

    info.spsId = int(r.ue());
    info.chromaFormatIdc = int(r.ue());
    if (info.chromaFormatIdc == 3) r.bits(1);      // separate_colour_plane
    info.picWidth  = int(r.ue());
    info.picHeight = int(r.ue());
    if (r.bits(1)) {                               // conformance_window
        r.ue(); r.ue(); r.ue(); r.ue();
    }
    info.bitDepthLuma   = int(r.ue()) + 8;
    info.bitDepthChroma = int(r.ue()) + 8;
    info.log2MaxPocLsb  = int(r.ue()) + 4;
    int subLayerOrdering = r.bits(1);
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
    info.scalingListEnabled = r.bits(1);
    if (info.scalingListEnabled) {
        info.scalingListDataPresent = r.bits(1);
        if (info.scalingListDataPresent) {
            bool flat = true;
            parseScalingListData(r, &flat);
            info.scalingListFlat16 = flat;
        } else {
            // Default lists active — NOT flat.
            info.scalingListFlat16 = false;
        }
    }
    info.ampEnabled = r.bits(1);
    info.saoEnabled = r.bits(1);
    info.pcmEnabled = r.bits(1);
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
    info.longTermRefPicsPresent = r.bits(1);
    if (info.longTermRefPicsPresent) {
        info.invalidReason = QStringLiteral("long-term ref pics unsupported");
        return info;
    }
    info.temporalMvpEnabled = r.bits(1);
    info.strongIntraSmoothing = r.bits(1);
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
    int sc = ttStartCodeLength(ppsNal);
    QByteArray rbsp = ttRbspFromNal(ppsNal.mid(sc));
    TTBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 34) {
        info.invalidReason = QStringLiteral("not a PPS NAL");
        return info;
    }
    info.ppsId = int(r.ue());
    info.spsId = int(r.ue());
    info.dependentSliceSegments = r.bits(1);
    info.outputFlagPresent = r.bits(1);
    info.numExtraSliceHeaderBits = int(r.bits(3));
    info.signDataHiding = r.bits(1);
    info.cabacInitPresent = r.bits(1);
    info.numRefIdxL0DefaultMinus1 = int(r.ue());
    info.numRefIdxL1DefaultMinus1 = int(r.ue());
    r.se();                                      // init_qp_minus26
    r.bits(1);                                     // constrained_intra_pred
    r.bits(1);                                     // transform_skip_enabled
    if (r.bits(1))                                 // cu_qp_delta_enabled
        r.ue();                                  // diff_cu_qp_delta_depth
    r.se();                                      // pps_cb_qp_offset
    r.se();                                      // pps_cr_qp_offset
    info.sliceChromaQpOffsetsPresent = r.bits(1);
    info.weightedPred = r.bits(1);
    info.weightedBipred = r.bits(1);
    r.bits(1);                                     // transquant_bypass_enabled
    info.tilesEnabled = r.bits(1);
    info.entropyCodingSync = r.bits(1);
    if (info.tilesEnabled) {
        info.invalidReason = QStringLiteral("tiles unsupported");
        return info;
    }
    info.ppsLoopFilterAcrossSlices = r.bits(1);
    info.deblockingControlPresent = r.bits(1);
    if (info.deblockingControlPresent) {
        r.bits(1);                                 // deblocking_filter_override_enabled
        if (r.bits(1) == 0) {                      // pps_deblocking_filter_disabled
            r.se();                              // pps_beta_offset_div2
            r.se();                              // pps_tc_offset_div2
        }
    }
    if (r.bits(1)) {                               // pps_scaling_list_data_present
        bool flatIgnored = true;
        parseScalingListData(r, &flatIgnored);   // walk to stay in sync
    }
    info.listsModificationPresent = r.bits(1);
    r.ue();                                      // log2_parallel_merge_level_minus2
    info.sliceHeaderExtension = r.bits(1);
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
    int sc = ttStartCodeLength(ppsNalWithStartCode);
    if (sc == 0 || newPpsId < 1 || newPpsId > 63)
        return QByteArray();
    QByteArray rbsp = ttRbspFromNal(ppsNalWithStartCode.mid(sc));
    TTBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    if (((hdr >> 9) & 0x3F) != 34) return QByteArray();
    if (r.bits(1) != 1) return QByteArray();       // pps_id must be ue(0) = '1'

    // Locate the rbsp stop bit (last set bit) so trailing alignment can be
    // rebuilt after the shift.
    int totalBits = rbsp.size() * 8;
    int last1 = totalBits - 1;
    const quint8* d = reinterpret_cast<const quint8*>(rbsp.constData());
    while (last1 > r.pos() && ((d[last1 >> 3] >> (7 - (last1 & 7))) & 1) == 0)
        --last1;
    if (last1 <= r.pos()) return QByteArray();

    TTBitWriter w;
    w.bits(hdr, 16);
    w.ue(quint32(newPpsId));
    w.copyBits(r, last1 - r.pos());
    w.rbspTrailingBits();                           // stop bit + padding

    return ppsNalWithStartCode.left(sc) + ttNalFromRbsp(w.data());
}

// --------------------------------------------------------------- slice header
bool isIrapNal(int t)  { return t >= 16 && t <= 23; }
bool isIdrNal(int t)   { return t == 19 || t == 20; }

THevcSliceHeader parseHevcSliceHeader(const QByteArray& nalWithSc,
                                      const THevcSpsSeamInfo& sps,
                                      const THevcPpsSeamInfo& pps)
{
    THevcSliceHeader h;
    int sc = ttStartCodeLength(nalWithSc);
    QByteArray rbsp = ttRbspFromNal(nalWithSc.mid(sc));
    TTBitReader r(rbsp);

    quint32 hdr = r.bits(16);
    h.nalType = int((hdr >> 9) & 0x3F);
    h.nuhRest = hdr & 0x1FF;

    auto fail = [&h](const char* why) {
        h.ok = false; h.error = QString::fromLatin1(why); return h;
    };

    if (r.bits(1) != 1) return fail("first_slice_segment_in_pic_flag != 1");
    if (isIrapNal(h.nalType)) h.noOutputPrior = r.bits(1);
    h.ppsId = int(r.ue());
    if (h.ppsId != 0) return fail("encoder pps_id != 0");
    for (int i = 0; i < pps.numExtraSliceHeaderBits; ++i) r.bits(1);
    h.sliceType = int(r.ue());
    if (h.sliceType == 0) return fail("B slice in encoder output");
    if (h.sliceType > 2) return fail("bad slice_type");

    if (!isIdrNal(h.nalType)) {
        h.pocLsb = int(r.bits(sps.log2MaxPocLsb));
        if (r.bits(1) != 0) return fail("st_rps_sps_flag != 0");
        quint32 nneg = r.ue(), npos = r.ue();
        if (nneg > 16 || npos > 16) return fail("RPS too large");
        for (quint32 i = 0; i < nneg; ++i) {
            THevcRpsEntry e; e.deltaPoc = int(r.ue()) + 1; e.used = r.bits(1);
            h.rpsNeg.append(e);
        }
        for (quint32 i = 0; i < npos; ++i) {
            THevcRpsEntry e; e.deltaPoc = int(r.ue()) + 1; e.used = r.bits(1);
            h.rpsPos.append(e);
        }
        if (sps.temporalMvpEnabled) h.tmvp = r.bits(1);
    }
    if (sps.saoEnabled) { h.saoLuma = r.bits(1); h.saoChroma = r.bits(1); }

    if (h.sliceType == 1) {                       // P
        h.numRefIdxOverride = r.bits(1);
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
                h.lumaWeightFlag.append(r.bits(1));
            for (int i = 0; i < nActive; ++i) {
                if (h.lumaWeightFlag.at(i)) {
                    qint32 wgt = r.se(), off = r.se();
                    h.lumaWeights.append({wgt, off});
                }
            }
            if (sps.chromaFormatIdc != 0) {
                for (int i = 0; i < nActive; ++i)
                    h.chromaWeightFlag.append(r.bits(1));
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
        h.loopFilterAcross = r.bits(1);
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

    if (r.bits(1) != 1) return fail("alignment stop bit missing");
    while (r.pos() & 7) {
        if (r.bits(1) != 0) return fail("alignment zero bit not zero");
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
    TTBitWriter w;
    w.bits(0, 1);                                    // forbidden_zero
    w.bits(quint32(h.nalType), 6);
    w.bits(h.nuhRest, 9);
    w.bits(1, 1);                                    // first_slice
    if (isIrapNal(h.nalType)) w.bits(h.noOutputPrior, 1);
    w.ue(quint32(writePpsId));
    // numExtraSliceHeaderBits is 0 for x265 PPS (asserted in the preflight)
    w.ue(quint32(h.sliceType));
    if (!isIdrNal(h.nalType)) {
        w.bits(quint32(h.pocLsb), writePocBits);
        w.bits(0, 1);                                // st_rps_sps_flag
        w.ue(quint32(h.rpsNeg.size()));
        w.ue(quint32(h.rpsPos.size()));
        for (const THevcRpsEntry& e : h.rpsNeg) {
            w.ue(quint32(e.deltaPoc - 1)); w.bits(e.used, 1);
        }
        for (const THevcRpsEntry& e : h.rpsPos) {
            w.ue(quint32(e.deltaPoc - 1)); w.bits(e.used, 1);
        }
        if (sps.temporalMvpEnabled) w.bits(h.tmvp, 1);
    }
    if (sps.saoEnabled) { w.bits(h.saoLuma, 1); w.bits(h.saoChroma, 1); }
    if (h.sliceType == 1) {
        w.bits(h.numRefIdxOverride, 1);
        if (h.numRefIdxOverride) w.ue(quint32(h.l0ActiveMinus1));
        if (h.hasCollocatedRefIdx) w.ue(h.collocatedRefIdx);
        if (pps.weightedPred) {
            w.ue(h.lumaLog2Denom);
            if (sps.chromaFormatIdc != 0) w.se(h.deltaChromaDenom);
            for (int f : h.lumaWeightFlag) w.bits(f, 1);
            for (const auto& lw : h.lumaWeights) { w.se(lw.first); w.se(lw.second); }
            if (sps.chromaFormatIdc != 0) {
                for (int f : h.chromaWeightFlag) w.bits(f, 1);
                for (const auto& cw : h.chromaWeights)
                    for (qint32 v : cw) w.se(v);
            }
        }
        w.ue(h.fiveMinusMaxMergeCand);
    }
    w.se(h.qpDelta);
    if (h.hasLoopFilterAcross) w.bits(h.loopFilterAcross, 1);
    if (pps.tilesEnabled || pps.entropyCodingSync) {
        w.ue(h.numEntryPoints);
        if (h.numEntryPoints > 0) {
            w.ue(h.offsetLenMinus1);
            for (quint32 off : h.entryPointOffsets)
                w.bits(off, int(h.offsetLenMinus1) + 1);
        }
    }
    w.rbspTrailingBits();
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
            QByteArray rbsp = ttRbspFromNal(auData.mid(i + sc));
            TTBitReader r(rbsp);
            r.bits(16);
            if (r.bits(1) != 1) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA not first_slice");
                return false;
            }
            r.bits(1);                              // no_output_of_prior_pics
            int ppsId = int(r.ue());
            int extraBits = (ppsId >= 0 && ppsId < ppsExtraBitsById.size())
                ? ppsExtraBitsById.at(ppsId) : -1;
            if (extraBits < 0) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA references unknown PPS %1").arg(ppsId);
                return false;
            }
            for (int k = 0; k < extraBits; ++k) r.bits(1);
            int sliceType = int(r.ue());
            if (sliceType != 2) {
                if (errorReason) *errorReason =
                    QStringLiteral("CRA slice_type != I");
                return false;
            }
            *craPoc = int(r.bits(srcPocBits));
            if (r.bits(1) != 0) {
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
                r.bits(1);                          // used flag (irrelevant)
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
            out += ttNalFromRbsp(rebuilt);
        } else {
            return fail(QStringLiteral("unexpected NAL type %1").arg(type));
        }
        i = end;
    }
    return out;
}

int ttHevcNextNal(const QByteArray& data, int from, int* scLen, int* nalType)
{
    const int i = ttNextStartCode(reinterpret_cast<const uint8_t*>(data.constData()),
                                  data.size(), from, scLen);
    if (i < 0) return -1;
    *nalType = (quint8(data.at(i + *scLen)) >> 1) & 0x3F;
    return i;
}
