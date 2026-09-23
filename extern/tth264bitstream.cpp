/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "tth264bitstream.h"
#include "../avstream/ttannexb.h"
#include "../avstream/ttbitstream.h"
#include "../avstream/tth264syntax.h"
#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"

#include <QDebug>
#include <cstring>

// ----------------------------------------------------------------------------
// Access-unit helpers shared by the bitstream surgery below
// ----------------------------------------------------------------------------

// RBSP of the first slice NAL (type 1 or 5) of an Annex-B access unit;
// empty when the AU holds no slice.
static QByteArray firstSliceRbsp(const QByteArray& auData)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    const int size = auData.size();
    int scLen = 0;
    for (int sc = ttNextStartCode(data, size, 0, &scLen); sc >= 0;
         sc = ttNextStartCode(data, size, sc + scLen, &scLen)) {
        const int nalStart = sc + scLen;
        uint8_t nalType = data[nalStart] & 0x1F;
        if (nalType != 1 && nalType != 5) continue;
        int nalEnd = ttNalEnd(data, size, nalStart + 1);
        return ttRbspFromNal(auData.mid(nalStart, nalEnd - nalStart));
    }
    return QByteArray();
}

// Forward declaration (defined after writeParameterSets)
QByteArray ttPatchH264SpsReorderFrames(const QByteArray& spsNal, int maxReorderFrames, bool isPAFF);

// Patch all H.264 SPS NALs within an access unit's raw data.
// Scans for start codes followed by NAL type 7 (SPS), patches each with
// ttPatchH264SpsReorderFrames(). Returns modified data, or original if no SPS found.
// isPAFF: when true, increases num_ref_frames and max_dec_frame_buffering for
// PAFF→MBAFF DPB transitions (not needed for non-PAFF streams).
QByteArray ttPatchSpsNalsInAccessUnit(const QByteArray& auData, int maxReorderFrames, bool isPAFF)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int size = auData.size();
    QByteArray result;
    result.reserve(size + 64);  // small extra for patched SPS growth

    int pos = 0;
    bool patched = false;

    while (pos < size) {
        // Find next start code
        int scLen = 0;
        const int scStart = ttNextStartCode(data, size, pos, &scLen);

        if (scStart < 0) {
            // No more start codes, copy remainder
            result.append(auData.mid(pos));
            break;
        }

        // Copy data before this start code
        if (scStart > pos)
            result.append(auData.mid(pos, scStart - pos));

        // Find end of this NAL (next start code or end of data)
        int nalStart = scStart + scLen;
        int nalEnd = ttNalEnd(data, size, nalStart + 1);

        // Check NAL type (lower 5 bits of first byte after start code)
        int nalType = (nalStart < size) ? (data[nalStart] & 0x1F) : -1;

        if (nalType == 7) {  // SPS
            // Extract this SPS NAL with its start code, patch it
            QByteArray spsNal = auData.mid(scStart, nalEnd - scStart);
            QByteArray patchedSps = ttPatchH264SpsReorderFrames(spsNal, maxReorderFrames, isPAFF);
            if (!patchedSps.isEmpty()) {
                result.append(patchedSps);
                patched = true;
            } else {
                result.append(spsNal);  // patch failed, keep original
            }
        } else {
            // Not an SPS, copy as-is
            result.append(auData.mid(scStart, nalEnd - scStart));
        }

        pos = nalEnd;
    }

    return patched ? result : auData;
}

static void skipPredWeightList(TTBitReader& r, int numRefs);

// ----------------------------------------------------------------------------
// Parse H.264 SPS fields needed for frame_num patching and POC domain fix.
// Input: raw SPS NAL data WITH start code prefix.
// Returns struct with parsed values; log2MaxFrameNumMinus4 = -1 on error.
// ----------------------------------------------------------------------------
TTH264SpsInfo ttParseH264SpsInfo(const QByteArray& spsNal)
{
    TTH264SpsInfo info = { -1, -1, -1, true, -1, -1, 8 };

    // Find and strip start code
    const int startCodeLen = ttStartCodeLength(spsNal);
    if (startCodeLen == 0)
        return info;

    QByteArray nalBody = spsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 7)
        return info;

    const QByteArray rbsp = ttRbspFromNal(nalBody);
    TTBitReader r(rbsp);
    TTH264SpsHeader h;
    const bool complete = ttParseH264SpsHeader(r, h);
    info.bitDepthLuma = h.bitDepthLuma;
    info.log2MaxFrameNumMinus4 = h.log2MaxFrameNumMinus4;
    info.pocType = h.pocType;
    if (!complete)
        return info;                    // over-long POC cycle: partial result, as before
    info.log2MaxPocLsbMinus4 = h.log2MaxPocLsbMinus4;
    info.frameMbsOnly = h.frameMbsOnly;

    // Uncropped luma dimensions: map units are frame MBs when
    // frame_mbs_only_flag is set, field MB pairs otherwise (height x2).
    // Sufficient for the encoder POC probe, which only needs VALID encoder
    // dimensions; cropping (e.g. 1088 vs 1080) does not affect POC fields.
    info.picWidth  = static_cast<int>(h.picWidthInMbsMinus1 + 1) * 16;
    info.picHeight = static_cast<int>(h.picHeightInMapUnitsMinus1 + 1) * 16
                     * (info.frameMbsOnly ? 1 : 2);

    return info;
}

// ----------------------------------------------------------------------------
// Read frame_num from a raw H.264 slice NAL (after start code).
// Returns frame_num value, or -1 on error.
// frameNumBitWidth = log2_max_frame_num_minus4 + 4
// ----------------------------------------------------------------------------
static int readFrameNumFromSlice(const uint8_t* nalData, int nalSize, int frameNumBitWidth)
{
    if (nalSize < 3 || frameNumBitWidth <= 0) return -1;

    uint8_t nalType = nalData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return -1;  // not a slice

    TTBitReader r(nalData, nalSize);
    r.setPos(8);  // skip NAL header byte
    r.ue();       // first_mb_in_slice
    r.ue();       // slice_type
    r.ue();       // pic_parameter_set_id

    // frame_num is u(v) with v = frameNumBitWidth
    return static_cast<int>(r.bits(frameNumBitWidth));
}

// ----------------------------------------------------------------------------
// Patch frame_num in a raw H.264 slice NAL (after start code).
// Overwrites frame_num in-place (fixed-width field, no size change).
// frameNumBitWidth = log2_max_frame_num_minus4 + 4
// ----------------------------------------------------------------------------
static void writeFrameNumInSlice(uint8_t* nalData, int nalSize, int frameNumBitWidth,
                                  uint32_t newFrameNum)
{
    if (nalSize < 3 || frameNumBitWidth <= 0) return;

    uint8_t nalType = nalData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return;  // not a slice

    TTBitReader r(nalData, nalSize);
    r.setPos(8);  // skip NAL header byte
    r.ue();       // first_mb_in_slice
    r.ue();       // slice_type
    r.ue();       // pic_parameter_set_id

    // Overwrite frame_num at current position
    ttOverwriteBits(nalData, nalSize, r.pos(), newFrameNum, frameNumBitWidth);
}

// ----------------------------------------------------------------------------
// Locate poc_lsb bit position in a raw H.264 slice NAL (RBSP, after EP3 removal).
// Returns bit position of poc_lsb field, or -1 if not applicable.
// Only valid for poc_type == 0 slices.
// ----------------------------------------------------------------------------
static int locatePocLsbInSlice(const uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, bool frameMbsOnly)
{
    if (rbspSize < 3 || frameNumBitWidth <= 0) return -1;

    uint8_t nalType = rbspData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return -1;

    TTBitReader r(rbspData, rbspSize);
    r.setPos(8);                  // skip NAL header byte
    r.ue();                       // first_mb_in_slice
    r.ue();                       // slice_type
    r.ue();                       // pic_parameter_set_id
    r.bits(frameNumBitWidth);     // frame_num

    if (!frameMbsOnly) {
        if (r.flag())             // field_pic_flag
            r.bits(1);            // bottom_field_flag
    }

    if (nalType == 5) {
        r.ue();                   // idr_pic_id
    }

    // The reader now stands on pic_order_cnt_lsb
    return r.pos();
}

