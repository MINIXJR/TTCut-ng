/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttmkvmergeprovider.cpp                                          */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMKVMERGEPROVIDER
// MKV muxer using libav matroska output format
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

#include "ttmkvmergeprovider.h"
#include "../avstream/ttnaluparser.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDir>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace {

// Returns true if the byte at `b` starts a Video Coding Layer NAL unit
// for the given codec. `b` must point to the first NAL payload byte
// (after the start code).
//
// H.264: 1-byte header, 5-bit nal_unit_type in bits 0-4.
//        VCL types: 1 (non-IDR slice), 5 (IDR slice).
// H.265: 2-byte header, 6-bit nal_unit_type in bits 1-6 of first byte.
//        VCL types: 0-31 per HEVC spec.
bool isVclNalByte(enum AVCodecID codec, const uint8_t* b)
{
    switch (codec) {
        case AV_CODEC_ID_H264: {
            uint8_t nt = b[0] & 0x1F;
            return nt == H264::NAL_SLICE || nt == H264::NAL_IDR_SLICE;
        }
        case AV_CODEC_ID_HEVC: {
            uint8_t nt = (b[0] >> 1) & 0x3F;
            return nt <= 31;
        }
        case AV_CODEC_ID_MPEG2VIDEO:
            // MPEG-2 ES has no NAL layer; treat every packet as "VCL" so
            // it is passed through to the matroska writer unchanged.
            return true;
        default:
            Q_ASSERT_X(false, "isVclNalByte",
                       "unexpected video codec in MKV ES mux path");
            return false;
    }
}

} // namespace

// ----------------------------------------------------------------------------
// Helper: libav error code to QString
// ----------------------------------------------------------------------------
static QString avErrStr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// ----------------------------------------------------------------------------
// Decode VDR's Windows-1252 hex encoding (#XX) in filenames
// ----------------------------------------------------------------------------
static QChar win1252ToUnicode(unsigned char byte)
{
    static const ushort map[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,  // 88-8F
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178   // 98-9F
    };
    if (byte >= 0x80 && byte <= 0x9F)
        return QChar(map[byte - 0x80]);
    return QChar(byte);
}

static QString decodeVdrName(const QString& name)
{
    QString result;
    result.reserve(name.size());

    for (int i = 0; i < name.size(); ++i) {
        if (name[i] == QChar('#') && i + 2 < name.size()) {
            bool ok;
            uint val = name.mid(i + 1, 2).toUInt(&ok, 16);
            if (ok && val >= 0x20) {
                result += win1252ToUnicode(static_cast<unsigned char>(val));
                i += 2;
                continue;
            }
        }
        result += (name[i] == QChar('_')) ? QChar(' ') : name[i];
    }

    return result;
}

// ----------------------------------------------------------------------------
// Check if file is a raw elementary stream (needs special demuxer handling)
// ----------------------------------------------------------------------------
static bool isElementaryStream(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return (suffix == "264" || suffix == "h264" ||
            suffix == "265" || suffix == "h265" || suffix == "hevc" ||
            suffix == "m2v" || suffix == "mpv");
}

// ----------------------------------------------------------------------------
// Open input file with appropriate format detection
// For ES files, forces the correct demuxer format
// ----------------------------------------------------------------------------
static AVFormatContext* openInput(const QString& filePath, int& ret)
{
    AVFormatContext* fmtCtx = nullptr;
    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    if (isElementaryStream(filePath)) {
        QString suffix = QFileInfo(filePath).suffix().toLower();
        av_dict_set(&opts, "probesize", "50000000", 0);
        av_dict_set(&opts, "analyzeduration", "10000000", 0);

        if (suffix == "264" || suffix == "h264")
            inputFmt = av_find_input_format("h264");
        else if (suffix == "265" || suffix == "h265" || suffix == "hevc")
            inputFmt = av_find_input_format("hevc");
        else if (suffix == "m2v" || suffix == "mpv")
            inputFmt = av_find_input_format("mpegvideo");
    }

    ret = avformat_open_input(&fmtCtx, filePath.toUtf8().constData(), inputFmt, &opts);
    av_dict_free(&opts);

    if (ret < 0)
        return nullptr;

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return nullptr;
    }

    return fmtCtx;
}

// ----------------------------------------------------------------------------
// Check if format is a container (MKV, MP4, TS) vs raw ES
// Used to distinguish container remux from ES mux mode
// ----------------------------------------------------------------------------
static bool isContainerFormat(const AVFormatContext* fmtCtx)
{
    if (!fmtCtx || !fmtCtx->iformat)
        return false;

    QString fmtName = QString::fromUtf8(fmtCtx->iformat->name);
    return fmtName.contains("matroska") || fmtName.contains("webm") ||
           fmtName.contains("mp4") || fmtName.contains("mov") ||
           fmtName.contains("mpegts") || fmtName.contains("avi");
}

// ----------------------------------------------------------------------------
// MuxInput: one input stream for the interleaved mux loop
// ----------------------------------------------------------------------------
struct MuxInput {
    AVFormatContext* fmtCtx;
    int srcIdx;         // Stream index in source file
    int outIdx;         // Stream index in output file
    AVPacket* pkt;
    bool eof;
    int64_t syncMs;     // Sync offset in milliseconds
    bool assignPts;     // True = assign PTS from frameCount (raw ES video)
    int64_t frameDur;   // Frame duration in output time_base units
    int64_t frameCount; // Frame counter for PTS assignment
    bool ownsCtx;       // True = this MuxInput owns the AVFormatContext (for cleanup)
    MuxInput()
        : fmtCtx(nullptr), srcIdx(-1), outIdx(-1), pkt(nullptr), eof(false)
        , syncMs(0), assignPts(false), frameDur(0), frameCount(0)
        , ownsCtx(false) {}
};

