/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttnaluparser.h                                                  */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2026  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTNALUPARSER
// NAL Unit parser for H.264/H.265 elementary streams
// Provides low-level access to NAL structure for smart cutting
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#ifndef TTNALUPARSER_H
#define TTNALUPARSER_H

#include <QString>
#include <QList>
#include <QByteArray>
#include <QFile>

// ----------------------------------------------------------------------------
// NAL Unit types for H.264 (AVC)
// ----------------------------------------------------------------------------
namespace H264 {
    enum NalUnitType {
        NAL_UNSPECIFIED     = 0,
        NAL_SLICE           = 1,   // Non-IDR slice (P or B)
        NAL_DPA             = 2,   // Data partition A
        NAL_DPB             = 3,   // Data partition B
        NAL_DPC             = 4,   // Data partition C
        NAL_IDR_SLICE       = 5,   // IDR slice (keyframe)
        NAL_SEI             = 6,   // Supplemental enhancement info
        NAL_SPS             = 7,   // Sequence parameter set
        NAL_PPS             = 8,   // Picture parameter set
        NAL_AUD             = 9,   // Access unit delimiter
        NAL_END_SEQUENCE    = 10,  // End of sequence
        NAL_END_STREAM      = 11,  // End of stream
        NAL_FILLER          = 12,  // Filler data
        NAL_SPS_EXT         = 13,  // SPS extension
        NAL_PREFIX          = 14,  // Prefix NAL unit
        NAL_SUBSET_SPS      = 15,  // Subset SPS
        NAL_AUXILIARY       = 19,  // Auxiliary coded picture
        NAL_SLICE_EXT       = 20,  // Coded slice extension
    };

    enum SliceType {
        SLICE_P  = 0,
        SLICE_B  = 1,
        SLICE_I  = 2,
        SLICE_SP = 3,
        SLICE_SI = 4,
        // Types 5-9 are the same but indicate all slices in the picture are of this type
        SLICE_P_ALL  = 5,
        SLICE_B_ALL  = 6,
        SLICE_I_ALL  = 7,
        SLICE_SP_ALL = 8,
        SLICE_SI_ALL = 9,
    };
}

// ----------------------------------------------------------------------------
// NAL Unit types for H.265 (HEVC)
// ----------------------------------------------------------------------------
namespace H265 {
    enum NalUnitType {
        NAL_TRAIL_N         = 0,   // Trailing picture, non-ref
        NAL_TRAIL_R         = 1,   // Trailing picture, ref
        NAL_TSA_N           = 2,   // Temporal sub-layer access, non-ref
        NAL_TSA_R           = 3,   // Temporal sub-layer access, ref
        NAL_STSA_N          = 4,   // Step-wise temporal sub-layer access, non-ref
        NAL_STSA_R          = 5,   // Step-wise temporal sub-layer access, ref
        NAL_RADL_N          = 6,   // Random access decodable leading, non-ref
        NAL_RADL_R          = 7,   // Random access decodable leading, ref
        NAL_RASL_N          = 8,   // Random access skipped leading, non-ref
        NAL_RASL_R          = 9,   // Random access skipped leading, ref
        NAL_BLA_W_LP        = 16,  // Broken link access with leading pictures
        NAL_BLA_W_RADL      = 17,  // BLA with RADL
        NAL_BLA_N_LP        = 18,  // BLA without leading pictures
        NAL_IDR_W_RADL      = 19,  // IDR with RADL
        NAL_IDR_N_LP        = 20,  // IDR without leading pictures
        NAL_CRA_NUT         = 21,  // Clean random access
        NAL_VPS             = 32,  // Video parameter set
        NAL_SPS             = 33,  // Sequence parameter set
        NAL_PPS             = 34,  // Picture parameter set
        NAL_AUD             = 35,  // Access unit delimiter
        NAL_EOS             = 36,  // End of sequence
        NAL_EOB             = 37,  // End of bitstream
        NAL_FD              = 38,  // Filler data
        NAL_PREFIX_SEI      = 39,  // Prefix SEI
        NAL_SUFFIX_SEI      = 40,  // Suffix SEI
    };

    enum SliceType {
        SLICE_B = 0,
        SLICE_P = 1,
        SLICE_I = 2,
    };
}

// ----------------------------------------------------------------------------
// NAL Unit information structure
// ----------------------------------------------------------------------------
struct TTNalUnit {
    int64_t fileOffset;          // Position in file (start of start code)
    int64_t dataOffset;          // Position of actual NAL data (after start code)
    int64_t size;                // Size including start code
    int64_t dataSize;            // Size of NAL data only

    uint8_t type;                // nal_unit_type
    uint8_t refIdc;              // nal_ref_idc (H.264) or nuh_layer_id (H.265)
    uint8_t temporalId;          // temporal_id (H.265)

    bool isKeyframe;             // IDR or I-slice (used for GOP detection)
    bool isIDR;                  // True IDR frame (safe for random access/stream-copy start)
    bool isSlice;                // Contains picture data
    bool isSPS;                  // Sequence parameter set
    bool isPPS;                  // Picture parameter set
    bool isVPS;                  // Video parameter set (H.265 only)
    bool isSEI;                  // Supplemental enhancement info
    bool isFiller;               // Filler data (can be removed)
    bool isAUD;                  // Access unit delimiter

    // Slice information (only valid if isSlice)
    int sliceType;               // I, P, B
    int frameNum;                // frame_num (H.264)
    int poc;                     // Picture Order Count
    int firstMbInSlice;          // First macroblock address
    int ppsId;                   // PPS ID used by this slice
};