// ----------------------------------------------------------------------------
// Read poc_lsb from a raw H.264 slice NAL (RBSP).
// Returns poc_lsb value, or -1 on error.
// ----------------------------------------------------------------------------
static int readPocLsbFromSlice(const uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, int pocLsbBitWidth,
                                bool frameMbsOnly)
{
    if (pocLsbBitWidth <= 0) return -1;
    int bitPos = locatePocLsbInSlice(rbspData, rbspSize, frameNumBitWidth, frameMbsOnly);
    if (bitPos < 0) return -1;
    TTBitReader r(rbspData, rbspSize);
    r.setPos(bitPos);
    return static_cast<int>(r.bits(pocLsbBitWidth));
}

// ----------------------------------------------------------------------------
// Write poc_lsb in a raw H.264 slice NAL (RBSP, in-place).
// Fixed-width field — no bit shifting, CABAC data stays intact.
// ----------------------------------------------------------------------------
static void writePocLsbInSlice(uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, int pocLsbBitWidth,
                                bool frameMbsOnly, uint32_t newPocLsb)
{
    if (pocLsbBitWidth <= 0) return;
    int bitPos = locatePocLsbInSlice(rbspData, rbspSize, frameNumBitWidth, frameMbsOnly);
    if (bitPos < 0) return;
    ttOverwriteBits(rbspData, rbspSize, bitPos, newPocLsb, pocLsbBitWidth);
}

// ----------------------------------------------------------------------------
// Read poc_lsb from the first slice NAL of an access unit.
// Handles start codes and emulation prevention.
// Returns poc_lsb value, or -1 if not applicable.
// ----------------------------------------------------------------------------
int ttReadPocLsbFromAU(const QByteArray& auData, int frameNumBitWidth,
                            int pocLsbBitWidth, bool frameMbsOnly)
{
    if (pocLsbBitWidth <= 0 || frameNumBitWidth <= 0 || auData.isEmpty())
        return -1;
    const QByteArray rbsp = firstSliceRbsp(auData);
    if (rbsp.isEmpty())
        return -1;
    return readPocLsbFromSlice(
        reinterpret_cast<const uint8_t*>(rbsp.constData()),
        rbsp.size(), frameNumBitWidth, pocLsbBitWidth, frameMbsOnly);
}

// ----------------------------------------------------------------------------
// Patch poc_lsb in the last slice NAL of a packet (raw encoder output).
// The last slice's poc_lsb becomes prevPicOrderCntLsb for the next picture.
// Handles start codes and emulation prevention correctly.
// Returns patched data, or original if no slice found or patch not needed.
// ----------------------------------------------------------------------------
QByteArray ttPatchPocLsbInPacket(const QByteArray& packetData,
                                       int frameNumBitWidth, int pocLsbBitWidth,
                                       bool frameMbsOnly, uint32_t newPocLsb)
{
    if (pocLsbBitWidth <= 0 || frameNumBitWidth <= 0 || packetData.isEmpty())
        return packetData;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());

    // Find the LAST slice NAL in the packet (skip SPS/PPS/SEI)
    int lastSliceScStart = -1;
    int lastSliceScLen = 0;
    int lastSliceNalStart = -1;
    int pos = 0;

    while (pos < packetData.size()) {
        int scLen = 0;
        int scStart = ttNextStartCode(data, packetData.size(), pos, &scLen);
        if (scStart < 0) break;

        int nalStart = scStart + scLen;
        if (nalStart >= packetData.size()) break;

        uint8_t nalType = data[nalStart] & 0x1F;
        if (nalType == 1 || nalType == 5) {
            lastSliceScStart = scStart;
            lastSliceScLen = scLen;
            lastSliceNalStart = nalStart;
        }
        pos = nalStart + 1;
    }

    if (lastSliceNalStart < 0)
        return packetData;  // no slice NAL found

    // Find end of this slice NAL
    int nalEnd = ttNalEnd(data, packetData.size(), lastSliceNalStart + 1);

    // Extract NAL body, remove EP3, patch poc_lsb, re-add EP3
    QByteArray nalBody = packetData.mid(lastSliceNalStart, nalEnd - lastSliceNalStart);
    QByteArray rbsp = ttRbspFromNal(nalBody);

    // Verify we can read poc_lsb before patching
    int oldPocLsb = readPocLsbFromSlice(
        reinterpret_cast<const uint8_t*>(rbsp.constData()), rbsp.size(),
        frameNumBitWidth, pocLsbBitWidth, frameMbsOnly);
    if (oldPocLsb < 0)
        return packetData;

    // Patch poc_lsb in RBSP
    writePocLsbInSlice(reinterpret_cast<uint8_t*>(rbsp.data()), rbsp.size(),
                        frameNumBitWidth, pocLsbBitWidth, frameMbsOnly, newPocLsb);

    // Re-add emulation prevention
    QByteArray patchedNalBody = ttNalFromRbsp(rbsp);

    // Rebuild packet: data before slice NAL + start code + patched NAL + data after
    QByteArray result;
    result.reserve(packetData.size() + 8);
    result.append(packetData.constData(), lastSliceScStart);
    result.append(packetData.constData() + lastSliceScStart, lastSliceScLen);
    result.append(patchedNalBody);
    if (nalEnd < packetData.size())
        result.append(packetData.mid(nalEnd));

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      POC fix: patched encoder slice poc_lsb" << oldPocLsb
                 << "->" << newPocLsb;
    }

    return result;
}

