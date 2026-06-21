// Unit test for TTLeadingPicClassifier (HEVC NoRaslOutputFlag rule).
#include <cstdio>
#include <vector>
#include <cstdint>
extern "C" { void av_log_set_level(int); }
#include "../../avstream/ttdisplayordermap.h"
#include "../../avstream/ttnaluparser.h"   // H265:: NAL type constants

// AV_CODEC_ID_HEVC=173, AV_CODEC_ID_H264=27 (avoid pulling avcodec.h here).
static const int CODEC_HEVC = 173;
static const int CODEC_H264 = 27;

// Build a minimal Annex-B packet with one VCL NAL of the given HEVC type.
static std::vector<uint8_t> hevcPkt(int nalType) {
    return {0,0,1, (uint8_t)((nalType<<1)&0x7E), 0x01, 0xAA, 0xBB};
}
// Standalone EOS NAL (no VCL slice).
static std::vector<uint8_t> hevcEos() {
    return {0,0,1, (uint8_t)((H265::NAL_EOS<<1)&0x7E), 0x01};
}

static int failures = 0;
static void expect(const char* name, bool got, bool want) {
    if (got != want) { failures++; printf("FAIL  %s (got=%d want=%d)\n", name, got, want); }
    else             printf("PASS  %s\n", name);
}

int main() {
    av_log_set_level(0);
    using namespace H265;
    // First CRA -> arms dropping; its RASL pics are dropped; first TRAIL ends it.
    {
        TTLeadingPicClassifier c(CODEC_HEVC);
        expect("cra0",        c.classifyPacket(hevcPkt(NAL_CRA_NUT).data(), 7), false);
        expect("rasl_r_drop", c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), true);
        expect("rasl_n_drop", c.classifyPacket(hevcPkt(NAL_RASL_N).data(), 7), true);
        expect("trail_ends",  c.classifyPacket(hevcPkt(NAL_TRAIL_R).data(), 7), false);
        expect("rasl_after_trail_not_dropped",
                              c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), false);
    }
    // Mid-stream CRA (not first, no EOS) -> NoRaslOutputFlag=0, RASL displayed.
    {
        TTLeadingPicClassifier c(CODEC_HEVC);
        c.classifyPacket(hevcPkt(NAL_IDR_W_RADL).data(), 7);   // first IRAP (IDR)
        c.classifyPacket(hevcPkt(NAL_TRAIL_R).data(), 7);
        expect("mid_cra",        c.classifyPacket(hevcPkt(NAL_CRA_NUT).data(), 7), false);
        expect("mid_cra_rasl_kept",
                                 c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), false);
    }
    // CRA after EOS -> NoRaslOutputFlag=1, RASL dropped again.
    {
        TTLeadingPicClassifier c(CODEC_HEVC);
        c.classifyPacket(hevcPkt(NAL_IDR_W_RADL).data(), 7);
        c.classifyPacket(hevcPkt(NAL_TRAIL_R).data(), 7);
        c.classifyPacket(hevcEos().data(), 5);
        expect("cra_after_eos",  c.classifyPacket(hevcPkt(NAL_CRA_NUT).data(), 7), false);
        expect("rasl_after_eos_dropped",
                                 c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), true);
    }
    // RADL is never dropped (decodable leading).
    {
        TTLeadingPicClassifier c(CODEC_HEVC);
        c.classifyPacket(hevcPkt(NAL_CRA_NUT).data(), 7);
        expect("radl_not_dropped",
                                 c.classifyPacket(hevcPkt(NAL_RADL_R).data(), 7), false);
        // RADL must NOT close the leading sequence: a following RASL is still dropped.
        expect("rasl_after_radl_dropped",
                                 c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), true);
    }
    // BLA always arms dropping even if not first / no EOS.
    {
        TTLeadingPicClassifier c(CODEC_HEVC);
        c.classifyPacket(hevcPkt(NAL_IDR_W_RADL).data(), 7);
        c.classifyPacket(hevcPkt(NAL_TRAIL_R).data(), 7);
        expect("bla",            c.classifyPacket(hevcPkt(NAL_BLA_W_LP).data(), 7), false);
        expect("bla_rasl_dropped",
                                 c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), true);
    }
    // Non-HEVC: always false.
    {
        TTLeadingPicClassifier c(CODEC_H264);
        expect("h264_never_drops", c.classifyPacket(hevcPkt(NAL_RASL_R).data(), 7), false);
    }
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n"); return 0;
}
