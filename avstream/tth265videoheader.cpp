/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOHEADER
// H.265/HEVC Video Header structures implementation
// ----------------------------------------------------------------------------

#include "tth265videoheader.h"

// -----------------------------------------------------------------------------
// TTH265VPS - Video Parameter Set
// -----------------------------------------------------------------------------
TTH265VPS::TTH265VPS()
    : TTH265VideoHeader()
    , mVpsId(0)
    , mMaxLayers(1)
    , mMaxSubLayers(1)
    , mTemporalIdNesting(false)
{
    setNalUnitType(HEVC_NAL_VPS);
}

// -----------------------------------------------------------------------------
// TTH265SPS - Sequence Parameter Set
// -----------------------------------------------------------------------------
TTH265SPS::TTH265SPS()
    : TTH265VideoHeader()
    , mSpsId(0)
    , mVpsId(0)
    , mProfile(HEVC_PROFILE_MAIN)
    , mTier(0)
    , mLevel(0)
    , mWidth(0)
    , mHeight(0)
    , mBitDepthLuma(8)
    , mBitDepthChroma(8)
    , mFrameRate(25.0)
{
    setNalUnitType(HEVC_NAL_SPS);
}

QString TTH265SPS::profileString() const
{
    switch (mProfile) {
        case HEVC_PROFILE_MAIN:    return "Main";
        case HEVC_PROFILE_MAIN_10: return "Main 10";
        case HEVC_PROFILE_MAIN_SP: return "Main Still Picture";
        case HEVC_PROFILE_REXT:    return "Range Extensions";
        default:                   return QString("Profile %1").arg(mProfile);
    }
}

QString TTH265SPS::levelString() const
{
    // Level is stored as level * 30 in HEVC (e.g., level 5.1 = 153)
    int major = mLevel / 30;
    int minor = (mLevel % 30) / 3;

    if (minor > 0) {
        return QString("%1.%2").arg(major).arg(minor);
    }
    return QString::number(major);
}

// -----------------------------------------------------------------------------
// TTH265PPS - Picture Parameter Set
// -----------------------------------------------------------------------------
TTH265PPS::TTH265PPS()
    : TTH265VideoHeader()
    , mPpsId(0)
    , mSpsId(0)
    , mSignDataHiding(false)
    , mCabacInitPresent(false)
{
    setNalUnitType(HEVC_NAL_PPS);
}

// -----------------------------------------------------------------------------
// TTH265AccessUnit - Complete picture
// -----------------------------------------------------------------------------
TTH265AccessUnit::TTH265AccessUnit()
    : TTH265VideoHeader()
    , mFrameIndex(0)
    , mPts(0)
    , mDts(0)
    , mSliceType(HEVC_SLICE_B)
    , mIsIDR(false)
    , mIsRAP(false)
    , mIsReference(false)
    , mGopIndex(0)
    , mTemporalId(0)
    , mPocValue(0)
{
}