// ----------------------------------------------------------------------------
// Find and parse the first SPS NAL (type 7) in an Annex B H.264 packet.
// Used to extract encoder SPS parameters from inline SPS/PPS in first output.
// Returns true if SPS was found and parsed successfully.
// ----------------------------------------------------------------------------
bool ttFindH264SpsInPacket(const QByteArray& packetData, TTH264SpsInfo& spsInfo)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(packetData.constData());
    const int sz = packetData.size();

    int scLen = 0;
    for (int pos = 0, sc; (sc = ttNextStartCode(d, sz, pos, &scLen)) >= 0; ) {
        const int nalType = d[sc + scLen] & 0x1F;
        const int nalEnd = ttNalEnd(d, sz, sc + scLen + 1);
        if (nalType == 7) {
            QByteArray spsNal = packetData.mid(sc, nalEnd - sc);
            spsInfo = ttParseH264SpsInfo(spsNal);
            return (spsInfo.log2MaxFrameNumMinus4 >= 0);
        }
        pos = nalEnd;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Patch frame_num in all slice NALs of an access unit.
// Handles emulation prevention bytes correctly.
// Returns patched AU data, or original on error.
// frameNumDelta is added to each frame_num (modulo maxFrameNum).
// ----------------------------------------------------------------------------
QByteArray ttPatchFrameNumInAU(const QByteArray& auData, int frameNumBitWidth,
                                     int frameNumDelta, int maxFrameNum)
{
    if (frameNumDelta == 0 || frameNumBitWidth <= 0)
        return auData;

    QByteArray result;
    result.reserve(auData.size() + 64);
    bool patched = false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int pos = 0;

    while (pos < auData.size()) {
        // Find next start code
        int scLen = 0;
        const int scStart = ttNextStartCode(data, auData.size(), pos, &scLen);
        if (scStart < 0) {
            result.append(auData.mid(pos));
            break;
        }

        // Copy data before start code
        if (scStart > pos)
            result.append(auData.mid(pos, scStart - pos));

        // Find end of this NAL (next start code or end of data)
        int nalEnd = ttNalEnd(data, auData.size(), scStart + scLen + 1);

        // Get NAL body (after start code)
        QByteArray nalBody = auData.mid(scStart + scLen, nalEnd - scStart - scLen);
        if (!nalBody.isEmpty()) {
            uint8_t nalType = (uint8_t)nalBody[0] & 0x1F;

            if (nalType == 1 || nalType == 5) {
                // Slice NAL — remove emulation prevention, patch, re-add
                QByteArray rbsp = ttRbspFromNal(nalBody);
                int frameNum = readFrameNumFromSlice(
                    reinterpret_cast<const uint8_t*>(rbsp.constData()),
                    rbsp.size(), frameNumBitWidth);

                if (frameNum >= 0) {
                    int newFrameNum = (frameNum + frameNumDelta) % maxFrameNum;
                    if (newFrameNum < 0) newFrameNum += maxFrameNum;

                    writeFrameNumInSlice(
                        reinterpret_cast<uint8_t*>(rbsp.data()),
                        rbsp.size(), frameNumBitWidth, newFrameNum);

                    QByteArray patchedNal = ttNalFromRbsp(rbsp);
                    result.append(auData.mid(scStart, scLen));  // start code
                    result.append(patchedNal);
                    patched = true;
                    pos = nalEnd;
                    continue;
                }
            }
        }

        // Not a slice or patch failed — copy as-is
        result.append(auData.mid(scStart, nalEnd - scStart));
        pos = nalEnd;
    }

    return patched ? result : auData;
}

// ----------------------------------------------------------------------------
// Read frame_num from the first slice NAL of an access unit.
// Returns -1 if not H.264 or on error.
// ----------------------------------------------------------------------------
int ttReadFrameNumFromAU(const QByteArray& auData, int frameNumBitWidth)
{
    if (frameNumBitWidth <= 0 || auData.isEmpty())
        return -1;
    const QByteArray rbsp = firstSliceRbsp(auData);
    if (rbsp.isEmpty())
        return -1;
    return readFrameNumFromSlice(
        reinterpret_cast<const uint8_t*>(rbsp.constData()),
        rbsp.size(), frameNumBitWidth);
}

// ----------------------------------------------------------------------------
// Neutralize MMCO commands in all reference slices of an AU.
// After EOS flush, the DPB is empty. Non-IDR slices with adaptive MMCO
// commands try to unref frames that no longer exist → "mmco: unref short
// failure" + DPB overflow. Fix: set adaptive_ref_pic_marking_mode_flag
// from 1→0, removing all MMCO data. The RBSP is rebuilt with the MMCO
// bits removed and CABAC data byte-aligned.
// Handles all slice types: I, P, B (with ref_pic_list_modification,
// pred_weight_table, etc.).
// ----------------------------------------------------------------------------
QByteArray ttNeutralizeMmcoInAU(const QByteArray& auData,
    int log2MaxFrameNum, int pocLsbBitWidth, bool frameMbsOnly,
    const TTH264PpsInfo& pps)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int auSize = auData.size();
    QByteArray result = auData;

    // Iterate over all NALs in the AU
    int pos = 0;
    while (pos < result.size()) {
        data = reinterpret_cast<const uint8_t*>(result.constData());
        auSize = result.size();

        int scLen = 0;
        const int scStart = ttNextStartCode(data, auSize, pos, &scLen);
        if (scStart < 0) break;

        int nalStart = scStart + scLen;
        if (nalStart >= auSize) break;

        uint8_t nalByte = data[nalStart];
        uint8_t nalType = nalByte & 0x1F;
        uint8_t nalRefIdc = (nalByte >> 5) & 0x03;

        if (nalType != 1 || nalRefIdc == 0) {
            pos = nalStart + 1;
            continue;
        }

        // Find end of this NAL
        int nalEnd = ttNalEnd(data, auSize, nalStart + 1);

        QByteArray nalBody = result.mid(nalStart, nalEnd - nalStart);
        const QByteArray rbsp = ttRbspFromNal(nalBody);
        const uint8_t* r = reinterpret_cast<const uint8_t*>(rbsp.constData());
        const int rSize = rbsp.size();

        // Parse slice header
        TTBitReader br(rbsp);
        br.setPos(8);  // skip NAL header byte
        br.ue();                       // first_mb_in_slice
        uint32_t sliceType = br.ue();  // slice_type
        int sliceTypeM5 = sliceType % 5;  // 0=P, 1=B, 2=I, 3=SP, 4=SI

        br.ue();                              // pic_parameter_set_id
        br.bits(log2MaxFrameNum);           // frame_num

        bool isFieldSlice = false;
        if (!frameMbsOnly) {
            uint32_t fieldPicFlag = br.bits(1);
            isFieldSlice = (fieldPicFlag != 0);
            if (isFieldSlice)
                br.bits(1);                 // bottom_field_flag
        }

        // pic_order_cnt_lsb (poc_type == 0)
        br.bits(pocLsbBitWidth);

        // delta_pic_order_cnt_bottom: only when PPS flag set AND frame slice
        if (pps.bottomFieldPicOrderPresent && !isFieldSlice)
            br.se();

        // --- P/B specific fields ---
        if (sliceTypeM5 == 1)  // B-slice
            br.bits(1);  // direct_spatial_mv_pred_flag

        int numRefL0 = pps.numRefIdxL0DefaultActiveMinus1;
        int numRefL1 = pps.numRefIdxL1DefaultActiveMinus1;

        if (sliceTypeM5 == 0 || sliceTypeM5 == 1 || sliceTypeM5 == 3) {
            // P, B, or SP: num_ref_idx_active_override_flag
            uint32_t overrideFlag = br.bits(1);
            if (overrideFlag) {
                numRefL0 = br.ue();  // num_ref_idx_l0_active_minus1
                if (sliceTypeM5 == 1)
                    numRefL1 = br.ue();  // num_ref_idx_l1_active_minus1
            }
        }
        // num_ref_idx_lX_active_minus1 <= 31 (H.264 7.4.3); bounds the
        // pred-weight loops when a damaged header yields a huge value.
        numRefL0 = qMin(numRefL0, 31);
        numRefL1 = qMin(numRefL1, 31);

        // A slice header that ends before its terminator, or holds a code
        // the reader rejects (more than 31 leading zeros), would keep the
        // RPLM/MMCO loops below spinning: after either the reader returns 0
        // and never yields idc 3. Such a slice is left unchanged.
        bool truncated = false;

        // ref_pic_list_modification (P, SP, B only)
        if (sliceTypeM5 != 2 && sliceTypeM5 != 4) {
            // L0
            uint32_t rplmFlag = br.bits(1);
            if (rplmFlag) {
                while (true) {
                    if (br.error() || br.atEnd()) { truncated = true; break; }
                    uint32_t idc = br.ue();
                    if (idc == 3) break;
                    br.ue();  // abs_diff_pic_num_minus1 or long_term_pic_num
                }
            }
            // L1 (B-slices only)
            if (sliceTypeM5 == 1) {
                rplmFlag = br.bits(1);
                if (rplmFlag) {
                    while (true) {
                        if (br.error() || br.atEnd()) { truncated = true; break; }
                        uint32_t idc = br.ue();
                        if (idc == 3) break;
                        br.ue();
                    }
                }
            }
        }
        if (truncated) {
            pos = nalEnd;
            continue;
        }

        // pred_weight_table (P with weighted_pred, B with weighted_bipred_idc==1)
        bool hasWeightTable = false;
        if ((sliceTypeM5 == 0 || sliceTypeM5 == 3) && pps.weightedPredFlag)
            hasWeightTable = true;
        if (sliceTypeM5 == 1 && pps.weightedBipredIdc == 1)
            hasWeightTable = true;

        if (hasWeightTable) {
            br.ue();  // luma_log2_weight_denom
            br.ue();  // chroma_log2_weight_denom
            skipPredWeightList(br, numRefL0);
            if (sliceTypeM5 == 1)
                skipPredWeightList(br, numRefL1);
        }

        // dec_ref_pic_marking
        int flagBitPos = br.pos();
        uint32_t adaptiveFlag = br.bits(1);

        if (adaptiveFlag == 0) {
            pos = nalEnd;
            continue;
        }

        // Skip all MMCO commands
        int mmcoBitsStart = br.pos();
        while (true) {
            if (br.error() || br.atEnd()) { truncated = true; break; }
            uint32_t mmcoOp = br.ue();
            if (mmcoOp == 0) break;
            if (mmcoOp == 1 || mmcoOp == 3)
                br.ue();  // difference_of_pic_nums_minus1
            if (mmcoOp == 2)
                br.ue();  // long_term_pic_num
            if (mmcoOp == 3 || mmcoOp == 6)
                br.ue();  // long_term_frame_idx
            if (mmcoOp == 4)
                br.ue();  // max_long_term_frame_idx_plus1
        }
        if (truncated) {
            pos = nalEnd;
            continue;
        }
        int afterMmcoBitPos = br.pos();
        int mmcoBitsRemoved = afterMmcoBitPos - mmcoBitsStart;

        // Parse remaining header after MMCO
        // cabac_init_idc: present for non-I/SI when CABAC
        if (pps.entropyCodingModeFlag && sliceTypeM5 != 2 && sliceTypeM5 != 4)
            br.ue();  // cabac_init_idc

        br.se();  // slice_qp_delta

        if (sliceTypeM5 == 3 || sliceTypeM5 == 4) {
            if (sliceTypeM5 == 3)
                br.bits(1);  // sp_for_switch_flag (u(1))
            br.se();  // slice_qs_delta
        }

        if (pps.deblockingFilterControlPresent) {
            uint32_t disableDeblocking = br.ue();
            if (disableDeblocking != 1) {
                br.se();  // slice_alpha_c0_offset_div2
                br.se();  // slice_beta_offset_div2
            }
        }
        int headerEndBitPos = br.pos();
        // A header the reader could not read to its end (damaged data after
        // the loops, e.g. inside slice_qp_delta) is left unchanged rather
        // than rebuilt from the zeros the reader substitutes.
        if (br.error()) {
            pos = nalEnd;
            continue;
        }

        // Rebuild RBSP without MMCO data
        int origCabacByte = (headerEndBitPos + 7) / 8;

        int cabacDataSize = rSize - origCabacByte;
        if (cabacDataSize < 0) {
            pos = nalEnd;
            continue;
        }

        TTBitReader src(rbsp);
        TTBitWriter w;

        // Copy header bits before flag
        w.copyBits(src, flagBitPos);

        // Write adaptive_ref_pic_marking_mode_flag = 0
        w.bits(0, 1);

        // Copy post-MMCO header bits
        src.setPos(afterMmcoBitPos);
        w.copyBits(src, headerEndBitPos - afterMmcoBitPos);

        if (pps.entropyCodingModeFlag) {
            // CABAC: cabac_alignment_one_bits up to the byte boundary, then
            // the payload bytes verbatim from the old boundary.
            w.alignWith(1);
            w.appendBytes(r + origCabacByte, cabacDataSize);
        } else {
            // CAVLC: slice_data follows the header without alignment. Copy
            // it up to the rbsp_stop_one_bit and write the trailing bits
            // anew - the header got shorter, so the old zero padding would
            // no longer end on a byte boundary.
            int stopBit = rSize * 8 - 1;
            while (stopBit >= headerEndBitPos
                   && ((r[stopBit >> 3] >> (7 - (stopBit & 7))) & 1) == 0)
                --stopBit;
            if (stopBit < headerEndBitPos) {   // no stop bit: keep the slice as is
                pos = nalEnd;
                continue;
            }
            w.copyBits(src, stopBit - headerEndBitPos);
            w.rbspTrailingBits();
        }

        QByteArray newNalBody = ttNalFromRbsp(w.data());

        // Reassemble AU with replaced NAL
        QByteArray newResult;
        newResult.reserve(auSize);
        newResult.append(result.left(scStart));
        newResult.append(result.mid(scStart, scLen));
        newResult.append(newNalBody);
        newResult.append(result.mid(nalEnd));

        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "    MMCO neutralized: slice_type=" << sliceType
                     << mmcoBitsRemoved << "bits removed,"
                     << "NAL" << nalBody.size() << "->" << newNalBody.size() << "bytes";
        }

        result = newResult;
        // Continue scanning from after the replaced NAL
        pos = scStart + scLen + newNalBody.size();
    }

    return result;
}

