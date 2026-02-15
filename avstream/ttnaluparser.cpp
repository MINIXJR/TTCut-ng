/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttnaluparser.cpp                                                */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2026  */
/*----------------------------------------------------------------------------*/

#include "ttnaluparser.h"

#include <QDebug>
#include <QFileInfo>
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

    qDebug() << "TTNaluParser: Opened" << filePath;
    qDebug() << "  File size:" << mFileSize << "bytes";
    qDebug() << "  Codec:" << codecName();

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

    qDebug() << "TTNaluParser: Parsing file...";

    // Find all NAL units
    mFile.seek(0);

    int64_t currentPos = 0;
    int64_t startCodePos = 0;
    int startCodeLen = 0;
    int nalCount = 0;

    while (currentPos < mFileSize) {
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

            // Track parameter sets
            if (nal.isSPS) mSPSList.append(nalCount - 1);
            if (nal.isPPS) mPPSList.append(nalCount - 1);
            if (nal.isVPS) mVPSList.append(nalCount - 1);

            // Progress output
            if (nalCount % 10000 == 0) {
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

    qDebug() << "  NAL units found:" << nalCount;
    qDebug() << "  SPS:" << mSPSList.size() << ", PPS:" << mPPSList.size();
    if (mCodecType == NALU_CODEC_H265) {
        qDebug() << "  VPS:" << mVPSList.size();
    }

    // Build access units (group NALs into frames)
    buildAccessUnits();

    // Build GOP structure
    buildGOPs();

    qDebug() << "TTNaluParser: Parsing complete";
    qDebug() << "  Access Units (frames):" << mAccessUnits.size();
    qDebug() << "  GOPs:" << mGops.size();

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
            qDebug() << "Warning: Could not map entire file, falling back to chunk mode";
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

    // Read NAL header (first 1-2 bytes after start code)
    mFile.seek(nal.dataOffset);
    QByteArray header = mFile.read(32);  // Read enough for basic header parsing

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
// Parse H.264 slice header (basic info only)
// ----------------------------------------------------------------------------
bool TTNaluParser::parseH264SliceHeader(const QByteArray& data, TTNalUnit& nal)
{
    if (data.size() < 3) return false;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    int bitPos = 8;  // Skip NAL header byte

    // first_mb_in_slice (ue(v))
    nal.firstMbInSlice = static_cast<int>(readExpGolombUE(bytes, bitPos));

    // slice_type (ue(v))
    uint32_t sliceType = readExpGolombUE(bytes, bitPos);
    // Normalize slice type (0-4 and 5-9 mean the same thing)
    if (sliceType > 4) sliceType -= 5;
    nal.sliceType = static_cast<int>(sliceType);

    // pic_parameter_set_id (ue(v))
    nal.ppsId = static_cast<int>(readExpGolombUE(bytes, bitPos));

    // frame_num would require knowing log2_max_frame_num from SPS
    // For now, we skip it

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

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    int bitPos = 16;  // Skip 2-byte NAL header

    // first_slice_segment_in_pic_flag (1 bit)
    uint32_t firstSliceFlag = readBits(bytes, bitPos, 1);
    nal.firstMbInSlice = (firstSliceFlag == 1) ? 0 : -1;

    // For BLA/IDR/CRA: no_output_of_prior_pics_flag (1 bit)
    if (nal.type >= H265::NAL_BLA_W_LP && nal.type <= H265::NAL_CRA_NUT) {
        readBits(bytes, bitPos, 1);  // no_output_of_prior_pics_flag
    }

    // slice_pic_parameter_set_id (ue(v))
    nal.ppsId = static_cast<int>(readExpGolombUE(bytes, bitPos));

    // For first slice in picture, we can read slice_type directly from the bitstream.
    // For dependent slices (firstSliceFlag == 0), we'd need PPS data we don't have,
    // so fall back to NAL-type heuristic.
    if (firstSliceFlag == 1) {
        // HEVC spec: slice_type is ue(v) right after slice_pic_parameter_set_id
        // (assuming num_extra_slice_header_bits == 0, which is standard for DVB)
        uint32_t sliceType = readExpGolombUE(bytes, bitPos);
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

    qDebug() << "  Built" << mAccessUnits.size() << "access units";
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
// Find IDR frame before given AU index
// IDR frames are true random access points, safe for stream-copy start
// ----------------------------------------------------------------------------
int TTNaluParser::findIDRBefore(int auIndex) const
{
    for (int i = auIndex; i >= 0; i--) {
        if (mAccessUnits[i].isIDR) {
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
// Get start AU of GOP
// ----------------------------------------------------------------------------
int TTNaluParser::getGopStartAU(int gopIndex) const
{
    if (gopIndex >= 0 && gopIndex < mGops.size()) {
        return mGops[gopIndex].startAU;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Get end AU of GOP
// ----------------------------------------------------------------------------
int TTNaluParser::getGopEndAU(int gopIndex) const
{
    if (gopIndex >= 0 && gopIndex < mGops.size()) {
        return mGops[gopIndex].endAU;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Compute B-frame reorder delay from first GOP structure
// Returns the number of B-frames between reference frames in decode order.
// This equals the decoder's reorder delay for display-order output.
// ----------------------------------------------------------------------------
int TTNaluParser::computeReorderDelay() const
{
    if (mGops.isEmpty() || mAccessUnits.size() < 3) return 0;

    // Use the first GOP: count B-frames and reference frames (excluding keyframe)
    const TTGopInfo& gop = mGops[0];
    int bFrames = 0;
    int refFrames = 0;

    for (int i = gop.startAU + 1; i <= gop.endAU && i < mAccessUnits.size(); i++) {
        const TTAccessUnit& au = mAccessUnits[i];
        bool isB = false;
        if (mCodecType == NALU_CODEC_H265) {
            isB = (au.sliceType == H265::SLICE_B);
        } else {
            isB = (au.sliceType == H264::SLICE_B ||
                   au.sliceType == H264::SLICE_B_ALL);
        }

        if (isB) {
            bFrames++;
        } else {
            refFrames++;
        }
    }

    int delay = (refFrames > 0) ? (bFrames / refFrames) : bFrames;
    qDebug() << "TTNaluParser: GOP[0] has" << bFrames << "B-frames,"
             << refFrames << "ref-frames -> reorder delay:" << delay;
    return delay;
}

// ----------------------------------------------------------------------------
// Format NAL type as string
// ----------------------------------------------------------------------------
QString TTNaluParser::formatNalType(uint8_t type) const
{
    if (mCodecType == NALU_CODEC_H264) {
        switch (type) {
            case H264::NAL_SLICE:     return "SLICE";
            case H264::NAL_IDR_SLICE: return "IDR";
            case H264::NAL_SEI:       return "SEI";
            case H264::NAL_SPS:       return "SPS";
            case H264::NAL_PPS:       return "PPS";
            case H264::NAL_AUD:       return "AUD";
            case H264::NAL_FILLER:    return "FILLER";
            default: return QString("TYPE_%1").arg(type);
        }
    } else if (mCodecType == NALU_CODEC_H265) {
        switch (type) {
            case H265::NAL_TRAIL_R:   return "TRAIL_R";
            case H265::NAL_TRAIL_N:   return "TRAIL_N";
            case H265::NAL_IDR_W_RADL: return "IDR_W_RADL";
            case H265::NAL_IDR_N_LP:  return "IDR_N_LP";
            case H265::NAL_CRA_NUT:   return "CRA";
            case H265::NAL_VPS:       return "VPS";
            case H265::NAL_SPS:       return "SPS";
            case H265::NAL_PPS:       return "PPS";
            case H265::NAL_AUD:       return "AUD";
            case H265::NAL_FD:        return "FILLER";
            case H265::NAL_PREFIX_SEI: return "SEI_PREFIX";
            case H265::NAL_SUFFIX_SEI: return "SEI_SUFFIX";
            default: return QString("TYPE_%1").arg(type);
        }
    }
    return QString("UNKNOWN_%1").arg(type);
}

// ----------------------------------------------------------------------------
// Check if NAL type is a keyframe
// ----------------------------------------------------------------------------
bool TTNaluParser::isKeyframeType(uint8_t type, TTNaluCodecType codec)
{
    if (codec == NALU_CODEC_H264) {
        return type == H264::NAL_IDR_SLICE;
    } else if (codec == NALU_CODEC_H265) {
        return (type >= H265::NAL_BLA_W_LP && type <= H265::NAL_CRA_NUT);
    }
    return false;
}

// ----------------------------------------------------------------------------
// Check if NAL type is a slice
// ----------------------------------------------------------------------------
bool TTNaluParser::isSliceType(uint8_t type, TTNaluCodecType codec)
{
    if (codec == NALU_CODEC_H264) {
        return type == H264::NAL_SLICE || type == H264::NAL_IDR_SLICE;
    } else if (codec == NALU_CODEC_H265) {
        return (type <= H265::NAL_RASL_R) ||
               (type >= H265::NAL_BLA_W_LP && type <= H265::NAL_CRA_NUT);
    }
    return false;
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTNaluParser::setError(const QString& error)
{
    mLastError = error;
    qDebug() << "TTNaluParser error:" << error;
}

// ----------------------------------------------------------------------------
// Read Exp-Golomb unsigned value (ue(v))
// ----------------------------------------------------------------------------
uint32_t TTNaluParser::readExpGolombUE(const uint8_t* data, int& bitPos)
{
    // Count leading zeros
    int leadingZeros = 0;
    while (readBits(data, bitPos, 1) == 0 && leadingZeros < 32) {
        leadingZeros++;
    }

    if (leadingZeros == 0) {
        return 0;
    }

    // Read the value bits
    uint32_t value = readBits(data, bitPos, leadingZeros);
    return (1u << leadingZeros) - 1 + value;
}

// ----------------------------------------------------------------------------
// Read Exp-Golomb signed value (se(v))
// ----------------------------------------------------------------------------
int32_t TTNaluParser::readExpGolombSE(const uint8_t* data, int& bitPos)
{
    uint32_t ue = readExpGolombUE(data, bitPos);
    if (ue & 1) {
        return static_cast<int32_t>((ue + 1) / 2);
    } else {
        return -static_cast<int32_t>(ue / 2);
    }
}

// ----------------------------------------------------------------------------
// Read bits from byte array
// ----------------------------------------------------------------------------
uint32_t TTNaluParser::readBits(const uint8_t* data, int& bitPos, int numBits)
{
    uint32_t value = 0;

    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        int bitIndex = 7 - (bitPos % 8);

        value <<= 1;
        value |= (data[byteIndex] >> bitIndex) & 1;
        bitPos++;
    }

    return value;
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

                    int bitPos = 16;  // Skip 2-byte NAL header

                    // first_slice_segment_in_pic_flag (1 bit)
                    uint32_t firstSliceFlag = readBits(nalData, bitPos, 1);

                    // For BLA/IDR/CRA: no_output_of_prior_pics_flag (1 bit)
                    if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT) {
                        readBits(nalData, bitPos, 1);
                    }

                    if (firstSliceFlag != 1) {
                        // Dependent slice — need PPS info we don't have
                        // Fall back to NAL-type heuristic
                        if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT)
                            return H265::SLICE_I;
                        return -1;
                    }

                    // slice_pic_parameter_set_id (ue(v))
                    readExpGolombUE(nalData, bitPos);

                    // slice_type (ue(v))
                    // Guard: make sure we have enough data
                    if (bitPos / 8 + 2 >= nalDataSize) return -1;

                    uint32_t sliceType = readExpGolombUE(nalData, bitPos);
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
            int bitPos = 16;  // Skip 2-byte NAL header

            uint32_t firstSliceFlag = readBits(data, bitPos, 1);

            if (nalType >= H265::NAL_BLA_W_LP && nalType <= H265::NAL_CRA_NUT) {
                readBits(data, bitPos, 1);
            }

            if (firstSliceFlag != 1) return -1;

            readExpGolombUE(data, bitPos);  // pps_id

            if (bitPos / 8 + 2 >= size) return -1;

            uint32_t sliceType = readExpGolombUE(data, bitPos);
            if (sliceType <= 2) {
                return static_cast<int>(sliceType);
            }
        }
    }

    return -1;
}
