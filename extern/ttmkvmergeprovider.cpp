/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMKVMERGEPROVIDER
// MKV muxer using libav matroska output format
// ----------------------------------------------------------------------------

#include "ttmkvmergeprovider.h"
#include "ttffmpegwrapper.h"
#include "../avstream/ttnaluparser.h"
#include "../avstream/ttavstream.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <QDebug>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QTemporaryFile>
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

// Payload index of the first VCL NAL of `codec` behind a start code, -1 when
// the packet holds none.
int firstVclPayload(enum AVCodecID codec, const uint8_t* d, int sz)
{
    for (int s = TTNaluParser::findStartCodePayload(d, sz, 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(d, sz, s)) {
        if (isVclNalByte(codec, d + s)) return s;
    }
    return -1;
}

// True when the packet carries a picture: a VCL NAL behind a start code, or a
// packet without start code whose first byte is a VCL NAL header. sz > 0.
bool containsVclNal(enum AVCodecID codec, const uint8_t* d, int sz)
{
    return firstVclPayload(codec, d, sz) >= 0 || isVclNalByte(codec, d);
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
// Open input file with appropriate format detection.
// ES detection and format lookup live in TTFFmpegWrapper as static helpers
// so this provider and the wrapper agree on which extensions are ES.
// ----------------------------------------------------------------------------
static AVFormatContext* openInput(const QString& filePath, int& ret)
{
    AVFormatContext* fmtCtx = nullptr;
    AVDictionary* opts = nullptr;
    const AVInputFormat* inputFmt = nullptr;

    if (TTFFmpegWrapper::isElementaryStreamPath(filePath)) {
        av_dict_set(&opts, "probesize", "50000000", 0);
        av_dict_set(&opts, "analyzeduration", "10000000", 0);
        inputFmt = TTFFmpegWrapper::esInputFormatForPath(filePath);
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
// Matroska output context for outputFile, titled after its file name ("_cut"
// stripped, VDR #XX decoded). nullptr when libav cannot create one.
// ----------------------------------------------------------------------------
static AVFormatContext* allocMatroskaOutput(const QString& outputFile)
{
    AVFormatContext* outCtx = nullptr;
    const int ret = avformat_alloc_output_context2(&outCtx, nullptr, "matroska",
                                                   outputFile.toUtf8().constData());
    if (ret < 0 || !outCtx) return nullptr;

    QString baseName = QFileInfo(outputFile).completeBaseName();
    if (baseName.endsWith("_cut")) baseName.chop(4);
    const QString title = decodeVdrName(baseName);
    if (!title.isEmpty())
        av_dict_set(&outCtx->metadata, "title", title.toUtf8().constData(), 0);
    return outCtx;
}

// Close the output file (if it was opened) and free the context. Safe on a
// context whose pb was never opened and on nullptr.
static void freeMatroskaOutput(AVFormatContext*& outCtx)
{
    if (!outCtx) return;
    if (!(outCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outCtx->pb);
    avformat_free_context(outCtx);
    outCtx = nullptr;
}

// Scan a packet for an H.264 SPS NAL (type 7) and extract log2_max_frame_num.
// This is needed because Smart Cut output contains TWO different SPS:
//   1. Encoder SPS (x264 MBAFF, log2_max_frame_num=4)
//   2. Source SPS  (PAFF stream, log2_max_frame_num=9)
// The muxer must use the correct log2_max_frame_num for field_pic_flag parsing,
// otherwise it reads field_pic_flag at the wrong bit position and may falsely
// detect re-encoded frames as field packets, causing frame merging corruption.
static bool parseInlineSpsLog2MaxFrameNum(const uint8_t* data, int size, int& log2MaxFrameNum)
{
    TTNaluParser::H264SpsBasics sps;
    if (!TTNaluParser::parseH264SpsBasics(data, size, sps)) return false;
    log2MaxFrameNum = sps.log2MaxFrameNum;
    return true;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TTMkvMergeProvider::TTMkvMergeProvider()
    : QObject()
    , mAudioSyncOffsetMs(0)
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
// Option setters
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setDefaultDuration(const QString& trackId, const QString& duration)
{
    int id = trackId.toInt();
    mTrackOptions[id].defaultDuration = duration;
    if (TTSettings::instance()->logMkvMux())
        qDebug() << "TTMkvMergeProvider: default duration for track" << id << "=" << duration;
}

void TTMkvMergeProvider::setVideoOptions(const TTMkvVideoOptions& options)
{
    // A stream without a frame rate would turn the default duration into a
    // division by zero; without one the muxer keeps the packet timestamps.
    if (options.frameRate > 0) {
        const int frameDurationNs = static_cast<int>(1000000000.0 / options.frameRate);
        setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
    }
    setIsPAFF(options.isPAFF, options.paffLog2MaxFrameNum);
    setVideoCodecId(options.codecId);
    setVideoDisplayOrder(options.displayOrder);
    setAudioSyncOffset(options.audioSyncOffsetMs);
}

TTMkvVideoOptions TTMkvMergeProvider::videoOptionsFor(const TTVideoStream* videoStream,
                                                      double frameRate, int audioSyncOffsetMs)
{
    TTMkvVideoOptions options;
    options.frameRate           = frameRate;
    options.isPAFF              = videoStream->isPAFF();
    options.paffLog2MaxFrameNum = videoStream->paffLog2MaxFrameNum();
    options.codecId             = videoCodecIdFor(videoStream->streamType());
    options.audioSyncOffsetMs   = audioSyncOffsetMs;
    return options;
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
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "TTMkvMergeProvider: audio sync offset" << offsetMs << "ms";
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

    outCtx->chapters = static_cast<AVChapter**>(av_malloc(chapters.size() * sizeof(AVChapter*)));
    if (!outCtx->chapters) {
        qWarning() << "av_malloc failed for chapter array — chapters dropped";
        return;
    }
    outCtx->nb_chapters = 0;  // populated below; only set for entries actually allocated
    for (int i = 0; i < chapters.size(); i++) {
        AVChapter* ch = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
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

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "Added" << chapters.size() << "chapters from" << chapterFile;
}

// -----------------------------------------------------------------------------
// Helpers for mux() — see docs/superpowers/specs/2026-05-03-mux-split-refactor.md
// -----------------------------------------------------------------------------

// Read next packet matching srcIdx from this input.
bool TTMkvMergeProvider::readNextPacket(MuxInput& in)
{
    while (av_read_frame(in.fmtCtx, in.pkt) >= 0) {
        if (in.pkt->stream_index == in.srcIdx)
            return true;
        av_packet_unref(in.pkt);
    }
    in.eof = true;
    return false;
}

// Get normalized PTS in AV_TIME_BASE for comparison across inputs.
int64_t TTMkvMergeProvider::getNormalizedPts(const MuxInput& in) const
{
    int64_t pts;
    if (in.assignPts) {
        // PTS from the frame count, in ns -> AV_TIME_BASE (same source as
        // assignEsTimestamps, so interleaving follows the written PTS)
        pts = av_rescale_q(in.frameCount * in.frameDurNs,
            AVRational{1, 1000000000}, AV_TIME_BASE_Q);
    } else if (in.pkt->pts != AV_NOPTS_VALUE) {
        pts = av_rescale_q(in.pkt->pts,
            in.fmtCtx->streams[in.srcIdx]->time_base, AV_TIME_BASE_Q);
    } else {
        pts = 0;
    }
    return pts + in.syncMs * 1000;  // ms → µs (AV_TIME_BASE = µs)
}

// Set up the video input/output stream pair. videoInCtx is owned by the
// caller (mux()); outVin.ownsCtx stays false.
bool TTMkvMergeProvider::setupVideoInput(AVFormatContext* outCtx,
                                          AVFormatContext* videoInCtx,
                                          MuxInput& outVin,
                                          int64_t& videoDurationNs)
{
    videoDurationNs = 0;

    int videoIdx = av_find_best_stream(videoInCtx, AVMEDIA_TYPE_VIDEO,
                                        -1, -1, nullptr, 0);
    if (videoIdx < 0) {
        setError("No video stream in input");
        return false;
    }

    AVStream* videoOut = avformat_new_stream(outCtx, nullptr);
    if (!videoOut) {
        setError("avformat_new_stream failed for video");
        return false;
    }
    int ret = avcodec_parameters_copy(videoOut->codecpar,
                                       videoInCtx->streams[videoIdx]->codecpar);
    if (ret < 0) {
        setError(QString("avcodec_parameters_copy failed for video: %1")
                     .arg(avErrStr(ret)));
        return false;
    }
    videoOut->time_base = videoInCtx->streams[videoIdx]->time_base;

    // Copy SAR to stream level (matroska muxer uses stream SAR, not codecpar SAR)
    if (videoOut->codecpar->sample_aspect_ratio.num > 0)
        videoOut->sample_aspect_ratio = videoOut->codecpar->sample_aspect_ratio;

    outVin.fmtCtx = videoInCtx;
    outVin.srcIdx = videoIdx;
    outVin.outIdx = 0;
    outVin.ownsCtx = false;  // caller owns videoInCtx
    outVin.pkt = av_packet_alloc();
    if (!outVin.pkt) {
        setError("av_packet_alloc failed for video input");
        return false;
    }

    // Frame duration from setDefaultDuration (e.g. "40000000ns" for 25fps).
    // NOTE: frameDur is recalculated AFTER avformat_write_header() because the
    // matroska muxer changes time_base during header write (e.g. to 1/1000).
    if (mTrackOptions.contains(0) && !mTrackOptions[0].defaultDuration.isEmpty()) {
        QString dur = mTrackOptions[0].defaultDuration;
        if (dur.endsWith("ns")) {
            videoDurationNs = dur.left(dur.length() - 2).toLongLong();
            if (videoDurationNs > 0) {
                outVin.assignPts = true;
                videoOut->r_frame_rate = av_make_q(1000000000, (int)videoDurationNs);
                videoOut->avg_frame_rate = videoOut->r_frame_rate;

                // Display-PTS mode: per-packet display positions supplied by
                // TTESSmartCut. reorderOffset lowers DTS (never lifts PTS -
                // lifting would shift video against untouched audio).
                if (!mVideoDisplayOrder.isEmpty()) {
                    outVin.displayOrder = mVideoDisplayOrder;
                    int maxLead = 0;
                    for (int i = 0; i < outVin.displayOrder.size(); ++i)
                        maxLead = qMax(maxLead, i - outVin.displayOrder[i]);
                    outVin.reorderOffset = maxLead;
                    if (TTSettings::instance()->logMkvMux())
                        qDebug() << "  Video: display-PTS mode,"
                                 << outVin.displayOrder.size() << "entries,"
                                 << "reorderOffset" << outVin.reorderOffset;
                }
            }
        }
    }
    return true;
}

// Open each media file of one type, create one output stream per usable
// input, append a MuxInput to `inputs`. A file that cannot be used is skipped
// and recorded in `dropped` as "<file>: <reason>"; whether that fails the mux
// is the caller's decision (acceptDroppedInputs). Language: explicit list first; for
// audio the `_xxx` suffix of the file name is the fallback. Audio tracks are
// flagged AV_DISPOSITION_DEFAULT (avcodec_parameters_copy() does not carry
// the disposition, and without it only the first track would be default)
// and carry the .info A/V sync offset; per-track user delay is already baked
// into the cut audio file's keepList times, so it is NOT added here again.
bool TTMkvMergeProvider::addMediaInputs(AVFormatContext* outCtx,
                                         const QStringList& files,
                                         const QStringList& languages,
                                         int mediaType,
                                         int& nextOutIdx,
                                         QList<MuxInput>& inputs,
                                         int audioSyncMs,
                                         QStringList& dropped)
{
    const bool  isAudio = (mediaType == AVMEDIA_TYPE_AUDIO);
    QRegularExpression langRe("_([a-z]{3})(?:_\\d+)?$");

    for (int i = 0; i < files.size(); i++) {
        auto drop = [&](const QString& reason) {
            dropped << QString("%1: %2").arg(files[i], reason);
        };
        if (!QFile::exists(files[i])) {
            drop("file missing");
            continue;
        }

        int ret = 0;
        AVFormatContext* inCtx = openInput(files[i], ret);
        if (!inCtx) {
            drop(QString("cannot open (%1)").arg(avErrStr(ret)));
            continue;
        }

        int srcIdx = av_find_best_stream(inCtx, static_cast<AVMediaType>(mediaType),
                                         -1, -1, nullptr, 0);
        if (srcIdx < 0) {
            drop(isAudio ? "no audio stream" : "no subtitle stream");
            avformat_close_input(&inCtx);
            continue;
        }

        AVStream* out = avformat_new_stream(outCtx, nullptr);
        if (!out) {
            drop("avformat_new_stream failed");
            avformat_close_input(&inCtx);
            continue;
        }
        ret = avcodec_parameters_copy(out->codecpar, inCtx->streams[srcIdx]->codecpar);
        if (ret < 0) {
            drop(QString("avcodec_parameters_copy failed (%1)").arg(avErrStr(ret)));
            avformat_close_input(&inCtx);
            continue;
        }
        out->time_base = inCtx->streams[srcIdx]->time_base;
        if (isAudio)
            out->disposition |= AV_DISPOSITION_DEFAULT;

        QString lang;
        if (i < languages.size() && !languages[i].isEmpty()) {
            lang = languages[i];
        } else if (isAudio) {
            QRegularExpressionMatch m = langRe.match(QFileInfo(files[i]).completeBaseName());
            if (m.hasMatch()) lang = m.captured(1);
        }
        if (!lang.isEmpty())
            av_dict_set(&out->metadata, "language", lang.toUtf8().constData(), 0);

        MuxInput in;
        in.fmtCtx  = inCtx;
        in.srcIdx  = srcIdx;
        in.outIdx  = nextOutIdx++;
        in.syncMs  = isAudio ? audioSyncMs : 0;
        in.ownsCtx = true;
        in.pkt = av_packet_alloc();
        if (!in.pkt) {
            drop("av_packet_alloc failed");
            avformat_close_input(&inCtx);
            continue;
        }
        inputs.append(in);

        if (TTSettings::instance()->logMkvMux()) {
            if (isAudio)
                qDebug() << "  Audio" << i << ":" << files[i] << "lang=" << lang << "outIdx=" << in.outIdx;
            else
                qDebug() << "  Subtitle" << i << ":" << files[i];
        }
    }
    return true;
}

bool TTMkvMergeProvider::addAudioInputs(AVFormatContext* outCtx,
                                         const QStringList& audioFiles,
                                         const QStringList& languages,
                                         int& nextOutIdx,
                                         QList<MuxInput>& inputs,
                                         int audioSyncMs)
{
    return addMediaInputs(outCtx, audioFiles, languages, AVMEDIA_TYPE_AUDIO,
                          nextOutIdx, inputs, audioSyncMs, mDroppedInputs);
}

bool TTMkvMergeProvider::addSubtitleInputs(AVFormatContext* outCtx,
                                            const QStringList& subtitleFiles,
                                            int& nextOutIdx,
                                            QList<MuxInput>& inputs)
{
    return addMediaInputs(outCtx, subtitleFiles, mSubtitleLanguages, AVMEDIA_TYPE_SUBTITLE,
                          nextOutIdx, inputs, 0, mDroppedInputs);
}

bool TTMkvMergeProvider::acceptDroppedInputs(int requestedCount)
{
    if (mDroppedInputs.isEmpty()) return true;
    if (mRequireAllInputs) {
        setError(QString("%1 of %2 audio/subtitle file(s) could not be used: %3")
                     .arg(mDroppedInputs.size()).arg(requestedCount)
                     .arg(mDroppedInputs.join("; ")));
        return false;
    }
    for (const QString& d : mDroppedInputs)
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("MKV mux: input skipped - %1").arg(d));
    return true;
}

// PAFF: merge both field packets into a single MKV block.
// The matroska muxer discards the second field packet (same PTS, DTS+1
// treated as duplicate). Without merging, only top fields end up in the
// MKV → half the fields missing → artifacts.
//
// Caller has already detected isFieldPacket=true for the current in.pkt.
// On return, in.pkt holds the merged packet with PTS/DTS/duration set,
// frameCount has been incremented, and the caller writes via
// av_interleaved_write_frame.
// ----------------------------------------------------------------------------
// Timestamp assignment for raw-ES video packets (one call per output FRAME;
// PAFF field pairs are merged before this point). Display-PTS mode uses the
// SmartCut-supplied display order: pts = display*dur (true display time,
// keeps A/V relation), dts = (i - reorderOffset)*dur (lowered so pts >= dts;
// a negative start is normalized by avoid_negative_ts for ALL streams
// together). On index overrun: one-shot warning, fall back to linear.
//
// Every timestamp is computed in ns and rounded to the output time base on
// its own. The matroska time base is 1 ms; multiplying a duration that was
// rounded to whole ms once (33 ms for 29.97 fps) accumulated the error:
// -1.1 % at 29.97 fps, +0.7 % at 23.976 fps against the audio
// (tools/diag/gate_mkv_framerate.sh).
// ----------------------------------------------------------------------------
void TTMkvMergeProvider::assignEsTimestamps(MuxInput& in)
{
    const AVRational nsBase  = {1, 1000000000};
    const AVRational outBase = {in.tbNum, in.tbDen};
    auto frameTime = [&](int64_t frames) {
        return av_rescale_q(frames * in.frameDurNs, nsBase, outBase);
    };

    const int64_t linear = frameTime(in.frameCount);
    in.pkt->pts = linear;
    in.pkt->dts = linear;
    if (!in.displayOrder.isEmpty()) {
        if (in.frameCount < in.displayOrder.size()) {
            in.pkt->pts = frameTime(in.displayOrder[(int)in.frameCount]);
            in.pkt->dts = frameTime(in.frameCount - in.reorderOffset);
        } else if (!in.displayOrderWarned) {
            in.displayOrderWarned = true;
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-PTS list exhausted at packet %1 of %2 - "
                        "falling back to linear PTS for remaining packets")
                    .arg(in.frameCount).arg(in.displayOrder.size()));
        }
    }
    in.pkt->duration = in.frameDur;
    in.frameCount++;
}

bool TTMkvMergeProvider::processPAFFFieldPair(MuxInput& in,
                                               int activeLog2MaxFrameNum,
                                               int64_t totalPacketsWritten)
{
    QByteArray firstField(reinterpret_cast<const char*>(in.pkt->data),
                          in.pkt->size);
    int firstFlags = in.pkt->flags;  // preserve keyframe flag
    av_packet_unref(in.pkt);

    // Read the second field packet from the same input
    readNextPacket(in);

    // Skip non-VCL packets between field pairs (e.g. SEI). This path is
    // H.264-only (mIsPAFF implies H.264), but use the codec-aware helper
    // for consistency.
    const AVCodecID codec = static_cast<AVCodecID>(mVideoCodecId);
    while (!in.eof && in.pkt->data && in.pkt->size > 0) {
        if (firstVclPayload(codec, in.pkt->data, in.pkt->size) >= 0) break;
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "  MKV PAFF: skip non-VCL between fields, sz=" << in.pkt->size;
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
        return false;
    }
    memcpy(in.pkt->data, merged.constData(), merged.size());
    in.pkt->flags = firstFlags;  // restore keyframe flag

    assignEsTimestamps(in);

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "  MKV PAFF: merged field pair pkt" << totalPacketsWritten
                 << "pts=" << in.pkt->pts << "fc=" << in.frameCount
                 << "sz=" << in.pkt->size
                 << "l2mfn=" << activeLog2MaxFrameNum;
    return true;
}

// ----------------------------------------------------------------------------
// MPEG-2 display order from the ES bitstream. temporal_reference (10 bits
// right after the picture_start_code) is the display position within the
// GOP; group_start_code delimits GOPs. display = gopBase + temporal_ref,
// gopBase advancing by the picture count of each finished GOP. Self-check:
// the temporal_reference values of every GOP must form a gap-free
// permutation 0..n-1 - anything else (broken GOP, unexpected structure,
// field-coded pictures diverging from libav's frame packetization) returns
// an empty list and the muxer keeps its legacy linear PTS.
// ----------------------------------------------------------------------------
QVector<int> TTMkvMergeProvider::buildMpeg2DisplayOrder(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return QVector<int>();

    QVector<int> order;
    QVector<int> gopRefs;          // temporal_references of the current GOP
    int gopBase = 0;
    bool bad = false;

    auto flushGop = [&]() {
        if (gopRefs.isEmpty()) return;
        // permutation check: sorted refs must be exactly 0..n-1
        QVector<int> sorted = gopRefs;
        std::sort(sorted.begin(), sorted.end());
        for (int i = 0; i < sorted.size(); ++i) {
            if (sorted[i] != i) { bad = true; return; }
        }
        for (int r : gopRefs) order.append(gopBase + r);
        gopBase += gopRefs.size();
        gopRefs.clear();
    };

    const qint64 CHUNK = 1 << 20;
    QByteArray buf;
    qint64 filePos = 0;
    QByteArray carry;              // overlap so start codes crossing chunk borders are seen

    while (!bad && !(buf = f.read(CHUNK)).isEmpty()) {
        QByteArray scan = carry + buf;
        const uchar* d = reinterpret_cast<const uchar*>(scan.constData());
        int n = scan.size();
        // stop 5 bytes short: a start code + 2 payload bytes must fit
        for (int i = 0; i + 5 < n; ++i) {
            if (d[i] != 0 || d[i+1] != 0 || d[i+2] != 1) continue;
            uchar code = d[i+3];
            if (code == 0x00) {
                // picture_start_code: temporal_reference = next 10 bits
                int tref = (d[i+4] << 2) | (d[i+5] >> 6);
                gopRefs.append(tref);
                i += 5;
            } else if (code == 0xB8) {
                flushGop();
                if (bad) break;
                i += 3;
            }
        }
        // keep the last 5 bytes as overlap for the next chunk
        carry = scan.right(qMin(5, scan.size()));
        filePos += buf.size();
    }
    Q_UNUSED(filePos);
    if (!bad) flushGop();

    if (bad || order.isEmpty()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("MPEG-2 display-order scan of %1 failed permutation "
                    "check - keeping legacy linear PTS")
                .arg(QFileInfo(filePath).fileName()));
        return QVector<int>();
    }
    return order;
}

// -----------------------------------------------------------------------------
// One raw-ES video packet of the interleave loop: skip it when it carries no
// picture (EOS, SPS/PPS only - it must not advance frameCount, or PTS shift
// by phantom frames), merge a PAFF field pair, otherwise stamp it as one frame.
//
// PAFF: re-encoded frames are single packets (MBAFF), stream-copied frames may
// be 2 field packets, told apart by field_pic_flag. Its bit position depends
// on log2_max_frame_num, and Smart Cut ES carry two SPS (encoder, e.g. 4, then
// source, e.g. 9), so the width follows the most recent inline SPS; a wrong
// width detects re-encoded frames as fields and merges them.
// -----------------------------------------------------------------------------
TTMkvMergeProvider::EsVideoStep TTMkvMergeProvider::prepareEsVideoPacket(
        MuxInput& in, int& activeLog2MaxFrameNum, int64_t totalPacketsWritten)
{
    const uint8_t* d  = in.pkt->data;
    const int      sz = in.pkt->size;
    bool isFieldPacket = false;
    bool hasVclNal     = true;   // an empty packet is written, as it always was

    if (mIsPAFF && in.outIdx == 0 && d && sz > 4) {
        int newL2mfn = 0;
        if (parseInlineSpsLog2MaxFrameNum(d, sz, newL2mfn)
                && newL2mfn != activeLog2MaxFrameNum) {
            if (TTSettings::instance()->logMkvMux())
                qDebug() << "  MKV PAFF: SPS change log2_max_frame_num"
                         << activeLog2MaxFrameNum << "->" << newL2mfn
                         << "at packet" << totalPacketsWritten;
            activeLog2MaxFrameNum = newL2mfn;
        }

        const int nalStart = TTNaluParser::findH264SlicePayload(d, sz);
        hasVclNal = (nalStart >= 0);
        if (hasVclNal) {
            int  frameNum = 0;
            bool isBottom = false;
            TTNaluParser::parseH264SliceFieldInfo(d + nalStart, sz - nalStart,
                                                  activeLog2MaxFrameNum,
                                                  frameNum, isFieldPacket, isBottom);
        }
    } else if (in.outIdx == 0 && d && sz > 0) {
        hasVclNal = containsVclNal(static_cast<AVCodecID>(mVideoCodecId), d, sz);
    }

    if (in.outIdx == 0 && !hasVclNal) {
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "  MKV PAFF: skip non-VCL video packet"
                     << totalPacketsWritten << "sz=" << sz << "fc=" << in.frameCount;
        return EsVideoStep::Skip;
    }

    if (isFieldPacket)
        return processPAFFFieldPair(in, activeLog2MaxFrameNum, totalPacketsWritten)
                   ? EsVideoStep::Write : EsVideoStep::Error;

    // Frame packet (progressive or MBAFF re-encoded): 1 packet = 1 frame
    assignEsTimestamps(in);
    if (in.outIdx == 0 && TTSettings::instance()->logMkvMux())
        qDebug() << "  MKV: frame pkt" << totalPacketsWritten
                 << "pts=" << in.pkt->pts << "fc=" << in.frameCount
                 << "sz=" << in.pkt->size
                 << "field=" << isFieldPacket
                 << "l2mfn=" << activeLog2MaxFrameNum;
    return EsVideoStep::Write;
}

// -----------------------------------------------------------------------------
// The interleave loop shared by mux() and muxAudioOnly(): always writes the
// input whose next packet has the smallest normalized PTS, so the tracks come
// out interleaved in the file (av_interleaved_write_frame alone gives up after
// its 10 s max_interleave_delta when one input is fed ahead of the others).
// Raw-ES video goes through prepareEsVideoPacket(); every other input is
// rescaled to the output time base. progressPercent() returns -1 when no
// percent is known. False on abort or a write error (lastError set); the
// caller cleans up.
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::writeInterleaved(AVFormatContext* outCtx, QList<MuxInput>& inputs,
                                          const std::function<int()>& progressPercent)
{
    int lastPercent = -1;
    int64_t totalPacketsWritten = 0;

    // Active SPS log2_max_frame_num for PAFF field_pic_flag parsing: Smart Cut
    // ES carry two SPS (encoder, e.g. 4, then source, e.g. 9) - see
    // prepareEsVideoPacket().
    int activeLog2MaxFrameNum = mH264Log2MaxFrameNum;

    for (int i = 0; i < inputs.size(); i++) {
        bool got = readNextPacket(inputs[i]);
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "    Input" << i << ": first read ="
                     << (got ? "OK" : "EOF")
                     << "srcIdx=" << inputs[i].srcIdx
                     << "assignPts=" << inputs[i].assignPts;
    }

    while (true) {
        if (checkAbort()) return false;

        int bestIdx = -1;
        int64_t bestPts = INT64_MAX;
        for (int i = 0; i < inputs.size(); i++) {
            if (inputs[i].eof) continue;
            int64_t npts = getNormalizedPts(inputs[i]);
            if (npts < bestPts) {
                bestPts = npts;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;  // All inputs exhausted

        MuxInput& in = inputs[bestIdx];

        if (in.assignPts) {
            const EsVideoStep step =
                prepareEsVideoPacket(in, activeLog2MaxFrameNum, totalPacketsWritten);
            if (step == EsVideoStep::Error) return false;
            if (step == EsVideoStep::Skip) {
                readNextPacket(in);
                continue;
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

        int wfRet = av_interleaved_write_frame(outCtx, in.pkt);
        // av_interleaved_write_frame takes ownership and unrefs the packet
        if (wfRet < 0) {
            setError(QString("av_interleaved_write_frame failed: %1").arg(avErrStr(wfRet)));
            return false;
        }
        totalPacketsWritten++;

        const int percent = progressPercent();
        if (percent >= 0 && percent != lastPercent) {
            lastPercent = percent;
            emit progressChanged(percent, tr("Muxing..."));
        }

        readNextPacket(in);
    }

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "  Mux: total packets written:" << totalPacketsWritten;
    return true;
}

// -----------------------------------------------------------------------------
// mux(): elementary-stream video plus separate audio/subtitle files ->
// interleaved matroska.
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::mux(const QString& outputFile,
                              const QString& videoFile,
                              const QStringList& audioFiles,
                              const QStringList& subtitleFiles)
{
    // mWasAborted is an output, not an input: cleared at every run's entry
    // point. mAbortRequested needs no clearing point of its own -- providers
    // are created fresh per operation (see the member declaration).
    mWasAborted = false;
    mDroppedInputs.clear();

    if (videoFile.isEmpty() || !QFile::exists(videoFile)) {
        setError(QString("Video file not found: %1").arg(videoFile));
        return false;
    }

    // MPEG-2 ES: no smart-cut supplied display order exists (the MPEG-2 cut
    // path predates it) - derive it from the bitstream's temporal_reference
    // so B-frames get true display PTS like the H.26x paths. Empty result
    // (self-check failed) keeps the legacy linear assignment.
    if (mVideoDisplayOrder.isEmpty()
            && mVideoCodecId == AV_CODEC_ID_MPEG2VIDEO) {
        mVideoDisplayOrder = buildMpeg2DisplayOrder(videoFile);
        if (!mVideoDisplayOrder.isEmpty() && TTSettings::instance()->logMkvMux())
            qDebug() << "  MPEG-2 display order derived from temporal_reference:"
                     << mVideoDisplayOrder.size() << "pictures";
    }

    if (TTSettings::instance()->logMkvMux()) {
        qDebug() << "TTMkvMergeProvider::mux (libav matroska)";
        qDebug() << "  Output:" << outputFile;
        qDebug() << "  MKV mux: videoCodecId =" << avcodec_get_name(static_cast<AVCodecID>(mVideoCodecId));
        qDebug() << "  Video:" << videoFile
                 << "size:" << QFileInfo(videoFile).size() << "bytes";
    }
    for (int i = 0; i < audioFiles.size(); i++) {
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "  Audio" << i << ":" << audioFiles[i]
                     << "size:" << QFileInfo(audioFiles[i]).size() << "bytes";
    }

    int ret = 0;
    AVFormatContext* videoInCtx = openInput(videoFile, ret);
    if (!videoInCtx) {
        setError(QString("Cannot open video: %1 (%2)")
                     .arg(videoFile, avErrStr(ret)));
        return false;
    }

    AVFormatContext* outCtx = allocMatroskaOutput(outputFile);
    if (!outCtx) {
        avformat_close_input(&videoInCtx);
        setError("Cannot create matroska output context");
        return false;
    }

    // Common cleanup lambda — every exit path goes through this.
    // Safe to call multiple times: avformat_close_input nulls its argument.
    QList<MuxInput> inputs;
    auto cleanupAll = [&]() {
        for (auto& mi : inputs) {
            if (mi.pkt) av_packet_free(&mi.pkt);
            if (mi.ownsCtx && mi.fmtCtx) avformat_close_input(&mi.fmtCtx);
        }
        avformat_close_input(&videoInCtx);
        freeMatroskaOutput(outCtx);
    };

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "  Mode: ES mux";

    int64_t videoDurationNs = 0;
    MuxInput vin;
    if (!setupVideoInput(outCtx, videoInCtx, vin, videoDurationNs)) {
        cleanupAll();
        return false;
    }
    inputs.append(vin);

    int nextOutIdx = 1;
    addAudioInputs(outCtx, audioFiles, mAudioLanguages, nextOutIdx, inputs,
                    mAudioSyncOffsetMs);
    addSubtitleInputs(outCtx, subtitleFiles, nextOutIdx, inputs);
    if (!acceptDroppedInputs(audioFiles.size() + subtitleFiles.size())) {
        cleanupAll();
        return false;
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
            cleanupAll();
            return false;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        setError(QString("Cannot write header: %1").arg(avErrStr(ret)));
        cleanupAll();
        return false;
    }

    // Recalculate frameDur after write_header (muxer may change time_base)
    if (videoDurationNs > 0 && !inputs.isEmpty() && inputs[0].assignPts) {
        MuxInput& v = inputs[0];
        AVStream* videoOut = outCtx->streams[v.outIdx];
        v.frameDurNs = videoDurationNs;
        v.tbNum      = videoOut->time_base.num;
        v.tbDen      = videoOut->time_base.den;
        v.frameDur   = av_rescale_q(videoDurationNs,
            AVRational{1, 1000000000}, videoOut->time_base);
        if (TTSettings::instance()->logMkvMux())
            qDebug() << "  Video: frame duration" << videoDurationNs << "ns ="
                     << v.frameDur << "tb-units (time_base"
                     << videoOut->time_base.num << "/" << videoOut->time_base.den
                     << "after write_header)";
    }

    // PAFF mode requires a valid log2_max_frame_num for field-pair parsing.
    // The default ctor value (4) only happens to work for encoder SPS; a
    // caller that passes no width to setIsPAFF() gets wrong source SPS
    // parsing. Surface the misconfiguration here.
    if (mIsPAFF && mH264Log2MaxFrameNum == 0) {
        qWarning() << "MKV mux: PAFF stream but mH264Log2MaxFrameNum is 0 — "
                      "setIsPAFF() got no log2_max_frame_num; "
                      "PAFF field detection will be wrong";
    }

    // Progress follows the video file: it dominates the output size.
    const int64_t totalVideoSize = avio_size(videoInCtx->pb);
    if (TTSettings::instance()->logMkvMux())
        qDebug() << "  ES mux: totalVideoSize=" << totalVideoSize
                 << "inputs=" << inputs.size();
    auto videoPercent = [&]() -> int {
        return totalVideoSize > 0
            ? int(avio_tell(videoInCtx->pb) * 100 / totalVideoSize) : -1;
    };
    if (!writeInterleaved(outCtx, inputs, videoPercent)) {
        cleanupAll();
        return false;
    }

    // Display-PTS self-check: the list must cover the video packets exactly.
    // A shortfall was already warned per-packet; a surplus means the list and
    // the packetization disagree (gates treat this warning as FAIL).
    for (const MuxInput& in : inputs) {
        if (in.assignPts && !in.displayOrder.isEmpty()
                && in.frameCount != in.displayOrder.size()
                && !in.displayOrderWarned) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-PTS list size %1 does not match %2 written "
                        "video packets")
                    .arg(in.displayOrder.size()).arg(in.frameCount));
        }
    }

    av_write_trailer(outCtx);

    cleanupAll();

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "TTMkvMergeProvider::mux complete:" << outputFile
                 << "output size:" << QFileInfo(outputFile).size() << "bytes";
    return true;
}

// -----------------------------------------------------------------------------
// Audio-only matroska output (.mka): copies all input audio streams into a
// single matroska container with optional language tags. Stream-copy only.
// -----------------------------------------------------------------------------
int TTMkvMergeProvider::videoCodecIdFor(TTAVTypes::AVStreamType type)
{
    switch (type) {
      case TTAVTypes::h265_video: return AV_CODEC_ID_HEVC;
      case TTAVTypes::h264_video: return AV_CODEC_ID_H264;
      default:                    return AV_CODEC_ID_MPEG2VIDEO;
    }
}

bool TTMkvMergeProvider::muxAudioOnly(const QString& outputFile,
                                      const QStringList& audioFiles,
                                      const QStringList& audioLanguages)
{
    // mWasAborted is an output, not an input: cleared at every run's entry
    // point. mAbortRequested needs no clearing point of its own -- providers
    // are created fresh per operation (see the member declaration).
    mWasAborted = false;
    mDroppedInputs.clear();

    if (audioFiles.isEmpty()) {
        setError("muxAudioOnly: empty input list");
        return false;
    }

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "TTMkvMergeProvider::muxAudioOnly:" << audioFiles.size()
                 << "tracks ->" << outputFile;

    AVFormatContext* outCtx = allocMatroskaOutput(outputFile);
    if (!outCtx) {
        setError("Cannot create matroska output context");
        return false;
    }
    int ret = 0;

    // Use the shared helper (INFO-6 absorption). muxAudioOnly has no video,
    // so audio output streams start at index 0; sync offset is always 0.
    QList<MuxInput> inputs;
    int nextOutIdx = 0;
    addAudioInputs(outCtx, audioFiles, audioLanguages, nextOutIdx, inputs, 0);

    auto cleanupInputs = [&]() {
        for (auto& mi : inputs) {
            if (mi.pkt) av_packet_free(&mi.pkt);
            if (mi.ownsCtx && mi.fmtCtx) avformat_close_input(&mi.fmtCtx);
        }
    };
    // Every exit path below goes through this (same pattern as mux()).
    // avio_closep() tolerates a pb that was never opened.
    auto closeAll = [&]() {
        cleanupInputs();
        freeMatroskaOutput(outCtx);
    };

    if (!acceptDroppedInputs(audioFiles.size())) {
        closeAll();
        return false;
    }
    if (inputs.isEmpty()) {
        closeAll();
        setError("muxAudioOnly: no usable audio streams");
        return false;
    }

    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            closeAll();
            setError(QString("muxAudioOnly: cannot open output: %1").arg(avErrStr(ret)));
            return false;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        closeAll();
        setError(QString("muxAudioOnly: write_header failed: %1").arg(avErrStr(ret)));
        return false;
    }

    // Interleaved like mux(); progress over the bytes read from all tracks.
    int64_t totalBytes = 0;
    for (const MuxInput& mi : inputs) totalBytes += qMax<int64_t>(0, avio_size(mi.fmtCtx->pb));
    auto bytesPercent = [&]() -> int {
        if (totalBytes <= 0) return -1;
        int64_t done = 0;
        for (const MuxInput& mi : inputs) done += qMax<int64_t>(0, avio_tell(mi.fmtCtx->pb));
        return int(qMin<int64_t>(100, done * 100 / totalBytes));
    };
    if (!writeInterleaved(outCtx, inputs, bytesPercent)) {
        closeAll();
        return false;
    }

    av_write_trailer(outCtx);

    closeAll();

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "muxAudioOnly complete:" << outputFile
                 << "size:" << QFileInfo(outputFile).size() << "bytes";
    return true;
}

// -----------------------------------------------------------------------------
// Set error message
// -----------------------------------------------------------------------------
void TTMkvMergeProvider::setError(const QString& error)
{
    ttSetLastError(mLastError, __FILE__, __LINE__, "TTMkvMergeProvider", error);
}

// -----------------------------------------------------------------------------
// Poll point for cooperative abort. Returns true (and records the abort)
// when a requestAbort() arrived; every caller returns false through its
// normal error path afterwards.
// -----------------------------------------------------------------------------
bool TTMkvMergeProvider::checkAbort()
{
    if (!mAbortRequested.load(std::memory_order_relaxed)) return false;
    mWasAborted = true;
    // Deliberately not setError(): a user cancel is not a failure and must
    // not read as one in the log (setError() logs at warning level via
    // TTMessageLogger). Set mLastError directly so lastError() still
    // contains "aborted", without the warning-level log line. Mirrors
    // TTESSmartCut::checkAbort().
    mLastError = "aborted by user";
    return true;
}

// -----------------------------------------------------------------------------
// Generate chapter file for MKV (OGM/Matroska format)
// Returns the path to the generated file, or empty string on failure
// -----------------------------------------------------------------------------
QString TTMkvMergeProvider::generateChapterFile(qint64 durationMs, int intervalMinutes,
                                                  const QString& outputDir)
{
    if (durationMs <= 0 || intervalMinutes <= 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("Invalid parameters for chapter generation"));
        return QString();
    }

    qint64 intervalMs = static_cast<qint64>(intervalMinutes) * 60 * 1000;

    // A name of its own: a fixed "chapters.txt" overwrote - and after the mux
    // deleted - a user file of that name in the output directory, and two
    // cuts into one directory shared it. The caller removes the file.
    QTemporaryFile chapterFile(QDir(outputDir).filePath("ttcut-chapters-XXXXXX.txt"));
    chapterFile.setAutoRemove(false);
    if (!chapterFile.open()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("Failed to create a chapter file in %1").arg(outputDir));
        return QString();
    }
    const QString chapterFilePath = chapterFile.fileName();

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

    if (TTSettings::instance()->logMkvMux())
        qDebug() << "Generated chapter file with" << (chapterNum - 1) << "chapters:"
                 << chapterFilePath;
    return chapterFilePath;
}