// ----------------------------------------------------------------------------
// SPS Unification: Rewrite a single encoder slice NAL to be compatible with
// the source SPS. Changes pps_id, widens frame_num and poc_lsb bit fields,
// inserts field_pic_flag=0 if needed. CABAC data is realigned.
// Input: NAL body (after start code, WITH emulation prevention bytes).
// Returns: rewritten NAL body, or empty on error.
// ----------------------------------------------------------------------------
QByteArray ttRewriteEncoderSliceForSourceSps(
    const QByteArray& nalBody,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const TTH264PpsInfo& encPps, uint32_t newPpsId, int frameIndex,
    int pocLsbBase)
{
    if (nalBody.isEmpty()) return QByteArray();

    uint8_t nalHeader = static_cast<uint8_t>(nalBody[0]);
    uint8_t nalType = nalHeader & 0x1F;
    if (nalType != 1 && nalType != 5) return QByteArray();  // only slice NALs

    uint8_t nalRefIdc = (nalHeader >> 5) & 0x03;

    // Demote subsequent IDRs (frameIndex > 0) to non-IDR I-slices.
    // The encoder produces IDRs at regular intervals (keyint). With linear
    // frame_num, a mid-sequence IDR at fn=54 causes a frame_num gap (the spec
    // requires fn=0 for IDR, but we write fn=54). This gap creates gray DPB
    // frames → artifacts. Converting to non-IDR keeps the linear fn sequence
    // intact and prevents unwanted DPB flushes within the re-encode.
    uint8_t origNalType = nalType;
    bool demoteIdr = (nalType == 5 && frameIndex > 0);
    if (demoteIdr) {
        nalType = 1;  // non-IDR slice
    }

    // Remove emulation prevention → RBSP
    const QByteArray oldRbsp = ttRbspFromNal(nalBody);
    const uint8_t* oldData = reinterpret_cast<const uint8_t*>(oldRbsp.constData());
    const int oldSize = oldRbsp.size();

    TTBitReader r(oldRbsp);
    TTBitWriter w;

    // 1. NAL header — write with potentially changed nalType (IDR→non-IDR)
    uint32_t header = r.bits(8);
    if (demoteIdr) {
        // Rewrite header: keep forbidden_zero_bit + nal_ref_idc, change nal_unit_type
        header = (header & 0xE0) | (nalType & 0x1F);
    }
    w.bits(header, 8);

    // 2. first_mb_in_slice (UE) — copy
    uint32_t firstMb = r.ue();
    w.ue(firstMb);

    // 3. slice_type (UE) — copy, save for later
    uint32_t sliceType = r.ue();
    w.ue(sliceType);
    uint32_t sliceTypeMod = sliceType % 5;
    // 0=P, 1=B, 2=I, 3=SP, 4=SI

    // 4. pps_id (UE) — read old, write new
    r.ue();  // skip old pps_id
    w.ue(newPpsId);

    // 5. frame_num — read encoder fn, write linear frameIndex to eliminate gaps.
    // The encoder cycles fn 0..MaxFN-1 (e.g., 0..15 with MaxFN=16). After SPS
    // rewriting to source MaxFN (e.g., 512), this cycling creates frame_num gaps
    // at every wrap (15→0 with MaxFN=512 = gap of 496 frames). At the re-encode
    // → stream-copy transition, the gap between the last encoder fn (15) and the
    // first stream-copy fn (e.g., 181) causes the decoder to create gray gap
    // frames in the DPB. Open-GOP B-frames then reference these gray frames as
    // L0 → block artifacts. Fix: use the linear frame index (0,1,2,...,N-1) as
    // frame_num. This produces a monotonic sequence that directly precedes the
    // stream-copy frame_nums, eliminating all gaps.
    uint32_t encFrameNum = r.bits(encLog2MaxFN);
    uint32_t srcMaxFrameNum = 1u << srcLog2MaxFN;
    uint32_t newFrameNum = static_cast<uint32_t>(frameIndex) % srcMaxFrameNum;
    w.bits(newFrameNum, srcLog2MaxFN);

    // 6. field_pic_flag (if !frame_mbs_only in SOURCE SPS)
    bool fieldPicFlag = false;
    if (!srcFrameMbsOnly) {
        if (!encFrameMbsOnly) {
            // Encoder also has field_pic_flag — read and copy
            fieldPicFlag = (r.bits(1) != 0);
            w.bits(fieldPicFlag ? 1 : 0, 1);
            if (fieldPicFlag) {
                uint32_t bottomFlag = r.bits(1);
                w.bits(bottomFlag, 1);
            }
        } else {
            // Encoder has frame_mbs_only=1, INSERT field_pic_flag=0 (frame-coded)
            w.bits(0, 1);
        }
    }

    // 7. idr_pic_id (only in original IDR NALs, NAL type 5)
    if (origNalType == 5) {
        uint32_t idrPicId = r.ue();
        if (!demoteIdr) {
            // Keep as IDR (first frame): write idr_pic_id
            w.ue(idrPicId);
        }
        // Demoted IDR: skip writing — non-IDR slices don't have idr_pic_id
    }

    // 8. poc_lsb (if poc_type 0) — linearize, not just widen.
    // The encoder's poc_lsb wraps at encMaxPocLsb (e.g. 16), producing
    // values 0,2,4,...,14,0,2,... When widened to srcMaxPocLsb (e.g. 256),
    // the decoder sees frequent backward jumps (14→0 is only -14, which
    // doesn't trigger PicOrderCntMsb increment at MaxPocLsb=256).
    // Fix: compute linear poc_lsb = (frameIndex * 2) % srcMaxPocLsb,
    // which only wraps at the source's MaxPocLsb boundary.
    //
    // The rewritten slice is decoded under the SOURCE SPS (poc_type 0), so
    // pic_order_cnt_lsb MUST be written whenever srcLog2MaxPocLsb > 0 — even
    // when the encoder slice has no such field (progressive libx264 emits
    // poc_type 2, encLog2MaxPocLsb == 0): then nothing is consumed from the
    // old header and the field is INSERTED. Skipping the write (the pre-fix
    // behaviour) bit-shifted every following header field and mass-corrupted
    // the output (defect B, 2026-07-16).
    if (srcLog2MaxPocLsb > 0) {
        if (encLog2MaxPocLsb > 0)
            r.bits(encLog2MaxPocLsb);  // skip old
        int srcMaxPocLsb = 1 << srcLog2MaxPocLsb;
        // pocLsbBase >= 0: anchored numbering (non-PAFF POC seam) so the last
        // encoded frame lands directly below the copy-start POC. Otherwise
        // legacy linear numbering from 0 (PAFF path, byte-identical output).
        uint32_t newPocLsb = (pocLsbBase >= 0)
            ? static_cast<uint32_t>((pocLsbBase + 2 * frameIndex) % srcMaxPocLsb)
            : (static_cast<uint32_t>(frameIndex) * 2) % srcMaxPocLsb;
        w.bits(newPocLsb, srcLog2MaxPocLsb);

        // delta_pic_order_cnt_bottom (if PPS flag && !field_pic_flag).
        // Presence in the REWRITTEN slice is governed by the encoder PPS
        // (pps_id=1) the slice references. A poc_type-2 encoder slice carries
        // no such field to copy — write the neutral 0 in that case.
        if (encPps.bottomFieldPicOrderPresent && !fieldPicFlag) {
            int32_t deltaBottom = (encLog2MaxPocLsb > 0)
                ? r.se()
                : 0;
            w.se(deltaBottom);
        }
    }

    // 9. redundant_pic_cnt (if PPS flag) — copy
    if (encPps.redundantPicCntPresent) {
        uint32_t rpc = r.ue();
        w.ue(rpc);
    }

    // 10. For P/B slices: additional fields before dec_ref_pic_marking
    if (sliceTypeMod == 1) {
        // B-slice: direct_spatial_mv_pred_flag (1 bit)
        uint32_t dsmpf = r.bits(1);
        w.bits(dsmpf, 1);
    }

    if (sliceTypeMod == 0 || sliceTypeMod == 1 || sliceTypeMod == 3) {
        // P, B, or SP: num_ref_idx_active_override_flag
        uint32_t overrideFlag = r.bits(1);
        w.bits(overrideFlag, 1);
        int numRefL0 = encPps.numRefIdxL0DefaultActiveMinus1;
        if (overrideFlag) {
            uint32_t numRefL0Override = r.ue();
            w.ue(numRefL0Override);
            numRefL0 = numRefL0Override;
            if (sliceTypeMod == 1) {
                uint32_t numRefL1Override = r.ue();
                w.ue(numRefL1Override);
            }
        }
        // num_ref_idx_l0_active_minus1 <= 31 (H.264 7.4.3): bounds the
        // weight loop below; the value written above stays as read.
        numRefL0 = qMin(numRefL0, 31);

        // 11. ref_pic_list_modification
        // Short-term entries (idc 0/1) carry abs_diff_pic_num_minus1 values the
        // encoder computed in ITS PicNum domain (MaxPicNum = 1<<encLog2MaxFN,
        // fn cycling). The rewritten slice runs with LINEAR frame_num under the
        // source SPS (MaxPicNum = 1<<srcLog2MaxFN), so those modular diffs must
        // be translated: resolve each entry to the actual referenced frame via
        // the H.264 8.2.4.3.1 predictor chain in the encoder domain, then
        // re-encode the diff against the linear numbering. Without this, any
        // re-encode longer than the encoder's fn cycle (16 frames) references
        // pictures ~MaxPicNum back ("reference picture missing during
        // reorder"). Long-term entries (idc 2) are domain-independent — copied
        // verbatim. Field slices never occur here (x264 emits frame-coded
        // output only).
        int encMaxPicNum = 1 << encLog2MaxFN;
        // A header that ends inside the list or holds a code the reader
        // rejects never delivers idc 3: after either the reader returns 0.
        // Such a slice is rejected (the caller keeps it as is).
        bool truncated = false;
        auto translateRplmList = [&](void) {
            int predOld = static_cast<int>(encFrameNum);
            int predNew = frameIndex;
            uint32_t idc;
            do {
                if (r.error() || r.atEnd()) { truncated = true; break; }
                idc = r.ue();
                if (idc == 0 || idc == 1) {
                    uint32_t v = r.ue();
                    // Encoder-domain target picNum (modular predictor chain)
                    int t = (idc == 0) ? predOld - static_cast<int>(v) - 1
                                       : predOld + static_cast<int>(v) + 1;
                    t = ((t % encMaxPicNum) + encMaxPicNum) % encMaxPicNum;
                    predOld = t;
                    // Actual referenced frame index: unique j < frameIndex
                    // within one fn cycle with j mod encMax == t.
                    int delta = ((static_cast<int>(encFrameNum) - t) % encMaxPicNum
                                 + encMaxPicNum) % encMaxPicNum;
                    int j = frameIndex - delta;
                    if (delta == 0 || j < 0) {
                        // Inconsistent entry — keep original values (decoder
                        // will fall back to the default list entry).
                        if (TTSettings::instance()->logSmartCut())
                            qDebug() << "      RPLM translation bail-out: idc" << idc
                                     << "v" << v << "at frameIndex" << frameIndex;
                        w.ue(idc);
                        w.ue(v);
                        continue;
                    }
                    // Re-encode against the linear numbering. d == 0 means the
                    // entry re-lists the SAME picture (x264 pads short ref
                    // lists with full-cycle no-op diffs, e.g. abs_diff 16 with
                    // MaxPicNum 16) — expressed in the target domain as a full
                    // cycle: abs_diff = srcMaxPicNum.
                    int d = predNew - j;
                    uint32_t newIdc = (d >= 0) ? 0u : 1u;
                    uint32_t newV = (d == 0)
                        ? srcMaxFrameNum - 1
                        : static_cast<uint32_t>((d > 0 ? d : -d) - 1);
                    predNew = j;
                    w.ue(newIdc);
                    w.ue(newV);
                } else if (idc == 2) {
                    uint32_t v = r.ue();
                    w.ue(idc);
                    w.ue(v);
                } else {
                    w.ue(idc);
                }
            } while (idc != 3);
        };
        // For P/SP slices: ref_pic_list_modification_flag_l0
        uint32_t rplmFlag0 = r.bits(1);
        w.bits(rplmFlag0, 1);
        if (rplmFlag0)
            translateRplmList();
        if (truncated)
            return QByteArray();
        if (sliceTypeMod == 1) {
            // B-slice: ref_pic_list_modification_flag_l1
            uint32_t rplmFlag1 = r.bits(1);
            w.bits(rplmFlag1, 1);
            if (rplmFlag1)
                translateRplmList();
            if (truncated)
                return QByteArray();
        }

        // 12. pred_weight_table (if weighted pred for P, or explicit bipred for B)
        bool needWeightTable = (encPps.weightedPredFlag && (sliceTypeMod == 0 || sliceTypeMod == 3))
                            || (encPps.weightedBipredIdc == 1 && sliceTypeMod == 1);
        if (needWeightTable) {
            uint32_t lumaLog2WeightDenom = r.ue();
            w.ue(lumaLog2WeightDenom);
            // ChromaArrayType=1 for 4:2:0 (standard DVB/x264)
            uint32_t chromaLog2WeightDenom = r.ue();
            w.ue(chromaLog2WeightDenom);

            // L0 weights
            for (int i = 0; i <= numRefL0; i++) {
                uint32_t lumaFlag = r.bits(1);
                w.bits(lumaFlag, 1);
                if (lumaFlag) {
                    int32_t wgt = r.se();
                    int32_t o = r.se();
                    w.se(wgt);
                    w.se(o);
                }
                uint32_t chromaFlag = r.bits(1);
                w.bits(chromaFlag, 1);
                if (chromaFlag) {
                    for (int j = 0; j < 2; j++) {
                        int32_t wgt = r.se();
                        int32_t o = r.se();
                        w.se(wgt);
                        w.se(o);
                    }
                }
            }
            // L1 weights for B-slices omitted (bf=0, no B-slices)
        }
    }

    // 13. dec_ref_pic_marking — copy (with IDR→non-IDR conversion if needed)
    if (nalRefIdc != 0) {
        if (origNalType == 5 && demoteIdr) {
            // Demoted IDR: READ IDR format (2 bits), WRITE non-IDR format
            r.bits(1);  // no_output_of_prior_pics
            r.bits(1);  // long_term_reference
            // Write adaptive_ref_pic_marking_mode_flag = 0 (sliding window)
            w.bits(0, 1);
        } else if (origNalType == 5) {
            // First IDR (frameIndex=0): keep IDR format
            uint32_t noOutput = r.bits(1);
            uint32_t longTerm = r.bits(1);
            w.bits(noOutput, 1);
            w.bits(longTerm, 1);
        } else {
            // Non-IDR: adaptive_ref_pic_marking_mode_flag + possible MMCO
            uint32_t modeFlag = r.bits(1);
            w.bits(modeFlag, 1);
            if (modeFlag) {
                uint32_t mmco;
                do {
                    // Truncated header: no op 0 will come (see RPLM above).
                    if (r.error() || r.atEnd())
                        return QByteArray();
                    mmco = r.ue();
                    w.ue(mmco);
                    if (mmco == 1 || mmco == 3) {
                        uint32_t v = r.ue();
                        w.ue(v);
                    }
                    if (mmco == 2) {
                        uint32_t v = r.ue();
                        w.ue(v);
                    }
                    if (mmco == 3 || mmco == 6) {
                        uint32_t v = r.ue();
                        w.ue(v);
                    }
                    if (mmco == 4) {
                        uint32_t v = r.ue();
                        w.ue(v);
                    }
                } while (mmco != 0);
            }
        }
    }

    // 14. cabac_init_idc (for P/B slices with CABAC) — copy
    if (encPps.entropyCodingModeFlag && sliceTypeMod != 2 && sliceTypeMod != 4) {
        uint32_t cabacInitIdc = r.ue();
        w.ue(cabacInitIdc);
    }

    // 15. slice_qp_delta (SE) — copy
    int32_t sliceQpDelta = r.se();
    w.se(sliceQpDelta);

    // 16. Deblocking filter params (if PPS flag) — copy
    if (encPps.deblockingFilterControlPresent) {
        uint32_t ddfIdc = r.ue();
        w.ue(ddfIdc);
        if (ddfIdc != 1) {
            int32_t alphaOff = r.se();
            int32_t betaOff = r.se();
            w.se(alphaOff);
            w.se(betaOff);
        }
    }

    // A header the reader could not read to its end is rejected: the
    // fields above would carry the zeros the reader substitutes.
    if (r.error())
        return QByteArray();

    // 17. CABAC alignment + slice data
    if (encPps.entropyCodingModeFlag) {
        // cabac_alignment_one_bit(s) exist ONLY while the header does not end
        // on a byte boundary (H.264 7.3.4: "while !byte_aligned()"). A header
        // that already ends byte-aligned has NO alignment bits — consuming or
        // emitting them unconditionally shifts the CABAC payload by a full
        // byte and silently kills the whole slice (defect E: the widened
        // rewritten header hit exactly 48 bits on 08x04's first IDR, the
        // decoder discarded it and concealed the frame gray).
        // All alignment bits are 1 (cabac_alignment_one_bit = f(1)).
        int oldPad = (8 - (r.pos() % 8)) % 8;   // 0 when already aligned
        if (oldPad > 0) r.bits(oldPad);

        w.alignWith(1);                           // nothing when already aligned

        // Copy CABAC data bytes (byte-aligned in both streams)
        int oldCabacByte = r.pos() / 8;
        int cabacLen = oldSize - oldCabacByte;
        if (cabacLen > 0)
            w.appendBytes(oldData + oldCabacByte, cabacLen);
    } else {
        // CAVLC: copy remaining bits
        w.copyBits(r, r.sizeBits() - r.pos());
    }

    // Re-add emulation prevention bytes
    return ttNalFromRbsp(w.data());
}

