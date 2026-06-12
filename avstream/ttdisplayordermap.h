/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2026 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2026                                                      */
/* FILE     : ttdisplayordermap.h                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                          DATE: 06/12/2026 */
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

#endif // TTDISPLAYORDERMAP_H