// Scan a packet for an H.264 SPS NAL (type 7) and extract log2_max_frame_num.
// This is needed because Smart Cut output contains TWO different SPS:
//   1. Encoder SPS (x264 MBAFF, log2_max_frame_num=4)
//   2. Source SPS  (PAFF stream, log2_max_frame_num=9)
// The muxer must use the correct log2_max_frame_num for field_pic_flag parsing,
// otherwise it reads field_pic_flag at the wrong bit position and may falsely
// detect re-encoded frames as field packets, causing frame merging corruption.
static bool parseInlineSpsLog2MaxFrameNum(const uint8_t* data, int size, int& log2MaxFrameNum)
{
    for (int p = 0; p < size - 5; p++) {
        if (data[p] != 0 || data[p+1] != 0) continue;
        int s = -1;
        if (data[p+2] == 1) s = p + 3;
        else if (data[p+2] == 0 && p+3 < size && data[p+3] == 1) s = p + 4;
        if (s < 0 || s >= size) continue;
        uint8_t nt = data[s] & 0x1F;
        if (nt != 7) continue;  // Not SPS

        // Parse SPS: skip NAL header, profile_idc, constraint_flags, level_idc
        const uint8_t* sps = data + s + 1;
        int spsSz = size - s - 1;
        if (spsSz < 4) continue;

        uint8_t profile_idc = sps[0];
        int bp = 24;  // After profile_idc(8) + constraint(8) + level(8)

        // sps_id
        TTNaluParser::readExpGolombUE(sps, spsSz, bp);

        // High profile: chroma, bit depth, etc.
        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
            profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
            profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135) {
            uint32_t chroma = TTNaluParser::readExpGolombUE(sps, spsSz, bp);
            if (chroma == 3)
                TTNaluParser::readBits(sps, spsSz, bp, 1); // separate_colour_plane
            TTNaluParser::readExpGolombUE(sps, spsSz, bp); // bit_depth_luma
            TTNaluParser::readExpGolombUE(sps, spsSz, bp); // bit_depth_chroma
            TTNaluParser::readBits(sps, spsSz, bp, 1);     // qpprime_y_zero
            uint32_t scaling = TTNaluParser::readBits(sps, spsSz, bp, 1);
            if (scaling) {
                int cnt = (chroma != 3) ? 8 : 12;
                for (int i = 0; i < cnt; i++) {
                    uint32_t present = TTNaluParser::readBits(sps, spsSz, bp, 1);
                    if (present) {
                        int sz = (i < 6) ? 16 : 64;
                        int lastScale = 8, nextScale = 8;
                        for (int j = 0; j < sz; j++) {
                            if (nextScale != 0) {
                                // Read signed exp-golomb (delta_scale)
                                int delta = TTNaluParser::readExpGolombSE(sps, spsSz, bp);
                                nextScale = (lastScale + delta + 256) % 256;
                            }
                            lastScale = (nextScale == 0) ? lastScale : nextScale;
                        }
                    }
                }
            }
        }

        uint32_t l2mfn = TTNaluParser::readExpGolombUE(sps, spsSz, bp);
        log2MaxFrameNum = static_cast<int>(l2mfn) + 4;
        return true;
    }
    return false;
}

// Read next packet matching srcIdx from this input
static bool readNextPacket(MuxInput& in)
{
    while (av_read_frame(in.fmtCtx, in.pkt) >= 0) {
        if (in.pkt->stream_index == in.srcIdx)
            return true;
        av_packet_unref(in.pkt);
    }
    in.eof = true;
    return false;
}

// Get normalized PTS in AV_TIME_BASE for comparison across inputs
static int64_t getNormalizedPts(const MuxInput& in, const AVFormatContext* outCtx)
{
    int64_t pts;
    if (in.assignPts) {
        // PTS from frame count in output time_base → rescale to AV_TIME_BASE
        pts = av_rescale_q(in.frameCount * in.frameDur,
            outCtx->streams[in.outIdx]->time_base, AV_TIME_BASE_Q);
    } else if (in.pkt->pts != AV_NOPTS_VALUE) {
        pts = av_rescale_q(in.pkt->pts,
            in.fmtCtx->streams[in.srcIdx]->time_base, AV_TIME_BASE_Q);
    } else {
        pts = 0;
    }
    return pts + in.syncMs * 1000;  // ms → µs (AV_TIME_BASE = µs)
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TTMkvMergeProvider::TTMkvMergeProvider()
    : QObject()
    , mAudioSyncOffsetMs(0)
    , mVideoSyncOffsetMs(0)
    , mTotalDurationMs(0)
    , mIsPAFF(false)
    , mH264Log2MaxFrameNum(4)
    , mVideoCodecId(AV_CODEC_ID_NONE)
{
}

void TTMkvMergeProvider::setTotalDurationMs(qint64 durationMs)
{
    mTotalDurationMs = durationMs;
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
TTMkvMergeProvider::~TTMkvMergeProvider()
{
}

// -----------------------------------------------------------------------------
// Always available (libav is linked at build time)
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::isAvailable() const
{
    return true;
}

bool TTMkvMergeProvider::isMkvMergeInstalled()
{
    return true;  // libav matroska muxer is always available
}

QString TTMkvMergeProvider::mkvMergeVersion()
{
    return QString("libav (built-in)");
}

QString TTMkvMergeProvider::mkvMergePath()
{
    return QString();
}

// -----------------------------------------------------------------------------
// Option setters (same API as before)
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setDefaultDuration(const QString& trackId, const QString& duration)
{
    int id = trackId.toInt();
    mTrackOptions[id].defaultDuration = duration;
    qDebug() << "TTMkvMergeProvider: default duration for track" << id << "=" << duration;
}

void TTMkvMergeProvider::setTrackName(int trackId, const QString& name)
{
    mTrackOptions[trackId].name = name;
}

void TTMkvMergeProvider::setLanguage(int trackId, const QString& lang)
{
    mTrackOptions[trackId].language = lang;
}

void TTMkvMergeProvider::setChapterFile(const QString& chapterFile)
{
    // Defensive: reject NUL/control bytes regardless of caller. The current
    // callers all hand in paths produced internally by generateChapterFile(),
    // but a future caller could plumb in user-controlled data.
    for (QChar c : chapterFile) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F) {
            qWarning() << "setChapterFile: rejecting path with control bytes";
            return;
        }
    }
    mChapterFile = chapterFile;
}

