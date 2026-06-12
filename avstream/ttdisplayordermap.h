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
    int  poc   = 0;
    bool isIDR = false;
};

class TTDisplayOrderMap
{
public:
    TTDisplayOrderMap() = default;

    // Pure algorithm: decode-ordered POC entries -> display rank per decode
    // position. Returned vector is a permutation of 0..n-1.
    static QVector<int> displayRanksFromPoc(const QVector<TTPocEntry>& entries);

    // Build both directions from decode-ordered entries.
    void build(const QVector<TTPocEntry>& entries);

    // Build directly from precomputed ranks (used by TTFFmpegWrapper when
    // the index is shared between wrapper instances).
    void buildFromRanks(const QVector<int>& decodeToDisplay);

    // Standalone build: own libav parser pass over an ES file (H.264/H.265).
    // Used by TTESSmartCut when no wrapper map was injected (--auto-cut etc.).
    // Returns an invalid map on failure.
    static TTDisplayOrderMap buildFromFile(const QString& filePath);

    bool isValid() const { return !mDecodeToDisplay.isEmpty(); }
    int  count() const   { return mDecodeToDisplay.size(); }

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
// per emitted access unit. Parser emissions lag the input by one packet, so
// POC is collected emission-side while IDR is detected input-side (packet
// data scan) — both end up aligned per decode-order position.
class TTPocCollector
{
public:
    explicit TTPocCollector(int avCodecId);     // AV_CODEC_ID_H264 / _HEVC
    ~TTPocCollector();

    bool isOpen() const { return mParser != nullptr; }

    // Feed one complete demuxed packet; appends 0..n POC values internally.
    void feedPacket(const uint8_t* data, int size);
    // Flush the parser; call once after the last packet.
    void finish();

    // Decode-order POC per emitted AU.
    const QVector<int>& pocs() const { return mPocs; }

    // IDR detection on raw packet data (input-side, exact per-packet pairing).
    static bool packetIsIDR(const uint8_t* data, int size, int avCodecId);

private:
    AVCodecParserContext* mParser = nullptr;
    AVCodecContext*       mCtx    = nullptr;
    QVector<int>          mPocs;
};

#endif // TTDISPLAYORDERMAP_H
