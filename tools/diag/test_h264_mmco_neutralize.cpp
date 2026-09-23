// Gate for ttNeutralizeMmcoInAU (extern/tth264bitstream.cpp): after removing
// the MMCO commands, the slice data must follow the rewritten header exactly
// as the entropy coder requires - bit-contiguous for CAVLC, after
// cabac_alignment_one_bits for CABAC. A P slice with MMCO is built for both;
// the expected RBSP is the same header with adaptive_ref_pic_marking_mode_flag
// 0 and the original slice data behind it.
//   test_h264_mmco_neutralize      exit 0 = PASS, 1 = FAIL
#include <cstdio>
#include <QByteArray>

#include "avstream/ttbitstream.h"
#include "extern/tth264bitstream.h"

static constexpr int kFnBits = 4;
static constexpr int kPocBits = 6;

// Header of a P slice (nal_ref_idc 2), frame_mbs_only, poc_type 0, PPS with
// deblocking_filter_control_present and no weighted prediction.
static void writeHeader(TTBitWriter& w, bool cabac, bool withMmco)
{
    w.bits(0x41, 8);          // nal_ref_idc 2, type 1
    w.ue(0);                  // first_mb_in_slice
    w.ue(5);                  // slice_type P
    w.ue(0);                  // pic_parameter_set_id
    w.bits(3, kFnBits);       // frame_num
    w.bits(10, kPocBits);     // pic_order_cnt_lsb
    w.bits(0, 1);             // num_ref_idx_active_override_flag
    w.bits(0, 1);             // ref_pic_list_modification_flag_l0
    w.bits(withMmco, 1);      // adaptive_ref_pic_marking_mode_flag
    if (withMmco) { w.ue(1); w.ue(2); w.ue(0); }   // MMCO 1 (diff 2), end
    if (cabac) w.ue(1);       // cabac_init_idc
    w.se(-3);                 // slice_qp_delta
    w.ue(0); w.se(1); w.se(-1);  // deblocking: disable_idc 0, alpha, beta
}

// Slice data: CAVLC continues right after the header; CABAC starts at the
// next byte boundary after cabac_alignment_one_bits.
static void writeData(TTBitWriter& w, bool cabac)
{
    if (cabac) {
        w.alignWith(1);
        for (int i = 0; i < 12; ++i) w.bits(uint32_t((i * 29 + 5) & 0xFF), 8);
    } else {
        for (int i = 0; i < 12; ++i) w.bits(uint32_t((i * 53 + 7) & 0xFF), 8);
        w.bits(0x5, 3);
        w.rbspTrailingBits();
    }
}

int main()
{
    int failures = 0;
    for (bool cabac : { false, true }) {
        const TTH264PpsInfo pps = { cabac, false, true, false, false, 0, 0, 0, true };
        TTBitWriter in;  writeHeader(in, cabac, true);  writeData(in, cabac);
        TTBitWriter exp; writeHeader(exp, cabac, false); writeData(exp, cabac);

        const QByteArray au = QByteArray("\x00\x00\x00\x01", 4) + ttNalFromRbsp(in.data());
        const QByteArray out = ttNeutralizeMmcoInAU(au, kFnBits, kPocBits, true, pps);
        const QByteArray outRbsp = ttRbspFromNal(out.mid(4));
        const bool pass = outRbsp == exp.data();
        printf("%s %s: MMCO removed, slice data %s\n", pass ? "PASS" : "FAIL",
               cabac ? "CABAC" : "CAVLC", pass ? "intact" : "differs");
        if (!pass)
            printf("  expected %s\n  got      %s\n", exp.data().toHex(' ').constData(),
                   outRbsp.toHex(' ').constData());
        if (!pass) ++failures;
    }
    printf("%s\n", failures ? "MMCO-NEUTRALIZE FAIL" : "MMCO-NEUTRALIZE PASS");
    return failures ? 1 : 0;
}
