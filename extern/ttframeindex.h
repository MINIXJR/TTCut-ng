/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFRAMEINDEX
// Frame/GOP index types shared by TTFFmpegWrapper and TTFrameIndexer
// ----------------------------------------------------------------------------

#ifndef TTFRAMEINDEX_H
#define TTFRAMEINDEX_H

#include <climits>

#include <QList>
#include <QVector>

// ----------------------------------------------------------------------------
// Frame information for frame index
// ----------------------------------------------------------------------------
struct TTFrameInfo {
    int64_t pts = 0;        // Presentation timestamp
    int64_t dts = 0;        // Decode timestamp
    int64_t fileOffset = 0; // Byte offset in file
    int64_t packetSize = 0; // Packet size in bytes
    int frameType = 0;      // AV_PICTURE_TYPE_I, _P, _B
    bool isKeyframe = false; // IDR frame (H.264) or keyframe
    int gopIndex = 0;       // Which GOP this frame belongs to
    int frameIndex = 0;     // Sequential frame number
    bool isFieldCoded = false; // true if merged from two PAFF field packets
    // True decode-order index of the frame that decodeFrame() delivers for this
    // (decode-order) position. Differs from frameIndex when B-frame reorder
    // shifts decode vs display order. Lazily filled on first decode; -1 = unknown.
    // Used by playback to seek mpv to the actually-displayed frame's time.
    int deliveredDecodeIndex = -1;

    // Display-order support (H.26x): POC from libav's codec parser, IDR flag
    // from packet NAL scan. INT_MIN poc == "not collected" (identity map).
    int  poc   = INT_MIN;
    bool isIDR = false;
    bool isDroppedLeading = false;   // HEVC RASL leading pic of a NoRaslOutputFlag IRAP

    // Internal scratch for the indexer's PAFF post-processing pass:
    int  paffFrameNum  = -1;     // -1 = not a field; else frame_num for matching
    bool isBottomField = false;  // valid only when isFieldCoded == true
};

// ----------------------------------------------------------------------------
// GOP (Group of Pictures) information
// ----------------------------------------------------------------------------
struct TTGOPInfo {
    int gopIndex;           // GOP number
    int startFrame;         // First frame index in this GOP
    int endFrame;           // Last frame index in this GOP
    int64_t startPts;       // PTS of first frame
    int64_t endPts;         // PTS of last frame
    bool isClosed;          // Closed GOP (no external references)
};

// H.264 PAFF field info from packet data
struct TTFieldInfo {
    bool isField;        // field_pic_flag
    bool isBottomField;  // bottom_field_flag
    int frameNum;        // frame_num from slice header
};

// A frame index alone is incomplete. isPAFF / frameMbsOnlyFlag /
// log2MaxFrameNum are produced ONLY by TTFrameIndexer::build() (SPS parse +
// packet scan). An adopter that receives the bare list keeps the constructor defaults,
// its decode-order tagging then counts PAFF field packets as frames, and
// decodeFrame() never sees a field-pair target AU — it drains the file to EOF
// (measured 72 675 ms on 06x03 display 3566, against 13 ms with metadata).
// Bundling them is what stops an index from travelling without its metadata.
// The defaults below MUST match TTFFmpegWrapper's own member defaults
// (ttffmpegwrapper.cpp:82 ff.).
// gops, rawToMerged and rawPacketCount are filled by TTFrameIndexer only; a
// wrapper re-exporting an adopted index leaves them empty.
struct TTFrameIndexBundle {
    QList<TTFrameInfo> index;
    QList<TTGOPInfo>   gops;            // GOP table derived from index (filled by TTFrameIndexer)
    QVector<int>       rawToMerged;     // raw packet -> merged frame (PAFF); empty = identity; ~w = collapsed bottom field of w
    int                rawPacketCount = 0;
    bool isPAFF           = false;
    bool frameMbsOnlyFlag = true;
    int  log2MaxFrameNum  = 4;

    bool isEmpty() const { return index.isEmpty(); }

    // --- Raw->merged AU map (PAFF) ---
    // buildFrameIndex scans one packet per AU ("raw"); for H.264 PAFF,
    // mergePAFFFieldsInIndex then collapses top+bottom field pairs, so the
    // final frameIndex() is "merged". .info doubled-PTS candidates are
    // raw-AU-numbered; these accessors translate. Only the index OWNER has
    // the map — wrappers that adopt an index via setFrameIndex() never ran
    // the merge and see identity (they adopt the already-merged list).
    // Encoding: rawToMerged entry >= 0 -> merged index; entry < 0 ->
    // collapsed bottom field, merged index = ~entry. Empty = identity.
    int rawToMergedIndex(int raw) const
    {
        if (raw < 0 || raw >= rawPacketCount) return -1;
        if (rawToMerged.isEmpty()) return raw;          // identity
        const int v = rawToMerged.at(raw);
        return v >= 0 ? v : ~v;
    }
    bool rawIsCollapsedField(int raw) const
    {
        if (raw < 0 || raw >= rawPacketCount) return false;
        return !rawToMerged.isEmpty() && rawToMerged.at(raw) < 0;
    }
};

#endif // TTFRAMEINDEX_H