void TTMkvMergeProvider::setAudioLanguages(const QStringList& languages)
{
    mAudioLanguages = languages;
}

void TTMkvMergeProvider::setSubtitleLanguages(const QStringList& languages)
{
    mSubtitleLanguages = languages;
}

void TTMkvMergeProvider::setAudioSyncOffset(int offsetMs)
{
    mAudioSyncOffsetMs = offsetMs;
    if (offsetMs != 0)
        qDebug() << "TTMkvMergeProvider: audio sync offset" << offsetMs << "ms";
}

void TTMkvMergeProvider::setVideoSyncOffset(int offsetMs)
{
    mVideoSyncOffsetMs = offsetMs;
    if (offsetMs != 0)
        qDebug() << "TTMkvMergeProvider: video sync offset" << offsetMs << "ms";
}

// -----------------------------------------------------------------------------
// Parse OGM chapter file and add chapters to output context
// -----------------------------------------------------------------------------
static void addChaptersFromFile(AVFormatContext* outCtx, const QString& chapterFile, int64_t totalDurationMs)
{
    QFile cf(chapterFile);
    if (!cf.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QRegularExpression chapterRe("CHAPTER(\\d+)=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{3})");
    QRegularExpression nameRe("CHAPTER(\\d+)NAME=(.+)");
    QTextStream in(&cf);
    QList<QPair<int64_t, QString>> chapters;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QRegularExpressionMatch tm = chapterRe.match(line);
        if (tm.hasMatch()) {
            int64_t ms = tm.captured(2).toInt() * 3600000LL
                       + tm.captured(3).toInt() * 60000LL
                       + tm.captured(4).toInt() * 1000LL
                       + tm.captured(5).toInt();
            chapters.append({ms, QString()});
        }
        QRegularExpressionMatch nm = nameRe.match(line);
        if (nm.hasMatch() && !chapters.isEmpty()) {
            chapters.last().second = nm.captured(2).trimmed();
        }
    }

    if (chapters.isEmpty())
        return;

    outCtx->chapters = (AVChapter**)av_malloc(chapters.size() * sizeof(AVChapter*));
    if (!outCtx->chapters) {
        qWarning() << "av_malloc failed for chapter array — chapters dropped";
        return;
    }
    outCtx->nb_chapters = 0;  // populated below; only set for entries actually allocated
    for (int i = 0; i < chapters.size(); i++) {
        AVChapter* ch = (AVChapter*)av_mallocz(sizeof(AVChapter));
        if (!ch) {
            qWarning() << "av_mallocz failed for chapter" << i << "— remaining chapters dropped";
            break;
        }
        ch->id = i;
        ch->time_base = {1, 1000};
        ch->start = chapters[i].first;
        ch->end = (i + 1 < chapters.size()) ? chapters[i + 1].first - 1
                 : (totalDurationMs > 0 ? totalDurationMs : ch->start + 300000);
        if (!chapters[i].second.isEmpty()) {
            av_dict_set(&ch->metadata, "title",
                         chapters[i].second.toUtf8().constData(), 0);
        }
        outCtx->chapters[i] = ch;
        outCtx->nb_chapters = i + 1;
    }

    qDebug() << "Added" << chapters.size() << "chapters from" << chapterFile;
}

