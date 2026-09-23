/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTH264BITSTREAM_H
#define TTH264BITSTREAM_H

#include <QByteArray>
#include <cstdint>

// H.264 bitstream surgery for the smart cut: SPS/PPS parsing, frame_num and
// POC patches, MMCO neutralization, the SPS unification slice rewrite and
// the reorder patch. Bit access goes through avstream/ttbitstream; a slice
// header that cannot be read to its end is left unchanged (neutralize) or
// rejected (rewrite). See docs/code-map/smart-cut.md.

struct TTH264SpsInfo {
    int log2MaxFrameNumMinus4;   // -1 on error
    int pocType;                 // pic_order_cnt_type (0, 1, or 2)
    int log2MaxPocLsbMinus4;     // only valid if pocType == 0, -1 otherwise
    bool frameMbsOnly;           // frame_mbs_only_flag
    int picWidth;                // luma width in samples, uncropped (-1 on error)
    int picHeight;               // luma height in samples, uncropped (-1 on error)
    int bitDepthLuma;            // 8 unless the high-profile branch says otherwise
};
TTH264SpsInfo ttParseH264SpsInfo(const QByteArray& spsNal);
int ttReadFrameNumFromAU(const QByteArray& auData, int frameNumBitWidth);
int ttReadPocLsbFromAU(const QByteArray& auData, int frameNumBitWidth,
                             int pocLsbBitWidth, bool frameMbsOnly);
bool ttFindH264SpsInPacket(const QByteArray& packetData, TTH264SpsInfo& spsInfo);
QByteArray ttPatchSpsNalsInAccessUnit(const QByteArray& auData, int maxReorderFrames, bool isPAFF);

// IDR injection and SPS unification helpers
struct TTH264PpsInfo {
    bool entropyCodingModeFlag;            // 0=CAVLC, 1=CABAC
    bool bottomFieldPicOrderPresent;       // bottom_field_pic_order_in_frame_present_flag
    bool deblockingFilterControlPresent;   // deblocking_filter_control_present_flag
    bool redundantPicCntPresent;           // redundant_pic_cnt_present_flag
    bool weightedPredFlag;                 // weighted_pred_flag (P-slices)
    int  weightedBipredIdc;                // weighted_bipred_idc (B-slices)
    int  numRefIdxL0DefaultActiveMinus1;   // for pred_weight_table parsing
    int  numRefIdxL1DefaultActiveMinus1;   // for B-slice pred_weight_table parsing
    bool valid;                            // true if parsing succeeded
};
TTH264PpsInfo ttParseH264PpsInfo(const QByteArray& ppsNal);

// SPS unification helpers (for PAFF seamless re-encode→stream-copy transition)
QByteArray ttRewriteEncoderSliceForSourceSps(
    const QByteArray& nalBody,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const TTH264PpsInfo& encPps, uint32_t newPpsId, int frameIndex,
    int pocLsbBase);
// *failedSlices (optional) counts slice NALs that could not be rewritten and
// were kept as the encoder wrote them.
QByteArray ttRewriteEncoderPacketForSourceSps(
    const QByteArray& packetData,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const TTH264PpsInfo& encPps, uint32_t newPpsId, int frameIndex,
    int pocLsbBase, int* failedSlices = nullptr);
QByteArray ttExtractPpsFromPacket(const QByteArray& packetData);
QByteArray ttPatchH264PpsId(const QByteArray& ppsNal, uint32_t newPpsId);

// MMCO neutralization for stream-copy AUs after EOS
QByteArray ttNeutralizeMmcoInAU(const QByteArray& auData,
    int log2MaxFrameNum, int pocLsbBitWidth, bool frameMbsOnly,
    const TTH264PpsInfo& pps);
QByteArray ttPatchFrameNumInAU(const QByteArray& auData, int frameNumBitWidth,
    int frameNumDelta, int maxFrameNum);

// Patch poc_lsb in the last slice NAL of a packet (raw encoder output).
// Returns patched data, or original if no slice found or patch not needed.
QByteArray ttPatchPocLsbInPacket(const QByteArray& packetData,
                               int frameNumBitWidth, int pocLsbBitWidth,
                               bool frameMbsOnly, uint32_t newPocLsb);

// Patch H.264 SPS NAL to set bitstream_restriction with max_num_reorder_frames.
// Input and output WITH start code prefix; empty on error.
QByteArray ttPatchH264SpsReorderFrames(const QByteArray& spsNal, int maxReorderFrames, bool isPAFF);

#endif // TTH264BITSTREAM_H
