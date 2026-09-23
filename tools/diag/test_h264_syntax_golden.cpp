// Golden-output harness for the H.264/H.265 bit-stream helpers (spec
// docs/superpowers/specs/2026-09-23-bitstream-unification-design.md).
// Prints one line per (input, function): call count and an MD5 over all
// outputs. Recorded once with the OLD bit layer into
// tools/diag/testdata/h264-syntax/golden.txt; every later build must print
// the same. WELL-FORMED input only - truncated input is where old and new
// reader semantics differ by design (test_h264_truncated_slice checks that
// those calls terminate).
//   test_h264_syntax_golden <fixture-cache-dir> <vector-dir> <work-dir>
//   (work-dir receives the small streams built from the hand-made SPS)
#include <cstdio>
#include <functional>
#include <map>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QString>
#include <QVector>

#include "avstream/ttbitstream.h"
#include "avstream/tth264syntax.h"
#include "avstream/ttnaluparser.h"
#include "extern/tth264bitstream.h"
#include "extern/tthevcseam.h"

// ---------------------------------------------------------------- digest
// QCryptographicHash is not copyable, so Digest lives in a std::map and is
// only ever default-constructed in place (operator[]).
class Digest
{
public:
    void add(const QByteArray& out)
    {
        const quint32 n = quint32(out.size());
        const char len[4] = { char(n >> 24), char(n >> 16), char(n >> 8), char(n) };
        mHash.addData(QByteArrayView(len, 4));
        mHash.addData(out);
        ++mCount;
    }
    void addInt(qint64 v) { add(QByteArray::number(v)); }
    QString line(const QString& input, const QString& fn)
    {
        return QString("%1 %2 n=%3 md5=%4").arg(input, fn).arg(mCount)
               .arg(QString::fromLatin1(mHash.result().toHex()));
    }
private:
    QCryptographicHash mHash{QCryptographicHash::Md5};
    int mCount = 0;
};

class Report
{
public:
    Digest& at(const QString& fn) { if (!mOrder.contains(fn)) mOrder.append(fn); return mMap[fn]; }
    void print(const QString& input)
    {
        for (const QString& fn : mOrder) printf("%s\n", qPrintable(mMap[fn].line(input, fn)));
    }
private:
    std::map<QString, Digest> mMap;
    QStringList mOrder;
};

static QByteArray spsInfoText(const TTH264SpsInfo& s)
{
    return QString("fn=%1 poc=%2 lsb=%3 fmo=%4 %5x%6 bd=%7")
        .arg(s.log2MaxFrameNumMinus4).arg(s.pocType).arg(s.log2MaxPocLsbMinus4)
        .arg(int(s.frameMbsOnly)).arg(s.picWidth).arg(s.picHeight).arg(s.bitDepthLuma).toLatin1();
}

static QByteArray ppsInfoText(const TTH264PpsInfo& p)
{
    return QString("cabac=%1 bfpoc=%2 dbk=%3 rpc=%4 wp=%5 wbi=%6 l0=%7 l1=%8 valid=%9")
        .arg(int(p.entropyCodingModeFlag)).arg(int(p.bottomFieldPicOrderPresent))
        .arg(int(p.deblockingFilterControlPresent)).arg(int(p.redundantPicCntPresent))
        .arg(int(p.weightedPredFlag)).arg(p.weightedBipredIdc)
        .arg(p.numRefIdxL0DefaultActiveMinus1).arg(p.numRefIdxL1DefaultActiveMinus1)
        .arg(int(p.valid)).toLatin1();
}

static QByteArray basicsText(bool ok, const TTNaluParser::H264SpsBasics& b)
{
    return QString("ok=%1 fn=%2 fmo=%3 have=%4").arg(int(ok)).arg(b.log2MaxFrameNum)
        .arg(int(b.frameMbsOnlyFlag)).arg(int(b.haveFrameMbsOnlyFlag)).toLatin1();
}

// ---------------------------------------------------- hand-built SPS set
// Branches no encoder in the vector set emits: SPS scaling matrices
// (4:2:0 and 4:4:4), poc_type 1 with a short and an over-long cycle,
// VUI with NAL+VCL HRD, frame cropping, no VUI at all.
struct SpsSpec {
    const char* name;
    int  profile = 100;
    int  chroma = 1;
    bool scaling = false;
    int  pocType = 0;
    int  pocCycle = 0;
    bool frameMbsOnly = true;
    bool cropping = false;
    bool vui = true;
    bool hrd = false;
};

static void writeHrd(TTBitWriter& w)
{
    w.ue(1);                       // cpb_cnt_minus1
    w.bits(4, 4); w.bits(6, 4);    // bit_rate_scale, cpb_size_scale
    for (int i = 0; i < 2; ++i) { w.ue(1000 + i); w.ue(2000 + i); w.bits(i & 1, 1); }
    w.bits(23, 5); w.bits(23, 5); w.bits(23, 5); w.bits(24, 5);
}

