/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2024 / TTCut-ng                                */
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

#include "ttffmpegwrapper.h"

#include <QDebug>
#include <QTime>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

// Include libav headers (C libraries)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
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

    int ret = avformat_open_input(&mFormatCtx, filePath.toUtf8().constData(),
                                   nullptr, nullptr);
    if (ret < 0) {
        setError(QString("Could not open file: %1").arg(avErrorToString(ret)));
        return false;
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
    if (formatName.contains("h264") || formatName.contains("hevc") ||
        formatName.contains("mpegvideo") || formatName.contains("m2v")) {
        return CONTAINER_ELEMENTARY;
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
// Check if file is a container format (not elementary stream)
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::isContainerFormat() const
{
    TTContainerType ct = detectContainer();
    return ct != CONTAINER_UNKNOWN && ct != CONTAINER_ELEMENTARY;
}

// ----------------------------------------------------------------------------
// Demux container to elementary streams
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::demuxToElementary(const QString& outputDir, QString* videoFile, QString* audioFile)
{
    if (!mFormatCtx) {
        setError("No file open");
        return false;
    }

    if (!isContainerFormat()) {
        // Already elementary, just return the source file
        if (videoFile) {
            *videoFile = QString::fromUtf8(mFormatCtx->url);
        }
        return true;
    }

    QString sourceFile = QString::fromUtf8(mFormatCtx->url);
    QFileInfo fileInfo(sourceFile);
    QString baseName = fileInfo.completeBaseName();

    // Determine output file names based on codec
    TTVideoCodecType codec = detectVideoCodec();
    QString videoExt;
    switch (codec) {
        case CODEC_MPEG2: videoExt = ".m2v"; break;
        case CODEC_H264:  videoExt = ".h264"; break;
        case CODEC_H265:  videoExt = ".h265"; break;
        default:          videoExt = ".es"; break;
    }

    QString videoOutput = outputDir + "/" + baseName + videoExt;
    QString audioOutput = outputDir + "/" + baseName + ".ac3";  // Assume AC3 for now

    // Build ffmpeg demux command
    QStringList args;
    args << "-y"
         << "-i" << sourceFile;

    // Determine output format based on codec
    QString outputFormat;
    switch (codec) {
        case CODEC_MPEG2: outputFormat = "mpeg2video"; break;
        case CODEC_H264:  outputFormat = "h264"; break;
        case CODEC_H265:  outputFormat = "hevc"; break;
        default:          outputFormat = "rawvideo"; break;
    }

    // Extract video stream
    if (mVideoStreamIndex >= 0) {
        args << "-map" << QString("0:%1").arg(mVideoStreamIndex)
             << "-c:v" << "copy"
             << "-an";  // No audio

        // Add bitstream filter for H.264/H.265 from MP4/MKV containers
        // to convert from AVCC/HVCC to Annex B format
        TTContainerType container = detectContainer();
        if (codec == CODEC_H264 && (container == CONTAINER_MP4 || container == CONTAINER_MKV)) {
            args << "-bsf:v" << "h264_mp4toannexb";
        } else if (codec == CODEC_H265 && (container == CONTAINER_MP4 || container == CONTAINER_MKV)) {
            args << "-bsf:v" << "hevc_mp4toannexb";
        }

        args << "-f" << outputFormat
             << videoOutput;
    }

    qDebug() << "FFmpeg demux video command:" << args.join(" ");

    QProcess procVideo;
    procVideo.start("/usr/bin/ffmpeg", args);

    if (!procVideo.waitForStarted(5000)) {
        setError("FFmpeg failed to start for video demux");
        return false;
    }

    if (!procVideo.waitForFinished(300000)) {
        setError("FFmpeg video demux timed out");
        procVideo.kill();
        return false;
    }

    if (procVideo.exitCode() != 0) {
        setError(QString("FFmpeg video demux failed: %1").arg(
            QString::fromUtf8(procVideo.readAllStandardError())));
        return false;
    }

    if (videoFile) {
        *videoFile = videoOutput;
    }

    // Extract audio stream if present
    if (mAudioStreamIndex >= 0 && audioFile) {
        QStringList audioArgs;
        audioArgs << "-y"
                  << "-i" << sourceFile
                  << "-map" << QString("0:%1").arg(mAudioStreamIndex)
                  << "-c:a" << "copy"
                  << "-vn"  // No video
                  << audioOutput;

        qDebug() << "FFmpeg demux audio command:" << audioArgs.join(" ");

        QProcess procAudio;
        procAudio.start("/usr/bin/ffmpeg", audioArgs);

        if (procAudio.waitForStarted(5000) && procAudio.waitForFinished(300000)) {
            if (procAudio.exitCode() == 0) {
                *audioFile = audioOutput;
            }
        }
    }

    qDebug() << "Demux complete. Video:" << videoOutput;
    return true;
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

    // Seek to beginning
    av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);

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

    // Seek back to beginning
    av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);

    qDebug() << "Frame index built:" << mFrameIndex.size() << "frames in"
             << (currentGOP + 1) << "GOPs";

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

    int64_t seekPts = mFrameIndex[keyframeIndex].pts;

    int ret = av_seek_frame(mFormatCtx, mVideoStreamIndex, seekPts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        setError(QString("Seek failed: %1").arg(avErrorToString(ret)));
        return false;
    }

    // Flush codec buffers after seek
    if (mVideoCodecCtx) {
        avcodec_flush_buffers(mVideoCodecCtx);
    }

    mCurrentFrameIndex = keyframeIndex;
    return true;
}

// ----------------------------------------------------------------------------
// Decode frame at specific index and return as QImage
// ----------------------------------------------------------------------------
QImage TTFFmpegWrapper::decodeFrame(int frameIndex)
{
    if (!seekToFrame(frameIndex)) {
        return QImage();
    }

    // Decode frames until we reach the target frame
    while (mCurrentFrameIndex < frameIndex) {
        QImage img = decodeCurrentFrame();
        if (img.isNull()) {
            return QImage();
        }
        mCurrentFrameIndex++;
    }

    return decodeCurrentFrame();
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
