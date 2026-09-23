// EPB gate for TTNaluParser::parseH264SpsBasics (spec
// docs/superpowers/specs/2026-09-23-bitstream-unification-design.md).
//   test_sps_basics_epb                 synthetic SPS whose header carries
//                                       emulation-prevention bytes before
//                                       frame_mbs_only_flag, and truncated
//                                       SPS; exit 0 = PASS
//   test_sps_basics_epb scan <file>...  measurement: per distinct SPS in the
//                                       first 64 MiB of each file, whether it
//                                       holds EPB and whether reading it raw
//                                       and de-escaped gives the same result
#include <cstdio>
#include <QByteArray>
#include <QFile>
#include <QSet>

#include "avstream/ttbitstream.h"
#include "avstream/ttnaluparser.h"

// SPS (main profile, poc_type 1) with offsets large enough to force EPB
// into the header. Returns the NAL with a 4-byte start code.
static QByteArray buildSps(bool frameMbsOnly)
{
    TTBitWriter w;
    w.bits(0x67, 8);                 // NAL header: nal_ref_idc 3, type 7
    w.bits(77, 8);                   // profile_idc (main: no high-profile block)
    w.bits(0, 8);                    // constraint flags
    w.bits(40, 8);                   // level_idc
    w.ue(0);                         // seq_parameter_set_id
    w.ue(5);                         // log2_max_frame_num_minus4 -> 9
    w.ue(1);                         // pic_order_cnt_type
    w.bits(0, 1);                    // delta_pic_order_always_zero_flag
    w.se(-(1 << 30));                // offset_for_non_ref_pic
    w.se(0);                         // offset_for_top_to_bottom_field
    w.ue(1);                         // num_ref_frames_in_pic_order_cnt_cycle
    w.se(1 << 30);                   // offset_for_ref_frame[0]
    w.ue(4);                         // max_num_ref_frames
    w.bits(0, 1);                    // gaps_in_frame_num_value_allowed_flag
    w.ue(119);                       // pic_width_in_mbs_minus1
    w.ue(frameMbsOnly ? 67 : 33);    // pic_height_in_map_units_minus1
    w.bits(frameMbsOnly ? 1 : 0, 1); // frame_mbs_only_flag
    if (!frameMbsOnly) w.bits(1, 1); // mb_adaptive_frame_field_flag
    w.bits(1, 1);                    // direct_8x8_inference_flag
    w.bits(0, 1);                    // frame_cropping_flag
    w.bits(0, 1);                    // vui_parameters_present_flag
    w.rbspTrailingBits();
    return QByteArray("\x00\x00\x00\x01", 4) + ttNalFromRbsp(w.data());
}

