/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth265videoheader.h                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOHEADER
// H.265/HEVC Video Header structures for frame-accurate cutting
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

#ifndef TTH265VIDEOHEADER_H
#define TTH265VIDEOHEADER_H

#include "ttavheader.h"

#include <QString>
#include <QList>

// -----------------------------------------------------------------------------
// H.265/HEVC NAL Unit Types (subset relevant for editing)
// -----------------------------------------------------------------------------
enum TTHNALU_Type {
    HEVC_NAL_TRAIL_N        = 0,   // Trailing picture, non-reference
    HEVC_NAL_TRAIL_R        = 1,   // Trailing picture, reference
    HEVC_NAL_TSA_N          = 2,   // Temporal sub-layer access, non-ref
    HEVC_NAL_TSA_R          = 3,   // Temporal sub-layer access, ref
    HEVC_NAL_STSA_N         = 4,   // Step-wise temporal sub-layer access
    HEVC_NAL_STSA_R         = 5,
    HEVC_NAL_RADL_N         = 6,   // Random access decodable leading
    HEVC_NAL_RADL_R         = 7,
    HEVC_NAL_RASL_N         = 8,   // Random access skipped leading
    HEVC_NAL_RASL_R         = 9,
    HEVC_NAL_BLA_W_LP       = 16,  // Broken link access with leading
    HEVC_NAL_BLA_W_RADL     = 17,
    HEVC_NAL_BLA_N_LP       = 18,
    HEVC_NAL_IDR_W_RADL     = 19,  // IDR with RADL
    HEVC_NAL_IDR_N_LP       = 20,  // IDR without leading pictures
    HEVC_NAL_CRA_NUT        = 21,  // Clean random access
    HEVC_NAL_VPS            = 32,  // Video parameter set
    HEVC_NAL_SPS            = 33,  // Sequence parameter set
    HEVC_NAL_PPS            = 34,  // Picture parameter set
    HEVC_NAL_AUD            = 35,  // Access unit delimiter
    HEVC_NAL_EOS_NUT        = 36,  // End of sequence
    HEVC_NAL_EOB_NUT        = 37,  // End of bitstream
    HEVC_NAL_FD_NUT         = 38,  // Filler data
    HEVC_NAL_PREFIX_SEI     = 39,  // Supplemental enhancement info
    HEVC_NAL_SUFFIX_SEI     = 40
};

// -----------------------------------------------------------------------------
// H.265 Slice Types
// -----------------------------------------------------------------------------
enum TTH265SliceType {
    HEVC_SLICE_B = 0,
    HEVC_SLICE_P = 1,
    HEVC_SLICE_I = 2
};

// -----------------------------------------------------------------------------
// H.265 Profiles
// -----------------------------------------------------------------------------
enum TTH265Profile {
    HEVC_PROFILE_MAIN       = 1,
    HEVC_PROFILE_MAIN_10    = 2,
    HEVC_PROFILE_MAIN_SP    = 3,   // Main Still Picture
    HEVC_PROFILE_REXT       = 4    // Range Extensions
};

// -----------------------------------------------------------------------------
// TTH265VideoHeader - Abstract base class for H.265 header types
// -----------------------------------------------------------------------------
class TTH265VideoHeader : public TTAVHeader
{
public:
    TTH265VideoHeader() : mNalUnitType(0), mOffset(0), mSize(0) {}
    virtual ~TTH265VideoHeader() {}

    int nalUnitType() const { return mNalUnitType; }
    int64_t offset() const { return mOffset; }
    int size() const { return mSize; }

    void setNalUnitType(int type) { mNalUnitType = type; }
    void setOffset(int64_t offset) { mOffset = offset; }
    void setSize(int size) { mSize = size; }

    static QString nalTypeString(int type);

protected:
    int mNalUnitType;
    int64_t mOffset;
    int mSize;
};

// -----------------------------------------------------------------------------
// TTH265VPS - Video Parameter Set (unique to H.265)
// -----------------------------------------------------------------------------
class TTH265VPS : public TTH265VideoHeader
{
public:
    TTH265VPS();
    virtual ~TTH265VPS() {}

    int vpsId() const { return mVpsId; }
    int maxLayers() const { return mMaxLayers; }
    int maxSubLayers() const { return mMaxSubLayers; }
    bool temporalIdNesting() const { return mTemporalIdNesting; }

    void setVpsId(int id) { mVpsId = id; }
    void setMaxLayers(int layers) { mMaxLayers = layers; }
    void setMaxSubLayers(int subLayers) { mMaxSubLayers = subLayers; }
    void setTemporalIdNesting(bool nesting) { mTemporalIdNesting = nesting; }

private:
    int mVpsId;
    int mMaxLayers;
    int mMaxSubLayers;
    bool mTemporalIdNesting;
};

