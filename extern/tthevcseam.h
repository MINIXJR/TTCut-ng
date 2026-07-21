/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2026 / ttcut-ng                                */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2026                                                      */
/* FILE     : tthevcseam.h                                                    */
/*----------------------------------------------------------------------------*/
/* HEVC seam machinery for the smart-cut RASL-preserving seam (Defekt A /     */
/* H.265): SPS/PPS field parsers, pps_id patcher, encoder slice rewriter.     */
/* Bitstream-level only — no libav dependency. Reference implementation and   */
/* measured ground truth: CLAUDE_TMP/TTCut-ng/hevc_rasl/poc/ (PoC 2026-07-21).*/
/*----------------------------------------------------------------------------*/

#ifndef TTHEVCSEAM_H
#define TTHEVCSEAM_H

#include <QByteArray>
#include <QString>
#include <QVector>

// Source/encoder SPS fields relevant for the seam fix. CABAC-/parse-relevant
// fields must match between source and encoder (compareHevcSpsForSeam);
// whitelisted fields (poc width, dpb sizes, VUI) may differ.
struct THevcSpsSeamInfo {
    bool valid = false;
    QString invalidReason;

    int  spsId = -1;
    int  maxSubLayersMinus1 = -1;
    int  chromaFormatIdc = -1;
    int  picWidth = 0;
    int  picHeight = 0;
    int  bitDepthLuma = 8;
    int  bitDepthChroma = 8;
    int  log2MaxPocLsb = -1;          // 4..16
    int  maxDecPicBufferingMinus1 = -1;   // informational (whitelisted)
    int  log2MinCbSizeMinus3 = -1;
    int  log2DiffMaxMinCbSize = -1;
    int  log2MinTbSizeMinus2 = -1;
    int  log2DiffMaxMinTbSize = -1;
    int  tuDepthInter = -1;           // max_transform_hierarchy_depth_inter
    int  tuDepthIntra = -1;
    bool scalingListEnabled = false;
    bool scalingListDataPresent = false;
    bool scalingListFlat16 = true;    // all matrices flat value 16 (== neutral)
    bool ampEnabled = false;
    bool saoEnabled = false;
    bool pcmEnabled = false;
    int  numShortTermRefPicSets = -1;
    bool longTermRefPicsPresent = false;
    bool temporalMvpEnabled = false;
    bool strongIntraSmoothing = false;
};

// PPS fields that determine slice-header layout (needed to parse/rebuild
// encoder slice headers) plus the id pair for the free-pps_id scan.
struct THevcPpsSeamInfo {
    bool valid = false;
    QString invalidReason;

    int  ppsId = -1;
    int  spsId = -1;
    bool dependentSliceSegments = false;
    bool outputFlagPresent = false;
    int  numExtraSliceHeaderBits = 0;
    bool signDataHiding = false;      // informational
    bool cabacInitPresent = false;
    int  numRefIdxL0DefaultMinus1 = 0;
    int  numRefIdxL1DefaultMinus1 = 0;
    bool weightedPred = false;
    bool weightedBipred = false;
    bool tilesEnabled = false;
    bool entropyCodingSync = false;   // WPP -> entry points in slice header
    bool ppsLoopFilterAcrossSlices = false;
    bool deblockingControlPresent = false;
    bool listsModificationPresent = false;
    bool sliceChromaQpOffsetsPresent = false;
    bool sliceHeaderExtension = false;
};

// Per-segment rewrite context, filled by TTESSmartCut preflight + first
// encoder packet. All POC values live in the SOURCE poc_lsb domain.
struct THevcSliceRewriteCtx {
    THevcSpsSeamInfo encSps;      // encoder SPS (parse-side widths/flags)
    THevcPpsSeamInfo encPps;      // encoder PPS (slice-header layout)
    bool encHeadersParsed = false;

    int  srcPocBits = 0;          // write-side poc_lsb width (source SPS)
    int  craPoc = -1;             // copy-start CRA poc_lsb (source domain)
    int  numRasl = 0;             // RASL AUs following the CRA
    int  pocBase = -1;            // poc for encoder packet index 0 (set in
                                  // reencodeFrames once N is known)
    int  encPpsId = -1;           // free pps_id for the encoder PPS
    QVector<int> retainPocs;      // absolute POCs from the CRA RPS (ascending)
};

// --- Parsers (input NAL may carry a 3- or 4-byte start code or none) -------
THevcSpsSeamInfo parseHevcSpsSeamInfo(const QByteArray& spsNal);
THevcPpsSeamInfo parseHevcPpsSeamInfo(const QByteArray& ppsNal);

// Copy-start CRA probe: parses the CRA slice header up to and including the
// short-term RPS (PPS-layout-agnostic apart from numExtraSliceHeaderBits,
// resolved via ppsExtraBitsById). On success fills craPoc + retainPocs.
bool parseHevcCraRpsInfo(const QByteArray& auData, int srcPocBits,
                         const QVector<int>& ppsExtraBitsById,
                         int* craPoc, QVector<int>* retainPocs,
                         QString* errorReason);

// PPS copy with pps_pic_parameter_set_id rewritten 0 -> newPpsId.
// Pure bit-shift after the id field (no semantic parse); trailing bits are
// re-derived. Input must carry a start code; output carries the same one.
// Returns empty array on failure.
QByteArray patchHevcPpsId(const QByteArray& ppsNalWithStartCode, int newPpsId);

// Rewrite one encoder packet (x265 output: optional VPS/SPS/PPS/SEI + one
// slice). Drops VPS+SPS (source sets rule), patches PPS to ctx.encPpsId,
// rewrites the slice (IDR->CRA demotion on packetIndex 0, POC anchoring,
// pps_id remap, RPS retain extension). Returns empty array on failure with
// *errorReason set — caller falls back to the standard seam.
QByteArray rewriteHevcEncoderPacket(const QByteArray& packetData,
                                    const THevcSliceRewriteCtx& ctx,
                                    int packetIndex,
                                    QString* errorReason);

// --- Shared low-level helpers (exposed for the diag harness) ---------------
QByteArray ttHevcDeescape(const QByteArray& nalData);
QByteArray ttHevcEscape(const QByteArray& rbsp);
// Returns payload offset (3, 4) for a start-coded NAL, or 0 if none.
int ttHevcStartCodeLen(const QByteArray& nal);

#endif // TTHEVCSEAM_H
