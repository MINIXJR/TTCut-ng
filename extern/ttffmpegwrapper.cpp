/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttffmpegwrapper.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFFMPEGWRAPPER
// Wrapper class for libav/ffmpeg functionality
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

#include "ttffmpegwrapper.h"
#include "ttessmartcut.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttnaluparser.h"
#include "../common/ttcut.h"

#include <algorithm>
#include <cmath>

#include <QDebug>
#include <QElapsedTimer>
#include <QTime>

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

// Include libav headers (C libraries)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// Static initialization flag
static bool sFFmpegInitialized = false;

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
TTFFmpegWrapper::TTFFmpegWrapper()
    : QObject()
    , mFormatCtx(nullptr)
    , mVideoCodecCtx(nullptr)
    , mSwsCtx(nullptr)
    , mDecodedFrame(nullptr)
    , mRgbFrame(nullptr)
    , mVideoStreamIndex(-1)
    , mAudioStreamIndex(-1)
    , mCurrentFrameIndex(-1)
    , mDecoderFrameIndex(-1)
    , mDecoderDrained(false)
    , mIsElementaryStream(false)
    , mAnalysisMode(false)
    , mIsPAFF(false)
    , mH264Log2MaxFrameNum(4)
    , mH264FrameMbsOnlyFlag(true)
    , mFrameCacheMaxSize(30)
{
    initializeFFmpeg();
}

// ----------------------------------------------------------------------------
// Destructor
// ----------------------------------------------------------------------------
TTFFmpegWrapper::~TTFFmpegWrapper()
{
    closeFile();
}

// ----------------------------------------------------------------------------
// Initialize FFmpeg libraries (call once at startup)
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::initializeFFmpeg()
{
    if (!sFFmpegInitialized) {
        // Note: av_register_all() is deprecated in newer FFmpeg versions
        // and not needed for FFmpeg 4.0+
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
        av_register_all();
#endif
        sFFmpegInitialized = true;
        qDebug() << "FFmpeg initialized, version:" << av_version_info();
    }
}

// ----------------------------------------------------------------------------
// Cleanup FFmpeg (call at shutdown)
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::cleanupFFmpeg()
{
    // Nothing to do in modern FFmpeg
    sFFmpegInitialized = false;
}

// ----------------------------------------------------------------------------
// Open media file
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::openFile(const QString& filePath)
{
    closeFile();

    // Check if this is an elementary stream (by extension)
    bool isES = isElementaryStreamPath(filePath);
    mIsElementaryStream = isES;

    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    if (isES) {
        // For elementary streams, we need special handling
        // Set large probesize and analyzeduration for proper detection
        av_dict_set(&opts, "probesize", "50000000", 0);  // 50MB
        av_dict_set(&opts, "analyzeduration", "10000000", 0);  // 10 seconds
        inputFmt = esInputFormatForPath(filePath);
        qDebug() << "Opening ES file with forced format:" << (inputFmt ? inputFmt->name : "auto");
    }

    int ret = avformat_open_input(&mFormatCtx, filePath.toUtf8().constData(),
                                   inputFmt, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        setError(QString("Could not open file: %1").arg(avErrorToString(ret)));
        return false;
    }

    // For ES files, set larger analyze duration
    if (isES) {
        mFormatCtx->max_analyze_duration = 10 * AV_TIME_BASE;  // 10 seconds
        mFormatCtx->probesize = 50000000;  // 50MB
    }

    ret = avformat_find_stream_info(mFormatCtx, nullptr);
    if (ret < 0) {
        setError(QString("Could not find stream info: %1").arg(avErrorToString(ret)));
        closeFile();
        return false;
    }

    // Find best video and audio streams
    mVideoStreamIndex = findBestVideoStream();
    mAudioStreamIndex = findBestAudioStream();

    // Open video decoder context if video stream found
    if (mVideoStreamIndex >= 0) {
        AVStream* videoStream = mFormatCtx->streams[mVideoStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);

        if (codec) {
            mVideoCodecCtx = avcodec_alloc_context3(codec);
            if (mVideoCodecCtx) {
                int p2cRet = avcodec_parameters_to_context(mVideoCodecCtx, videoStream->codecpar);
                if (p2cRet < 0) {
                    qDebug() << "Warning: avcodec_parameters_to_context failed:" << avErrorToString(p2cRet);
                    avcodec_free_context(&mVideoCodecCtx);
                    mVideoCodecCtx = nullptr;
                } else {
                    if (mAnalysisMode) {
                        mVideoCodecCtx->thread_count = 0;  // auto-detect (all cores)
                        mVideoCodecCtx->thread_type = FF_THREAD_SLICE;
                        mVideoCodecCtx->skip_loop_filter = AVDISCARD_ALL;  // skip deblocking (safe for analysis)
                    } else {
                        mVideoCodecCtx->thread_count = 1;
                        mVideoCodecCtx->thread_type = FF_THREAD_SLICE;
                    }
                    ret = avcodec_open2(mVideoCodecCtx, codec, nullptr);
                    if (ret < 0) {
                        qDebug() << "Warning: Could not open video codec:" << avErrorToString(ret);
                        avcodec_free_context(&mVideoCodecCtx);
                        mVideoCodecCtx = nullptr;
                    }
                }
            }
        }
    }

    qDebug() << "Opened file:" << filePath;
    qDebug() << "  Streams:" << mFormatCtx->nb_streams;
    qDebug() << "  Video stream:" << mVideoStreamIndex;
    qDebug() << "  Audio stream:" << mAudioStreamIndex;

    return true;
}

// ----------------------------------------------------------------------------
// Close media file
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::closeFile()
{
    mFrameIndex.clear();
    mGOPIndex.clear();

    if (mRgbFrame) {
        av_frame_free(&mRgbFrame);
        mRgbFrame = nullptr;
    }

    if (mDecodedFrame) {
        av_frame_free(&mDecodedFrame);
        mDecodedFrame = nullptr;
    }

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }

    if (mVideoCodecCtx) {
        avcodec_free_context(&mVideoCodecCtx);
        mVideoCodecCtx = nullptr;
    }

    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = nullptr;
    }

    mVideoStreamIndex = -1;
    mAudioStreamIndex = -1;
    mCurrentFrameIndex = -1;
    mDecoderFrameIndex = -1;
    mDecoderDrained = false;
    mIsElementaryStream = false;
    mIsPAFF = false;
    mH264Log2MaxFrameNum = 4;
    mH264FrameMbsOnlyFlag = true;
    clearFrameCache();
}

// ----------------------------------------------------------------------------
// Get stream count
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::getStreamCount() const
{
    return mFormatCtx ? mFormatCtx->nb_streams : 0;
}

// ----------------------------------------------------------------------------
// Get stream information
// ----------------------------------------------------------------------------
TTStreamInfo TTFFmpegWrapper::getStreamInfo(int streamIndex) const
{
    TTStreamInfo info = {};

    if (!mFormatCtx || streamIndex < 0 ||
        streamIndex >= static_cast<int>(mFormatCtx->nb_streams)) {
        return info;
    }

    AVStream* stream = mFormatCtx->streams[streamIndex];
    AVCodecParameters* codecpar = stream->codecpar;

    info.streamIndex = streamIndex;
    info.codecType = codecpar->codec_type;
    info.codecId = codecpar->codec_id;
    info.codecName = avcodec_get_name(codecpar->codec_id);
    info.bitRate = codecpar->bit_rate;
    info.duration = stream->duration;

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        info.width = codecpar->width;
        info.height = codecpar->height;
        info.profile = codecpar->profile;
        info.level = codecpar->level;

        // Calculate frame rate
        if (stream->avg_frame_rate.den > 0) {
            info.frameRate = av_q2d(stream->avg_frame_rate);
        } else if (stream->r_frame_rate.den > 0) {
            info.frameRate = av_q2d(stream->r_frame_rate);
        }

        // Estimate frame count
        if (stream->nb_frames > 0) {
            info.numFrames = stream->nb_frames;
        } else if (info.frameRate > 0 && stream->duration > 0) {
            double durationSec = stream->duration * av_q2d(stream->time_base);
            info.numFrames = static_cast<int64_t>(durationSec * info.frameRate);
        }
    }
    else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        info.sampleRate = codecpar->sample_rate;
        info.channels = codecpar->ch_layout.nb_channels;
        info.bitsPerSample = codecpar->bits_per_coded_sample;
    }

    return info;
}

// ----------------------------------------------------------------------------
// Find best video stream
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::findBestVideoStream() const
{
    if (!mFormatCtx) return -1;

    return av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_VIDEO,
                               -1, -1, nullptr, 0);
}

// ----------------------------------------------------------------------------
// Find best audio stream
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::findBestAudioStream() const
{
    if (!mFormatCtx) return -1;

    return av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO,
                               -1, -1, nullptr, 0);
}

// ----------------------------------------------------------------------------
// Detect video codec type
// ----------------------------------------------------------------------------
TTVideoCodecType TTFFmpegWrapper::detectVideoCodec() const
{
    if (!mFormatCtx || mVideoStreamIndex < 0) {
        return CODEC_UNKNOWN;
    }

    AVCodecParameters* codecpar = mFormatCtx->streams[mVideoStreamIndex]->codecpar;

    switch (codecpar->codec_id) {
        case AV_CODEC_ID_MPEG2VIDEO:
            return CODEC_MPEG2;
        case AV_CODEC_ID_H264:
            return CODEC_H264;
        case AV_CODEC_ID_HEVC:
            return CODEC_H265;
        default:
            return CODEC_UNKNOWN;
    }
}

