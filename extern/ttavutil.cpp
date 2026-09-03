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