// -----------------------------------------------------------------------------
// TTH265SPS - Sequence Parameter Set
// -----------------------------------------------------------------------------
class TTH265SPS : public TTH265VideoHeader
{
public:
    TTH265SPS();
    virtual ~TTH265SPS() {}

    // Identification
    int spsId() const { return mSpsId; }
    int vpsId() const { return mVpsId; }

    // Profile and level
    int profile() const { return mProfile; }
    int tier() const { return mTier; }
    int level() const { return mLevel; }

    // Resolution
    int width() const { return mWidth; }
    int height() const { return mHeight; }

    // Bit depth
    int bitDepthLuma() const { return mBitDepthLuma; }
    int bitDepthChroma() const { return mBitDepthChroma; }

    // Frame rate (from VUI if present)
    double frameRate() const { return mFrameRate; }

    // Setters
    void setSpsId(int id) { mSpsId = id; }
    void setVpsId(int id) { mVpsId = id; }
    void setProfile(int profile) { mProfile = profile; }
    void setTier(int tier) { mTier = tier; }
    void setLevel(int level) { mLevel = level; }
    void setWidth(int w) { mWidth = w; }
    void setHeight(int h) { mHeight = h; }
    void setBitDepthLuma(int depth) { mBitDepthLuma = depth; }
    void setBitDepthChroma(int depth) { mBitDepthChroma = depth; }
    void setFrameRate(double rate) { mFrameRate = rate; }

    QString profileString() const;
    QString levelString() const;

private:
    int mSpsId;
    int mVpsId;
    int mProfile;
    int mTier;      // 0 = Main tier, 1 = High tier
    int mLevel;
    int mWidth;
    int mHeight;
    int mBitDepthLuma;
    int mBitDepthChroma;
    double mFrameRate;
};

// -----------------------------------------------------------------------------
// TTH265PPS - Picture Parameter Set
// -----------------------------------------------------------------------------
class TTH265PPS : public TTH265VideoHeader
{
public:
    TTH265PPS();
    virtual ~TTH265PPS() {}

    int ppsId() const { return mPpsId; }
    int spsId() const { return mSpsId; }
    bool signDataHidingEnabled() const { return mSignDataHiding; }
    bool cabacInitPresent() const { return mCabacInitPresent; }

    void setPpsId(int id) { mPpsId = id; }
    void setSpsId(int id) { mSpsId = id; }
    void setSignDataHidingEnabled(bool enabled) { mSignDataHiding = enabled; }
    void setCabacInitPresent(bool present) { mCabacInitPresent = present; }

private:
    int mPpsId;
    int mSpsId;
    bool mSignDataHiding;
    bool mCabacInitPresent;
};

// -----------------------------------------------------------------------------
// TTH265AccessUnit - One complete picture (access unit)
// Used for frame-accurate cutting
// -----------------------------------------------------------------------------
class TTH265AccessUnit : public TTH265VideoHeader
{
public:
    TTH265AccessUnit();
    virtual ~TTH265AccessUnit() {}

    // Frame identification
    int frameIndex() const { return mFrameIndex; }
    int64_t pts() const { return mPts; }
    int64_t dts() const { return mDts; }

    // Frame type
    int sliceType() const { return mSliceType; }
    bool isIDR() const { return mIsIDR; }
    bool isRAP() const { return mIsRAP; }  // Random Access Point (IDR, CRA, BLA)
    bool isReference() const { return mIsReference; }

    // GOP structure
    int gopIndex() const { return mGopIndex; }
    int temporalId() const { return mTemporalId; }
    int pocValue() const { return mPocValue; }  // Picture Order Count

    // Setters
    void setFrameIndex(int idx) { mFrameIndex = idx; }
    void setPts(int64_t pts) { mPts = pts; }
    void setDts(int64_t dts) { mDts = dts; }
    void setSliceType(int type) { mSliceType = type; }
    void setIsIDR(bool idr) { mIsIDR = idr; }
    void setIsRAP(bool rap) { mIsRAP = rap; }
    void setIsReference(bool ref) { mIsReference = ref; }
    void setGopIndex(int idx) { mGopIndex = idx; }
    void setTemporalId(int tid) { mTemporalId = tid; }
    void setPocValue(int poc) { mPocValue = poc; }

    QString frameTypeString() const;

private:
    int mFrameIndex;
    int64_t mPts;
    int64_t mDts;
    int mSliceType;
    bool mIsIDR;
    bool mIsRAP;
    bool mIsReference;
    int mGopIndex;
    int mTemporalId;
    int mPocValue;
};

#endif // TTH265VIDEOHEADER_H