// -----------------------------------------------------------------------------
// Main muxing function
// Two modes:
//   1. Container remux: MKV input → copy all streams, add chapters
//   2. ES mux: video ES + separate audio/subtitle → interleaved MKV
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::mux(const QString& outputFile,
                              const QString& videoFile,
                              const QStringList& audioFiles,
                              const QStringList& subtitleFiles)
{
    if (videoFile.isEmpty() || !QFile::exists(videoFile)) {
        setError(QString("Video file not found: %1").arg(videoFile));
        return false;
    }

    qDebug() << "TTMkvMergeProvider::mux (libav matroska)";
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  MKV mux: videoCodecId =" << avcodec_get_name(static_cast<AVCodecID>(mVideoCodecId));
    qDebug() << "  Video:" << videoFile
             << "size:" << QFileInfo(videoFile).size() << "bytes";
    for (int i = 0; i < audioFiles.size(); i++) {
        qDebug() << "  Audio" << i << ":" << audioFiles[i]
                 << "size:" << QFileInfo(audioFiles[i]).size() << "bytes";
    }

    // Open video/main input
    int ret = 0;
    AVFormatContext* videoInCtx = openInput(videoFile, ret);
    if (!videoInCtx) {
        setError(QString("Cannot open video: %1 (%2)")
                     .arg(videoFile, avErrStr(ret)));
        return false;
    }

    bool containerRemux = isContainerFormat(videoInCtx) &&
                          audioFiles.isEmpty() && subtitleFiles.isEmpty();

    // Create matroska output context
    AVFormatContext* outCtx = nullptr;
    ret = avformat_alloc_output_context2(&outCtx, nullptr, "matroska",
                                          outputFile.toUtf8().constData());
    if (ret < 0 || !outCtx) {
        avformat_close_input(&videoInCtx);
        setError("Cannot create matroska output context");
        return false;
    }

    // Set title metadata from output filename, stripping "_cut" suffix
    QString baseName = QFileInfo(outputFile).completeBaseName();
    if (baseName.endsWith("_cut")) baseName.chop(4);
    QString title = decodeVdrName(baseName);
    if (!title.isEmpty()) {
        av_dict_set(&outCtx->metadata, "title", title.toUtf8().constData(), 0);
    }

    // Build list of MuxInputs and output streams
    QList<MuxInput> inputs;
    bool success = false;
    int64_t videoDurationNs = 0;

    if (containerRemux) {
        // --- Container remux mode ---
        // Copy all streams from input container (video + audio + subs)
        qDebug() << "  Mode: container remux (" << videoInCtx->nb_streams << "streams)";

        for (unsigned i = 0; i < videoInCtx->nb_streams; i++) {
            AVStream* outStream = avformat_new_stream(outCtx, nullptr);
            if (!outStream) continue;

            avcodec_parameters_copy(outStream->codecpar,
                                     videoInCtx->streams[i]->codecpar);
            outStream->time_base = videoInCtx->streams[i]->time_base;
            outStream->sample_aspect_ratio = videoInCtx->streams[i]->sample_aspect_ratio;
            av_dict_copy(&outStream->metadata,
                          videoInCtx->streams[i]->metadata, 0);
        }

        // Single MuxInput that reads ALL streams (srcIdx = -1 means accept all)
        // Handled separately below since all streams share one AVFormatContext

    } else {
        // --- ES mux mode ---
        // Video + separate audio + subtitle files, interleaved
        qDebug() << "  Mode: ES mux";

        // Video stream
        int videoIdx = av_find_best_stream(videoInCtx, AVMEDIA_TYPE_VIDEO,
                                            -1, -1, nullptr, 0);
        if (videoIdx < 0) {
            avformat_close_input(&videoInCtx);
            avformat_free_context(outCtx);
            setError("No video stream in input");
            return false;
        }

        AVStream* videoOut = avformat_new_stream(outCtx, nullptr);
        avcodec_parameters_copy(videoOut->codecpar,
                                 videoInCtx->streams[videoIdx]->codecpar);
        videoOut->time_base = videoInCtx->streams[videoIdx]->time_base;

        // Copy SAR to stream level (matroska muxer uses stream SAR, not codecpar SAR)
        if (videoOut->codecpar->sample_aspect_ratio.num > 0)
            videoOut->sample_aspect_ratio = videoOut->codecpar->sample_aspect_ratio;

        MuxInput vin;
        vin.fmtCtx = videoInCtx;
        vin.srcIdx = videoIdx;
        vin.outIdx = 0;
        vin.syncMs = mVideoSyncOffsetMs;
        vin.ownsCtx = false;  // We'll close videoInCtx at the end
        vin.pkt = av_packet_alloc();
        if (!vin.pkt) { qDebug() << "av_packet_alloc failed for video input"; goto cleanup; }

        // Frame duration from setDefaultDuration (e.g. "40000000ns" for 25fps)
        // NOTE: frameDur is recalculated AFTER avformat_write_header() because
        // the matroska muxer changes time_base during header write (e.g. to 1/1000)
        if (mTrackOptions.contains(0) && !mTrackOptions[0].defaultDuration.isEmpty()) {
            QString dur = mTrackOptions[0].defaultDuration;
            if (dur.endsWith("ns")) {
                videoDurationNs = dur.left(dur.length() - 2).toLongLong();
                if (videoDurationNs > 0) {
                    vin.assignPts = true;
                    videoOut->r_frame_rate = av_make_q(1000000000, (int)videoDurationNs);
                    videoOut->avg_frame_rate = videoOut->r_frame_rate;
                }
            }
        }

        inputs.append(vin);

        // Audio streams
        QRegularExpression langRe("_([a-z]{3})(?:_\\d+)?$");
        int outStreamIdx = 1;

        for (int i = 0; i < audioFiles.size(); i++) {
            if (!QFile::exists(audioFiles[i])) continue;

            AVFormatContext* audioCtx = openInput(audioFiles[i], ret);
            if (!audioCtx) continue;

            int audioIdx = av_find_best_stream(audioCtx, AVMEDIA_TYPE_AUDIO,
                                                -1, -1, nullptr, 0);
            if (audioIdx < 0) {
                avformat_close_input(&audioCtx);
                continue;
            }

            AVStream* audioOut = avformat_new_stream(outCtx, nullptr);
            avcodec_parameters_copy(audioOut->codecpar,
                                     audioCtx->streams[audioIdx]->codecpar);
            audioOut->time_base = audioCtx->streams[audioIdx]->time_base;

            // Language metadata
            QString lang;
            if (i < mAudioLanguages.size() && !mAudioLanguages[i].isEmpty()) {
                lang = mAudioLanguages[i];
            } else {
                QRegularExpressionMatch m = langRe.match(
                    QFileInfo(audioFiles[i]).completeBaseName());
                if (m.hasMatch()) lang = m.captured(1);
            }
            if (!lang.isEmpty()) {
                av_dict_set(&audioOut->metadata, "language",
                             lang.toUtf8().constData(), 0);
            }

            MuxInput ain;
            ain.fmtCtx = audioCtx;
            ain.srcIdx = audioIdx;
            ain.outIdx = outStreamIdx++;
            {
                // A/V sync offset from .info file (e.g. DVB stream A/V misalignment).
                // Per-track user delay is already baked into the cut audio file's
                // keepList times — do NOT add it here again.
                ain.syncMs = mAudioSyncOffsetMs;
            }
            ain.ownsCtx = true;
            ain.pkt = av_packet_alloc();
            if (!ain.pkt) { qDebug() << "av_packet_alloc failed for audio input"; avformat_close_input(&audioCtx); continue; }
            inputs.append(ain);

            qDebug() << "  Audio" << i << ":" << audioFiles[i]
                     << "lang=" << lang << "outIdx=" << ain.outIdx;
        }

        // Subtitle streams
        for (int i = 0; i < subtitleFiles.size(); i++) {
            if (!QFile::exists(subtitleFiles[i])) continue;

            AVFormatContext* subCtx = openInput(subtitleFiles[i], ret);
            if (!subCtx) continue;

            int subIdx = av_find_best_stream(subCtx, AVMEDIA_TYPE_SUBTITLE,
                                              -1, -1, nullptr, 0);
            if (subIdx < 0) {
                avformat_close_input(&subCtx);
                continue;
            }

            AVStream* subOut = avformat_new_stream(outCtx, nullptr);
            avcodec_parameters_copy(subOut->codecpar,
                                     subCtx->streams[subIdx]->codecpar);
            subOut->time_base = subCtx->streams[subIdx]->time_base;

            QString lang;
            if (i < mSubtitleLanguages.size() && !mSubtitleLanguages[i].isEmpty()) {
                lang = mSubtitleLanguages[i];
            }
            if (!lang.isEmpty()) {
                av_dict_set(&subOut->metadata, "language",
                             lang.toUtf8().constData(), 0);
            }

            MuxInput sin;
            sin.fmtCtx = subCtx;
            sin.srcIdx = subIdx;
            sin.outIdx = outStreamIdx++;
            sin.ownsCtx = true;
            sin.pkt = av_packet_alloc();
            if (!sin.pkt) { qDebug() << "av_packet_alloc failed for subtitle input"; avformat_close_input(&subCtx); continue; }
            inputs.append(sin);

            qDebug() << "  Subtitle" << i << ":" << subtitleFiles[i];
        }
    }

    // Add chapters if chapter file is set
    if (!mChapterFile.isEmpty() && QFile::exists(mChapterFile)) {
        addChaptersFromFile(outCtx, mChapterFile, mTotalDurationMs);
    }

    // Open output file
    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(),
                         AVIO_FLAG_WRITE);
        if (ret < 0) {
            setError(QString("Cannot open output: %1").arg(avErrStr(ret)));
            goto cleanup;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        setError(QString("Cannot write header: %1").arg(avErrStr(ret)));
        goto cleanup;
    }

    // ---- Recalculate frameDur after write_header (muxer may change time_base) ----
    if (!containerRemux && videoDurationNs > 0 && !inputs.isEmpty() && inputs[0].assignPts) {
        MuxInput& vin = inputs[0];
        AVStream* videoOut = outCtx->streams[vin.outIdx];
        vin.frameDur = av_rescale_q(videoDurationNs,
            AVRational{1, 1000000000}, videoOut->time_base);
        qDebug() << "  Video: frame duration" << videoDurationNs << "ns ="
                 << vin.frameDur << "tb-units (time_base"
                 << videoOut->time_base.num << "/" << videoOut->time_base.den
                 << "after write_header)";
    }

    // ---- Write packets ----

    if (containerRemux) {
        // Container remux: simple loop, copy all packets
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) { qDebug() << "av_packet_alloc failed for container remux"; goto cleanup; }
        int64_t totalSize = avio_size(videoInCtx->pb);
        int lastPercent = -1;

        while (av_read_frame(videoInCtx, pkt) >= 0) {
            unsigned srcIdx = pkt->stream_index;
            if (srcIdx >= outCtx->nb_streams) {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_rescale_ts(pkt,
                videoInCtx->streams[srcIdx]->time_base,
                outCtx->streams[srcIdx]->time_base);
            pkt->pos = -1;
            av_interleaved_write_frame(outCtx, pkt);

            if (totalSize > 0) {
                int percent = (int)(avio_tell(videoInCtx->pb) * 100 / totalSize);
                if (percent != lastPercent) {
                    lastPercent = percent;
                    emit progressChanged(percent, tr("Muxing..."));
                }
            }
        }
        av_packet_free(&pkt);

    } else {
        // ES mux: interleaved multi-input loop
        int64_t totalVideoSize = avio_size(videoInCtx->pb);
        int lastPercent = -1;
        int64_t totalPacketsWritten = 0;

        // Track active SPS log2_max_frame_num for correct PAFF field_pic_flag parsing.
        // Smart Cut ES files contain two SPS: encoder (log2_max_frame_num=4) then
        // source (log2_max_frame_num=9). Using the wrong width causes false field
        // detection and frame merging corruption.
        int activeLog2MaxFrameNum = mH264Log2MaxFrameNum;

        qDebug() << "  ES mux: totalVideoSize=" << totalVideoSize
                 << "inputs=" << inputs.size();

        // Read first packet from each input
        for (int i = 0; i < inputs.size(); i++) {
            bool got = readNextPacket(inputs[i]);
            qDebug() << "    Input" << i << ": first read ="
                     << (got ? "OK" : "EOF")
                     << "srcIdx=" << inputs[i].srcIdx
                     << "assignPts=" << inputs[i].assignPts;
        }

        // Write packets in PTS order
        while (true) {
            int bestIdx = -1;
            int64_t bestPts = INT64_MAX;

            for (int i = 0; i < inputs.size(); i++) {
                if (inputs[i].eof) continue;
                int64_t npts = getNormalizedPts(inputs[i], outCtx);
                if (npts < bestPts) {
                    bestPts = npts;
                    bestIdx = i;
                }
            }

            if (bestIdx < 0) break;  // All inputs exhausted

            MuxInput& in = inputs[bestIdx];

            // Assign or rescale PTS
            if (in.assignPts) {
                // PAFF detection: re-encoded frames are single packets (MBAFF),
                // stream-copied frames may be 2 field packets (PAFF).
                // Parse field_pic_flag from each video packet to distinguish.
                //
                // IMPORTANT: Track inline SPS to use correct log2_max_frame_num.
                // Smart Cut ES contains encoder SPS (log2_max_frame_num=4) followed
                // by source SPS (log2_max_frame_num=9). Using the wrong width causes
                // field_pic_flag to be read at the wrong bit position.
                bool isFieldPacket = false;
                bool hasVclNal = false;
                if (mIsPAFF && in.outIdx == 0 && in.pkt->data && in.pkt->size > 4) {
                    const uint8_t* d = in.pkt->data;
                    int sz = in.pkt->size;

                    // Update log2_max_frame_num from inline SPS if present
                    int newL2mfn = 0;
                    if (parseInlineSpsLog2MaxFrameNum(d, sz, newL2mfn)) {
                        if (newL2mfn != activeLog2MaxFrameNum) {
                            qDebug() << "  MKV PAFF: SPS change log2_max_frame_num"
                                     << activeLog2MaxFrameNum << "->" << newL2mfn
                                     << "at packet" << totalPacketsWritten;
                            activeLog2MaxFrameNum = newL2mfn;
                        }
                    }

                    // Find VCL NAL and parse field_pic_flag (H.264-only path)
                    int nalStart = -1;
                    for (int p = 0; p < sz - 4; p++) {
                        if (d[p] == 0 && d[p+1] == 0) {
                            int s = -1;
                            if (d[p+2] == 1) s = p + 3;
                            else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
                            if (s >= 0 && s < sz && isVclNalByte(AV_CODEC_ID_H264, d + s)) {
                                nalStart = s; break;
                            }
                        }
                    }
                    if (nalStart < 0 && sz >= 1 && isVclNalByte(AV_CODEC_ID_H264, d)) {
                        nalStart = 0;
                    }
                    hasVclNal = (nalStart >= 0);
                    if (hasVclNal) {
                        const uint8_t* nal = d + nalStart;
                        int nalSz = sz - nalStart;
                        int bp = 8;
                        TTNaluParser::readExpGolombUE(nal, nalSz, bp); // first_mb
                        TTNaluParser::readExpGolombUE(nal, nalSz, bp); // slice_type
                        TTNaluParser::readExpGolombUE(nal, nalSz, bp); // pps_id
                        TTNaluParser::readBits(nal, nalSz, bp, activeLog2MaxFrameNum); // frame_num
                        isFieldPacket = (TTNaluParser::readBits(nal, nalSz, bp, 1) == 1); // field_pic_flag
                    }
                } else if (in.outIdx == 0 && in.pkt->data && in.pkt->size > 0) {
                    // Non-PAFF video: check for VCL NAL (codec-aware)
                    const uint8_t* d = in.pkt->data;
                    int sz = in.pkt->size;
                    const AVCodecID codec = static_cast<AVCodecID>(mVideoCodecId);
                    for (int p = 0; p < sz - 3; p++) {
                        if (d[p] == 0 && d[p+1] == 0) {
                            int s = -1;
                            if (d[p+2] == 1) s = p + 3;
                            else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
                            if (s >= 0 && s < sz && isVclNalByte(codec, d + s)) {
                                hasVclNal = true; break;
                            }
                        }
                    }
                    if (!hasVclNal && sz >= 1 && isVclNalByte(codec, d)) {
                        hasVclNal = true;
                    }
                } else {
                    // Audio/subtitle or empty: always write
                    hasVclNal = true;
                }

                // Skip non-VCL video packets (EOS, SPS/PPS-only).
                // These contain no video frames and must not increment frameCount,
                // otherwise PTS gets shifted by phantom frames.
                if (in.outIdx == 0 && !hasVclNal) {
                    qDebug() << "  MKV PAFF: skip non-VCL video packet"
                             << totalPacketsWritten << "sz=" << in.pkt->size
                             << "fc=" << in.frameCount;
                    readNextPacket(in);
                    continue;
                }

                if (isFieldPacket) {
                    // PAFF: merge both field packets into a single MKV block.
                    // The matroska muxer discards the second field packet (same PTS,
                    // DTS+1 treated as duplicate). Without merging, only top fields
                    // end up in the MKV → half the fields missing → artifacts.
                    QByteArray firstField(reinterpret_cast<const char*>(in.pkt->data),
                                          in.pkt->size);
                    int firstFlags = in.pkt->flags;  // preserve keyframe flag
                    av_packet_unref(in.pkt);

                    // Read the second field packet from the same input
                    readNextPacket(in);

                    // Skip non-VCL packets between field pairs (e.g. SEI).
                    // This path is H.264-only (mIsPAFF implies H.264), but use
                    // the codec-aware helper for consistency.
                    const AVCodecID codec = static_cast<AVCodecID>(mVideoCodecId);
                    while (!in.eof && in.pkt->data && in.pkt->size > 0) {
                        const uint8_t* nd = in.pkt->data;
                        int nsz = in.pkt->size;
                        bool nextIsVcl = false;
                        for (int p = 0; p < nsz - 3; p++) {
                            if (nd[p] == 0 && nd[p+1] == 0) {
                                int s = -1;
                                if (nd[p+2] == 1) s = p + 3;
                                else if (nd[p+2] == 0 && p+3 < nsz && nd[p+3] == 1) s = p + 4;
                                if (s >= 0 && s < nsz && isVclNalByte(codec, nd + s)) {
                                    nextIsVcl = true; break;
                                }
                            }
                        }
                        if (nextIsVcl) break;
                        qDebug() << "  MKV PAFF: skip non-VCL between fields, sz=" << nsz;
                        av_packet_unref(in.pkt);
                        readNextPacket(in);
                    }

                    QByteArray merged = firstField;
                    if (!in.eof) {
                        merged.append(reinterpret_cast<const char*>(in.pkt->data), in.pkt->size);
                        av_packet_unref(in.pkt);
                    }

                    if (av_new_packet(in.pkt, merged.size()) < 0) {
                        setError("av_new_packet failed during PAFF field merge");
                        goto cleanup;
                    }
                    memcpy(in.pkt->data, merged.constData(), merged.size());
                    in.pkt->flags = firstFlags;  // restore keyframe flag

                    in.pkt->pts = in.frameCount * in.frameDur;
                    in.pkt->dts = in.pkt->pts;
                    in.pkt->duration = in.frameDur;
                    in.frameCount++;

                    qDebug() << "  MKV PAFF: merged field pair pkt" << totalPacketsWritten
                             << "pts=" << in.pkt->pts << "fc=" << in.frameCount
                             << "sz=" << in.pkt->size
                             << "l2mfn=" << activeLog2MaxFrameNum;
                } else {
                    // Frame packet (progressive or MBAFF re-encoded): 1 packet = 1 frame
                    in.pkt->pts = in.frameCount * in.frameDur;
                    in.pkt->dts = in.pkt->pts;
                    in.pkt->duration = in.frameDur;
                    in.frameCount++;

                    if (in.outIdx == 0) {
                        qDebug() << "  MKV: frame pkt" << totalPacketsWritten
                                 << "pts=" << in.pkt->pts << "fc=" << in.frameCount
                                 << "sz=" << in.pkt->size
                                 << "field=" << isFieldPacket
                                 << "l2mfn=" << activeLog2MaxFrameNum;
                    }
                }
            } else {
                av_packet_rescale_ts(in.pkt,
                    in.fmtCtx->streams[in.srcIdx]->time_base,
                    outCtx->streams[in.outIdx]->time_base);
            }

            // Apply sync offset
            if (in.syncMs != 0) {
                int64_t off = av_rescale_q(in.syncMs,
                    AVRational{1, 1000},
                    outCtx->streams[in.outIdx]->time_base);
                in.pkt->pts += off;
                in.pkt->dts += off;
            }

            in.pkt->stream_index = in.outIdx;
            in.pkt->pos = -1;

            av_interleaved_write_frame(outCtx, in.pkt);
            // av_interleaved_write_frame takes ownership and unrefs the packet
            totalPacketsWritten++;

            if (totalVideoSize > 0) {
                int percent = (int)(avio_tell(videoInCtx->pb) * 100 / totalVideoSize);
                if (percent != lastPercent) {
                    lastPercent = percent;
                    emit progressChanged(percent, tr("Muxing..."));
                }
            }

            readNextPacket(in);
        }

        qDebug() << "  ES mux: total packets written:" << totalPacketsWritten;
    }

    av_write_trailer(outCtx);
    success = true;

    qDebug() << "TTMkvMergeProvider::mux complete:" << outputFile
             << "output size:" << QFileInfo(outputFile).size() << "bytes";