// ----------------------------------------------------------------------------
// Extract the first PPS NAL (with start code) from an encoder packet.
// Returns the complete PPS NAL including start code, or empty if not found.
// ----------------------------------------------------------------------------
QByteArray ttExtractPpsFromPacket(const QByteArray& packetData)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());
    int size = packetData.size();
    int pos = 0;

    while (pos < size) {
        int scLen = 0;
        const int scStart = ttNextStartCode(data, size, pos, &scLen);
        if (scStart < 0) break;

        int nalStart = scStart + scLen;
        int nalEnd = ttNalEnd(data, size, nalStart + 1);

        if (nalStart < size) {
            int nalType = data[nalStart] & 0x1F;
            if (nalType == 8) {  // PPS
                return packetData.mid(scStart, nalEnd - scStart);
            }
        }
        pos = nalEnd;
    }
    return QByteArray();
}

// ----------------------------------------------------------------------------
// Patch pps_id in a PPS NAL via RBSP reconstruction.
// Input: PPS NAL WITH start code. Returns patched PPS NAL WITH start code.
// ----------------------------------------------------------------------------
QByteArray ttPatchH264PpsId(const QByteArray& ppsNal, uint32_t newPpsId)
{
    const int startCodeLen = ttStartCodeLength(ppsNal);
    if (startCodeLen == 0)
        return QByteArray();

    QByteArray startCode = ppsNal.left(startCodeLen);
    QByteArray nalBody = ppsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 8)
        return QByteArray();

    const QByteArray oldRbsp = ttRbspFromNal(nalBody);
    TTBitReader r(oldRbsp);
    TTBitWriter w;

    // NAL header (8 bits) — copy
    w.bits(r.bits(8), 8);

    // old pps_id — skip; write new
    r.ue();
    w.ue(newPpsId);

    // Copy all remaining bits
    w.copyBits(r, r.sizeBits() - r.pos());

    QByteArray result = startCode;
    result.append(ttNalFromRbsp(w.data()));
    return result;
}

