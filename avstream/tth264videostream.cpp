/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : tth264videostream.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTH264VIDEOSTREAM
// H.264/AVC Video Stream handling for frame-accurate cutting
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

#include "tth264videostream.h"
#include "ttvideoheaderlist.h"
#include "ttvideoindexlist.h"
#include "ttesinfo.h"
#include "../data/ttcutparameter.h"
#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../common/istatusreporter.h"

#include <QDebug>
#include <QDir>
#include <QProcess>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TTH264VideoStream::TTH264VideoStream(const QFileInfo& fInfo)
    : TTVideoStream(fInfo)
    , mFFmpeg(nullptr)
    , mSPS(nullptr)
{
    mLog = TTMessageLogger::getInstance();
    stream_type = TTAVTypes::h264_video;
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
TTH264VideoStream::~TTH264VideoStream()
{
    closeStream();

    // Clean up access units
    qDeleteAll(mAccessUnits);
    mAccessUnits.clear();

    delete mSPS;
    mSPS = nullptr;
}

// -----------------------------------------------------------------------------
// Return stream type
// -----------------------------------------------------------------------------
TTAVTypes::AVStreamType TTH264VideoStream::streamType() const
{
    return TTAVTypes::h264_video;
}

// -----------------------------------------------------------------------------
// Return frame rate (using stored value from FFmpeg)
// -----------------------------------------------------------------------------
float TTH264VideoStream::frameRate()
{
    // Use the frame_rate member stored in createHeaderList()
    // This avoids trying to access MPEG-2 sequence headers which don't exist in H.264
    return frame_rate;
}

int TTH264VideoStream::paffLog2MaxFrameNum() const
{
    return mFFmpeg ? mFFmpeg->h264Log2MaxFrameNum() : 4;
}

// -----------------------------------------------------------------------------
// Open stream using FFmpeg wrapper
// -----------------------------------------------------------------------------
bool TTH264VideoStream::openStream()
{
    if (mFFmpeg != nullptr) {
        return true; // Already open
    }

    mFFmpeg = new TTFFmpegWrapper();

    if (!mFFmpeg->openFile(filePath())) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to open H.264 stream: %1").arg(mFFmpeg->lastError()));
        delete mFFmpeg;
        mFFmpeg = nullptr;
        return false;
    }

    // Verify this is actually H.264
    TTVideoCodecType codecType = mFFmpeg->detectVideoCodec();
    if (codecType != CODEC_H264) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("File is not H.264, detected: %1")
                .arg(TTFFmpegWrapper::codecTypeToString(codecType)));
        delete mFFmpeg;
        mFFmpeg = nullptr;
        return false;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Opened H.264 stream: %1").arg(filePath()));

    return true;
}