cleanup:
    // Close all input contexts
    avformat_close_input(&videoInCtx);
    for (auto& in : inputs) {
        if (in.pkt) av_packet_free(&in.pkt);
        if (in.ownsCtx && in.fmtCtx)
            avformat_close_input(&in.fmtCtx);
    }

    // Close output
    if (outCtx) {
        if (!(outCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outCtx->pb);
        avformat_free_context(outCtx);
    }

    return success;
}

// -----------------------------------------------------------------------------
// Audio-only matroska output (.mka): copies all input audio streams into a
// single matroska container with optional language tags. Stream-copy only.
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::muxAudioOnly(const QString& outputFile,
                                      const QStringList& audioFiles,
                                      const QStringList& audioLanguages)
{
    if (audioFiles.isEmpty()) {
        setError("muxAudioOnly: empty input list");
        return false;
    }

    qDebug() << "TTMkvMergeProvider::muxAudioOnly:" << audioFiles.size() << "tracks ->" << outputFile;

    AVFormatContext* outCtx = nullptr;
    int ret = avformat_alloc_output_context2(&outCtx, nullptr, "matroska",
                                              outputFile.toUtf8().constData());
    if (ret < 0 || !outCtx) {
        setError("Cannot create matroska output context");
        return false;
    }

    QString baseName = QFileInfo(outputFile).completeBaseName();
    if (baseName.endsWith("_cut")) baseName.chop(4);
    QString title = decodeVdrName(baseName);
    if (!title.isEmpty()) {
        av_dict_set(&outCtx->metadata, "title", title.toUtf8().constData(), 0);
    }

    // Open all inputs and create one output stream per usable input.
    QList<AVFormatContext*> inputs;
    QList<int> inputAudioIdx;
    QList<int> outStreamIdx;

    for (int i = 0; i < audioFiles.size(); i++) {
        AVFormatContext* inCtx = openInput(audioFiles[i], ret);
        if (!inCtx) {
            qDebug() << "  skip" << audioFiles[i] << ":" << avErrStr(ret);
            continue;
        }
        int audioIdx = av_find_best_stream(inCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioIdx < 0) {
            avformat_close_input(&inCtx);
            continue;
        }

        AVStream* outStream = avformat_new_stream(outCtx, nullptr);
        if (!outStream) {
            avformat_close_input(&inCtx);
            continue;
        }
        avcodec_parameters_copy(outStream->codecpar, inCtx->streams[audioIdx]->codecpar);
        outStream->time_base = inCtx->streams[audioIdx]->time_base;

        if (i < audioLanguages.size() && !audioLanguages[i].isEmpty()) {
            av_dict_set(&outStream->metadata, "language",
                        audioLanguages[i].toUtf8().constData(), 0);
        }

        inputs.append(inCtx);
        inputAudioIdx.append(audioIdx);
        outStreamIdx.append(outStream->index);
    }

    if (inputs.isEmpty()) {
        avformat_free_context(outCtx);
        setError("muxAudioOnly: no usable audio streams");
        return false;
    }

    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            for (auto* c : inputs) avformat_close_input(&c);
            avformat_free_context(outCtx);
            setError(QString("muxAudioOnly: cannot open output: %1").arg(avErrStr(ret)));
            return false;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        for (auto* c : inputs) avformat_close_input(&c);
        if (!(outCtx->oformat->flags & AVFMT_NOFILE)) avio_closep(&outCtx->pb);
        avformat_free_context(outCtx);
        setError(QString("muxAudioOnly: write_header failed: %1").arg(avErrStr(ret)));
        return false;
    }

    // Read packets from each input and rescale into the matroska output.
    // av_interleaved_write_frame handles interleaving across tracks.
    AVPacket* pkt = av_packet_alloc();
    for (int i = 0; i < inputs.size(); i++) {
        AVRational inTb  = inputs[i]->streams[inputAudioIdx[i]]->time_base;
        AVRational outTb = outCtx->streams[outStreamIdx[i]]->time_base;

        while (av_read_frame(inputs[i], pkt) >= 0) {
            if (pkt->stream_index != inputAudioIdx[i]) {
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = outStreamIdx[i];
            av_packet_rescale_ts(pkt, inTb, outTb);
            pkt->pos = -1;
            av_interleaved_write_frame(outCtx, pkt);
            av_packet_unref(pkt);
        }
    }
    av_packet_free(&pkt);

    av_write_trailer(outCtx);

    for (auto* c : inputs) avformat_close_input(&c);
    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) avio_closep(&outCtx->pb);
    avformat_free_context(outCtx);

    qDebug() << "muxAudioOnly complete:" << outputFile
             << "size:" << QFileInfo(outputFile).size() << "bytes";
    return true;
}