// ----------------------------------------------------------------------------
// Convert codec type to string
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::codecTypeToString(TTVideoCodecType type)
{
    switch (type) {
        case CODEC_MPEG2: return "MPEG-2";
        case CODEC_H264:  return "H.264/AVC";
        case CODEC_H265:  return "H.265/HEVC";
        default:          return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// Detect container type
// ----------------------------------------------------------------------------
TTContainerType TTFFmpegWrapper::detectContainer() const
{
    if (!mFormatCtx || !mFormatCtx->iformat) {
        return CONTAINER_UNKNOWN;
    }

    QString formatName = QString::fromUtf8(mFormatCtx->iformat->name);

    // IMPORTANT: Check elementary stream formats FIRST, before generic "mpeg"
    // "mpegvideo" contains "mpeg" so must be checked before CONTAINER_PS
    if (formatName.contains("mpegvideo") || formatName.contains("m2v") ||
        formatName.contains("h264") || formatName.contains("hevc")) {
        return CONTAINER_ELEMENTARY;
    }

    // Check for common container formats
    if (formatName.contains("mpegts") || formatName.contains("ts")) {
        return CONTAINER_TS;
    }
    if (formatName.contains("mpeg") || formatName.contains("vob")) {
        return CONTAINER_PS;
    }
    if (formatName.contains("matroska") || formatName.contains("webm")) {
        return CONTAINER_MKV;
    }
    if (formatName.contains("mp4") || formatName.contains("mov") || formatName.contains("m4v")) {
        return CONTAINER_MP4;
    }

    return CONTAINER_UNKNOWN;
}

// ----------------------------------------------------------------------------
// Convert container type to string
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::containerTypeToString(TTContainerType type)
{
    switch (type) {
        case CONTAINER_ELEMENTARY: return "Elementary Stream";
        case CONTAINER_TS:         return "MPEG Transport Stream";
        case CONTAINER_PS:         return "MPEG Program Stream";
        case CONTAINER_MKV:        return "Matroska";
        case CONTAINER_MP4:        return "MP4/ISOBMFF";
        default:                   return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// Parse H.264 SPS from extradata for PAFF detection
// Sets mH264Log2MaxFrameNum and mH264FrameMbsOnlyFlag
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::parseH264SpsFromExtradata(const uint8_t* data, int size)
{
    if (!data || size < 5) return;

    int nalStart = -1;
    for (int pos = 0; pos < size - 4; pos++) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            int start = -1;
            if (data[pos+2] == 1) start = pos + 3;
            else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) start = pos + 4;
            if (start >= 0 && start < size && (data[start] & 0x1F) == 7) {
                nalStart = start;
                break;
            }
        }
    }
    if (nalStart < 0) return;

    const uint8_t* sps = data + nalStart;
    int spsSize = size - nalStart;
    int bitPos = 8;

    int profileIdc = static_cast<int>(TTNaluParser::readBits(sps, spsSize, bitPos, 8));
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);  // constraint+reserved
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);  // level_idc
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);  // sps_id

    if (TTNaluParser::isH264HighProfile(static_cast<uint32_t>(profileIdc))) {
        int chromaFormatIdc = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        if (chromaFormatIdc == 3) TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        uint32_t scalingPresent = TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        if (scalingPresent) {
            int numLists = (chromaFormatIdc != 3) ? 8 : 12;
            for (int i = 0; i < numLists; i++) {
                if (TTNaluParser::readBits(sps, spsSize, bitPos, 1)) {
                    int listSize = (i < 6) ? 16 : 64;
                    int lastScale = 8, nextScale = 8;
                    for (int j = 0; j < listSize; j++) {
                        if (nextScale != 0) {
                            int delta = TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
                            nextScale = (lastScale + delta + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    mH264Log2MaxFrameNum = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos)) + 4;

    int pocType = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
    if (pocType == 0) {
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    } else if (pocType == 1) {
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        int n = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        // Spec H.264 7.4.2.1.1: num_ref_frames_in_pic_order_cnt_cycle <= 255.
        if (n > 256) return;
        for (int i = 0; i < n; i++) TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
    }

    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    TTNaluParser::readBits(sps, spsSize, bitPos, 1);
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);

    mH264FrameMbsOnlyFlag = (TTNaluParser::readBits(sps, spsSize, bitPos, 1) == 1);

    if (!mH264FrameMbsOnlyFlag) {
        qDebug() << "FFmpegWrapper SPS: frame_mbs_only_flag=0, log2_max_frame_num=" << mH264Log2MaxFrameNum;
    }
}

// ----------------------------------------------------------------------------
// Parse H.264 field info from packet data (field_pic_flag, bottom_field_flag)
// ----------------------------------------------------------------------------
TTFFmpegWrapper::TTFieldInfo TTFFmpegWrapper::parseH264FieldInfoFromPacket(const uint8_t* data, int size)
{
    TTFieldInfo result = {false, false, -1};
    if (!data || size < 4 || mH264FrameMbsOnlyFlag) return result;

    int nalStart = -1;
    for (int pos = 0; pos < size - 4; pos++) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            int start = -1;
            if (data[pos+2] == 1) start = pos + 3;
            else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) start = pos + 4;
            if (start >= 0 && start < size) {
                uint8_t nalType = data[start] & 0x1F;
                if (nalType == 1 || nalType == 5) { nalStart = start; break; }
            }
        }
    }

    if (nalStart < 0 && size >= 3) {
        uint8_t nalType = data[0] & 0x1F;
        if (nalType == 1 || nalType == 5) nalStart = 0;
    }
    if (nalStart < 0) return result;

    const uint8_t* nal = data + nalStart;
    int nalSize = size - nalStart;
    int bitPos = 8;

    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // first_mb_in_slice
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // slice_type
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // pps_id

    result.frameNum = static_cast<int>(TTNaluParser::readBits(nal, nalSize, bitPos, mH264Log2MaxFrameNum));

    result.isField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    if (result.isField) {
        result.isBottomField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    }

    return result;
}

