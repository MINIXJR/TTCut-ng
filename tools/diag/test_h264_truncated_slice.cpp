// Termination gate for truncated H.264 slice headers (spec
// docs/superpowers/specs/2026-09-23-bitstream-unification-design.md).
// A slice header that ends inside the MMCO or the ref_pic_list_modification
// loop must not hang ttNeutralizeMmcoInAU (source AUs after the seam, i.e.
// possibly damaged DVB data) or ttRewriteEncoderSliceForSourceSps. Each call
// runs in its own thread; more than 10 s = FAIL (the process then exits
// without joining the stuck thread).
//   test_h264_truncated_slice [case]   exit 0 = PASS, 1 = FAIL
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <utility>
#include <vector>
#include <QByteArray>

#include "avstream/ttbitstream.h"
#include "extern/tth264bitstream.h"

static const TTH264PpsInfo kPps = { false, false, true, false, false, 0, 0, 0, true };  // CAVLC

// P slice, nal_ref_idc 2, frame_num 4 bits, poc_lsb 6 bits, frame_mbs_only.
// `tail` writes the fields after frame_num/poc and ends the RBSP right
// there - no stop bit, no slice data.
static QByteArray slice(const std::function<void(TTBitWriter&)>& tail)
{
    TTBitWriter w;
    w.bits(0x41, 8);          // nal_ref_idc 2, type 1
    w.ue(0);                  // first_mb_in_slice
    w.ue(5);                  // slice_type P (all slices)
    w.ue(0);                  // pic_parameter_set_id
    w.bits(3, 4);             // frame_num
    w.bits(10, 6);            // pic_order_cnt_lsb
    tail(w);
    return ttNalFromRbsp(w.data());
}

static bool finishes(const char* what, const std::function<void()>& call)
{
    auto fut = std::async(std::launch::async, call);
    const bool done = fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    printf("%s %s\n", done ? "PASS" : "FAIL (timeout)", what);
    if (!done) { fflush(stdout); std::_Exit(1); }
    return done;
}

int main(int argc, char** argv)
{
    const QByteArray sc("\x00\x00\x00\x01", 4);
    // Cut inside the MMCO loop: flag 1, op 1 + value, then nothing.
    const QByteArray mmcoCut = slice([](TTBitWriter& w) {
        w.bits(0, 1);                       // num_ref_idx_active_override_flag
        w.bits(0, 1);                       // ref_pic_list_modification_flag_l0
        w.bits(1, 1);                       // adaptive_ref_pic_marking_mode_flag
        w.ue(1); w.ue(0);                   // MMCO 1, difference_of_pic_nums_minus1
    });
    // Cut inside the RPLM loop: flag 1, idc 0 + value, then nothing.
    const QByteArray rplmCut = slice([](TTBitWriter& w) {
        w.bits(0, 1);
        w.bits(1, 1);                       // ref_pic_list_modification_flag_l0
        w.ue(0); w.ue(0);                   // idc 0, abs_diff_pic_num_minus1
    });
    // Cut right after the override flag asked for an explicit count.
    const QByteArray overrideCut = slice([](TTBitWriter& w) { w.bits(1, 1); });
    // Damage inside the RPLM list that is NOT at the end of the data: 40 zero
    // bits (> 31 leading zeros) make ue() fail mid-buffer, then valid-looking
    // bytes follow. The reader's error flag is set while atEnd() is false.
    // Complete MMCO list, then the header ends inside slice_qp_delta: the
    // loops terminate normally, the damage is in the fields after them.
    const QByteArray qpCut = slice([](TTBitWriter& w) {
        w.bits(0, 1);                       // num_ref_idx_active_override_flag
        w.bits(0, 1);                       // ref_pic_list_modification_flag_l0
        w.bits(1, 1);                       // adaptive_ref_pic_marking_mode_flag
        w.ue(1); w.ue(0); w.ue(0);          // MMCO 1, its value, end of list
        w.bits(0, 6);                       // slice_qp_delta: leading zeros only
    });
    const QByteArray rplmZeroRun = slice([](TTBitWriter& w) {
        w.bits(0, 1);                       // num_ref_idx_active_override_flag
        w.bits(1, 1);                       // ref_pic_list_modification_flag_l0
        w.bits(0, 32); w.bits(0, 8);        // 40 zero bits
        for (int i = 0; i < 8; ++i) w.bits(0xA5, 8);
    });

    const std::vector<std::pair<const char*, std::function<void()>>> cases = {
        { "ttNeutralizeMmcoInAU, cut inside MMCO",
          [&] { ttNeutralizeMmcoInAU(sc + mmcoCut, 4, 6, true, kPps); } },
        { "ttNeutralizeMmcoInAU, cut inside RPLM",
          [&] { ttNeutralizeMmcoInAU(sc + rplmCut, 4, 6, true, kPps); } },
        { "ttNeutralizeMmcoInAU, cut after override flag",
          [&] { ttNeutralizeMmcoInAU(sc + overrideCut, 4, 6, true, kPps); } },
        { "ttRewriteEncoderSliceForSourceSps, cut inside MMCO",
          [&] { ttRewriteEncoderSliceForSourceSps(mmcoCut, 4, 6, true, 6, 8, true, kPps, 1, 3, 0); } },
        { "ttRewriteEncoderSliceForSourceSps, cut inside RPLM",
          [&] { ttRewriteEncoderSliceForSourceSps(rplmCut, 4, 6, true, 6, 8, true, kPps, 1, 3, 0); } },
        { "ttNeutralizeMmcoInAU, zero run inside RPLM (error before the end)",
          [&] { ttNeutralizeMmcoInAU(sc + rplmZeroRun, 4, 6, true, kPps); } },
        { "ttRewriteEncoderSliceForSourceSps, zero run inside RPLM (error before the end)",
          [&] { ttRewriteEncoderSliceForSourceSps(rplmZeroRun, 4, 6, true, 6, 8, true, kPps, 1, 3, 0); } },
    };
    // Verdicts on the results, not only termination: a slice whose header
    // cannot be read to its end must not be rebuilt from made-up values.
    int failures = 0;
    auto verdict = [&failures](bool ok, const char* what) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    // An optional case number runs that case alone (to see which of several
    // hang: the first timeout ends the process).
    const int only = (argc > 1) ? atoi(argv[1]) : -1;
    for (int i = 0; i < int(cases.size()); ++i)
        if (only < 0 || only == i) finishes(cases[i].first, cases[i].second);
    if (only < 0 || only == int(cases.size())) {   // verdicts: case number = cases.size()
        const QByteArray au = sc + qpCut;
        verdict(ttNeutralizeMmcoInAU(au, 4, 6, true, kPps) == au,
                "ttNeutralizeMmcoInAU leaves a slice cut inside slice_qp_delta unchanged");
        verdict(ttRewriteEncoderSliceForSourceSps(qpCut, 4, 6, true, 6, 8, true, kPps, 1, 3, 0).isEmpty(),
                "ttRewriteEncoderSliceForSourceSps rejects a slice cut inside slice_qp_delta");
    }
    if (failures) { printf("TRUNCATED-SLICE FAIL\n"); return 1; }
    printf("TRUNCATED-SLICE PASS\n");
    return 0;
}