static QByteArray buildSps(const SpsSpec& s)
{
    TTBitWriter w;
    w.bits(0x67, 8);
    w.bits(uint32_t(s.profile), 8);
    w.bits(0, 8);
    w.bits(40, 8);
    w.ue(0);
    if (ttIsH264HighProfile(uint32_t(s.profile))) {
        w.ue(uint32_t(s.chroma));
        if (s.chroma == 3) w.bits(0, 1);
        w.ue(0); w.ue(0); w.bits(0, 1);
        w.bits(s.scaling ? 1 : 0, 1);
        if (s.scaling) {
            const int lists = (s.chroma != 3) ? 8 : 12;
            for (int i = 0; i < lists; ++i) {
                const bool present = (i % 3) != 2;
                w.bits(present ? 1 : 0, 1);
                if (!present) continue;
                const int size = (i < 6) ? 16 : 64;
                // delta_scale: -8 on the first entry makes nextScale 0 for
                // list 1 (use-default path), small ramps elsewhere.
                for (int j = 0; j < size; ++j) {
                    const int delta = (i == 1 && j == 0) ? -8 : ((j % 5) - 2);
                    w.se(delta);
                    if (i == 1 && j == 0) break;   // nextScale == 0 ends the list
                }
            }
        }
    }
    w.ue(4);                                   // log2_max_frame_num_minus4
    w.ue(uint32_t(s.pocType));
    if (s.pocType == 0) {
        w.ue(2);
    } else if (s.pocType == 1) {
        w.bits(0, 1); w.se(-2); w.se(1);
        w.ue(uint32_t(s.pocCycle));
        for (int i = 0; i < s.pocCycle; ++i) w.se((i % 2) ? 2 : -3);
    }
    w.ue(4);                                   // max_num_ref_frames
    w.bits(0, 1);
    w.ue(119);
    w.ue(s.frameMbsOnly ? 67 : 33);
    w.bits(s.frameMbsOnly ? 1 : 0, 1);
    if (!s.frameMbsOnly) w.bits(1, 1);
    w.bits(1, 1);                              // direct_8x8_inference_flag
    w.bits(s.cropping ? 1 : 0, 1);
    if (s.cropping) { w.ue(0); w.ue(0); w.ue(0); w.ue(4); }
    w.bits(s.vui ? 1 : 0, 1);
    if (s.vui) {
        w.bits(1, 1); w.bits(255, 8); w.bits(16, 16); w.bits(11, 16);   // Extended_SAR
        w.bits(1, 1); w.bits(1, 1);                                     // overscan
        w.bits(1, 1); w.bits(5, 3); w.bits(0, 1); w.bits(1, 1);
        w.bits(1, 8); w.bits(1, 8); w.bits(1, 8);                       // colour description
        w.bits(1, 1); w.ue(0); w.ue(0);                                 // chroma loc
        w.bits(1, 1); w.bits(1, 32); w.bits(50, 32); w.bits(1, 1);      // timing (EPB in VUI)
        w.bits(s.hrd ? 1 : 0, 1); if (s.hrd) writeHrd(w);               // NAL HRD
        w.bits(s.hrd ? 1 : 0, 1); if (s.hrd) writeHrd(w);               // VCL HRD
        if (s.hrd) w.bits(0, 1);                                        // low_delay_hrd_flag
        w.bits(0, 1);                                                   // pic_struct_present_flag
        w.bits(0, 1);                                                   // bitstream_restriction_flag
    }
    w.rbspTrailingBits();
    return QByteArray("\x00\x00\x00\x01", 4) + ttNalFromRbsp(w.data());
}

static QVector<SpsSpec> handSpsSpecs()
{
    return {
        { "sps_high_scaling",   100, 1, true,  0, 0,   true,  true,  true,  false },
        { "sps_444_scaling",    244, 3, true,  0, 0,   true,  false, true,  false },
        { "sps_poc1",            77, 1, false, 1, 3,   true,  false, true,  false },
        { "sps_poc1_toolong",    77, 1, false, 1, 300, true,  false, true,  false },
        { "sps_field_hrd",       77, 1, false, 0, 0,   false, false, true,  true  },
        { "sps_baseline_novui",  66, 1, false, 2, 0,   true,  false, false, false },
    };
}

static void runHandBuiltSps()
{
    for (const SpsSpec& s : handSpsSpecs()) {
        const QByteArray sps = buildSps(s);
        const TTH264SpsInfo info = ttParseH264SpsInfo(sps);
        printf("%s parseH264SpsInfo %s\n", s.name, spsInfoText(info).constData());
        TTNaluParser::H264SpsBasics b;
        const bool ok = TTNaluParser::parseH264SpsBasics(
            reinterpret_cast<const uint8_t*>(sps.constData()), sps.size(), b);
        printf("%s parseH264SpsBasics %s\n", s.name, basicsText(ok, b).constData());
        Report rep;
        for (int reorder : { 0, 2 })
            for (bool paff : { false, true }) {
                rep.at("patchH264SpsReorderFrames").add(ttPatchH264SpsReorderFrames(sps, reorder, paff));
                rep.at("patchSpsNalsInAccessUnit").add(ttPatchSpsNalsInAccessUnit(sps, reorder, paff));
            }
        TTH264SpsInfo found{};
        const bool foundOk = ttFindH264SpsInPacket(sps, found);   // before spsInfoText(found):
        rep.at("findH264SpsInPacket").add(QByteArray::number(foundOk) + spsInfoText(found));  // operand order is unspecified
        rep.print(s.name);
    }
}