// ----------------------------------------------------------------------------
// Build frame index by scanning all packets
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::buildFrameIndex(int videoStreamIndex)
{
    if (!mFormatCtx) {
        setError("No file open");
        return false;
    }

    if (videoStreamIndex < 0) {
        videoStreamIndex = mVideoStreamIndex;
    }

    if (videoStreamIndex < 0) {
        setError("No video stream found");
        return false;
    }

    mFrameIndex.clear();

    // For raw ES files, seek to byte 0 instead of using av_seek_frame
    // av_seek_frame doesn't work well with raw h264/hevc demuxers
    QString suffix = QFileInfo(QString::fromUtf8(mFormatCtx->url)).suffix().toLower();
    bool isES = (suffix == "264" || suffix == "h264" ||
                 suffix == "265" || suffix == "h265" || suffix == "hevc" ||
                 suffix == "m2v" || suffix == "mpv");

    if (isES && mFormatCtx->pb) {
        // For ES files, seek to byte 0 and flush
        avio_seek(mFormatCtx->pb, 0, SEEK_SET);
        avformat_flush(mFormatCtx);
        qDebug() << "ES file: seeked to byte 0";
    } else {
        // For container formats, use normal seek
        av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        setError("Could not allocate packet");
        return false;
    }

    int currentGOP = 0;
    int frameIndex = 0;
    int64_t lastProgress = -1;

    // Get estimated frame count for progress
    TTStreamInfo streamInfo = getStreamInfo(videoStreamIndex);
    int64_t estimatedFrames = streamInfo.numFrames > 0 ? streamInfo.numFrames : 10000;

    qDebug() << "Building frame index for stream" << videoStreamIndex;
    qDebug() << "Estimated frames:" << estimatedFrames;

    // Parse SPS from extradata for PAFF detection (H.264 only)
    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
    if (codecId == AV_CODEC_ID_H264) {
        uint8_t* extradata = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata;
        int extradataSize = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata_size;
        if (extradata && extradataSize > 0) {
            parseH264SpsFromExtradata(extradata, extradataSize);
        }
    }

    // PAFF field merging state
    bool hasPendingTopField = false;
    TTFrameInfo pendingTopFrame;
    int pendingTopFrameNum = -1;

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            // Check for PAFF field packet (H.264 only)
            AVCodecID cid = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
            if (cid == AV_CODEC_ID_H264 && !mH264FrameMbsOnlyFlag) {
                TTFieldInfo fieldInfo = parseH264FieldInfoFromPacket(packet->data, packet->size);
                if (fieldInfo.isField) {
                    mIsPAFF = true;

                    if (!fieldInfo.isBottomField) {
                        // Top field: buffer it
                        if (hasPendingTopField) {
                            // Orphaned top field — emit as standalone frame
                            pendingTopFrame.isFieldCoded = true;
                            pendingTopFrame.gopIndex = currentGOP;
                            mFrameIndex.append(pendingTopFrame);
                            frameIndex++;
                        }
                        pendingTopFrame = TTFrameInfo();
                        pendingTopFrame.pts = packet->pts;
                        pendingTopFrame.dts = packet->dts;
                        pendingTopFrame.fileOffset = packet->pos;
                        pendingTopFrame.packetSize = packet->size;
                        pendingTopFrame.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
                        pendingTopFrame.frameIndex = frameIndex;
                        pendingTopFrame.isFieldCoded = true;

                        // Determine frame type for top field
                        if (pendingTopFrame.isKeyframe) {
                            pendingTopFrame.frameType = AV_PICTURE_TYPE_I;
                        } else {
                            int sliceType = TTNaluParser::parseH264SliceTypeFromPacket(
                                packet->data, packet->size);
                            if (sliceType == H264::SLICE_B)
                                pendingTopFrame.frameType = AV_PICTURE_TYPE_B;
                            else if (sliceType == H264::SLICE_I)
                                pendingTopFrame.frameType = AV_PICTURE_TYPE_I;
                            else
                                pendingTopFrame.frameType = AV_PICTURE_TYPE_P;
                        }

                        pendingTopFrameNum = fieldInfo.frameNum;
                        hasPendingTopField = true;
                        av_packet_unref(packet);
                        continue;
                    } else {
                        // Bottom field
                        if (hasPendingTopField && fieldInfo.frameNum == pendingTopFrameNum) {
                            // Merge: use top field's info, add bottom field's packet size
                            pendingTopFrame.packetSize += packet->size;
                            if (pendingTopFrame.isKeyframe) {
                                if (frameIndex > 0) currentGOP++;
                            }
                            pendingTopFrame.gopIndex = currentGOP;
                            mFrameIndex.append(pendingTopFrame);
                            frameIndex++;

                            int64_t progress = (frameIndex * 100) / estimatedFrames;
                            if (progress != lastProgress && progress <= 100) {
                                emit progressChanged(static_cast<int>(progress),
                                    tr("Indexing frame %1...").arg(frameIndex));
                                lastProgress = progress;
                            }

                            hasPendingTopField = false;
                            pendingTopFrameNum = -1;
                            av_packet_unref(packet);
                            continue;
                        }
                        // Unmatched bottom field — fall through to normal handling
                        if (hasPendingTopField) {
                            // Emit orphaned top field first
                            pendingTopFrame.gopIndex = currentGOP;
                            mFrameIndex.append(pendingTopFrame);
                            frameIndex++;
                            hasPendingTopField = false;
                            pendingTopFrameNum = -1;
                        }
                    }
                } else if (hasPendingTopField) {
                    // Non-field packet after a pending top field — emit orphaned top
                    pendingTopFrame.gopIndex = currentGOP;
                    mFrameIndex.append(pendingTopFrame);
                    frameIndex++;
                    hasPendingTopField = false;
                    pendingTopFrameNum = -1;
                }
            }

            TTFrameInfo frameInfo;
            frameInfo.pts = packet->pts;
            frameInfo.dts = packet->dts;
            frameInfo.fileOffset = packet->pos;
            frameInfo.packetSize = packet->size;
            frameInfo.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            frameInfo.frameIndex = frameIndex;
            frameInfo.isFieldCoded = false;

            // Determine frame type
            if (frameInfo.isKeyframe) {
                frameInfo.frameType = AV_PICTURE_TYPE_I;
                // New GOP starts at keyframe
                if (frameIndex > 0) {
                    currentGOP++;
                }
            } else {
                // Parse slice_type from packet data for B-frame detection
                if (cid == AV_CODEC_ID_H264) {
                    int sliceType = TTNaluParser::parseH264SliceTypeFromPacket(
                        packet->data, packet->size);
                    // H264: P=0, B=1, I=2
                    if (sliceType == H264::SLICE_B)
                        frameInfo.frameType = AV_PICTURE_TYPE_B;
                    else if (sliceType == H264::SLICE_I)
                        frameInfo.frameType = AV_PICTURE_TYPE_I;
                    else
                        frameInfo.frameType = AV_PICTURE_TYPE_P;
                } else if (cid == AV_CODEC_ID_HEVC) {
                    int sliceType = TTNaluParser::parseH265SliceTypeFromPacket(
                        packet->data, packet->size);
                    // H265: B=0, P=1, I=2
                    if (sliceType == H265::SLICE_B)
                        frameInfo.frameType = AV_PICTURE_TYPE_B;
                    else if (sliceType == H265::SLICE_I)
                        frameInfo.frameType = AV_PICTURE_TYPE_I;
                    else
                        frameInfo.frameType = AV_PICTURE_TYPE_P;
                } else {
                    frameInfo.frameType = AV_PICTURE_TYPE_P;
                }
            }

            frameInfo.gopIndex = currentGOP;
            mFrameIndex.append(frameInfo);
            frameIndex++;

            // Progress reporting
            int64_t progress = (frameIndex * 100) / estimatedFrames;
            if (progress != lastProgress && progress <= 100) {
                emit progressChanged(static_cast<int>(progress),
                    tr("Indexing frame %1...").arg(frameIndex));
                lastProgress = progress;
            }
        }

        av_packet_unref(packet);
    }

    // Flush any pending top field after loop
    if (hasPendingTopField) {
        pendingTopFrame.gopIndex = currentGOP;
        mFrameIndex.append(pendingTopFrame);
        frameIndex++;
    }

    av_packet_free(&packet);

    // Seek back to beginning - use avio_seek for ES files
    if (isES && mFormatCtx->pb) {
        avio_seek(mFormatCtx->pb, 0, SEEK_SET);
        avformat_flush(mFormatCtx);
        qDebug() << "ES file: seeked back to byte 0 after index build";
    } else {
        av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    qDebug() << "Frame index built:" << mFrameIndex.size() << "frames in"
             << (currentGOP + 1) << "GOPs";

    // Debug: Check first frame's fileOffset for ES files
    if (isES && !mFrameIndex.isEmpty()) {
        qDebug() << "First frame fileOffset:" << mFrameIndex[0].fileOffset
                 << "packetSize:" << mFrameIndex[0].packetSize;
    }

    // For elementary streams without PTS/DTS, calculate timestamps from frame rate
    if (!mFrameIndex.isEmpty() && mFrameIndex[0].pts == AV_NOPTS_VALUE) {
        qDebug() << "Elementary stream detected - calculating PTS/DTS from frame rate";

        // Get frame rate from .info file if available, otherwise from stream
        double frameRate = streamInfo.frameRate;
        QString sourceFile = QString::fromUtf8(mFormatCtx->url);
        QString infoFile = TTESInfo::findInfoFile(sourceFile);

        if (!infoFile.isEmpty()) {
            TTESInfo esInfo(infoFile);
            if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
                frameRate = esInfo.frameRate();
                qDebug() << "Using frame rate from .info file:" << frameRate;
            }
        }

        // Validate frame rate
        if (frameRate <= 0 || frameRate > 120) {
            frameRate = 25.0; // Default fallback
            qDebug() << "Invalid frame rate, using default:" << frameRate;
        }

        // PAFF: field-rate reported as frame-rate, correct to actual frame-rate
        if (mIsPAFF && frameRate > 30) {
            qDebug() << "PAFF: correcting frame rate from" << frameRate << "to" << frameRate / 2.0;
            frameRate /= 2.0;
        }

        // Get time base from stream
        AVStream* videoStream = mFormatCtx->streams[videoStreamIndex];
        AVRational timeBase = videoStream->time_base;

        // Calculate frame duration in stream time base
        // pts_increment = time_base / frame_rate
        // For time_base = 1/90000 and frame_rate = 25, pts_increment = 3600
        int64_t frameDuration = av_rescale_q(1, av_make_q(1, static_cast<int>(frameRate * 1000)), timeBase) / 1000;
        if (frameDuration <= 0) {
            frameDuration = av_rescale_q(1, av_make_q(1, 25), timeBase); // Fallback to 25fps
        }

        qDebug() << "Time base:" << timeBase.num << "/" << timeBase.den;
        qDebug() << "Frame rate:" << frameRate << "fps";
        qDebug() << "Frame duration:" << frameDuration << "ticks";

        // Assign sequential PTS/DTS values
        int64_t currentPts = 0;
        for (int i = 0; i < mFrameIndex.size(); ++i) {
            mFrameIndex[i].pts = currentPts;
            mFrameIndex[i].dts = currentPts;
            currentPts += frameDuration;
        }

        qDebug() << "Calculated timestamps for" << mFrameIndex.size() << "frames";
        qDebug() << "First PTS:" << mFrameIndex.first().pts
                 << "Last PTS:" << mFrameIndex.last().pts;
    }

    emit progressChanged(100, tr("Indexed %1 frames").arg(mFrameIndex.size()));

    return true;
}

// ----------------------------------------------------------------------------
// Build GOP index from frame index
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::buildGOPIndex()
{
    if (mFrameIndex.isEmpty()) {
        setError("Frame index is empty, build it first");
        return false;
    }

    mGOPIndex.clear();

    int currentGOP = -1;
    TTGOPInfo gopInfo;

    for (int i = 0; i < mFrameIndex.size(); i++) {
        const TTFrameInfo& frame = mFrameIndex[i];

        if (frame.gopIndex != currentGOP) {
            // Save previous GOP
            if (currentGOP >= 0) {
                gopInfo.endFrame = i - 1;
                gopInfo.endPts = mFrameIndex[i - 1].pts;
                mGOPIndex.append(gopInfo);
            }

            // Start new GOP
            currentGOP = frame.gopIndex;
            gopInfo.gopIndex = currentGOP;
            gopInfo.startFrame = i;
            gopInfo.startPts = frame.pts;
            gopInfo.isClosed = true; // Assume closed, would need more analysis for accuracy
        }
    }

    // Save last GOP
    if (currentGOP >= 0 && !mFrameIndex.isEmpty()) {
        gopInfo.endFrame = mFrameIndex.size() - 1;
        gopInfo.endPts = mFrameIndex.last().pts;
        mGOPIndex.append(gopInfo);
    }

    qDebug() << "GOP index built:" << mGOPIndex.size() << "GOPs";

    return true;
}

// ----------------------------------------------------------------------------
// Get frame at index
// ----------------------------------------------------------------------------
TTFrameInfo TTFFmpegWrapper::frameAt(int index) const
{
    if (index >= 0 && index < mFrameIndex.size()) {
        return mFrameIndex[index];
    }
    return TTFrameInfo();
}

// ----------------------------------------------------------------------------
// Find frame by PTS
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::findFrameByPts(int64_t pts) const
{
    // Binary search for efficiency
    int left = 0;
    int right = mFrameIndex.size() - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;

        if (mFrameIndex[mid].pts == pts) {
            return mid;
        }

        if (mFrameIndex[mid].pts < pts) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    // Return closest frame if exact match not found
    return left < mFrameIndex.size() ? left : mFrameIndex.size() - 1;
}

