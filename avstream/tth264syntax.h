/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTH264SYNTAX_H
#define TTH264SYNTAX_H

#include "ttbitstream.h"

#include <cstdint>

// Fields of the H.264 SPS header (7.3.2.1.1) up to frame_mbs_only_flag, as
// far as TTNaluParser and the smart cut use them.
struct TTH264SpsHeader {
    uint32_t profileIdc = 0, spsId = 0, chromaFormatIdc = 1;
    int  bitDepthLuma = 8;
    int  log2MaxFrameNumMinus4 = -1, pocType = -1, log2MaxPocLsbMinus4 = -1;
    int  maxNumRefFramesBitPos = -1;   // bit position of max_num_ref_frames
    uint32_t maxNumRefFrames = 0;
    uint32_t picWidthInMbsMinus1 = 0, picHeightInMapUnitsMinus1 = 0;
    bool frameMbsOnly = true;
    bool pocCycleTooLong = false;      // num_ref_frames_in_pic_order_cnt_cycle > 256
    bool truncated = false;            // the SPS ended (or held a rejected code) mid-header
};

// True for H.264 profile_idc values whose SPS carries the high-profile
// extension fields (chroma_format_idc, bit_depth_*_minus8, scaling lists).
bool ttIsH264HighProfile(uint32_t profileIdc);

// H.264 SPS header walk, shared by TTNaluParser and the smart cut. r stands
// on the NAL header byte of an RBSP (emulation prevention removed) and is
// left right after frame_mbs_only_flag. Returns false when
// num_ref_frames_in_pic_order_cnt_cycle exceeds 256 (the spec allows 255;
// pocCycleTooLong set) or when the data ends inside the header (truncated
// set): the walk stops there, fields read so far are valid, the rest keep
// their "unknown" defaults (log2MaxFrameNumMinus4 = -1 when even that is
// missing). Callers keep their own early-stop behaviour on false.
bool ttParseH264SpsHeader(TTBitReader& r, TTH264SpsHeader& out);

// seq_scaling_matrix_present_flag and, when set, the scaling lists.
void ttSkipH264ScalingMatrix(TTBitReader& r, uint32_t chromaFormatIdc);

#endif // TTH264SYNTAX_H
