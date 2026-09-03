/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFFMPEGWRAPPER
// Wrapper class for libav/ffmpeg functionality
// ----------------------------------------------------------------------------

#include "ttffmpegwrapper.h"
#include "ttavutil.h"
#include "ttessmartcut.h"
#include "../avstream/ttdisplayordermap.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttnaluparser.h"
#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

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
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// Returns the bit depth of the luma component for the given AVPixelFormat.
// Falls back to 8 for unknown / AV_PIX_FMT_NONE so callers using uint8_t* read
// paths stay safe.
static int yPlaneDepth(int avPixelFormat)
{
    const AVPixFmtDescriptor* desc =
        av_pix_fmt_desc_get((AVPixelFormat)avPixelFormat);
    return (desc && desc->nb_components > 0) ? desc->comp[0].depth : 8;
}

// Static initialization flag — std::call_once gives us thread-safe one-shot
// initialization so concurrent TTFFmpegWrapper construction from multiple
// threads can't accidentally run av_register_all twice.
#include <mutex>
static std::once_flag sFFmpegInitOnce;
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
    , mSearchMode(false)
    , mCancelToken(nullptr)
    , mIsPAFF(false)
    , mH264Log2MaxFrameNum(4)
    , mH264FrameMbsOnlyFlag(true)
    , mRawPacketCount(0)
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
    std::call_once(sFFmpegInitOnce, []() {
        // Note: av_register_all() is deprecated in newer FFmpeg versions
        // and not needed for FFmpeg 4.0+
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
        av_register_all();
#endif
        sFFmpegInitialized = true;
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "FFmpeg initialized, version:" << av_version_info();
    });
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
        if (TTSettings::instance()->logFFmpegDecoder())
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
                    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                        QString("Warning: avcodec_parameters_to_context failed: %1").arg(avErrorToString(p2cRet)));
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
                        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                            QString("Warning: Could not open video codec: %1").arg(avErrorToString(ret)));
                        avcodec_free_context(&mVideoCodecCtx);
                        mVideoCodecCtx = nullptr;
                    }
                }
            }
        }
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Opened file:" << filePath;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "  Streams:" << mFormatCtx->nb_streams;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "  Video stream:" << mVideoStreamIndex;
    if (TTSettings::instance()->logFFmpegDecoder())
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

    if (mPendingPacket)
        av_packet_free(&mPendingPacket);

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

    // Free YUV-plane tight-packed buffers (re-allocated on next decodeFrameYUV)
    delete[] mYBuffer; mYBuffer = nullptr;
    delete[] mUBuffer; mUBuffer = nullptr;
    delete[] mVBuffer; mVBuffer = nullptr;
    mYUVBufferWidth = 0;
    mYUVBufferHeight = 0;

    // Free slow-path swscale context (used for 10-bit / non-YUV420P inputs)
    if (mSwsCtxYUV) { sws_freeContext(mSwsCtxYUV); mSwsCtxYUV = nullptr; }
    mSwsCtxYUVSrcFmt = -1;
    mSwsCtxYUVWidth  = 0;
    mSwsCtxYUVHeight = 0;

    mDecoderFrameIndex = -1;
    mDecoderDrained = false;
    mIsElementaryStream = false;
    mIsPAFF = false;
    mH264Log2MaxFrameNum = 4;
    mH264FrameMbsOnlyFlag = true;
    clearFrameCache();
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

        // Calculate frame rate. Prefer r_frame_rate over avg_frame_rate:
        // for raw H.264/H.265 ES files without container PTS, libav often
        // returns half the real rate as avg (a B-frame reorder window quirk).
        // r_frame_rate comes from the SPS/codec timing and is reliable.
        // PAFF/MBAFF streams fall through the existing frame_rate>30 PAFF
        // correction in tth26xvideostream.cpp, so doubling the input here
        // does not double the final progressive frame rate.
        if (stream->r_frame_rate.den > 0) {
            info.frameRate = av_q2d(stream->r_frame_rate);
        } else if (stream->avg_frame_rate.den > 0) {
            info.frameRate = av_q2d(stream->avg_frame_rate);
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
// Sample aspect ratio of the video stream (width/height factor, 1.0 = square
// pixels). The codec context carries the SPS-derived SAR once a frame was
// decoded; codecpar is the fallback for callers that never decoded.
// ----------------------------------------------------------------------------
double TTFFmpegWrapper::sampleAspectRatio() const
{
    AVRational sar = {0, 1};

    if (mVideoCodecCtx)
        sar = mVideoCodecCtx->sample_aspect_ratio;

    if ((sar.num <= 0 || sar.den <= 0) && mFormatCtx && mVideoStreamIndex >= 0)
        sar = mFormatCtx->streams[mVideoStreamIndex]->codecpar->sample_aspect_ratio;

    if (sar.num <= 0 || sar.den <= 0)
        return 1.0;

    return av_q2d(sar);
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
// Parse H.264 SPS from extradata for PAFF detection
// Sets mH264Log2MaxFrameNum and mH264FrameMbsOnlyFlag
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::parseH264SpsFromExtradata(const uint8_t* data, int size)
{
    if (!data || size < 5) return;

    int nalStart = -1;
    for (int s = TTNaluParser::findStartCodePayload(data, size, 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(data, size, s)) {
        if ((data[s] & 0x1F) == 7) { nalStart = s; break; }
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
        if (TTSettings::instance()->logFFmpegDecoder())
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
    for (int s = TTNaluParser::findStartCodePayload(data, size, 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(data, size, s)) {
        uint8_t nalType = data[s] & 0x1F;
        if (nalType == 1 || nalType == 5) { nalStart = s; break; }
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
    if (!setupIndexingPass(videoStreamIndex)) return false;
    if (videoStreamIndex < 0) videoStreamIndex = mVideoStreamIndex;

    scanPacketsIntoRawIndex(videoStreamIndex);
    mergePAFFFieldsInIndex();
    finalizeFrameIndex();
    buildDisplayOrderMap();
    rewindContext(videoStreamIndex);

    if (!mFrameIndex.isEmpty() && mFrameIndex[0].pts == AV_NOPTS_VALUE) {
        assignPtsFromFrameRate(videoStreamIndex);
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

    if (TTSettings::instance()->logFFmpegDecoder())
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
// Set error message
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::setError(const QString& error)
{
    mLastError = error;
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("TTFFmpegWrapper error: %1").arg(error));
}

// ----------------------------------------------------------------------------
// Convert libav error code to string
// ----------------------------------------------------------------------------
QString TTFFmpegWrapper::avErrorToString(int errnum)
{
    return ttAvErrorToString(errnum);
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
    // Search-mode: skip the prev-keyframe DPB-prefill. Safe when the caller
    // only asks for I-frames (intra-coded, self-decodable for their own pixels).
    // Saves ~one full GOP of decode work per call.
    if (!mSearchMode && keyframeIndex > 0) {
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
            if (TTSettings::instance()->logFFmpegDecoder())
                qDebug() << "ES seek: fileOffset was -1, using" << byteOffset;
        }

        ret = avio_seek(mFormatCtx->pb, byteOffset, SEEK_SET);
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "ES seek to byte" << byteOffset << "seekKeyframe:" << seekKeyframe << "targetKeyframe:" << keyframeIndex << "avio_seek result:" << ret;
        if (ret >= 0) {
            avformat_flush(mFormatCtx);
            ret = 0;  // Success
        } else {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("avio_seek failed with: %1 %2").arg(ret).arg(avErrorToString(ret)));
        }
    } else {
        // For container formats, use timestamp-based seeking
        int64_t seekPts = mFrameIndex[seekKeyframe].pts;
        if (TTSettings::instance()->logFFmpegDecoder())
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
    // A packet held back by a send-EAGAIN belongs to the pre-seek tag domain;
    // sending it into the flushed decoder would deliver one frame under a
    // stale decode-order tag.
    if (mPendingPacket)
        av_packet_free(&mPendingPacket);
    mDecoderDrained = false;

    mCurrentFrameIndex = seekKeyframe;
    mDecoderFrameIndex = seekKeyframe;
    return true;
}

// decodeFrame(n): n is a DISPLAY position. The skip-loop counts decoder
// output (display order) from the seek keyframe, so the delivered frame is
// the display-rank-n frame. This was verified bit-exact against the POC
// display-order map (0 mismatches over 162,530 frames, 2026-06-12; the
// throwaway harness for that run was removed on 2026-07-31 — the standing
// check is tools/diag/test_h264_leading, which compares the production map
// against the same decoder ground truth) — decodeFrame and TTDisplayOrderMap
// derive from the same decoder output order. Do NOT "fix" this to
// decode-order indexing.
// ----------------------------------------------------------------------------
// Decode frame at specific index and return as QImage
// ----------------------------------------------------------------------------
QImage TTFFmpegWrapper::decodeFrame(int frameIndex)
{
    return decodeFrameInternal(frameIndex, 0);
}

QImage TTFFmpegWrapper::decodeFrameInternal(int frameIndex, int fallbackDepth)
{
    // Bounds check — frameIndex is a DISPLAY position. When the display-order
    // map is valid the visible range is [0, displayCount()) (= n minus dropped
    // RASL leading pics for HEVC); otherwise it is the raw decode dimension.
    // Both this guard and the map lookup below must agree on the upper bound.
    const int frameUpperBound =
        (mDisplayOrderMap.isValid() && mDisplayOrderMap.displayCount() > 0)
        ? mDisplayOrderMap.displayCount()
        : mFrameIndex.size();
    if (frameIndex < 0 || frameIndex >= frameUpperBound) {
        if (TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "decodeFrame: index" << frameIndex
                     << "out of range (0 -" << frameUpperBound-1 << ")";
        }
        if (frameIndex >= frameUpperBound && frameUpperBound > 0) {
            frameIndex = frameUpperBound - 1;
            if (TTSettings::instance()->logFFmpegDecoder())
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

    // Map the DISPLAY position to the decode-order AU to deliver. This is the
    // SAME map the smart cut uses (displayOrderMap), so the still-image shows
    // exactly the frame the cut starts with for cut-in N (WYSIWYG). The old code
    // counted (frameIndex - seekKeyframe) decoder outputs, which yields the
    // display-RANK frame — off by the local B-frame reorder amount.
    int targetAU = frameIndex;
    if (mDisplayOrderMap.isValid() && frameIndex >= 0 && frameIndex < mDisplayOrderMap.displayCount())
        targetAU = mDisplayOrderMap.displayToDecode(frameIndex);

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "decodeFrame: display" << frameIndex << "-> targetAU" << targetAU
                 << "total_frames=" << mFrameIndex.size();

    // Always seek (consistent DPB state across decoder instances; the LRU cache
    // mitigates the cost). Decoder emits frames in DISPLAY order, each tagged
    // with its decode-order AU in pts; decode until the output whose pts ==
    // targetAU, then convert THAT frame. Same decode work as the old skip; only
    // the stop condition changed (deliver the mapped AU, not the Nth output).
    QImage result;
    for (int attempt = 0; attempt < 2 && result.isNull(); ++attempt) {
        if (!seekToFrame(targetAU)) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrame: seekToFrame failed for AU %1 (display %2)")
                    .arg(targetAU).arg(frameIndex));
            break;
        }
        mDecoderFrameIndex = mCurrentFrameIndex;
        mDecodeOrderTag    = mCurrentFrameIndex;

        int guard = 0;
        // The target AU must appear between the seek point and shortly after
        // it — reorder and field pairing move it by a GOP at most, never by
        // the length of the file. The old bound (the whole index) turned every
        // undelivered target into a full decode to EOF: 41 s per attempt on
        // 06x03, twice, then again for the neighbour frame.
        // seekStart is where the loop actually starts decoding, i.e.
        // mCurrentFrameIndex after seekToFrame() — not targetAU, not frameIndex.
        const int seekStart = mCurrentFrameIndex;
        // Placeholder only — the scan below always assigns a real value,
        // either the second keyframe past the target or (fewer than two
        // left) end of file. Never read at this initial value.
        int headroomEnd = targetAU;
        int keyframesSeen = 0;
        for (int i = targetAU + 1; i < mFrameIndex.size(); ++i) {
            if (!mFrameIndex[i].isKeyframe) continue;
            headroomEnd = i;
            if (++keyframesSeen == 2) break;   // second keyframe beyond the target
        }
        if (keyframesSeen < 2)
            headroomEnd = mFrameIndex.size() - 1;   // fewer than two left: end of file
        const int guardMax = qBound(1,
                                    (targetAU - seekStart) + qMax(headroomEnd - targetAU, 256),
                                    mFrameIndex.size() > 0 ? mFrameIndex.size() : 100000);
        const bool logTags = TTSettings::instance()->logFFmpegDecoder();
        while (guard++ < guardMax) {
            if (isCancelled()) return QImage();   // caller gave up; not a failure
            if (!skipCurrentFrame()) break;   // decodes one output into mDecodedFrame
            if (logTags && (guard <= 5 || static_cast<int>(mDecodedFrame->pts) >= targetAU - 2))
                qDebug() << "  skip-loop output" << guard << "pts-tag" << mDecodedFrame->pts
                         << "(target" << targetAU << ")";
            if (static_cast<int>(mDecodedFrame->pts) == targetAU) {
                result = convertDecodedFrameToImage();
                break;
            }
        }
        if (logTags && result.isNull())
            qDebug() << "  skip-loop ended after" << guard << "outputs without hitting target" << targetAU;
        if (result.isNull() && attempt == 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrame: targetAU %1 (display %2) not delivered — retrying with fresh seek")
                    .arg(targetAU).arg(frameIndex));
            mDecoderDrained = true;  // force a clean re-seek on the retry
        }
    }

    // deliveredDecodeIndex is now exact: the delivered frame's decode AU == targetAU.
    // Used by the playback seek path (onPlayVideo) to land mpv on the shown frame.
    if (!result.isNull() && frameIndex >= 0 && frameIndex < mFrameIndex.size())
        mFrameIndex[frameIndex].deliveredDecodeIndex = targetAU;

    // Fallback: try exactly one frame earlier if the target cannot be decoded.
    if (result.isNull() && frameIndex > 0 && fallbackDepth == 0 && !isCancelled()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("decodeFrame: retry failed — trying frame %1").arg(frameIndex - 1));
        mDecoderDrained = true;  // force a seek in the neighbour attempt
        result = decodeFrameInternal(frameIndex - 1, 1);
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
    } else if (!isCancelled()) {
        if (mSearchMode && TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "Search-mode decodeFrame: failure at frame" << frameIndex
                     << "(possibly non-IDR I-slice with DPB inconsistency)";
        }
        TTMessageLogger::getInstance()->errorMsg(__FILE__, __LINE__,
            QString("decodeFrame: FAILED to decode frame %1 and fallback (total_frames=%2)")
                .arg(frameIndex).arg(mFrameIndex.size()));
    }
    return result;
}

// ----------------------------------------------------------------------------
// Decode a frame and expose YUV420P planes via TFrameInfo
// ----------------------------------------------------------------------------
//! Sequential-decode optimization: when frameIndex == mDecoderFrameIndex+1,
//! the next frame is decoded directly without re-seek (~6-10x faster on
//! H.264/H.265). The first call after openFile/closeFile/seekToFrame
//! triggers a full seek+DPB-prefill+skip-to-target.
//!
//! Pixel format: 8-bit YUV420P only. Returns false on other formats.
bool TTFFmpegWrapper::decodeFrameYUV(int frameIndex, TFrameInfo& outInfo)
{
    if (!mFormatCtx || !mVideoCodecCtx) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("decodeFrameYUV: not initialized"));
        return false;
    }
    // Bounds check — frameIndex is a DISPLAY position (see data/ttsearchtask.cpp:274-282).
    // When the display-order map is valid the visible range is [0, displayCount()); otherwise
    // fall back to the raw decode dimension (H.264/MPEG-2 maps are strict permutations).
    const int displayCount = mDisplayOrderMap.isValid()
                           ? mDisplayOrderMap.displayCount() : mFrameIndex.size();
    if (frameIndex < 0 || frameIndex >= displayCount) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("decodeFrameYUV: index %1 out of range (0 - %2)")
                .arg(frameIndex).arg(displayCount - 1));
        return false;
    }

    // Display position -> raw decode AU (identity if no map).
    int targetAU = frameIndex;
    if (mDisplayOrderMap.isValid() && frameIndex >= 0 && frameIndex < mDisplayOrderMap.displayCount())
        targetAU = mDisplayOrderMap.displayToDecode(frameIndex);

    // Ensure mDecodedFrame is allocated
    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: could not allocate decoded frame"));
            return false;
        }
    }

    // Sequential-decode path: previous call delivered the immediately preceding display
    // position; the decoder is still positioned to emit the next output.
    bool sequentialPath = (mDecoderFrameIndex == frameIndex - 1
                           && !mDecoderDrained
                           && mDecoderFrameIndex >= 0);

    if (!sequentialPath) {
        // Non-sequential path: seek by raw decode AU, then skip until the decoder
        // emits the output whose pts tag == targetAU (mirrors decodeFrame exactly).
        if (!seekToFrame(targetAU)) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: seekToFrame failed for AU %1 (display %2)")
                    .arg(targetAU).arg(frameIndex));
            return false;
        }
        mDecoderFrameIndex = mCurrentFrameIndex;
        mDecodeOrderTag    = mCurrentFrameIndex;

        const int guardMax = mFrameIndex.size() > 0 ? mFrameIndex.size() : 100000;
        int guard = 0;
        bool reached = false;
        while (guard++ < guardMax) {
            if (!skipCurrentFrame()) break;   // decodes one output into mDecodedFrame
            if (static_cast<int>(mDecodedFrame->pts) == targetAU) { reached = true; break; }
        }
        if (!reached) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: could not reach AU %1 (display %2)")
                    .arg(targetAU).arg(frameIndex));
            return false;
        }
        // Target frame is now in mDecodedFrame — fall through to YUV conversion below.
        mDecoderFrameIndex = frameIndex;
    }

    // For the sequential path: decode the next output from the stream into mDecodedFrame.
    // For the non-sequential path: mDecodedFrame already holds the target frame (set above).
    bool gotFrame = !sequentialPath;

    if (sequentialPath) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: could not allocate packet"));
            return false;
        }
        while (!gotFrame && av_read_frame(mFormatCtx, packet) >= 0) {
            if (packet->stream_index == mVideoStreamIndex) {
                int ret = avcodec_send_packet(mVideoCodecCtx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }
                ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
                if (ret == 0)
                    gotFrame = true;
            }
            av_packet_unref(packet);
        }
        // EOF drain if no frame yet
        if (!gotFrame) {
            avcodec_send_packet(mVideoCodecCtx, nullptr);
            if (avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame) == 0) {
                gotFrame = true;
                mDecoderDrained = true;
            }
        }
        av_packet_free(&packet);
        if (!gotFrame) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: no frame decoded for display %1").arg(frameIndex));
            return false;
        }
        mDecoderFrameIndex = frameIndex;
    }

    int srcFmt = mDecodedFrame->format;
    int w = mDecodedFrame->width;
    int h = mDecodedFrame->height;
    int cw = w / 2;
    int ch = h / 2;

    // Allocate / re-allocate tight-packed output buffers on dimension change
    if (mYUVBufferWidth != w || mYUVBufferHeight != h) {
        delete[] mYBuffer;
        delete[] mUBuffer;
        delete[] mVBuffer;
        mYBuffer = new quint8[w * h];
        mUBuffer = new quint8[cw * ch];
        mVBuffer = new quint8[cw * ch];
        mYUVBufferWidth = w;
        mYUVBufferHeight = h;
    }

    if (srcFmt == AV_PIX_FMT_YUV420P) {
        // Fast path: tight-pack memcpy from libav's strided 8-bit planes
        for (int row = 0; row < h; row++) {
            memcpy(mYBuffer + row * w,
                   mDecodedFrame->data[0] + row * mDecodedFrame->linesize[0],
                   w);
        }
        for (int row = 0; row < ch; row++) {
            memcpy(mUBuffer + row * cw,
                   mDecodedFrame->data[1] + row * mDecodedFrame->linesize[1],
                   cw);
            memcpy(mVBuffer + row * cw,
                   mDecodedFrame->data[2] + row * mDecodedFrame->linesize[2],
                   cw);
        }
    } else {
        // Slow path: convert any other planar/interleaved YUV (10-bit Main 10,
        // 4:2:2, 4:4:4, etc.) to 8-bit YUV420P via swscale, writing directly
        // into our tight-packed output buffers.
        if (!mSwsCtxYUV ||
            mSwsCtxYUVSrcFmt != srcFmt ||
            mSwsCtxYUVWidth  != w ||
            mSwsCtxYUVHeight != h) {
            if (mSwsCtxYUV) sws_freeContext(mSwsCtxYUV);
            mSwsCtxYUV = sws_getContext(
                w, h, (AVPixelFormat)srcFmt,
                w, h, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!mSwsCtxYUV) {
                TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                    QString("decodeFrameYUV: sws_getContext failed for src fmt %1 -> YUV420P at %2x%3")
                        .arg(srcFmt).arg(w).arg(h));
                return false;
            }
            mSwsCtxYUVSrcFmt = srcFmt;
            mSwsCtxYUVWidth  = w;
            mSwsCtxYUVHeight = h;
        }

        uint8_t* dst[4]    = { mYBuffer, mUBuffer, mVBuffer, nullptr };
        int      dstStride[4] = { w, cw, cw, 0 };
        int swsRet = sws_scale(mSwsCtxYUV,
                               mDecodedFrame->data, mDecodedFrame->linesize,
                               0, h, dst, dstStride);
        if (swsRet <= 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("decodeFrameYUV: sws_scale failed (ret=%1) for src fmt %2")
                    .arg(swsRet).arg(srcFmt));
            return false;
        }
    }

    // Populate outInfo
    outInfo.Y = mYBuffer;
    outInfo.U = mUBuffer;
    outInfo.V = mVBuffer;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.size = w * h;
    outInfo.chroma_width = cw;
    outInfo.chroma_height = ch;
    outInfo.chroma_size = cw * ch;
    // Map libav pict_type to MPEG-2 type (I=1, P=2, B=3); unknown→0
    switch (mDecodedFrame->pict_type) {
        case AV_PICTURE_TYPE_I: outInfo.type = 1; break;
        case AV_PICTURE_TYPE_P: outInfo.type = 2; break;
        case AV_PICTURE_TYPE_B: outInfo.type = 3; break;
        default:                outInfo.type = 0; break;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Decode current frame and return as QImage
// ----------------------------------------------------------------------------
// Convert the already-decoded mDecodedFrame to a QImage. Lazy-inits the RGB
// frame + scaler. No packet read/decode — operates on whatever is currently in
// mDecodedFrame. Shared by decodeCurrentFrame() and decodeFrame().
QImage TTFFmpegWrapper::convertDecodedFrameToImage()
{
    if (!mVideoCodecCtx || !mDecodedFrame) return QImage();

    if (!mRgbFrame) {
        mRgbFrame = av_frame_alloc();
        if (!mRgbFrame) { setError("Could not allocate RGB frame"); return QImage(); }
        // Ref-counted buffer: av_frame_free will release it via AVBufferRef.
        mRgbFrame->format = AV_PIX_FMT_RGB24;
        mRgbFrame->width  = mVideoCodecCtx->width;
        mRgbFrame->height = mVideoCodecCtx->height;
        if (av_frame_get_buffer(mRgbFrame, 1) < 0) {
            setError("Could not allocate RGB frame buffer");
            av_frame_free(&mRgbFrame);
            return QImage();
        }
    }
    if (!mSwsCtx) {
        mSwsCtx = sws_getContext(
            mVideoCodecCtx->width, mVideoCodecCtx->height, mVideoCodecCtx->pix_fmt,
            mVideoCodecCtx->width, mVideoCodecCtx->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!mSwsCtx) { setError("Could not create scaler context"); return QImage(); }
    }
    sws_scale(mSwsCtx, mDecodedFrame->data, mDecodedFrame->linesize,
              0, mVideoCodecCtx->height, mRgbFrame->data, mRgbFrame->linesize);
    return QImage(mRgbFrame->data[0],
                  mVideoCodecCtx->width, mVideoCodecCtx->height,
                  mRgbFrame->linesize[0],
                  QImage::Format_RGB888).copy();
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
    if (!decoded) {
        if (mSearchMode && TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "Search-mode isFrameBlack: decode failure at frame" << frameIndex
                     << "(possibly non-IDR I-slice with DPB inconsistency)";
        }
        return false;
    }

    mDecoderFrameIndex = frameIndex;
    mCurrentFrameIndex = frameIndex;

    // Analyze Y-plane directly (YUV420P: data[0] = Y, linesize[0] = Y stride).
    // For 10/12-bit content (HEVC Main 10/12), each sample is two bytes; cast
    // to uint16_t and right-shift to compare against the 8-bit threshold.
    int w = mDecodedFrame->width, h = mDecodedFrame->height;
    uint8_t* yPlane = mDecodedFrame->data[0];
    int yStride = mDecodedFrame->linesize[0];
    if (!yPlane || w <= 0 || h <= 0) return false;

    int depth = yPlaneDepth(mDecodedFrame->format);
    int shift = (depth > 8) ? (depth - 8) : 0;

    int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;
    const int step = 2;
    const int earlyExitSamples = 500;
    long lumaSum = 0;
    int totalPixels = 0, blackPixels = 0;

    for (int row = y0; row < y1; row += step) {
        uint8_t* rowBase = yPlane + row * yStride;
        if (shift == 0) {
            for (int col = x0; col < x1; col += step) {
                totalPixels++;
                int y = rowBase[col];
                lumaSum += y;
                if (y < pixelThreshold) blackPixels++;
            }
        } else {
            const uint16_t* line16 = (const uint16_t*)rowBase;
            for (int col = x0; col < x1; col += step) {
                totalPixels++;
                int y = line16[col] >> shift;
                lumaSum += y;
                if (y < pixelThreshold) blackPixels++;
            }
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
    if (!decoded) {
        if (mSearchMode && TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "Search-mode buildHistogram: decode failure at frame" << frameIndex
                     << "(possibly non-IDR I-slice with DPB inconsistency)";
        }
        return false;
    }

    mDecoderFrameIndex = frameIndex;
    mCurrentFrameIndex = frameIndex;

    // Build histogram from Y-plane center 80%. 10/12-bit samples are
    // right-shifted to 8-bit so the 256-bucket layout and downstream
    // histogramDifference math keep matching 8-bit-derived thresholds.
    int w = mDecodedFrame->width, h = mDecodedFrame->height;
    uint8_t* yPlane = mDecodedFrame->data[0];
    int yStride = mDecodedFrame->linesize[0];
    if (!yPlane || w <= 0 || h <= 0) return false;

    int depth = yPlaneDepth(mDecodedFrame->format);
    int shift = (depth > 8) ? (depth - 8) : 0;

    int x0 = w / 10, y0 = h / 10, x1 = w - x0, y1 = h - y0;
    const int step = 2;

    for (int row = y0; row < y1; row += step) {
        uint8_t* rowBase = yPlane + row * yStride;
        if (shift == 0) {
            for (int col = x0; col < x1; col += step) {
                hist[rowBase[col]]++;
                totalPixels++;
            }
        } else {
            const uint16_t* line16 = (const uint16_t*)rowBase;
            for (int col = x0; col < x1; col += step) {
                hist[line16[col] >> shift]++;
                totalPixels++;
            }
        }
    }
    return totalPixels > 0;
}

// ----------------------------------------------------------------------------
// Decode-order tag for a packet about to be sent to the decoder.
// Returns the current tag, then advances it for the NEXT frame. The tag must
// count FRAMES, not packets: with PAFF, two field packets (top + bottom) form
// one frame, so the bottom field must NOT advance the tag — both fields carry
// the same frame-level decode index. This keeps deliveredDecodeIndex in frame
// units, matching the temp playback MKV which merges field pairs into one frame
// (pts = frameCount * frameDur). For progressive/MBAFF (1 packet = 1 frame)
// every packet advances the tag.
// ----------------------------------------------------------------------------
int64_t TTFFmpegWrapper::decodeOrderTagForPacket(const AVPacket* packet)
{
    int64_t tag = mDecodeOrderTag;

    bool advance = true;
    if (mIsPAFF && packet && packet->data && packet->size > 0) {
        TTFieldInfo fi = parseH264FieldInfoFromPacket(packet->data, packet->size);
        // Top field starts a frame but does not complete it — the following
        // bottom field shares the same frame-level decode index. So the TOP
        // field must NOT advance the tag (both fields return the same tag);
        // the bottom field (or any non-field packet) completes the frame and
        // advances the tag for the next frame.
        if (fi.isField && !fi.isBottomField)
            advance = false;
    }
    if (advance)
        mDecodeOrderTag++;

    return tag;
}

QImage TTFFmpegWrapper::decodeNearestKeyframe(int displayPos, int* shownDisplayPos)
{
    if (shownDisplayPos) *shownDisplayPos = -1;
    if (mFrameIndex.isEmpty()) return QImage();

    // Display position -> decode-order AU, then walk back to the keyframe.
    int targetAU = qBound(0, displayPos, int(mFrameIndex.size()) - 1);
    if (mDisplayOrderMap.isValid() && displayPos >= 0
        && displayPos < mDisplayOrderMap.displayCount())
        targetAU = mDisplayOrderMap.displayToDecode(displayPos);
    int keyAU = qBound(0, targetAU, int(mFrameIndex.size()) - 1);
    while (keyAU > 0 && !mFrameIndex[keyAU].isKeyframe)
        keyAU--;

    const int keyDisplay = mDisplayOrderMap.isValid()
                               ? mDisplayOrderMap.decodeToDisplay(keyAU)
                               : keyAU;
    if (shownDisplayPos) *shownDisplayPos = keyDisplay;

    // The LRU cache is shared with decodeFrame() and keyed by display
    // position - dragging back and forth over the same GOP is then free.
    if (keyDisplay >= 0 && mFrameCache.contains(keyDisplay)) {
        mFrameCacheLRU.removeOne(keyDisplay);
        mFrameCacheLRU.append(keyDisplay);
        return mFrameCache[keyDisplay];
    }

    // Borrow the search path's no-prefill seek: mSearchMode is only read by
    // seekToFrame() to decide whether to prefill from the previous keyframe.
    const bool savedSearchMode = mSearchMode;
    mSearchMode = true;
    const bool seekOk = seekToFrame(keyAU);
    mSearchMode = savedSearchMode;
    if (!seekOk) return QImage();

    mDecoderFrameIndex = mCurrentFrameIndex;
    mDecodeOrderTag    = mCurrentFrameIndex;

    // The keyframe is the first packet sent, but with a B-hierarchy the
    // decoder may want a few more packets before it emits output. It emits in
    // display order, so the keyframe (lowest POC of its GOP) comes out first
    // or nearly so - a small guard suffices and keeps a broken stream from
    // turning the preview into the very drain this is meant to avoid.
    QImage result;
    int guard = 0;
    while (guard++ < 64) {
        if (!skipCurrentFrame()) break;
        if (static_cast<int>(mDecodedFrame->pts) == keyAU) {
            result = convertDecodedFrameToImage();
            break;
        }
    }

    if (!result.isNull() && keyDisplay >= 0) {
        mFrameCache[keyDisplay] = result;
        mFrameCacheLRU.append(keyDisplay);
        while (mFrameCacheLRU.size() > mFrameCacheMaxSize) {
            int evict = mFrameCacheLRU.takeFirst();
            mFrameCache.remove(evict);
        }
        mCurrentFrameIndex = keyDisplay;
        mDecoderFrameIndex = keyDisplay;
    }
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

    const bool logRc = TTSettings::instance()->logFFmpegDecoder();

    // A frame may already be waiting from packets sent on earlier calls - the
    // decoder buffers reordered output (hierarchical B). Taking it first is
    // this call's result and, more importantly, the ONLY correct reaction to
    // a previous send_packet EAGAIN: the API's contract is "output full ->
    // receive first, then resend the SAME packet". The old code dropped the
    // packet instead; with a B-hierarchy the queue then stayed full, EVERY
    // remaining packet of the file was read and dropped one by one, the
    // skip-loop's target tag never appeared, and one decodeFrame() call
    // degraded into minutes of read-and-discard (measured on UHD HEVC:
    // "packet with tag 4562..EOF DROPPED", file read to the end twice, then
    // the same again recursively for frame-1).
    int ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
    if (ret == 0)
        return true;

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    bool decoded = false;

    // Feed until the decoder produces a frame: first the packet a previous
    // EAGAIN left pending, then the file. For PAFF the decoder needs 2 field
    // packets per frame (EAGAIN on receive after the first) - unchanged.
    int readRc = 0;
    for (;;) {
        AVPacket* toSend = mPendingPacket;
        if (toSend == nullptr) {
            readRc = av_read_frame(mFormatCtx, packet);
            if (readRc < 0)
                break;                                    // EOF -> drain below
            if (packet->stream_index != mVideoStreamIndex) {
                av_packet_unref(packet);
                continue;
            }
            packet->pts = decodeOrderTagForPacket(packet);
            toSend = packet;
        }

        const int sendRc = avcodec_send_packet(mVideoCodecCtx, toSend);
        if (sendRc == AVERROR(EAGAIN)) {
            // Output queue full. Keep the packet - its tag is already
            // assigned - and take a frame out; the packet goes in on the
            // next call (or the next receive-fail loop round).
            if (toSend != mPendingPacket) {
                mPendingPacket = av_packet_alloc();
                if (mPendingPacket == nullptr) { av_packet_unref(packet); break; }
                av_packet_move_ref(mPendingPacket, packet);
            }
            ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
            if (ret == 0) {
                decoded = true;
                break;
            }
            // send EAGAIN AND receive EAGAIN would violate the API contract;
            // bail out instead of spinning.
            if (logRc)
                qDebug() << "  skipCurrentFrame: send AND receive EAGAIN -"
                         << avErrorToString(ret);
            break;
        }

        if (toSend == mPendingPacket)
            av_packet_free(&mPendingPacket);
        else
            av_packet_unref(packet);

        if (sendRc < 0) {
            // Real send error: this AU is lost, its tag stays consumed (as
            // before) - but unlike EAGAIN this is rare and data-dependent.
            if (logRc)
                qDebug() << "  skipCurrentFrame: send_packet" << avErrorToString(sendRc)
                         << "- packet with tag" << (mDecodeOrderTag - 1) << "DROPPED";
            continue;
        }

        ret = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
        if (ret == 0) {
            decoded = true;
            break;
        }
        // EAGAIN: decoder needs more data (e.g. PAFF second field) -> keep feeding
        if (ret != AVERROR(EAGAIN) && logRc)
            qDebug() << "  skipCurrentFrame: receive_frame" << avErrorToString(ret);
    }

    // EOF drain: flush decoder pipeline to retrieve buffered frames
    if (!decoded) {
        avcodec_send_packet(mVideoCodecCtx, nullptr);
        int recvRet = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
        if (recvRet == 0) {
            decoded = true;
            mDecoderDrained = true;
        } else {
            if (TTSettings::instance()->logFFmpegDecoder()) {
                qDebug() << "skipCurrentFrame: EOF drain exhausted"
                         << "receive_frame=" << recvRet;
            }
        }
    }

    if (!decoded && logRc)
        qDebug() << "  skipCurrentFrame: read loop ended, av_read_frame rc ="
                 << avErrorToString(readRc);
    av_packet_free(&packet);
    return decoded;
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
// Assign sequential PTS/DTS to mFrameIndex from frame rate (.info or stream)
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::assignPtsFromFrameRate(int videoStreamIndex)
{
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Elementary stream detected - calculating PTS/DTS from frame rate";

    // Get frame rate from .info file if available, otherwise from stream
    TTStreamInfo streamInfo = getStreamInfo(videoStreamIndex);
    double frameRate = streamInfo.frameRate;
    QString sourceFile = QString::fromUtf8(mFormatCtx->url);
    QString infoFile = TTESInfo::findInfoFile(sourceFile);

    if (!infoFile.isEmpty()) {
        TTESInfo esInfo(infoFile);
        if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
            frameRate = esInfo.frameRate();
            if (TTSettings::instance()->logFFmpegDecoder())
                qDebug() << "Using frame rate from .info file:" << frameRate;
        }
    }

    // Validate frame rate
    if (frameRate <= 0 || frameRate > 120) {
        frameRate = 25.0; // Default fallback
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("Invalid frame rate, using default: %1").arg(frameRate));
    }

    // PAFF: field-rate reported as frame-rate, correct to actual frame-rate
    if (mIsPAFF && frameRate > 30) {
        if (TTSettings::instance()->logFFmpegDecoder())
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

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Time base:" << timeBase.num << "/" << timeBase.den;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Frame rate:" << frameRate << "fps";
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Frame duration:" << frameDuration << "ticks";

    // Assign sequential PTS/DTS values
    int64_t currentPts = 0;
    for (int i = 0; i < mFrameIndex.size(); ++i) {
        mFrameIndex[i].pts = currentPts;
        mFrameIndex[i].dts = currentPts;
        currentPts += frameDuration;
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Calculated timestamps for" << mFrameIndex.size() << "frames";
    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "First PTS:" << mFrameIndex.first().pts
                 << "Last PTS:" << mFrameIndex.last().pts;
    }
}

// ----------------------------------------------------------------------------
// Validate + reset state + seek to start + parse SPS for PAFF detection
// ----------------------------------------------------------------------------
bool TTFFmpegWrapper::setupIndexingPass(int videoStreamIndex)
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
    mIsPAFF = false;
    mRawPacketCount = 0;
    mRawToMerged.clear();

    // For raw ES files, seek to byte 0 instead of using av_seek_frame.
    // av_seek_frame doesn't work well with raw h264/hevc demuxers.
    QString suffix = QFileInfo(QString::fromUtf8(mFormatCtx->url)).suffix().toLower();
    bool isES = (suffix == "264" || suffix == "h264" ||
                 suffix == "265" || suffix == "h265" || suffix == "hevc" ||
                 suffix == "m2v" || suffix == "mpv");

    if (isES && mFormatCtx->pb) {
        avio_seek(mFormatCtx->pb, 0, SEEK_SET);
        avformat_flush(mFormatCtx);
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "ES file: seeked to byte 0";
    } else {
        av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    // Parse SPS for PAFF detection (H.264 only)
    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
    if (codecId == AV_CODEC_ID_H264) {
        uint8_t* extradata = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata;
        int extradataSize = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata_size;
        if (extradata && extradataSize > 0) {
            parseH264SpsFromExtradata(extradata, extradataSize);
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Seek back to start of stream, log frame index summary
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::rewindContext(int videoStreamIndex)
{
    QString suffix = QFileInfo(QString::fromUtf8(mFormatCtx->url)).suffix().toLower();
    bool isES = (suffix == "264" || suffix == "h264" ||
                 suffix == "265" || suffix == "h265" || suffix == "hevc" ||
                 suffix == "m2v" || suffix == "mpv");

    if (isES && mFormatCtx->pb) {
        avio_seek(mFormatCtx->pb, 0, SEEK_SET);
        avformat_flush(mFormatCtx);
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "ES file: seeked back to byte 0 after index build";
    } else {
        av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "Frame index built:" << mFrameIndex.size() << "frames in"
                 << (mFrameIndex.isEmpty() ? 0 : mFrameIndex.last().gopIndex + 1) << "GOPs";
    }

    // Debug: Check first frame's fileOffset for ES files
    if (isES && !mFrameIndex.isEmpty()) {
        if (TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "First frame fileOffset:" << mFrameIndex[0].fileOffset
                     << "packetSize:" << mFrameIndex[0].packetSize;
        }
    }

}

// ----------------------------------------------------------------------------
// Scan: append one TTFrameInfo per video packet (raw — no field merging here)
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::scanPacketsIntoRawIndex(int videoStreamIndex)
{
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        setError("Could not allocate packet");
        return;
    }

    TTStreamInfo streamInfo = getStreamInfo(videoStreamIndex);
    int64_t estimatedFrames = streamInfo.numFrames > 0 ? streamInfo.numFrames : 10000;
    int64_t lastProgress = -1;
    int rawCount = 0;

    // Prefer byte-position progress over the frame-count estimate: raw ES
    // files report no frame count (estimatedFrames falls back to a fixed
    // 10000), so on real recordings (~360000 frames for a 2h capture) the
    // frame-based progress hits 100 at ~3% of the file and the `<= 100`
    // gate below then silences every further emission for the rest of the
    // scan. Byte position is monotonic and known up front for seekable
    // input, so it stays accurate for the whole scan.
    int64_t totalBytes = (mFormatCtx->pb) ? avio_size(mFormatCtx->pb) : -1;
    const bool useByteProgress = totalBytes > 0;
    int64_t lastValidPos = 0;

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Building frame index for stream" << videoStreamIndex;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Estimated frames:" << estimatedFrames << "totalBytes:" << totalBytes;

    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;

    // POC collection for the display-order map (H.26x only). POC arrives
    // emission-side (parser lags one packet); IDR is detected input-side.
    const bool collectPoc = (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC);
    TTPocCollector pocCollector(collectPoc ? codecId : AV_CODEC_ID_NONE);
    TTLeadingPicClassifier leadingClassifier(collectPoc ? codecId : AV_CODEC_ID_NONE);

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            TTFrameInfo info;
            info.pts        = packet->pts;
            info.dts        = packet->dts;
            info.fileOffset = packet->pos;
            info.packetSize = packet->size;
            info.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            info.frameIndex = -1;       // filled by finalizeFrameIndex
            info.gopIndex   = -1;       // filled by finalizeFrameIndex
            info.isFieldCoded = false;  // may be set true below

            if (collectPoc) {
                info.isIDR = TTPocCollector::packetIsIDR(packet->data, packet->size, codecId);
                info.isDroppedLeading = leadingClassifier.classifyPacket(packet->data, packet->size);
                pocCollector.feedPacket(packet->data, packet->size);
            }

            // Field detection (H.264 PAFF only)
            if (codecId == AV_CODEC_ID_H264 && !mH264FrameMbsOnlyFlag) {
                TTFieldInfo fi = parseH264FieldInfoFromPacket(packet->data, packet->size);
                if (fi.isField) {
                    mIsPAFF = true;
                    info.isFieldCoded  = true;
                    info.isBottomField = fi.isBottomField;
                    info.paffFrameNum  = fi.frameNum;
                }
            }

            // Frame type
            if (info.isKeyframe) {
                info.frameType = AV_PICTURE_TYPE_I;
            } else if (codecId == AV_CODEC_ID_H264) {
                int slice = TTNaluParser::parseH264SliceTypeFromPacket(packet->data, packet->size);
                info.frameType = (slice == H264::SLICE_B) ? AV_PICTURE_TYPE_B
                               : (slice == H264::SLICE_I) ? AV_PICTURE_TYPE_I
                                                          : AV_PICTURE_TYPE_P;
            } else if (codecId == AV_CODEC_ID_HEVC) {
                int slice = TTNaluParser::parseH265SliceTypeFromPacket(packet->data, packet->size);
                info.frameType = (slice == H265::SLICE_B) ? AV_PICTURE_TYPE_B
                               : (slice == H265::SLICE_I) ? AV_PICTURE_TYPE_I
                                                          : AV_PICTURE_TYPE_P;
            } else {
                info.frameType = AV_PICTURE_TYPE_P;
            }

            mFrameIndex.append(info);
            rawCount++;

            if (useByteProgress) {
                if (packet->pos >= 0)
                    lastValidPos = packet->pos;
                int64_t progress = qMin<int64_t>((lastValidPos * 100) / totalBytes, 100);
                if (progress != lastProgress) {
                    emit progressChanged(static_cast<int>(progress),
                        tr("Indexing frame %1...").arg(rawCount));
                    lastProgress = progress;
                }
            } else {
                // Non-seekable input: fall back to the frame-count estimate
                // (unchanged from before this fix — still subject to the
                // same >100% silencing when estimatedFrames underestimates,
                // but only reachable when byte-based progress is unavailable).
                int64_t progress = (rawCount * 100) / estimatedFrames;
                if (progress != lastProgress && progress <= 100) {
                    emit progressChanged(static_cast<int>(progress),
                        tr("Indexing frame %1...").arg(rawCount));
                    lastProgress = progress;
                }
            }
        }
        av_packet_unref(packet);
    }

    if (collectPoc) {
        pocCollector.finish();
        const QVector<int>& pocs = pocCollector.pocs();
        if (pocs.size() == mFrameIndex.size()) {
            for (int i = 0; i < pocs.size(); ++i)
                mFrameIndex[i].poc = pocs[i];
        } else {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("POC collection mismatch: %1 emissions for %2 packets "
                        "- display map falls back to identity")
                    .arg(pocs.size()).arg(mFrameIndex.size()));
        }
    }

    av_packet_free(&packet);
}

// ----------------------------------------------------------------------------
// PAFF post-processing: collapse adjacent top+bottom field pairs in-place
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::mergePAFFFieldsInIndex()
{
    mRawPacketCount = mFrameIndex.size();
    mRawToMerged.clear();
    if (!mIsPAFF) return;                 // identity map (empty)

    mRawToMerged.resize(mRawPacketCount);

    int w = 0;  // write index
    for (int r = 0; r < mFrameIndex.size(); ) {
        const TTFrameInfo& cur = mFrameIndex[r];
        bool merged = false;

        if (cur.isFieldCoded && !cur.isBottomField && r + 1 < mFrameIndex.size()) {
            const TTFrameInfo& next = mFrameIndex[r + 1];
            if (next.isFieldCoded && next.isBottomField &&
                next.paffFrameNum == cur.paffFrameNum)
            {
                // Merge: keep top's PTS/DTS/offset/type/keyframe/POC, sum packetSize
                TTFrameInfo merged_info = cur;
                merged_info.packetSize += next.packetSize;
                mFrameIndex[w] = merged_info;
                mRawToMerged[r]     = w;      // top field
                mRawToMerged[r + 1] = ~w;     // bottom field, collapsed
                w++;
                r += 2;
                merged = true;
            }
        }

        if (!merged) {
            if (w != r) mFrameIndex[w] = mFrameIndex[r];
            mRawToMerged[r] = w;
            w++;
            r++;
        }
    }
    while (mFrameIndex.size() > w) mFrameIndex.removeLast();
}

// ----------------------------------------------------------------------------
// Assign gopIndex (increments at each keyframe) and frameIndex (= position)
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::finalizeFrameIndex()
{
    int currentGOP = 0;
    for (int i = 0; i < mFrameIndex.size(); ++i) {
        if (i > 0 && mFrameIndex[i].isKeyframe) {
            currentGOP++;
        }
        mFrameIndex[i].gopIndex   = currentGOP;
        mFrameIndex[i].frameIndex = i;
    }
}

// ----------------------------------------------------------------------------
// Derive the display-order map from poc/isIDR collected during the scan
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::buildDisplayOrderMap()
{
    // Identity fallback covers: MPEG-2 (.m2v via wrapper), missing POC data,
    // degenerate parser output. Identity == pre-map behavior.
    auto identity = [this](const char* reason) {
        QVector<int> ranks(mFrameIndex.size());
        for (int i = 0; i < ranks.size(); ++i) ranks[i] = i;
        mDisplayOrderMap.buildFromRanks(ranks);
        if (reason && TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "display-order map: identity fallback -" << reason;
    };

    if (mFrameIndex.isEmpty()) { mDisplayOrderMap = TTDisplayOrderMap(); return; }
    if (mFrameIndex[0].poc == INT_MIN) { identity("no POC collected"); return; }

    QVector<TTPocEntry> entries(mFrameIndex.size());
    bool allSame = true;
    for (int i = 0; i < mFrameIndex.size(); ++i) {
        entries[i] = {mFrameIndex[i].poc, mFrameIndex[i].isIDR,
                      mFrameIndex[i].isDroppedLeading, mFrameIndex[i].isKeyframe};
        if (mFrameIndex[i].poc != mFrameIndex[0].poc) allSame = false;
    }
    if (allSame && mFrameIndex.size() > 1) { identity("constant POC"); return; }

    // H.264 open-GOP cold start: mark leading pics libav drops (mirrors the HEVC
    // RASL handling done input-side via TTLeadingPicClassifier). No-op otherwise.
    const AVCodecID codecId = mVideoCodecCtx ? mVideoCodecCtx->codec_id : AV_CODEC_ID_NONE;
    TTDisplayOrderMap::markH264ColdStartLeadingPics(entries, codecId);

    mDisplayOrderMap.build(entries);
    if (!mDisplayOrderMap.isValid()) identity("rank validation failed");
}

void TTFFmpegWrapper::setFrameIndexEntries(const QList<TTFrameInfo>& index)
{
    mFrameIndex = index;
    // Adopted index is already merged: the raw->merged view is identity here
    // (only the index owner ran mergePAFFFieldsInIndex and holds a real map).
    mRawPacketCount = mFrameIndex.size();
    mRawToMerged.clear();
    buildDisplayOrderMap();   // poc/isIDR/isDroppedLeading travel inside TTFrameInfo entries
}

void TTFFmpegWrapper::setFrameIndex(const TTFrameIndexBundle& bundle)
{
    adoptStreamMetadata(bundle.isPAFF, bundle.frameMbsOnlyFlag, bundle.log2MaxFrameNum);
    setFrameIndexEntries(bundle.index);   // rebuilds the display map from the adopted entries
}

TTFrameIndexBundle TTFFmpegWrapper::frameIndexBundle() const
{
    TTFrameIndexBundle b;
    b.index             = mFrameIndex;
    b.isPAFF            = mIsPAFF;
    b.frameMbsOnlyFlag  = mH264FrameMbsOnlyFlag;
    b.log2MaxFrameNum   = mH264Log2MaxFrameNum;
    return b;
}