// ------------------------------------------------ hand-built PPS/slices
// Slice-header branches the encoders in the vector set never emit, found
// by the gcov run over the golden set: B slices with L1 modification and
// explicit bi-pred weights, long-term RPLM/MMCO operations, SP/SI slices,
// redundant_pic_cnt, delta_pic_order_cnt_bottom, field slices, IDR
// demotion, the RPLM translation bail-out, multiple slice groups, 3-byte
// start codes and leading bytes before the first start code.
static constexpr int kFnBits = 4;
static constexpr int kPocBits = 6;

static QByteArray buildPps(const TTH264PpsInfo& p, int sliceGroups = 1)
{
    TTBitWriter w;
    w.bits(0x68, 8);
    w.ue(0); w.ue(0);                            // pps_id, sps_id
    w.bits(p.entropyCodingModeFlag, 1);
    w.bits(p.bottomFieldPicOrderPresent, 1);
    w.ue(uint32_t(sliceGroups - 1));
    if (sliceGroups > 1) {                       // slice_group_map_type 0 + run lengths
        w.ue(0);
        for (int i = 0; i < sliceGroups; ++i) w.ue(uint32_t(10 + i));
    }
    w.ue(uint32_t(p.numRefIdxL0DefaultActiveMinus1));
    w.ue(uint32_t(p.numRefIdxL1DefaultActiveMinus1));
    w.bits(p.weightedPredFlag, 1);
    w.bits(uint32_t(p.weightedBipredIdc), 2);
    w.se(0); w.se(0); w.se(-2);                  // pic_init_qp/qs, chroma_qp_index_offset
    w.bits(p.deblockingFilterControlPresent, 1);
    w.bits(0, 1);                                // constrained_intra_pred_flag
    w.bits(p.redundantPicCntPresent, 1);
    w.rbspTrailingBits();
    return QByteArray("\x00\x00\x00\x01", 4) + ttNalFromRbsp(w.data());
}

struct SliceSpec {
    const char* name;
    int  nalType;                  // 1 or 5
    int  refIdc;
    int  sliceType;                // 0=P 1=B 2=I 3=SP 4=SI
    bool frameMbsOnly;
    bool field;
    bool bottom;
    TTH264PpsInfo pps;
    bool override;
    int  l0, l1;
    QVector<QPair<int, int>> rplmL0, rplmL1;   // (idc, value); idc 3 is appended
    QVector<int> mmco;             // ops with their arguments, 0-terminated
    int  payloadBytes;
};

// NAL body, no start code. fnBits/pocLsb follow the SPS the slice is paired
// with (poc_lsb only exists for pic_order_cnt_type 0).
static QByteArray buildSlice(const SliceSpec& s, int fnBits = kFnBits, bool pocLsb = true)
{
    TTBitWriter w;
    const int m = s.sliceType;
    w.bits(uint32_t((s.refIdc << 5) | s.nalType), 8);
    w.ue(0);                                     // first_mb_in_slice
    w.ue(uint32_t(m + 5));                       // slice_type
    w.ue(0);                                     // pic_parameter_set_id
    w.bits(5, fnBits);                           // frame_num
    if (!s.frameMbsOnly) {
        w.bits(s.field, 1);
        if (s.field) w.bits(s.bottom, 1);
    }
    if (s.nalType == 5) w.ue(3);                 // idr_pic_id
    if (pocLsb) {
        w.bits(12, kPocBits);                    // pic_order_cnt_lsb
        if (s.pps.bottomFieldPicOrderPresent && !s.field) w.se(-1);
    }
    if (s.pps.redundantPicCntPresent) w.ue(0);
    if (m == 1) w.bits(1, 1);                    // direct_spatial_mv_pred_flag
    int l0 = s.pps.numRefIdxL0DefaultActiveMinus1, l1 = s.pps.numRefIdxL1DefaultActiveMinus1;
    if (m == 0 || m == 1 || m == 3) {
        w.bits(s.override, 1);
        if (s.override) {
            w.ue(uint32_t(s.l0)); l0 = s.l0;
            if (m == 1) { w.ue(uint32_t(s.l1)); l1 = s.l1; }
        }
    }
    auto rplm = [&w](const QVector<QPair<int, int>>& list) {
        w.bits(list.isEmpty() ? 0 : 1, 1);
        if (list.isEmpty()) return;
        for (const auto& e : list) { w.ue(uint32_t(e.first)); w.ue(uint32_t(e.second)); }
        w.ue(3);
    };
    if (m != 2 && m != 4) { rplm(s.rplmL0); if (m == 1) rplm(s.rplmL1); }
    const bool weights = (s.pps.weightedPredFlag && (m == 0 || m == 3))
                         || (s.pps.weightedBipredIdc == 1 && m == 1);
    if (weights) {
        w.ue(5); w.ue(4);                        // luma/chroma log2 weight denom
        auto table = [&w](int n) {
            for (int i = 0; i <= n; ++i) {
                const bool luma = (i % 2) == 0, chroma = (i % 3) == 0;
                w.bits(luma, 1);
                if (luma) { w.se(3 - i); w.se(i - 1); }
                w.bits(chroma, 1);
                if (chroma) for (int k = 0; k < 4; ++k) w.se(k - 2);
            }
        };
        table(l0);
        if (m == 1) table(l1);
    }
    if (s.refIdc != 0) {
        if (s.nalType == 5) {
            w.bits(0, 1); w.bits(1, 1);          // no_output_of_prior_pics, long_term_reference
        } else {
            w.bits(s.mmco.isEmpty() ? 0 : 1, 1);
            for (int v : s.mmco) w.ue(uint32_t(v));
        }
    }
    if (s.pps.entropyCodingModeFlag && m != 2 && m != 4) w.ue(1);   // cabac_init_idc
    w.se(-3);                                    // slice_qp_delta
    if (m == 3 || m == 4) { if (m == 3) w.bits(1, 1); w.se(2); }
    if (s.pps.deblockingFilterControlPresent) { w.ue(0); w.se(1); w.se(-1); }
    if (s.pps.entropyCodingModeFlag) {
        w.alignWith(1);                          // cabac_alignment_one_bit
        for (int i = 0; i < s.payloadBytes; ++i) w.bits(uint32_t((i * 37 + 11) & 0xFF), 8);
    } else {
        for (int i = 0; i < s.payloadBytes; ++i) w.bits(uint32_t((i * 53 + 7) & 0xFF), 8);
        w.bits(0x5, 3);                          // CAVLC data ends mid-byte
        w.rbspTrailingBits();
    }
    return ttNalFromRbsp(w.data());
}

