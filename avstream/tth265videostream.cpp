/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth265videostream.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH265VIDEOSTREAM
// H.265/HEVC Video Stream handling implementation
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

#include "tth265videostream.h"
#include "ttvideoheaderlist.h"
#include "ttvideoindexlist.h"
#include "ttesinfo.h"
#include "../data/ttcutparameter.h"
#include "../common/ttexception.h"
#include "../common/ttcut.h"

#include <QDebug>
#include <QDir>
#include <QProcess>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TTH265VideoStream::TTH265VideoStream(const QFileInfo& fInfo)
    : TTVideoStream(fInfo)
    , mFFmpeg(nullptr)
    , mSPS(nullptr)
    , mVPS(nullptr)
    , mEncoderPreset("medium")
    , mEncoderCrf(20)       // HEVC CRF slightly higher than H.264 for same quality
    , mEncoderProfile("main")
{
    mLog = TTMessageLogger::getInstance();
    mLog->infoMsg(__FILE__, __LINE__,
        QString("Creating H.265/HEVC video stream for: %1").arg(fInfo.filePath()));
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
TTH265VideoStream::~TTH265VideoStream()
{
    closeStream();

    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();

    delete mSPS;
    delete mVPS;
}

// -----------------------------------------------------------------------------
// Return stream type
// -----------------------------------------------------------------------------
TTAVTypes::AVStreamType TTH265VideoStream::streamType() const
{
    return TTAVTypes::h265_video;
}

// -----------------------------------------------------------------------------
// Return frame rate (using stored value from FFmpeg)
// -----------------------------------------------------------------------------
float TTH265VideoStream::frameRate()
{
    // Use the frame_rate member stored in createHeaderList()
    // This avoids trying to access MPEG-2 sequence headers which don't exist in H.265
    return frame_rate;
}

// -----------------------------------------------------------------------------
// Open stream using FFmpeg wrapper
// -----------------------------------------------------------------------------
bool TTH265VideoStream::openStream()
{
    if (mFFmpeg && mFFmpeg->isOpen()) {
        return true;
    }

    if (!mFFmpeg) {
        mFFmpeg = new TTFFmpegWrapper();
    }

    if (!mFFmpeg->openFile(stream_info->filePath())) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to open H.265 stream: %1").arg(mFFmpeg->lastError()));
        return false;
    }

    // Verify this is indeed H.265
    TTVideoCodecType codecType = mFFmpeg->detectVideoCodec();
    if (codecType != CODEC_H265) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("File is not H.265/HEVC: detected %1")
                .arg(TTFFmpegWrapper::codecTypeToString(codecType)));
        closeStream();
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Close stream
// -----------------------------------------------------------------------------
bool TTH265VideoStream::closeStream()
{
    if (mFFmpeg) {
        mFFmpeg->closeFile();
        delete mFFmpeg;
        mFFmpeg = nullptr;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Create header list by parsing NAL units via FFmpeg
// -----------------------------------------------------------------------------
int TTH265VideoStream::createHeaderList()
{
    if (!openStream()) {
        return -1;
    }

    mLog->infoMsg(__FILE__, __LINE__, "Building H.265 header list...");

    // Build frame index first
    if (!mFFmpeg->buildFrameIndex()) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(mFFmpeg->lastError()));
        return -1;
    }

    // Build GOP index
    if (!mFFmpeg->buildGOPIndex()) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build GOP index: %1").arg(mFFmpeg->lastError()));
        return -1;
    }

    // Extract SPS/VPS info from first video stream
    int videoIdx = mFFmpeg->findBestVideoStream();
    if (videoIdx >= 0) {
        TTStreamInfo info = mFFmpeg->getStreamInfo(videoIdx);

        // Create SPS with extracted info
        if (!mSPS) {
            mSPS = new TTH265SPS();
        }
        mSPS->setWidth(info.width);
        mSPS->setHeight(info.height);
        mSPS->setProfile(info.profile);
        mSPS->setLevel(info.level);
        if (info.frameRate > 0) {
            mSPS->setFrameRate(info.frameRate);
        }

        // Store frame rate - prefer .info file over ffmpeg detection
        frame_rate = static_cast<float>(info.frameRate);
        QString infoFile = TTESInfo::findInfoFile(filePath());
        if (!infoFile.isEmpty()) {
            TTESInfo esInfo(infoFile);
            if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
                frame_rate = static_cast<float>(esInfo.frameRate());
                mSPS->setFrameRate(esInfo.frameRate());
                mLog->infoMsg(__FILE__, __LINE__,
                    QString("Using frame rate from .info file: %1 fps").arg(frame_rate));
            }
        }

        // Store bit rate
        bit_rate = static_cast<float>(info.bitRate) / 1000.0f; // kbit/s

        // Create VPS (minimal info)
        if (!mVPS) {
            mVPS = new TTH265VPS();
        }

        mLog->infoMsg(__FILE__, __LINE__,
            QString("H.265 stream: %1x%2, %3 fps, %4 %5")
                .arg(info.width)
                .arg(info.height)
                .arg(info.frameRate, 0, 'f', 2)
                .arg(mSPS->profileString())
                .arg(mSPS->levelString()));
    }

    buildHeaderListFromFFmpeg();

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Header list created: %1 frames, %2 GOPs")
            .arg(mFFmpeg->frameCount())
            .arg(mFFmpeg->gopCount()));

    return mFFmpeg->frameCount();
}

