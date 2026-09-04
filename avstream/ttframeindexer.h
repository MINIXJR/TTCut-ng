/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFRAMEINDEXER
// Builds a TTFrameIndexBundle from a video file: one libav packet scan, the
// PAFF field merge, the GOP table. Split out of TTFFmpegWrapper so an index
// can be produced without a decoder instance.
// ----------------------------------------------------------------------------

#ifndef TTFRAMEINDEXER_H
#define TTFRAMEINDEXER_H

#include <cstdint>
#include <functional>

#include <QString>

#include "ttframeindex.h"

// Forward declaration for libav types (avoid including C headers in .h)
struct AVFormatContext;

class TTFrameIndexer
{
public:
    using ProgressFn = std::function<void(int percent, const QString& message)>;

    TTFrameIndexer() = default;
    // build() always closes the context it opened, so mFormatCtx is null
    // outside build(). The destructor guards the failure paths; deleting the
    // copy operations keeps the single owner of that context single.
    ~TTFrameIndexer();

    TTFrameIndexer(const TTFrameIndexer&) = delete;
    TTFrameIndexer& operator=(const TTFrameIndexer&) = delete;

    // Opens filePath, scans every video packet into a bundle, closes the
    // file. videoStreamIndex -1 = best video stream. progress may be null.
    // Returns true even when the scan found no video packet (frames=0), as
    // TTFFmpegWrapper::buildFrameIndex() did; callers check the bundle size.
    bool build(const QString& filePath, int videoStreamIndex, const ProgressFn& progress);
    const TTFrameIndexBundle& bundle() const { return mBundle; }
    QString lastError() const { return mLastError; }

    // H.264 slice-header field info (field_pic_flag, bottom_field_flag,
    // frame_num). Shared with TTFFmpegWrapper::decodeOrderTagForPacket.
    static TTFieldInfo parseH264FieldInfo(const uint8_t* data, int size,
                                          bool frameMbsOnlyFlag, int log2MaxFrameNum);

private:
    // Parse the stream's SPS extradata; sets mBundle.log2MaxFrameNum and
    // mBundle.frameMbsOnlyFlag (H.264 PAFF detection).
    void parseH264SpsFromExtradata(const uint8_t* data, int size);

    // Validate the stream index, clear the bundle, seek to byte 0 (ES) or
    // PTS 0 (container), parse SPS extradata for H.264 PAFF detection.
    // Returns false on validation/seek failure.
    bool setupIndexingPass(int videoStreamIndex);

    // Outer av_read_frame loop. Appends one TTFrameInfo per video packet
    // (top fields, bottom fields, and normal frames are all separate
    // entries). Sets mBundle.isPAFF = true when a field packet is found.
    // Leaves gopIndex and frameIndex at -1 (filled in by finalizeFrameIndex).
    // Reports progress through progressFn, which may be null. The callback is
    // NOT named "progress" here: the loop has its own int64_t progress.
    void scanPacketsIntoRawIndex(int videoStreamIndex, const ProgressFn& progressFn);

    // PAFF post-processing: walk mBundle.index, collapse adjacent
    // top+bottom field pairs (matching paffFrameNum) into a single entry
    // (top's fields + summed packetSize). No-op if !mBundle.isPAFF. In-place.
    void mergePAFFFieldsInIndex();

    // Walk mBundle.index assigning gopIndex (incremented at each keyframe)
    // and frameIndex (= position) to every entry.
    void finalizeFrameIndex();

    // For elementary streams whose first frame has no PTS: walk mBundle.index
    // and assign sequential PTS/DTS values from frame rate (read from .info
    // file or stream metadata). Validates and falls back to 25 fps. Halves the
    // PAFF rate.
    void assignPtsFromFrameRate(int videoStreamIndex);

    // Derive mBundle.gops from the finalized index (one entry per gopIndex
    // run). Was TTFFmpegWrapper::buildGOPIndex.
    void buildGops();

    // Derive mBundle.displayMap from the poc/isIDR fields of the finalized
    // index. Falls back to the identity map when POC data is missing or
    // degenerate. Was TTFFmpegWrapper::buildDisplayOrderMap; the codec id
    // gates the H.264 cold-start leading-picture marking.
    void buildDisplayMap(int avCodecId);

    void setError(const QString& error);

    AVFormatContext*   mFormatCtx = nullptr;
    TTFrameIndexBundle mBundle;
    QString            mLastError;
};

#endif // TTFRAMEINDEXER_H