// ----------------------------------------------------------------------------
// Access Unit (Frame) information
// Groups NAL units that belong to the same picture
// ----------------------------------------------------------------------------
struct TTAccessUnit {
    int index;                   // Frame index (display order based on POC)
    int decodeIndex;             // Decode order index
    QList<int> nalIndices;       // Indices of NAL units in this AU

    int64_t startOffset;         // Start offset of first NAL
    int64_t endOffset;           // End offset of last NAL

    bool isKeyframe;             // Contains IDR/CRA/BLA or I-slice
    bool isIDR;                  // True IDR frame (safe for random access/stream-copy start)
    int sliceType;               // Dominant slice type (I, P, B)
    int poc;                     // Picture Order Count
    int gopIndex;                // Which GOP this frame belongs to
};

// ----------------------------------------------------------------------------
// GOP information
// ----------------------------------------------------------------------------
struct TTGopInfo {
    int index;                   // GOP index
    int startAU;                 // First Access Unit index
    int endAU;                   // Last Access Unit index (inclusive)
    int keyframeAU;              // Index of the keyframe AU
    int frameCount;              // Number of frames in GOP
    bool isClosed;               // No references outside GOP
};

// ----------------------------------------------------------------------------
// Codec type enumeration
// ----------------------------------------------------------------------------
enum TTNaluCodecType {
    NALU_CODEC_UNKNOWN,
    NALU_CODEC_H264,
    NALU_CODEC_H265
};

// ----------------------------------------------------------------------------
// TTNaluParser class
// ----------------------------------------------------------------------------
class TTNaluParser
{
public:
    TTNaluParser();
    ~TTNaluParser();

    // File operations
    bool openFile(const QString& filePath);
    void closeFile();
    bool isOpen() const { return mFile.isOpen(); }

    // Full file parsing
    bool parseFile();

    // Accessors
    TTNaluCodecType codecType() const { return mCodecType; }
    QString codecName() const;

    const QList<TTNalUnit>& nalUnits() const { return mNalUnits; }
    int nalUnitCount() const { return mNalUnits.size(); }

    const QList<TTAccessUnit>& accessUnits() const { return mAccessUnits; }
    int accessUnitCount() const { return mAccessUnits.size(); }

    const QList<TTGopInfo>& gops() const { return mGops; }
    int gopCount() const { return mGops.size(); }

    // NAL Unit access
    TTNalUnit nalUnitAt(int index) const;
    QByteArray readNalData(int index);
    QByteArray readNalDataWithStartCode(int index);

    // Access Unit (frame) access
    TTAccessUnit accessUnitAt(int index) const;
    QByteArray readAccessUnitData(int index);

    // Parameter sets
    QByteArray getSPS(int index = 0) const;
    QByteArray getPPS(int index = 0) const;
    QByteArray getVPS(int index = 0) const;  // H.265 only
    int spsCount() const { return mSPSList.size(); }
    int ppsCount() const { return mPPSList.size(); }
    int vpsCount() const { return mVPSList.size(); }

    // Search functions
    int findKeyframeBefore(int auIndex) const;
    int findKeyframeAfter(int auIndex) const;
    int findIDRBefore(int auIndex) const;
    int findIDRAfter(int auIndex) const;
    int findGopForAU(int auIndex) const;

    // GOP information
    TTGopInfo gopAt(int index) const;
    int getGopStartAU(int gopIndex) const;
    int getGopEndAU(int gopIndex) const;

    // Utility
    QString formatNalType(uint8_t type) const;
    static bool isKeyframeType(uint8_t type, TTNaluCodecType codec);
    static bool isSliceType(uint8_t type, TTNaluCodecType codec);

    // Error handling
    QString lastError() const { return mLastError; }

private:
    // File handling
    QFile mFile;
    QString mFilePath;
    int64_t mFileSize;

    // Codec type
    TTNaluCodecType mCodecType;

    // Memory-mapped file pointer (for fast access)
    uchar* mMappedFile;

    // Parsed data
    QList<TTNalUnit> mNalUnits;
    QList<TTAccessUnit> mAccessUnits;
    QList<TTGopInfo> mGops;

    // Parameter sets (indices into mNalUnits)
    QList<int> mSPSList;
    QList<int> mPPSList;
    QList<int> mVPSList;

    // Error handling
    QString mLastError;
    void setError(const QString& error);

    // Parsing helpers
    bool detectCodecType();
    int findNextStartCode(int64_t startPos, int64_t& codePos, int& codeLen);
    bool parseNalUnit(int64_t offset, int startCodeLen, TTNalUnit& nal);
    bool parseSliceHeader(const QByteArray& data, TTNalUnit& nal);
    void buildAccessUnits();
    void buildGOPs();

    // H.264 specific parsing
    bool parseH264NalUnit(const QByteArray& data, TTNalUnit& nal);
    bool parseH264SliceHeader(const QByteArray& data, TTNalUnit& nal);

    // H.265 specific parsing
    bool parseH265NalUnit(const QByteArray& data, TTNalUnit& nal);
    bool parseH265SliceHeader(const QByteArray& data, TTNalUnit& nal);

    // Exp-Golomb decoding (for slice header parsing)
    static uint32_t readExpGolombUE(const uint8_t* data, int& bitPos);
    static int32_t readExpGolombSE(const uint8_t* data, int& bitPos);
    static uint32_t readBits(const uint8_t* data, int& bitPos, int numBits);
};

#endif // TTNALUPARSER_H