// ----------------------------------------------------------------------------
// Find GOP for frame
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::findGOPForFrame(int frameIndex) const
{
    if (frameIndex >= 0 && frameIndex < mFrameIndex.size()) {
        return mFrameIndex[frameIndex].gopIndex;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Convert PTS to seconds
// ----------------------------------------------------------------------------
double TTFFmpegWrapper::ptsToSeconds(int64_t pts, int streamIndex) const
{
    if (!mFormatCtx || streamIndex < 0 ||
        streamIndex >= static_cast<int>(mFormatCtx->nb_streams)) {
        return 0.0;
    }

    if (pts == AV_NOPTS_VALUE) {
        return 0.0;
    }

    AVStream* stream = mFormatCtx->streams[streamIndex];
    return pts * av_q2d(stream->time_base);
}

// ----------------------------------------------------------------------------
// Convert seconds to PTS
// ----------------------------------------------------------------------------
int64_t TTFFmpegWrapper::secondsToPts(double seconds, int streamIndex) const
{
    if (!mFormatCtx || streamIndex < 0 ||
        streamIndex >= static_cast<int>(mFormatCtx->nb_streams)) {
        return 0;
    }

    AVStream* stream = mFormatCtx->streams[streamIndex];
    return static_cast<int64_t>(seconds / av_q2d(stream->time_base));
}

// ----------------------------------------------------------------------------
// Format timestamp as string
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::formatTimestamp(int64_t pts, int streamIndex) const
{
    double seconds = ptsToSeconds(pts, streamIndex);
    int hours = static_cast<int>(seconds / 3600);
    int minutes = static_cast<int>((seconds - hours * 3600) / 60);
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::setError(const QString& error)
{
    mLastError = error;
    qDebug() << "TTFFmpegWrapper error:" << error;
}

// ----------------------------------------------------------------------------
// Convert libav error code to string
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::avErrorToString(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return QString::fromUtf8(errbuf);
}

// ----------------------------------------------------------------------------
// Elementary-stream detection (shared with TTMkvMergeProvider)
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::isElementaryStreamPath(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return (suffix == "264" || suffix == "h264" ||
            suffix == "265" || suffix == "h265" || suffix == "hevc" ||
            suffix == "m2v" || suffix == "mpv");
}

const AVInputFormat* TTFFmpegWrapper::esInputFormatForPath(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "264" || suffix == "h264")
        return av_find_input_format("h264");
    if (suffix == "265" || suffix == "h265" || suffix == "hevc")
        return av_find_input_format("hevc");
    if (suffix == "m2v" || suffix == "mpv")
        return av_find_input_format("mpegvideo");
    return nullptr;
}

// ----------------------------------------------------------------------------
// Get frame type from packet (requires decoding for accuracy)
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::getFrameType(AVPacket* packet, AVCodecContext* codecCtx)
{
    // Simple check: keyframe flag
    if (packet->flags & AV_PKT_FLAG_KEY) {
        return AV_PICTURE_TYPE_I;
    }

    // Parse slice_type from packet data for B-frame detection
    if (codecCtx && codecCtx->codec_id == AV_CODEC_ID_H264) {
        int sliceType = TTNaluParser::parseH264SliceTypeFromPacket(
            packet->data, packet->size);
        if (sliceType == H264::SLICE_B)
            return AV_PICTURE_TYPE_B;
        else if (sliceType == H264::SLICE_I)
            return AV_PICTURE_TYPE_I;
    } else if (codecCtx && codecCtx->codec_id == AV_CODEC_ID_HEVC) {
        int sliceType = TTNaluParser::parseH265SliceTypeFromPacket(
            packet->data, packet->size);
        if (sliceType == H265::SLICE_B)
            return AV_PICTURE_TYPE_B;
        else if (sliceType == H265::SLICE_I)
            return AV_PICTURE_TYPE_I;
    }

    return AV_PICTURE_TYPE_P;
}

// ----------------------------------------------------------------------------
// Get video width
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::videoWidth() const
{
    if (mVideoCodecCtx) {
        return mVideoCodecCtx->width;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Get video height
// ----------------------------------------------------------------------------
int TTFFmpegWrapper::videoHeight() const
{
    if (mVideoCodecCtx) {
        return mVideoCodecCtx->height;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Seek to specific frame index
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::seekToFrame(int frameIndex)
{
    if (!mFormatCtx || mVideoStreamIndex < 0) {
        setError("No file open or no video stream");
        return false;
    }

    if (frameIndex < 0 || frameIndex >= mFrameIndex.size()) {
        setError(QString("Frame index %1 out of range").arg(frameIndex));
        return false;
    }

    // Seek to the keyframe before this frame
    int keyframeIndex = frameIndex;
    while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe) {
        keyframeIndex--;
    }

    // Seek to the keyframe BEFORE keyframeIndex to fill the DPB with reference
    // frames from the previous GOP. This prevents Open-GOP B-frames at the start
    // of the target GOP from decoding incorrectly after a flush.
    int seekKeyframe = keyframeIndex;
    if (keyframeIndex > 0) {
        int prevKey = keyframeIndex - 1;
        while (prevKey > 0 && !mFrameIndex[prevKey].isKeyframe) {
            prevKey--;
        }
        if (mFrameIndex[prevKey].isKeyframe) {
            seekKeyframe = prevKey;
        }
    }

    int64_t ret;
    if (mIsElementaryStream && mFormatCtx->pb) {
        // For ES files, use byte-based seeking
        int64_t byteOffset = mFrameIndex[seekKeyframe].fileOffset;

        // If fileOffset is -1 (unknown), seek to byte 0 for first keyframe
        if (byteOffset < 0) {
            if (seekKeyframe == 0) {
                byteOffset = 0;
            } else {
                // For other frames, try to find a valid offset
                // Walk back to find a frame with valid offset
                for (int i = seekKeyframe; i >= 0; i--) {
                    if (mFrameIndex[i].fileOffset >= 0) {
                        byteOffset = mFrameIndex[i].fileOffset;
                        break;
                    }
                }
                if (byteOffset < 0) byteOffset = 0;  // Fallback to start
            }
            qDebug() << "ES seek: fileOffset was -1, using" << byteOffset;
        }

        ret = avio_seek(mFormatCtx->pb, byteOffset, SEEK_SET);
        qDebug() << "ES seek to byte" << byteOffset << "seekKeyframe:" << seekKeyframe << "targetKeyframe:" << keyframeIndex << "avio_seek result:" << ret;
        if (ret >= 0) {
            avformat_flush(mFormatCtx);
            ret = 0;  // Success
        } else {
            qDebug() << "avio_seek failed with:" << ret << avErrorToString(ret);
        }
    } else {
        // For container formats, use timestamp-based seeking
        int64_t seekPts = mFrameIndex[seekKeyframe].pts;
        qDebug() << "Container seek to PTS" << seekPts << "seekKeyframe:" << seekKeyframe << "targetKeyframe:" << keyframeIndex;
        ret = av_seek_frame(mFormatCtx, mVideoStreamIndex, seekPts, AVSEEK_FLAG_BACKWARD);
    }

    if (ret < 0) {
        setError(QString("Seek failed: %1").arg(avErrorToString(ret)));
        return false;
    }

    // Flush codec buffers after seek
    if (mVideoCodecCtx) {
        avcodec_flush_buffers(mVideoCodecCtx);
    }
    mDecoderDrained = false;

    mCurrentFrameIndex = seekKeyframe;
    mDecoderFrameIndex = seekKeyframe;
    return true;
}

// ----------------------------------------------------------------------------
// Decode frame at specific index and return as QImage
// ----------------------------------------------------------------------------
QImage TTFFmpegWrapper::decodeFrame(int frameIndex)
{
    // Bounds check — TTNaluParser and FFmpeg demuxer may count different frames
    if (frameIndex < 0 || frameIndex >= mFrameIndex.size()) {
        qDebug() << "decodeFrame: index" << frameIndex
                 << "out of range (0 -" << mFrameIndex.size()-1 << ")";
        if (frameIndex >= mFrameIndex.size() && mFrameIndex.size() > 0) {
            frameIndex = mFrameIndex.size() - 1;
            qDebug() << "decodeFrame: clamped to last valid frame" << frameIndex;
        } else {
            return QImage();
        }
    }

    // Check LRU cache first
    if (mFrameCache.contains(frameIndex)) {
        // Move to back of LRU list (most recently used)
        mFrameCacheLRU.removeOne(frameIndex);
        mFrameCacheLRU.append(frameIndex);
        return mFrameCache[frameIndex];
    }

    // Always seek to ensure consistent DPB state across decoder instances.
    // The sequential optimization (reusing decoder position without seeking)
    // was disabled because it produces a different DPB state than a fresh seek
    // with DPB prefill. This caused Open-GOP B-frames to decode differently
    // in the CutOut and CurrentFrame widgets for the same frame index.
    // The LRU frame cache mitigates the performance impact for repeated access.
    bool needSeek = true;

    if (needSeek) {
        int keyframeIndex = frameIndex;
        while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe) {
            keyframeIndex--;
        }
        qDebug() << "decodeFrame: seek target=" << frameIndex
                 << "keyframe=" << keyframeIndex
                 << "frames_to_decode=" << (frameIndex - keyframeIndex + 1)
                 << "total_frames=" << mFrameIndex.size();

        if (!seekToFrame(frameIndex)) {
            qDebug() << "decodeFrame: seekToFrame failed for frame" << frameIndex;
            return QImage();
        }
        mDecoderFrameIndex = mCurrentFrameIndex;
    }

    // Skip intermediate frames (decode without RGB conversion)
    while (mDecoderFrameIndex < frameIndex) {
        if (!skipCurrentFrame()) {
            // EOF drain exhausted during skip — break and try decodeCurrentFrame,
            // since the decoder may still have the target frame buffered
            qDebug() << "decodeFrame: skip failed at" << mDecoderFrameIndex
                     << "(target=" << frameIndex << ") drained=" << mDecoderDrained
                     << "— trying decodeCurrentFrame directly";
            break;
        }
        mDecoderFrameIndex++;
    }

    // Decode final target frame with full RGB conversion
    QImage result = decodeCurrentFrame();

    // Fallback: re-seek and retry if first attempt failed
    if (result.isNull()) {
        qDebug() << "decodeFrame: first decode attempt failed for frame" << frameIndex
                 << "— retrying with fresh seek";

        int keyframeIndex = frameIndex;
        while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe) {
            keyframeIndex--;
        }

        if (seekToFrame(frameIndex)) {
            mDecoderFrameIndex = mCurrentFrameIndex;

            // Skip to target again
            while (mDecoderFrameIndex < frameIndex) {
                if (!skipCurrentFrame()) break;
                mDecoderFrameIndex++;
            }

            result = decodeCurrentFrame();
        }
    }

    // Fallback: try one frame earlier if target frame cannot be decoded
    if (result.isNull() && frameIndex > 0) {
        qDebug() << "decodeFrame: retry failed — trying frame" << (frameIndex - 1);
        // Recursive call with frameIndex-1 (will seek fresh)
        mDecoderDrained = true;  // Force seek in recursive call
        result = decodeFrame(frameIndex - 1);
        if (!result.isNull()) {
            // Return the nearby frame but don't cache it under the wrong index
            return result;
        }
    }

    if (!result.isNull()) {
        mDecoderFrameIndex = frameIndex;
        mCurrentFrameIndex = frameIndex;

        // Store in LRU cache
        mFrameCache[frameIndex] = result;
        mFrameCacheLRU.append(frameIndex);
        while (mFrameCacheLRU.size() > mFrameCacheMaxSize) {
            int evict = mFrameCacheLRU.takeFirst();
            mFrameCache.remove(evict);
        }
    } else {
        qDebug() << "decodeFrame: FAILED to decode frame" << frameIndex
                 << "and fallback (total_frames=" << mFrameIndex.size() << ")";
    }
    return result;
}

// ----------------------------------------------------------------------------
// Decode current frame and return as QImage
// ----------------------------------------------------------------------------
QImage TTFFmpegWrapper::decodeCurrentFrame()
{
    if (!mFormatCtx || !mVideoCodecCtx) {
        setError("No file open or decoder not initialized");
        return QImage();
    }

    // Allocate frames if needed
    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) {
            setError("Could not allocate decoded frame");
            return QImage();
        }
    }

    if (!mRgbFrame) {
        mRgbFrame = av_frame_alloc();
        if (!mRgbFrame) {
            setError("Could not allocate RGB frame");
            return QImage();
        }

        // Ref-counted buffer: av_frame_free will release it via AVBufferRef.
        // (av_image_fill_arrays + av_malloc would leak since no buf[0] is set.)
        mRgbFrame->format = AV_PIX_FMT_RGB24;
        mRgbFrame->width  = mVideoCodecCtx->width;
        mRgbFrame->height = mVideoCodecCtx->height;
        int bufRet = av_frame_get_buffer(mRgbFrame, 1);
        if (bufRet < 0) {
            setError("Could not allocate RGB frame buffer");
            av_frame_free(&mRgbFrame);
            return QImage();
        }
    }

    // Initialize scaler if needed
    if (!mSwsCtx) {
        mSwsCtx = sws_getContext(
            mVideoCodecCtx->width, mVideoCodecCtx->height, mVideoCodecCtx->pix_fmt,
            mVideoCodecCtx->width, mVideoCodecCtx->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!mSwsCtx) {
            setError("Could not create scaler context");
            return QImage();
        }
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        setError("Could not allocate packet");
        return QImage();
    }

    QImage result;

    // Read packets until we get a complete frame
    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == mVideoStreamIndex) {
            int ret = avcodec_send_packet(mVideoCodecCtx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
            if (ret == 0) {
                // Convert to RGB
                sws_scale(mSwsCtx,
                    mDecodedFrame->data, mDecodedFrame->linesize,
                    0, mVideoCodecCtx->height,
                    mRgbFrame->data, mRgbFrame->linesize);

                // Create QImage from RGB data
                result = QImage(mRgbFrame->data[0],
                    mVideoCodecCtx->width, mVideoCodecCtx->height,
                    mRgbFrame->linesize[0],
                    QImage::Format_RGB888).copy();

                av_packet_unref(packet);
                break;
            }
        }
        av_packet_unref(packet);
    }

    // EOF drain: flush decoder pipeline to retrieve buffered frames
    if (result.isNull()) {
        int ret = avcodec_send_packet(mVideoCodecCtx, nullptr);
        int recvRet = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
        if (recvRet == 0) {
            // Convert to RGB
            sws_scale(mSwsCtx,
                mDecodedFrame->data, mDecodedFrame->linesize,
                0, mVideoCodecCtx->height,
                mRgbFrame->data, mRgbFrame->linesize);

            result = QImage(mRgbFrame->data[0],
                mVideoCodecCtx->width, mVideoCodecCtx->height,
                mRgbFrame->linesize[0],
                QImage::Format_RGB888).copy();

            mDecoderDrained = true;
        } else {
            qDebug() << "decodeCurrentFrame: EOF drain failed"
                     << "send_packet=" << ret << "receive_frame=" << recvRet;
        }
    }

    av_packet_free(&packet);
    return result;
}