// -----------------------------------------------------------------------------
// Close stream
// -----------------------------------------------------------------------------
bool TTH264VideoStream::closeStream()
{
    if (mFFmpeg) {
        mFFmpeg->closeFile();
        delete mFFmpeg;
        mFFmpeg = nullptr;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Create header list using FFmpeg
// -----------------------------------------------------------------------------
int TTH264VideoStream::createHeaderList()
{
    // Emit Start with total=100 for percentage-based progress
    emit statusReport(StatusReportArgs::Start, tr("Opening H.264 stream..."), 100);

    if (!openStream()) {
        emit statusReport(StatusReportArgs::Error, tr("Failed to open H.264 stream"), 0);
        return -1;
    }

    // Forward FFmpeg progress to statusReport (buildFrameIndex is the slow part).
    // Must be done after openStream() since that is what creates mFFmpeg.
    connect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this, [this](int percent, const QString&) {
        // Map FFmpeg 0-100% to our 10-80% range (buildFrameIndex phase)
        int mapped = 10 + percent * 70 / 100;
        emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), mapped);
    });

    mLog->infoMsg(__FILE__, __LINE__, "Creating H.264 header list...");
    emit statusReport(StatusReportArgs::Step, tr("Creating H.264 header list..."), 10);

    // Get stream info and create SPS
    int videoStreamIdx = mFFmpeg->findBestVideoStream();
    if (videoStreamIdx < 0) {
        mLog->errorMsg(__FILE__, __LINE__, "No video stream found");
        emit statusReport(StatusReportArgs::Error, tr("No video stream found"), 0);
        return -1;
    }

    TTStreamInfo streamInfo = mFFmpeg->getStreamInfo(videoStreamIdx);

    // Create SPS with video information
    if (mSPS) {
        delete mSPS;
    }
    mSPS = new TTH264SPS();
    mSPS->setWidth(streamInfo.width);
    mSPS->setHeight(streamInfo.height);
    mSPS->setProfileIdc(streamInfo.profile);
    mSPS->setLevelIdc(streamInfo.level);
    if (streamInfo.frameRate > 0) {
        mSPS->setFrameRate(streamInfo.frameRate);
    }

    // Store frame rate - prefer .info file over ffmpeg detection
    frame_rate = static_cast<float>(streamInfo.frameRate);

    // Check for .info file with correct frame rate
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
    bit_rate = static_cast<float>(streamInfo.bitRate) / 1000.0f; // kbit/s

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 stream: %1x%2 @ %3 fps, Profile: %4, Level: %5")
            .arg(streamInfo.width)
            .arg(streamInfo.height)
            .arg(frame_rate, 0, 'f', 2)
            .arg(mSPS->profileString())
            .arg(mSPS->levelString()));

    emit statusReport(StatusReportArgs::Step, tr("Building frame index..."), 10);

    // Build frame index (slow — progress forwarded via lambda above)
    if (!mFFmpeg->buildFrameIndex(videoStreamIdx)) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(mFFmpeg->lastError()));
        disconnect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this, nullptr);
        emit statusReport(StatusReportArgs::Error, tr("Failed to build frame index"), 0);
        return -1;
    }

    // Disconnect progress forwarding
    disconnect(mFFmpeg, &TTFFmpegWrapper::progressChanged, this, nullptr);

    // Correct frame rate for PAFF streams (field rate → frame rate)
    if (mFFmpeg->isPAFF() && frame_rate > 30) {
        mLog->infoMsg(__FILE__, __LINE__,
            QString("PAFF detected: correcting frame rate from %1 to %2 fps")
                .arg(frame_rate).arg(frame_rate / 2.0f));
        frame_rate /= 2.0f;
        if (mSPS) mSPS->setFrameRate(static_cast<double>(frame_rate));
    }

    emit statusReport(StatusReportArgs::Step, tr("Building GOP index..."), 82);

    // Build GOP index
    mFFmpeg->buildGOPIndex();

    emit statusReport(StatusReportArgs::Step, tr("Processing frames..."), 90);

    // Create access units from frame index
    buildHeaderListFromFFmpeg();

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 header list created: %1 frames, %2 GOPs")
            .arg(mAccessUnits.size())
            .arg(mFFmpeg->gopCount()));

    emit statusReport(StatusReportArgs::Finished, tr("H.264 header list created"), 100);

    return mAccessUnits.size();
}

// -----------------------------------------------------------------------------
// Build header list from FFmpeg frame index
// -----------------------------------------------------------------------------
void TTH264VideoStream::buildHeaderListFromFFmpeg()
{
    // Clear existing access units
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

        // Set slice type based on frame type
        if (frame.isKeyframe) {
            au->setSliceType(SLICE_TYPE_I);
            au->setNalType(NAL_SLICE_IDR);
        } else if (frame.frameType == 1) { // AV_PICTURE_TYPE_I
            au->setSliceType(SLICE_TYPE_I);
            au->setNalType(NAL_SLICE);
        } else if (frame.frameType == 2) { // AV_PICTURE_TYPE_P
            au->setSliceType(SLICE_TYPE_P);
            au->setNalType(NAL_SLICE);
        } else if (frame.frameType == 3) { // AV_PICTURE_TYPE_B
            au->setSliceType(SLICE_TYPE_B);
            au->setNalType(NAL_SLICE);
        } else {
            au->setSliceType(SLICE_TYPE_P); // Default to P
            au->setNalType(NAL_SLICE);
        }

        mAccessUnits.append(au);
    }
}