// -----------------------------------------------------------------------------
// Build header list from FFmpeg frame index
// -----------------------------------------------------------------------------
void TTH265VideoStream::buildHeaderListFromFFmpeg()
{
    if (!mFFmpeg) return;

    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();

    const QList<TTFrameInfo>& frameIndex = mFFmpeg->frameIndex();

    for (int i = 0; i < frameIndex.size(); i++) {
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

// -----------------------------------------------------------------------------
// Create index list
// -----------------------------------------------------------------------------
int TTH265VideoStream::createIndexList()
{
    if (!mFFmpeg || mFFmpeg->frameCount() == 0) {
        mLog->errorMsg(__FILE__, __LINE__,
            "Cannot create index list: header list not built");
        return -1;
    }

    mLog->infoMsg(__FILE__, __LINE__, "Creating H.265 index list...");

    if (index_list == nullptr) {
        index_list = new TTVideoIndexList();
    }

    // Build index from frame info
    buildIndexListFromFFmpeg();

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Index list created: %1 entries").arg(index_list->count()));

    return index_list->count();
}

// -----------------------------------------------------------------------------
// Build index list from FFmpeg data
// -----------------------------------------------------------------------------
void TTH265VideoStream::buildIndexListFromFFmpeg()
{
    if (!mFFmpeg || !index_list) return;

    for (int i = 0; i < mAccessUnits.size(); i++) {
        TTH265AccessUnit* au = mAccessUnits[i];

        TTVideoIndex* vidIndex = new TTVideoIndex();
        vidIndex->setDisplayOrder(i);
        vidIndex->setHeaderListIndex(i);

        // Map slice type to picture coding type (compatible with MPEG-2)
        int codingType = 1; // I-frame
        if (!au->isIDR()) {
            switch (au->sliceType()) {
                case HEVC_SLICE_I:
                    codingType = 1;
                    break;
                case HEVC_SLICE_P:
                    codingType = 2;
                    break;
                case HEVC_SLICE_B:
                    codingType = 3;
                    break;
            }
        }
        vidIndex->setPictureCodingType(codingType);

        index_list->add(vidIndex);
    }
}

// -----------------------------------------------------------------------------
// Cut operation
// -----------------------------------------------------------------------------
void TTH265VideoStream::cut(int start, int end, TTCutParameter* cp)
{
    if (!cp) {
        mLog->errorMsg(__FILE__, __LINE__, "Cut parameter is null");
        return;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.265 cut from frame %1 to %2").arg(start).arg(end));

    // Find RAP boundaries for the cut range
    int rapBefore = findRAPBefore(start);
    int rapAfter = findRAPAfter(end);

    // Determine which segments need re-encoding
    bool needStartReencode = (start != rapBefore && start > 0);
    bool needEndReencode = false;

    // Check if end frame is the frame before a RAP (clean boundary)
    int nextRAP = findRAPAfter(end);
    if (nextRAP > 0 && end != (nextRAP - 1)) {
        needEndReencode = true;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Cut strategy: start=%1, end=%2, rapBefore=%3, rapAfter=%4")
            .arg(start).arg(end).arg(rapBefore).arg(rapAfter));
    mLog->infoMsg(__FILE__, __LINE__,
        QString("Need re-encode: start=%1, end=%2")
            .arg(needStartReencode).arg(needEndReencode));

    if (needStartReencode || needEndReencode) {
        // Use partial re-encoding
        encodeSegment(start, end, cp);
    } else {
        // Pure stream copy possible
        copyFrameSegment(rapBefore, rapAfter, cp);
    }
}

// -----------------------------------------------------------------------------
// Check if position is valid cut-in point
// With encoder mode: any frame is valid (will be re-encoded)
// Without encoder mode: must be RAP for lossless cutting
// pos < 0 means use currentIndex() (like MPEG-2 implementation)
// -----------------------------------------------------------------------------
bool TTH265VideoStream::isCutInPoint(int pos)
{
    // When encoder mode is enabled, any frame can be a cut-in point
    if (TTCut::encoderMode) {
        return true;
    }

    // Use current position if pos < 0 (same convention as MPEG-2)
    int index = (pos < 0) ? currentIndex() : pos;

    if (index < 0 || index >= mAccessUnits.size()) {
        return false;
    }

    // For lossless cutting, must be a Random Access Point
    TTH265AccessUnit* au = mAccessUnits[index];
    return au->isRAP();
}

// -----------------------------------------------------------------------------
// Check if position is valid cut-out point
// With encoder mode: any frame is valid
// Without encoder mode: any frame (will re-encode at boundaries if needed)
// pos < 0 means use currentIndex()
// -----------------------------------------------------------------------------
bool TTH265VideoStream::isCutOutPoint(int pos)
{
    // When encoder mode is enabled, any frame can be a cut-out point
    if (TTCut::encoderMode) {
        return true;
    }

    // Use current position if pos < 0 (same convention as MPEG-2)
    int index = (pos < 0) ? currentIndex() : pos;

    if (index < 0 || index >= mAccessUnits.size()) {
        return false;
    }

    // For lossless cutting, should be the frame before a RAP
    // Or the last frame
    if (index == mAccessUnits.size() - 1) {
        return true;
    }

    // Check if next frame is a RAP
    if (index + 1 < mAccessUnits.size()) {
        TTH265AccessUnit* nextAU = mAccessUnits[index + 1];
        if (nextAU->isRAP()) {
            return true;
        }
    }

    return false;
}

// -----------------------------------------------------------------------------
// Get frame at index
// -----------------------------------------------------------------------------
TTH265AccessUnit* TTH265VideoStream::frameAt(int index)
{
    if (index >= 0 && index < mAccessUnits.size()) {
        return mAccessUnits[index];
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Find Random Access Point before frame index
// -----------------------------------------------------------------------------
int TTH265VideoStream::findRAPBefore(int frameIndex)
{
    for (int i = frameIndex; i >= 0; i--) {
        if (mAccessUnits[i]->isRAP()) {
            return i;
        }
    }
    return 0;  // First frame is always accessible
}

// -----------------------------------------------------------------------------
// Find Random Access Point after frame index
// -----------------------------------------------------------------------------
int TTH265VideoStream::findRAPAfter(int frameIndex)
{
    for (int i = frameIndex; i < mAccessUnits.size(); i++) {
        if (mAccessUnits[i]->isRAP()) {
            return i;
        }
    }
    return mAccessUnits.size() - 1;  // Last frame
}

// -----------------------------------------------------------------------------
// Get GOP count
// -----------------------------------------------------------------------------
int TTH265VideoStream::gopCount() const
{
    if (mFFmpeg) {
        return mFFmpeg->gopCount();
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Find GOP for frame
// -----------------------------------------------------------------------------
int TTH265VideoStream::findGOPForFrame(int frameIndex)
{
    if (mFFmpeg) {
        return mFFmpeg->findGOPForFrame(frameIndex);
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Get GOP start frame
// -----------------------------------------------------------------------------
int TTH265VideoStream::getGOPStart(int gopIndex)
{
    if (mFFmpeg && gopIndex >= 0 && gopIndex < mFFmpeg->gopCount()) {
        return mFFmpeg->gopIndex()[gopIndex].startFrame;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Get GOP end frame
// -----------------------------------------------------------------------------
int TTH265VideoStream::getGOPEnd(int gopIndex)
{
    if (mFFmpeg && gopIndex >= 0 && gopIndex < mFFmpeg->gopCount()) {
        return mFFmpeg->gopIndex()[gopIndex].endFrame;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Copy segment without re-encoding
// -----------------------------------------------------------------------------
void TTH265VideoStream::copyFrameSegment(int startFrame, int endFrame, TTCutParameter* cp)
{
    Q_UNUSED(cp);

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Copy segment: frames %1 to %2").arg(startFrame).arg(endFrame));

    // TODO: Implement actual byte-level copying
    // This requires reading packets from source and writing to output TTFileBuffer
    // For now, this is a placeholder
}

// -----------------------------------------------------------------------------
// Encode segment (partial re-encoding)
// -----------------------------------------------------------------------------
void TTH265VideoStream::encodeSegment(int startFrame, int endFrame, TTCutParameter* cp)
{
    Q_UNUSED(cp);

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Encode segment: frames %1 to %2").arg(startFrame).arg(endFrame));

    // Get timestamps for the segment
    double startTime = mFFmpeg->ptsToSeconds(
        mAccessUnits[startFrame]->pts(),
        mFFmpeg->findBestVideoStream());
    double endTime = mFFmpeg->ptsToSeconds(
        mAccessUnits[endFrame]->pts(),
        mFFmpeg->findBestVideoStream());

    // TODO: Implement actual re-encoding using ffmpeg
    // This will require:
    // 1. Decode frames from startFrame to endFrame
    // 2. Re-encode with H.265 codec
    // 3. Write to output TTFileBuffer
    Q_UNUSED(startTime);
    Q_UNUSED(endTime);
}

// -----------------------------------------------------------------------------
// Check if NAL type is a Random Access Point
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Re-encode part of stream with H.265
// -----------------------------------------------------------------------------
void TTH265VideoStream::encodePartH265(int start, int end, TTCutParameter* cp)
{
    // Delegate to encodeSegment
    encodeSegment(start, end, cp);
}
