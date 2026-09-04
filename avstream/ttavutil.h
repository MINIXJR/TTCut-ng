/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAVUTIL_H
#define TTAVUTIL_H

#include <cstdint>
#include <QString>

// Forward declarations for libav types (avoid including C headers in .h)
struct AVFormatContext;
struct AVInputFormat;

// Free helpers over libav shared by TTFFmpegWrapper, TTAudioCutter and
// TTFrameIndexer. No state, no Qt objects.

// av_strerror() as a QString.
QString ttAvErrorToString(int errnum);

// ----------------------------------------------------------------------------
// Stream information structure
// ----------------------------------------------------------------------------
struct TTStreamInfo {
    int streamIndex = -1;
    int codecType = -1;     // AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, etc.
    int codecId = 0;        // AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, etc.
    QString codecName;      // "h264", "hevc", "mpeg2video", etc.

    // Video specific
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    int64_t bitRate = 0;
    int profile = 0;
    int level = 0;

    // Audio specific
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;

    // Common
    int64_t duration = 0;   // in stream timebase
    int64_t numFrames = 0;  // estimated frame count
};

// ----------------------------------------------------------------------------
// Video codec types (moved here from ttffmpegwrapper.h 2026-09-04 so the
// stream classes need no wrapper header for the enum)
// ----------------------------------------------------------------------------
enum TTVideoCodecType {
    CODEC_UNKNOWN = 0,
    CODEC_MPEG2,
    CODEC_H264,
    CODEC_H265
};

// "MPEG-2" / "H.264/AVC" / "H.265/HEVC" / "Unknown"
QString ttCodecTypeToString(TTVideoCodecType type);

// AV_CODEC_ID_* -> TTVideoCodecType (MPEG-2, H.264, HEVC; else CODEC_UNKNOWN).
TTVideoCodecType ttCodecTypeFromAvCodecId(int avCodecId);

// What a stream class needs to know about a video file at open time.
struct TTVideoProbe {
    TTVideoCodecType codecType = CODEC_UNKNOWN;
    int              videoStreamIndex = -1;   // av_find_best_stream(VIDEO); -1 = none
    TTStreamInfo     info;                    // of videoStreamIndex; default when none
};

// Opens filePath, reads codec type, best video stream and its TTStreamInfo,
// closes the file. Returns false only when the file cannot be opened or has
// no stream info (*error then describes it); a file without a video stream
// still returns true with videoStreamIndex == -1 and CODEC_UNKNOWN — the
// same distinction TTFFmpegWrapper::openFile()/detectVideoCodec() make.
bool ttProbeVideo(const QString& filePath, TTVideoProbe* out, QString* error);

// Recognises raw H.264/H.265/MPEG-2 elementary streams by extension.
bool ttIsElementaryStreamPath(const QString& filePath);
// Returns the libav input format ('h264', 'hevc', 'mpegvideo') matching
// the file extension, or nullptr for non-ES paths.
const AVInputFormat* ttEsInputFormatForPath(const QString& filePath);

// Opens filePath with avformat_open_input (ES paths get forced input format
// plus large probesize/analyzeduration) and runs avformat_find_stream_info.
// On failure *ctx is left null and, if error is non-null, *error holds a
// message describing the failure. Returns false on failure.
bool ttOpenInput(AVFormatContext** ctx, const QString& filePath, QString* error);

// TTStreamInfo for one stream of an already-open context.
TTStreamInfo ttStreamInfo(const AVFormatContext* ctx, int streamIndex);

#endif // TTAVUTIL_H