// -----------------------------------------------------------------------------
// Create index list
// -----------------------------------------------------------------------------
int TTH264VideoStream::createIndexList()
{
    if (mAccessUnits.isEmpty()) {
        mLog->errorMsg(__FILE__, __LINE__,
            "Cannot create index list: no frames in header list");
        return -1;
    }

    // Initialize index list
    if (index_list == nullptr) {
        index_list = new TTVideoIndexList();
    }

    for (int i = 0; i < mAccessUnits.size(); ++i) {
        TTH264AccessUnit* au = mAccessUnits[i];

        TTVideoIndex* vidIndex = new TTVideoIndex();
        vidIndex->setDisplayOrder(i); // TODO: Use POC for display order
        vidIndex->setHeaderListIndex(i);

        // Map slice type to picture coding type (compatible with MPEG-2)
        int codingType = 1; // I-frame
        if (!au->isIDR()) {
            switch (au->sliceType()) {
                case SLICE_TYPE_I:
                case SLICE_TYPE_I_ALL:
                    codingType = 1;
                    break;
                case SLICE_TYPE_P:
                case SLICE_TYPE_P_ALL:
                    codingType = 2;
                    break;
                case SLICE_TYPE_B:
                case SLICE_TYPE_B_ALL:
                    codingType = 3;
                    break;
                default:
                    codingType = 2;
            }
        }
        vidIndex->setPictureCodingType(codingType);

        index_list->add(vidIndex);
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 index list created: %1 entries").arg(index_list->count()));

    return index_list->count();
}

// -----------------------------------------------------------------------------
// Check if position is valid cut-in point
// With encoder mode: any frame is valid (will be re-encoded)
// Without encoder mode: must be IDR frame for lossless cutting
// pos < 0 means use currentIndex() (like MPEG-2 implementation)
// -----------------------------------------------------------------------------
bool TTH264VideoStream::isCutInPoint(int pos)
{
    // When encoder mode is enabled, any frame can be a cut-in point
    // (frames will be re-encoded around cut points)
    if (TTCut::encoderMode) {
        return true;
    }

    // Use current position if pos < 0 (same convention as MPEG-2)
    int index = (pos < 0) ? currentIndex() : pos;

    if (index < 0 || index >= mAccessUnits.size()) {
        return false;
    }

    return mAccessUnits[index]->isIDR();
}

// -----------------------------------------------------------------------------
// Check if position is valid cut-out point
// With encoder mode: any frame is valid
// Without encoder mode: any frame (will re-encode at boundaries if needed)
// pos < 0 means use currentIndex()
// -----------------------------------------------------------------------------
bool TTH264VideoStream::isCutOutPoint(int pos)
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

    // For now, allow any frame as cut-out point
    // The cut() method will handle re-encoding if needed
    return true;
}

// -----------------------------------------------------------------------------
// Get frame at index
// -----------------------------------------------------------------------------
TTH264AccessUnit* TTH264VideoStream::frameAt(int index)
{
    if (index >= 0 && index < mAccessUnits.size()) {
        return mAccessUnits[index];
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Find IDR frame before given position
// -----------------------------------------------------------------------------
int TTH264VideoStream::findIDRBefore(int frameIndex)
{
    for (int i = frameIndex; i >= 0; --i) {
        if (mAccessUnits[i]->isIDR()) {
            return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Find IDR frame after given position
// -----------------------------------------------------------------------------
int TTH264VideoStream::findIDRAfter(int frameIndex)
{
    for (int i = frameIndex; i < mAccessUnits.size(); ++i) {
        if (mAccessUnits[i]->isIDR()) {
            return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Get number of GOPs
// -----------------------------------------------------------------------------
int TTH264VideoStream::gopCount() const
{
    if (mFFmpeg) {
        return mFFmpeg->gopCount();
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Find GOP index for frame
// -----------------------------------------------------------------------------
int TTH264VideoStream::findGOPForFrame(int frameIndex)
{
    if (frameIndex >= 0 && frameIndex < mAccessUnits.size()) {
        return mAccessUnits[frameIndex]->gopIndex();
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Get start frame of GOP
// -----------------------------------------------------------------------------
int TTH264VideoStream::getGOPStart(int gopIndex)
{
    for (int i = 0; i < mAccessUnits.size(); ++i) {
        if (mAccessUnits[i]->gopIndex() == gopIndex) {
            return i;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Get end frame of GOP
// -----------------------------------------------------------------------------
int TTH264VideoStream::getGOPEnd(int gopIndex)
{
    int lastFrame = -1;
    for (int i = 0; i < mAccessUnits.size(); ++i) {
        if (mAccessUnits[i]->gopIndex() == gopIndex) {
            lastFrame = i;
        } else if (mAccessUnits[i]->gopIndex() > gopIndex) {
            break;
        }
    }
    return lastFrame;
}

// -----------------------------------------------------------------------------
// cut() — required by TTAVStream pure-virtual contract, but H.264 cutting
// goes through TTESSmartCut (driven by TTCutVideoTask), not through here.
// Throw loudly so a future virtual-dispatch caller cannot silently produce
// empty output via this stale prototype path.
// -----------------------------------------------------------------------------
void TTH264VideoStream::cut(int cutInPos, int cutOutPos, TTCutParameter* /*cp*/)
{
    Q_UNUSED(cutInPos);
    Q_UNUSED(cutOutPos);
    throw TTInvalidOperationException(__FILE__, __LINE__,
        "TTH264VideoStream::cut is a deprecated stub; use TTESSmartCut instead");
}
