/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2024 / TTCut-ng                                */
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
/* either version 2 of the License, or (at your option) any later version.    */
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
#include "../data/ttcutparameter.h"
#include "../common/ttcut.h"

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
    , mEncoderPreset("medium")
    , mEncoderCrf(18)
    , mEncoderProfile("high")
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
    if (!openStream()) {
        return -1;
    }

    mLog->infoMsg(__FILE__, __LINE__, "Creating H.264 header list...");

    // Get stream info and create SPS
    int videoStreamIdx = mFFmpeg->findBestVideoStream();
    if (videoStreamIdx < 0) {
        mLog->errorMsg(__FILE__, __LINE__, "No video stream found");
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

    // Store frame rate and bit rate
    frame_rate = static_cast<float>(streamInfo.frameRate);
    bit_rate = static_cast<float>(streamInfo.bitRate) / 1000.0f; // kbit/s

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 stream: %1x%2 @ %3 fps, Profile: %4, Level: %5")
            .arg(streamInfo.width)
            .arg(streamInfo.height)
            .arg(streamInfo.frameRate, 0, 'f', 2)
            .arg(mSPS->profileString())
            .arg(mSPS->levelString()));

    // Build frame index
    if (!mFFmpeg->buildFrameIndex(videoStreamIdx)) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("Failed to build frame index: %1").arg(mFFmpeg->lastError()));
        return -1;
    }

    // Build GOP index
    mFFmpeg->buildGOPIndex();

    // Create access units from frame index
    buildHeaderListFromFFmpeg();

    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 header list created: %1 frames, %2 GOPs")
            .arg(mAccessUnits.size())
            .arg(mFFmpeg->gopCount()));

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
// Check if position is valid cut-in point (must be IDR frame)
// -----------------------------------------------------------------------------
bool TTH264VideoStream::isCutInPoint(int pos)
{
    if (pos < 0 || pos >= mAccessUnits.size()) {
        return false;
    }

    return mAccessUnits[pos]->isIDR();
}

