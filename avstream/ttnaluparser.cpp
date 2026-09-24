/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttnaluparser.h"
#include "ttbitstream.h"
#include "tth264syntax.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <QDebug>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

// Start code patterns
static const uint8_t START_CODE_3[3] = {0x00, 0x00, 0x01};
static const uint8_t START_CODE_4[4] = {0x00, 0x00, 0x00, 0x01};

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
TTNaluParser::TTNaluParser()
    : mFileSize(0)
    , mCodecType(NALU_CODEC_UNKNOWN)
    , mMappedFile(nullptr)
    , mIsPAFF(false)
{
}

// ----------------------------------------------------------------------------
// Destructor
// ----------------------------------------------------------------------------
TTNaluParser::~TTNaluParser()
{
    closeFile();
}

// ----------------------------------------------------------------------------
// Open file for parsing
// ----------------------------------------------------------------------------
bool TTNaluParser::openFile(const QString& filePath)
{
    closeFile();

    mFilePath = filePath;
    mFile.setFileName(filePath);

    if (!mFile.open(QIODevice::ReadOnly)) {
        setError(QString("Cannot open file: %1").arg(filePath));
        return false;
    }

    mFileSize = mFile.size();

    // Detect codec type from file extension and content
    if (!detectCodecType()) {
        closeFile();
        return false;
    }

    if (TTSettings::instance()->logAVStream()) {
        qDebug() << "TTNaluParser: Opened" << filePath;
        qDebug() << "  File size:" << mFileSize << "bytes";
        qDebug() << "  Codec:" << codecName();
    }

    return true;
}

// ----------------------------------------------------------------------------
// Close file
// ----------------------------------------------------------------------------
void TTNaluParser::closeFile()
{
    // Unmap memory-mapped file first
    if (mMappedFile && mFile.isOpen()) {
        mFile.unmap(mMappedFile);
        mMappedFile = nullptr;
    }

    if (mFile.isOpen()) {
        mFile.close();
    }

    mNalUnits.clear();
    mAccessUnits.clear();
    mGops.clear();
    mSPSList.clear();
    mPPSList.clear();
    mVPSList.clear();
    mSpsInfoMap.clear();
    mPpsToSpsMap.clear();
    mIsPAFF = false;
    mCodecType = NALU_CODEC_UNKNOWN;
    mFileSize = 0;
}

