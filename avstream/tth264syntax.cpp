/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "tth264syntax.h"

// Returns true for H.264 profile_idc values whose SPS carries the high-profile
// extension fields (chroma_format_idc, bit_depth_*_minus8, scaling lists).
// Per ITU-T H.264 (08/2021) §7.3.2.1.1: 100 (High), 110 (High10),
// 122 (High422), 244 (High444Predictive), 44 (CAVLC444), 83 (Scalable Baseline),
// 86 (Scalable High), 118 (Multiview High), 128 (Stereo High),
// 134 (MFC High), 135 (MFC Depth High), 138 (Multiview Depth High),
// 139 (Enhanced Multiview Depth High).
bool ttIsH264HighProfile(uint32_t profileIdc)
{
    switch (profileIdc) {
        case 100: case 110: case 122:
        case 244: case 44:  case 83:
        case 86:  case 118: case 128:
        case 134: case 135: case 138: case 139:
            return true;
        default:
            return false;
    }
}

void ttSkipH264ScalingMatrix(TTBitReader& r, uint32_t chromaFormatIdc)
{
    if (!r.flag()) return;                       // seq_scaling_matrix_present_flag
    const int lists = (chromaFormatIdc != 3) ? 8 : 12;
    for (int i = 0; i < lists; ++i) {
        if (!r.flag()) continue;                 // seq_scaling_list_present_flag[i]
        const int size = (i < 6) ? 16 : 64;
        int lastScale = 8, nextScale = 8;
        for (int j = 0; j < size; ++j) {
            if (nextScale != 0)
                nextScale = (lastScale + r.se() + 256) % 256;
            lastScale = (nextScale == 0) ? lastScale : nextScale;
        }
    }
}

bool ttParseH264SpsHeader(TTBitReader& r, TTH264SpsHeader& out)
{
    // After every group of fields: a read past the end (or a rejected
    // Exp-Golomb code) means the group was not in the data. Its fields go
    // back to their "unknown" defaults and the walk ends - the reader's
    // zero substitutes must never reach a caller as real values.
    auto truncatedHere = [&r, &out]() {
        if (!r.error()) return false;
        out.truncated = true;
        return true;
    };

    r.bits(8);                                   // NAL header
    out.profileIdc = r.bits(8);
    r.bits(8);                                   // constraint_set flags + reserved
    r.bits(8);                                   // level_idc
    out.spsId = r.ue();
    if (truncatedHere()) { out.profileIdc = 0; out.spsId = 0; return false; }

    if (ttIsH264HighProfile(out.profileIdc)) {
        out.chromaFormatIdc = r.ue();
        if (out.chromaFormatIdc == 3) r.bits(1); // separate_colour_plane_flag
        out.bitDepthLuma = 8 + int(r.ue());      // bit_depth_luma_minus8
        r.ue();                                  // bit_depth_chroma_minus8
        r.bits(1);                               // qpprime_y_zero_transform_bypass_flag
        ttSkipH264ScalingMatrix(r, out.chromaFormatIdc);
        if (truncatedHere()) { out.chromaFormatIdc = 1; out.bitDepthLuma = 8; return false; }
    }

    out.log2MaxFrameNumMinus4 = int(r.ue());
    if (truncatedHere()) { out.log2MaxFrameNumMinus4 = -1; return false; }

    out.pocType = int(r.ue());
    if (out.pocType == 0) {
        out.log2MaxPocLsbMinus4 = int(r.ue());
    } else if (out.pocType == 1) {
        r.bits(1);                               // delta_pic_order_always_zero_flag
        r.se();                                  // offset_for_non_ref_pic
        r.se();                                  // offset_for_top_to_bottom_field
        const uint32_t n = r.ue();               // num_ref_frames_in_pic_order_cnt_cycle
        if (!r.error() && n > 256) { out.pocCycleTooLong = true; return false; }
        for (uint32_t i = 0; i < n && !r.error(); ++i)
            r.se();                              // offset_for_ref_frame
    }
    if (truncatedHere()) { out.pocType = -1; out.log2MaxPocLsbMinus4 = -1; return false; }

    out.maxNumRefFramesBitPos = r.pos();
    out.maxNumRefFrames = r.ue();
    r.bits(1);                                   // gaps_in_frame_num_value_allowed_flag
    out.picWidthInMbsMinus1 = r.ue();
    out.picHeightInMapUnitsMinus1 = r.ue();
    if (truncatedHere()) {
        out.maxNumRefFramesBitPos = -1; out.maxNumRefFrames = 0;
        out.picWidthInMbsMinus1 = 0; out.picHeightInMapUnitsMinus1 = 0;
        return false;
    }

    out.frameMbsOnly = r.bits(1) != 0;
    if (truncatedHere()) { out.frameMbsOnly = true; return false; }
    return true;
}
