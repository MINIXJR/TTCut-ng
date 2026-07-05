/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2026 MINIXJR                                                 */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// TTDISPLAYORDERMAP
// Single source of truth for the H.26x display-order <-> decode-order (AU)
// mapping. POC values come from libav's AVCodecParser (battle-tested,
// AVCodecParserContext::output_picture_number == H.264 PicOrderCnt); display
// ranks are derived with a DPB-style bumping sort (window 16 = H.264/HEVC
// max DPB, flush at IDR). Verified bit-exact against decoder output order
// on MBAFF (162,530 frames), PAFF and HEVC streams (2026-06-12).

#ifndef TTDISPLAYORDERMAP_H
#define TTDISPLAYORDERMAP_H

#include <QVector>
#include <QString>
#include <cstdint>

struct TTPocEntry {
    int  poc             = 0;
    bool isIDR           = false;
    bool isDroppedLeading = false;   // RASL leading pic of a NoRaslOutputFlag IRAP
    bool key             = false;    // keyframe (I) — cold-start anchor for H.264 detection
};

class TTDisplayOrderMap
{
public:
    TTDisplayOrderMap() = default;

    // Pure algorithm: decode-ordered POC entries -> display rank per decode
    // position. Returns a vector of size n: -1 for dropped leading pictures
    // (RASL of a NoRaslOutputFlag IRAP, flagged isDroppedLeading), and 0..m-1
    // for the m decodable entries (m == count of non-dropped entries).
    static QVector<int> displayRanksFromPoc(const QVector<TTPocEntry>& entries);

    // Mark H.264 open-GOP cold-start leading pictures as isDroppedLeading.
    // At stream start a non-IDR I-frame's leading pictures (display before it,
    // reference a GOP before it that does not exist yet) are dropped by every
    // conforming decoder — libav emits count() minus these many frames. HEVC
    // handles its RASL equivalent via TTLeadingPicClassifier (NAL types); H.264
    // has no such NAL flag, so we detect them from POC: the contiguous run after
    // the first keyframe with POC < keyframe-POC. No-op for non-H.264 codecs, for
    // an IDR cold start (self-contained), and for streams without leading pics.
    // Idempotent. Both build paths call this before build()/displayRanksFromPoc.
    static void markH264ColdStartLeadingPics(QVector<TTPocEntry>& entries, int avCodecId);

    // Build both directions from decode-ordered entries.
    void build(const QVector<TTPocEntry>& entries);

    // Build directly from precomputed ranks (used by TTFFmpegWrapper when
    // the index is shared between wrapper instances).
    void buildFromRanks(const QVector<int>& decodeToDisplay);

    // Standalone build: own libav parser pass over an ES file (H.264/H.265).
    // Used by TTESSmartCut when no wrapper map was injected (--auto-cut etc.).
    // Returns an invalid map on failure.
    static TTDisplayOrderMap buildFromFile(const QString& filePath);

    bool isValid() const        { return !mDecodeToDisplay.isEmpty(); }
    int  count() const        { return mDecodeToDisplay.size(); }   // raw decode dimension (n)
    int  displayCount() const { return mDisplayToDecode.size(); }   // decodable/navigable frames (m)

    // Out-of-range indices return the input (identity) — callers at stream
    // edges stay safe.
    int decodeToDisplay(int decodeIdx) const;
    int displayToDecode(int displayPos) const;

private:
    QVector<int> mDecodeToDisplay;
    QVector<int> mDisplayToDecode;
};

// Forward-declare libav struct types so the header stays independent of the
// libav includes (the .cpp pulls them in via extern "C").
struct AVCodecParserContext;
struct AVCodecContext;

// Feeds ES packets through libav's codec parser and collects one TTPocEntry
// per emitted access unit. Parser emissions lag the input by 0..N packets;
// IDR is detected input-side and queued so it stays aligned with the emission
// it corresponds to. Both paths (MBAFF with 2-packet AUs and standard 1-packet
// AUs) produce one entry per emitted access unit.
//
// Two usage modes:
//   1. feedPacket(data, size, isIDR) — IDR queued with each packet,
//      dequeued on emission. Used by buildFromFile.
//   2. feedPacket(data, size) + packetIsIDR() called separately — used by
//      TTFFmpegWrapper where IDR is stored per-TTFrameInfo at scan time.
class TTPocCollector
{
public:
    // avCodecId: AV_CODEC_ID_H264 / _HEVC (AV_CODEC_ID_NONE → isOpen() false, all no-ops).
    // trackIDR: if true, activates mode 1 — IDR flags passed to feedPacket are
    //   queued and dequeued at emission time, populating entries() in addition to pocs().
    //   Must be true from construction if IDR tracking is needed (late activation
    //   would misassign IDR flags to wrong emissions).
    explicit TTPocCollector(int avCodecId, bool trackIDR = false);
    ~TTPocCollector();

    bool isOpen() const { return mParser != nullptr; }

    // Feed one complete demuxed packet.
    // In mode 1 (trackIDR=true), isIDR is queued and matched to the emitted AU.
    // In mode 2 (trackIDR=false), isIDR is ignored and only pocs() is populated.
    void feedPacket(const uint8_t* data, int size, bool isIDR = false);
    // Flush the parser; call once after the last packet.
    void finish();

    // Decode-order POC per emitted AU (both modes).
    const QVector<int>& pocs() const { return mPocs; }

    // Decode-order TTPocEntry per emitted AU (mode 1 only; empty in mode 2).
    const QVector<TTPocEntry>& entries() const { return mEntries; }

    // IDR detection on raw packet data (input-side, exact per-packet pairing).
    static bool packetIsIDR(const uint8_t* data, int size, int avCodecId);

private:
    AVCodecParserContext* mParser   = nullptr;
    AVCodecContext*       mCtx      = nullptr;
    QVector<int>          mPocs;
    QVector<TTPocEntry>   mEntries;    // populated only in mode 1
    QVector<bool>         mIdrQueue;   // pending IDR flags (mode 1), dequeued on emission
    bool                  mTrackIDR;   // set at construction

    void onEmit();  // called when parser emits an AU; records poc + dequeued IDR
};

// Stateful per-AU classifier for HEVC RASL leading pictures that a conforming
// decoder drops: the RASL pics associated with a NoRaslOutputFlag IRAP (the
// first IRAP in the bitstream, any IRAP after an EOS, and every BLA). Single
// source of truth for the drop rule, shared by TTFFmpegWrapper's index scan and
// TTDisplayOrderMap::buildFromFile. Non-HEVC codecs: classifyPacket() is a no-op
// returning false (H.264/MPEG-2 have no RASL NAL types).
class TTLeadingPicClassifier
{
public:
    explicit TTLeadingPicClassifier(int avCodecId);   // AV_CODEC_ID_HEVC -> active

    // Feed one complete demuxed AU packet (Annex-B). Returns true iff this AU is
    // a RASL leading pic of a NoRaslOutputFlag IRAP (i.e. dropped, no display pos).
    bool classifyPacket(const uint8_t* data, int size);

private:
    bool mIsHevc        = false;
    bool mSeenFirstIrap = false;   // for the "first IRAP in bitstream" rule
    bool mPrevWasEos    = false;   // an EOS/EOB NAL was seen -> next IRAP is NoRaslOutput
    bool mInNoRaslLeading = false; // inside the leading sequence of a NoRaslOutput IRAP
};

#endif // TTDISPLAYORDERMAP_H
