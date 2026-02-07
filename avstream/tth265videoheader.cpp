/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth265videoheader.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOHEADER
// H.265/HEVC Video Header structures implementation
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

#include "tth265videoheader.h"

// -----------------------------------------------------------------------------
// TTH265VideoHeader - Static helper methods
// -----------------------------------------------------------------------------
QString TTH265VideoHeader::nalTypeString(int type)
{
    switch (type) {
        case HEVC_NAL_TRAIL_N:    return "TRAIL_N";
        case HEVC_NAL_TRAIL_R:    return "TRAIL_R";
        case HEVC_NAL_TSA_N:      return "TSA_N";
        case HEVC_NAL_TSA_R:      return "TSA_R";
        case HEVC_NAL_STSA_N:     return "STSA_N";
        case HEVC_NAL_STSA_R:     return "STSA_R";
        case HEVC_NAL_RADL_N:     return "RADL_N";
        case HEVC_NAL_RADL_R:     return "RADL_R";
        case HEVC_NAL_RASL_N:     return "RASL_N";
        case HEVC_NAL_RASL_R:     return "RASL_R";
        case HEVC_NAL_BLA_W_LP:   return "BLA_W_LP";
        case HEVC_NAL_BLA_W_RADL: return "BLA_W_RADL";
        case HEVC_NAL_BLA_N_LP:   return "BLA_N_LP";
        case HEVC_NAL_IDR_W_RADL: return "IDR_W_RADL";
        case HEVC_NAL_IDR_N_LP:   return "IDR_N_LP";
        case HEVC_NAL_CRA_NUT:    return "CRA";
        case HEVC_NAL_VPS:        return "VPS";
        case HEVC_NAL_SPS:        return "SPS";
        case HEVC_NAL_PPS:        return "PPS";
        case HEVC_NAL_AUD:        return "AUD";
        case HEVC_NAL_EOS_NUT:    return "EOS";
        case HEVC_NAL_EOB_NUT:    return "EOB";
        case HEVC_NAL_FD_NUT:     return "FD";
        case HEVC_NAL_PREFIX_SEI: return "PREFIX_SEI";
        case HEVC_NAL_SUFFIX_SEI: return "SUFFIX_SEI";
        default:                  return QString("NAL_%1").arg(type);
    }
}

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

QString TTH265AccessUnit::frameTypeString() const
{
    QString typeStr;

    // Frame type based on slice type
    switch (mSliceType) {
        case HEVC_SLICE_I: typeStr = "I"; break;
        case HEVC_SLICE_P: typeStr = "P"; break;
        case HEVC_SLICE_B: typeStr = "B"; break;
        default:           typeStr = "?"; break;
    }

    // Add special markers
    if (mIsIDR) {
        typeStr += " (IDR)";
    } else if (mIsRAP) {
        typeStr += " (RAP)";
    }

    if (mIsReference) {
        typeStr += " [Ref]";
    }

    return typeStr;
}