// ----------------------------------------------------------------------------
// Lightweight black frame check — decode to YUV, analyze Y-plane directly.
// No RGB conversion, no QImage, no cache. Much faster than decodeFrame().
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::isFrameBlack(int frameIndex, int pixelThreshold, float ratioThreshold)
{
    if (frameIndex < 0 || frameIndex >= mFrameIndex.size()) return false;
    if (!mFormatCtx || !mVideoCodecCtx) return false;

    // Seek to keyframe for this frame
    int keyframeIndex = frameIndex;
    while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe)
        keyframeIndex--;

    // Only seek if needed (decoder already past the keyframe)
    bool needSeek = true;
    if (!mDecoderDrained && mDecoderFrameIndex >= 0 && mDecoderFrameIndex < frameIndex
        && mDecoderFrameIndex >= keyframeIndex)
        needSeek = false;

    if (needSeek) {
        if (!seekToFrame(frameIndex)) return false;
        mDecoderFrameIndex = mCurrentFrameIndex;
    }

    // Skip intermediate frames to reach target
    while (mDecoderFrameIndex < frameIndex) {
        if (!skipCurrentFrame()) break;
        mDecoderFrameIndex++;
    }

    // Decode one frame (YUV, no RGB conversion)
    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    bool decoded = false;
    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == mVideoStreamIndex) {
            if (avcodec_send_packet(mVideoCodecCtx, packet) >= 0) {
                if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
                    decoded = true;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    // EOF drain if needed
    if (!decoded) {
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
            decoded = true;
            mDecoderDrained = true;
        }
    }

    av_packet_free(&packet);
    if (!decoded) return false;

    mDecoderFrameIndex = frameIndex;
    mCurrentFrameIndex = frameIndex;

    // Analyze Y-plane directly (YUV420P: data[0] = Y, linesize[0] = Y stride)
    int w = mDecodedFrame->width, h = mDecodedFrame->height;
    uint8_t* yPlane = mDecodedFrame->data[0];
    int yStride = mDecodedFrame->linesize[0];
    if (!yPlane || w <= 0 || h <= 0) return false;

    int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;
    const int step = 2;
    const int earlyExitSamples = 500;
    long lumaSum = 0;
    int totalPixels = 0, blackPixels = 0;

    for (int row = y0; row < y1; row += step) {
        uint8_t* line = yPlane + row * yStride;
        for (int col = x0; col < x1; col += step) {
            totalPixels++;
            lumaSum += line[col];
            if (line[col] < pixelThreshold) blackPixels++;
        }
        if (totalPixels >= earlyExitSamples) {
            float avgSoFar = (float)lumaSum / totalPixels;
            if (avgSoFar > 20.0f) return false;  // video-range: black ≈ 16
        }
    }

    if (totalPixels == 0) return false;
    float avgLuma = (float)lumaSum / totalPixels;
    if (avgLuma > 20.0f) return false;

    return (float)blackPixels / totalPixels >= ratioThreshold;
}

// ----------------------------------------------------------------------------
// Build luma histogram for a single frame (public, for cached search)
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::buildHistogram(int frameIndex, int hist[256], int& totalPixels)
{
    memset(hist, 0, 256 * sizeof(int));
    totalPixels = 0;

    if (frameIndex < 0 || frameIndex >= mFrameIndex.size()) return false;
    if (!mFormatCtx || !mVideoCodecCtx) return false;

    // Seek to keyframe, skip intermediate frames
    if (!seekToFrame(frameIndex)) return false;
    mDecoderFrameIndex = mCurrentFrameIndex;

    while (mDecoderFrameIndex < frameIndex) {
        if (!skipCurrentFrame()) break;
        mDecoderFrameIndex++;
    }

    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    // Read packets until decoder produces a frame
    // In analysis mode (AVDISCARD_NONKEY), only keyframes produce output
    bool decoded = false;
    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == mVideoStreamIndex) {
            if (avcodec_send_packet(mVideoCodecCtx, packet) >= 0) {
                if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
                    decoded = true;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    if (!decoded) {
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
            decoded = true;
            mDecoderDrained = true;
        }
    }

    av_packet_free(&packet);
    if (!decoded) return false;

    mDecoderFrameIndex = frameIndex;
    mCurrentFrameIndex = frameIndex;

    // Build histogram from Y-plane center 80%
    int w = mDecodedFrame->width, h = mDecodedFrame->height;
    uint8_t* yPlane = mDecodedFrame->data[0];
    int yStride = mDecodedFrame->linesize[0];
    if (!yPlane || w <= 0 || h <= 0) return false;

    int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;
    const int step = 2;

    for (int row = y0; row < y1; row += step) {
        uint8_t* line = yPlane + row * yStride;
        for (int col = x0; col < x1; col += step) {
            hist[line[col]]++;
            totalPixels++;
        }
    }
    return totalPixels > 0;
}

// ----------------------------------------------------------------------------
// Scene change detection: decode two I-frames and compare luma histograms
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::isSceneChange(int indexA, int indexB, float threshold)
{
    int histA[256], histB[256];
    int totalA = 0, totalB = 0;

    if (!buildHistogram(indexA, histA, totalA)) return false;
    if (!buildHistogram(indexB, histB, totalB)) return false;

    // Compare normalized histograms
    float diff = 0.0f;
    for (int i = 0; i < 256; i++) {
        diff += qAbs((float)histA[i]/totalA - (float)histB[i]/totalB);
    }
    diff /= 2.0f;  // normalize to 0.0–1.0

    qDebug() << "Scene FFmpeg: frames" << indexA << "->" << indexB
             << "diff=" << diff << "threshold=" << threshold
             << (diff > threshold ? "MATCH" : "");
    return diff > threshold;
}