static void runHandBuiltSlices()
{
    const QByteArray sc4("\x00\x00\x00\x01", 4), sc3("\x00\x00\x01", 3);
    //                         cabac  bfpoc  dbk    rpc    wp     wbi l0 l1 valid
    const TTH264PpsInfo full   = { true,  true,  true,  true,  true,  1,  1, 1, true };
    const TTH264PpsInfo cavlc  = { false, false, true,  false, true,  0,  0, 0, true };
    const TTH264PpsInfo plain  = { true,  false, true,  false, false, 0,  0, 0, true };
    const QVector<SliceSpec> specs = {
        { "slice_b_full", 1, 2, 1, true, false, false, full, true, 1, 1,
          { {0, 2}, {2, 1} }, { {1, 0} }, { 1, 3, 2, 1, 3, 4, 1, 6, 2, 4, 3, 5, 0 }, 20 },
        { "slice_sp_cavlc", 1, 2, 3, true, false, false, cavlc, true, 2, 0,
          { {0, 0} }, {}, { 1, 0, 0 }, 30 },
        { "slice_si_cavlc", 1, 2, 4, true, false, false, cavlc, false, 0, 0,
          {}, {}, { 1, 1, 0 }, 12 },
        { "slice_idr", 5, 3, 2, true, false, false, plain, false, 0, 0,
          {}, {}, {}, 16 },
        { "slice_field_p", 1, 2, 0, false, true, true, plain, false, 0, 0,
          {}, {}, { 1, 1, 0 }, 16 },
        { "slice_mbaff_p", 1, 2, 0, false, false, false, full, true, 2, 0,
          { {1, 1} }, {}, { 1, 0, 0 }, 16 },
        { "slice_p_bailout", 1, 2, 0, true, false, false, plain, false, 0, 0,
          { {0, 20} }, {}, { 1, 0, 0 }, 16 },
        // All MMCO operations on a P slice (neutralize does not parse
        // redundant_pic_cnt, and the rewrite does not parse B slices - both
        // are existing behaviour, recorded as such by slice_b_full).
        { "slice_p_mmco_all", 1, 2, 0, true, false, false, plain, false, 0, 0,
          {}, {}, { 1, 3, 2, 1, 3, 4, 1, 6, 2, 4, 3, 5, 0 }, 16 },
    };
    const QByteArray sps = buildSps({ "sps_for_slices", 100, 1, false, 0, 0, true, false, true, false });

    for (const SliceSpec& s : specs) {
        Report rep;
        const QByteArray nal = buildSlice(s);
        const QByteArray pps = buildPps(s.pps);
        const QByteArray au = sc4 + nal;
        // Leading bytes before a 3-byte start code, then a second copy.
        const QByteArray au2 = QByteArray("\xAA\xBB", 2) + sc3 + nal + sc4 + nal;
        const QByteArray packet = sps + pps + sc4 + nal;
        // Access unit delimiter + SEI in front, as encoders emit them.
        const QByteArray packetAudSei = sc4 + QByteArray("\x09\xF0", 2)
                                      + sc4 + QByteArray("\x06\x05\x01\x00\x80", 5) + packet;
        // Leading bytes, filler data (type 12) and a second PPS with pps_id 1.
        QByteArray pps1 = pps;
        {
            TTBitWriter w; w.bits(0x68, 8); w.ue(1); w.ue(0); w.bits(1, 1); w.bits(0, 1); w.ue(0);
            w.ue(0); w.ue(0); w.bits(0, 1); w.bits(0, 2); w.se(0); w.se(0); w.se(0);
            w.bits(1, 1); w.bits(0, 1); w.bits(0, 1); w.rbspTrailingBits();
            pps1 = sc4 + ttNalFromRbsp(w.data());
        }
        const QByteArray packetOdd = QByteArray("\xAA", 1) + sc4 + QByteArray("\x0C\xFF\xFF\x80", 4)
                                   + pps1 + packet;
        for (const QByteArray& a : { au, au2 }) {
            rep.at("neutralizeMmcoInAU").add(ttNeutralizeMmcoInAU(a, kFnBits, kPocBits, s.frameMbsOnly, s.pps));
            rep.at("readFrameNumFromAU").addInt(ttReadFrameNumFromAU(a, kFnBits));
            rep.at("readPocLsbFromAU").addInt(ttReadPocLsbFromAU(a, kFnBits, kPocBits, s.frameMbsOnly));
            rep.at("patchFrameNumInAU").add(ttPatchFrameNumInAU(a, kFnBits, 3, 1 << kFnBits));
            rep.at("patchPocLsbInPacket").add(ttPatchPocLsbInPacket(a, kFnBits, kPocBits, s.frameMbsOnly, 7));
            rep.at("patchSpsNalsInAccessUnit").add(ttPatchSpsNalsInAccessUnit(sps + a, 2, !s.frameMbsOnly));
        }
        for (int frameIndex : { 0, 5 })
            rep.at("rewriteEncoderSliceForSourceSps").add(ttRewriteEncoderSliceForSourceSps(nal,
                kFnBits, kPocBits, s.frameMbsOnly, kFnBits + 2, kPocBits + 2, s.frameMbsOnly,
                s.pps, 1, frameIndex, 0));
        // PAFF unification: frame-only encoder slice into a field-capable
        // source domain; legacy linear POC numbering (pocLsbBase < 0).
        if (s.frameMbsOnly)
            rep.at("rewriteEncoderSliceForSourceSps.toField").add(ttRewriteEncoderSliceForSourceSps(nal,
                kFnBits, kPocBits, true, kFnBits + 2, kPocBits + 2, false, s.pps, 1, 5, 0));
        rep.at("rewriteEncoderSliceForSourceSps.linearPoc").add(ttRewriteEncoderSliceForSourceSps(nal,
            kFnBits, kPocBits, s.frameMbsOnly, kFnBits + 2, kPocBits + 2, s.frameMbsOnly,
            s.pps, 1, 5, -1));
        for (const QByteArray& pk : { packet, packetAudSei, packetOdd })
            rep.at("rewriteEncoderPacketForSourceSps").add(ttRewriteEncoderPacketForSourceSps(pk,
                kFnBits, kPocBits, s.frameMbsOnly, kFnBits + 2, kPocBits + 2, s.frameMbsOnly,
                s.pps, 1, 5, 0));
        rep.at("patchSpsNalsInAccessUnit.leadingBytes").add(
            ttPatchSpsNalsInAccessUnit(QByteArray("\xAA\xBB", 2) + packetAudSei, 2, false));
        rep.at("extractPpsFromPacket").add(ttExtractPpsFromPacket(packet));
        rep.at("parseH264PpsInfo").add(ppsInfoText(ttParseH264PpsInfo(pps)));
        rep.at("patchPpsId").add(ttPatchH264PpsId(pps, 1));
        rep.print(s.name);
    }

    // A packet without any slice (parameter sets only) and bytes without a
    // start code: the functions' pass-through paths.
    {
        Report rep;
        const QByteArray noSlice = sps + buildPps(plain);
        const QByteArray noStartCode("\x12\x34\x56\x78\x9A", 5);
        for (const QByteArray& a : { noSlice, noStartCode }) {
            rep.at("neutralizeMmcoInAU").add(ttNeutralizeMmcoInAU(a, kFnBits, kPocBits, true, plain));
            rep.at("readFrameNumFromAU").addInt(ttReadFrameNumFromAU(a, kFnBits));
            rep.at("readPocLsbFromAU").addInt(ttReadPocLsbFromAU(a, kFnBits, kPocBits, true));
            rep.at("patchFrameNumInAU").add(ttPatchFrameNumInAU(a, kFnBits, 3, 1 << kFnBits));
            rep.at("patchPocLsbInPacket").add(ttPatchPocLsbInPacket(a, kFnBits, kPocBits, true, 7));
            rep.at("patchSpsNalsInAccessUnit").add(ttPatchSpsNalsInAccessUnit(a, 2, false));
            rep.at("rewriteEncoderPacketForSourceSps").add(ttRewriteEncoderPacketForSourceSps(a,
                kFnBits, kPocBits, true, kFnBits + 2, kPocBits + 2, true, plain, 1, 5, 0));
            rep.at("extractPpsFromPacket").add(ttExtractPpsFromPacket(a));
            TTH264SpsInfo f{};
            const bool ok = ttFindH264SpsInPacket(a, f);
            rep.at("findH264SpsInPacket").add(QByteArray::number(ok) + spsInfoText(f));
            rep.at("parseH264SpsInfo").add(spsInfoText(ttParseH264SpsInfo(a)));
            rep.at("patchH264SpsReorderFrames").add(ttPatchH264SpsReorderFrames(a, 2, false));
        }
        rep.print("no_slice_or_startcode");
    }

    // PPS with two slice groups (FMO) and SPS behind a 3-byte start code.
    const TTH264PpsInfo fmo = { false, false, true, false, false, 0, 0, 0, true };
    printf("pps_two_slice_groups parseH264PpsInfo %s\n",
           ppsInfoText(ttParseH264PpsInfo(buildPps(fmo, 2))).constData());
    printf("sps_startcode3 parseH264SpsInfo %s\n",
           spsInfoText(ttParseH264SpsInfo(sc3 + sps.mid(4))).constData());
}