// -----------------------------------------------------------------------------
// Set error message
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setError(const QString& error)
{
    mLastError = error;
    qDebug() << "TTMkvMergeProvider error:" << error;
}

// -----------------------------------------------------------------------------
// Generate chapter file for MKV (OGM/Matroska format)
// Returns the path to the generated file, or empty string on failure
// -----------------------------------------------------------------------------
QString TTMkvMergeProvider::generateChapterFile(qint64 durationMs, int intervalMinutes,
                                                  const QString& outputDir)
{
    if (durationMs <= 0 || intervalMinutes <= 0) {
        qDebug() << "Invalid parameters for chapter generation";
        return QString();
    }

    qint64 intervalMs = static_cast<qint64>(intervalMinutes) * 60 * 1000;
    QString chapterFilePath = QDir(outputDir).filePath("chapters.txt");

    QFile chapterFile(chapterFilePath);
    if (!chapterFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to create chapter file:" << chapterFilePath;
        return QString();
    }

    QTextStream out(&chapterFile);

    int chapterNum = 1;
    qint64 currentTime = 0;

    while (currentTime < durationMs) {
        int hours = currentTime / (1000 * 60 * 60);
        int minutes = (currentTime / (1000 * 60)) % 60;
        int seconds = (currentTime / 1000) % 60;
        int millis = currentTime % 1000;

        out << QString("CHAPTER%1=%2:%3:%4.%5\n")
               .arg(chapterNum, 2, 10, QChar('0'))
               .arg(hours, 2, 10, QChar('0'))
               .arg(minutes, 2, 10, QChar('0'))
               .arg(seconds, 2, 10, QChar('0'))
               .arg(millis, 3, 10, QChar('0'));
        out << QString("CHAPTER%1NAME=Chapter %1\n")
               .arg(chapterNum, 2, 10, QChar('0'));

        chapterNum++;
        currentTime += intervalMs;
    }

    chapterFile.close();

    qDebug() << "Generated chapter file with" << (chapterNum - 1) << "chapters:"
             << chapterFilePath;
    return chapterFilePath;
}