static int runSynthetic()
{
    int failures = 0;
    for (bool fmo : { true, false }) {
        const QByteArray sps = buildSps(fmo);
        const bool hasEpb = ttRbspFromNal(sps.mid(4)).size() < sps.size() - 4;
        TTNaluParser::H264SpsBasics out;
        const bool ok = TTNaluParser::parseH264SpsBasics(
            reinterpret_cast<const uint8_t*>(sps.constData()), sps.size(), out);
        const bool pass = hasEpb && ok && out.log2MaxFrameNum == 9
                          && out.haveFrameMbsOnlyFlag && out.frameMbsOnlyFlag == fmo;
        printf("%s frame_mbs_only=%d: epb=%d ok=%d log2MaxFrameNum=%d have=%d frameMbsOnly=%d\n",
               pass ? "PASS" : "FAIL", fmo, hasEpb, ok, out.log2MaxFrameNum,
               out.haveFrameMbsOnlyFlag, out.frameMbsOnlyFlag);
        if (!pass) ++failures;
    }
    // Truncated SPS: the reader substitutes zeros past the end, so a field
    // that was never in the data must not be reported as known.
    //   cut before frame_mbs_only_flag -> log2MaxFrameNum known, flag unknown
    //   cut before log2_max_frame_num  -> nothing usable, return false
    auto truncatedSps = [](bool keepFrameNum) {
        TTBitWriter w;
        w.bits(0x67, 8); w.bits(77, 8); w.bits(0, 8); w.bits(40, 8); w.ue(0);
        if (keepFrameNum) {
            w.ue(5);                 // log2_max_frame_num_minus4 -> 9
            w.ue(0);                 // pic_order_cnt_type
            w.ue(2);                 // log2_max_pic_order_cnt_lsb_minus4
            w.ue(4);                 // max_num_ref_frames
        }
        return QByteArray("\x00\x00\x00\x01", 4) + ttNalFromRbsp(w.data());
    };
    {
        const QByteArray sps = truncatedSps(true);
        TTNaluParser::H264SpsBasics out;
        const bool ok = TTNaluParser::parseH264SpsBasics(
            reinterpret_cast<const uint8_t*>(sps.constData()), sps.size(), out);
        const bool pass = ok && out.log2MaxFrameNum == 9 && !out.haveFrameMbsOnlyFlag;
        printf("%s truncated before frame_mbs_only_flag: ok=%d log2MaxFrameNum=%d have=%d\n",
               pass ? "PASS" : "FAIL", ok, out.log2MaxFrameNum, out.haveFrameMbsOnlyFlag);
        if (!pass) ++failures;
    }
    {
        const QByteArray sps = truncatedSps(false);
        TTNaluParser::H264SpsBasics out;
        const bool ok = TTNaluParser::parseH264SpsBasics(
            reinterpret_cast<const uint8_t*>(sps.constData()), sps.size(), out);
        printf("%s truncated before log2_max_frame_num: ok=%d log2MaxFrameNum=%d have=%d\n",
               !ok ? "PASS" : "FAIL", ok, out.log2MaxFrameNum, out.haveFrameMbsOnlyFlag);
        if (ok) ++failures;
    }
    printf("%s\n", failures ? "SPS-BASICS-EPB FAIL" : "SPS-BASICS-EPB PASS");
    return failures ? 1 : 0;
}

static int runScan(int argc, char** argv)
{
    for (int a = 2; a < argc; ++a) {
        QFile f(QString::fromLocal8Bit(argv[a]));
        if (!f.open(QIODevice::ReadOnly)) { printf("%s: cannot open\n", argv[a]); continue; }
        const QByteArray buf = f.read(64LL * 1024 * 1024);
        const uint8_t* d = reinterpret_cast<const uint8_t*>(buf.constData());
        QSet<QByteArray> seen;
        int total = 0, withEpb = 0, mismatch = 0;
        for (int s = TTNaluParser::findStartCodePayload(d, buf.size(), 0); s >= 0;
             s = TTNaluParser::findStartCodePayload(d, buf.size(), s)) {
            if ((d[s] & 0x1F) != 7) continue;
            const int next = TTNaluParser::findStartCodePayload(d, buf.size(), s);
            const int end = next >= 0 ? next - 3 : buf.size();
            const QByteArray nal = buf.mid(s, end - s);
            if (seen.contains(nal)) continue;
            seen.insert(nal);
            ++total;
            const QByteArray rbsp = ttRbspFromNal(nal);
            if (rbsp.size() != nal.size()) ++withEpb;
            const QByteArray raw = QByteArray("\x00\x00\x00\x01", 4) + nal;
            const QByteArray esc = QByteArray("\x00\x00\x00\x01", 4) + rbsp;
            TTNaluParser::H264SpsBasics a, b;
            TTNaluParser::parseH264SpsBasics(reinterpret_cast<const uint8_t*>(raw.constData()), raw.size(), a);
            TTNaluParser::parseH264SpsBasics(reinterpret_cast<const uint8_t*>(esc.constData()), esc.size(), b);
            if (a.log2MaxFrameNum != b.log2MaxFrameNum || a.frameMbsOnlyFlag != b.frameMbsOnlyFlag
                || a.haveFrameMbsOnlyFlag != b.haveFrameMbsOnlyFlag)
                ++mismatch;
        }
        printf("%s: distinct SPS=%d with EPB=%d raw!=de-escaped=%d\n", argv[a], total, withEpb, mismatch);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && QByteArray(argv[1]) == "scan") return runScan(argc, argv);
    return runSynthetic();
}