// ----------------------------------------------------------------------------
// Skip current frame (decode for reference chain but skip RGB conversion)
// Used by decodeFrame() to efficiently skip intermediate frames
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::skipCurrentFrame()
{
    if (!mFormatCtx || !mVideoCodecCtx) return false;

    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    bool decoded = false;

    // Keep sending packets until a frame is produced.
    // For PAFF: decoder needs 2 field packets per frame (returns EAGAIN after first).
    // For progressive: 1 packet = 1 frame.
    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == mVideoStreamIndex) {
            int ret = avcodec_send_packet(mVideoCodecCtx, packet);
            av_packet_unref(packet);
            if (ret < 0) continue;

            ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
            if (ret == 0) {
                decoded = true;
                break;
            }
            // EAGAIN: decoder needs more data (e.g. PAFF second field) → keep reading
        } else {
            av_packet_unref(packet);
        }
    }

    // EOF drain: flush decoder pipeline to retrieve buffered frames
    if (!decoded) {
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        int recvRet = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
        if (recvRet == 0) {
            decoded = true;
            mDecoderDrained = true;
        } else {
            qDebug() << "skipCurrentFrame: EOF drain exhausted"
                     << "receive_frame=" << recvRet;
        }
    }

    av_packet_free(&packet);
    return decoded;
}

// ----------------------------------------------------------------------------
// Set frame cache size
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::setFrameCacheSize(int maxFrames)
{
    mFrameCacheMaxSize = qMax(0, maxFrames);
    while (mFrameCacheLRU.size() > mFrameCacheMaxSize) {
        int evict = mFrameCacheLRU.takeFirst();
        mFrameCache.remove(evict);
    }
}

// ----------------------------------------------------------------------------
// Clear frame cache
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::clearFrameCache()
{
    mFrameCache.clear();
    mFrameCacheLRU.clear();
}

// ----------------------------------------------------------------------------
// Analyze AC3 acmod (audio coding mode) for a segment between cutInTime and cutOutTime.
// Returns the majority acmod and detects changes at cut boundaries.
// Uses direct AC3 sync word scanning on the raw file instead of libav (which crashes
// on av_seek_frame for raw AC3 ES files).
// ----------------------------------------------------------------------------
TTFFmpegWrapper::AcmodInfo TTFFmpegWrapper::analyzeAcmod(const QString& audioFile,
                                                          double cutInTime, double cutOutTime)
{
    AcmodInfo info = { -1, -1, -1, 0.0, 0.0 };

    QFile file(audioFile);
    if (!file.open(QIODevice::ReadOnly))
        return info;

    // AC3 frame size lookup table [fscod][frmsizecod] in 16-bit words
    static const int AC3FrameWords[3][38] = {
        { 64, 64, 80, 80, 96, 96,112,112,128,128,160,160,192,192,224,224,256,256,320,320,
         384,384,448,448,512,512,640,640,768,768,896,896,1024,1024,1152,1152,1280,1280},
        { 69, 70, 87, 88,104,105,121,122,139,140,174,175,208,209,243,244,278,279,348,349,
         417,418,487,488,557,558,696,697,835,836,975,976,1114,1115,1253,1254,1393,1394},
        { 96, 96,120,120,144,144,168,168,192,192,240,240,288,288,336,336,384,384,480,480,
         576,576,672,672,768,768,960,960,1152,1152,1344,1344,1536,1536,1728,1728,1920,1920}
    };

    // Scan AC3 frames by sync word (0x0B77)
    static const int SAMPLE_FRAMES = 100;
    int acmodCount[8] = {0};
    int firstAcmod = -1;
    int lastAcmod = -1;
    int totalFrames = 0;
    int frameIndex = 0;
    double frameTime = 0.032;  // AC3 = 32ms per frame at 48kHz

    // Calculate frame indices for cutIn/cutOut
    int cutInFrame = static_cast<int>(cutInTime / frameTime);
    int cutOutFrame = static_cast<int>(cutOutTime / frameTime);

    quint8 header[8];
    qint64 pos = 0;

    while (pos < file.size() - 8) {
        file.seek(pos);
        if (file.read(reinterpret_cast<char*>(header), 8) != 8)
            break;

        // Check sync word
        if (header[0] != 0x0B || header[1] != 0x77) {
            pos++;
            continue;
        }

        int fscod = (header[4] >> 6) & 0x03;
        int frmsizecod = header[4] & 0x3F;
        if (fscod >= 3 || frmsizecod >= 38) {
            pos++;
            continue;
        }

        int frameSize = AC3FrameWords[fscod][frmsizecod] * 2;
        if (frameSize <= 0) {
            pos++;
            continue;
        }

        int acmod = (header[6] >> 5) & 0x07;

        // Sample first SAMPLE_FRAMES from CutIn and last SAMPLE_FRAMES before CutOut
        bool inCutInRange  = (frameIndex >= cutInFrame && frameIndex < cutInFrame + SAMPLE_FRAMES);
        bool inCutOutRange = (frameIndex >= cutOutFrame - SAMPLE_FRAMES && frameIndex < cutOutFrame);

        if (inCutInRange || inCutOutRange) {
            acmodCount[acmod]++;
            totalFrames++;
            if (firstAcmod < 0) firstAcmod = acmod;
            lastAcmod = acmod;
        }

        // Stop scanning well past cutOut
        if (frameIndex > cutOutFrame + SAMPLE_FRAMES)
            break;

        frameIndex++;
        pos += frameSize;
    }

    file.close();

    if (totalFrames == 0)
        return info;

    // Determine main acmod (majority)
    int mainAcmod = 0;
    int maxCount = 0;
    for (int i = 0; i < 8; i++) {
        if (acmodCount[i] > maxCount) {
            maxCount = acmodCount[i];
            mainAcmod = i;
        }
    }

    info.mainAcmod = mainAcmod;
    info.cutInAcmod = firstAcmod;
    info.cutOutAcmod = lastAcmod;

    qDebug() << "analyzeAcmod:" << QFileInfo(audioFile).fileName()
             << "main=" << mainAcmod << "cutIn=" << firstAcmod << "cutOut=" << lastAcmod
             << "frames=" << totalFrames;

    return info;
}

