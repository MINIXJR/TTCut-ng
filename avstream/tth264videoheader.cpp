/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth264videoheader.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// H.264/AVC Video Header Classes Implementation
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

#include "tth264videoheader.h"

// -----------------------------------------------------------------------------
// TTH264VideoHeader - Abstract base class
// -----------------------------------------------------------------------------
TTH264VideoHeader::TTH264VideoHeader()
    : TTVideoHeader()
    , mNalType(NAL_UNSPECIFIED)
    , mNalRefIdc(0)
{
}

TTH264VideoHeader::~TTH264VideoHeader()
{
}

bool TTH264VideoHeader::readHeader(TTFileBuffer* /* stream */)
{
    // H.264 headers are parsed via libav, not directly from file buffer
    return false;
}

bool TTH264VideoHeader::readHeader(TTFileBuffer* /* stream */, quint64 /* offset */)
{
    // H.264 headers are parsed via libav, not directly from file buffer
    return false;
}

void TTH264VideoHeader::parseBasicData(quint8* /* data */, int /* offset */)
{
    // H.264 headers are parsed via libav, not directly from raw data
}

// -----------------------------------------------------------------------------
// TTH264SPS - Sequence Parameter Set
// -----------------------------------------------------------------------------
TTH264SPS::TTH264SPS()
    : TTH264VideoHeader()
    , mProfileIdc(0)
    , mLevelIdc(0)
    , mWidth(0)
    , mHeight(0)
    , mFrameRate(0.0)
    , mHasFrameRate(false)
    , mSpsId(0)
{
    mNalType = NAL_SPS;
}

QString TTH264SPS::profileString() const
{
    switch (mProfileIdc) {
        case PROFILE_BASELINE:        return "Baseline";
        case PROFILE_MAIN:            return "Main";
        case PROFILE_EXTENDED:        return "Extended";
        case PROFILE_HIGH:            return "High";
        case PROFILE_HIGH_10:         return "High 10";
        case PROFILE_HIGH_422:        return "High 4:2:2";
        case PROFILE_HIGH_444:        return "High 4:4:4 Predictive";
        case PROFILE_CAVLC_444_INTRA: return "CAVLC 4:4:4 Intra";
        default:                      return QString("Unknown (%1)").arg(mProfileIdc);
    }
}

QString TTH264SPS::levelString() const
{
    // Level is stored as level * 10 (e.g., 40 = 4.0, 41 = 4.1)
    int major = mLevelIdc / 10;
    int minor = mLevelIdc % 10;
    return QString("%1.%2").arg(major).arg(minor);
}

// -----------------------------------------------------------------------------
// TTH264PPS - Picture Parameter Set
// -----------------------------------------------------------------------------
TTH264PPS::TTH264PPS()
    : TTH264VideoHeader()
    , mPpsId(0)
    , mSpsId(0)
{
    mNalType = NAL_PPS;
}

// -----------------------------------------------------------------------------
// TTH264AccessUnit - One complete video frame
// -----------------------------------------------------------------------------
TTH264AccessUnit::TTH264AccessUnit()
    : TTH264VideoHeader()
    , mSliceType(SLICE_TYPE_I)
    , mIsIDR(false)
    , mFrameNum(0)
    , mPoc(0)
    , mPts(0)
    , mDts(0)
    , mGopIndex(0)
    , mFrameSize(0)
{
    mNalType = NAL_SLICE;
}

QString TTH264AccessUnit::frameTypeString() const
{
    if (mIsIDR) {
        return "IDR";
    }

    switch (mSliceType) {
        case SLICE_TYPE_I:
        case SLICE_TYPE_I_ALL:
            return "I";
        case SLICE_TYPE_P:
        case SLICE_TYPE_P_ALL:
            return "P";
        case SLICE_TYPE_B:
        case SLICE_TYPE_B_ALL:
            return "B";
        case SLICE_TYPE_SP:
        case SLICE_TYPE_SP_ALL:
            return "SP";
        case SLICE_TYPE_SI:
        case SLICE_TYPE_SI_ALL:
            return "SI";
        default:
            return "?";
    }
}
