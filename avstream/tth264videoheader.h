/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth264videoheader.h                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// H.264/AVC Video Header Classes
// ----------------------------------------------------------------------------
// TTH264VideoHeader (abstract base)
// TTH264SPS - Sequence Parameter Set
// TTH264PPS - Picture Parameter Set
// TTH264AccessUnit - Access Unit (one video frame)
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

#ifndef TTH264VIDEOHEADER_H
#define TTH264VIDEOHEADER_H

#include "ttavheader.h"
#include <QString>

// Forward declaration
class TTFileBuffer;

// -----------------------------------------------------------------------------
// H.264 NAL Unit Types
// -----------------------------------------------------------------------------
enum H264NalUnitType {
    NAL_UNSPECIFIED     = 0,
    NAL_SLICE           = 1,   // Coded slice of a non-IDR picture
    NAL_SLICE_DPA       = 2,   // Coded slice data partition A
    NAL_SLICE_DPB       = 3,   // Coded slice data partition B
    NAL_SLICE_DPC       = 4,   // Coded slice data partition C
    NAL_SLICE_IDR       = 5,   // Coded slice of an IDR picture
    NAL_SEI             = 6,   // Supplemental enhancement information
    NAL_SPS             = 7,   // Sequence parameter set
    NAL_PPS             = 8,   // Picture parameter set
    NAL_AUD             = 9,   // Access unit delimiter
    NAL_END_SEQUENCE    = 10,  // End of sequence
    NAL_END_STREAM      = 11,  // End of stream
    NAL_FILLER_DATA     = 12,  // Filler data
    NAL_SPS_EXT         = 13,  // Sequence parameter set extension
    NAL_PREFIX          = 14,  // Prefix NAL unit
    NAL_SUB_SPS         = 15,  // Subset sequence parameter set
    NAL_SLICE_AUX       = 19,  // Coded slice of auxiliary coded picture
    NAL_SLICE_EXT       = 20,  // Coded slice extension
    NAL_SLICE_DEPTH     = 21   // Coded slice extension for depth view
};

// -----------------------------------------------------------------------------
// H.264 Picture/Slice Types
// -----------------------------------------------------------------------------
enum H264SliceType {
    SLICE_TYPE_P  = 0,
    SLICE_TYPE_B  = 1,
    SLICE_TYPE_I  = 2,
    SLICE_TYPE_SP = 3,
    SLICE_TYPE_SI = 4,
    // When slice_type > 4, all slices in picture are of the same type
    SLICE_TYPE_P_ALL  = 5,
    SLICE_TYPE_B_ALL  = 6,
    SLICE_TYPE_I_ALL  = 7,
    SLICE_TYPE_SP_ALL = 8,
    SLICE_TYPE_SI_ALL = 9
};

// -----------------------------------------------------------------------------
// H.264 Profile IDC values
// -----------------------------------------------------------------------------
enum H264Profile {
    PROFILE_BASELINE         = 66,
    PROFILE_MAIN             = 77,
    PROFILE_EXTENDED         = 88,
    PROFILE_HIGH             = 100,
    PROFILE_HIGH_10          = 110,
    PROFILE_HIGH_422         = 122,
    PROFILE_HIGH_444         = 244,
    PROFILE_CAVLC_444_INTRA  = 44
};

// -----------------------------------------------------------------------------
// Abstract class TTH264VideoHeader
// -----------------------------------------------------------------------------
class TTH264VideoHeader : public TTVideoHeader
{
public:
    TTH264VideoHeader();
    virtual ~TTH264VideoHeader();

    // NAL unit type for this header
    H264NalUnitType nalType() const { return mNalType; }
    void setNalType(H264NalUnitType type) { mNalType = type; }

    // NAL reference IDC (0-3, indicates if frame is used as reference)
    int nalRefIdc() const { return mNalRefIdc; }
    void setNalRefIdc(int idc) { mNalRefIdc = idc; }

    // These are required by base class but we use libav for parsing
    virtual bool readHeader(TTFileBuffer* stream) override;
    virtual bool readHeader(TTFileBuffer* stream, quint64 offset) override;
    virtual void parseBasicData(quint8* data, int offset = 0) override;

protected:
    H264NalUnitType mNalType;
    int mNalRefIdc;
};