// ------------------------------------------------------ H.264 streams
static int firstSliceNal(const QByteArray& au)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(au.constData());
    for (int s = TTNaluParser::findStartCodePayload(d, au.size(), 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(d, au.size(), s)) {
        const int t = d[s] & 0x1F;
        if (t == 1 || t == 5) return s;
    }
    return -1;
}

static void runH264File(const QString& label, const QString& path)
{
    TTNaluParser parser;
    if (!parser.openFile(path) || !parser.parseFile()) {
        printf("%s open/parse FAILED\n", qPrintable(label));
        return;
    }
    Report rep;
    const QByteArray sps = parser.getSPS(0);
    const QByteArray pps = parser.getPPS(0);
    const TTH264SpsInfo si = ttParseH264SpsInfo(sps);
    const TTH264PpsInfo pi = ttParseH264PpsInfo(pps);
    const bool paff = parser.isPAFF();
    rep.at("parser.summary").add(QString("aus=%1 gops=%2 paff=%3 sps=%4 pps=%5")
        .arg(parser.accessUnitCount()).arg(parser.gopCount()).arg(int(paff))
        .arg(parser.spsCount()).arg(parser.ppsCount()).toLatin1());
    for (int i = 0; i < parser.spsCount(); ++i) {
        const QByteArray s = parser.getSPS(i);
        rep.at("parseH264SpsInfo").add(spsInfoText(ttParseH264SpsInfo(s)));
        TTNaluParser::H264SpsBasics b;
        const bool ok = TTNaluParser::parseH264SpsBasics(
            reinterpret_cast<const uint8_t*>(s.constData()), s.size(), b);
        rep.at("parseH264SpsBasics").add(basicsText(ok, b));
        for (int reorder : { 0, 2 })
            for (bool pf : { false, true })
                rep.at("patchH264SpsReorderFrames").add(ttPatchH264SpsReorderFrames(s, reorder, pf));
    }
    for (int i = 0; i < parser.ppsCount(); ++i) {
        const QByteArray p = parser.getPPS(i);
        rep.at("parseH264PpsInfo").add(ppsInfoText(ttParseH264PpsInfo(p)));
        rep.at("patchPpsId").add(ttPatchH264PpsId(p, 1));
    }
    const int fnBits = si.log2MaxFrameNumMinus4 + 4;
    const int pocBits = si.log2MaxPocLsbMinus4 >= 0 ? si.log2MaxPocLsbMinus4 + 4 : 0;
    for (int i = 0; i < parser.accessUnitCount(); ++i) {
        const QByteArray au = parser.readAccessUnitData(i);
        const TTAccessUnit acc = parser.accessUnitAt(i);
        rep.at("parser.au").add(QString("%1 %2 %3 %4 %5 %6").arg(acc.index).arg(acc.decodeIndex)
            .arg(int(acc.isKeyframe)).arg(int(acc.isIDR)).arg(acc.sliceType).arg(int(acc.isFieldCoded)).toLatin1());
        rep.at("readFrameNumFromAU").addInt(ttReadFrameNumFromAU(au, fnBits));
        rep.at("readPocLsbFromAU").addInt(ttReadPocLsbFromAU(au, fnBits, pocBits, si.frameMbsOnly));
        rep.at("patchFrameNumInAU+3").add(ttPatchFrameNumInAU(au, fnBits, 3, 1 << fnBits));
        rep.at("patchFrameNumInAU-5").add(ttPatchFrameNumInAU(au, fnBits, -5, 1 << fnBits));
        if (pocBits > 0)
            rep.at("patchPocLsbInPacket").add(ttPatchPocLsbInPacket(au, fnBits, pocBits, si.frameMbsOnly, 5));
        rep.at("neutralizeMmcoInAU").add(ttNeutralizeMmcoInAU(au, fnBits, pocBits, si.frameMbsOnly, pi));
        rep.at("patchSpsNalsInAccessUnit").add(ttPatchSpsNalsInAccessUnit(au, 2, paff));
        rep.at("extractPpsFromPacket").add(ttExtractPpsFromPacket(au));
        rep.at("findH264SpsInPacket").add([&] { TTH264SpsInfo f{}; const bool ok = ttFindH264SpsInPacket(au, f);
                                                return QByteArray::number(ok) + spsInfoText(f); }());
        rep.at("parseH264SliceTypeFromPacket").addInt(TTNaluParser::parseH264SliceTypeFromPacket(
            reinterpret_cast<const uint8_t*>(au.constData()), au.size()));
        const int s = firstSliceNal(au);
        if (s >= 0) {
            int fn = -1; bool isField = false, isBottom = false;
            TTNaluParser::parseH264SliceFieldInfo(reinterpret_cast<const uint8_t*>(au.constData()) + s,
                                                  au.size() - s, fnBits, fn, isField, isBottom);
            rep.at("parseH264SliceFieldInfo").add(QString("%1 %2 %3").arg(fn).arg(int(isField)).arg(int(isBottom)).toLatin1());
        }
        // SPS unification: the stream's own slices as "encoder" slices,
        // widened into a source domain two bits wider (<= 16); field-coded
        // streams too (exercises the field_pic_flag path).
        if (pocBits > 0) {
            rep.at("rewriteEncoderPacketForSourceSps").add(ttRewriteEncoderPacketForSourceSps(au,
                fnBits, pocBits, si.frameMbsOnly,
                qMin(fnBits + 2, 16), qMin(pocBits + 2, 16), si.frameMbsOnly,
                pi, 1, i % 50, 0));
            if (si.frameMbsOnly && i % 10 == 0)
                rep.at("rewriteEncoderPacketForSourceSps.toField").add(ttRewriteEncoderPacketForSourceSps(au,
                    fnBits, pocBits, true, qMin(fnBits + 2, 16), qMin(pocBits + 2, 16), false,
                    pi, 1, i % 50, -1));
            if (acc.isIDR && i % 50 == 0)
                rep.at("rewriteEncoderPacketForSourceSps.idr5").add(ttRewriteEncoderPacketForSourceSps(au,
                    fnBits, pocBits, si.frameMbsOnly,
                    qMin(fnBits + 2, 16), qMin(pocBits + 2, 16), si.frameMbsOnly,
                    pi, 1, 5, 0));
        }
    }
    rep.print(label);
}