// ----------------------------------------------------------------------------
// Cut audio elementary stream using libav stream-copy (no external process)
// All segments are handled in a single pass with PTS offset management.
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::cutAudioStream(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList,
                                      bool normalizeAcmod,
                                      const QList<int>& targetAcmods)
{
    if (!QFile::exists(inputFile)) {
        setError(QString("Audio file not found: %1").arg(inputFile));
        return false;
    }

    if (cutList.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    qDebug() << "cutAudioStream: libav stream-copy";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutList.size();

    // Open input
    AVFormatContext* inFmtCtx = nullptr;
    int ret = avformat_open_input(&inFmtCtx, inputFile.toUtf8().constData(),
                                   nullptr, nullptr);
    if (ret < 0) {
        setError(QString("Cannot open audio input: %1").arg(avErrorToString(ret)));
        return false;
    }

    ret = avformat_find_stream_info(inFmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inFmtCtx);
        setError("Cannot find audio stream info");
        return false;
    }

    int audioIdx = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_AUDIO,
                                        -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        avformat_close_input(&inFmtCtx);
        setError("No audio stream found in input");
        return false;
    }

    AVStream* inStream = inFmtCtx->streams[audioIdx];

    // Open output — format auto-detected from file extension (.ac3, .mp2, etc.)
    AVFormatContext* outFmtCtx = nullptr;
    ret = avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr,
                                          outputFile.toUtf8().constData());
    if (ret < 0 || !outFmtCtx) {
        avformat_close_input(&inFmtCtx);
        setError("Cannot create audio output context");
        return false;
    }

    AVStream* outStream = avformat_new_stream(outFmtCtx, nullptr);
    if (!outStream) {
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        setError("Cannot create output audio stream");
        return false;
    }
    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    outStream->time_base = inStream->time_base;

    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, outputFile.toUtf8().constData(),
                         AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_close_input(&inFmtCtx);
            avformat_free_context(outFmtCtx);
            setError(QString("Cannot open output file: %1").arg(avErrorToString(ret)));
            return false;
        }
    }

    ret = avformat_write_header(outFmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inFmtCtx);
        avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        setError("Cannot write audio output header");
        return false;
    }

    // Audio frame duration in stream time_base units
    int64_t frameDuration = 0;
    if (inStream->codecpar->frame_size > 0 && inStream->codecpar->sample_rate > 0) {
        frameDuration = av_rescale_q(inStream->codecpar->frame_size,
            AVRational{1, inStream->codecpar->sample_rate}, inStream->time_base);
    }
    if (frameDuration <= 0) {
        frameDuration = av_rescale_q(1, AVRational{32, 1000}, inStream->time_base);
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        avformat_close_input(&inFmtCtx);
        avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        setError("Cannot allocate packet");
        return false;
    }

    // Setup AC3 decoder/encoder for acmod normalization (lazy init)
    AVCodecContext* ac3DecCtx = nullptr;
    AVCodecContext* ac3EncCtx = nullptr;
    struct SwrContext* swrCtx = nullptr;
    AVFrame* ac3Frame = nullptr;
    AVFrame* ac3ConvertedFrame = nullptr;
    bool acmodNormActive = normalizeAcmod &&
                           inStream->codecpar->codec_id == AV_CODEC_ID_AC3;
    int acmodReencoded = 0;

    // Process all segments in a single pass with PTS offset management
    int64_t ptsOffset = 0;
    int64_t nextOutputPts = 0;

    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        double startTime = cutList[segIdx].first;
        double endTime = cutList[segIdx].second;

        // Determine target acmod for this segment
        int segTargetAcmod = -1;
        if (acmodNormActive && segIdx < targetAcmods.size()) {
            segTargetAcmod = targetAcmods[segIdx];
        }

        qDebug() << "  Segment" << segIdx << ":" << startTime << "->" << endTime
                 << (segTargetAcmod >= 0 ? QString("targetAcmod=%1").arg(segTargetAcmod) : "");

        // Seek to just before start time using audio stream timebase
        int64_t seekTs = static_cast<int64_t>(startTime / av_q2d(inStream->time_base));
        av_seek_frame(inFmtCtx, audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);

        bool segmentStarted = false;
        while (av_read_frame(inFmtCtx, pkt) >= 0) {
            if (pkt->stream_index != audioIdx) {
                av_packet_unref(pkt);
                continue;
            }

            if (pkt->pts == AV_NOPTS_VALUE) {
                av_packet_unref(pkt);
                continue;
            }

            double pktTime = pkt->pts * av_q2d(inStream->time_base);

            // Skip packets before start time (1ms tolerance for frame alignment)
            if (pktTime < startTime - 0.001) {
                av_packet_unref(pkt);
                continue;
            }

            // Stop at end time — only include frames that fit completely
            double frameDurSec = frameDuration * av_q2d(inStream->time_base);
            if (pktTime + frameDurSec > endTime + 0.001) {
                av_packet_unref(pkt);
                break;
            }

            // Set PTS offset on first packet of each segment
            if (!segmentStarted) {
                ptsOffset = nextOutputPts - pkt->pts;
                segmentStarted = true;
            }

            // Check if this frame needs acmod re-encoding
            bool needsReencode = false;
            if (segTargetAcmod >= 0 && pkt->size >= 7) {
                int frameAcmod = (pkt->data[6] >> 5) & 0x07;
                needsReencode = (frameAcmod != segTargetAcmod);
            }

            if (needsReencode) {
                // Lazy init decoder/encoder on first re-encode
                if (!ac3DecCtx) {
                    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_AC3);
                    ac3DecCtx = dec ? avcodec_alloc_context3(dec) : nullptr;
                    if (!ac3DecCtx) { needsReencode = false; }
                    else {
                        if (avcodec_parameters_to_context(ac3DecCtx, inStream->codecpar) < 0 ||
                            avcodec_open2(ac3DecCtx, dec, nullptr) < 0) {
                            avcodec_free_context(&ac3DecCtx);
                            needsReencode = false;
                        } else {
                            ac3Frame = av_frame_alloc();
                            if (!ac3Frame) {
                                avcodec_free_context(&ac3DecCtx);
                                needsReencode = false;
                            }
                        }
                    }
                }
                if (needsReencode && !ac3EncCtx) {
                    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_AC3);
                    if (!enc) {
                        qWarning() << "AC3 encoder not available — skipping AC3 re-encode";
                        needsReencode = false;
                    } else {
                        ac3EncCtx = avcodec_alloc_context3(enc);
                        if (!ac3EncCtx) {
                            qWarning() << "avcodec_alloc_context3 failed for AC3 encoder";
                            needsReencode = false;
                        } else {
                            ac3EncCtx->sample_rate = inStream->codecpar->sample_rate;
                            ac3EncCtx->bit_rate = inStream->codecpar->bit_rate > 0
                                ? inStream->codecpar->bit_rate : 384000;
                            ac3EncCtx->time_base = inStream->time_base;
                            // Set target channel layout based on target acmod
                            if (segTargetAcmod == 7 || segTargetAcmod == 6) {
                                // 5.1: 3/2 + LFE
                                AVChannelLayout layout51 = AV_CHANNEL_LAYOUT_5POINT1;
                                av_channel_layout_copy(&ac3EncCtx->ch_layout, &layout51);
                            } else {
                                // Stereo: 2/0
                                AVChannelLayout layoutStereo = AV_CHANNEL_LAYOUT_STEREO;
                                av_channel_layout_copy(&ac3EncCtx->ch_layout, &layoutStereo);
                            }
                            ac3EncCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
                            if (avcodec_open2(ac3EncCtx, enc, nullptr) < 0) {
                                qWarning() << "avcodec_open2 failed for AC3 encoder";
                                avcodec_free_context(&ac3EncCtx);
                                needsReencode = false;
                            }
                        }
                    }
                }
            }

            if (needsReencode) {
                // Decode
                avcodec_send_packet(ac3DecCtx, pkt);
                int decRet = avcodec_receive_frame(ac3DecCtx, ac3Frame);
                if (decRet == 0) {
                    // Setup resampler on first use (channel layout conversion)
                    if (!swrCtx) {
                        swr_alloc_set_opts2(&swrCtx,
                            &ac3EncCtx->ch_layout, ac3EncCtx->sample_fmt, ac3EncCtx->sample_rate,
                            &ac3Frame->ch_layout, (AVSampleFormat)ac3Frame->format, ac3Frame->sample_rate,
                            0, nullptr);
                        swr_init(swrCtx);

                        ac3ConvertedFrame = av_frame_alloc();
                        if (!ac3ConvertedFrame) { av_packet_unref(pkt); continue; }
                        av_channel_layout_copy(&ac3ConvertedFrame->ch_layout, &ac3EncCtx->ch_layout);
                        ac3ConvertedFrame->format = ac3EncCtx->sample_fmt;
                        ac3ConvertedFrame->sample_rate = ac3EncCtx->sample_rate;
                        ac3ConvertedFrame->nb_samples = ac3Frame->nb_samples;
                        av_frame_get_buffer(ac3ConvertedFrame, 0);
                    }

                    // Convert channel layout
                    ac3ConvertedFrame->nb_samples = ac3Frame->nb_samples;
                    swr_convert(swrCtx,
                        ac3ConvertedFrame->data, ac3ConvertedFrame->nb_samples,
                        (const uint8_t**)ac3Frame->data, ac3Frame->nb_samples);

                    ac3ConvertedFrame->pts = pkt->pts + ptsOffset;

                    // Re-encode with target channel layout
                    avcodec_send_frame(ac3EncCtx, ac3ConvertedFrame);
                    AVPacket* encPkt = av_packet_alloc();
                    if (encPkt && avcodec_receive_packet(ac3EncCtx, encPkt) == 0) {
                        encPkt->pts = pkt->pts + ptsOffset;
                        encPkt->dts = encPkt->pts;
                        encPkt->stream_index = 0;
                        encPkt->pos = -1;
                        ret = av_write_frame(outFmtCtx, encPkt);
                        if (ret >= 0) {
                            nextOutputPts = encPkt->pts + frameDuration;
                            acmodReencoded++;
                        }
                    }
                    av_packet_free(&encPkt);
                }
                av_packet_unref(pkt);
            } else {
                // Normal stream-copy
                pkt->pts += ptsOffset;
                pkt->dts = pkt->pts;
                pkt->stream_index = 0;
                pkt->pos = -1;

                ret = av_write_frame(outFmtCtx, pkt);
                if (ret < 0) {
                    qDebug() << "  Warning: av_write_frame failed at" << pktTime;
                } else {
                    nextOutputPts = pkt->pts + frameDuration;
                }

                av_packet_unref(pkt);
            }
        }
    }

    // Cleanup acmod re-encode resources
    if (swrCtx)             swr_free(&swrCtx);
    if (ac3ConvertedFrame)  av_frame_free(&ac3ConvertedFrame);
    if (ac3DecCtx)          avcodec_free_context(&ac3DecCtx);
    if (ac3EncCtx)          avcodec_free_context(&ac3EncCtx);
    if (ac3Frame)           av_frame_free(&ac3Frame);
    if (acmodReencoded > 0) {
        qDebug() << "  AC3 acmod normalization: re-encoded" << acmodReencoded << "frames";
    }

    av_packet_free(&pkt);
    av_write_trailer(outFmtCtx);

    // Cleanup
    avformat_close_input(&inFmtCtx);
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmtCtx->pb);
    avformat_free_context(outFmtCtx);

    // Verify output
    QFileInfo outInfo(outputFile);
    if (!outInfo.exists() || outInfo.size() == 0) {
        setError(QString("Audio cut produced 0-byte output: %1").arg(outputFile));
        return false;
    }

    qDebug() << "cutAudioStream: Complete, output size:" << outInfo.size() << "bytes";
    return true;
}