// -----------------------------------------------------------------------------
// TTH264SPS - Sequence Parameter Set
// Contains video format information (resolution, profile, level, etc.)
// -----------------------------------------------------------------------------
class TTH264SPS : public TTH264VideoHeader
{
public:
    TTH264SPS();

    // Profile and level
    int profileIdc() const { return mProfileIdc; }
    int levelIdc() const { return mLevelIdc; }
    QString profileString() const;
    QString levelString() const;

    // Video dimensions
    int width() const { return mWidth; }
    int height() const { return mHeight; }

    // Frame rate (if specified in VUI)
    double frameRate() const { return mFrameRate; }
    bool hasFrameRate() const { return mHasFrameRate; }

    // SPS ID
    int spsId() const { return mSpsId; }

    // Setters for libav parsing
    void setProfileIdc(int profile) { mProfileIdc = profile; }
    void setLevelIdc(int level) { mLevelIdc = level; }
    void setWidth(int w) { mWidth = w; }
    void setHeight(int h) { mHeight = h; }
    void setFrameRate(double fps) { mFrameRate = fps; mHasFrameRate = true; }
    void setSpsId(int id) { mSpsId = id; }

private:
    int mProfileIdc;
    int mLevelIdc;
    int mWidth;
    int mHeight;
    double mFrameRate;
    bool mHasFrameRate;
    int mSpsId;
};

// -----------------------------------------------------------------------------
// TTH264PPS - Picture Parameter Set
// Contains picture-level coding parameters
// -----------------------------------------------------------------------------
class TTH264PPS : public TTH264VideoHeader
{
public:
    TTH264PPS();

    int ppsId() const { return mPpsId; }
    int spsId() const { return mSpsId; }

    void setPpsId(int id) { mPpsId = id; }
    void setSpsId(int id) { mSpsId = id; }

private:
    int mPpsId;
    int mSpsId;
};

// -----------------------------------------------------------------------------
// TTH264AccessUnit - Represents one complete video frame
// This is the main unit for frame-accurate cutting
// -----------------------------------------------------------------------------
class TTH264AccessUnit : public TTH264VideoHeader
{
public:
    TTH264AccessUnit();

    // Frame type (I, P, B)
    H264SliceType sliceType() const { return mSliceType; }
    void setSliceType(H264SliceType type) { mSliceType = type; }

    // Is this an IDR frame (random access point)?
    bool isIDR() const { return mIsIDR; }
    void setIDR(bool idr) { mIsIDR = idr; }

    // Is this a keyframe (can be used as cut-in point)?
    bool isKeyframe() const { return mIsIDR; }

    // Frame number in decode order
    int frameNum() const { return mFrameNum; }
    void setFrameNum(int num) { mFrameNum = num; }

    // POC (Picture Order Count) - display order
    int poc() const { return mPoc; }
    void setPoc(int poc) { mPoc = poc; }

    // Timestamps
    int64_t pts() const { return mPts; }
    int64_t dts() const { return mDts; }
    void setPts(int64_t pts) { mPts = pts; }
    void setDts(int64_t dts) { mDts = dts; }

    // GOP index (which GOP this frame belongs to)
    int gopIndex() const { return mGopIndex; }
    void setGopIndex(int idx) { mGopIndex = idx; }

    // Frame size in bytes
    int64_t frameSize() const { return mFrameSize; }
    void setFrameSize(int64_t size) { mFrameSize = size; }

    // String representation of frame type
    QString frameTypeString() const;

    // Can this frame be used as cut-in point?
    bool isCutInPoint() const { return mIsIDR; }

    // Can this frame be used as cut-out point?
    // (Any frame can be cut-out, but non-IDR requires re-encoding)
    bool isCutOutPoint() const { return true; }
    bool requiresReencode() const { return !mIsIDR; }

private:
    H264SliceType mSliceType;
    bool mIsIDR;
    int mFrameNum;
    int mPoc;
    int64_t mPts;
    int64_t mDts;
    int mGopIndex;
    int64_t mFrameSize;
};

#endif // TTH264VIDEOHEADER_H