// ------------------------------------------------------ H.265 streams
static void runH265File(const QString& label, const QString& path, const THevcSpsSeamInfo* srcSps,
                        THevcSpsSeamInfo* outSps, int* outCraPoc, QVector<int>* outRetain)
{
    TTNaluParser parser;
    if (!parser.openFile(path) || !parser.parseFile()) {
        printf("%s open/parse FAILED\n", qPrintable(label));
        return;
    }
    Report rep;
    THevcSpsSeamInfo sps = parseHevcSpsSeamInfo(parser.getSPS(0));
    THevcPpsSeamInfo pps = parseHevcPpsSeamInfo(parser.getPPS(0));
    if (outSps) *outSps = sps;
    rep.at("parser.summary").add(QString("aus=%1 gops=%2").arg(parser.accessUnitCount())
                                 .arg(parser.gopCount()).toLatin1());
    rep.at("parseHevcSpsSeamInfo").add(QString("v=%1 r=%2 id=%3 c=%4 %5x%6 bd=%7/%8 poc=%9 dpb=%10 cb=%11+%12 tb=%13+%14 tu=%15/%16 sl=%17/%18/%19 amp=%20 sao=%21 pcm=%22 rps=%23 lt=%24 tmvp=%25 sis=%26")
        .arg(int(sps.valid)).arg(sps.invalidReason).arg(sps.spsId).arg(sps.chromaFormatIdc)
        .arg(sps.picWidth).arg(sps.picHeight).arg(sps.bitDepthLuma).arg(sps.bitDepthChroma)
        .arg(sps.log2MaxPocLsb).arg(sps.maxDecPicBufferingMinus1).arg(sps.log2MinCbSizeMinus3)
        .arg(sps.log2DiffMaxMinCbSize).arg(sps.log2MinTbSizeMinus2).arg(sps.log2DiffMaxMinTbSize)
        .arg(sps.tuDepthInter).arg(sps.tuDepthIntra).arg(int(sps.scalingListEnabled))
        .arg(int(sps.scalingListDataPresent)).arg(int(sps.scalingListFlat16)).arg(int(sps.ampEnabled))
        .arg(int(sps.saoEnabled)).arg(int(sps.pcmEnabled)).arg(sps.numShortTermRefPicSets)
        .arg(int(sps.longTermRefPicsPresent)).arg(int(sps.temporalMvpEnabled)).arg(int(sps.strongIntraSmoothing)).toLatin1());
    rep.at("parseHevcPpsSeamInfo").add(QString("v=%1 r=%2 id=%3/%4 dep=%5 ofp=%6 extra=%7 sdh=%8 cip=%9 l0=%10 l1=%11 wp=%12 wbp=%13 tiles=%14 wpp=%15 lfa=%16 dbk=%17 lm=%18 cqo=%19 ext=%20")
        .arg(int(pps.valid)).arg(pps.invalidReason).arg(pps.ppsId).arg(pps.spsId)
        .arg(int(pps.dependentSliceSegments)).arg(int(pps.outputFlagPresent)).arg(pps.numExtraSliceHeaderBits)
        .arg(int(pps.signDataHiding)).arg(int(pps.cabacInitPresent)).arg(pps.numRefIdxL0DefaultMinus1)
        .arg(pps.numRefIdxL1DefaultMinus1).arg(int(pps.weightedPred)).arg(int(pps.weightedBipred))
        .arg(int(pps.tilesEnabled)).arg(int(pps.entropyCodingSync)).arg(int(pps.ppsLoopFilterAcrossSlices))
        .arg(int(pps.deblockingControlPresent)).arg(int(pps.listsModificationPresent))
        .arg(int(pps.sliceChromaQpOffsetsPresent)).arg(int(pps.sliceHeaderExtension)).toLatin1());
    rep.at("patchHevcPpsId").add(patchHevcPpsId(parser.getPPS(0), 5));

    QVector<int> extraBits(64, pps.numExtraSliceHeaderBits);
    THevcSliceRewriteCtx ctx;
    if (srcSps) {
        ctx.encSps = sps; ctx.encPps = pps; ctx.encHeadersParsed = true;
        ctx.srcPocBits = srcSps->log2MaxPocLsb; ctx.craPoc = outCraPoc ? *outCraPoc : 8;
        ctx.numRasl = 2; ctx.pocBase = 0; ctx.encPpsId = 1;
        if (outRetain) ctx.retainPocs = *outRetain;
    }
    for (int i = 0; i < parser.accessUnitCount(); ++i) {
        const QByteArray au = parser.readAccessUnitData(i);
        rep.at("parseH265SliceTypeFromPacket").addInt(TTNaluParser::parseH265SliceTypeFromPacket(
            reinterpret_cast<const uint8_t*>(au.constData()), au.size()));
        int sc = 0, type = 0;
        for (int p = 0; (p = ttHevcNextNal(au, p, &sc, &type)) >= 0; p += sc + 1) {
            if (type > 21) continue;
            const THevcSliceHeader h = parseHevcSliceHeader(au.mid(p), sps, pps);
            rep.at("parseHevcSliceHeader").add(h.ok ? QByteArray("ok") : h.error.toLatin1());
            if (h.ok)
                rep.at("buildHevcSliceHeader").add(buildHevcSliceHeader(h, sps, pps, qMin(sps.log2MaxPocLsb + 2, 16), 5));
            break;                                   // first slice of the AU only
        }
        if (!srcSps) {
            int craPoc = -1; QVector<int> retain; QString why;
            const bool ok = parseHevcCraRpsInfo(au, sps.log2MaxPocLsb, extraBits, &craPoc, &retain, &why);
            QByteArray t = QByteArray::number(ok) + " " + QByteArray::number(craPoc) + " " + why.toLatin1();
            for (int v : retain) t += " " + QByteArray::number(v);
            rep.at("parseHevcCraRpsInfo").add(t);
            if (ok && outCraPoc && *outCraPoc < 0) { *outCraPoc = craPoc; if (outRetain) *outRetain = retain; }
        } else {
            QString why;
            const QByteArray out = rewriteHevcEncoderPacket(au, ctx, i, &why);
            rep.at("rewriteHevcEncoderPacket").add(out.isEmpty() ? why.toLatin1() : out);
        }
    }
    rep.print(label);
}