// ----------------------------------------------------------------------------
// Rewrite all NALs in an encoder packet for source SPS compatibility.
// Strips SPS/PPS/SEI/AUD NALs, rewrites slice NALs.
// Returns rewritten packet, or original on error.
// ----------------------------------------------------------------------------
QByteArray ttRewriteEncoderPacketForSourceSps(
    const QByteArray& packetData,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const TTH264PpsInfo& encPps, uint32_t newPpsId, int frameIndex,
    int pocLsbBase)
{
    QByteArray result;
    result.reserve(packetData.size() + 128);
    bool modified = false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());
    int pos = 0;

    while (pos < packetData.size()) {
        int scLen = 0;
        int scStart = ttNextStartCode(data, packetData.size(), pos, &scLen);
        if (scStart < 0) {
            result.append(packetData.mid(pos));
            break;
        }

        if (scStart > pos)
            result.append(packetData.mid(pos, scStart - pos));

        int nalStart = scStart + scLen;
        int nalEnd = ttNalEnd(data, packetData.size(), nalStart + 1);

        if (nalStart < packetData.size()) {
            int nalType = data[nalStart] & 0x1F;

            if (nalType == 7 || nalType == 6 || nalType == 9) {
                // Strip SPS(7), SEI(6), AUD(9) — but KEEP PPS(8)!
                // PPS must stay in the packet so the MKV muxer includes it
                // in the same block as the encoder slices. This ensures the
                // decoder finds PPS(id=1) during both sequential and seek playback.
                modified = true;
            } else if (nalType == 8) {
                // PPS — patch pps_id from 0→newPpsId and keep in packet
                QByteArray ppsNal = packetData.mid(scStart, nalEnd - scStart);
                QByteArray patched = ttPatchH264PpsId(ppsNal, newPpsId);
                if (!patched.isEmpty()) {
                    result.append(patched);
                } else {
                    result.append(ppsNal);  // keep original if patch fails
                }
                modified = true;
            } else if (nalType == 1 || nalType == 5) {
                // Slice — rewrite
                QByteArray nalBody = packetData.mid(nalStart, nalEnd - nalStart);
                QByteArray rewritten = ttRewriteEncoderSliceForSourceSps(
                    nalBody, encLog2MaxFN, encLog2MaxPocLsb, encFrameMbsOnly,
                    srcLog2MaxFN, srcLog2MaxPocLsb, srcFrameMbsOnly,
                    encPps, newPpsId, frameIndex, pocLsbBase);
                if (!rewritten.isEmpty()) {
                    result.append(packetData.mid(scStart, scLen));  // start code
                    result.append(rewritten);
                    modified = true;
                } else {
                    // Rewrite failed — keep original NAL
                    result.append(packetData.mid(scStart, nalEnd - scStart));
                }
            } else {
                // Other NAL types — copy as-is
                result.append(packetData.mid(scStart, nalEnd - scStart));
            }
        }
        pos = nalEnd;
    }

    return modified ? result : packetData;
}

