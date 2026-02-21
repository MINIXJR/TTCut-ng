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

    outCtx->nb_chapters = chapters.size();
    outCtx->chapters = (AVChapter**)av_malloc(chapters.size() * sizeof(AVChapter*));
    for (int i = 0; i < chapters.size(); i++) {
        AVChapter* ch = (AVChapter*)av_mallocz(sizeof(AVChapter));
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

        MuxInput vin;
        vin.fmtCtx = videoInCtx;
        vin.srcIdx = videoIdx;
        vin.outIdx = 0;
        vin.syncMs = mVideoSyncOffsetMs;
        vin.ownsCtx = false;  // We'll close videoInCtx at the end
        vin.pkt = av_packet_alloc();

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
            ain.syncMs = mAudioSyncOffsetMs;
            ain.ownsCtx = true;
            ain.pkt = av_packet_alloc();
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
                in.pkt->pts = in.frameCount * in.frameDur;
                in.pkt->dts = in.pkt->pts;
                in.frameCount++;
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