// -----------------------------------------------------------------------------
// Check if position is valid cut-out point
// Any frame can be cut-out, but non-IDR requires re-encoding
// -----------------------------------------------------------------------------
bool TTH264VideoStream::isCutOutPoint(int pos)
{
    if (pos < 0 || pos >= mAccessUnits.size()) {
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
// Main cut operation
// -----------------------------------------------------------------------------
void TTH264VideoStream::cut(int cutInPos, int cutOutPos, TTCutParameter* cutParams)
{
    mLog->infoMsg(__FILE__, __LINE__,
        QString("H.264 cut: %1 -> %2").arg(cutInPos).arg(cutOutPos));

    if (!openStream()) {
        mLog->errorMsg(__FILE__, __LINE__, "Failed to open stream for cutting");
        return;
    }

    // Check if cut-in is at IDR
    bool cutInAtIDR = isCutInPoint(cutInPos);
    bool cutOutAtIDR = isCutInPoint(cutOutPos);

    if (cutInAtIDR && cutOutAtIDR) {
        // Both at IDR - can copy directly without re-encoding
        mLog->infoMsg(__FILE__, __LINE__, "Both cut points at IDR - direct copy");
        copyFrameSegment(cutInPos, cutOutPos, cutParams);
    } else {
        // Need to re-encode at least part of the segment
        mLog->infoMsg(__FILE__, __LINE__, "Cut points require partial re-encoding");

        // Find IDR boundaries
        int idrBefore = findIDRBefore(cutInPos);
        int idrAfter = findIDRAfter(cutOutPos);

        if (idrBefore < 0) idrBefore = 0;
        if (idrAfter < 0) idrAfter = mAccessUnits.size() - 1;

        // Strategy:
        // 1. Re-encode from idrBefore to cutInPos (if cutInPos != idrBefore)
        // 2. Copy from cutInPos to cutOutPos (the main segment)
        // 3. Re-encode from cutOutPos to idrAfter (if cutOutPos != idrAfter)

        // For simplicity in initial implementation, re-encode the affected GOPs
        if (!cutInAtIDR) {
            // Re-encode the first GOP segment
            int gopStart = findIDRBefore(cutInPos);
            encodeSegment(gopStart, cutInPos, cutParams);
        }

        // Copy the middle segment (if there is one between cut points at IDR boundaries)
        int copyStart = cutInAtIDR ? cutInPos : findIDRAfter(cutInPos);
        int copyEnd = cutOutAtIDR ? cutOutPos : findIDRBefore(cutOutPos);

        if (copyStart <= copyEnd && copyStart >= 0) {
            copyFrameSegment(copyStart, copyEnd, cutParams);
        }

        if (!cutOutAtIDR) {
            // Re-encode the last GOP segment
            int gopEnd = findIDRAfter(cutOutPos);
            if (gopEnd < 0) gopEnd = mAccessUnits.size() - 1;
            encodeSegment(cutOutPos, gopEnd, cutParams);
        }
    }

    closeStream();
}

// -----------------------------------------------------------------------------
// Copy segment directly (between keyframes)
// -----------------------------------------------------------------------------
void TTH264VideoStream::copyFrameSegment(int startFrame, int endFrame, TTCutParameter* cp)
{
    Q_UNUSED(cp);

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Copy segment: frames %1 to %2").arg(startFrame).arg(endFrame));

    // TODO: Implement actual byte-level copying
    // This requires reading packets from source and writing to output
    // For now, this is a placeholder
}

// -----------------------------------------------------------------------------
// Encode segment using ffmpeg
// -----------------------------------------------------------------------------
void TTH264VideoStream::encodeSegment(int startFrame, int endFrame, TTCutParameter* cp)
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

    // Build ffmpeg command for re-encoding
    QStringList args;
    args << "-y"                                    // Overwrite output
         << "-ss" << QString::number(startTime, 'f', 6)  // Start time
         << "-i" << filePath()                      // Input file
         << "-t" << QString::number(endTime - startTime, 'f', 6)  // Duration
         << "-c:v" << "libx264"                     // H.264 encoder
         << "-preset" << mEncoderPreset             // Encoding preset
         << "-crf" << QString::number(mEncoderCrf)  // Quality
         << "-profile:v" << mEncoderProfile         // Profile
         << "-pix_fmt" << "yuv420p";                // Pixel format

    // Add GOP settings to match source
    if (mSPS && mSPS->hasFrameRate()) {
        int gopSize = static_cast<int>(mSPS->frameRate() * 0.5); // ~0.5 second GOP
        args << "-g" << QString::number(gopSize);
    }

    // Output file
    QDir tempDir(TTCut::tempDirPath);
    QString outputFile = tempDir.filePath(
        QString("h264_encode_%1_%2.h264").arg(startFrame).arg(endFrame));
    args << "-f" << "h264"
         << outputFile;

    mLog->infoMsg(__FILE__, __LINE__,
        QString("FFmpeg encode command: ffmpeg %1").arg(args.join(" ")));

    // Execute ffmpeg
    QProcess proc;
    proc.start("/usr/bin/ffmpeg", args);

    if (!proc.waitForStarted(5000)) {
        mLog->errorMsg(__FILE__, __LINE__, "FFmpeg failed to start");
        return;
    }

    if (!proc.waitForFinished(300000)) { // 5 minute timeout
        mLog->errorMsg(__FILE__, __LINE__, "FFmpeg timed out");
        proc.kill();
        return;
    }

    if (proc.exitCode() != 0) {
        mLog->errorMsg(__FILE__, __LINE__,
            QString("FFmpeg encoding failed: %1")
                .arg(QString::fromUtf8(proc.readAllStandardError())));
        return;
    }

    mLog->infoMsg(__FILE__, __LINE__,
        QString("Segment encoded successfully: %1").arg(outputFile));
}

// -----------------------------------------------------------------------------
// Encode part (compatibility wrapper for existing code)
// -----------------------------------------------------------------------------
void TTH264VideoStream::encodePartH264(int start, int end, TTCutParameter* cp)
{
    encodeSegment(start, end, cp);
}