// ----------------------------------------------------------------------------
// Parse H.264 PPS for fields needed by IDR conversion.
// Input: raw PPS NAL data WITH start code prefix.
// ----------------------------------------------------------------------------
TTH264PpsInfo ttParseH264PpsInfo(const QByteArray& ppsNal)
{
    TTH264PpsInfo info = { true, false, true, false, false, 0, 0, 0, false };  // safe defaults

    // Find and strip start code
    const int startCodeLen = ttStartCodeLength(ppsNal);
    if (startCodeLen == 0)
        return info;

    QByteArray nalBody = ppsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 8)
        return info;  // not PPS

    const QByteArray rbsp = ttRbspFromNal(nalBody);
    TTBitReader r(rbsp);
    r.setPos(8);  // skip NAL header

    r.ue();   // pps_id
    r.ue();   // sps_id
    info.entropyCodingModeFlag = (r.bits(1) != 0);
    info.bottomFieldPicOrderPresent = (r.bits(1) != 0);
    uint32_t numSliceGroupsMinus1 = r.ue();
    if (numSliceGroupsMinus1 > 0) {
        // Complex slice group map — bail, use defaults (very rare in DVB)
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "  PPS: num_slice_groups > 1 (" << numSliceGroupsMinus1+1
                     << "), using default PPS flags for IDR conversion";
        }
        info.valid = true;
        return info;
    }
    info.numRefIdxL0DefaultActiveMinus1 = r.ue();
    info.numRefIdxL1DefaultActiveMinus1 = r.ue();
    info.weightedPredFlag = (r.bits(1) != 0);
    info.weightedBipredIdc = r.bits(2);
    r.se();   // pic_init_qp_minus26
    r.se();   // pic_init_qs_minus26
    r.se();   // chroma_qp_index_offset
    info.deblockingFilterControlPresent = (r.bits(1) != 0);
    r.bits(1);  // constrained_intra_pred_flag
    info.redundantPicCntPresent = (r.bits(1) != 0);
    info.valid = true;

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "  PPS parsed: entropy=" << (info.entropyCodingModeFlag ? "CABAC" : "CAVLC")
                 << "deblocking=" << info.deblockingFilterControlPresent
                 << "redundant_pic_cnt=" << info.redundantPicCntPresent
                 << "weighted_pred=" << info.weightedPredFlag
                 << "bottomFieldPicOrder=" << info.bottomFieldPicOrderPresent;
    }
    return info;
}


// pred_weight_table entries of one reference list (H.264 7.3.3.2): per
// entry the luma weight/offset pair and the chroma weight/offset pairs,
// each behind its presence flag.
static void skipPredWeightList(TTBitReader& r, int numRefs)
{
    for (int i = 0; i <= numRefs; i++) {
        if (r.flag()) {
            r.se();  // luma_weight
            r.se();  // luma_offset
        }
        if (r.flag()) {
            for (int k = 0; k < 4; k++)       // cb/cr weight and offset
                r.se();
        }
    }
}

// Skip H.264 HRD parameters in VUI
static void skipHrdParameters(TTBitReader& r)
{
    uint32_t cpb_cnt_minus1 = r.ue();
    // Spec H.264 E.1.2: cpb_cnt_minus1 is in [0,31]. Clamp to bound CPU time.
    if (cpb_cnt_minus1 > 31) cpb_cnt_minus1 = 31;
    r.bits(4);  // bit_rate_scale
    r.bits(4);  // cpb_size_scale
    for (uint32_t i = 0; i <= cpb_cnt_minus1; i++) {
        r.ue();     // bit_rate_value_minus1
        r.ue();     // cpb_size_value_minus1
        r.bits(1);  // cbr_flag
    }
    r.bits(5);  // initial_cpb_removal_delay_length_minus1
    r.bits(5);  // cpb_removal_delay_length_minus1
    r.bits(5);  // dpb_output_delay_length_minus1
    r.bits(5);  // time_offset_length
}

