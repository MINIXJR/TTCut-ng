/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth264videostream.cpp                                           */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH264VIDEOSTREAM
// H.264/AVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/

#include "tth264videostream.h"

#include <QDebug>

// -----------------------------------------------------------------------------
// Constructor / destructor
// -----------------------------------------------------------------------------
TTH264VideoStream::TTH264VideoStream(const QFileInfo& fInfo)
    : TTH26xVideoStream(fInfo)
    , mSPS(nullptr)
{
    stream_type = TTAVTypes::h264_video;
}

TTH264VideoStream::~TTH264VideoStream()
{
    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();
    delete mSPS;
    mSPS = nullptr;
    // mFFmpeg cleanup is handled by ~TTH26xVideoStream
}

// -----------------------------------------------------------------------------
// Stream identity
// -----------------------------------------------------------------------------
TTAVTypes::AVStreamType TTH264VideoStream::streamType() const
{
    return TTAVTypes::h264_video;
}

int TTH264VideoStream::paffLog2MaxFrameNum() const
{
    return mFFmpeg ? mFFmpeg->h264Log2MaxFrameNum() : 4;
}

// -----------------------------------------------------------------------------
// Hook implementations
// -----------------------------------------------------------------------------
TTVideoCodecType TTH264VideoStream::expectedCodec() const
{
    return CODEC_H264;
}

void TTH264VideoStream::resetSPS()
{
    delete mSPS;
    mSPS = nullptr;
}

void TTH264VideoStream::buildSPSFromStreamInfo(const TTStreamInfo& info)
{
    mSPS = new TTH264SPS();
    mSPS->setWidth(info.width);
    mSPS->setHeight(info.height);
    mSPS->setProfileIdc(info.profile);
    mSPS->setLevelIdc(info.level);
    if (info.frameRate > 0) {
        mSPS->setFrameRate(info.frameRate);
    }
}

void TTH264VideoStream::setSPSFrameRate(double fps)
{
    if (mSPS) mSPS->setFrameRate(fps);
}

QString TTH264VideoStream::spsDescription() const
{
    if (!mSPS) return QString();
    return QString("Profile: %1, Level: %2")
        .arg(mSPS->profileString(), mSPS->levelString());
}

void TTH264VideoStream::buildAccessUnits()
{
    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();

    const QList<TTFrameInfo>& frameIndex = mFFmpeg->frameIndex();
    for (int i = 0; i < frameIndex.size(); ++i) {
        const TTFrameInfo& frame = frameIndex[i];

        TTH264AccessUnit* au = new TTH264AccessUnit();
        au->setHeaderOffset(frame.fileOffset);
        au->setFrameSize(frame.packetSize);
        au->setPts(frame.pts);
        au->setDts(frame.dts);
        au->setGopIndex(frame.gopIndex);
        au->setIDR(frame.isKeyframe);

        if (frame.isKeyframe) {
            au->setSliceType(SLICE_TYPE_I);
            au->setNalType(NAL_SLICE_IDR);
        } else if (frame.frameType == 1) {        // AV_PICTURE_TYPE_I
            au->setSliceType(SLICE_TYPE_I);
            au->setNalType(NAL_SLICE);
        } else if (frame.frameType == 2) {        // AV_PICTURE_TYPE_P
            au->setSliceType(SLICE_TYPE_P);
            au->setNalType(NAL_SLICE);
        } else if (frame.frameType == 3) {        // AV_PICTURE_TYPE_B
            au->setSliceType(SLICE_TYPE_B);
            au->setNalType(NAL_SLICE);
        } else {
            au->setSliceType(SLICE_TYPE_P);       // default to P
            au->setNalType(NAL_SLICE);
        }

        mAccessUnits.append(au);
    }
}

bool TTH264VideoStream::accessUnitIsIDR(int idx) const
{
    if (idx < 0 || idx >= mAccessUnits.size()) return false;
    return mAccessUnits[idx]->isIDR();
}

int TTH264VideoStream::accessUnitToCodingType(int idx) const
{
    if (idx < 0 || idx >= mAccessUnits.size()) return 1;
    TTH264AccessUnit* au = mAccessUnits[idx];
    if (au->isIDR()) return 1;
    switch (au->sliceType()) {
        case SLICE_TYPE_I:
        case SLICE_TYPE_I_ALL:  return 1;
        case SLICE_TYPE_P:
        case SLICE_TYPE_P_ALL:  return 2;
        case SLICE_TYPE_B:
        case SLICE_TYPE_B_ALL:  return 3;
        default:                return 2;
    }
}

// -----------------------------------------------------------------------------
// Typed accessors
// -----------------------------------------------------------------------------
TTH264AccessUnit* TTH264VideoStream::frameAt(int index)
{
    if (index < 0 || index >= mAccessUnits.size()) return nullptr;
    return mAccessUnits[index];
}

int TTH264VideoStream::findIDRAfter(int frameIndex)
{
    for (int i = frameIndex; i < mAccessUnits.size(); ++i) {
        if (mAccessUnits[i]->isIDR()) return i;
    }
    return -1;
}