// The hand-built SPS as small elementary streams (SPS, PPS, an IDR and a
// P picture) through TTNaluParser: its SPS walk (scaling matrices,
// poc_type 1, field coding) is only reached through parseFile().
static void runParserOnHandBuiltStreams(const QString& workDir)
{
    const QByteArray sc4("\x00\x00\x00\x01", 4);
    const TTH264PpsInfo plain = { true, false, true, false, false, 0, 0, 0, true };
    for (const SpsSpec& sp : handSpsSpecs()) {
        const bool poc0 = sp.pocType == 0;
        const SliceSpec idr = { "idr", 5, 3, 2, sp.frameMbsOnly, false, false, plain, false, 0, 0,
                                {}, {}, {}, 24 };
        const SliceSpec p   = { "p",   1, 2, 0, sp.frameMbsOnly, false, false, plain, false, 0, 0,
                                {}, {}, { 1, 0, 0 }, 24 };
        QByteArray es = buildSps(sp) + buildPps(plain);
        es += sc4 + buildSlice(idr, 8, poc0);
        es += sc4 + buildSlice(p, 8, poc0);
        es += sc4 + buildSlice(p, 8, poc0);
        const QString path = workDir + "/hand_" + QString::fromLatin1(sp.name) + ".264";
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(es) != es.size()) {
            printf("%s write FAILED\n", sp.name);
            continue;
        }
        f.close();
        runH264File(QString("es_%1").arg(QString::fromLatin1(sp.name)), path);
    }
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <fixture-cache-dir> <vector-dir> <work-dir>\n", argv[0]);
        return 2;
    }
    const QString cache = QString::fromLocal8Bit(argv[1]);
    const QString vec = QString::fromLocal8Bit(argv[2]);
    const QString work = QString::fromLocal8Bit(argv[3]);
    if (!QDir().mkpath(work)) {
        fprintf(stderr, "cannot create %s\n", argv[3]);
        return 2;
    }

    runHandBuiltSps();
    runHandBuiltSlices();
    for (const char* f : { "tux_h264_1080p_progressive_test", "tux_h264_1080i_mbaff_test",
                           "tux_h264_1080i_paff_test", "tux_h264_1080p_progressive_duplicate",
                           "tux_h264_1080i_mbaff_duplicate" })
        runH264File(QString::fromLatin1(f), cache + "/" + f + ".264");
    for (const char* f : { "cqm", "yuv444", "hrd", "weightp", "mbaff" })
        runH264File(QString("vec_%1").arg(f), vec + "/" + f + ".264");
    runParserOnHandBuiltStreams(work);

    THevcSpsSeamInfo srcSps;
    int craPoc = -1;
    QVector<int> retain;
    runH265File("tux_hevc4k_cra_test", cache + "/tux_hevc4k_cra_test.265", nullptr, &srcSps, &craPoc, &retain);
    runH265File("vec_x265enc", vec + "/x265enc.265", &srcSps, nullptr, &craPoc, &retain);
    runH265File("vec_x265misc", vec + "/x265misc.265", &srcSps, nullptr, &craPoc, &retain);
    return 0;
}