// ----------------------------------------------------------------------------
// Cut SRT subtitle file
// SRT is text-based, so we parse and filter by time ranges
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::cutSrtSubtitle(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList)
{
    if (!QFile::exists(inputFile)) {
        setError(QString("SRT file not found: %1").arg(inputFile));
        return false;
    }

    qDebug() << "cutSrtSubtitle: Cutting SRT file";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;

    QFile inFile(inputFile);
    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError("Cannot open input SRT file");
        return false;
    }

    QFile outFile(outputFile);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        inFile.close();
        setError("Cannot create output SRT file");
        return false;
    }

    QTextStream in(&inFile);
    QTextStream srtOut(&outFile);

    int outputIndex = 1;
    QString line;
    int state = 0; // 0=expect index, 1=expect time, 2=expect text

    double startTime = 0, endTime = 0;
    QStringList textLines;

    // Calculate cumulative duration for offset calculation
    QList<double> cumulativeDurations;
    double totalKept = 0;
    for (int i = 0; i < cutList.size(); ++i) {
        cumulativeDurations.append(totalKept);
        totalKept += cutList[i].second - cutList[i].first;
    }

    while (!in.atEnd()) {
        line = in.readLine();

        if (state == 0) {
            // Expect subtitle index
            bool ok;
            line.trimmed().toInt(&ok);
            if (ok) {
                state = 1;
            }
        }
        else if (state == 1) {
            // Expect time code: 00:01:23,456 --> 00:01:25,789
            QRegularExpression timeRe("(\\d{2}):(\\d{2}):(\\d{2}),(\\d{3})\\s*-->\\s*(\\d{2}):(\\d{2}):(\\d{2}),(\\d{3})");
            QRegularExpressionMatch match = timeRe.match(line);

            if (match.hasMatch()) {
                startTime = match.captured(1).toInt() * 3600 +
                            match.captured(2).toInt() * 60 +
                            match.captured(3).toInt() +
                            match.captured(4).toInt() / 1000.0;
                endTime = match.captured(5).toInt() * 3600 +
                          match.captured(6).toInt() * 60 +
                          match.captured(7).toInt() +
                          match.captured(8).toInt() / 1000.0;
                state = 2;
                textLines.clear();
            } else {
                state = 0;
            }
        }
        else if (state == 2) {
            // Collect text lines until empty line
            if (line.trimmed().isEmpty()) {
                // End of subtitle entry - check if it falls within kept segments
                for (int i = 0; i < cutList.size(); ++i) {
                    double segStart = cutList[i].first;
                    double segEnd = cutList[i].second;

                    // Check if subtitle overlaps with this segment
                    if (startTime < segEnd && endTime > segStart) {
                        // Calculate adjusted times
                        double adjStart = qMax(startTime, segStart) - segStart + cumulativeDurations[i];
                        double adjEnd = qMin(endTime, segEnd) - segStart + cumulativeDurations[i];

                        // Format time code
                        auto formatTime = [](double t) -> QString {
                            int h = static_cast<int>(t / 3600);
                            int m = static_cast<int>((t - h * 3600) / 60);
                            int s = static_cast<int>(t - h * 3600 - m * 60);
                            int ms = static_cast<int>((t - static_cast<int>(t)) * 1000);
                            return QString("%1:%2:%3,%4")
                                .arg(h, 2, 10, QChar('0'))
                                .arg(m, 2, 10, QChar('0'))
                                .arg(s, 2, 10, QChar('0'))
                                .arg(ms, 3, 10, QChar('0'));
                        };

                        srtOut << outputIndex++ << "\n";
                        srtOut << formatTime(adjStart) << " --> " << formatTime(adjEnd) << "\n";
                        for (const QString& textLine : textLines) {
                            srtOut << textLine << "\n";
                        }
                        srtOut << "\n";
                        break; // Only write once per subtitle
                    }
                }
                state = 0;
            } else {
                textLines.append(line);
            }
        }
    }

    inFile.close();
    outFile.close();

    qDebug() << "cutSrtSubtitle: Complete";
    qDebug() << "  Subtitles written:" << (outputIndex - 1);

    return true;
}

// ----------------------------------------------------------------------------
// Detect audio burst near a cut boundary using libav (no external process).
// Decodes ~200ms of audio around a boundary, calculates per-frame RMS,
// and checks if boundary frames are significantly louder than context.
// Returns true if a sudden loudness burst (>20dB above median) is detected.
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::detectAudioBurst(const QString& audioFile, double boundaryTime,
                                        bool isCutOut, double& burstRmsDb, double& contextRmsDb)
{
    // Open audio file
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, audioFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qDebug() << "detectAudioBurst: cannot open" << audioFile;
        return false;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Find audio stream
    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = i;
            break;
        }
    }
    if (audioIdx < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVStream* stream = fmtCtx->streams[audioIdx];

    // Open decoder
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVCodecContext* decCtx = avcodec_alloc_context3(codec);
    if (!decCtx) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    if (avcodec_parameters_to_context(decCtx, stream->codecpar) < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }
    ret = avcodec_open2(decCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    int sampleRate = decCtx->sample_rate;
    int channels   = decCtx->ch_layout.nb_channels;
    if (sampleRate <= 0 || channels <= 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Audio frame duration is the natural per-codec snap unit:
    //   MP2 @48k = 24 ms, AC3 @48k = 32 ms.
    // planAudioCut snaps the audio cut to this grid, so the boundary can
    // round at most ½ frame past the video cut. Anything further past the
    // boundary in the SOURCE can never end up in the kept audio — clamping
    // the analysis tail to frameDuration/2 keeps the detector honest for
    // all codecs without a separate code path per format.
    double frameDuration = (double)decCtx->frame_size / sampleRate;
    if (frameDuration <= 0) frameDuration = 0.032;  // AC3 default
    double tailSec = frameDuration * 0.5;

    // Analyze a 200 ms context window on the kept side, plus the tail
    // (the only part of the source that frame-snapping could leak in).
    double windowStart, windowEnd;
    if (isCutOut) {
        windowStart = qMax(0.0, boundaryTime - 0.200);
        windowEnd   = boundaryTime + tailSec;
    } else {
        windowStart = qMax(0.0, boundaryTime - tailSec);
        windowEnd   = boundaryTime + 0.200;
    }

    // Seek to window start using stream timebase for precision
    int64_t seekTs = (int64_t)(windowStart / av_q2d(stream->time_base));
    ret = av_seek_frame(fmtCtx, audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // Fallback: seek from beginning
        av_seek_frame(fmtCtx, audioIdx, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(decCtx);

    // Decode audio and collect per-frame RMS values
    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }
    QList<double> rmsValues;

    while (av_read_frame(fmtCtx, packet) >= 0) {
        if (packet->stream_index != audioIdx) {
            av_packet_unref(packet);
            continue;
        }

        // Check if we're past the window. The frame-level reject below uses
        // a strict windowEnd, so the packet stop matches — no extra frame
        // duration of slack here either.
        double pktTime = packet->pts * av_q2d(stream->time_base);
        if (pktTime >= windowEnd) {
            av_packet_unref(packet);
            break;
        }

        ret = avcodec_send_packet(decCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(decCtx, frame) == 0) {
            // Check frame timestamp
            // Keep frames that overlap the window: reject those whose end is
            // at/before windowStart, or whose start is at/after windowEnd.
            // Earlier code allowed a +frameDuration slack on the upper bound,
            // which made the effective tail another full frame longer than
            // intended and produced false-positive bursts on material that
            // can't actually leak through frame snapping.
            double frameTime = frame->pts * av_q2d(stream->time_base);
            if (frameTime + frameDuration <= windowStart) {
                av_frame_unref(frame);
                continue;
            }
            if (frameTime >= windowEnd) {
                av_frame_unref(frame);
                goto done_reading;
            }

            // Calculate RMS from decoded samples
            double sumSq = 0.0;
            int totalSamples = frame->nb_samples * channels;

            if (totalSamples > 0) {
                // Handle different sample formats
                switch (decCtx->sample_fmt) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    for (int ch = 0; ch < channels; ch++) {
                        const float* data = (const float*)frame->data[
                            decCtx->sample_fmt == AV_SAMPLE_FMT_FLTP ? ch : 0];
                        int stride = (decCtx->sample_fmt == AV_SAMPLE_FMT_FLTP) ? 1 : channels;
                        int offset = (decCtx->sample_fmt == AV_SAMPLE_FMT_FLTP) ? 0 : ch;
                        for (int s = 0; s < frame->nb_samples; s++) {
                            double v = data[s * stride + offset];
                            sumSq += v * v;
                        }
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    const double scale = 1.0 / 32768.0;
                    for (int ch = 0; ch < channels; ch++) {
                        const int16_t* data = (const int16_t*)frame->data[
                            decCtx->sample_fmt == AV_SAMPLE_FMT_S16P ? ch : 0];
                        int stride = (decCtx->sample_fmt == AV_SAMPLE_FMT_S16P) ? 1 : channels;
                        int offset = (decCtx->sample_fmt == AV_SAMPLE_FMT_S16P) ? 0 : ch;
                        for (int s = 0; s < frame->nb_samples; s++) {
                            double v = data[s * stride + offset] * scale;
                            sumSq += v * v;
                        }
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    const double scale = 1.0 / 2147483648.0;
                    for (int ch = 0; ch < channels; ch++) {
                        const int32_t* data = (const int32_t*)frame->data[
                            decCtx->sample_fmt == AV_SAMPLE_FMT_S32P ? ch : 0];
                        int stride = (decCtx->sample_fmt == AV_SAMPLE_FMT_S32P) ? 1 : channels;
                        int offset = (decCtx->sample_fmt == AV_SAMPLE_FMT_S32P) ? 0 : ch;
                        for (int s = 0; s < frame->nb_samples; s++) {
                            double v = data[s * stride + offset] * scale;
                            sumSq += v * v;
                        }
                    }
                    break;
                }
                default:
                    // Unsupported format — skip
                    av_frame_unref(frame);
                    continue;
                }

                double rms = sqrt(sumSq / totalSamples);
                double rmsDb = (rms > 0.0) ? 20.0 * log10(rms) : -120.0;
                rmsValues.append(rmsDb);
            }
            av_frame_unref(frame);
        }
    }

done_reading:
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decCtx);
    avformat_close_input(&fmtCtx);

    if (rmsValues.size() < 3) {
        qDebug() << "detectAudioBurst: only" << rmsValues.size()
                 << "chunks at" << boundaryTime << "(need >=3)";
        return false;
    }

    // Calculate median RMS
    QList<double> sorted = rmsValues;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];

    // Check for burst: boundary chunks >20dB above median and above -40dB absolute
    // For CutOut: check last 2 chunks; for CutIn: check first 2 chunks
    int checkStart = isCutOut ? qMax(0, rmsValues.size() - 2) : 0;
    int checkEnd   = isCutOut ? rmsValues.size() : qMin(2, rmsValues.size());

    for (int i = checkStart; i < checkEnd; i++) {
        if (rmsValues[i] - median > 20.0 && rmsValues[i] > -40.0) {
            burstRmsDb = rmsValues[i];
            contextRmsDb = median;
            qDebug() << "detectAudioBurst: BURST at" << boundaryTime
                     << (isCutOut ? "CutOut" : "CutIn")
                     << "burst=" << burstRmsDb << "dB, context=" << median << "dB"
                     << "(" << rmsValues.size() << "chunks)";
            return true;
        }
    }

    qDebug() << "detectAudioBurst: OK at" << boundaryTime
             << (isCutOut ? "CutOut" : "CutIn")
             << "median=" << median << "dB (" << rmsValues.size() << "chunks)";
    return false;
}