// ----------------------------------------------------------------------------
// Detect codec type from file extension and content
// ----------------------------------------------------------------------------
bool TTNaluParser::detectCodecType()
{
    QString ext = QFileInfo(mFilePath).suffix().toLower();

    // First try extension
    if (ext == "264" || ext == "h264" || ext == "avc") {
        mCodecType = NALU_CODEC_H264;
    } else if (ext == "265" || ext == "h265" || ext == "hevc") {
        mCodecType = NALU_CODEC_H265;
    } else {
        // Try to detect from content
        // Read first NAL unit and check type
        mFile.seek(0);
        QByteArray header = mFile.read(256);

        // Find first start code
        int pos = 0;
        while (pos < header.size() - 4) {
            if (header[pos] == '\0' && header[pos+1] == '\0') {
                int nalStart = 0;
                if (header[pos+2] == '\x01') {
                    nalStart = pos + 3;
                } else if (header[pos+2] == '\0' && pos + 3 < header.size() && header[pos+3] == '\x01') {
                    nalStart = pos + 4;
                }

                if (nalStart > 0 && nalStart < header.size()) {
                    uint8_t firstByte = static_cast<uint8_t>(header[nalStart]);

                    // H.264: forbidden_zero_bit (1) + nal_ref_idc (2) + nal_unit_type (5)
                    // H.265: forbidden_zero_bit (1) + nal_unit_type (6) + nuh_layer_id (6) + nuh_temporal_id_plus1 (3)

                    // Check if it looks like H.264 SPS (type 7)
                    int h264Type = firstByte & 0x1F;
                    if (h264Type == 7 || h264Type == 8 || h264Type == 5 || h264Type == 1) {
                        mCodecType = NALU_CODEC_H264;
                        break;
                    }

                    // Check if it looks like H.265 VPS (type 32) or SPS (type 33)
                    int h265Type = (firstByte >> 1) & 0x3F;
                    if (h265Type == 32 || h265Type == 33 || h265Type == 34 ||
                        h265Type == 19 || h265Type == 20 || h265Type == 21) {
                        mCodecType = NALU_CODEC_H265;
                        break;
                    }
                }
            }
            pos++;
        }
    }

    if (mCodecType == NALU_CODEC_UNKNOWN) {
        setError("Cannot detect codec type (not H.264 or H.265)");
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Get codec name as string
// ----------------------------------------------------------------------------
QString TTNaluParser::codecName() const
{
    switch (mCodecType) {
        case NALU_CODEC_H264: return "H.264/AVC";
        case NALU_CODEC_H265: return "H.265/HEVC";
        default: return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// Parse entire file
// ----------------------------------------------------------------------------
bool TTNaluParser::parseFile()
{
    if (!mFile.isOpen()) {
        setError("File not open");
        return false;
    }

    mNalUnits.clear();
    mAccessUnits.clear();
    mGops.clear();
    mSPSList.clear();
    mPPSList.clear();
    mVPSList.clear();

    if (TTSettings::instance()->logAVStream())
        qDebug() << "TTNaluParser: Parsing file...";

    // Find all NAL units
    mFile.seek(0);

    int64_t currentPos = 0;
    int64_t startCodePos = 0;
    int startCodeLen = 0;
    int nalCount = 0;

    while (currentPos < mFileSize) {
        if (mAbortCallback && mAbortCallback()) {
            // Deliberately not setError(): a user cancel is not a parse
            // failure and must not log as one (mirrors
            // TTESSmartCut::checkAbort()'s reasoning).
            mLastError = "aborted by user";
            return false;
        }

        int result = findNextStartCode(currentPos, startCodePos, startCodeLen);
        if (result < 0) {
            break;  // No more start codes
        }

        // If we have a previous NAL unit, set its size
        if (nalCount > 0) {
            TTNalUnit& prevNal = mNalUnits[nalCount - 1];
            prevNal.size = startCodePos - prevNal.fileOffset;
            prevNal.dataSize = prevNal.size - (prevNal.dataOffset - prevNal.fileOffset);
        }

        // Parse this NAL unit
        TTNalUnit nal;
        if (parseNalUnit(startCodePos, startCodeLen, nal)) {
            mNalUnits.append(nal);
            nalCount++;

            // Track parameter sets (deduplicated after parse loop when sizes are set)
            if (nal.isSPS) {
                mSPSList.append(nalCount - 1);
                // Parse SPS for PAFF detection (H.264 only)
                // Note: nal.dataSize may be 0 here (set when next NAL is found),
                // so we read a fixed amount from dataOffset directly
                if (mCodecType == NALU_CODEC_H264) {
                    mFile.seek(nal.dataOffset);
                    QByteArray spsData = mFile.read(512);
                    parseH264SpsData(spsData);
                }
            }
            if (nal.isPPS) {
                mPPSList.append(nalCount - 1);
                // Parse PPS for PPS->SPS mapping (H.264 only)
                if (mCodecType == NALU_CODEC_H264) {
                    mFile.seek(nal.dataOffset);
                    QByteArray ppsData = mFile.read(64);
                    parseH264PpsData(ppsData);
                }
            }
            if (nal.isVPS) mVPSList.append(nalCount - 1);

            // Progress output
            if (nalCount % 10000 == 0) {
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "  Parsed" << nalCount << "NAL units...";
            }
        }

        currentPos = startCodePos + startCodeLen;
    }

    // Set size of last NAL unit
    if (nalCount > 0) {
        TTNalUnit& lastNal = mNalUnits[nalCount - 1];
        lastNal.size = mFileSize - lastNal.fileOffset;
        lastNal.dataSize = lastNal.size - (lastNal.dataOffset - lastNal.fileOffset);
    }

    if (TTSettings::instance()->logAVStream()) {
        qDebug() << "  NAL units found:" << nalCount;
        qDebug() << "  SPS:" << mSPSList.size() << ", PPS:" << mPPSList.size() << "(before dedup)";
    }

    // Deduplicate parameter sets (now that all NAL sizes are set)
    deduplicateList(mSPSList);
    deduplicateList(mPPSList);
    deduplicateList(mVPSList);

    if (TTSettings::instance()->logAVStream())
        qDebug() << "  SPS:" << mSPSList.size() << "(unique), PPS:" << mPPSList.size() << "(unique)";
    if (mCodecType == NALU_CODEC_H265) {
        if (TTSettings::instance()->logAVStream())
            qDebug() << "  VPS:" << mVPSList.size() << "(unique)";
    }

    // Build access units (group NALs into frames)
    buildAccessUnits();

    // Build GOP structure
    buildGOPs();

    if (TTSettings::instance()->logAVStream()) {
        qDebug() << "TTNaluParser: Parsing complete";
        qDebug() << "  Access Units (frames):" << mAccessUnits.size();
        qDebug() << "  GOPs:" << mGops.size();
    }

    return true;
}

// ----------------------------------------------------------------------------
// Find next start code (0x000001 or 0x00000001)
// Returns: 0 on success, -1 if no more start codes
// ULTRA-OPTIMIZED: Memory-maps entire file (you have 96GB RAM!)
// ----------------------------------------------------------------------------
int TTNaluParser::findNextStartCode(int64_t startPos, int64_t& codePos, int& codeLen)
{
    // Map entire file on first call (stays mapped until file is closed)
    if (!mMappedFile) {
        mMappedFile = mFile.map(0, mFileSize);

        if (!mMappedFile) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("Could not map entire file, falling back to chunk mode"));
            // Fallback to chunk-based reading
            mFile.seek(startPos);
            QByteArray buffer = mFile.read(qMin((int64_t)(64 * 1024 * 1024), mFileSize - startPos));
            if (buffer.isEmpty()) return -1;

            const char* data = buffer.constData();
            for (int i = 0; i < buffer.size() - 3; i++) {
                if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
                    if (i > 0 && data[i-1] == 0) {
                        codePos = startPos + i - 1;
                        codeLen = 4;
                    } else {
                        codePos = startPos + i;
                        codeLen = 3;
                    }
                    return 0;
                }
            }
            return -1;
        }
        if (TTSettings::instance()->logAVStream())
            qDebug() << "TTNaluParser: Mapped entire file to memory (" << (mFileSize / (1024*1024)) << "MB)";
    }

    // Direct memory search - FAST!
    const uchar* data = mMappedFile;
    int64_t end = mFileSize - 3;

    for (int64_t i = startPos; i < end; i++) {
        // Fast skip: most bytes are not 0
        if (data[i] != 0) continue;

        // Check for 3-byte start code: 0x000001
        if (data[i+1] == 0 && data[i+2] == 1) {
            // Check if it's actually a 4-byte start code: 0x00000001
            if (i > 0 && data[i-1] == 0) {
                codePos = i - 1;
                codeLen = 4;
            } else {
                codePos = i;
                codeLen = 3;
            }
            return 0;
        }
    }

    return -1;  // No more start codes
}

// ----------------------------------------------------------------------------
// Parse a single NAL unit at given offset
// ----------------------------------------------------------------------------
bool TTNaluParser::parseNalUnit(int64_t offset, int startCodeLen, TTNalUnit& nal)
{
    nal.fileOffset = offset;
    nal.dataOffset = offset + startCodeLen;
    nal.size = 0;  // Will be set later
    nal.dataSize = 0;

    // Initialize flags
    nal.isKeyframe = false;
    nal.isIDR = false;
    nal.isSlice = false;
    nal.isSPS = false;
    nal.isPPS = false;
    nal.isVPS = false;
    nal.isSEI = false;
    nal.isFiller = false;
    nal.isAUD = false;

    nal.sliceType = -1;
    nal.frameNum = -1;
    nal.poc = -1;
    nal.firstMbInSlice = -1;
    nal.ppsId = -1;
    nal.isField = false;
    nal.isBottomField = false;

    // Read NAL header (first 1-2 bytes after start code)
    mFile.seek(nal.dataOffset);
    QByteArray header = mFile.read(128);  // Read enough for slice header (4K first_mb_in_slice needs >32 bytes)

    if (header.isEmpty()) {
        return false;
    }

    if (mCodecType == NALU_CODEC_H264) {
        return parseH264NalUnit(header, nal);
    } else if (mCodecType == NALU_CODEC_H265) {
        return parseH265NalUnit(header, nal);
    }

    return false;
}

// ----------------------------------------------------------------------------
// Parse H.264 NAL unit header
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH264NalUnit(const QByteArray& data, TTNalUnit& nal)
{
    if (data.isEmpty()) return false;

    uint8_t firstByte = static_cast<uint8_t>(data[0]);

    // H.264 NAL header: forbidden_zero_bit (1) + nal_ref_idc (2) + nal_unit_type (5)
    // int forbiddenBit = (firstByte >> 7) & 0x01;
    nal.refIdc = (firstByte >> 5) & 0x03;
    nal.type = firstByte & 0x1F;
    nal.temporalId = 0;  // H.264 doesn't have temporal_id in NAL header

    // Categorize NAL type
    switch (nal.type) {
        case H264::NAL_SLICE:
            nal.isSlice = true;
            break;
        case H264::NAL_IDR_SLICE:
            nal.isSlice = true;
            nal.isKeyframe = true;
            nal.isIDR = true;  // True IDR - safe for random access
            break;
        case H264::NAL_SEI:
            nal.isSEI = true;
            break;
        case H264::NAL_SPS:
            nal.isSPS = true;
            break;
        case H264::NAL_PPS:
            nal.isPPS = true;
            break;
        case H264::NAL_AUD:
            nal.isAUD = true;
            break;
        case H264::NAL_FILLER:
            nal.isFiller = true;
            break;
    }

    // Parse slice header for additional info
    if (nal.isSlice && data.size() > 1) {
        parseH264SliceHeader(data, nal);

        // Also mark I-slices as keyframes for GOP detection
        // Many streams use non-IDR I-frames (Open GOPs)
        if (nal.sliceType == H264::SLICE_I || nal.sliceType == H264::SLICE_I_ALL) {
            nal.isKeyframe = true;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse H.264 SPS to extract fields needed for PAFF detection
// H.264 spec Table 7-3: Sequence Parameter Set RBSP syntax
// We need: sps_id, log2_max_frame_num_minus4, frame_mbs_only_flag
// ----------------------------------------------------------------------------
// Remove emulation prevention bytes (00 00 03 -> 00 00) to recover RBSP from
// a NAL unit body. Required before parsing any field that lives past the
// first ~3 bytes of the NAL, since 00 00 03 escapes can otherwise shift the
// bit position and produce wrong field values.
int TTNaluParser::findStartCodePayload(const uint8_t* data, int size, int from)
{
    for (int pos = from < 0 ? 0 : from; pos + 2 < size; ++pos) {
        if (data[pos] != 0 || data[pos + 1] != 0) continue;
        int s = -1;
        if (data[pos + 2] == 1) s = pos + 3;
        else if (data[pos + 2] == 0 && pos + 3 < size && data[pos + 3] == 1) s = pos + 4;
        if (s >= 0 && s < size) return s;
    }
    return -1;
}

int TTNaluParser::findH264SlicePayload(const uint8_t* data, int size)
{
    if (!data || size <= 0) return -1;
    auto isSlice = [](uint8_t header) {
        const uint8_t nalType = header & 0x1F;
        return nalType == H264::NAL_SLICE || nalType == H264::NAL_IDR_SLICE;
    };
    for (int s = findStartCodePayload(data, size, 0); s >= 0;
         s = findStartCodePayload(data, size, s)) {
        if (isSlice(data[s])) return s;
    }
    return isSlice(data[0]) ? 0 : -1;
}

void TTNaluParser::parseH264SpsData(const QByteArray& rawNal)
{
    // Strip emulation-prevention bytes before parsing — scaling lists and
    // VUI HRD parameters can extend past EP escapes, and reading them
    // bit-aligned without stripping shifts every following field.
    const QByteArray data = ttRbspFromNal(rawNal);
    if (data.size() < 5) return;

    TTBitReader r(data);
    TTH264SpsHeader h;
    if (!ttParseH264SpsHeader(r, h)) return;     // cycle > 256: store nothing (as before)

    TTSpsInfo info;
    info.spsId = static_cast<int>(h.spsId);
    info.log2MaxFrameNumMinus4 = h.log2MaxFrameNumMinus4;
    info.frameMbsOnlyFlag = h.frameMbsOnly;
    mSpsInfoMap[info.spsId] = info;

    if (!h.frameMbsOnly) {
        if (TTSettings::instance()->logAVStream())
            qDebug() << "  SPS" << info.spsId << ": frame_mbs_only_flag=0 (may contain field pictures)"
                     << "log2_max_frame_num_minus4=" << h.log2MaxFrameNumMinus4;
    }
}

// ----------------------------------------------------------------------------
// Parse H.264 PPS to extract PPS ID -> SPS ID mapping
// ----------------------------------------------------------------------------
void TTNaluParser::parseH264PpsData(const QByteArray& data)
{
    if (data.size() < 2) return;

    TTBitReader r(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    r.setPos(8);  // Skip NAL header byte

    int ppsId = static_cast<int>(r.ue());
    int spsId = static_cast<int>(r.ue());

    mPpsToSpsMap[ppsId] = spsId;
}

// ----------------------------------------------------------------------------
// Parse H.264 slice header
// Extracts: first_mb_in_slice, slice_type, pps_id, frame_num,
//           field_pic_flag, bottom_field_flag (for PAFF detection)
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH264SliceHeader(const QByteArray& data, TTNalUnit& nal)
{
    if (data.size() < 3) return false;

    TTBitReader r(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    r.setPos(8);  // Skip NAL header byte

    // first_mb_in_slice (ue(v))
    nal.firstMbInSlice = static_cast<int>(r.ue());

    // slice_type (ue(v))
    uint32_t sliceType = r.ue();
    // Normalize slice type (0-4 and 5-9 mean the same thing)
    if (sliceType > 4) sliceType -= 5;
    nal.sliceType = static_cast<int>(sliceType);

    // pic_parameter_set_id (ue(v))
    nal.ppsId = static_cast<int>(r.ue());

    // Look up SPS via PPS -> SPS chain for frame_num bit-width and field info
    int spsId = mPpsToSpsMap.value(nal.ppsId, -1);
    if (spsId < 0 || !mSpsInfoMap.contains(spsId)) {
        return true;  // Can't parse further without SPS info -- still valid
    }

    const TTSpsInfo& sps = mSpsInfoMap[spsId];

    // frame_num -- u(log2_max_frame_num_minus4 + 4) bits
    int frameNumBits = sps.log2MaxFrameNumMinus4 + 4;
    nal.frameNum = static_cast<int>(r.bits(frameNumBits));

    // field_pic_flag -- only present if frame_mbs_only_flag == 0
    if (!sps.frameMbsOnlyFlag) {
        nal.isField = (r.bits(1) == 1);
        if (nal.isField) {
            nal.isBottomField = (r.bits(1) == 1);
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse H.265 NAL unit header
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH265NalUnit(const QByteArray& data, TTNalUnit& nal)
{
    if (data.size() < 2) return false;

    uint8_t byte0 = static_cast<uint8_t>(data[0]);
    uint8_t byte1 = static_cast<uint8_t>(data[1]);

    // H.265 NAL header (2 bytes):
    // forbidden_zero_bit (1) + nal_unit_type (6) + nuh_layer_id (6) + nuh_temporal_id_plus1 (3)
    // int forbiddenBit = (byte0 >> 7) & 0x01;
    nal.type = (byte0 >> 1) & 0x3F;
    nal.refIdc = ((byte0 & 0x01) << 5) | ((byte1 >> 3) & 0x1F);  // nuh_layer_id
    nal.temporalId = (byte1 & 0x07) - 1;  // nuh_temporal_id_plus1 - 1

    // Categorize NAL type
    switch (nal.type) {
        case H265::NAL_TRAIL_N:
        case H265::NAL_TRAIL_R:
        case H265::NAL_TSA_N:
        case H265::NAL_TSA_R:
        case H265::NAL_STSA_N:
        case H265::NAL_STSA_R:
        case H265::NAL_RADL_N:
        case H265::NAL_RADL_R:
        case H265::NAL_RASL_N:
        case H265::NAL_RASL_R:
            nal.isSlice = true;
            break;

        case H265::NAL_BLA_W_LP:
        case H265::NAL_BLA_W_RADL:
        case H265::NAL_BLA_N_LP:
            nal.isSlice = true;
            nal.isKeyframe = true;
            nal.isIDR = true;  // BLA frames are safe for random access
            break;
        case H265::NAL_IDR_W_RADL:
        case H265::NAL_IDR_N_LP:
            nal.isSlice = true;
            nal.isKeyframe = true;
            nal.isIDR = true;  // True IDR - safe for random access
            break;
        case H265::NAL_CRA_NUT:
            nal.isSlice = true;
            nal.isKeyframe = true;
            // CRA is not marked as IDR because RASL pictures may depend on previous frames
            break;

        case H265::NAL_VPS:
            nal.isVPS = true;
            break;
        case H265::NAL_SPS:
            nal.isSPS = true;
            break;
        case H265::NAL_PPS:
            nal.isPPS = true;
            break;
        case H265::NAL_AUD:
            nal.isAUD = true;
            break;
        case H265::NAL_FD:
            nal.isFiller = true;
            break;
        case H265::NAL_PREFIX_SEI:
        case H265::NAL_SUFFIX_SEI:
            nal.isSEI = true;
            break;
    }

    // Parse slice header for additional info
    if (nal.isSlice && data.size() > 2) {
        parseH265SliceHeader(data, nal);

        // Also mark I-slices as keyframes for GOP detection
        if (nal.sliceType == H265::SLICE_I) {
            nal.isKeyframe = true;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse H.265 slice header (basic info only)
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH265SliceHeader(const QByteArray& data, TTNalUnit& nal)
{
    if (data.size() < 4) return false;

    TTBitReader r(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    r.setPos(16);  // Skip 2-byte NAL header

    // first_slice_segment_in_pic_flag (1 bit)
    uint32_t firstSliceFlag = r.bits(1);
    nal.firstMbInSlice = (firstSliceFlag == 1) ? 0 : -1;

    // For BLA/IDR/CRA: no_output_of_prior_pics_flag (1 bit)
    if (nal.type >= H265::NAL_BLA_W_LP && nal.type <= H265::NAL_CRA_NUT) {
        r.bits(1);  // no_output_of_prior_pics_flag
    }

    // slice_pic_parameter_set_id (ue(v))
    nal.ppsId = static_cast<int>(r.ue());

    // For first slice in picture, we can read slice_type directly from the bitstream.
    // For dependent slices (firstSliceFlag == 0), we'd need PPS data we don't have,
    // so fall back to NAL-type heuristic.
    if (firstSliceFlag == 1) {
        // HEVC spec: slice_type is ue(v) right after slice_pic_parameter_set_id
        // (assuming num_extra_slice_header_bits == 0, which is standard for DVB)
        uint32_t sliceType = r.ue();
        if (sliceType <= 2) {
            nal.sliceType = static_cast<int>(sliceType);
        } else {
            // Invalid value — fall back to heuristic
            if (nal.isKeyframe)
                nal.sliceType = H265::SLICE_I;
            else
                nal.sliceType = H265::SLICE_P;
        }
    } else {
        // Dependent slice — heuristic based on NAL type
        if (nal.isKeyframe) {
            nal.sliceType = H265::SLICE_I;
        } else if (nal.type == H265::NAL_TRAIL_N || nal.type == H265::NAL_TSA_N ||
                   nal.type == H265::NAL_STSA_N || nal.type == H265::NAL_RADL_N ||
                   nal.type == H265::NAL_RASL_N) {
            nal.sliceType = H265::SLICE_B;
        } else {
            nal.sliceType = H265::SLICE_P;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Build Access Units from NAL units
// An Access Unit is a set of NAL units that form one picture/frame
// ----------------------------------------------------------------------------
void TTNaluParser::buildAccessUnits()
{
    mAccessUnits.clear();

    if (mNalUnits.isEmpty()) return;

    TTAccessUnit currentAU;
    currentAU.index = 0;
    currentAU.decodeIndex = 0;
    currentAU.isKeyframe = false;
    currentAU.isIDR = false;
    currentAU.sliceType = -1;
    currentAU.poc = -1;
    currentAU.gopIndex = 0;
    currentAU.isFieldCoded = false;

    int auCount = 0;
    int currentGop = 0;

    for (int i = 0; i < mNalUnits.size(); i++) {
        const TTNalUnit& nal = mNalUnits[i];

        // Check for AU boundary
        // An AU starts with:
        // - AUD (Access Unit Delimiter)
        // - Or a slice NAL where first_mb_in_slice == 0 (first slice of picture)
        // - Or parameter sets followed by a slice

        bool isAUStart = false;

        if (nal.isAUD) {
            isAUStart = true;
        } else if (nal.isSlice && nal.firstMbInSlice == 0) {
            // First slice of a new picture
            if (!currentAU.nalIndices.isEmpty()) {
                // Check if we already have slices in current AU
                bool hasSlice = false;
                for (int idx : currentAU.nalIndices) {
                    if (mNalUnits[idx].isSlice) {
                        hasSlice = true;
                        break;
                    }
                }
                if (hasSlice) {
                    isAUStart = true;
                }
            }
        }

        if (isAUStart && !currentAU.nalIndices.isEmpty()) {
            // Save current AU
            currentAU.startOffset = mNalUnits[currentAU.nalIndices.first()].fileOffset;
            int lastIdx = currentAU.nalIndices.last();
            currentAU.endOffset = mNalUnits[lastIdx].fileOffset + mNalUnits[lastIdx].size;
            currentAU.decodeIndex = auCount;

            mAccessUnits.append(currentAU);
            auCount++;

            // Start new AU
            currentAU.index = auCount;
            currentAU.nalIndices.clear();
            currentAU.isKeyframe = false;
            currentAU.isIDR = false;
            currentAU.sliceType = -1;
            currentAU.poc = -1;
            currentAU.isFieldCoded = false;

            // Check for new GOP
            if (nal.isKeyframe) {
                currentGop++;
            }
            currentAU.gopIndex = currentGop;
        }

        // Add NAL to current AU
        currentAU.nalIndices.append(i);

        // Update AU properties from slice info
        if (nal.isKeyframe) {
            currentAU.isKeyframe = true;
            currentAU.gopIndex = currentGop;
        }
        if (nal.isIDR) {
            currentAU.isIDR = true;
        }
        if (nal.isSlice && currentAU.sliceType < 0) {
            currentAU.sliceType = nal.sliceType;
        }
    }

    // Save last AU
    if (!currentAU.nalIndices.isEmpty()) {
        currentAU.startOffset = mNalUnits[currentAU.nalIndices.first()].fileOffset;
        int lastIdx = currentAU.nalIndices.last();
        currentAU.endOffset = mNalUnits[lastIdx].fileOffset + mNalUnits[lastIdx].size;
        currentAU.decodeIndex = auCount;
        mAccessUnits.append(currentAU);
    }

    if (TTSettings::instance()->logAVStream())
        qDebug() << "  Built" << mAccessUnits.size() << "access units";

    mergeFieldPairsPAFF();
}

// First slice NAL of an access unit, or nullptr.
const TTNalUnit* TTNaluParser::firstSliceNal(const TTAccessUnit& au) const
{
    for (int idx : au.nalIndices) {
        if (mNalUnits[idx].isSlice) return &mNalUnits[idx];
    }
    return nullptr;
}

// First field-coded slice NAL of an access unit, or nullptr.
const TTNalUnit* TTNaluParser::firstFieldSliceNal(const TTAccessUnit& au) const
{
    for (int idx : au.nalIndices) {
        if (mNalUnits[idx].isSlice && mNalUnits[idx].isField) return &mNalUnits[idx];
    }
    return nullptr;
}

// Pass 2 of buildAccessUnits(): merge H.264 PAFF field pairs (top field AU
// followed by the bottom field AU with the same frame_num) into one frame
// AU, renumber, and set mIsPAFF accordingly.
void TTNaluParser::mergeFieldPairsPAFF()
{
    bool hasFieldSlices = false;
    if (mCodecType == NALU_CODEC_H264) {
        bool spsAllowsFields = false;
        for (auto it = mSpsInfoMap.constBegin(); it != mSpsInfoMap.constEnd(); ++it) {
            if (!it.value().frameMbsOnlyFlag) {
                spsAllowsFields = true;
                break;
            }
        }

        if (spsAllowsFields) {
            int mergeCount = 0;
            int i = 0;
            while (i < mAccessUnits.size() - 1) {
                TTAccessUnit& topAU = mAccessUnits[i];
                TTAccessUnit& botAU = mAccessUnits[i + 1];

                const TTNalUnit* topSlice = firstSliceNal(topAU);
                const TTNalUnit* botSlice = firstSliceNal(botAU);
                bool topIsField = topSlice ? topSlice->isField : false;
                bool botIsField = botSlice ? botSlice->isField : false;
                int topFrameNum = topSlice ? topSlice->frameNum : -1;
                int botFrameNum = botSlice ? botSlice->frameNum : -1;

                const TTNalUnit* topField = firstFieldSliceNal(topAU);
                const TTNalUnit* botField = firstFieldSliceNal(botAU);
                bool topIsTop = topField ? !topField->isBottomField : false;
                bool botIsBot = botField ? botField->isBottomField : false;

                if (topIsField && botIsField && topIsTop && botIsBot &&
                    topFrameNum >= 0 && topFrameNum == botFrameNum) {
                    topAU.nalIndices.append(botAU.nalIndices);
                    topAU.endOffset = botAU.endOffset;
                    topAU.isFieldCoded = true;
                    hasFieldSlices = true;

                    if (botAU.isKeyframe) topAU.isKeyframe = true;
                    if (botAU.isIDR) topAU.isIDR = true;

                    mAccessUnits.removeAt(i + 1);
                    mergeCount++;
                } else {
                    i++;
                }
            }

            if (mergeCount > 0) {
                for (int j = 0; j < mAccessUnits.size(); j++) {
                    mAccessUnits[j].index = j;
                    mAccessUnits[j].decodeIndex = j;
                }
                mIsPAFF = true;
                if (TTSettings::instance()->logAVStream())
                    qDebug() << "  PAFF detected: merged" << mergeCount << "field pairs"
                             << "-> " << mAccessUnits.size() << "frames";
            }
        }
    }

    if (!hasFieldSlices) {
        mIsPAFF = false;
    }
}

// ----------------------------------------------------------------------------
// Build GOP structure
// ----------------------------------------------------------------------------
void TTNaluParser::buildGOPs()
{
    mGops.clear();

    if (mAccessUnits.isEmpty()) return;

    TTGopInfo currentGop;
    currentGop.index = 0;
    currentGop.startAU = 0;
    currentGop.keyframeAU = -1;
    currentGop.frameCount = 0;
    currentGop.isClosed = true;

    for (int i = 0; i < mAccessUnits.size(); i++) {
        const TTAccessUnit& au = mAccessUnits[i];

        if (au.isKeyframe && i > 0) {
            // End current GOP
            currentGop.endAU = i - 1;
            currentGop.frameCount = currentGop.endAU - currentGop.startAU + 1;
            mGops.append(currentGop);

            // Start new GOP
            currentGop.index++;
            currentGop.startAU = i;
            currentGop.keyframeAU = i;
            currentGop.isClosed = true;
        }

        if (au.isKeyframe) {
            currentGop.keyframeAU = i;
        }
    }

    // End last GOP
    currentGop.endAU = mAccessUnits.size() - 1;
    currentGop.frameCount = currentGop.endAU - currentGop.startAU + 1;
    mGops.append(currentGop);

    if (TTSettings::instance()->logAVStream())
        qDebug() << "  Built" << mGops.size() << "GOPs";
}

// ----------------------------------------------------------------------------
// Get NAL unit at index
// ----------------------------------------------------------------------------
TTNalUnit TTNaluParser::nalUnitAt(int index) const
{
    if (index >= 0 && index < mNalUnits.size()) {
        return mNalUnits[index];
    }
    return TTNalUnit();
}

// ----------------------------------------------------------------------------
// Read NAL data (without start code)
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::readNalData(int index)
{
    if (index < 0 || index >= mNalUnits.size()) {
        return QByteArray();
    }

    const TTNalUnit& nal = mNalUnits[index];
    mFile.seek(nal.dataOffset);
    return mFile.read(nal.dataSize);
}

// ----------------------------------------------------------------------------
// Read NAL data with start code
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::readNalDataWithStartCode(int index)
{
    if (index < 0 || index >= mNalUnits.size()) {
        return QByteArray();
    }

    const TTNalUnit& nal = mNalUnits[index];
    mFile.seek(nal.fileOffset);
    return mFile.read(nal.size);
}

// ----------------------------------------------------------------------------
// Remove duplicate entries from a parameter set list (SPS/PPS/VPS).
// DVB streams repeat SPS/PPS before every I-slice, producing thousands of
// identical parameter sets. Without deduplication, writeParameterSets()
// writes all of them (e.g. 5705 SPS + 17115 PPS = 639KB for a 93-min file).
// Must be called after parsing is complete (all NAL sizes are set).
// ----------------------------------------------------------------------------
void TTNaluParser::deduplicateList(QList<int>& list)
{
    // QSet<QByteArray> hashes the data for O(1) average lookup; the previous
    // O(n^2) linear scan blew up on DVB streams that repeat SPS/PPS before
    // every I-slice (e.g. ~5700 SPS in a 93-minute file).
    QList<int> unique;
    QSet<QByteArray> seen;

    for (int nalIdx : list) {
        QByteArray data = readNalDataWithStartCode(nalIdx);
        if (data.isEmpty())
            continue;

        if (!seen.contains(data)) {
            unique.append(nalIdx);
            seen.insert(data);
        }
    }
    list = unique;
}

// ----------------------------------------------------------------------------
// Get Access Unit at index
// ----------------------------------------------------------------------------
TTAccessUnit TTNaluParser::accessUnitAt(int index) const
{
    if (index >= 0 && index < mAccessUnits.size()) {
        return mAccessUnits[index];
    }
    return TTAccessUnit();
}

// ----------------------------------------------------------------------------
// Read all NAL data for an Access Unit
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::readAccessUnitData(int index)
{
    if (index < 0 || index >= mAccessUnits.size()) {
        return QByteArray();
    }

    const TTAccessUnit& au = mAccessUnits[index];
    mFile.seek(au.startOffset);
    return mFile.read(au.endOffset - au.startOffset);
}

// ----------------------------------------------------------------------------
// Zero-copy pointer to Access Unit data via mmap
// Returns nullptr if file is not memory-mapped or index is invalid
// ----------------------------------------------------------------------------
const uchar* TTNaluParser::accessUnitPtr(int index, int64_t& size) const
{
    if (!mMappedFile || index < 0 || index >= mAccessUnits.size()) {
        size = 0;
        return nullptr;
    }

    const TTAccessUnit& au = mAccessUnits[index];
    size = au.endOffset - au.startOffset;
    return mMappedFile + au.startOffset;
}

// ----------------------------------------------------------------------------
// Get SPS data
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::getSPS(int index) const
{
    if (index < 0 || index >= mSPSList.size()) {
        return QByteArray();
    }
    return const_cast<TTNaluParser*>(this)->readNalDataWithStartCode(mSPSList[index]);
}

// ----------------------------------------------------------------------------
// Get PPS data
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::getPPS(int index) const
{
    if (index < 0 || index >= mPPSList.size()) {
        return QByteArray();
    }
    return const_cast<TTNaluParser*>(this)->readNalDataWithStartCode(mPPSList[index]);
}

// ----------------------------------------------------------------------------
// Get VPS data (H.265 only)
// ----------------------------------------------------------------------------
QByteArray TTNaluParser::getVPS(int index) const
{
    if (index < 0 || index >= mVPSList.size()) {
        return QByteArray();
    }
    return const_cast<TTNaluParser*>(this)->readNalDataWithStartCode(mVPSList[index]);
}

// ----------------------------------------------------------------------------
// Find keyframe before given AU index
// ----------------------------------------------------------------------------
int TTNaluParser::findKeyframeBefore(int auIndex) const
{
    for (int i = auIndex; i >= 0; i--) {
        if (mAccessUnits[i].isKeyframe) {
            return i;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Find keyframe after given AU index
// ----------------------------------------------------------------------------
int TTNaluParser::findKeyframeAfter(int auIndex) const
{
    for (int i = auIndex; i < mAccessUnits.size(); i++) {
        if (mAccessUnits[i].isKeyframe) {
            return i;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Find IDR frame after given AU index
// IDR frames are true random access points, safe for stream-copy start
// ----------------------------------------------------------------------------
int TTNaluParser::findIDRAfter(int auIndex) const
{
    for (int i = auIndex; i < mAccessUnits.size(); i++) {
        if (mAccessUnits[i].isIDR) {
            return i;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Find GOP for given AU index
// ----------------------------------------------------------------------------
int TTNaluParser::findGopForAU(int auIndex) const
{
    if (auIndex < 0 || auIndex >= mAccessUnits.size()) {
        return -1;
    }
    return mAccessUnits[auIndex].gopIndex;
}

// ----------------------------------------------------------------------------
// Get GOP at index
// ----------------------------------------------------------------------------
TTGopInfo TTNaluParser::gopAt(int index) const
{
    if (index >= 0 && index < mGops.size()) {
        return mGops[index];
    }
    return TTGopInfo();
}

// ----------------------------------------------------------------------------
// Compute B-frame reorder delay from first GOP structure
// Returns the number of B-frames between reference frames in decode order.
// This equals the decoder's reorder delay for display-order output.
// ----------------------------------------------------------------------------
int TTNaluParser::computeReorderDelay() const
{
    if (mGops.isEmpty() || mAccessUnits.size() < 3) return 0;

    // Scan up to 20 GOPs to find the max consecutive B-frames.
    // GOP[0] may be a degenerate recording-start fragment without B-frames,
    // so we check multiple GOPs and take the maximum.
    int maxConsecutiveB = 0;
    int gopsChecked = qMin(mGops.size(), 20);

    for (int g = 0; g < gopsChecked; g++) {
        const TTGopInfo& gop = mGops[g];
        int consecutiveB = 0;

        for (int i = gop.startAU; i <= gop.endAU && i < mAccessUnits.size(); i++) {
            const TTAccessUnit& au = mAccessUnits[i];
            bool isB = false;
            if (mCodecType == NALU_CODEC_H265) {
                isB = (au.sliceType == H265::SLICE_B);
            } else {
                isB = (au.sliceType == H264::SLICE_B ||
                       au.sliceType == H264::SLICE_B_ALL);
            }

            if (isB) {
                consecutiveB++;
                if (consecutiveB > maxConsecutiveB)
                    maxConsecutiveB = consecutiveB;
            } else {
                consecutiveB = 0;
            }
        }
    }

    if (TTSettings::instance()->logAVStream())
        qDebug() << "TTNaluParser: max consecutive B-frames in first" << gopsChecked
                 << "GOPs:" << maxConsecutiveB << "-> reorder delay:" << maxConsecutiveB;
    return maxConsecutiveB;
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTNaluParser::setError(const QString& error)
{
    ttSetLastError(mLastError, __FILE__, __LINE__, "TTNaluParser", error);
}

// ----------------------------------------------------------------------------
// The two SPS fields the PAFF handling needs (see the declaration).
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH264SpsBasics(const uint8_t* data, int size, H264SpsBasics& out)
{
    if (!data || size < 5) return false;

    int nalStart = -1;
    for (int s = findStartCodePayload(data, size, 0); s >= 0; s = findStartCodePayload(data, size, s)) {
        if ((data[s] & 0x1F) == 7) { nalStart = s; break; }
    }
    if (nalStart < 0) return false;

    // Emulation-prevention bytes before frame_mbs_only_flag shift every
    // later field (possible through large poc_type 1 offsets), so read the
    // de-escaped NAL - as parseH264SpsData does.
    const int next = findStartCodePayload(data, size, nalStart);
    const int nalEnd = next >= 0 ? next - 3 : size;
    const QByteArray rbsp = ttRbspFromNal(QByteArray::fromRawData(
        reinterpret_cast<const char*>(data + nalStart), nalEnd - nalStart));
    if (rbsp.size() < 5) return false;

    TTBitReader r(rbsp);
    TTH264SpsHeader h;
    const bool complete = ttParseH264SpsHeader(r, h);
    if (h.log2MaxFrameNumMinus4 < 0) return false;   // SPS ends before log2_max_frame_num
    out.log2MaxFrameNum = h.log2MaxFrameNumMinus4 + 4;
    if (!complete) return true;          // log2MaxFrameNum is valid, the rest is not read
    out.frameMbsOnlyFlag     = h.frameMbsOnly;
    out.haveFrameMbsOnlyFlag = true;
    return true;
}

void TTNaluParser::parseH264SliceFieldInfo(const uint8_t* nal, int nalSize, int log2MaxFrameNum,
                                           int& frameNum, bool& isField, bool& isBottomField)
{
    TTBitReader r(nal, nalSize);
    r.setPos(8);   // past the NAL header byte
    r.ue();   // first_mb_in_slice
    r.ue();   // slice_type
    r.ue();   // pic_parameter_set_id
    frameNum      = static_cast<int>(r.bits(log2MaxFrameNum));
    isField       = (r.bits(1) == 1);          // field_pic_flag
    isBottomField = isField && (r.bits(1) == 1); // bottom_field_flag
}

// ----------------------------------------------------------------------------
// Parse H.264 slice_type from raw packet data (e.g. AVPacket->data)
// Scans for first VCL NAL unit (type 1 or 5), parses slice header to extract
// slice_type. H.264 slice header: 1-byte NAL header, first_mb_in_slice (ue),
// slice_type (ue). Values 0-4 map to P/B/I/SP/SI, values 5-9 are same +5.
// Returns: H264::SLICE_P (0), H264::SLICE_B (1), H264::SLICE_I (2), or -1
// ----------------------------------------------------------------------------
int TTNaluParser::parseH264SliceTypeFromPacket(const uint8_t* data, int size)
{
    if (!data || size < 4) return -1;

    // Scan for Annex-B start code
    int pos = 0;
    int nalStart = -1;

    while (pos < size - 3) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            if (data[pos+2] == 1) {
                nalStart = pos + 3;
            } else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) {
                nalStart = pos + 4;
            }

            if (nalStart >= 0 && nalStart < size) {
                // H.264 NAL header: 1 byte (forbidden_zero_bit, nal_ref_idc, nal_unit_type)
                uint8_t nalType = data[nalStart] & 0x1F;

                // VCL NAL types: 1 (non-IDR slice), 5 (IDR slice)
                if (nalType == H264::NAL_SLICE || nalType == H264::NAL_IDR_SLICE) {
                    const uint8_t* nalData = data + nalStart;
                    int nalDataSize = size - nalStart;

                    if (nalDataSize < 3) return -1;
                    TTBitReader r(nalData, nalDataSize);
                    r.setPos(8);  // Skip 1-byte NAL header

                    // first_mb_in_slice (ue(v)) — skip
                    r.ue();

                    // slice_type (ue(v))
                    if (r.pos() / 8 + 1 >= nalDataSize) return -1;
                    uint32_t sliceType = r.ue();

                    // Normalize: values 5-9 are same as 0-4
                    if (sliceType >= 5 && sliceType <= 9)
                        sliceType -= 5;

                    if (sliceType <= 4)
                        return static_cast<int>(sliceType);
                    return -1;
                }
            }
            nalStart = -1;
        }
        pos++;
    }

    // No start code found — try raw NAL data
    if (size >= 3) {
        uint8_t nalType = data[0] & 0x1F;
        if (nalType == H264::NAL_SLICE || nalType == H264::NAL_IDR_SLICE) {
            TTBitReader r(data, size);
            r.setPos(8);
            r.ue();  // first_mb_in_slice
            if (r.pos() / 8 + 1 >= size) return -1;
            uint32_t sliceType = r.ue();
            if (sliceType >= 5 && sliceType <= 9)
                sliceType -= 5;
            if (sliceType <= 4)
                return static_cast<int>(sliceType);
        }
    }

    return -1;
}

// ----------------------------------------------------------------------------
// Parse HEVC slice_type from raw packet data (e.g. AVPacket->data)
// Scans for first VCL NAL unit (type 0-31), parses slice header to extract
// slice_type. Works with both Annex-B (start codes) and raw NAL packets.
// Returns: H265::SLICE_B (0), H265::SLICE_P (1), H265::SLICE_I (2), or -1
// ----------------------------------------------------------------------------
int TTNaluParser::parseH265SliceTypeFromPacket(const uint8_t* data, int size)
{
    if (!data || size < 6) return -1;

    // Scan for Annex-B start code (00 00 01 or 00 00 00 01)
    int pos = 0;
    int nalStart = -1;

    while (pos < size - 5) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            if (data[pos+2] == 1) {
                nalStart = pos + 3;
            } else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) {
                nalStart = pos + 4;
            }

            if (nalStart >= 0 && nalStart + 2 < size) {
                // Parse 2-byte HEVC NAL header
                uint8_t nalType = (data[nalStart] >> 1) & 0x3F;

                // VCL NAL types are 0-31
                if (nalType <= 31) {
                    // Found first VCL NAL — parse slice header
                    const uint8_t* nalData = data + nalStart;
                    int nalDataSize = size - nalStart;

                    // Need at least a few bytes for slice header
                    if (nalDataSize < 4) return -1;

                    TTBitReader r(nalData, nalDataSize);
                    r.setPos(16);  // Skip 2-byte NAL header

                    // first_slice_segment_in_pic_flag (1 bit)
                    uint32_t firstSliceFlag = r.bits(1);

                    // For BLA/IDR/CRA: no_output_of_prior_pics_flag (1 bit)
                    if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT) {
                        r.bits(1);
                    }

                    if (firstSliceFlag != 1) {
                        // Dependent slice — need PPS info we don't have
                        // Fall back to NAL-type heuristic
                        if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT)
                            return H265::SLICE_I;
                        return -1;
                    }

                    // slice_pic_parameter_set_id (ue(v))
                    r.ue();

                    // slice_type (ue(v))
                    // Guard: make sure we have enough data
                    if (r.pos() / 8 + 2 >= nalDataSize) return -1;

                    uint32_t sliceType = r.ue();
                    if (sliceType <= 2) {
                        return static_cast<int>(sliceType);
                    }
                    return -1;  // Invalid slice_type
                }
            }
            // Reset and continue scanning
            nalStart = -1;
        }
        pos++;
    }

    // No start code found — try interpreting as raw NAL data (no Annex-B framing)
    if (size >= 4) {
        uint8_t nalType = (data[0] >> 1) & 0x3F;
        if (nalType <= 31) {
            TTBitReader r(data, size);
            r.setPos(16);  // Skip 2-byte NAL header

            uint32_t firstSliceFlag = r.bits(1);

            if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT) {
                r.bits(1);
            }

            if (firstSliceFlag != 1) return -1;

            r.ue();  // pps_id

            if (r.pos() / 8 + 2 >= size) return -1;

            uint32_t sliceType = r.ue();
            if (sliceType <= 2) {
                return static_cast<int>(sliceType);
            }
        }
    }

    return -1;
}