// Patch H.264 SPS NAL to set bitstream_restriction with max_num_reorder_frames.
// Input: SPS NAL data WITH start code prefix.
// Returns patched SPS NAL data WITH start code prefix, or empty on error.
QByteArray ttPatchH264SpsReorderFrames(const QByteArray& spsNal, int maxReorderFrames, bool isPAFF)
{
    // Find and strip start code
    const int startCodeLen = ttStartCodeLength(spsNal);
    if (startCodeLen == 0)
        return QByteArray();  // no start code

    QByteArray nalBody = spsNal.mid(startCodeLen);

    // Verify NAL type = 7 (SPS)
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 7)
        return QByteArray();

    // Remove emulation prevention bytes to get RBSP
    const QByteArray rbsp = ttRbspFromNal(nalBody);
    TTBitReader r(rbsp);
    TTH264SpsHeader h;
    if (!ttParseH264SpsHeader(r, h))
        return QByteArray();                     // over-long POC cycle

    const int maxRefReadPos = h.maxNumRefFramesBitPos;  // bit position of max_num_ref_frames in RBSP
    const uint32_t max_num_ref_frames = h.maxNumRefFrames;
    // PAFF only: increase num_ref_frames to prevent DPB overflow from stale MMCO
    // references at the MBAFF re-encode → PAFF stream-copy transition.
    // Non-PAFF streams keep the original value to avoid DPB layout mismatch.
    const uint32_t patched_max_ref = isPAFF ? qMax(8u, max_num_ref_frames) : max_num_ref_frames;

    int mbAdaptiveBitPos = -1;  // bit position of mb_adaptive_frame_field_flag in RBSP
    if (!h.frameMbsOnly) {
        mbAdaptiveBitPos = r.pos();  // record position before reading
        r.bits(1);  // mb_adaptive_frame_field_flag
    }

    r.bits(1);  // direct_8x8_inference_flag

    if (r.flag()) {  // frame_cropping_flag
        r.ue();  // crop_left
        r.ue();  // crop_right
        r.ue();  // crop_top
        r.ue();  // crop_bottom
    }

    if (!r.flag()) {  // vui_parameters_present_flag
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("SPS patch: no VUI, cannot add bitstream_restriction"));
        return QByteArray();
    }

    // Parse VUI parameters to find bitstream_restriction_flag
    if (r.flag()) {  // aspect_ratio_info_present_flag
        uint32_t aspect_ratio_idc = r.bits(8);
        if (aspect_ratio_idc == 255) {  // Extended_SAR
            r.bits(16);  // sar_width
            r.bits(16);  // sar_height
        }
    }

    if (r.flag())    // overscan_info_present_flag
        r.bits(1);   // overscan_appropriate_flag

    if (r.flag()) {  // video_signal_type_present_flag
        r.bits(3);   // video_format
        r.bits(1);   // video_full_range_flag
        if (r.flag()) {  // colour_description_present_flag
            r.bits(8);   // colour_primaries
            r.bits(8);   // transfer_characteristics
            r.bits(8);   // matrix_coefficients
        }
    }

    if (r.flag()) {  // chroma_loc_info_present_flag
        r.ue();  // chroma_sample_loc_type_top_field
        r.ue();  // chroma_sample_loc_type_bottom_field
    }

    if (r.flag()) {  // timing_info_present_flag
        r.bits(32);  // num_units_in_tick
        r.bits(32);  // time_scale
        r.bits(1);   // fixed_frame_rate_flag
    }

    const bool nal_hrd_present = r.flag();
    if (nal_hrd_present) skipHrdParameters(r);

    const bool vcl_hrd_present = r.flag();
    if (vcl_hrd_present) skipHrdParameters(r);

    if (nal_hrd_present || vcl_hrd_present)
        r.bits(1);  // low_delay_hrd_flag

    r.bits(1);  // pic_struct_present_flag

    // Now at bitstream_restriction_flag position
    const int bsrFlagPos = r.pos();

    // Build new RBSP: copy up to max_num_ref_frames, write patched value,
    // then copy from after max_num_ref_frames to bsrFlagPos, then write new
    // bitstream_restriction section.
    TTBitReader copy(rbsp);
    TTBitWriter w;
    w.copyBits(copy, maxRefReadPos);

    // Write patched max_num_ref_frames (may be different bit-length than original)
    w.ue(patched_max_ref);

    // Skip the original max_num_ref_frames UE value to find where it ends.
    copy.ue();
    const int afterMaxRefPos = copy.pos();

    // Copy bits [afterMaxRefPos..bsrFlagPos) verbatim (gaps_flag, dimensions, VUI etc.)
    w.copyBits(copy, bsrFlagPos - afterMaxRefPos);

    // Write bitstream_restriction_flag = 1
    w.bits(1, 1);

    // Write bitstream_restriction fields
    w.bits(1, 1);                                // motion_vectors_over_pic_boundaries_flag
    w.ue(0);                                     // max_bytes_per_pic_denom
    w.ue(0);                                     // max_bits_per_mb_denom
    w.ue(16);                                    // log2_max_mv_length_horizontal
    w.ue(16);                                    // log2_max_mv_length_vertical
    w.ue(static_cast<uint32_t>(maxReorderFrames));  // max_num_reorder_frames
    // max_dec_frame_buffering: for PAFF, increase to at least 8 to prevent DPB
    // overflow at MBAFF re-encode → PAFF stream-copy transitions where MMCO
    // references non-existent frames. For non-PAFF, use original values.
    uint32_t maxDecBuf = isPAFF
        ? qMax(8u, qMax((uint32_t)maxReorderFrames, max_num_ref_frames))
        : qMax((uint32_t)maxReorderFrames, max_num_ref_frames);
    w.ue(maxDecBuf);                             // max_dec_frame_buffering

    // RBSP stop bit + byte alignment
    w.rbspTrailingBits();
    QByteArray newRbsp = w.data();

    // PAFF→MBAFF: set mb_adaptive_frame_field_flag=1 in the NEW RBSP.
    // Only needed for PAFF streams where the encoder produces MBAFF output.
    // The flag lies before everything appended after it, so patching the
    // finished RBSP is the same as patching it while building.
    if (isPAFF && mbAdaptiveBitPos >= 0) {
        // Calculate the new position: offset by the UE size difference
        int sizeOrigUE = afterMaxRefPos - maxRefReadPos;
        // Count bits of patched_max_ref UE
        int sizePatchedUE = 1;
        { uint32_t tmp = patched_max_ref + 1; while (tmp > 1) { tmp >>= 1; sizePatchedUE += 2; } }
        int newMbAdaptivePos = mbAdaptiveBitPos + (sizePatchedUE - sizeOrigUE);
        uint8_t* writeData = reinterpret_cast<uint8_t*>(newRbsp.data());
        int byteIdx = newMbAdaptivePos / 8;
        int bitIdx = 7 - (newMbAdaptivePos % 8);
        if (byteIdx < newRbsp.size() && !(writeData[byteIdx] & (1 << bitIdx))) {
            ttOverwriteBits(writeData, newRbsp.size(), newMbAdaptivePos, 1, 1);
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "  SPS patched: mb_adaptive_frame_field_flag 0->1 (PAFF->MBAFF signaling)";
        }
    }

    // Re-add emulation prevention bytes
    QByteArray patchedNal = ttNalFromRbsp(newRbsp);

    // Re-add start code
    QByteArray result;
    result.append(spsNal.constData(), startCodeLen);
    result.append(patchedNal);

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "  SPS patched: bitstream_restriction_flag=1, max_num_reorder_frames="
                 << maxReorderFrames << "max_dec_frame_buffering=" << maxDecBuf
                 << "(original" << rbsp.size() << "bytes, patched" << newRbsp.size() << "bytes)";
    }

    return result;
}
