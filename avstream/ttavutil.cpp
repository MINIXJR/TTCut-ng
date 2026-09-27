/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttavutil.h"
#include "../common/ttsettings.h"

#include <QDebug>
#include <QFileInfo>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

QString ttAvErrorToString(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return QString::fromUtf8(errbuf);
}

QString ttCodecTypeToString(TTVideoCodecType type)
{
    switch (type) {
        case CODEC_MPEG2: return "MPEG-2";
        case CODEC_H264:  return "H.264/AVC";
        case CODEC_H265:  return "H.265/HEVC";
        default:          return "Unknown";
    }
}

TTVideoCodecType ttCodecTypeFromAvCodecId(int avCodecId)
{
    switch (avCodecId) {
        case AV_CODEC_ID_MPEG2VIDEO: return CODEC_MPEG2;
        case AV_CODEC_ID_H264:       return CODEC_H264;
        case AV_CODEC_ID_HEVC:       return CODEC_H265;
        default:                     return CODEC_UNKNOWN;
    }
}

bool ttProbeVideo(const QString& filePath, TTVideoProbe* out, QString* error)
{
    AVFormatContext* ctx = nullptr;
    if (!ttOpenInput(&ctx, filePath, error))
        return false;

    TTVideoProbe probe;
    probe.videoStreamIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (probe.videoStreamIndex >= 0) {
        probe.info      = ttStreamInfo(ctx, probe.videoStreamIndex);
        probe.codecType = ttCodecTypeFromAvCodecId(probe.info.codecId);
    }
    avformat_close_input(&ctx);

    if (out) *out = probe;
    return true;
}

// ----------------------------------------------------------------------------
// Elementary-stream detection (shared with TTMkvMergeProvider)
// ----------------------------------------------------------------------------
bool ttIsElementaryStreamPath(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return (suffix == "264" || suffix == "h264" ||
            suffix == "265" || suffix == "h265" || suffix == "hevc" ||
            suffix == "m2v" || suffix == "mpv");
}

const AVInputFormat* ttEsInputFormatForPath(const QString& filePath)
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
// Open media file
// ----------------------------------------------------------------------------
bool ttOpenInput(AVFormatContext** ctx, const QString& filePath, QString* error)
{
    // Check if this is an elementary stream (by extension)
    bool isES = ttIsElementaryStreamPath(filePath);

    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    if (isES) {
        // For elementary streams, we need special handling
        // Set large probesize and analyzeduration for proper detection
        av_dict_set(&opts, "probesize", "50000000", 0);  // 50MB
        av_dict_set(&opts, "analyzeduration", "10000000", 0);  // 10 seconds
        inputFmt = ttEsInputFormatForPath(filePath);
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "Opening ES file with forced format:" << (inputFmt ? inputFmt->name : "auto");
    }

    int ret = avformat_open_input(ctx, filePath.toUtf8().constData(),
                                   inputFmt, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        if (error) *error = QString("Could not open file: %1").arg(ttAvErrorToString(ret));
        return false;
    }

    // For ES files, set larger analyze duration
    if (isES) {
        (*ctx)->max_analyze_duration = 10 * AV_TIME_BASE;  // 10 seconds
        (*ctx)->probesize = 50000000;  // 50MB
    }

    ret = avformat_find_stream_info(*ctx, nullptr);
    if (ret < 0) {
        if (error) *error = QString("Could not find stream info: %1").arg(ttAvErrorToString(ret));
        avformat_close_input(ctx);
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Get stream information
// ----------------------------------------------------------------------------
TTStreamInfo ttStreamInfo(const AVFormatContext* ctx, int streamIndex)
{
    TTStreamInfo info = {};

    if (!ctx || streamIndex < 0 ||
        streamIndex >= static_cast<int>(ctx->nb_streams)) {
        return info;
    }

    AVStream* stream = ctx->streams[streamIndex];
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

        // Frame rate = the SPS/VPS timing the libav parser read
        // (h264_parser.c: time_scale / (2 * num_units_in_tick); hevc/parser.c:
        // VPS or VUI timing), copied here by avformat_find_stream_info; 0 =
        // unknown. r_frame_rate and avg_frame_rate carry nothing for a raw ES
        // (measured 2026-09-27, libav 9.0.2): r_frame_rate is twice the frame
        // rate for raw H.264 progressive and MBAFF, the field rate for PAFF
        // and 1200000/1 without SPS timing; avg_frame_rate is the raw
        // demuxer's "framerate" option (default 25).
        // TTFrameIndexer::effectiveFrameRate decides between .info, this
        // value and an assumed 25.
        if (codecpar->framerate.num > 0 && codecpar->framerate.den > 0)
            info.frameRate = av_q2d(codecpar->framerate);

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
