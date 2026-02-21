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
#include <QTime>
#include <QProcess>
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
    QString suffix = QFileInfo(filePath).suffix().toLower();
    bool isES = (suffix == "264" || suffix == "h264" ||
                 suffix == "265" || suffix == "h265" || suffix == "hevc" ||
                 suffix == "m2v" || suffix == "mpv");

    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    if (isES) {
        // For elementary streams, we need special handling
        // Set large probesize and analyzeduration for proper detection
        av_dict_set(&opts, "probesize", "50000000", 0);  // 50MB
        av_dict_set(&opts, "analyzeduration", "10000000", 0);  // 10 seconds

        // Force input format based on extension
        if (suffix == "264" || suffix == "h264") {
            inputFmt = av_find_input_format("h264");
        } else if (suffix == "265" || suffix == "h265" || suffix == "hevc") {
            inputFmt = av_find_input_format("hevc");
        } else if (suffix == "m2v" || suffix == "mpv") {
            inputFmt = av_find_input_format("mpegvideo");
        }

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
                avcodec_parameters_to_context(mVideoCodecCtx, videoStream->codecpar);
                mVideoCodecCtx->thread_count = 1;
                mVideoCodecCtx->thread_type = FF_THREAD_SLICE;
                ret = avcodec_open2(mVideoCodecCtx, codec, nullptr);
                if (ret < 0) {
                    qDebug() << "Warning: Could not open video codec:" << avErrorToString(ret);
                    avcodec_free_context(&mVideoCodecCtx);
                    mVideoCodecCtx = nullptr;
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

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            TTFrameInfo frameInfo;
            frameInfo.pts = packet->pts;
            frameInfo.dts = packet->dts;
            frameInfo.fileOffset = packet->pos;
            frameInfo.packetSize = packet->size;
            frameInfo.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            frameInfo.frameIndex = frameIndex;

            // Determine frame type
            if (frameInfo.isKeyframe) {
                frameInfo.frameType = AV_PICTURE_TYPE_I;
                // New GOP starts at keyframe
                if (frameIndex > 0) {
                    currentGOP++;
                }
            } else {
                // Parse slice_type from packet data for B-frame detection
                AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
                if (codecId == AV_CODEC_ID_H264) {
                    int sliceType = TTNaluParser::parseH264SliceTypeFromPacket(
                        packet->data, packet->size);
                    // H264: P=0, B=1, I=2
                    if (sliceType == H264::SLICE_B)
                        frameInfo.frameType = AV_PICTURE_TYPE_B;
                    else if (sliceType == H264::SLICE_I)
                        frameInfo.frameType = AV_PICTURE_TYPE_I;
                    else
                        frameInfo.frameType = AV_PICTURE_TYPE_P;
                } else if (codecId == AV_CODEC_ID_HEVC) {
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
                    QString("Indexing frame %1...").arg(frameIndex));
                lastProgress = progress;
            }
        }

        av_packet_unref(packet);
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

    emit progressChanged(100, QString("Indexed %1 frames").arg(mFrameIndex.size()));

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

    // Check if this is an ES file (needs byte-based seeking)
    QString suffix = QFileInfo(QString::fromUtf8(mFormatCtx->url)).suffix().toLower();
    bool isES = (suffix == "264" || suffix == "h264" ||
                 suffix == "265" || suffix == "h265" || suffix == "hevc" ||
                 suffix == "m2v" || suffix == "mpv");

    int64_t ret;
    if (isES && mFormatCtx->pb) {
        // For ES files, use byte-based seeking
        int64_t byteOffset = mFrameIndex[keyframeIndex].fileOffset;

        // If fileOffset is -1 (unknown), seek to byte 0 for first keyframe
        if (byteOffset < 0) {
            if (keyframeIndex == 0) {
                byteOffset = 0;
            } else {
                // For other frames, try to find a valid offset
                // Walk back to find a frame with valid offset
                for (int i = keyframeIndex; i >= 0; i--) {
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
        qDebug() << "ES seek to byte" << byteOffset << "avio_seek result:" << ret;
        if (ret >= 0) {
            avformat_flush(mFormatCtx);
            ret = 0;  // Success
        } else {
            qDebug() << "avio_seek failed with:" << ret << avErrorToString(ret);
        }
    } else {
        // For container formats, use timestamp-based seeking
        int64_t seekPts = mFrameIndex[keyframeIndex].pts;
        qDebug() << "Container seek to PTS" << seekPts;
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

    mCurrentFrameIndex = keyframeIndex;
    mDecoderFrameIndex = keyframeIndex;
    return true;
}

// ----------------------------------------------------------------------------
// Decode frame at specific index and return as QImage
// ----------------------------------------------------------------------------
QImage TTFFmpegWrapper::decodeFrame(int frameIndex)
{
    // Check LRU cache first
    if (mFrameCache.contains(frameIndex)) {
        // Move to back of LRU list (most recently used)
        mFrameCacheLRU.removeOne(frameIndex);
        mFrameCacheLRU.append(frameIndex);
        return mFrameCache[frameIndex];
    }

    // Check if decoder is already positioned just before target frame
    // (sequential navigation: j/k stepping one frame at a time)
    bool needSeek = true;
    if (mDecoderDrained) {
        // Decoder was flushed for EOF drain — must seek to reset state
        needSeek = true;
    } else if (mDecoderFrameIndex >= 0 && mDecoderFrameIndex < frameIndex) {
        // Find keyframe for target
        int keyframeIndex = frameIndex;
        while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe) {
            keyframeIndex--;
        }
        // If decoder is at or past the keyframe, we can continue without seeking
        if (mDecoderFrameIndex >= keyframeIndex) {
            needSeek = false;
        }
    }

    if (needSeek) {
        int keyframeIndex = frameIndex;
        while (keyframeIndex > 0 && !mFrameIndex[keyframeIndex].isKeyframe) {
            keyframeIndex--;
        }
        qDebug() << "decodeFrame: seek target=" << frameIndex
                 << "keyframe=" << keyframeIndex
                 << "frames_to_decode=" << (frameIndex - keyframeIndex + 1);

        if (!seekToFrame(frameIndex)) {
            qDebug() << "decodeFrame: seekToFrame failed for frame" << frameIndex;
            return QImage();
        }
        mDecoderFrameIndex = mCurrentFrameIndex;
    }

    // Skip intermediate frames (decode without RGB conversion)
    while (mDecoderFrameIndex < frameIndex) {
        if (!skipCurrentFrame()) {
            qDebug() << "decodeFrame: skipCurrentFrame failed at" << mDecoderFrameIndex
                     << "(target=" << frameIndex << ")";
            return QImage();
        }
        mDecoderFrameIndex++;
    }

    // Decode final target frame with full RGB conversion
    QImage result = decodeCurrentFrame();
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

        // Allocate buffer for RGB frame
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
            mVideoCodecCtx->width, mVideoCodecCtx->height, 1);
        uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
        av_image_fill_arrays(mRgbFrame->data, mRgbFrame->linesize, buffer,
            AV_PIX_FMT_RGB24, mVideoCodecCtx->width, mVideoCodecCtx->height, 1);
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
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
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
        }
    }

    av_packet_free(&packet);
    return result;
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
    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == mVideoStreamIndex) {
            int ret = avcodec_send_packet(mVideoCodecCtx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }
            ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
            if (ret == 0) {
                // Frame decoded (reference chain updated) — no RGB conversion
                decoded = true;
                av_packet_unref(packet);
                break;
            }
        }
        av_packet_unref(packet);
    }

    // EOF drain: flush decoder pipeline to retrieve buffered frames
    if (!decoded) {
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
            decoded = true;
            mDecoderDrained = true;
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
// Cut audio elementary stream using FFmpeg
// Audio is cut time-based (ms-accurate) using ffmpeg concat demuxer
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::cutAudioStream(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList)
{
    if (!QFile::exists(inputFile)) {
        setError(QString("Audio file not found: %1").arg(inputFile));
        return false;
    }

    if (cutList.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    qDebug() << "cutAudioStream: Lossless stream-copy audio cutting";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutList.size();

    bool success = false;

    if (cutList.size() == 1) {
        // Single segment: direct stream-copy with -ss/-to
        QStringList args;
        args << "-y"
             << "-i" << inputFile
             << "-ss" << QString::number(cutList[0].first, 'f', 6)
             << "-to" << QString::number(cutList[0].second, 'f', 6)
             << "-c:a" << "copy"
             << outputFile;

        qDebug() << "  FFmpeg command: ffmpeg" << args.join(" ");

        QProcess proc;
        proc.start("/usr/bin/ffmpeg", args);

        if (!proc.waitForStarted(5000)) {
            setError("FFmpeg failed to start");
            return false;
        }

        if (!proc.waitForFinished(300000)) {
            setError("FFmpeg timed out");
            proc.kill();
            return false;
        }

        QString stderr = QString::fromUtf8(proc.readAllStandardError());
        if (!stderr.isEmpty()) qDebug() << "  FFmpeg stderr:" << stderr;

        if (proc.exitCode() != 0) {
            setError(QString("FFmpeg failed: %1").arg(stderr));
            return false;
        }
        success = true;

    } else {
        // Multiple segments: cut each to temp file, then concat
        QString tempDir = TTCut::tempDirPath;
        QStringList tempFiles;
        QString concatListFile = tempDir + "/audio_concat_list.txt";

        QFile concatFile(concatListFile);
        if (!concatFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            setError(QString("Cannot create concat list: %1").arg(concatListFile));
            return false;
        }

        QTextStream concatStream(&concatFile);

        for (int i = 0; i < cutList.size(); ++i) {
            QString tempFile = QString("%1/audio_seg_%2.%3")
                .arg(tempDir)
                .arg(i)
                .arg(QFileInfo(inputFile).suffix());
            tempFiles << tempFile;

            qDebug() << "  Segment" << i << ":" << cutList[i].first << "->" << cutList[i].second;

            QStringList args;
            args << "-y"
                 << "-i" << inputFile
                 << "-ss" << QString::number(cutList[i].first, 'f', 6)
                 << "-to" << QString::number(cutList[i].second, 'f', 6)
                 << "-c:a" << "copy"
                 << tempFile;

            QProcess proc;
            proc.start("/usr/bin/ffmpeg", args);

            if (!proc.waitForStarted(5000)) {
                setError(QString("FFmpeg failed to start for segment %1").arg(i));
                goto cleanup;
            }

            if (!proc.waitForFinished(300000)) {
                setError(QString("FFmpeg timed out for segment %1").arg(i));
                proc.kill();
                goto cleanup;
            }

            QString stderr = QString::fromUtf8(proc.readAllStandardError());
            if (!stderr.isEmpty()) qDebug() << "  Segment" << i << "stderr:" << stderr;

            if (proc.exitCode() != 0) {
                setError(QString("FFmpeg failed for segment %1: %2").arg(i).arg(stderr));
                goto cleanup;
            }

            // Check output size
            QFileInfo segInfo(tempFile);
            if (!segInfo.exists() || segInfo.size() == 0) {
                setError(QString("Segment %1 produced 0-byte output").arg(i));
                goto cleanup;
            }

            concatStream << "file '" << tempFile << "'\n";
        }
        concatFile.close();

        // Concat all segments
        {
            QStringList args;
            args << "-y"
                 << "-f" << "concat"
                 << "-safe" << "0"
                 << "-i" << concatListFile
                 << "-c:a" << "copy"
                 << outputFile;

            qDebug() << "  Concat command: ffmpeg" << args.join(" ");

            QProcess proc;
            proc.start("/usr/bin/ffmpeg", args);

            if (!proc.waitForStarted(5000)) {
                setError("FFmpeg concat failed to start");
                goto cleanup;
            }

            if (!proc.waitForFinished(300000)) {
                setError("FFmpeg concat timed out");
                proc.kill();
                goto cleanup;
            }

            QString stderr = QString::fromUtf8(proc.readAllStandardError());
            if (!stderr.isEmpty()) qDebug() << "  Concat stderr:" << stderr;

            if (proc.exitCode() != 0) {
                setError(QString("FFmpeg concat failed: %1").arg(stderr));
                goto cleanup;
            }
        }
        success = true;

cleanup:
        // Remove temp files
        for (const QString& tf : tempFiles) {
            QFile::remove(tf);
        }
        QFile::remove(concatListFile);
    }

    // Verify output
    QFileInfo outInfo(outputFile);
    if (!outInfo.exists() || outInfo.size() == 0) {
        setError(QString("Audio cut produced 0-byte output: %1").arg(outputFile));
        return false;
    }

    qDebug() << "cutAudioStream: Complete, output size:" << outInfo.size() << "bytes";
    return success;
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
    // Analyze window around boundary
    // CutOut: 200ms before + 48ms after (catch commercial audio leaking in)
    // CutIn: 48ms before + 200ms after
    double windowStart, windowEnd;
    if (isCutOut) {
        windowStart = qMax(0.0, boundaryTime - 0.200);
        windowEnd   = boundaryTime + 0.048;
    } else {
        windowStart = qMax(0.0, boundaryTime - 0.048);
        windowEnd   = boundaryTime + 0.200;
    }

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
    avcodec_parameters_to_context(decCtx, stream->codecpar);
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

    // Seek to window start using stream timebase for precision
    int64_t seekTs = (int64_t)(windowStart / av_q2d(stream->time_base));
    ret = av_seek_frame(fmtCtx, audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // Fallback: seek from beginning
        av_seek_frame(fmtCtx, audioIdx, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(decCtx);

    // Audio frame duration for tolerance (one frame)
    double frameDuration = (double)decCtx->frame_size / sampleRate;
    if (frameDuration <= 0) frameDuration = 0.032;  // AC3 default

    // Decode audio and collect per-frame RMS values
    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();
    QList<double> rmsValues;

    while (av_read_frame(fmtCtx, packet) >= 0) {
        if (packet->stream_index != audioIdx) {
            av_packet_unref(packet);
            continue;
        }

        // Check if we're past the window
        double pktTime = packet->pts * av_q2d(stream->time_base);
        if (pktTime > windowEnd + frameDuration) {
            av_packet_unref(packet);
            break;
        }

        ret = avcodec_send_packet(decCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(decCtx, frame) == 0) {
            // Check frame timestamp
            double frameTime = frame->pts * av_q2d(stream->time_base);
            if (frameTime < windowStart - frameDuration) {
                av_frame_unref(frame);
                continue;
            }
            if (frameTime > windowEnd + frameDuration) {
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
