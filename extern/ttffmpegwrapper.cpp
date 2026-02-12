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
                mVideoCodecCtx->thread_count = 0;  // auto-detect (use all cores)
                mVideoCodecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
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
                // Without full decoding, we can't distinguish P from B
                // This is a simplification - for accurate detection we'd need to decode
                frameInfo.frameType = AV_PICTURE_TYPE_P;
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
    Q_UNUSED(codecCtx);

    // Simple check: keyframe flag
    if (packet->flags & AV_PKT_FLAG_KEY) {
        return AV_PICTURE_TYPE_I;
    }

    // For more accurate detection, we would need to decode the frame
    // and check frame->pict_type
    // This is a simplification for initial implementation
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

    int ret;
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
    if (mDecoderFrameIndex >= 0 && mDecoderFrameIndex < frameIndex) {
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
// Extract segment from video (for cutting)
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::extractSegment(const QString& outputFile, int startFrame,
                                      int endFrame, bool reencode)
{
    if (!mFormatCtx || mFrameIndex.isEmpty()) {
        setError("No file open or frame index not built");
        return false;
    }

    if (startFrame < 0 || endFrame >= mFrameIndex.size() || startFrame > endFrame) {
        setError("Invalid frame range");
        return false;
    }

    // Get timestamps
    double startTime = ptsToSeconds(mFrameIndex[startFrame].pts, mVideoStreamIndex);
    double endTime = ptsToSeconds(mFrameIndex[endFrame].pts, mVideoStreamIndex);
    double duration = endTime - startTime;

    // Build ffmpeg command
    QStringList args;
    args << "-y"  // Overwrite
         << "-ss" << QString::number(startTime, 'f', 6)
         << "-i" << QString::fromUtf8(mFormatCtx->url)
         << "-t" << QString::number(duration, 'f', 6);

    if (reencode) {
        // Re-encode with H.264
        args << "-c:v" << "libx264"
             << "-preset" << "medium"
             << "-crf" << "18"
             << "-pix_fmt" << "yuv420p";
    } else {
        // Stream copy (only works between keyframes)
        args << "-c:v" << "copy";
    }

    args << "-an"  // No audio for now
         << outputFile;

    qDebug() << "FFmpeg extract command:" << args.join(" ");

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

    if (proc.exitCode() != 0) {
        setError(QString("FFmpeg failed: %1").arg(
            QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Helper: Check if timestamp is within any of the "keep" segments
// ----------------------------------------------------------------------------
static bool isTimestampIncluded(double ts, const QList<QPair<double, double>>& keepList)
{
    for (const auto& segment : keepList) {
        if (ts >= segment.first && ts < segment.second) {
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Helper: Check if a range overlaps with any "keep" segment
// Returns: 0 = no overlap (drop), 1 = full overlap (copy), 2 = partial overlap (encode)
// ----------------------------------------------------------------------------
static int getRangeMode(double startTs, double endTs, const QList<QPair<double, double>>& keepList)
{
    bool anyIncluded = false;
    bool anyExcluded = false;

    // Check start and end
    if (isTimestampIncluded(startTs, keepList)) anyIncluded = true;
    else anyExcluded = true;

    if (isTimestampIncluded(endTs, keepList)) anyIncluded = true;
    else anyExcluded = true;

    // Check if any keepList boundary falls within this range
    for (const auto& segment : keepList) {
        if (segment.first > startTs && segment.first < endTs) {
            // A cut-in point is within this range
            anyIncluded = true;
            anyExcluded = true;
        }
        if (segment.second > startTs && segment.second < endTs) {
            // A cut-out point is within this range
            anyIncluded = true;
            anyExcluded = true;
        }
    }

    if (anyIncluded && anyExcluded) return 2;  // Partial - need to encode
    if (anyIncluded) return 1;                  // Full copy
    return 0;                                   // Full drop
}

// ----------------------------------------------------------------------------
// Wrap elementary stream in MKV container with proper timestamps
// Used for ES files that have no PTS/DTS - mkvmerge assigns timestamps
// based on frame duration.
// Returns path to temporary MKV file, or empty string on failure.
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::wrapElementaryStream(const QString& esFile, double frameRate)
{
    if (!QFile::exists(esFile)) {
        setError(QString("ES file not found: %1").arg(esFile));
        return QString();
    }

    // Determine codec from file extension
    QString ext = QFileInfo(esFile).suffix().toLower();
    bool isH264 = (ext == "264" || ext == "h264" || ext == "avc");
    bool isH265 = (ext == "265" || ext == "h265" || ext == "hevc");

    if (!isH264 && !isH265) {
        setError(QString("Unsupported ES format: %1").arg(ext));
        return QString();
    }

    // Try to get frame rate from .info file if not provided
    if (frameRate <= 0) {
        QString infoFile = TTESInfo::findInfoFile(esFile);
        if (!infoFile.isEmpty()) {
            TTESInfo info(infoFile);
            if (info.isLoaded() && info.frameRate() > 0) {
                frameRate = info.frameRate();
                qDebug() << "Using frame rate from .info file:" << frameRate;
            }
        }
    }

    // Default to 25fps if still no frame rate
    if (frameRate <= 0) {
        frameRate = 25.0;
        qDebug() << "No frame rate found, using default:" << frameRate;
    }

    // Calculate frame duration in nanoseconds for mkvmerge
    // 50fps = 20,000,000 ns, 25fps = 40,000,000 ns
    int64_t frameDurationNs = static_cast<int64_t>(1000000000.0 / frameRate);

    // Create temporary output file
    QFileInfo esInfo(esFile);
    QString tempMkv = esInfo.absolutePath() + "/." + esInfo.completeBaseName() + "_temp.mkv";

    qDebug() << "Wrapping ES in MKV container";
    qDebug() << "  Input:" << esFile;
    qDebug() << "  Output:" << tempMkv;
    qDebug() << "  Frame rate:" << frameRate << "fps";
    qDebug() << "  Frame duration:" << frameDurationNs << "ns";

    // Build mkvmerge command
    QStringList args;
    args << "-o" << tempMkv
         << "--default-duration" << QString("0:%1ns").arg(frameDurationNs)
         << esFile;

    QProcess proc;
    proc.start("mkvmerge", args);

    if (!proc.waitForStarted(5000)) {
        setError("mkvmerge failed to start");
        return QString();
    }

    if (!proc.waitForFinished(300000)) { // 5 minute timeout
        setError("mkvmerge timed out");
        proc.kill();
        return QString();
    }

    if (proc.exitCode() != 0 && proc.exitCode() != 1) {
        // mkvmerge returns 1 for warnings, 0 for success, 2+ for errors
        setError(QString("mkvmerge failed (exit code %1): %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardError())));
        return QString();
    }

    if (!QFile::exists(tempMkv)) {
        setError("mkvmerge did not create output file");
        return QString();
    }

    qDebug() << "ES wrapped successfully:" << tempMkv;
    return tempMkv;
}

// ----------------------------------------------------------------------------
// Wrap ES in MKV using libav directly (no external tools)
// This gives full control over timestamps and avoids CLI tool quirks
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::wrapElementaryStreamLibav(const QString& esFile, double frameRate)
{
    if (!QFile::exists(esFile)) {
        setError(QString("ES file not found: %1").arg(esFile));
        return QString();
    }

    // Determine codec from file extension
    QString ext = QFileInfo(esFile).suffix().toLower();
    bool isH264 = (ext == "264" || ext == "h264" || ext == "avc");
    bool isH265 = (ext == "265" || ext == "h265" || ext == "hevc");

    if (!isH264 && !isH265) {
        setError(QString("Unsupported ES format for libav wrapping: %1").arg(ext));
        return QString();
    }

    // Try to get frame rate from .info file if not provided
    if (frameRate <= 0) {
        QString infoFile = TTESInfo::findInfoFile(esFile);
        if (!infoFile.isEmpty()) {
            TTESInfo info(infoFile);
            if (info.isLoaded() && info.frameRate() > 0) {
                frameRate = info.frameRate();
                qDebug() << "wrapElementaryStreamLibav: Using frame rate from .info:" << frameRate;
            }
        }
    }

    // Default to 25fps if still no frame rate
    if (frameRate <= 0) {
        frameRate = 25.0;
        qDebug() << "wrapElementaryStreamLibav: No frame rate found, using default:" << frameRate;
    }

    // Create temporary output file
    QFileInfo esInfo(esFile);
    QString tempMkv = esInfo.absolutePath() + "/." + esInfo.completeBaseName() + "_libav.mkv";

    qDebug() << "wrapElementaryStreamLibav: Wrapping ES in MKV container";
    qDebug() << "  Input:" << esFile;
    qDebug() << "  Output:" << tempMkv;
    qDebug() << "  Frame rate:" << frameRate << "fps";

    int ret;
    AVFormatContext* inFmtCtx = nullptr;
    AVFormatContext* outFmtCtx = nullptr;

    // === Open input ES file ===
    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    // Set probing options for ES files
    av_dict_set(&opts, "probesize", "50000000", 0);  // 50MB
    av_dict_set(&opts, "analyzeduration", "10000000", 0);  // 10 seconds

    // Force input format based on extension
    if (isH264) {
        inputFmt = av_find_input_format("h264");
    } else if (isH265) {
        inputFmt = av_find_input_format("hevc");
    }

    ret = avformat_open_input(&inFmtCtx, esFile.toUtf8().constData(), inputFmt, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        setError(QString("wrapElementaryStreamLibav: Could not open input: %1").arg(avErrorToString(ret)));
        return QString();
    }

    ret = avformat_find_stream_info(inFmtCtx, nullptr);
    if (ret < 0) {
        setError(QString("wrapElementaryStreamLibav: Could not find stream info: %1").arg(avErrorToString(ret)));
        avformat_close_input(&inFmtCtx);
        return QString();
    }

    // Find video stream
    int videoStreamIdx = -1;
    for (unsigned int i = 0; i < inFmtCtx->nb_streams; i++) {
        if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    if (videoStreamIdx < 0) {
        setError("wrapElementaryStreamLibav: No video stream found in ES file");
        avformat_close_input(&inFmtCtx);
        return QString();
    }

    AVStream* inStream = inFmtCtx->streams[videoStreamIdx];

    // === Create output MKV context ===
    ret = avformat_alloc_output_context2(&outFmtCtx, nullptr, "matroska", tempMkv.toUtf8().constData());
    if (ret < 0 || !outFmtCtx) {
        setError(QString("wrapElementaryStreamLibav: Could not create output context: %1").arg(avErrorToString(ret)));
        avformat_close_input(&inFmtCtx);
        return QString();
    }

    // Create output stream
    AVStream* outStream = avformat_new_stream(outFmtCtx, nullptr);
    if (!outStream) {
        setError("wrapElementaryStreamLibav: Could not create output stream");
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        return QString();
    }

    // Copy codec parameters from input
    ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    if (ret < 0) {
        setError(QString("wrapElementaryStreamLibav: Could not copy codec parameters: %1").arg(avErrorToString(ret)));
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        return QString();
    }
    outStream->codecpar->codec_tag = 0;  // Let muxer choose

    // Set time base based on frame rate
    // Use high-precision timebase (e.g., 1/90000 for broadcast, or 1/1000 for ms precision)
    outStream->time_base = (AVRational){1, 90000};

    // Calculate frame duration in output timebase
    // frame_duration = timebase / fps = 90000 / 50 = 1800 ticks for 50fps
    int64_t frameDuration = av_rescale_q(1, (AVRational){1, (int)(frameRate * 1000)},
                                          outStream->time_base) * 1000;
    // Simpler calculation: frameDuration = 90000 / frameRate
    frameDuration = (int64_t)(90000.0 / frameRate + 0.5);

    qDebug() << "  Output timebase: 1/90000";
    qDebug() << "  Frame duration:" << frameDuration << "ticks";

    // === Open output file ===
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, tempMkv.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            setError(QString("wrapElementaryStreamLibav: Could not open output file: %1").arg(avErrorToString(ret)));
            avformat_close_input(&inFmtCtx);
            avformat_free_context(outFmtCtx);
            return QString();
        }
    }

    // Write header
    ret = avformat_write_header(outFmtCtx, nullptr);
    if (ret < 0) {
        setError(QString("wrapElementaryStreamLibav: Could not write header: %1").arg(avErrorToString(ret)));
        avformat_close_input(&inFmtCtx);
        avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        return QString();
    }

    // === Read packets and write with proper timestamps ===
    AVPacket* packet = av_packet_alloc();
    int64_t frameCount = 0;
    int64_t currentPts = 0;
    int64_t currentDts = 0;

    qDebug() << "wrapElementaryStreamLibav: Processing packets...";

    while (av_read_frame(inFmtCtx, packet) >= 0) {
        if (packet->stream_index != videoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        // Assign monotonic PTS/DTS based on frame count
        // For ES files, libav often returns AV_NOPTS_VALUE or unreliable timestamps
        // We generate clean timestamps ourselves
        packet->pts = currentPts;
        packet->dts = currentDts;
        packet->duration = frameDuration;

        // Remap stream index to output
        packet->stream_index = 0;

        // Rescale timestamps if input has valid timebase (usually not for ES)
        // Since we're generating timestamps ourselves, just use them directly

        // Write packet
        ret = av_interleaved_write_frame(outFmtCtx, packet);
        if (ret < 0) {
            qDebug() << "wrapElementaryStreamLibav: Warning: write error at frame" << frameCount
                     << "-" << avErrorToString(ret);
        }

        // Advance timestamps
        // For B-frames: PTS may differ from DTS
        // Simple approach: assume all frames have same duration, DTS is monotonic
        currentDts += frameDuration;
        currentPts += frameDuration;
        frameCount++;

        av_packet_unref(packet);

        // Progress output every 1000 frames
        if (frameCount % 1000 == 0) {
            qDebug() << "  Processed" << frameCount << "frames...";
        }
    }

    av_packet_free(&packet);

    // Write trailer
    ret = av_write_trailer(outFmtCtx);
    if (ret < 0) {
        qDebug() << "wrapElementaryStreamLibav: Warning: trailer write error:" << avErrorToString(ret);
    }

    // Cleanup
    avformat_close_input(&inFmtCtx);
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFmtCtx->pb);
    }
    avformat_free_context(outFmtCtx);

    if (!QFile::exists(tempMkv)) {
        setError("wrapElementaryStreamLibav: Output file was not created");
        return QString();
    }

    qDebug() << "wrapElementaryStreamLibav: ES wrapped successfully";
    qDebug() << "  Frames processed:" << frameCount;
    qDebug() << "  Output:" << tempMkv;

    return tempMkv;
}

// ----------------------------------------------------------------------------
// Smart Cut for Elementary Streams
// Re-encodes only the partial GOPs at cut-in points, stream-copies the rest.
// This allows frame-accurate cutting while minimizing re-encoding.
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::smartCutElementaryStream(const QString& inputFile,
                                                const QString& audioFile,
                                                const QString& outputFile,
                                                const QList<QPair<double, double>>& cutList,
                                                double frameRate)
{
    if (mFrameIndex.isEmpty()) {
        setError("Frame index not built - call buildFrameIndex first");
        return false;
    }

    if (frameRate <= 0) frameRate = 25.0;

    qDebug() << "smartCutElementaryStream: Smart Cut via RAW decode (Z13)";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Audio:" << audioFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Frame rate:" << frameRate << "fps";

    QString tempDir = QFileInfo(inputFile).absolutePath();
    bool hasAudio = !audioFile.isEmpty() && QFile::exists(audioFile);

    // Wrap entire ES in MKV for decode and stream-copy operations
    // mkvmerge generates proper PTS/DTS timestamps for B-frames
    QString wrappedES = QString("%1/.smartcut_wrapped.mkv").arg(tempDir);
    int64_t frameDurationNs = static_cast<int64_t>(1000000000.0 / frameRate);
    QString wrapCmd = QString("mkvmerge -o \"%1\" --default-duration 0:%2ns \"%3\" 2>&1")
        .arg(wrappedES).arg(frameDurationNs).arg(inputFile);
    qDebug() << "  Wrapping ES (mkvmerge):" << wrapCmd;
    system(qPrintable(wrapCmd));

    // Get video dimensions from wrapped file
    int videoWidth = 1920;
    int videoHeight = 1080;
    QString probeCmd = QString("ffprobe -v error -select_streams v:0 "
                               "-show_entries stream=width,height -of csv=p=0 \"%1\"")
        .arg(wrappedES);
    QProcess probeProc;
    probeProc.start("bash", QStringList() << "-c" << probeCmd);
    probeProc.waitForFinished(5000);
    QString probeOut = probeProc.readAllStandardOutput().trimmed();
    QStringList dims = probeOut.split(",");
    if (dims.size() >= 2) {
        videoWidth = dims[0].toInt();
        videoHeight = dims[1].toInt();
    }
    qDebug() << "  Video dimensions:" << videoWidth << "x" << videoHeight;

    // Extract actual keyframe timestamps from wrapped MKV
    // This is needed because frame_number/fps doesn't match MKV timestamps exactly
    QMap<int, double> keyframeTimestamps;  // frame_number -> actual_timestamp
    QString kfProbeCmd = QString("ffprobe -v error -select_streams v:0 "
                                  "-show_entries packet=pts_time,flags -of csv=p=0 \"%1\"")
        .arg(wrappedES);
    QProcess kfProbe;
    kfProbe.start("bash", QStringList() << "-c" << kfProbeCmd);
    kfProbe.waitForFinished(60000);
    QString kfOutput = kfProbe.readAllStandardOutput();
    QStringList kfLines = kfOutput.split("\n");
    int frameNum = 0;
    for (const QString& line : kfLines) {
        if (line.isEmpty()) continue;
        QStringList parts = line.split(",");
        if (parts.size() >= 2 && parts[1].contains("K")) {
            double pts = parts[0].toDouble();
            keyframeTimestamps[frameNum] = pts;
        }
        frameNum++;
    }
    qDebug() << "  Found" << keyframeTimestamps.size() << "keyframes in wrapped MKV";

    QStringList segmentFiles;
    int prevSegmentEndFrame = -1;

    // libx264 parameters - match original stream parameters for SPS/PPS compatibility
    // refs=1 matches original, bf=0 to avoid DTS reordering at transitions
    QString x264Params = "-profile:v high -level:v 4.0 -refs 1 -bf 0 "
                         "-color_primaries bt709 -color_trc bt709 -colorspace bt709 "
                         "-pix_fmt yuv420p -preset fast -crf 18";

    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        double startTime = cutList[segIdx].first;
        double endTime = cutList[segIdx].second;

        // Use floor for cutOut to avoid including extra frames due to rounding
        int cutInFrame = qRound(startTime * frameRate);
        int cutOutFrame = static_cast<int>(endTime * frameRate);  // floor, not round
        cutInFrame = qBound(0, cutInFrame, mFrameIndex.size() - 1);
        cutOutFrame = qBound(0, cutOutFrame, mFrameIndex.size() - 1);

        if (cutInFrame >= cutOutFrame) continue;

        // Find keyframes
        int kBeforeIn = cutInFrame;
        while (kBeforeIn > 0 && !mFrameIndex[kBeforeIn].isKeyframe) kBeforeIn--;

        int kAfterIn = cutInFrame + 1;
        while (kAfterIn < mFrameIndex.size() && !mFrameIndex[kAfterIn].isKeyframe) kAfterIn++;
        if (kAfterIn >= mFrameIndex.size()) kAfterIn = mFrameIndex.size() - 1;

        qDebug() << "  Segment" << segIdx << ": cut-in=" << cutInFrame << ", cut-out=" << cutOutFrame;
        qDebug() << "    K_before_in=" << kBeforeIn << ", K_after_in=" << kAfterIn;

        // Check if we need to re-encode the start (cut-in is not at a keyframe)
        bool needReencodeStart = (cutInFrame != kBeforeIn);

        // Stream-copy starts at the keyframe AFTER cut-in
        int streamCopyStart = needReencodeStart ? kAfterIn : cutInFrame;

        // Handle overlap with previous segment
        if (prevSegmentEndFrame >= 0 && streamCopyStart <= prevSegmentEndFrame) {
            streamCopyStart = prevSegmentEndFrame + 1;
            qDebug() << "    Overlap avoided: stream-copy starts at" << streamCopyStart;
        }

        QString segmentFile = QString("%1/.smartcut_seg_%2.mkv").arg(tempDir).arg(segIdx);
        segmentFiles << segmentFile;

        QStringList partFiles;

        // =====================================================================
        // Part 1: Re-encode partial GOP via RAW decode
        // Strategy: Extract small segment with mkvmerge (fast), then decode to RAW
        // This avoids the slow select filter that processes all frames
        // =====================================================================
        if (needReencodeStart) {
            QString startPart = QString("%1/.smartcut_seg_%2_reencode.mkv").arg(tempDir).arg(segIdx);
            partFiles << startPart;

            int reencodeStart = cutInFrame;
            int reencodeEnd = kAfterIn - 1;  // Last frame before the next keyframe
            int numFrames = reencodeEnd - reencodeStart + 1;

            qDebug() << "    RAW Re-encode:" << reencodeStart << "->" << reencodeEnd
                     << "(" << numFrames << "frames)";

            // Step 1: Extract small segment with mkvmerge (very fast, no decoding)
            // We need from kBeforeIn (keyframe before cutIn) to the keyframe AFTER kAfterIn
            // This ensures all B-frames can be decoded (they may reference the next keyframe)
            double extractStart = kBeforeIn / frameRate;

            // Find keyframe AFTER kAfterIn to ensure all B-frames are decodable
            int kAfterAfterIn = kAfterIn + 1;
            while (kAfterAfterIn < mFrameIndex.size() && !mFrameIndex[kAfterAfterIn].isKeyframe)
                kAfterAfterIn++;
            if (kAfterAfterIn >= mFrameIndex.size())
                kAfterAfterIn = mFrameIndex.size() - 1;

            double extractEnd = (kAfterAfterIn + 1) / frameRate;  // Include the second keyframe
            qDebug() << "    Extract range: kBeforeIn=" << kBeforeIn << "kAfterIn=" << kAfterIn
                     << "kAfterAfterIn=" << kAfterAfterIn;

            QString extractTs1 = QString("%1:%2:%3.%4")
                .arg(int(extractStart / 3600), 2, 10, QChar('0'))
                .arg(int(fmod(extractStart / 60, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(extractStart, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(extractStart * 1000, 1000)), 3, 10, QChar('0'));
            QString extractTs2 = QString("%1:%2:%3.%4")
                .arg(int(extractEnd / 3600), 2, 10, QChar('0'))
                .arg(int(fmod(extractEnd / 60, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(extractEnd, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(extractEnd * 1000, 1000)), 3, 10, QChar('0'));

            QString smallClip = QString("%1/.smartcut_seg_%2_small.mkv").arg(tempDir).arg(segIdx);
            QString extractCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 -A \"%4\" 2>&1")
                .arg(smallClip).arg(extractTs1).arg(extractTs2).arg(wrappedES);
            qDebug() << "    CMD (extract small):" << extractCmd;
            system(qPrintable(extractCmd));

            // Step 2: Decode the small clip to RAW
            // Use TIME-based selection (more robust than frame numbers)
            // The clip PTS is normalized to start near 0, so calculate relative times
            QString rawFile = QString("%1/.smartcut_seg_%2_raw.yuv").arg(tempDir).arg(segIdx);

            // Target times relative to clip start (kBeforeIn)
            double selectStart = (reencodeStart - kBeforeIn) / frameRate;
            double selectEnd = (reencodeEnd - kBeforeIn + 1) / frameRate;  // +1 for inclusive end

            QString decodeCmd = QString(
                "ffmpeg -y -i \"%1\" "
                "-vf \"select='gte(t\\,%2)*lt(t\\,%3)',setpts=N/FR/TB\" "
                "-vsync 0 -f rawvideo -pix_fmt yuv420p \"%4\" 2>&1")
                .arg(smallClip)
                .arg(selectStart, 0, 'f', 6)
                .arg(selectEnd, 0, 'f', 6)
                .arg(rawFile);
            qDebug() << "    CMD (decode to RAW):" << decodeCmd
                     << "selectTime:" << selectStart << "-" << selectEnd;
            system(qPrintable(decodeCmd));

            // Step 3: Encode RAW to H.264 with clean timestamps
            QString encodeCmd;
            if (hasAudio) {
                // Calculate audio segment times
                double audioStart = reencodeStart / frameRate;
                double audioDur = numFrames / frameRate;

                encodeCmd = QString(
                    "ffmpeg -y -f rawvideo -pix_fmt yuv420p -s %1x%2 -r %3 -i \"%4\" "
                    "-ss %5 -t %6 -i \"%7\" "
                    "-map 0:v -map 1:a -c:v libx264 %8 -c:a copy \"%9\" 2>&1")
                    .arg(videoWidth).arg(videoHeight)
                    .arg(frameRate, 0, 'f', 6)
                    .arg(rawFile)
                    .arg(audioStart, 0, 'f', 6).arg(audioDur, 0, 'f', 6)
                    .arg(audioFile)
                    .arg(x264Params)
                    .arg(startPart);
            } else {
                encodeCmd = QString(
                    "ffmpeg -y -f rawvideo -pix_fmt yuv420p -s %1x%2 -r %3 -i \"%4\" "
                    "-c:v libx264 %5 \"%6\" 2>&1")
                    .arg(videoWidth).arg(videoHeight)
                    .arg(frameRate, 0, 'f', 6)
                    .arg(rawFile)
                    .arg(x264Params)
                    .arg(startPart);
            }
            qDebug() << "    CMD (encode RAW):" << encodeCmd;
            system(qPrintable(encodeCmd));

            // Cleanup temp files
            QFile::remove(rawFile);
            QFile::remove(smallClip);
        }

        // =====================================================================
        // Part 2: Stream-copy from keyframe (kAfterIn) to cutOutFrame
        // Use mkvmerge --split for frame-accurate extraction (no B-frame issues)
        // =====================================================================
        if (streamCopyStart <= cutOutFrame) {
            QString copyPart = QString("%1/.smartcut_seg_%2_copy.mkv").arg(tempDir).arg(segIdx);
            partFiles << copyPart;

            // Calculate timestamps for mkvmerge split
            // mkvmerge uses milliseconds, and its timestamps align with frame display order
            double startTimeSec = streamCopyStart / frameRate;
            double endTimeSec = (cutOutFrame + 1) / frameRate;  // +1 because end is exclusive
            int copyFrameCount = cutOutFrame - streamCopyStart + 1;

            qDebug() << "    Stream-copy (mkvmerge):" << streamCopyStart << "->" << cutOutFrame
                     << "(" << copyFrameCount << "frames)"
                     << "time:" << startTimeSec << "-" << endTimeSec;

            // Use mkvmerge --split parts: for frame-accurate cutting
            // Format: --split parts:START-END (timestamps in HH:MM:SS.nnn format)
            QString startTs = QString("%1:%2:%3.%4")
                .arg(int(startTimeSec / 3600), 2, 10, QChar('0'))
                .arg(int(fmod(startTimeSec / 60, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(startTimeSec, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(startTimeSec * 1000, 1000)), 3, 10, QChar('0'));
            QString endTs = QString("%1:%2:%3.%4")
                .arg(int(endTimeSec / 3600), 2, 10, QChar('0'))
                .arg(int(fmod(endTimeSec / 60, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(endTimeSec, 60)), 2, 10, QChar('0'))
                .arg(int(fmod(endTimeSec * 1000, 1000)), 3, 10, QChar('0'));

            // Use mkvmerge --split for stream-copy extraction
            // mkvmerge normalizes timestamps and preserves B-frame PTS/DTS correctly
            if (hasAudio) {
                // Extract video segment with mkvmerge
                QString videoOnly = QString("%1/.smartcut_seg_%2_video.mkv").arg(tempDir).arg(segIdx);
                QString videoCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 -A \"%4\" 2>&1")
                    .arg(videoOnly).arg(startTs).arg(endTs).arg(wrappedES);
                qDebug() << "    CMD (video mkvmerge):" << videoCmd;
                system(qPrintable(videoCmd));

                // Extract audio segment with mkvmerge
                QString audioOnly = QString("%1/.smartcut_seg_%2_audio.mkv").arg(tempDir).arg(segIdx);
                QString audioCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 \"%4\" 2>&1")
                    .arg(audioOnly).arg(startTs).arg(endTs).arg(audioFile);
                qDebug() << "    CMD (audio mkvmerge):" << audioCmd;
                system(qPrintable(audioCmd));

                // Mux video + audio
                QString muxCmd = QString("mkvmerge -o \"%1\" \"%2\" \"%3\" 2>&1")
                    .arg(copyPart).arg(videoOnly).arg(audioOnly);
                qDebug() << "    CMD (mux):" << muxCmd;
                system(qPrintable(muxCmd));

                QFile::remove(videoOnly);
                QFile::remove(audioOnly);
            } else {
                // Video only - use mkvmerge split
                QString videoCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 \"%4\" 2>&1")
                    .arg(copyPart).arg(startTs).arg(endTs).arg(wrappedES);
                qDebug() << "    CMD (video mkvmerge):" << videoCmd;
                system(qPrintable(videoCmd));
            }
        }

        // =====================================================================
        // Concatenate re-encoded + stream-copied parts
        // Use FFmpeg concat (handles SPS/PPS without decoder artifacts)
        // =====================================================================
        if (partFiles.size() == 1) {
            QFile::rename(partFiles[0], segmentFile);
        } else if (partFiles.size() > 1) {
            // Create concat list
            QString concatList = QString("%1/.smartcut_seg_%2_concat.txt").arg(tempDir).arg(segIdx);
            QFile listFile(concatList);
            if (listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&listFile);
                for (const QString& part : partFiles) {
                    out << "file '" << part << "'\n";
                }
                listFile.close();
            }

            QString concatCmd = QString(
                "ffmpeg -y -f concat -safe 0 -i \"%1\" -c copy \"%2\" 2>&1")
                .arg(concatList).arg(segmentFile);
            qDebug() << "    Concat (ffmpeg):" << concatCmd;
            system(qPrintable(concatCmd));

            // Cleanup
            QFile::remove(concatList);
            for (const QString& part : partFiles) QFile::remove(part);
        }

        prevSegmentEndFrame = cutOutFrame;
    }

    // =========================================================================
    // Final concatenation of all segments
    // Use FFmpeg concat (handles SPS/PPS) then mkvmerge remux (fixes timestamps)
    // =========================================================================
    if (segmentFiles.size() == 1) {
        QFile::rename(segmentFiles[0], outputFile);
    } else if (segmentFiles.size() > 1) {
        // Step 1: FFmpeg concat (handles SPS/PPS differences without decoder artifacts)
        QString concatList = QString("%1/.smartcut_final_concat.txt").arg(tempDir);
        QString tempOutput = QString("%1/.smartcut_ffmpeg_concat.mkv").arg(tempDir);
        QFile listFile(concatList);
        if (listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&listFile);
            for (const QString& seg : segmentFiles) {
                out << "file '" << seg << "'\n";
            }
            listFile.close();
        }

        QString ffmpegCmd = QString(
            "ffmpeg -y -f concat -safe 0 -i \"%1\" -c copy \"%2\" 2>&1")
            .arg(concatList).arg(tempOutput);
        qDebug() << "  Final concat step 1 (ffmpeg):" << ffmpegCmd;
        system(qPrintable(ffmpegCmd));

        // Step 2: mkvmerge remux to fix timestamps
        QString mkvmergeCmd = QString(
            "mkvmerge -o \"%1\" --fix-bitstream-timing-information 0:1 \"%2\" 2>&1")
            .arg(outputFile).arg(tempOutput);
        qDebug() << "  Final concat step 2 (mkvmerge fix):" << mkvmergeCmd;
        system(qPrintable(mkvmergeCmd));

        QFile::remove(concatList);
        QFile::remove(tempOutput);
        for (const QString& seg : segmentFiles) QFile::remove(seg);
    }

    // Cleanup
    QFile::remove(wrappedES);

    qDebug() << "smartCutElementaryStream: Complete";
    return QFile::exists(outputFile);
}

// ----------------------------------------------------------------------------
// Smart Cut for Elementary Streams V2 - using native NAL parser
// This is the preferred method for ES Smart Cut - no external tool dependencies.
// Re-encodes only partial GOPs at cut-in points, stream-copies the rest.
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::smartCutElementaryStreamV2(const QString& inputFile,
                                                  const QString& audioFile,
                                                  const QString& outputFile,
                                                  const QList<QPair<double, double>>& cutList,
                                                  double frameRate)
{
    Q_UNUSED(audioFile);  // Audio handled separately

    qDebug() << "smartCutElementaryStreamV2: Native ES Smart Cut";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutList.size();

    // Create the ES Smart Cut engine
    TTESSmartCut smartCut;

    // Initialize with ES file
    if (!smartCut.initialize(inputFile, frameRate)) {
        setError(QString("ES Smart Cut init failed: %1").arg(smartCut.lastError()));
        return false;
    }

    qDebug() << "  Codec:" << smartCut.codecType();
    qDebug() << "  Frames:" << smartCut.frameCount();
    qDebug() << "  GOPs:" << smartCut.gopCount();
    qDebug() << "  Frame rate:" << smartCut.frameRate() << "fps";

    // Determine output format
    QString outExt = QFileInfo(outputFile).suffix().toLower();
    bool wrapToContainer = (outExt == "mkv" || outExt == "ts" || outExt == "mp4");

    QString esOutput;
    if (wrapToContainer) {
        // Cut to temp ES first, then mux to container
        esOutput = QFileInfo(inputFile).absolutePath() + "/." +
                   QFileInfo(inputFile).completeBaseName() + "_smartcut." +
                   QFileInfo(inputFile).suffix();
    } else {
        esOutput = outputFile;
    }

    // Perform the smart cut
    if (!smartCut.smartCut(esOutput, cutList)) {
        setError(QString("ES Smart Cut failed: %1").arg(smartCut.lastError()));
        return false;
    }

    qDebug() << "  Stream-copied frames:" << smartCut.framesStreamCopied();
    qDebug() << "  Re-encoded frames:" << smartCut.framesReencoded();
    qDebug() << "  Output size:" << smartCut.bytesWritten() << "bytes";

    // Wrap to container if needed
    if (wrapToContainer) {
        qDebug() << "  Wrapping to container:" << outputFile;

        // Get frame rate for mkvmerge
        double fps = smartCut.frameRate();
        int64_t frameDurationNs = static_cast<int64_t>(1000000000.0 / fps);

        // Use mkvmerge to wrap the cut ES
        QStringList args;
        args << "-o" << outputFile
             << "--default-duration" << QString("0:%1ns").arg(frameDurationNs)
             << esOutput;

        QProcess proc;
        proc.start("mkvmerge", args);

        if (!proc.waitForStarted(5000)) {
            setError("mkvmerge failed to start");
            QFile::remove(esOutput);
            return false;
        }

        if (!proc.waitForFinished(300000)) {
            setError("mkvmerge timed out");
            proc.kill();
            QFile::remove(esOutput);
            return false;
        }

        if (proc.exitCode() != 0 && proc.exitCode() != 1) {
            setError(QString("mkvmerge failed: %1")
                .arg(QString::fromUtf8(proc.readAllStandardError())));
            QFile::remove(esOutput);
            return false;
        }

        // Cleanup temp ES
        QFile::remove(esOutput);
    }

    qDebug() << "smartCutElementaryStreamV2: Complete";
    return QFile::exists(outputFile) || !wrapToContainer;
}

// ----------------------------------------------------------------------------
// Cut elementary stream at byte level (legacy - starts at keyframes only)
// This copies NAL units between keyframes, avoiding timestamp issues entirely.
// Times are converted to frame indices using the frame index.
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::cutElementaryStream(const QString& inputFile,
                                           const QString& outputFile,
                                           const QList<QPair<double, double>>& cutList)
{
    if (mFrameIndex.isEmpty()) {
        setError("Frame index not built - call buildFrameIndex first");
        return false;
    }

    // Get frame rate for time-to-frame conversion
    double frameRate = 25.0;
    QString infoFile = TTESInfo::findInfoFile(inputFile);
    if (!infoFile.isEmpty()) {
        TTESInfo info(infoFile);
        if (info.isLoaded() && info.frameRate() > 0) {
            frameRate = info.frameRate();
        }
    }

    qDebug() << "cutElementaryStream: Byte-level ES cutting";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Frame rate:" << frameRate << "fps";
    qDebug() << "  Total frames:" << mFrameIndex.size();

    // Open input file for reading
    QFile inFile(inputFile);
    if (!inFile.open(QIODevice::ReadOnly)) {
        setError(QString("Cannot open input file: %1").arg(inputFile));
        return false;
    }

    // Open output file for writing
    QFile outFile(outputFile);
    if (!outFile.open(QIODevice::WriteOnly)) {
        inFile.close();
        setError(QString("Cannot create output file: %1").arg(outputFile));
        return false;
    }

    int64_t totalBytesWritten = 0;
    int prevSegmentEndFrame = -1;  // Track previous segment end to avoid overlap

    // Process each segment to keep
    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        double startTime = cutList[segIdx].first;
        double endTime = cutList[segIdx].second;

        // Convert times to frame indices
        int startFrame = qRound(startTime * frameRate);
        int endFrame = qRound(endTime * frameRate);

        // Clamp to valid range
        startFrame = qBound(0, startFrame, mFrameIndex.size() - 1);
        endFrame = qBound(0, endFrame, mFrameIndex.size() - 1);

        if (startFrame >= endFrame) {
            qDebug() << "  Segment" << segIdx << "is empty, skipping";
            continue;
        }

        // Find keyframe at or before start (for clean cut-in)
        int keyframeStart = startFrame;
        while (keyframeStart > 0 && !mFrameIndex[keyframeStart].isKeyframe) {
            keyframeStart--;
        }

        // Avoid overlap: if keyframe is before previous segment end, start after previous end
        int actualStart = keyframeStart;
        if (prevSegmentEndFrame >= 0 && keyframeStart <= prevSegmentEndFrame) {
            actualStart = prevSegmentEndFrame + 1;
            qDebug() << "  Segment" << segIdx << ": avoiding overlap, adjusted start from"
                     << keyframeStart << "to" << actualStart;
        }

        // Get byte range to copy
        int64_t startOffset = mFrameIndex[actualStart].fileOffset;
        int64_t endOffset;

        if (endFrame < mFrameIndex.size() - 1) {
            endOffset = mFrameIndex[endFrame + 1].fileOffset;
        } else {
            // Last frame - read to end of file
            endOffset = inFile.size();
        }

        int64_t bytesToCopy = endOffset - startOffset;

        qDebug() << "  Segment" << segIdx << ":"
                 << "frames" << actualStart << "->" << endFrame
                 << "(" << (endFrame - actualStart + 1) << "frames)"
                 << ", bytes" << startOffset << "->" << endOffset
                 << "(" << bytesToCopy << "bytes)";

        // Seek and copy
        if (!inFile.seek(startOffset)) {
            setError(QString("Cannot seek to position %1").arg(startOffset));
            inFile.close();
            outFile.close();
            return false;
        }

        // Copy in chunks
        const int64_t chunkSize = 1024 * 1024; // 1 MB chunks
        int64_t remaining = bytesToCopy;

        while (remaining > 0) {
            int64_t toRead = qMin(remaining, chunkSize);
            QByteArray chunk = inFile.read(toRead);

            if (chunk.isEmpty()) {
                setError("Read error during copying");
                inFile.close();
                outFile.close();
                return false;
            }

            outFile.write(chunk);
            remaining -= chunk.size();
            totalBytesWritten += chunk.size();
        }

        // Track this segment's end for overlap detection
        prevSegmentEndFrame = endFrame;
    }

    inFile.close();
    outFile.close();

    qDebug() << "cutElementaryStream: Complete";
    qDebug() << "  Bytes written:" << totalBytesWritten;

    return true;
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

    qDebug() << "cutAudioStream: Sample-precise audio cutting via atrim filter";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutList.size();

    // Detect audio codec from file extension for re-encoding
    QString suffix = QFileInfo(inputFile).suffix().toLower();
    QString audioCodec;
    QStringList codecArgs;
    if (suffix == "ac3") {
        audioCodec = "ac3";
        codecArgs << "-c:a" << "ac3" << "-b:a" << "384k";
    } else if (suffix == "mp3") {
        audioCodec = "libmp3lame";
        codecArgs << "-c:a" << "libmp3lame" << "-b:a" << "256k";
    } else if (suffix == "aac") {
        audioCodec = "aac";
        codecArgs << "-c:a" << "aac" << "-b:a" << "256k";
    } else {
        // mp2 and fallback
        audioCodec = "mp2";
        codecArgs << "-c:a" << "mp2" << "-b:a" << "384k";
    }

    // Build atrim filter graph for sample-precise cutting
    // Each segment: atrim=start:end, reset timestamps
    // Then concatenate all segments
    QString filterGraph;
    if (cutList.size() == 1) {
        filterGraph = QString("[0:a]atrim=%1:%2,asetpts=PTS-STARTPTS[out]")
            .arg(cutList[0].first, 0, 'f', 6)
            .arg(cutList[0].second, 0, 'f', 6);
    } else {
        QStringList filterParts;
        QStringList streamLabels;
        for (int i = 0; i < cutList.size(); ++i) {
            QString label = QString("a%1").arg(i);
            filterParts << QString("[0:a]atrim=%1:%2,asetpts=PTS-STARTPTS[%3]")
                .arg(cutList[i].first, 0, 'f', 6)
                .arg(cutList[i].second, 0, 'f', 6)
                .arg(label);
            streamLabels << QString("[%1]").arg(label);
            qDebug() << "  Segment" << i << ":" << cutList[i].first << "->" << cutList[i].second;
        }
        filterGraph = filterParts.join(";") + ";" +
            streamLabels.join("") +
            QString("concat=n=%1:v=0:a=1[out]").arg(cutList.size());
    }

    QStringList args;
    args << "-y"
         << "-i" << inputFile
         << "-filter_complex" << filterGraph
         << "-map" << "[out]";
    args << codecArgs;
    args << outputFile;

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

    if (proc.exitCode() != 0) {
        setError(QString("FFmpeg failed: %1").arg(QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }

    qDebug() << "cutAudioStream: Complete";
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
// Complete ES cutting workflow: video + audio + subtitles + mux
// This is the main entry point for cutting demuxed ES files
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::cutAndMuxElementaryStreams(const QString& videoES,
                                                  const QString& audioES,
                                                  const QString& outputFile,
                                                  const QList<QPair<double, double>>& cutList,
                                                  double frameRate)
{
    qDebug() << "cutAndMuxElementaryStreams: Complete ES workflow";
    qDebug() << "  Video ES:" << videoES;
    qDebug() << "  Audio ES:" << audioES;
    qDebug() << "  Output:" << outputFile;

    // Get frame rate from .info file if not provided
    QString infoFile = TTESInfo::findInfoFile(videoES);
    if (!infoFile.isEmpty()) {
        TTESInfo esInfo(infoFile);
        if (esInfo.isLoaded() && frameRate <= 0 && esInfo.frameRate() > 0) {
            frameRate = esInfo.frameRate();
        }
    }
    if (frameRate <= 0) frameRate = 25.0;

    qDebug() << "  Frame rate:" << frameRate << "fps";

    // Create temp file paths
    QFileInfo videoInfo(videoES);
    QString tempDir = videoInfo.absolutePath();
    QString cutSrtFile;

    // Step 1: Open and index the video ES
    if (!openFile(videoES)) {
        setError(QString("Cannot open video ES: %1").arg(lastError()));
        return false;
    }

    if (!buildFrameIndex()) {
        closeFile();
        setError(QString("Cannot build frame index: %1").arg(lastError()));
        return false;
    }

    // Calculate adjusted cut times based on actual video frame positions
    // (keyframe alignment and overlap avoidance)
    QList<QPair<double, double>> adjustedCutList;
    int prevSegmentEndFrame = -1;

    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        int startFrame = qRound(cutList[segIdx].first * frameRate);
        int endFrame = qRound(cutList[segIdx].second * frameRate);
        startFrame = qBound(0, startFrame, mFrameIndex.size() - 1);
        endFrame = qBound(0, endFrame, mFrameIndex.size() - 1);

        // Find keyframe at or before start
        int keyframeStart = startFrame;
        while (keyframeStart > 0 && !mFrameIndex[keyframeStart].isKeyframe) {
            keyframeStart--;
        }

        // Avoid overlap with previous segment
        int actualStart = keyframeStart;
        if (prevSegmentEndFrame >= 0 && keyframeStart <= prevSegmentEndFrame) {
            actualStart = prevSegmentEndFrame + 1;
        }

        // Convert back to times
        double adjustedStartTime = actualStart / frameRate;
        double adjustedEndTime = (endFrame + 1) / frameRate;  // +1 because end is inclusive

        adjustedCutList.append(qMakePair(adjustedStartTime, adjustedEndTime));
        qDebug() << "  Adjusted cut" << segIdx << ":"
                 << cutList[segIdx].first << "->" << adjustedStartTime
                 << "," << cutList[segIdx].second << "->" << adjustedEndTime;

        prevSegmentEndFrame = endFrame;
    }

    // Step 2: Smart Cut video+audio (re-encode only partial GOPs, stream-copy rest)
    QString tempOutput = tempDir + "/." + videoInfo.completeBaseName() + "_smartcut.mkv";
    qDebug() << "Step 1: Smart Cut (video + audio)...";
    if (!smartCutElementaryStream(videoES, audioES, tempOutput, cutList, frameRate)) {
        closeFile();
        setError(QString("Smart Cut failed: %1").arg(lastError()));
        return false;
    }
    closeFile();

    // Step 3: Cut SRT subtitles if present
    // Look for SRT file matching video name
    QString baseName = videoInfo.completeBaseName();
    if (baseName.endsWith("_video")) {
        baseName = baseName.left(baseName.length() - 6);
    }
    QString srtFile = tempDir + "/" + baseName + ".srt";

    bool hasSrt = QFile::exists(srtFile);
    if (hasSrt) {
        cutSrtFile = tempDir + "/." + baseName + "_cut.srt";

        qDebug() << "Step 2: Cutting SRT subtitles with adjusted times...";
        if (!cutSrtSubtitle(srtFile, cutSrtFile, adjustedCutList)) {
            qDebug() << "  Warning: SRT cutting failed, continuing without subtitles";
            hasSrt = false;
        }
    }

    // Step 4: Final output
    // Smart Cut produces MKV with video+audio. If SRT exists, mux it in; otherwise just rename.
    if (hasSrt) {
        qDebug() << "Step 3: Muxing SRT into final output...";

        QStringList muxArgs;
        muxArgs << "-o" << outputFile << tempOutput << cutSrtFile;
        qDebug() << "  mkvmerge" << muxArgs.join(" ");

        QProcess muxProc;
        muxProc.start("mkvmerge", muxArgs);

        if (!muxProc.waitForStarted(5000)) {
            QFile::remove(tempOutput);
            QFile::remove(cutSrtFile);
            setError("mkvmerge failed to start");
            return false;
        }

        if (!muxProc.waitForFinished(300000)) {
            QFile::remove(tempOutput);
            QFile::remove(cutSrtFile);
            setError("mkvmerge timed out");
            muxProc.kill();
            return false;
        }

        // Cleanup temp files
        QFile::remove(tempOutput);
        QFile::remove(cutSrtFile);

        if (muxProc.exitCode() != 0 && muxProc.exitCode() != 1) {
            setError(QString("mkvmerge failed (exit %1): %2")
                .arg(muxProc.exitCode())
                .arg(QString::fromUtf8(muxProc.readAllStandardError())));
            return false;
        }
    } else {
        // No SRT - just rename the Smart Cut output to final output
        qDebug() << "Step 3: Renaming Smart Cut output to final output...";
        if (QFile::exists(outputFile)) {
            QFile::remove(outputFile);
        }
        if (!QFile::rename(tempOutput, outputFile)) {
            setError("Failed to rename Smart Cut output to final output");
            return false;
        }
    }

    if (!QFile::exists(outputFile)) {
        setError("Output file was not created");
        return false;
    }

    qDebug() << "cutAndMuxElementaryStreams: Complete!";
    qDebug() << "  Output:" << outputFile;

    return true;
}

// ----------------------------------------------------------------------------
// Smart cut using avcut approach
// Direct writing to single output, no global headers, GOP-based processing
// For ES input: uses byte-level cutting (no timestamp issues)
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::smartCut(const QString& outputFile,
                                const QList<QPair<double, double>>& cutList)
{
    if (!mFormatCtx) {
        setError("No input file open");
        return false;
    }

    if (cutList.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    if (mVideoStreamIndex < 0) {
        setError("No video stream found");
        return false;
    }

    qDebug() << "smartCut: Starting avcut-style processing";
    qDebug() << "  Input:" << QString::fromUtf8(mFormatCtx->url);
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Keep segments:" << cutList.size();

    // Check if input is an elementary stream
    // For ES files, use byte-level cutting (no timestamp issues!)
    bool inputIsES = (detectContainer() == CONTAINER_ELEMENTARY);

    if (inputIsES) {
        qDebug() << "smartCut: Input is elementary stream - using byte-level cutting";
        qDebug() << "  (This avoids timestamp discontinuity issues)";

        // Build frame index first (required for byte-level cutting)
        if (mFrameIndex.isEmpty()) {
            qDebug() << "smartCut: Building frame index for ES cutting...";
            if (!buildFrameIndex()) {
                setError("Failed to build frame index for ES cutting");
                return false;
            }
            qDebug() << "smartCut: Frame index built:" << mFrameIndex.size() << "frames";
        }

        QString esFile = QString::fromUtf8(mFormatCtx->url);
        QString esOutput = outputFile;

        // If output is MKV/TS/MP4, cut to temp ES first, then mux
        QString outExt = QFileInfo(outputFile).suffix().toLower();
        bool outputIsContainer = (outExt == "mkv" || outExt == "ts" || outExt == "mp4" || outExt == "m2ts");

        if (outputIsContainer) {
            // Output to temp ES file, then mux to final container
            esOutput = QFileInfo(esFile).absolutePath() + "/." +
                       QFileInfo(esFile).completeBaseName() + "_cut." +
                       QFileInfo(esFile).suffix();
        }

        // Perform byte-level ES cutting
        bool result = cutElementaryStream(esFile, esOutput, cutList);

        if (!result) {
            return false;
        }

        // If output should be container, mux the cut ES
        if (outputIsContainer) {
            // Get frame rate from .info file
            double frameRate = -1;
            QString infoFile = TTESInfo::findInfoFile(esFile);
            if (!infoFile.isEmpty()) {
                TTESInfo info(infoFile);
                if (info.isLoaded()) {
                    frameRate = info.frameRate();
                }
            }
            if (frameRate <= 0) frameRate = 25.0;

            // Mux with mkvmerge
            QString tempMkv = wrapElementaryStream(esOutput, frameRate);
            if (!tempMkv.isEmpty()) {
                // Rename to final output
                QFile::remove(outputFile);
                QFile::rename(tempMkv, outputFile);
            }

            // Cleanup temp ES
            QFile::remove(esOutput);
        }

        return true;
    }

    int ret;
    AVFormatContext* outFmtCtx = nullptr;
    AVCodecContext* encCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    AVBSFContext* bsfDumpExtra = nullptr;
    AVBSFContext* bsfToAnnexB = nullptr;

    // Input stream info
    AVStream* inVideoStream = mFormatCtx->streams[mVideoStreamIndex];
    AVStream* inAudioStream = (mAudioStreamIndex >= 0) ? mFormatCtx->streams[mAudioStreamIndex] : nullptr;

    // Calculate stream start time offset (video streams may not start at 0)
    double streamStartTime = 0.0;
    if (inVideoStream->start_time != AV_NOPTS_VALUE) {
        streamStartTime = inVideoStream->start_time * av_q2d(inVideoStream->time_base);
    } else if (mFormatCtx->start_time != AV_NOPTS_VALUE) {
        streamStartTime = mFormatCtx->start_time / (double)AV_TIME_BASE;
    }
    qDebug() << "  Stream start time offset:" << streamStartTime << "seconds";

    // === Open output file ===
    ret = avformat_alloc_output_context2(&outFmtCtx, nullptr, "matroska", outputFile.toUtf8().constData());
    if (ret < 0 || !outFmtCtx) {
        setError(QString("Could not create output context: %1").arg(avErrorToString(ret)));
        return false;
    }

    // === Create output video stream ===
    AVStream* outVideoStream = avformat_new_stream(outFmtCtx, nullptr);
    if (!outVideoStream) {
        setError("Could not create output video stream");
        avformat_free_context(outFmtCtx);
        return false;
    }

    // Copy codec parameters from input
    ret = avcodec_parameters_copy(outVideoStream->codecpar, inVideoStream->codecpar);
    if (ret < 0) {
        setError(QString("Could not copy codec parameters: %1").arg(avErrorToString(ret)));
        avformat_free_context(outFmtCtx);
        return false;
    }
    outVideoStream->time_base = inVideoStream->time_base;
    outVideoStream->codecpar->codec_tag = 0;  // Let muxer choose

    // === Create output audio stream if present ===
    AVStream* outAudioStream = nullptr;
    if (inAudioStream) {
        outAudioStream = avformat_new_stream(outFmtCtx, nullptr);
        if (outAudioStream) {
            avcodec_parameters_copy(outAudioStream->codecpar, inAudioStream->codecpar);
            outAudioStream->time_base = inAudioStream->time_base;
            outAudioStream->codecpar->codec_tag = 0;
        }
    }

    // === Open decoder for video ===
    const AVCodec* decoder = avcodec_find_decoder(inVideoStream->codecpar->codec_id);
    if (!decoder) {
        setError("Could not find video decoder");
        avformat_free_context(outFmtCtx);
        return false;
    }

    decCtx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decCtx, inVideoStream->codecpar);
    decCtx->framerate = av_guess_frame_rate(mFormatCtx, inVideoStream, nullptr);
    decCtx->time_base = av_inv_q(decCtx->framerate);

    ret = avcodec_open2(decCtx, decoder, nullptr);
    if (ret < 0) {
        setError(QString("Could not open decoder: %1").arg(avErrorToString(ret)));
        avcodec_free_context(&decCtx);
        avformat_free_context(outFmtCtx);
        return false;
    }

    // === Setup encoder (will be opened when needed) ===
    const AVCodec* encoder = nullptr;
    if (inVideoStream->codecpar->codec_id == AV_CODEC_ID_H264) {
        encoder = avcodec_find_encoder_by_name("libx264");
    } else if (inVideoStream->codecpar->codec_id == AV_CODEC_ID_HEVC) {
        encoder = avcodec_find_encoder_by_name("libx265");
    } else {
        encoder = avcodec_find_encoder(inVideoStream->codecpar->codec_id);
    }

    if (!encoder) {
        setError("Could not find video encoder");
        avcodec_free_context(&decCtx);
        avformat_free_context(outFmtCtx);
        return false;
    }

    // === Setup bitstream filters ===
    // dump_extra: Add SPS/PPS to keyframes (for encoded output)
    const AVBitStreamFilter* bsfDumpExtraFilter = av_bsf_get_by_name("dump_extra");
    if (bsfDumpExtraFilter) {
        av_bsf_alloc(bsfDumpExtraFilter, &bsfDumpExtra);
        avcodec_parameters_copy(bsfDumpExtra->par_in, inVideoStream->codecpar);
        av_bsf_init(bsfDumpExtra);
    }

    // h264_mp4toannexb: Convert AVCC to Annex B if needed
    const AVBitStreamFilter* bsfAnnexBFilter = av_bsf_get_by_name("h264_mp4toannexb");
    if (bsfAnnexBFilter && inVideoStream->codecpar->codec_id == AV_CODEC_ID_H264) {
        av_bsf_alloc(bsfAnnexBFilter, &bsfToAnnexB);
        avcodec_parameters_copy(bsfToAnnexB->par_in, inVideoStream->codecpar);
        av_bsf_init(bsfToAnnexB);
    }

    // === Open output file ===
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            setError(QString("Could not open output file: %1").arg(avErrorToString(ret)));
            avcodec_free_context(&decCtx);
            if (bsfDumpExtra) av_bsf_free(&bsfDumpExtra);
            if (bsfToAnnexB) av_bsf_free(&bsfToAnnexB);
            avformat_free_context(outFmtCtx);
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(outFmtCtx, nullptr);
    if (ret < 0) {
        setError(QString("Could not write header: %1").arg(avErrorToString(ret)));
        avcodec_free_context(&decCtx);
        if (bsfDumpExtra) av_bsf_free(&bsfDumpExtra);
        if (bsfToAnnexB) av_bsf_free(&bsfToAnnexB);
        avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        return false;
    }

    // === Process packets ===
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // GOP buffer
    QList<AVPacket*> gopPackets;
    QList<AVFrame*> gopFrames;

    // Timing state
    int64_t videoDtsOffset = 0;
    int64_t audioDtsOffset = 0;
    int64_t lastVideoDts = 0;
    int64_t lastAudioDts = 0;
    int64_t droppedVideoDuration = 0;
    int64_t droppedAudioDuration = 0;
    bool encoderOpen = false;

    // Calculate frame duration in stream timebase (for encoded packets that may have duration=0)
    // Frame duration = timebase * fps = 1/90000 * 50 = 1800 ticks for 50fps @ 90kHz
    int64_t frameDurationInStreamTB = av_rescale_q(1, av_inv_q(inVideoStream->avg_frame_rate), inVideoStream->time_base);
    qDebug() << "  Frame duration in stream timebase:" << frameDurationInStreamTB;

    // Counters (declared here so lambdas can capture them)
    int packetsWritten = 0;
    int framesEncoded = 0;

    // Lambda to open encoder
    auto openEncoder = [&]() -> bool {
        if (encoderOpen && encCtx) {
            avcodec_free_context(&encCtx);
        }

        encCtx = avcodec_alloc_context3(encoder);
        if (!encCtx) return false;

        encCtx->width = decCtx->width;
        encCtx->height = decCtx->height;
        encCtx->pix_fmt = decCtx->pix_fmt;
        encCtx->time_base = decCtx->time_base;
        encCtx->framerate = decCtx->framerate;
        encCtx->sample_aspect_ratio = decCtx->sample_aspect_ratio;
        encCtx->color_primaries = decCtx->color_primaries;
        encCtx->color_trc = decCtx->color_trc;
        encCtx->colorspace = decCtx->colorspace;
        encCtx->color_range = decCtx->color_range;
        encCtx->profile = decCtx->profile;
        encCtx->level = decCtx->level;

        // Quality settings
        encCtx->qmin = 16;
        encCtx->qmax = 26;
        encCtx->max_qdiff = 4;

        // IMPORTANT: Disable B-frames for encoding to avoid PTS/DTS complexity
        // This matches avcut's approach - re-encoded sections use I/P only
        // This makes DTS = PTS for all encoded packets, simplifying timestamp handling
        encCtx->max_b_frames = 0;

        encCtx->thread_count = 1;
        encCtx->codec_tag = 0;

        // CRITICAL: Do NOT use global header - we need SPS/PPS in-stream
        // This is the key avcut insight!
        // enc_cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;  // DON'T DO THIS

        int ret = avcodec_open2(encCtx, encoder, nullptr);
        if (ret < 0) {
            qDebug() << "Failed to open encoder:" << avErrorToString(ret);
            return false;
        }

        encoderOpen = true;
        return true;
    };

    // Lambda to close encoder (flush and close)
    auto closeEncoder = [&]() {
        if (!encoderOpen || !encCtx) return;

        // Flush encoder
        qDebug() << "    Flushing encoder...";
        avcodec_send_frame(encCtx, nullptr);
        AVPacket* encPkt = av_packet_alloc();
        int flushedPackets = 0;
        while (avcodec_receive_packet(encCtx, encPkt) == 0) {
            encPkt->stream_index = 0;
            // Without B-frames: DTS = PTS (decode order = presentation order)
            int64_t dtsInEncTimebase = av_rescale_q(lastVideoDts, inVideoStream->time_base, encCtx->time_base);
            encPkt->dts = dtsInEncTimebase;
            encPkt->pts = dtsInEncTimebase;  // Same as DTS since no B-frames
            // Increment lastVideoDts by one frame duration
            lastVideoDts += frameDurationInStreamTB;
            av_packet_rescale_ts(encPkt, encCtx->time_base, outVideoStream->time_base);
            av_interleaved_write_frame(outFmtCtx, encPkt);
            av_packet_unref(encPkt);
            packetsWritten++;
            framesEncoded++;
            flushedPackets++;
        }
        av_packet_free(&encPkt);
        qDebug() << "    Flushed" << flushedPackets << "encoded packets";

        avcodec_free_context(&encCtx);
        encoderOpen = false;
    };

    qDebug() << "smartCut: Starting packet processing";
    for (int i = 0; i < cutList.size(); i++) {
        qDebug() << "  Keep segment" << i << ":" << cutList[i].first << "-" << cutList[i].second << "seconds";
    }

    // Seek to beginning
    av_seek_frame(mFormatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(decCtx);

    int packetsRead = 0;
    bool firstGopLogged = false;

    // For proper timecode: output should start at 0, so we need to track
    // the timestamp of the first frame that gets written
    double firstKeptTimestamp = cutList.first().first;  // Start of first keep segment
    int64_t firstKeptPts = (int64_t)((firstKeptTimestamp + streamStartTime) / av_q2d(inVideoStream->time_base));
    qDebug() << "  First kept timestamp:" << firstKeptTimestamp << "seconds, PTS offset:" << firstKeptPts;

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        packetsRead++;

        if (packet->stream_index == mVideoStreamIndex) {
            double packetPts = packet->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
            bool isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;

            // If this is a keyframe and we have buffered packets, process the GOP
            if (isKeyframe && !gopPackets.isEmpty()) {
                double gopStartTs = gopPackets.first()->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
                double gopEndTs = gopPackets.last()->pts * av_q2d(inVideoStream->time_base) - streamStartTime;

                int mode = getRangeMode(gopStartTs, gopEndTs, cutList);

                // Log first few GOPs to verify timestamp calculation
                if (!firstGopLogged || packetsWritten > 0) {
                    qDebug() << "GOP: start=" << gopStartTs << "end=" << gopEndTs << "mode=" << mode
                             << (mode == 0 ? "(drop)" : mode == 1 ? "(copy)" : "(encode)");
                    if (!firstGopLogged) firstGopLogged = true;
                }

                if (mode == 1) {
                    // Full copy - stream copy all packets in GOP
                    static bool firstCopyDebug = true;
                    if (firstCopyDebug) {
                        qDebug() << "  First copy GOP: droppedVideoDuration=" << droppedVideoDuration
                                 << "firstKeptPts=" << firstKeptPts;
                        firstCopyDebug = false;
                    }
                    for (AVPacket* gopPkt : gopPackets) {
                        AVPacket* outPkt = av_packet_clone(gopPkt);
                        outPkt->stream_index = 0;
                        // Adjust PTS to start at 0: subtract first kept PTS and any dropped duration
                        int64_t origPts = outPkt->pts;
                        outPkt->pts = outPkt->pts - firstKeptPts - droppedVideoDuration;
                        static int ptsDebugCount = 0;
                        if (ptsDebugCount < 3) {
                            qDebug() << "    Packet: origPts=" << origPts << "newPts=" << outPkt->pts
                                     << "dts=" << lastVideoDts;
                            ptsDebugCount++;
                        }
                        outPkt->dts = lastVideoDts;
                        lastVideoDts += outPkt->duration;
                        av_packet_rescale_ts(outPkt, inVideoStream->time_base, outVideoStream->time_base);
                        av_interleaved_write_frame(outFmtCtx, outPkt);
                        av_packet_free(&outPkt);
                        packetsWritten++;
                    }
                } else if (mode == 2) {
                    // Partial - need to decode and encode
                    qDebug() << "  Encoding GOP at" << gopStartTs << "-" << gopEndTs;

                    if (!encoderOpen) {
                        if (!openEncoder()) {
                            qDebug() << "  ERROR: Failed to open encoder!";
                            continue;
                        }
                    }

                    // Lambda to process a decoded frame
                    int gopFrameIdx = 0;
                    auto processFrame = [&](AVFrame* f) {
                        double frameTs = f->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
                        bool included = isTimestampIncluded(frameTs, cutList);
                        // Show first 3 and last 3 frames of each GOP, plus any included frames
                        if (gopFrameIdx < 3 || included) {
                            qDebug() << "    Frame" << gopFrameIdx << "PTS:" << f->pts
                                     << "frameTs:" << frameTs << "included:" << included;
                        }
                        gopFrameIdx++;

                        if (included) {
                            // Adjust PTS to start at 0
                            f->pts = f->pts - firstKeptPts - droppedVideoDuration;
                            f->pict_type = AV_PICTURE_TYPE_NONE;  // Let encoder decide

                            int ret = avcodec_send_frame(encCtx, f);
                            if (ret < 0) {
                                qDebug() << "    ERROR sending frame to encoder:" << avErrorToString(ret);
                                return;
                            }
                            static int framesSentToEncoder = 0;
                            framesSentToEncoder++;
                            if (framesSentToEncoder <= 5) {
                                qDebug() << "    Sent frame to encoder, total sent:" << framesSentToEncoder;
                            }

                            AVPacket* encPkt = av_packet_alloc();
                            while (avcodec_receive_packet(encCtx, encPkt) == 0) {
                                encPkt->stream_index = 0;
                                // Without B-frames: DTS = PTS (decode order = presentation order)
                                int64_t dtsInEncTimebase = av_rescale_q(lastVideoDts, inVideoStream->time_base, encCtx->time_base);
                                encPkt->dts = dtsInEncTimebase;
                                encPkt->pts = dtsInEncTimebase;  // Same as DTS since no B-frames
                                // Increment lastVideoDts by one frame duration
                                lastVideoDts += frameDurationInStreamTB;
                                av_packet_rescale_ts(encPkt, encCtx->time_base, outVideoStream->time_base);
                                av_interleaved_write_frame(outFmtCtx, encPkt);
                                av_packet_unref(encPkt);
                                packetsWritten++;
                                framesEncoded++;
                            }
                            av_packet_free(&encPkt);
                        } else {
                            // Only track dropped frames that are AFTER firstKeptTimestamp
                            // (frames before are already accounted for by firstKeptPts subtraction)
                            if (frameTs >= firstKeptTimestamp) {
                                droppedVideoDuration += f->duration;
                            }
                        }
                    };

                    // Send all packets to decoder
                    int gopFramesDecoded = 0;
                    for (AVPacket* gopPkt : gopPackets) {
                        int ret = avcodec_send_packet(decCtx, gopPkt);
                        if (ret < 0) {
                            qDebug() << "    ERROR sending packet to decoder:" << avErrorToString(ret);
                            continue;
                        }
                        while (avcodec_receive_frame(decCtx, frame) == 0) {
                            gopFramesDecoded++;
                            processFrame(frame);
                            av_frame_unref(frame);
                        }
                    }

                    // Flush decoder to get remaining B-frames
                    avcodec_send_packet(decCtx, nullptr);
                    while (avcodec_receive_frame(decCtx, frame) == 0) {
                        gopFramesDecoded++;
                        processFrame(frame);
                        av_frame_unref(frame);
                    }
                    avcodec_flush_buffers(decCtx);

                    qDebug() << "    Decoded" << gopFramesDecoded << "frames, encoded" << framesEncoded;

                    // Flush and restart encoder after encoding section
                    closeEncoder();
                } else {
                    // Full drop - only track if this GOP is AFTER the first kept segment
                    // (otherwise it's already accounted for by firstKeptPts subtraction)
                    if (gopEndTs >= cutList.first().first) {
                        for (AVPacket* gopPkt : gopPackets) {
                            droppedVideoDuration += gopPkt->duration;
                        }
                    }
                }

                // Clear GOP buffer
                for (AVPacket* gopPkt : gopPackets) {
                    av_packet_free(&gopPkt);
                }
                gopPackets.clear();
            }

            // Add current packet to GOP buffer
            gopPackets.append(av_packet_clone(packet));

        } else if (packet->stream_index == mAudioStreamIndex && outAudioStream) {
            // Audio packet - check if timestamp is in keep range
            // Use video start time as reference since cutList is relative to video
            double audioTs = packet->pts * av_q2d(inAudioStream->time_base) - streamStartTime;

            if (isTimestampIncluded(audioTs, cutList)) {
                AVPacket* outPkt = av_packet_clone(packet);
                outPkt->stream_index = outAudioStream->index;
                // Calculate audio firstKeptPts in audio time_base
                int64_t audioFirstKeptPts = (int64_t)((firstKeptTimestamp + streamStartTime) / av_q2d(inAudioStream->time_base));
                outPkt->pts = outPkt->pts - audioFirstKeptPts - droppedAudioDuration;
                outPkt->dts = lastAudioDts;
                lastAudioDts += outPkt->duration;
                av_packet_rescale_ts(outPkt, inAudioStream->time_base, outAudioStream->time_base);
                av_interleaved_write_frame(outFmtCtx, outPkt);
                av_packet_free(&outPkt);
            } else {
                // Only track dropped audio that is AFTER the first kept segment
                // (audio before firstKeptTimestamp is already accounted for by audioFirstKeptPts subtraction)
                if (audioTs >= firstKeptTimestamp) {
                    droppedAudioDuration += packet->duration;
                }
            }
        }

        av_packet_unref(packet);
    }

    // Process remaining GOP
    if (!gopPackets.isEmpty()) {
        double gopStartTs = gopPackets.first()->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
        double gopEndTs = gopPackets.last()->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
        int mode = getRangeMode(gopStartTs, gopEndTs, cutList);

        if (mode >= 1) {
            // Copy remaining packets (simplified for last GOP)
            for (AVPacket* gopPkt : gopPackets) {
                double pktTs = gopPkt->pts * av_q2d(inVideoStream->time_base) - streamStartTime;
                if (isTimestampIncluded(pktTs, cutList)) {
                    AVPacket* outPkt = av_packet_clone(gopPkt);
                    outPkt->stream_index = 0;
                    // Adjust PTS to start at 0
                    outPkt->pts = outPkt->pts - firstKeptPts - droppedVideoDuration;
                    outPkt->dts = lastVideoDts;
                    lastVideoDts += outPkt->duration;
                    av_packet_rescale_ts(outPkt, inVideoStream->time_base, outVideoStream->time_base);
                    av_interleaved_write_frame(outFmtCtx, outPkt);
                    av_packet_free(&outPkt);
                    packetsWritten++;
                }
            }
        }

        for (AVPacket* gopPkt : gopPackets) {
            av_packet_free(&gopPkt);
        }
        gopPackets.clear();
    }

    // Cleanup
    closeEncoder();
    av_write_trailer(outFmtCtx);

    qDebug() << "smartCut: Complete";
    qDebug() << "  Packets read:" << packetsRead;
    qDebug() << "  Packets written:" << packetsWritten;
    qDebug() << "  Frames encoded:" << framesEncoded;

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decCtx);
    if (bsfDumpExtra) av_bsf_free(&bsfDumpExtra);
    if (bsfToAnnexB) av_bsf_free(&bsfToAnnexB);

    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFmtCtx->pb);
    }
    avformat_free_context(outFmtCtx);

    return true;
}

// ----------------------------------------------------------------------------
// Concatenate multiple video segments
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::concatenateSegments(const QString& outputFile,
                                           const QStringList& segmentFiles)
{
    if (segmentFiles.isEmpty()) {
        setError("No segments to concatenate");
        return false;
    }

    // Create concat list file
    QString listFile = outputFile + ".txt";
    QFile file(listFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError("Could not create concat list file");
        return false;
    }

    QTextStream out(&file);
    for (const QString& segment : segmentFiles) {
        out << "file '" << segment << "'\n";
    }
    file.close();

    // Build ffmpeg concat command
    QStringList args;
    args << "-y"
         << "-f" << "concat"
         << "-safe" << "0"
         << "-i" << listFile
         << "-c" << "copy"
         << outputFile;

    qDebug() << "FFmpeg concat command:" << args.join(" ");

    QProcess proc;
    proc.start("/usr/bin/ffmpeg", args);

    if (!proc.waitForStarted(5000)) {
        setError("FFmpeg failed to start");
        QFile::remove(listFile);
        return false;
    }

    if (!proc.waitForFinished(300000)) {
        setError("FFmpeg timed out");
        proc.kill();
        QFile::remove(listFile);
        return false;
    }

    QFile::remove(listFile);

    if (proc.exitCode() != 0) {
        setError(QString("FFmpeg concat failed: %1").arg(
            QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }

    return true;
}
