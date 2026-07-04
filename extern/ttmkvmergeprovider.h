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

#ifndef TTMKVMERGEPROVIDER_H
#define TTMKVMERGEPROVIDER_H

#include <QString>
#include <QVector>
#include <QStringList>
#include <QList>
#include <QObject>
#include <QMap>

// Libav forward decls — keep heavy headers out of this public header.
struct AVFormatContext;
struct AVPacket;

// -----------------------------------------------------------------------------
// TTMkvMergeProvider
// MKV container output using libav matroska muxer (no external binary needed)
// -----------------------------------------------------------------------------
class TTMkvMergeProvider : public QObject
{
    Q_OBJECT

public:
    TTMkvMergeProvider();
    virtual ~TTMkvMergeProvider();

    // Main muxing function
    bool mux(const QString& outputFile,
             const QString& videoFile,
             const QStringList& audioFiles,
             const QStringList& subtitleFiles = QStringList());

    // Audio-only matroska output (typically .mka): copies all given audio
    // streams into one matroska container with optional language tags.
    bool muxAudioOnly(const QString& outputFile,
                      const QStringList& audioFiles,
                      const QStringList& audioLanguages = QStringList());

    // Always available (libav is linked at build time)
    bool isAvailable() const;
    QString lastError() const { return mLastError; }

    // MKV-specific options
    void setDefaultDuration(const QString& trackType, const QString& duration);
    void setTrackName(int trackId, const QString& name);
    void setLanguage(int trackId, const QString& lang);
    void setChapterFile(const QString& chapterFile);

    // Language tags for audio/subtitle tracks (ISO 639-2/B)
    void setAudioLanguages(const QStringList& languages);
    void setSubtitleLanguages(const QStringList& languages);

    // A/V sync offset in milliseconds (from .info file)
    void setAudioSyncOffset(int offsetMs);
    void setVideoSyncOffset(int offsetMs);

    // Total duration for chapter end calculation (avoids INT64_MAX overflow)
    void setTotalDurationMs(qint64 durationMs);

    // PAFF: H.264 field-coded stream — 2 field packets per frame in ES.
    // log2MaxFrameNum needed to parse field_pic_flag from slice headers.
    void setIsPAFF(bool paff, int log2MaxFrameNum = 4) {
        mIsPAFF = paff;
        mH264Log2MaxFrameNum = log2MaxFrameNum;
    }

    // Video codec of the ES input stream — used by the muxer to parse
    // NAL unit types correctly. H.264 and H.265 have different header layouts.
    // Caller (ttavdata.cpp) passes an AVCodecID value from libavcodec/codec_id.h
    // (implicit enum-to-int conversion). Stored as int to keep libav headers
    // out of this public header.
    void setVideoCodecId(int codecId) { mVideoCodecId = codecId; }

    // Display order of the video ES packets (from TTESSmartCut::
    // outputDisplayOrder()). Empty list = legacy linear PTS assignment.
    void setVideoDisplayOrder(const QVector<int>& order) { mVideoDisplayOrder = order; }

    // MPEG-2: derive the display order directly from the ES bitstream
    // (temporal_reference per picture header, GOP-relative). Returns an
    // empty list on any inconsistency (per-GOP permutation check fails,
    // no pictures found) - callers then keep legacy linear PTS. Static
    // and side-effect-free so the fallback path is unit-testable.
    static QVector<int> buildMpeg2DisplayOrder(const QString& filePath);

    // Compatibility stubs (always available — libav is built-in)
    static bool isMkvMergeInstalled();
    static QString mkvMergeVersion();
    static QString mkvMergePath();

    // Chapter generation
    static QString generateChapterFile(qint64 durationMs, int intervalMinutes,
                                        const QString& outputDir);

signals:
    void progressChanged(int percent, const QString& message);

private:
    QString mLastError;
    QString mChapterFile;
    int mAudioSyncOffsetMs;
    int mVideoSyncOffsetMs;
    qint64 mTotalDurationMs;
    bool mIsPAFF;
    int mH264Log2MaxFrameNum;
    int mVideoCodecId;
    QVector<int> mVideoDisplayOrder;  // display-PTS order for video ES (may be empty)   // AVCodecID value (from libavcodec/codec_id.h)
    QStringList mAudioLanguages;
    QStringList mSubtitleLanguages;

    struct TrackOption {
        QString name;
        QString language;
        QString defaultDuration;
    };
    QMap<int, TrackOption> mTrackOptions;

    // One input stream for the interleaved mux loop.
    // Definition lives in the header so private member helpers can take it
    // by reference. AVFormatContext/AVPacket are forward-declared above —
    // libav headers stay out of clients of this header.
    struct MuxInput {
        AVFormatContext* fmtCtx;
        int srcIdx;          // Stream index in source file
        int outIdx;          // Stream index in output file
        AVPacket* pkt;
        bool eof;
        int64_t syncMs;      // Sync offset in milliseconds
        bool assignPts;      // True = assign PTS from frameCount (raw ES video)
        int64_t frameDur;    // Frame duration in output time_base units
        int64_t frameCount;  // Frame counter for PTS assignment
        bool ownsCtx;        // True = this MuxInput owns the AVFormatContext
        // Display position per packet (frame units, from TTESSmartCut).
        // Empty = legacy linear PTS. reorderOffset = max(i - displayOrder[i])
        // lowers DTS so pts >= dts holds without shifting PTS against audio.
        QVector<int> displayOrder;
        int reorderOffset;
        bool displayOrderWarned;  // one-shot mismatch warning latch
        MuxInput()
            : fmtCtx(nullptr), srcIdx(-1), outIdx(-1), pkt(nullptr), eof(false)
            , syncMs(0), assignPts(false), frameDur(0), frameCount(0)
            , ownsCtx(false), reorderOffset(0), displayOrderWarned(false) {}
    };

    // mux() implementation split — see docs/superpowers/specs/2026-05-03-mux-split-refactor.md
    void assignEsTimestamps(MuxInput& in);

    bool setupVideoInput(AVFormatContext* outCtx,
                          AVFormatContext* videoInCtx,
                          MuxInput& outVin,
                          int64_t& videoDurationNs);

    bool addAudioInputs(AVFormatContext* outCtx,
                         const QStringList& audioFiles,
                         const QStringList& languages,
                         int& nextOutIdx,
                         QList<MuxInput>& inputs,
                         int audioSyncMs);

    bool addSubtitleInputs(AVFormatContext* outCtx,
                            const QStringList& subtitleFiles,
                            int& nextOutIdx,
                            QList<MuxInput>& inputs);

    bool processPAFFFieldPair(MuxInput& in,
                               int& activeLog2MaxFrameNum,
                               int64_t totalPacketsWritten);

    // Per-input read helper + normalized PTS calc (used by interleaved write loop).
    // Not static so they can take MuxInput& without exposing the struct.
    bool readNextPacket(MuxInput& in);
    int64_t getNormalizedPts(const MuxInput& in,
                              const AVFormatContext* outCtx) const;

    void setError(const QString& error);
};

#endif // TTMKVMERGEPROVIDER_H
