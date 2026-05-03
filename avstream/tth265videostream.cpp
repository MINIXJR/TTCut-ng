/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth265videostream.cpp                                           */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOSTREAM
// H.265/HEVC Video Stream — codec-specific bits only. Common ffmpeg / GOP /
// header-list flow lives in TTH26xVideoStream.
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*----------------------------------------------------------------------------*/

#include "tth265videostream.h"

#include <QDebug>

// -----------------------------------------------------------------------------
// Constructor / destructor
// -----------------------------------------------------------------------------
TTH265VideoStream::TTH265VideoStream(const QFileInfo& fInfo)
    : TTH26xVideoStream(fInfo)
    , mSPS(nullptr)
    , mVPS(nullptr)
{
    mLog->infoMsg(__FILE__, __LINE__,
        QString("Creating H.265/HEVC video stream for: %1").arg(fInfo.filePath()));
}

TTH265VideoStream::~TTH265VideoStream()
{
    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();
    delete mSPS;
    mSPS = nullptr;
    delete mVPS;
    mVPS = nullptr;
    // mFFmpeg cleanup is handled by ~TTH26xVideoStream
}

// -----------------------------------------------------------------------------
// Stream identity
// -----------------------------------------------------------------------------
TTAVTypes::AVStreamType TTH265VideoStream::streamType() const
{
    return TTAVTypes::h265_video;
}

// -----------------------------------------------------------------------------
// Hook implementations
// -----------------------------------------------------------------------------
TTVideoCodecType TTH265VideoStream::expectedCodec() const
{
    return CODEC_H265;
}

void TTH265VideoStream::resetSPS()
{
    delete mSPS;
    mSPS = nullptr;
    delete mVPS;
    mVPS = nullptr;
}

void TTH265VideoStream::buildSPSFromStreamInfo(const TTStreamInfo& info)
{
    mSPS = new TTH265SPS();
    mSPS->setWidth(info.width);
    mSPS->setHeight(info.height);
    mSPS->setProfile(info.profile);
    mSPS->setLevel(info.level);
    if (info.frameRate > 0) {
        mSPS->setFrameRate(info.frameRate);
    }

    // Minimal VPS placeholder; full VPS parsing happens elsewhere if needed.
    mVPS = new TTH265VPS();
}

void TTH265VideoStream::setSPSFrameRate(double fps)
{
    if (mSPS) mSPS->setFrameRate(fps);
}

QString TTH265VideoStream::spsDescription() const
{
    if (!mSPS) return QString();
    return QString("%1 %2").arg(mSPS->profileString(), mSPS->levelString());
}

void TTH265VideoStream::buildAccessUnits()
{
    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();

    const QList<TTFrameInfo>& frameIndex = mFFmpeg->frameIndex();

    for (int i = 0; i < frameIndex.size(); ++i) {
        const TTFrameInfo& frame = frameIndex[i];

        TTH265AccessUnit* au = new TTH265AccessUnit();
        au->setFrameIndex(frame.frameIndex);
        au->setPts(frame.pts);
        au->setDts(frame.dts);
        au->setOffset(frame.fileOffset);
        au->setSize(static_cast<int>(frame.packetSize));
        au->setGopIndex(frame.gopIndex);

        // Determine frame type
        if (frame.isKeyframe) {
            au->setSliceType(HEVC_SLICE_I);
            au->setIsIDR(true);
            au->setIsRAP(true);
            au->setIsReference(true);
        } else if (frame.frameType == 1) {  // AV_PICTURE_TYPE_I (non-IDR)
            au->setSliceType(HEVC_SLICE_I);
            au->setIsRAP(true);  // Could be CRA
            au->setIsReference(true);
        } else if (frame.frameType == 2) {  // AV_PICTURE_TYPE_P
            au->setSliceType(HEVC_SLICE_P);
            au->setIsReference(true);
        } else {  // AV_PICTURE_TYPE_B or unknown
            au->setSliceType(HEVC_SLICE_B);
            au->setIsReference(false);  // Assume non-reference B frame
        }

        mAccessUnits.append(au);
    }
}

bool TTH265VideoStream::accessUnitIsIDR(int idx) const
{
    if (idx < 0 || idx >= mAccessUnits.size()) return false;
    return mAccessUnits[idx]->isIDR();
}

bool TTH265VideoStream::accessUnitIsRAP(int idx) const
{
    if (idx < 0 || idx >= mAccessUnits.size()) return false;
    return mAccessUnits[idx]->isRAP();
}

int TTH265VideoStream::accessUnitToCodingType(int idx) const
{
    if (idx < 0 || idx >= mAccessUnits.size()) return 1;
    TTH265AccessUnit* au = mAccessUnits[idx];
    if (au->isIDR()) return 1;
    switch (au->sliceType()) {
        case HEVC_SLICE_I:  return 1;
        case HEVC_SLICE_P:  return 2;
        case HEVC_SLICE_B:  return 3;
        default:            return 1;
    }
}

// -----------------------------------------------------------------------------
// Typed accessors
// -----------------------------------------------------------------------------
TTH265AccessUnit* TTH265VideoStream::frameAt(int index)
{
    if (index < 0 || index >= mAccessUnits.size()) return nullptr;
    return mAccessUnits[index];
}

int TTH265VideoStream::findRAPBefore(int frameIndex)
{
    for (int i = frameIndex; i >= 0; --i) {
        if (mAccessUnits[i]->isRAP()) return i;
    }
    return 0;  // matches previous behaviour: first frame as fallback
}

int TTH265VideoStream::findRAPAfter(int frameIndex)
{
    for (int i = frameIndex; i < mAccessUnits.size(); ++i) {
        if (mAccessUnits[i]->isRAP()) return i;
    }
    return mAccessUnits.size() - 1;
}

bool TTH265VideoStream::isRAPNalType(int nalType)
{
    switch (nalType) {
        case HEVC_NAL_IDR_W_RADL:
        case HEVC_NAL_IDR_N_LP:
        case HEVC_NAL_CRA_NUT:
        case HEVC_NAL_BLA_W_LP:
        case HEVC_NAL_BLA_W_RADL:
        case HEVC_NAL_BLA_N_LP:
            return true;
        default:
            return false;
    }
}
