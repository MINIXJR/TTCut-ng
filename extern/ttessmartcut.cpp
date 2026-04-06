/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttessmartcut.cpp                                                */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2026  */
/*----------------------------------------------------------------------------*/

#include "ttessmartcut.h"
#include "../avstream/ttesinfo.h"
#include "../common/ttcut.h"

#include <QDebug>
#include <QFileInfo>

// Include libav headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// Forward declarations for static helpers used by member functions
struct H264SpsInfo {
    int log2MaxFrameNumMinus4;   // -1 on error
    int pocType;                 // pic_order_cnt_type (0, 1, or 2)
    int log2MaxPocLsbMinus4;     // only valid if pocType == 0, -1 otherwise
    bool frameMbsOnly;           // frame_mbs_only_flag
};
static H264SpsInfo parseH264SpsInfo(const QByteArray& spsNal);
static int readFrameNumFromAU(const QByteArray& auData, int frameNumBitWidth);
static int readPocLsbFromAU(const QByteArray& auData, int frameNumBitWidth,
                             int pocLsbBitWidth, bool frameMbsOnly);
static bool findH264SpsInPacket(const QByteArray& packetData, H264SpsInfo& spsInfo);
static QByteArray patchSpsNalsInAccessUnit(const QByteArray& auData, int maxReorderFrames);

// IDR injection and SPS unification helpers
struct H264PpsInfo {
    bool entropyCodingModeFlag;            // 0=CAVLC, 1=CABAC
    bool bottomFieldPicOrderPresent;       // bottom_field_pic_order_in_frame_present_flag
    bool deblockingFilterControlPresent;   // deblocking_filter_control_present_flag
    bool redundantPicCntPresent;           // redundant_pic_cnt_present_flag
    bool weightedPredFlag;                 // weighted_pred_flag (P-slices)
    int  weightedBipredIdc;                // weighted_bipred_idc (B-slices)
    int  numRefIdxL0DefaultActiveMinus1;   // for pred_weight_table parsing
    int  numRefIdxL1DefaultActiveMinus1;   // for B-slice pred_weight_table parsing
    bool valid;                            // true if parsing succeeded
};
static H264PpsInfo parseH264PpsInfo(const QByteArray& ppsNal);
static QByteArray convertAUToIDR(const QByteArray& auData,
    int log2MaxFrameNum, int pocType, int log2MaxPocLsb,
    bool frameMbsOnly, const H264PpsInfo& pps);

// SPS unification helpers (for PAFF seamless re-encode→stream-copy transition)
static QByteArray rewriteEncoderSliceForSourceSps(
    const QByteArray& nalBody,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const H264PpsInfo& encPps, uint32_t newPpsId, int frameIndex);
static QByteArray rewriteEncoderPacketForSourceSps(
    const QByteArray& packetData,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const H264PpsInfo& encPps, uint32_t newPpsId, int frameIndex);
static QByteArray extractPpsFromPacket(const QByteArray& packetData);
static QByteArray patchPpsId(const QByteArray& ppsNal, uint32_t newPpsId);

// MMCO neutralization for stream-copy AUs after EOS
static QByteArray neutralizeMmcoInAU(const QByteArray& auData,
    int log2MaxFrameNum, int pocLsbBitWidth, bool frameMbsOnly,
    const H264PpsInfo& pps);
static QByteArray patchFrameNumInAU(const QByteArray& auData, int frameNumBitWidth,
    int frameNumDelta, int maxFrameNum);

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
TTESSmartCut::TTESSmartCut()
    : QObject()
    , mIsInitialized(false)
    , mPresetOverride(-1)
    , mFrameRate(25.0)
    , mDecoder(nullptr)
    , mEncoder(nullptr)
    , mDecodedWidth(0)
    , mDecodedHeight(0)
    , mDecodedPixFmt(AV_PIX_FMT_NONE)
    , mInterlaced(false)
    , mTopFieldFirst(true)
    , mReorderDelay(0)
    , mLog2MaxFrameNum(0)
    , mLog2MaxPocLsb(0)
    , mPocType(-1)
    , mFrameMbsOnly(true)
    , mEncoderLog2MaxFrameNum(0)
    , mEncoderLog2MaxPocLsb(0)
    , mEncoderPocType(-1)
    , mEncoderFrameMbsOnly(true)
    , mSpsUnification(false)
    , mSpsUnificationOutFile(nullptr)
    , mEncoderPacketsWritten(0)
    , mSyntheticPpsNeeded(false)
    , mPpsReserveOffset(-1)
    , mEncoderPts(0)
    , mFramesStreamCopied(0)
    , mFramesReencoded(0)
    , mBytesWritten(0)
{
}

// ----------------------------------------------------------------------------
// Destructor
// ----------------------------------------------------------------------------
TTESSmartCut::~TTESSmartCut()
{
    cleanup();
}

// ----------------------------------------------------------------------------
// Initialize with ES file
// ----------------------------------------------------------------------------
bool TTESSmartCut::initialize(const QString& esFile, double frameRate)
{
    cleanup();

    mInputFile = esFile;

    // Try to get frame rate from .info file if not provided
    if (frameRate <= 0) {
        QString infoFile = TTESInfo::findInfoFile(esFile);
        if (!infoFile.isEmpty()) {
            TTESInfo info(infoFile);
            if (info.isLoaded() && info.frameRate() > 0) {
                frameRate = info.frameRate();
                qDebug() << "TTESSmartCut: Using frame rate from .info:" << frameRate;
            }
        }
    }

    // Default to 25fps if still no frame rate
    if (frameRate <= 0) {
        frameRate = 25.0;
        qDebug() << "TTESSmartCut: No frame rate found, using default:" << frameRate;
    }
    mFrameRate = frameRate;

    // Open and parse the ES file
    if (!mParser.openFile(esFile)) {
        setError(QString("Cannot open ES file: %1").arg(mParser.lastError()));
        return false;
    }

    qDebug() << "TTESSmartCut: Parsing ES file...";
    emit progressChanged(0, tr("Parsing ES file..."));

    if (!mParser.parseFile()) {
        setError(QString("Cannot parse ES file: %1").arg(mParser.lastError()));
        mParser.closeFile();
        return false;
    }

    // Correct frame rate if PAFF detected with old .info file
    if (mParser.isPAFF() && mFrameRate > 30) {
        qDebug() << "TTESSmartCut: PAFF detected, correcting frame rate from" << mFrameRate
                 << "to" << mFrameRate / 2.0;
        mFrameRate /= 2.0;
    }

    // Parse H.264 SPS for frame_num patching and POC domain mismatch fix
    if (mParser.codecType() == NALU_CODEC_H264 && mParser.spsCount() > 0) {
        QByteArray sps = mParser.getSPS(0);
        H264SpsInfo spsInfo = parseH264SpsInfo(sps);
        if (spsInfo.log2MaxFrameNumMinus4 >= 0) {
            mLog2MaxFrameNum = spsInfo.log2MaxFrameNumMinus4 + 4;
            mPocType = spsInfo.pocType;
            mLog2MaxPocLsb = (spsInfo.pocType == 0) ? spsInfo.log2MaxPocLsbMinus4 + 4 : 0;
            mFrameMbsOnly = spsInfo.frameMbsOnly;
            qDebug() << "TTESSmartCut: Source SPS - log2_max_frame_num=" << mLog2MaxFrameNum
                     << "poc_type=" << mPocType << "log2_max_poc_lsb=" << mLog2MaxPocLsb
                     << "frame_mbs_only=" << mFrameMbsOnly;
        } else {
            qDebug() << "TTESSmartCut: WARNING - could not parse SPS";
        }
    }

    qDebug() << "TTESSmartCut: Initialization complete";
    qDebug() << "  File:" << esFile;
    qDebug() << "  Codec:" << mParser.codecName();
    qDebug() << "  Frames:" << mParser.accessUnitCount();
    qDebug() << "  GOPs:" << mParser.gopCount();
    qDebug() << "  Frame rate:" << mFrameRate << "fps";

    mIsInitialized = true;
    return true;
}

// ----------------------------------------------------------------------------
// Cleanup
// ----------------------------------------------------------------------------
void TTESSmartCut::cleanup()
{
    freeDecoder();
    freeEncoder();
    mParser.closeFile();
    mIsInitialized = false;
    mDecodedWidth = 0;
    mDecodedHeight = 0;
    mDecodedPixFmt = AV_PIX_FMT_NONE;
    mReorderDelay = 0;
    mLog2MaxFrameNum = 0;
    mLog2MaxPocLsb = 0;
    mPocType = -1;
    mFrameMbsOnly = true;
    mEncoderLog2MaxFrameNum = 0;
    mEncoderLog2MaxPocLsb = 0;
    mSpsUnification = false;
    mSpsUnificationOutFile = nullptr;
    mEncoderPacketsWritten = 0;
    mEncoderPocType = -1;
    mEncoderFrameMbsOnly = true;
    mEncoderPts = 0;
    mFramesStreamCopied = 0;
    mFramesReencoded = 0;
    mBytesWritten = 0;
}

// ----------------------------------------------------------------------------
// Get codec type
// ----------------------------------------------------------------------------
TTNaluCodecType TTESSmartCut::codecType() const
{
    return mParser.codecType();
}

// ----------------------------------------------------------------------------
// Get frame count
// ----------------------------------------------------------------------------
int TTESSmartCut::frameCount() const
{
    return mParser.accessUnitCount();
}

// ----------------------------------------------------------------------------
// Get GOP count
// ----------------------------------------------------------------------------
int TTESSmartCut::gopCount() const
{
    return mParser.gopCount();
}

// ----------------------------------------------------------------------------
// Get B-frame reorder delay (measured from decoder after first reencode)
// ----------------------------------------------------------------------------
int TTESSmartCut::reorderDelay() const
{
    return mReorderDelay;
}

// ----------------------------------------------------------------------------
// Convert time to frame index
// ----------------------------------------------------------------------------
int TTESSmartCut::timeToFrame(double timeSeconds) const
{
    return qRound(timeSeconds * mFrameRate);
}

// ----------------------------------------------------------------------------
// Convert frame index to time
// ----------------------------------------------------------------------------
double TTESSmartCut::frameToTime(int frameIndex) const
{
    return frameIndex / mFrameRate;
}

// ----------------------------------------------------------------------------
// Smart Cut (time-based)
// ----------------------------------------------------------------------------
bool TTESSmartCut::smartCut(const QString& outputFile,
                            const QList<QPair<double, double>>& cutList)
{
    // Convert time-based cut list to frame-based
    QList<QPair<int, int>> cutFrames;
    for (const auto& segment : cutList) {
        int startFrame = timeToFrame(segment.first);
        int endFrame = timeToFrame(segment.second);
        cutFrames.append(qMakePair(startFrame, endFrame));
    }

    return smartCutFrames(outputFile, cutFrames);
}

// ----------------------------------------------------------------------------
// Smart Cut (frame-based)
// ----------------------------------------------------------------------------
bool TTESSmartCut::smartCutFrames(const QString& outputFile,
                                   const QList<QPair<int, int>>& cutFrames)
{
    if (!mIsInitialized) {
        setError("Not initialized - call initialize() first");
        return false;
    }

    if (cutFrames.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    qDebug() << "TTESSmartCut: Starting smart cut";
    qDebug() << "  Input:" << mInputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutFrames.size();

    // Reset statistics
    mFramesStreamCopied = 0;
    mFramesReencoded = 0;
    mBytesWritten = 0;
    mTotalFrames = 0;
    mCurrentSegment = 0;
    mTotalSegments = 0;
    mActualOutputRanges.clear();

    // Analyze cut points
    QList<TTCutSegmentInfo> segments = analyzeCutPoints(cutFrames);

    // First segment override: The decoder starts with an empty delayed_pic[]
    // reorder buffer, so no IDR barrier is needed. Force pure stream-copy
    // to avoid unnecessary re-encoding at non-IDR I-frame cut-ins.
    if (!segments.isEmpty() && segments[0].needsReencodeAtStart) {
        TTAccessUnit firstAU = mParser.accessUnitAt(segments[0].startFrame);
        if (firstAU.isKeyframe) {
            qDebug() << "  First segment: overriding re-encode to pure stream-copy"
                     << "(decoder starts fresh, no delayed_pic[] barrier needed)";
            segments[0].needsReencodeAtStart = false;
            segments[0].reencodeStartFrame = -1;
            segments[0].reencodeEndFrame = -1;
            segments[0].streamCopyStartFrame = segments[0].startFrame;
            segments[0].streamCopyEndFrame = segments[0].endFrame;
        }
    }

    // Open output file
    QFile outFile(outputFile);
    if (!outFile.open(QIODevice::WriteOnly)) {
        setError(QString("Cannot create output file: %1").arg(outputFile));
        return false;
    }

    // For PAFF H.264 SPS unification: encoder PPS(id=1) is written INLINE
    // before the re-encode section (in processSegment), not at the ES start.
    // Writing filler/PPS at the ES start corrupts the MKV muxer's NAL parsing
    // ("Invalid NAL unit size" errors on all source keyframe AUs).
    // Seeking into the re-encode section still works because the decoder finds
    // the inline PPS when decoding from the nearest keyframe.
    mSyntheticPpsNeeded = false;
    mPpsReserveOffset = -1;

    // Detect B-frame reorder delay from parsed stream structure.
    // This is needed for SPS patching so the decoder pre-allocates its
    // reorder buffer instead of increasing it on-the-fly (which causes stutter).
    if (mReorderDelay == 0) {
        mReorderDelay = mParser.computeReorderDelay();
        if (mReorderDelay > 0) {
            qDebug() << "  B-frame reorder delay from stream analysis:" << mReorderDelay;
        }
    }

    // Process each segment
    mTotalFrames = 0;
    for (const auto& seg : segments) {
        mTotalFrames += (seg.endFrame - seg.startFrame + 1);
    }

    // H.264 frame_num patching: track cumulative delta for inter-segment continuity.
    // Without this, frame_num gaps at segment boundaries cause the decoder to generate
    // dummy reference frames, resulting in visual stuttering/flashing.
    int cumulativeFrameNumDelta = 0;
    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

    mTotalSegments = segments.size();
    for (int i = 0; i < segments.size(); ++i) {
        const TTCutSegmentInfo& seg = segments[i];
        mCurrentSegment = i + 1;

        qDebug() << "  Processing segment" << i << ":"
                 << "frames" << seg.startFrame << "->" << seg.endFrame;

        if (seg.needsReencodeAtStart) {
            qDebug() << "    Re-encode:" << seg.reencodeStartFrame
                     << "->" << seg.reencodeEndFrame;
        }
        qDebug() << "    Stream-copy:" << seg.streamCopyStartFrame
                 << "->" << seg.streamCopyEndFrame;

        int segActualStart = -1;
        if (!processSegment(outFile, seg, cumulativeFrameNumDelta, &segActualStart)) {
            outFile.close();
            return false;
        }

        // Record actual output range (start may differ due to B-frame reorder)
        int actualStart = (segActualStart >= 0) ? segActualStart : seg.startFrame;
        mActualOutputRanges.append(qMakePair(actualStart, seg.endFrame));

        // Between segments: write EOS NAL + SPS/PPS, compute frame_num delta
        if (i < segments.size() - 1) {
            // Write EOS NAL to flush decoder DPB
            QByteArray eosNal;
            if (mParser.codecType() == NALU_CODEC_H265) {
                eosNal = QByteArray::fromHex("000000014A01");  // EOB_NUT (type 37)
            } else {
                eosNal = QByteArray::fromHex("000000010B");    // end_of_stream (type 11)
            }
            outFile.write(eosNal);
            writeParameterSets(outFile, mReorderDelay);
            qDebug() << "    Wrote EOS + SPS/PPS between segments" << i << "and" << i + 1;

            // Compute frame_num delta for next segment (H.264 only)
            if (mLog2MaxFrameNum > 0 && mParser.codecType() == NALU_CODEC_H264) {
                // Find last reference frame (I or P, not B) in this segment's stream-copy range
                int lastRef = seg.streamCopyEndFrame;
                if (lastRef < 0) lastRef = seg.endFrame;
                int searchStart = (seg.streamCopyStartFrame >= 0)
                    ? seg.streamCopyStartFrame : seg.startFrame;

                while (lastRef > searchStart) {
                    TTAccessUnit au = mParser.accessUnitAt(lastRef);
                    if (au.sliceType != H264::SLICE_B && au.sliceType != H264::SLICE_B_ALL)
                        break;
                    lastRef--;
                }

                QByteArray lastAU = mParser.readAccessUnitData(lastRef);
                int lastRefFN = readFrameNumFromAU(lastAU, mLog2MaxFrameNum);

                // Next segment's first stream-copy frame
                const TTCutSegmentInfo& nextSeg = segments[i + 1];
                int nextStart = (nextSeg.streamCopyStartFrame >= 0)
                    ? nextSeg.streamCopyStartFrame : nextSeg.startFrame;
                QByteArray nextAU = mParser.readAccessUnitData(nextStart);
                int nextFirstFN = readFrameNumFromAU(nextAU, mLog2MaxFrameNum);

                if (lastRefFN >= 0 && nextFirstFN >= 0) {
                    // Output frame_num of last ref frame (with current delta applied)
                    int outputLastRefFN = (lastRefFN + cumulativeFrameNumDelta) % maxFrameNum;
                    // Expected next frame_num for continuity
                    int expectedNext = (outputLastRefFN + 1) % maxFrameNum;
                    // New cumulative delta for next segment
                    cumulativeFrameNumDelta = (expectedNext - nextFirstFN + maxFrameNum) % maxFrameNum;

                    qDebug() << "    frame_num: seg" << i << "lastRef=" << lastRefFN
                             << "output=" << outputLastRefFN
                             << ", seg" << (i+1) << "first=" << nextFirstFN
                             << "-> delta=" << cumulativeFrameNumDelta;
                } else {
                    qDebug() << "    WARNING: Could not read frame_num (lastRef=" << lastRefFN
                             << ", nextFirst=" << nextFirstFN << ") - skipping patching";
                }
            }
        }

        // Progress is emitted granularly from streamCopyFrames/reencodeFrames
    }

    outFile.close();
    mBytesWritten = QFileInfo(outputFile).size();

    emit progressChanged(100, tr("Cut complete"));

    qDebug() << "TTESSmartCut: Complete";
    qDebug() << "  Frames stream-copied:" << mFramesStreamCopied;
    qDebug() << "  Frames re-encoded:" << mFramesReencoded;
    qDebug() << "  Bytes written:" << mBytesWritten;

    return true;
}

// ----------------------------------------------------------------------------
// Analyze cut points
// ----------------------------------------------------------------------------
QList<TTCutSegmentInfo> TTESSmartCut::analyzeCutPoints(
    const QList<QPair<int, int>>& cutFrames)
{
    QList<TTCutSegmentInfo> segments;

    for (const auto& cut : cutFrames) {
        TTCutSegmentInfo seg;
        seg.startFrame = qBound(0, cut.first, frameCount() - 1);
        seg.endFrame = qBound(0, cut.second, frameCount() - 1);

        if (seg.startFrame >= seg.endFrame) {
            continue;  // Skip empty segments
        }

        // Find GOPs
        seg.cutInGOP = mParser.findGopForAU(seg.startFrame);
        seg.cutOutGOP = mParser.findGopForAU(seg.endFrame);

        // Check if cut-in is at keyframe
        int keyframeBefore = mParser.findKeyframeBefore(seg.startFrame);
        bool isAtKeyframe = (keyframeBefore == seg.startFrame);
        bool isAtIDR = isAtKeyframe && mParser.accessUnitAt(seg.startFrame).isIDR;

        // Re-encode when cut-in is NOT at a keyframe (mid-GOP cut), OR when
        // cut-in is at a Non-IDR I-frame. Non-IDR I-frames don't flush the
        // decoder's delayed_pic[] reorder buffer — old frames from the previous
        // segment remain and get interleaved with the new segment's output.
        // Re-encoding produces an IDR which triggers idr() in the decoder,
        // clearing all references and creating a proper output barrier.
        seg.needsReencodeAtStart = !isAtKeyframe || !isAtIDR;

        if (isAtKeyframe && !isAtIDR) {
            qDebug() << "    Cut-in at non-IDR I-frame" << seg.startFrame
                     << "- re-encode needed for IDR boundary";
        }

        // Check if cut-out is at B-frame (optional re-encode)
        TTAccessUnit au = mParser.accessUnitAt(seg.endFrame);
        seg.needsReencodeAtEnd = false;  // For now, don't re-encode at end

        // Calculate frame ranges
        // SMART CUT: Re-encode ONLY from cut-in to next keyframe, then stream-copy
        // For DVB streams with Open GOPs (no IDR), we use I-slices as stream-copy points
        if (seg.needsReencodeAtStart) {
            // First try to find IDR, then fall back to any keyframe (I-slice)
            int nextKeyframe = mParser.findIDRAfter(seg.startFrame);
            bool usingIDR = (nextKeyframe >= 0 && nextKeyframe <= seg.endFrame);

            if (!usingIDR) {
                // No IDR found - try I-slice (Open GOP support)
                nextKeyframe = mParser.findKeyframeAfter(seg.startFrame);
                if (nextKeyframe == seg.startFrame) {
                    // Start is already at keyframe - find next one
                    nextKeyframe = mParser.findKeyframeAfter(seg.startFrame + 1);
                }
            }

            if (nextKeyframe < 0 || nextKeyframe > seg.endFrame) {
                // No keyframe in segment - must re-encode all
                qDebug() << "    No keyframe in segment - re-encoding all";
                seg.reencodeStartFrame = seg.startFrame;
                seg.reencodeEndFrame = seg.endFrame;
                seg.streamCopyStartFrame = -1;
                seg.streamCopyEndFrame = -1;
            } else {
                // Smart Cut: Re-encode from cut-in to just before keyframe
                seg.reencodeStartFrame = seg.startFrame;
                seg.reencodeEndFrame = nextKeyframe - 1;
                seg.streamCopyStartFrame = nextKeyframe;
                seg.streamCopyEndFrame = seg.endFrame;
                qDebug() << "    Smart Cut: Re-encode" << seg.reencodeStartFrame << "->" << seg.reencodeEndFrame
                         << ", Stream-copy from" << (usingIDR ? "IDR" : "I-slice") << nextKeyframe;
            }
        } else {
            // Cut-in is at keyframe - pure stream copy
            TTAccessUnit au = mParser.accessUnitAt(seg.startFrame);
            qDebug() << "    Cut-in at" << (au.isIDR ? "IDR" : "I-slice") << "- pure stream copy";
            seg.reencodeStartFrame = -1;
            seg.reencodeEndFrame = -1;
            seg.streamCopyStartFrame = seg.startFrame;
            seg.streamCopyEndFrame = seg.endFrame;
        }

        segments.append(seg);
    }

    return segments;
}

// ----------------------------------------------------------------------------
// Check if SPS changes at a cut boundary (aspect ratio / resolution change)
// ----------------------------------------------------------------------------
bool TTESSmartCut::hasSPSChangeAtBoundary(int frameIndex, bool isCutOut)
{
    if (!mIsInitialized) return false;
    if (mParser.spsCount() <= 1) return false;  // Only 1 SPS in entire stream, no changes

    // Find the two frames to compare:
    // CutOut: compare cutOut frame vs cutOut+1 (first frame outside segment)
    // CutIn: compare cutIn frame vs cutIn-1 (last frame before segment)
    int frameA = frameIndex;
    int frameB = isCutOut ? frameIndex + 1 : frameIndex - 1;

    if (frameB < 0 || frameB >= mParser.accessUnitCount()) return false;

    // Find which SPS NAL is closest before each frame
    const auto& nalUnits = mParser.nalUnits();
    const auto& auA = mParser.accessUnitAt(frameA);
    const auto& auB = mParser.accessUnitAt(frameB);

    // Search backward from each AU's first NAL for the most recent SPS
    auto findActiveSPS = [&](const TTAccessUnit& au) -> int {
        int firstNal = au.nalIndices.isEmpty() ? 0 : au.nalIndices.first();
        for (int i = firstNal; i >= 0; i--) {
            if (nalUnits[i].isSPS) return i;
        }
        return -1;
    };

    int spsA = findActiveSPS(auA);
    int spsB = findActiveSPS(auB);

    if (spsA < 0 || spsB < 0) return false;
    if (spsA == spsB) return false;  // Same SPS NAL, no change

    // Different SPS NALs — compare raw data
    QByteArray dataA = mParser.readNalData(spsA);
    QByteArray dataB = mParser.readNalData(spsB);

    return dataA != dataB;
}

// ----------------------------------------------------------------------------
// Process a single segment - Smart Cut using pure libav
// Strategy: Both re-encoded and stream-copied sections are self-contained
// Each section starts with its own SPS/PPS + IDR, allowing clean decoder reset
// ----------------------------------------------------------------------------
bool TTESSmartCut::processSegment(QFile& outFile, const TTCutSegmentInfo& segment,
                                   int& frameNumDelta, int* actualStartAU)
{
    if (actualStartAU)
        *actualStartAU = -1;  // -1 = no adjustment

    // If only stream-copy (no re-encoding), write directly
    if (segment.reencodeStartFrame < 0) {
        qDebug() << "    Pure stream-copy segment";
        return streamCopyFrames(outFile, segment.streamCopyStartFrame,
                                segment.streamCopyEndFrame, mReorderDelay, frameNumDelta);
    }

    // If only re-encoding (no stream-copy), write directly
    if (segment.streamCopyStartFrame < 0) {
        qDebug() << "    Pure re-encode segment";
        return reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                              -1, nullptr, actualStartAU);
    }

    // Mixed segment: Re-encode partial GOP + stream-copy from keyframe
    qDebug() << "    Smart Cut: Re-encode" << segment.reencodeStartFrame << "->" << segment.reencodeEndFrame
             << "then stream-copy" << segment.streamCopyStartFrame << "->" << segment.streamCopyEndFrame;

    // PAFF H.264: SPS Unification — rewrite encoder output to match source SPS.
    // This eliminates the MBAFF→PAFF mode switch at the transition, allowing
    // seamless stream-copy without IDR/EOS DPB flush (which causes stutter).
    // Strategy:
    //   1. Write source SPS/PPS (id=0) before re-encode
    //   2. Write encoder PPS with id=1 (extracted from first encoder packet)
    //   3. Rewrite encoder slice NALs to use source SPS params + pps_id=1
    //   4. At transition: no IDR, no EOS — just continue with stream-copy
    //   5. Stream-copy frames use source PPS (id=0) naturally
    // SPS Unification is only needed for PAFF (separated fields).
    // MBAFF (frame-based interlacing) also has frame_mbs_only=0 but does NOT
    // need SPS unification — the encoder produces MBAFF output that is compatible
    // with the source MBAFF stream. Only PAFF has the MBAFF→PAFF mode mismatch.
    // Force SPS Unification path for all PAFF segments (test: skip fallback)
    bool useSpsUnification = (mParser.isPAFF() && mParser.codecType() == NALU_CODEC_H264);

    if (useSpsUnification) {
        qDebug() << "    PAFF SPS Unification: rewriting encoder output for source SPS";
        qDebug() << "      Encoder: log2_fn=" << mEncoderLog2MaxFrameNum
                 << "log2_poc=" << mEncoderLog2MaxPocLsb
                 << "frame_mbs_only=" << mEncoderFrameMbsOnly;
        qDebug() << "      Source:  log2_fn=" << mLog2MaxFrameNum
                 << "log2_poc=" << mLog2MaxPocLsb
                 << "frame_mbs_only=" << mFrameMbsOnly;

        // 1. Write source SPS/PPS (id=0) first — these are the "active" params
        writeParameterSets(outFile, mReorderDelay);

        // 2. Re-encode with SPS unification
        //    Encoder packets are rewritten on-the-fly in reencodeFrames.
        //    The encoder PPS (id=1) is written after extraction from first packet.
        mSpsUnification = true;
        mSpsUnificationOutFile = &outFile;
        mEncoderPacketsWritten = 0;

        int adjustedStart = -1;
        if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                            segment.streamCopyStartFrame, &adjustedStart, actualStartAU)) {
            mSpsUnification = false;
            mSpsUnificationOutFile = nullptr;
            return false;
        }
        mSpsUnification = false;
        mSpsUnificationOutFile = nullptr;

        int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
        int scEnd = segment.streamCopyEndFrame;

        if (scStart > scEnd) {
            qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
            return true;
        }

        // 3. EOS to flush DPB before stream-copy.
        // The re-encode produces MBAFF frames (x264 can't do PAFF). Without
        // EOS, these stay in the DPB and corrupt PAFF B-frame references at
        // stream-copy start (mmco failures, exceeds max, visual artifacts).
        // EOS flushes all MBAFF references so stream-copy starts clean.
        // The overlap extension (in reencodeFrames) ensures the re-encode
        // covers all frames up to the next keyframe, so no Open-GOP B-frames
        // need the flushed MBAFF references.
        {
            static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x0B};
            outFile.write(eosNal, sizeof(eosNal));
            qDebug() << "    PAFF SPS Unification: EOS before stream-copy at" << scStart;
        }

        // Do NOT write SPS/PPS here — the first stream-copy keyframe AU has
        // inline SPS/PPS which patchSpsNalsInAccessUnit will patch.
        // Writing duplicate SPS/PPS causes the h264 parser to combine them
        // with the first AU into one oversized packet → "Invalid NAL unit size".

        // frameNumDelta bridges re-encoded frame_nums (0..N) to stream-copy
        // frame_nums (original values from source ES).
        QByteArray firstAU = mParser.readAccessUnitData(scStart);
        {
            int lastEncFrameNum = mEncoderPacketsWritten;  // encoder fn goes 0..N-1
            int firstScFrameNum = readFrameNumFromAU(firstAU, mLog2MaxFrameNum);
            if (firstScFrameNum > 0) {
                frameNumDelta = lastEncFrameNum - firstScFrameNum;
                qDebug() << "    frameNumDelta:" << frameNumDelta
                         << "(encoder last fn:" << lastEncFrameNum
                         << ", stream-copy first fn:" << firstScFrameNum << ")";
            } else {
                frameNumDelta = 0;
            }
        }

        // MMCO neutralization count: one full GOP (~32 AUs) covers DPB refill
        int mmcoNeutralizeCount = 32;

        if (!streamCopyFrames(outFile, scStart, scEnd,
                              mReorderDelay, frameNumDelta, mmcoNeutralizeCount))
            return false;

    } else if (mParser.isPAFF() && mParser.codecType() == NALU_CODEC_H264) {
        // PAFF fallback: SPS unification not possible (encoder SPS not yet parsed)
        // Use IDR injection as before
        qDebug() << "    PAFF fallback: using IDR injection (encoder SPS not available)";

        int adjustedStart = -1;
        if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                            segment.streamCopyStartFrame, &adjustedStart, actualStartAU)) {
            return false;
        }

        int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
        int scEnd = segment.streamCopyEndFrame;

        if (scStart > scEnd) {
            qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
            return true;
        }

        // EOS to flush MBAFF references from DPB before PAFF stream-copy
        {
            static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x0B};
            outFile.write(eosNal, sizeof(eosNal));
            qDebug() << "    PAFF fallback: EOS before stream-copy at" << scStart;
        }

        H264PpsInfo ppsInfo = { true, false, true, false, false, 0, 0, 0, false };
        if (mParser.ppsCount() > 0)
            ppsInfo = parseH264PpsInfo(mParser.getPPS(0));

        writeParameterSets(outFile, mReorderDelay);

        QByteArray firstAU;
        if (mParser.isMapped()) {
            int64_t auSize;
            const uchar* auPtr = mParser.accessUnitPtr(scStart, auSize);
            if (auPtr) firstAU = QByteArray(reinterpret_cast<const char*>(auPtr), auSize);
        }
        if (firstAU.isEmpty())
            firstAU = mParser.readAccessUnitData(scStart);
        if (firstAU.isEmpty()) {
            setError(QString("Failed to read first stream-copy AU at frame %1").arg(scStart));
            return false;
        }

        int origFrameNum = readFrameNumFromAU(firstAU, mLog2MaxFrameNum);
        QByteArray idrAU = convertAUToIDR(firstAU, mLog2MaxFrameNum, mPocType,
                                           mLog2MaxPocLsb, mFrameMbsOnly, ppsInfo);
        if (mReorderDelay > 0)
            idrAU = patchSpsNalsInAccessUnit(idrAU, mReorderDelay);

        if (outFile.write(idrAU) != idrAU.size()) {
            setError("Failed to write IDR AU");
            return false;
        }
        mFramesStreamCopied++;

        frameNumDelta = (origFrameNum > 0) ? -origFrameNum : 0;
        if (scStart + 1 <= scEnd) {
            if (!streamCopyFrames(outFile, scStart + 1, scEnd,
                                  mReorderDelay, frameNumDelta))
                return false;
        }
    } else {
        // Standard path: re-encode + EOS + stream-copy
        int adjustedStart = -1;
        if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                            segment.streamCopyStartFrame, &adjustedStart, actualStartAU)) {
            return false;
        }

        int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
        int scEnd = segment.streamCopyEndFrame;

        if (scStart > scEnd) {
            qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
            return true;
        }
        // Non-PAFF: use EOS to flush decoder DPB
        if (mParser.codecType() == NALU_CODEC_H264) {
            static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x0B};
            outFile.write(eosNal, sizeof(eosNal));
            qDebug() << "    Inserted H.264 EOS NAL (type 11) - flushing DPB at" << scStart;
        } else if (mParser.codecType() == NALU_CODEC_H265) {
            static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x4A, 0x01};
            outFile.write(eosNal, sizeof(eosNal));
            qDebug() << "    Inserted H.265 EOS NAL (type 37) - flushing DPB at" << scStart;
        }

        // Write source parameter sets
        writeParameterSets(outFile, mReorderDelay);

        // Stream-copy from keyframe
        if (!streamCopyFrames(outFile, scStart, scEnd,
                              mReorderDelay, frameNumDelta)) {
            return false;
        }
    }

    return true;
}

// Forward declaration (defined after writeParameterSets)
static QByteArray patchH264SpsReorderFrames(const QByteArray& spsNal, int maxReorderFrames);

// Patch all H.264 SPS NALs within an access unit's raw data.
// Scans for start codes followed by NAL type 7 (SPS), patches each with
// patchH264SpsReorderFrames(). Returns modified data, or original if no SPS found.
static QByteArray patchSpsNalsInAccessUnit(const QByteArray& auData, int maxReorderFrames)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int size = auData.size();
    QByteArray result;
    result.reserve(size + 64);  // small extra for patched SPS growth

    int pos = 0;
    bool patched = false;

    while (pos < size) {
        // Find next start code
        int scStart = -1;
        int scLen = 0;
        for (int j = pos; j + 2 < size; j++) {
            if (data[j] == 0 && data[j+1] == 0) {
                if (j + 2 < size && data[j+2] == 1) {
                    scStart = j; scLen = 3; break;
                } else if (j + 3 < size && data[j+2] == 0 && data[j+3] == 1) {
                    scStart = j; scLen = 4; break;
                }
            }
        }

        if (scStart < 0) {
            // No more start codes, copy remainder
            result.append(auData.mid(pos));
            break;
        }

        // Copy data before this start code
        if (scStart > pos)
            result.append(auData.mid(pos, scStart - pos));

        // Find end of this NAL (next start code or end of data)
        int nalStart = scStart + scLen;
        int nalEnd = size;
        for (int j = nalStart + 1; j + 2 < size; j++) {
            if (data[j] == 0 && data[j+1] == 0 &&
                (data[j+2] == 1 || (j + 3 < size && data[j+2] == 0 && data[j+3] == 1))) {
                nalEnd = j;
                break;
            }
        }

        // Check NAL type (lower 5 bits of first byte after start code)
        int nalType = (nalStart < size) ? (data[nalStart] & 0x1F) : -1;

        if (nalType == 7) {  // SPS
            // Extract this SPS NAL with its start code, patch it
            QByteArray spsNal = auData.mid(scStart, nalEnd - scStart);
            QByteArray patchedSps = patchH264SpsReorderFrames(spsNal, maxReorderFrames);
            if (!patchedSps.isEmpty()) {
                result.append(patchedSps);
                patched = true;
            } else {
                result.append(spsNal);  // patch failed, keep original
            }
        } else {
            // Not an SPS, copy as-is
            result.append(auData.mid(scStart, nalEnd - scStart));
        }

        pos = nalEnd;
    }

    return patched ? result : auData;
}

// Forward declarations for bitstream helpers (defined after writeParameterSets)
static QByteArray removeEmulationPrevention(const QByteArray& nal);
static QByteArray addEmulationPrevention(const QByteArray& rbsp);
static uint32_t spsReadBits(const uint8_t* data, int dataSize, int& bitPos, int numBits);
static void spsWriteBits(uint8_t* data, int dataSize, int& bitPos, uint32_t value, int numBits);
static uint32_t spsReadUE(const uint8_t* data, int dataSize, int& bitPos);
static int32_t spsReadSE(const uint8_t* data, int dataSize, int& bitPos);
static void spsWriteUE(uint8_t* data, int dataSize, int& bitPos, uint32_t value);
static void spsWriteSE(uint8_t* data, int dataSize, int& bitPos, int32_t value);
static void skipScalingList(const uint8_t* data, int dataSize, int& bitPos, int sizeOfScalingList);

// ----------------------------------------------------------------------------
// Parse H.264 SPS fields needed for frame_num patching and POC domain fix.
// Input: raw SPS NAL data WITH start code prefix.
// Returns struct with parsed values; log2MaxFrameNumMinus4 = -1 on error.
// ----------------------------------------------------------------------------
static H264SpsInfo parseH264SpsInfo(const QByteArray& spsNal)
{
    H264SpsInfo info = { -1, -1, -1, true };

    // Find and strip start code
    int startCodeLen = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(spsNal.constData());
    if (spsNal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
        startCodeLen = 4;
    else if (spsNal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
        startCodeLen = 3;
    else
        return info;

    QByteArray nalBody = spsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 7)
        return info;

    QByteArray rbsp = removeEmulationPrevention(nalBody);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(rbsp.constData());
    int dataSize = rbsp.size();
    int bitPos = 0;

    spsReadBits(data, dataSize, bitPos, 8);  // NAL header
    uint32_t profile_idc = spsReadBits(data, dataSize, bitPos, 8);
    spsReadBits(data, dataSize, bitPos, 8);  // constraint flags
    spsReadBits(data, dataSize, bitPos, 8);  // level_idc
    spsReadUE(data, dataSize, bitPos);       // seq_parameter_set_id

    // High profile extensions
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        uint32_t chroma_format_idc = spsReadUE(data, dataSize, bitPos);
        if (chroma_format_idc == 3)
            spsReadBits(data, dataSize, bitPos, 1);  // separate_colour_plane_flag
        spsReadUE(data, dataSize, bitPos);    // bit_depth_luma_minus8
        spsReadUE(data, dataSize, bitPos);    // bit_depth_chroma_minus8
        spsReadBits(data, dataSize, bitPos, 1); // qpprime_y_zero_transform_bypass_flag
        uint32_t seq_scaling_matrix_present = spsReadBits(data, dataSize, bitPos, 1);
        if (seq_scaling_matrix_present) {
            int limit = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < limit; i++) {
                uint32_t present = spsReadBits(data, dataSize, bitPos, 1);
                if (present)
                    skipScalingList(data, dataSize, bitPos, (i < 6) ? 16 : 64);
            }
        }
    }

    info.log2MaxFrameNumMinus4 = static_cast<int>(spsReadUE(data, dataSize, bitPos));

    info.pocType = static_cast<int>(spsReadUE(data, dataSize, bitPos));
    if (info.pocType == 0) {
        info.log2MaxPocLsbMinus4 = static_cast<int>(spsReadUE(data, dataSize, bitPos));
    } else if (info.pocType == 1) {
        spsReadBits(data, dataSize, bitPos, 1);  // delta_pic_order_always_zero_flag
        spsReadSE(data, dataSize, bitPos);       // offset_for_non_ref_pic
        spsReadSE(data, dataSize, bitPos);       // offset_for_top_to_bottom_field
        uint32_t num_ref = spsReadUE(data, dataSize, bitPos);
        for (uint32_t i = 0; i < num_ref; i++)
            spsReadSE(data, dataSize, bitPos);   // offset_for_ref_frame
    }

    spsReadUE(data, dataSize, bitPos);  // max_num_ref_frames
    spsReadBits(data, dataSize, bitPos, 1);  // gaps_in_frame_num_allowed_flag
    spsReadUE(data, dataSize, bitPos);  // pic_width_in_mbs_minus1
    spsReadUE(data, dataSize, bitPos);  // pic_height_in_map_units_minus1

    info.frameMbsOnly = (spsReadBits(data, dataSize, bitPos, 1) != 0);

    return info;
}

// ----------------------------------------------------------------------------
// Read frame_num from a raw H.264 slice NAL (after start code).
// Returns frame_num value, or -1 on error.
// frameNumBitWidth = log2_max_frame_num_minus4 + 4
// ----------------------------------------------------------------------------
static int readFrameNumFromSlice(const uint8_t* nalData, int nalSize, int frameNumBitWidth)
{
    if (nalSize < 3 || frameNumBitWidth <= 0) return -1;

    uint8_t nalType = nalData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return -1;  // not a slice

    int bitPos = 8;  // skip NAL header byte
    spsReadUE(nalData, nalSize, bitPos);   // first_mb_in_slice
    spsReadUE(nalData, nalSize, bitPos);   // slice_type
    spsReadUE(nalData, nalSize, bitPos);   // pic_parameter_set_id

    // frame_num is u(v) with v = frameNumBitWidth
    return static_cast<int>(spsReadBits(nalData, nalSize, bitPos, frameNumBitWidth));
}

// ----------------------------------------------------------------------------
// Patch frame_num in a raw H.264 slice NAL (after start code).
// Overwrites frame_num in-place (fixed-width field, no size change).
// frameNumBitWidth = log2_max_frame_num_minus4 + 4
// ----------------------------------------------------------------------------
static void writeFrameNumInSlice(uint8_t* nalData, int nalSize, int frameNumBitWidth,
                                  uint32_t newFrameNum)
{
    if (nalSize < 3 || frameNumBitWidth <= 0) return;

    uint8_t nalType = nalData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return;  // not a slice

    int bitPos = 8;  // skip NAL header byte
    spsReadUE(nalData, nalSize, bitPos);   // first_mb_in_slice
    spsReadUE(nalData, nalSize, bitPos);   // slice_type
    spsReadUE(nalData, nalSize, bitPos);   // pic_parameter_set_id

    // Overwrite frame_num at current position
    spsWriteBits(nalData, nalSize, bitPos, newFrameNum, frameNumBitWidth);
}

// ----------------------------------------------------------------------------
// Locate poc_lsb bit position in a raw H.264 slice NAL (RBSP, after EP3 removal).
// Returns bit position of poc_lsb field, or -1 if not applicable.
// Only valid for poc_type == 0 slices.
// ----------------------------------------------------------------------------
static int locatePocLsbInSlice(const uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, bool frameMbsOnly)
{
    if (rbspSize < 3 || frameNumBitWidth <= 0) return -1;

    uint8_t nalType = rbspData[0] & 0x1F;
    if (nalType != 1 && nalType != 5) return -1;

    int bitPos = 8;  // skip NAL header byte
    spsReadUE(rbspData, rbspSize, bitPos);   // first_mb_in_slice
    spsReadUE(rbspData, rbspSize, bitPos);   // slice_type
    spsReadUE(rbspData, rbspSize, bitPos);   // pic_parameter_set_id
    spsReadBits(rbspData, rbspSize, bitPos, frameNumBitWidth);  // frame_num

    if (!frameMbsOnly) {
        uint32_t fieldPicFlag = spsReadBits(rbspData, rbspSize, bitPos, 1);
        if (fieldPicFlag)
            spsReadBits(rbspData, rbspSize, bitPos, 1);  // bottom_field_flag
    }

    if (nalType == 5) {
        spsReadUE(rbspData, rbspSize, bitPos);  // idr_pic_id
    }

    // bitPos now points to pic_order_cnt_lsb
    return bitPos;
}

// ----------------------------------------------------------------------------
// Read poc_lsb from a raw H.264 slice NAL (RBSP).
// Returns poc_lsb value, or -1 on error.
// ----------------------------------------------------------------------------
static int readPocLsbFromSlice(const uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, int pocLsbBitWidth,
                                bool frameMbsOnly)
{
    if (pocLsbBitWidth <= 0) return -1;
    int bitPos = locatePocLsbInSlice(rbspData, rbspSize, frameNumBitWidth, frameMbsOnly);
    if (bitPos < 0) return -1;
    return static_cast<int>(spsReadBits(rbspData, rbspSize, bitPos, pocLsbBitWidth));
}

// ----------------------------------------------------------------------------
// Write poc_lsb in a raw H.264 slice NAL (RBSP, in-place).
// Fixed-width field — no bit shifting, CABAC data stays intact.
// ----------------------------------------------------------------------------
static void writePocLsbInSlice(uint8_t* rbspData, int rbspSize,
                                int frameNumBitWidth, int pocLsbBitWidth,
                                bool frameMbsOnly, uint32_t newPocLsb)
{
    if (pocLsbBitWidth <= 0) return;
    int bitPos = locatePocLsbInSlice(rbspData, rbspSize, frameNumBitWidth, frameMbsOnly);
    if (bitPos < 0) return;
    spsWriteBits(rbspData, rbspSize, bitPos, newPocLsb, pocLsbBitWidth);
}

// ----------------------------------------------------------------------------
// Read poc_lsb from the first slice NAL of an access unit.
// Handles start codes and emulation prevention.
// Returns poc_lsb value, or -1 if not applicable.
// ----------------------------------------------------------------------------
static int readPocLsbFromAU(const QByteArray& auData, int frameNumBitWidth,
                             int pocLsbBitWidth, bool frameMbsOnly)
{
    if (pocLsbBitWidth <= 0 || frameNumBitWidth <= 0 || auData.isEmpty())
        return -1;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());

    // Find first slice NAL (type 1 or 5)
    int pos = 0;
    while (pos < auData.size()) {
        int scStart = -1;
        for (int i = pos; i + 3 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < auData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; break;
                }
            }
        }
        if (scStart < 0) break;

        int scLen = (data[scStart+2] == 0) ? 4 : 3;
        int nalStart = scStart + scLen;
        if (nalStart >= auData.size()) break;

        uint8_t nalType = data[nalStart] & 0x1F;
        if (nalType == 1 || nalType == 5) {
            int nalEnd = auData.size();
            for (int i = nalStart + 1; i + 2 < auData.size(); i++) {
                if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                    nalEnd = i; break;
                }
            }
            QByteArray nalBody = auData.mid(nalStart, nalEnd - nalStart);
            QByteArray rbsp = removeEmulationPrevention(nalBody);
            return readPocLsbFromSlice(
                reinterpret_cast<const uint8_t*>(rbsp.constData()),
                rbsp.size(), frameNumBitWidth, pocLsbBitWidth, frameMbsOnly);
        }

        pos = nalStart + 1;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Patch poc_lsb in the last slice NAL of a packet (raw encoder output).
// The last slice's poc_lsb becomes prevPicOrderCntLsb for the next picture.
// Handles start codes and emulation prevention correctly.
// Returns patched data, or original if no slice found or patch not needed.
// ----------------------------------------------------------------------------
static QByteArray patchPocLsbInPacket(const QByteArray& packetData,
                                       int frameNumBitWidth, int pocLsbBitWidth,
                                       bool frameMbsOnly, uint32_t newPocLsb)
{
    if (pocLsbBitWidth <= 0 || frameNumBitWidth <= 0 || packetData.isEmpty())
        return packetData;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());

    // Find the LAST slice NAL in the packet (skip SPS/PPS/SEI)
    int lastSliceScStart = -1;
    int lastSliceScLen = 0;
    int lastSliceNalStart = -1;
    int pos = 0;

    while (pos < packetData.size()) {
        int scStart = -1;
        int scLen = 0;
        for (int i = pos; i + 2 < packetData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < packetData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; scLen = 4; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; scLen = 3; break;
                }
            }
        }
        if (scStart < 0) break;

        int nalStart = scStart + scLen;
        if (nalStart >= packetData.size()) break;

        uint8_t nalType = data[nalStart] & 0x1F;
        if (nalType == 1 || nalType == 5) {
            lastSliceScStart = scStart;
            lastSliceScLen = scLen;
            lastSliceNalStart = nalStart;
        }
        pos = nalStart + 1;
    }

    if (lastSliceNalStart < 0)
        return packetData;  // no slice NAL found

    // Find end of this slice NAL
    int nalEnd = packetData.size();
    for (int i = lastSliceNalStart + 1; i + 2 < packetData.size(); i++) {
        if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
            nalEnd = i; break;
        }
    }

    // Extract NAL body, remove EP3, patch poc_lsb, re-add EP3
    QByteArray nalBody = packetData.mid(lastSliceNalStart, nalEnd - lastSliceNalStart);
    QByteArray rbsp = removeEmulationPrevention(nalBody);

    // Verify we can read poc_lsb before patching
    int oldPocLsb = readPocLsbFromSlice(
        reinterpret_cast<const uint8_t*>(rbsp.constData()), rbsp.size(),
        frameNumBitWidth, pocLsbBitWidth, frameMbsOnly);
    if (oldPocLsb < 0)
        return packetData;

    // Patch poc_lsb in RBSP
    writePocLsbInSlice(reinterpret_cast<uint8_t*>(rbsp.data()), rbsp.size(),
                        frameNumBitWidth, pocLsbBitWidth, frameMbsOnly, newPocLsb);

    // Re-add emulation prevention
    QByteArray patchedNalBody = addEmulationPrevention(rbsp);

    // Rebuild packet: data before slice NAL + start code + patched NAL + data after
    QByteArray result;
    result.reserve(packetData.size() + 8);
    result.append(packetData.constData(), lastSliceScStart);
    result.append(packetData.constData() + lastSliceScStart, lastSliceScLen);
    result.append(patchedNalBody);
    if (nalEnd < packetData.size())
        result.append(packetData.mid(nalEnd));

    qDebug() << "      POC fix: patched encoder slice poc_lsb" << oldPocLsb
             << "->" << newPocLsb;

    return result;
}

// ----------------------------------------------------------------------------
// Find and parse the first SPS NAL (type 7) in an Annex B H.264 packet.
// Used to extract encoder SPS parameters from inline SPS/PPS in first output.
// Returns true if SPS was found and parsed successfully.
// ----------------------------------------------------------------------------
static bool findH264SpsInPacket(const QByteArray& packetData, H264SpsInfo& spsInfo)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(packetData.constData());
    int sz = packetData.size();

    for (int pos = 0; pos < sz - 4; ) {
        int scLen = 0;
        if (d[pos] == 0 && d[pos+1] == 0 && d[pos+2] == 0 && d[pos+3] == 1)
            scLen = 4;
        else if (d[pos] == 0 && d[pos+1] == 0 && d[pos+2] == 1)
            scLen = 3;
        if (scLen == 0) { pos++; continue; }

        int nalStart = pos;
        int nalType = d[pos + scLen] & 0x1F;

        int nalEnd = sz;
        for (int j = pos + scLen + 1; j < sz - 2; j++) {
            if (d[j] == 0 && d[j+1] == 0 &&
                (d[j+2] == 1 || (j + 3 < sz && d[j+2] == 0 && d[j+3] == 1))) {
                nalEnd = j;
                break;
            }
        }

        if (nalType == 7) {
            QByteArray spsNal = packetData.mid(nalStart, nalEnd - nalStart);
            spsInfo = parseH264SpsInfo(spsNal);
            return (spsInfo.log2MaxFrameNumMinus4 >= 0);
        }
        pos = nalEnd;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Patch frame_num in all slice NALs of an access unit.
// Handles emulation prevention bytes correctly.
// Returns patched AU data, or original on error.
// frameNumDelta is added to each frame_num (modulo maxFrameNum).
// ----------------------------------------------------------------------------
static QByteArray patchFrameNumInAU(const QByteArray& auData, int frameNumBitWidth,
                                     int frameNumDelta, int maxFrameNum)
{
    if (frameNumDelta == 0 || frameNumBitWidth <= 0)
        return auData;

    QByteArray result;
    result.reserve(auData.size() + 64);
    bool patched = false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int pos = 0;

    while (pos < auData.size()) {
        // Find next start code
        int scStart = -1;
        for (int i = pos; i + 3 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < auData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i;
                    break;
                }
                if (data[i+2] == 1) {
                    scStart = i;
                    break;
                }
            }
        }
        if (scStart < 0) {
            result.append(auData.mid(pos));
            break;
        }

        // Copy data before start code
        if (scStart > pos)
            result.append(auData.mid(pos, scStart - pos));

        // Determine start code length
        int scLen = (data[scStart+2] == 0) ? 4 : 3;

        // Find end of this NAL (next start code or end of data)
        int nalEnd = auData.size();
        for (int i = scStart + scLen + 1; i + 2 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                nalEnd = i;
                break;
            }
        }

        // Get NAL body (after start code)
        QByteArray nalBody = auData.mid(scStart + scLen, nalEnd - scStart - scLen);
        if (!nalBody.isEmpty()) {
            uint8_t nalType = (uint8_t)nalBody[0] & 0x1F;

            if (nalType == 1 || nalType == 5) {
                // Slice NAL — remove emulation prevention, patch, re-add
                QByteArray rbsp = removeEmulationPrevention(nalBody);
                int frameNum = readFrameNumFromSlice(
                    reinterpret_cast<const uint8_t*>(rbsp.constData()),
                    rbsp.size(), frameNumBitWidth);

                if (frameNum >= 0) {
                    int newFrameNum = (frameNum + frameNumDelta) % maxFrameNum;
                    if (newFrameNum < 0) newFrameNum += maxFrameNum;

                    writeFrameNumInSlice(
                        reinterpret_cast<uint8_t*>(rbsp.data()),
                        rbsp.size(), frameNumBitWidth, newFrameNum);

                    QByteArray patchedNal = addEmulationPrevention(rbsp);
                    result.append(auData.mid(scStart, scLen));  // start code
                    result.append(patchedNal);
                    patched = true;
                    pos = nalEnd;
                    continue;
                }
            }
        }

        // Not a slice or patch failed — copy as-is
        result.append(auData.mid(scStart, nalEnd - scStart));
        pos = nalEnd;
    }

    return patched ? result : auData;
}

// ----------------------------------------------------------------------------
// Read frame_num from the first slice NAL of an access unit.
// Returns -1 if not H.264 or on error.
// ----------------------------------------------------------------------------
static int readFrameNumFromAU(const QByteArray& auData, int frameNumBitWidth)
{
    if (frameNumBitWidth <= 0 || auData.isEmpty())
        return -1;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());

    // Find first slice NAL (type 1 or 5) in the AU
    int pos = 0;
    while (pos < auData.size()) {
        int scStart = -1;
        for (int i = pos; i + 3 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < auData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; break;
                }
            }
        }
        if (scStart < 0) break;

        int scLen = (data[scStart+2] == 0) ? 4 : 3;
        int nalStart = scStart + scLen;
        if (nalStart >= auData.size()) break;

        uint8_t nalType = data[nalStart] & 0x1F;
        if (nalType == 1 || nalType == 5) {
            // Found a slice — remove emulation prevention, read frame_num
            int nalEnd = auData.size();
            for (int i = nalStart + 1; i + 2 < auData.size(); i++) {
                if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                    nalEnd = i; break;
                }
            }
            QByteArray nalBody = auData.mid(nalStart, nalEnd - nalStart);
            QByteArray rbsp = removeEmulationPrevention(nalBody);
            return readFrameNumFromSlice(
                reinterpret_cast<const uint8_t*>(rbsp.constData()),
                rbsp.size(), frameNumBitWidth);
        }

        // Skip to next NAL
        pos = nalStart + 1;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// Neutralize MMCO commands in all reference slices of an AU.
// After EOS flush, the DPB is empty. Non-IDR slices with adaptive MMCO
// commands try to unref frames that no longer exist → "mmco: unref short
// failure" + DPB overflow. Fix: set adaptive_ref_pic_marking_mode_flag
// from 1→0, removing all MMCO data. The RBSP is rebuilt with the MMCO
// bits removed and CABAC data byte-aligned.
// Handles all slice types: I, P, B (with ref_pic_list_modification,
// pred_weight_table, etc.).
// ----------------------------------------------------------------------------
static QByteArray neutralizeMmcoInAU(const QByteArray& auData,
    int log2MaxFrameNum, int pocLsbBitWidth, bool frameMbsOnly,
    const H264PpsInfo& pps)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int auSize = auData.size();
    QByteArray result = auData;

    // Iterate over all NALs in the AU
    int pos = 0;
    while (pos < result.size()) {
        data = reinterpret_cast<const uint8_t*>(result.constData());
        auSize = result.size();

        int scStart = -1;
        for (int i = pos; i + 2 < auSize; i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < auSize && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; break;
                }
            }
        }
        if (scStart < 0) break;

        int scLen = (data[scStart+2] == 0) ? 4 : 3;
        int nalStart = scStart + scLen;
        if (nalStart >= auSize) break;

        uint8_t nalByte = data[nalStart];
        uint8_t nalType = nalByte & 0x1F;
        uint8_t nalRefIdc = (nalByte >> 5) & 0x03;

        if (nalType != 1 || nalRefIdc == 0) {
            pos = nalStart + 1;
            continue;
        }

        // Find end of this NAL
        int nalEnd = auSize;
        for (int i = nalStart + 1; i + 2 < auSize; i++) {
            if (data[i] == 0 && data[i+1] == 0 &&
                (data[i+2] == 0 || data[i+2] == 1)) {
                nalEnd = i; break;
            }
        }

        QByteArray nalBody = result.mid(nalStart, nalEnd - nalStart);
        QByteArray rbsp = removeEmulationPrevention(nalBody);
        const uint8_t* r = reinterpret_cast<const uint8_t*>(rbsp.constData());
        int rSize = rbsp.size();

        // Parse slice header
        int bitPos = 8;  // skip NAL header byte
        spsReadUE(r, rSize, bitPos);                       // first_mb_in_slice
        uint32_t sliceType = spsReadUE(r, rSize, bitPos);  // slice_type
        int sliceTypeM5 = sliceType % 5;  // 0=P, 1=B, 2=I, 3=SP, 4=SI

        spsReadUE(r, rSize, bitPos);                              // pic_parameter_set_id
        spsReadBits(r, rSize, bitPos, log2MaxFrameNum);           // frame_num

        bool isFieldSlice = false;
        if (!frameMbsOnly) {
            uint32_t fieldPicFlag = spsReadBits(r, rSize, bitPos, 1);
            isFieldSlice = (fieldPicFlag != 0);
            if (isFieldSlice)
                spsReadBits(r, rSize, bitPos, 1);                 // bottom_field_flag
        }

        // pic_order_cnt_lsb (poc_type == 0)
        spsReadBits(r, rSize, bitPos, pocLsbBitWidth);

        // delta_pic_order_cnt_bottom: only when PPS flag set AND frame slice
        if (pps.bottomFieldPicOrderPresent && !isFieldSlice)
            spsReadSE(r, rSize, bitPos);

        // --- P/B specific fields ---
        if (sliceTypeM5 == 1)  // B-slice
            spsReadBits(r, rSize, bitPos, 1);  // direct_spatial_mv_pred_flag

        int numRefL0 = pps.numRefIdxL0DefaultActiveMinus1;
        int numRefL1 = pps.numRefIdxL1DefaultActiveMinus1;

        if (sliceTypeM5 == 0 || sliceTypeM5 == 1 || sliceTypeM5 == 3) {
            // P, B, or SP: num_ref_idx_active_override_flag
            uint32_t overrideFlag = spsReadBits(r, rSize, bitPos, 1);
            if (overrideFlag) {
                numRefL0 = spsReadUE(r, rSize, bitPos);  // num_ref_idx_l0_active_minus1
                if (sliceTypeM5 == 1)
                    numRefL1 = spsReadUE(r, rSize, bitPos);  // num_ref_idx_l1_active_minus1
            }
        }

        // ref_pic_list_modification (P, SP, B only)
        if (sliceTypeM5 != 2 && sliceTypeM5 != 4) {
            // L0
            uint32_t rplmFlag = spsReadBits(r, rSize, bitPos, 1);
            if (rplmFlag) {
                while (true) {
                    uint32_t idc = spsReadUE(r, rSize, bitPos);
                    if (idc == 3) break;
                    spsReadUE(r, rSize, bitPos);  // abs_diff_pic_num_minus1 or long_term_pic_num
                }
            }
            // L1 (B-slices only)
            if (sliceTypeM5 == 1) {
                rplmFlag = spsReadBits(r, rSize, bitPos, 1);
                if (rplmFlag) {
                    while (true) {
                        uint32_t idc = spsReadUE(r, rSize, bitPos);
                        if (idc == 3) break;
                        spsReadUE(r, rSize, bitPos);
                    }
                }
            }
        }

        // pred_weight_table (P with weighted_pred, B with weighted_bipred_idc==1)
        bool hasWeightTable = false;
        if ((sliceTypeM5 == 0 || sliceTypeM5 == 3) && pps.weightedPredFlag)
            hasWeightTable = true;
        if (sliceTypeM5 == 1 && pps.weightedBipredIdc == 1)
            hasWeightTable = true;

        if (hasWeightTable) {
            spsReadUE(r, rSize, bitPos);  // luma_log2_weight_denom
            spsReadUE(r, rSize, bitPos);  // chroma_log2_weight_denom
            for (int i = 0; i <= numRefL0; i++) {
                uint32_t lumaFlag = spsReadBits(r, rSize, bitPos, 1);
                if (lumaFlag) {
                    spsReadSE(r, rSize, bitPos);  // luma_weight
                    spsReadSE(r, rSize, bitPos);  // luma_offset
                }
                uint32_t chromaFlag = spsReadBits(r, rSize, bitPos, 1);
                if (chromaFlag) {
                    spsReadSE(r, rSize, bitPos);  // cb_weight
                    spsReadSE(r, rSize, bitPos);  // cb_offset
                    spsReadSE(r, rSize, bitPos);  // cr_weight
                    spsReadSE(r, rSize, bitPos);  // cr_offset
                }
            }
            if (sliceTypeM5 == 1) {
                for (int i = 0; i <= numRefL1; i++) {
                    uint32_t lumaFlag = spsReadBits(r, rSize, bitPos, 1);
                    if (lumaFlag) {
                        spsReadSE(r, rSize, bitPos);
                        spsReadSE(r, rSize, bitPos);
                    }
                    uint32_t chromaFlag = spsReadBits(r, rSize, bitPos, 1);
                    if (chromaFlag) {
                        spsReadSE(r, rSize, bitPos);
                        spsReadSE(r, rSize, bitPos);
                        spsReadSE(r, rSize, bitPos);
                        spsReadSE(r, rSize, bitPos);
                    }
                }
            }
        }

        // dec_ref_pic_marking
        int flagBitPos = bitPos;
        uint32_t adaptiveFlag = spsReadBits(r, rSize, bitPos, 1);

        if (adaptiveFlag == 0) {
            pos = nalEnd;
            continue;
        }

        // Skip all MMCO commands
        int mmcoBitsStart = bitPos;
        while (true) {
            uint32_t mmcoOp = spsReadUE(r, rSize, bitPos);
            if (mmcoOp == 0) break;
            if (mmcoOp == 1 || mmcoOp == 3)
                spsReadUE(r, rSize, bitPos);  // difference_of_pic_nums_minus1
            if (mmcoOp == 2)
                spsReadUE(r, rSize, bitPos);  // long_term_pic_num
            if (mmcoOp == 3 || mmcoOp == 6)
                spsReadUE(r, rSize, bitPos);  // long_term_frame_idx
            if (mmcoOp == 4)
                spsReadUE(r, rSize, bitPos);  // max_long_term_frame_idx_plus1
        }
        int afterMmcoBitPos = bitPos;
        int mmcoBitsRemoved = afterMmcoBitPos - mmcoBitsStart;

        // Parse remaining header after MMCO
        // cabac_init_idc: present for non-I/SI when CABAC
        if (pps.entropyCodingModeFlag && sliceTypeM5 != 2 && sliceTypeM5 != 4)
            spsReadUE(r, rSize, bitPos);  // cabac_init_idc

        spsReadSE(r, rSize, bitPos);  // slice_qp_delta

        if (sliceTypeM5 == 3 || sliceTypeM5 == 4) {
            if (sliceTypeM5 == 3)
                spsReadBits(r, rSize, bitPos, 1);  // sp_for_switch_flag (u(1))
            spsReadSE(r, rSize, bitPos);  // slice_qs_delta
        }

        if (pps.deblockingFilterControlPresent) {
            uint32_t disableDeblocking = spsReadUE(r, rSize, bitPos);
            if (disableDeblocking != 1) {
                spsReadSE(r, rSize, bitPos);  // slice_alpha_c0_offset_div2
                spsReadSE(r, rSize, bitPos);  // slice_beta_offset_div2
            }
        }
        int headerEndBitPos = bitPos;

        // Rebuild RBSP without MMCO data
        int origCabacByte = (headerEndBitPos + 7) / 8;
        int newHeaderBits = flagBitPos + 1 + (headerEndBitPos - afterMmcoBitPos);
        int newCabacByte = (newHeaderBits + 7) / 8;
        int alignBitsNeeded = newCabacByte * 8 - newHeaderBits;

        int cabacDataSize = rSize - origCabacByte;
        if (cabacDataSize < 0) {
            pos = nalEnd;
            continue;
        }

        QByteArray newRbsp(newCabacByte + cabacDataSize, '\0');
        uint8_t* out = reinterpret_cast<uint8_t*>(newRbsp.data());
        int outBit = 0;

        // Copy header bits before flag
        for (int i = 0; i < flagBitPos; i++) {
            int srcByte = i / 8, srcBitIdx = 7 - (i % 8);
            if ((r[srcByte] >> srcBitIdx) & 1) {
                int dstByte = outBit / 8, dstBitIdx = 7 - (outBit % 8);
                out[dstByte] |= (1 << dstBitIdx);
            }
            outBit++;
        }

        // Write flag = 0 (buffer is zero-initialized)
        outBit++;

        // Copy post-MMCO header bits
        for (int i = afterMmcoBitPos; i < headerEndBitPos; i++) {
            int srcByte = i / 8, srcBitIdx = 7 - (i % 8);
            if ((r[srcByte] >> srcBitIdx) & 1) {
                int dstByte = outBit / 8, dstBitIdx = 7 - (outBit % 8);
                out[dstByte] |= (1 << dstBitIdx);
            }
            outBit++;
        }

        // CABAC alignment bits (1 per spec)
        for (int i = 0; i < alignBitsNeeded; i++) {
            int dstByte = outBit / 8, dstBitIdx = 7 - (outBit % 8);
            out[dstByte] |= (1 << dstBitIdx);
            outBit++;
        }

        // Copy CABAC data bytes verbatim
        if (cabacDataSize > 0)
            memcpy(out + newCabacByte, r + origCabacByte, cabacDataSize);

        QByteArray newNalBody = addEmulationPrevention(newRbsp);

        // Reassemble AU with replaced NAL
        QByteArray newResult;
        newResult.reserve(auSize);
        newResult.append(result.left(scStart));
        newResult.append(result.mid(scStart, scLen));
        newResult.append(newNalBody);
        newResult.append(result.mid(nalEnd));

        qDebug() << "    MMCO neutralized: slice_type=" << sliceType
                 << mmcoBitsRemoved << "bits removed,"
                 << "NAL" << nalBody.size() << "->" << newNalBody.size() << "bytes";

        result = newResult;
        // Continue scanning from after the replaced NAL
        pos = scStart + scLen + newNalBody.size();
    }

    return result;
}

// ----------------------------------------------------------------------------
// Stream-copy frames (no re-encoding)
// If patchReorderFrames > 0, patches H.264 SPS NALs inline for correct
// decoder reorder buffer signaling.
// ----------------------------------------------------------------------------
bool TTESSmartCut::streamCopyFrames(QFile& outFile, int startFrame, int endFrame,
                                     int patchReorderFrames, int frameNumDelta,
                                     int neutralizeMmcoFrames)
{
    qDebug() << "    Stream-copying frames" << startFrame << "->" << endFrame;
    if (frameNumDelta != 0) {
        qDebug() << "    frame_num delta:" << frameNumDelta
                 << "(MaxFrameNum=" << (1 << mLog2MaxFrameNum) << ")";
    }
    if (neutralizeMmcoFrames > 0) {
        qDebug() << "    MMCO neutralization for first" << neutralizeMmcoFrames << "frames";
    }

    bool needsPatching = (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264)
                      || (frameNumDelta != 0 && mLog2MaxFrameNum > 0 && mParser.codecType() == NALU_CODEC_H264)
                      || (neutralizeMmcoFrames > 0);
    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

    // --- Bulk-write path: no patching needed, mmap available ---
    if (!needsPatching && mParser.isMapped()) {
        int64_t startSize, endSize;
        const uchar* startPtr = mParser.accessUnitPtr(startFrame, startSize);
        const uchar* endPtr = mParser.accessUnitPtr(endFrame, endSize);

        if (startPtr && endPtr) {
            int64_t totalSize = (endPtr + endSize) - startPtr;
            qDebug() << "    Bulk-write:" << (endFrame - startFrame + 1) << "frames,"
                     << (totalSize / (1024*1024)) << "MB";

            if (outFile.write(reinterpret_cast<const char*>(startPtr), totalSize) != totalSize) {
                setError(QString("Bulk write failed for frames %1-%2").arg(startFrame).arg(endFrame));
                return false;
            }

            mFramesStreamCopied += (endFrame - startFrame + 1);
            return true;
        }
        // Fall through to per-frame path if accessUnitPtr failed
    }

    // --- Per-frame path: patching required or mmap unavailable ---
    for (int i = startFrame; i <= endFrame; ++i) {
        QByteArray auData;

        // Prefer mmap over QFile seek+read
        if (mParser.isMapped()) {
            int64_t auSize;
            const uchar* auPtr = mParser.accessUnitPtr(i, auSize);
            if (auPtr) {
                auData = QByteArray(reinterpret_cast<const char*>(auPtr), auSize);
            }
        }

        // Fallback to QFile read
        if (auData.isEmpty()) {
            auData = mParser.readAccessUnitData(i);
        }

        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1").arg(i));
            return false;
        }

        // Neutralize MMCO in first N frames after EOS (PAFF DPB refill)
        if (neutralizeMmcoFrames > 0 && (i - startFrame) < neutralizeMmcoFrames &&
            mParser.codecType() == NALU_CODEC_H264) {
            H264PpsInfo ppsInfo = { true, false, true, false, false, 0, 0, 0, false };
            if (mParser.ppsCount() > 0)
                ppsInfo = parseH264PpsInfo(mParser.getPPS(0));
            auData = neutralizeMmcoInAU(auData, mLog2MaxFrameNum,
                mLog2MaxPocLsb, mFrameMbsOnly, ppsInfo);
        }

        // Patch H.264 SPS NALs inline if requested
        if (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264) {
            auData = patchSpsNalsInAccessUnit(auData, patchReorderFrames);
        }

        // Patch H.264 frame_num for inter-segment continuity
        if (frameNumDelta != 0 && mLog2MaxFrameNum > 0 &&
            mParser.codecType() == NALU_CODEC_H264) {
            auData = patchFrameNumInAU(auData, mLog2MaxFrameNum, frameNumDelta, maxFrameNum);
        }

        // Write to output
        if (outFile.write(auData) != auData.size()) {
            setError(QString("Failed to write frame %1").arg(i));
            return false;
        }

        mFramesStreamCopied++;

        // Granular progress update (every 50 frames to avoid signal overhead)
        if (mTotalFrames > 0 && (mFramesStreamCopied % 50 == 0 || i == endFrame)) {
            int percent = ((mFramesStreamCopied + mFramesReencoded) * 100) / mTotalFrames;
            emit progressChanged(qMin(percent, 99),
                tr("Processing segment %1/%2").arg(mCurrentSegment).arg(mTotalSegments));
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Re-encode frames (for partial GOPs)
// ----------------------------------------------------------------------------
bool TTESSmartCut::reencodeFrames(QFile& outFile, int startFrame, int endFrame,
                                  int streamCopyStartFrame, int* adjustedStreamCopyStart,
                                  int* actualStartAU)
{
    qDebug() << "    Re-encoding frames" << startFrame << "->" << endFrame;

    if (adjustedStreamCopyStart)
        *adjustedStreamCopyStart = -1;  // -1 = no adjustment needed
    if (actualStartAU)
        *actualStartAU = -1;  // -1 = no adjustment (realStartAU == startFrame)

    // Find the keyframe we need to decode from.
    // H.264/H.265 decoders with frame-threading have an initialization delay:
    // the first D display-order frames are consumed by the pipeline and never
    // appear in the output. If startFrame is close to the nearest keyframe
    // (within the delay window), the target frames may be "eaten" by this delay.
    // Solution: go back one additional keyframe if the runway is too short.
    // This adds ~1 GOP of extra decoding but ensures all target frames appear.
    int decodeStart = mParser.findKeyframeBefore(startFrame);
    if (decodeStart < 0) decodeStart = 0;

    const int DECODER_RUNWAY = 10;  // safety margin for frame-threading delay
    if (startFrame - decodeStart < DECODER_RUNWAY && decodeStart > 0) {
        int prevKeyframe = mParser.findKeyframeBefore(decodeStart - 1);
        if (prevKeyframe >= 0) {
            qDebug() << "      Runway too short (" << (startFrame - decodeStart)
                     << "frames), going back from keyframe" << decodeStart
                     << "to previous keyframe" << prevKeyframe;
            decodeStart = prevKeyframe;
        }
    }

    qDebug() << "      Decoding from keyframe at frame" << decodeStart;

    // Setup decoder if needed
    if (!mDecoder) {
        if (!setupDecoder()) {
            return false;
        }
    } else {
        // Reset decoder state for new segment
        // After a previous segment's flush, decoder is in EOF state
        // avcodec_flush_buffers resets it to accept new input
        avcodec_flush_buffers(mDecoder);
        qDebug() << "      Decoder reset for new segment";
    }

    // For multi-segment handling: libx264's lookahead thread can't be restarted
    // after flush, so we need to recreate the encoder for each segment
    if (mEncoder) {
        qDebug() << "      Recreating encoder for new segment";
        freeEncoder();
        // encoderInitialized will be false, triggering setupEncoder() below
    }

    // Decode ALL frames from keyframe to endFrame using correct FFmpeg API pattern.
    // The decoder outputs frames in DISPLAY ORDER (reordered by PTS),
    // but we feed AUs in DECODE ORDER (file order). With B-frames these differ.
    // We collect ALL decoded frames first, then select by display position.
    //
    // Important: avcodec_receive_frame() can return multiple frames per send_packet,
    // and decodeFrame() only retrieves one. So we call avcodec_receive_frame() in a
    // loop after each send_packet to drain all available output.
    QList<AVFrame*> allDecodedFrames;
    bool encoderInitialized = (mEncoder != nullptr);

    // Helper lambda: drain all available frames from decoder
    auto drainDecoder = [&]() {
        while (true) {
            AVFrame* frame = av_frame_alloc();
            if (!frame) break;
            int ret = avcodec_receive_frame(mDecoder, frame);
            if (ret < 0) {
                av_frame_free(&frame);
                break;  // EAGAIN (need more input) or EOF
            }

            // Initialize encoder after first successful decode
            if (!encoderInitialized) {
                qDebug() << "      First decoded frame: " << frame->width << "x" << frame->height
                         << " pix_fmt=" << frame->format;

                mDecodedWidth = frame->width;
                mDecodedHeight = frame->height;
                mDecodedPixFmt = static_cast<AVPixelFormat>(frame->format);

                // Detect interlaced content from first decoded frame
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 30, 0)
                // FFmpeg 6.1+: interlace info via frame flags
                mInterlaced = (frame->flags & AV_FRAME_FLAG_INTERLACED);
                mTopFieldFirst = (frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
#else
                mInterlaced = (frame->interlaced_frame != 0);
                mTopFieldFirst = (frame->top_field_first != 0);
#endif
                if (mInterlaced) {
                    qDebug() << "      Interlaced source detected:"
                             << (mTopFieldFirst ? "TFF" : "BFF");
                }

                if (!setupEncoder()) {
                    av_frame_free(&frame);
                    for (AVFrame* f : allDecodedFrames) av_frame_free(&f);
                    allDecodedFrames.clear();
                    return false;
                }
                encoderInitialized = true;

                // Encoder SPS is parsed later, after first avcodec_receive_packet()
                // (x264 doesn't populate extradata until first packet is produced)
            }

            allDecodedFrames.append(frame);

            // Track B-frame reorder delay — the decoder updates has_b_frames
            // dynamically as it encounters B-frames. Source SPS may lack
            // bitstream_restriction, so has_b_frames starts at 0 and increases
            // only after actual B-frames are decoded.
            if (mReorderDelay == 0 && mDecoder->has_b_frames > 0) {
                mReorderDelay = mDecoder->has_b_frames;
                qDebug() << "      Decoder has_b_frames:" << mReorderDelay
                         << "(detected after" << allDecodedFrames.size() << "frames)";
            }
        }
        return true;
    };

    // Extend decode range beyond endFrame to include forward reference frames.
    // B-frames near endFrame need a P-frame beyond endFrame as reference.
    // Decode up to the next keyframe (exclusive) so the decoder has all references.
    // Extend decode range well beyond endFrame. The HEVC decoder with frame-threading
    // has an internal delay of ~7 frames that persists even through flush. By feeding
    // extra AUs beyond endFrame, our target frames (startFrame..endFrame) are safely
    // within the output range instead of stuck in the decoder's trailing buffer.
    // We also need the next keyframe as forward reference for B-frames near endFrame.
    int decodeEnd = qMin(endFrame + 20, frameCount() - 1);

    // Pre-extend decode range to cover the next keyframe after streamCopyStartFrame.
    // When B-frame reorder delay shifts the display-order CutIn past the stream-copy
    // boundary, we need to re-encode up to the next keyframe. Decoding these frames
    // upfront avoids having to reset the decoder from EOF state later.
    if (streamCopyStartFrame >= 0) {
        int potentialNextKF = mParser.findKeyframeAfter(streamCopyStartFrame + 1);
        if (potentialNextKF > 0) {
            int potentialExtEnd = qMin(potentialNextKF + 20, frameCount() - 1);
            if (potentialExtEnd > decodeEnd) {
                qDebug() << "      Pre-extending decode range to cover potential next keyframe"
                         << potentialNextKF << "(decode end:" << potentialExtEnd << ")";
                decodeEnd = potentialExtEnd;
            }
        }
    }

    qDebug() << "      Decode range:" << decodeStart << "->" << decodeEnd
             << "(endFrame=" << endFrame << ", extra=" << (decodeEnd - endFrame) << ")";

    // Feed all AUs from keyframe through extended range
    for (int i = decodeStart; i <= decodeEnd; ++i) {
        QByteArray auData = mParser.readAccessUnitData(i);
        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1 for decoding").arg(i));
            for (AVFrame* f : allDecodedFrames) av_frame_free(&f);
            return false;
        }

        // Send packet to decoder, retry on EAGAIN
        AVPacket* packet = av_packet_alloc();
        if (!packet) { qDebug() << "av_packet_alloc failed"; break; }
        av_new_packet(packet, auData.size());
        memcpy(packet->data, auData.constData(), auData.size());

        // Tag packet with AU index so we can identify frames in decoder output.
        // The decoder preserves PTS from input to output, allowing us to map
        // each decoded frame back to its AU (decode-order) index.
        packet->pts = i;
        packet->dts = i;

        while (true) {
            int ret = avcodec_send_packet(mDecoder, packet);
            if (ret == 0) break;  // accepted
            if (ret == AVERROR(EAGAIN)) {
                // Decoder input full, drain output first then retry
                if (!drainDecoder()) { av_packet_free(&packet); return false; }
                continue;
            }
            // Other error, skip this AU
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "      send_packet error at frame" << i << ":" << errbuf;
            break;
        }
        av_packet_free(&packet);

        // Drain all available output frames
        if (!drainDecoder()) return false;
    }

    // Enter drain mode: send NULL once, then drain remaining buffered frames
    avcodec_send_packet(mDecoder, nullptr);

    // Drain loop for flush - call receive_frame until EOF
    while (true) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) break;
        int ret = avcodec_receive_frame(mDecoder, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            break;
        }

        // Initialize encoder if needed (for edge case where all frames come from flush)
        if (!encoderInitialized) {
            mDecodedWidth = frame->width;
            mDecodedHeight = frame->height;
            mDecodedPixFmt = static_cast<AVPixelFormat>(frame->format);
            if (!setupEncoder()) {
                av_frame_free(&frame);
                for (AVFrame* f : allDecodedFrames) av_frame_free(&f);
                return false;
            }
            encoderInitialized = true;
        }

        allDecodedFrames.append(frame);
    }

    int lostFrames = (decodeEnd - decodeStart + 1) - allDecodedFrames.size();

    qDebug() << "      Decoded" << allDecodedFrames.size() << "frames from"
             << (decodeEnd - decodeStart + 1) << "input AUs"
             << "(" << lostFrames << "lost to decoder delay)";

    // ---- Display-order to AU-index mapping ----
    QList<AVFrame*> framesToEncode;
    int streamCopyLimit = (streamCopyStartFrame >= 0) ? streamCopyStartFrame : (endFrame + 1);
    int realStartAU = startFrame;  // fallback if mapping fails

    if (mParser.isPAFF()) {
        // PAFF: Position-based frame selection counting from decodeStart.
        // The navigation decoder (individual field packets) and this decoder
        // (merged AUs) produce slightly different display-order sequences due
        // to B-frame reorder buffering. This means position-based counting may
        // select frames whose AU indices extend past streamCopyLimit, causing
        // the re-encode to overlap with stream-copy.
        //
        // When overlap is detected, we extend the re-encode to the next
        // keyframe (collecting all decoded frames in between) and adjust
        // streamCopyStart accordingly. This prevents both frame repetitions
        // (from overlap) and frame gaps (from jumping stream-copy forward
        // without filling the re-encode).
        int skipCount = startFrame - decodeStart;
        int encodeCount = streamCopyLimit - startFrame;

        qDebug() << "      PAFF frame selection: startFrame" << startFrame
                 << "decodeStart" << decodeStart << "skipCount" << skipCount
                 << "encodeCount" << encodeCount
                 << "decoded" << allDecodedFrames.size() << "streamCopyLimit" << streamCopyLimit;

        // First pass: collect encodeCount frames and track max AU
        int collected = 0;
        int maxSelectedAU = -1;
        for (int i = 0; i < allDecodedFrames.size(); ++i) {
            if (i < skipCount) continue;
            if (collected >= encodeCount) break;
            int au = static_cast<int>(allDecodedFrames[i]->pts);
            if (au > maxSelectedAU) maxSelectedAU = au;
            collected++;
        }

        // Check for overlap and determine extended limit if needed
        int extendedStreamCopyLimit = streamCopyLimit;
        if (maxSelectedAU >= streamCopyLimit && adjustedStreamCopyStart) {
            int nextKF = mParser.findKeyframeAfter(streamCopyLimit + 1);
            if (nextKF > 0) {
                extendedStreamCopyLimit = nextKF;
                *adjustedStreamCopyStart = nextKF;
                qDebug() << "      PAFF overlap: maxSelectedAU" << maxSelectedAU
                         << ">= streamCopyLimit" << streamCopyLimit
                         << "-> extending re-encode to next keyframe" << nextKF;
            }
        }

        // Second pass: collect frames — initial encodeCount + extension if needed
        collected = 0;
        for (int i = 0; i < allDecodedFrames.size(); ++i) {
            if (i < skipCount) {
                av_frame_free(&allDecodedFrames[i]);
                continue;
            }

            int au = static_cast<int>(allDecodedFrames[i]->pts);

            // Collect initial encodeCount frames (position-based)
            // OR extension frames (AU-index-based, fill gap to next keyframe)
            bool inInitialRange = (collected < encodeCount);
            bool inExtension = (extendedStreamCopyLimit > streamCopyLimit
                                && collected >= encodeCount
                                && au >= streamCopyLimit
                                && au < extendedStreamCopyLimit);

            if (inInitialRange || inExtension) {
                if (framesToEncode.isEmpty()) {
                    realStartAU = au;
                }
                framesToEncode.append(allDecodedFrames[i]);
                collected++;
            } else {
                av_frame_free(&allDecodedFrames[i]);
            }
        }
        allDecodedFrames.clear();

        qDebug() << "      PAFF: selected" << framesToEncode.size() << "frames,"
                 << "realStartAU =" << realStartAU
                 << "(requested:" << encodeCount
                 << ", extended to:" << extendedStreamCopyLimit << ")";

        // PAFF: Do NOT report actualStartAU.
        // The video output has the full segment frame count (position-based
        // counting matches the UI). Audio keepList must NOT be shortened.
    } else {
        // Non-PAFF: Display-order to AU-index mapping (tested and stable since v0.61.4-v0.61.6).
        // TTCut-ng navigates by seeking to the nearest keyframe and counting decoded
        // frames in display order. Due to B-frame reordering, the displayed content
        // at "frame N" comes from a DIFFERENT AU than AU N. The frame number stored
        // in the project file is the AU index, but the user selected content at the
        // corresponding DISPLAY position.
        //
        // We replicate TTCut-ng's navigation: find the keyframe in our decoder output,
        // count forward by displayOffset frames (skipping Open GOP B-frames from
        // before the keyframe), and use the resulting AU index for frame selection.
        // This is safe for all streams: without B-frames, displayOffset maps to the
        // same AU index (no-op). With B-frames at CutIn=keyframe, displayOffset=0.
        int uiKeyframe = mParser.findKeyframeBefore(startFrame);
        int displayOffset = startFrame - uiKeyframe;

        // Find the UI keyframe in our decoder output by PTS
        int kfOutputIdx = -1;
        for (int i = 0; i < allDecodedFrames.size(); ++i) {
            if (allDecodedFrames[i]->pts == uiKeyframe) {
                kfOutputIdx = i;
                break;
            }
        }

        if (kfOutputIdx >= 0) {
            // Count displayOffset frames forward from keyframe in display order,
            // skipping Open GOP B-frames from before the keyframe (these wouldn't
            // be in TTCut-ng's decode since it starts fresh at the keyframe).
            int count = 0;
            for (int i = kfOutputIdx; i < allDecodedFrames.size(); ++i) {
                int au = static_cast<int>(allDecodedFrames[i]->pts);
                if (au < uiKeyframe) continue;  // Skip Open GOP B-frames
                if (count == displayOffset) {
                    realStartAU = au;
                    break;
                }
                count++;
            }
            qDebug() << "      Display-order mapping: UI frame" << startFrame
                     << "= keyframe" << uiKeyframe << "+" << displayOffset
                     << "-> AU" << realStartAU
                     << "(keyframe at decoded[" << kfOutputIdx << "])";
        } else {
            qDebug() << "      WARNING: keyframe AU" << uiKeyframe
                     << "not found in decoder output, using AU index directly";
        }

        // Check if display-order mapping moved CutIn past the stream-copy boundary.
        // This happens when the B-frame reorder delay shifts the real content to AUs
        // at or after the stream-copy keyframe.
        if (realStartAU >= streamCopyLimit) {
            // B-frame reorder delay moved CutIn AU at or past stream-copy boundary.
            // Extend re-encode to next keyframe so encoder produces IDR for clean
            // DPB reset. Any POC domain mismatch at the transition is handled by
            // poc_lsb patching below.
            int nextKF = mParser.findKeyframeAfter(streamCopyStartFrame + 1);
            if (nextKF < 0 || (endFrame >= 0 && nextKF > endFrame + 50)) {
                nextKF = frameCount();
            }
            streamCopyLimit = nextKF;

            qDebug() << "      CutIn AU" << realStartAU
                     << ">= stream-copy start" << streamCopyStartFrame
                     << "-> extending re-encode to next keyframe" << nextKF;

            if (adjustedStreamCopyStart) {
                *adjustedStreamCopyStart = nextKF;
            }
        }

        // Report actual start AU if it differs from requested (B-frame reorder shift)
        if (actualStartAU && realStartAU != startFrame) {
            *actualStartAU = realStartAU;
            qDebug() << "      Actual start AU:" << realStartAU
                     << "(requested:" << startFrame << ", shift:" << (realStartAU - startFrame) << "frames)";
        }

        // Select frames by corrected AU index range [realStartAU, streamCopyLimit)
        // Open-GOP B-frames before realStartAU are ad content in display order
        // and must be excluded from the output.
        for (int i = 0; i < allDecodedFrames.size(); ++i) {
            int auIndex = static_cast<int>(allDecodedFrames[i]->pts);
            if (auIndex >= realStartAU && auIndex < streamCopyLimit) {
                framesToEncode.append(allDecodedFrames[i]);
            } else {
                av_frame_free(&allDecodedFrames[i]);
            }
        }
        allDecodedFrames.clear();
    }

    qDebug() << "      Selected" << framesToEncode.size() << "frames for encoding"
             << "(AU range" << startFrame << "-" << (streamCopyLimit - 1) << ")";

    // Re-encode frames
    // Buffer the last encoder packet so we can patch poc_lsb before writing it,
    // preventing POC domain mismatch at the re-encode→stream-copy transition.
    bool firstFrame = true;
    bool encoderSpsParsed = false;
    int framesSent = 0;
    int packetsReceived = 0;
    QByteArray pendingPacket;  // buffered last encoder packet

    // Encoder PPS info for SPS unification (parsed from first encoder packet)
    H264PpsInfo encPpsForRewrite = { true, false, true, false, false, 0, 0, 0, false };
    bool encPpsParsed = false;

    // Helper: apply SPS reorder patch (or SPS unification rewrite) and buffer
    auto writeEncoderPacket = [&](const QByteArray& rawData) -> bool {
        QByteArray encodedData = rawData;

        if (mSpsUnification && mParser.codecType() == NALU_CODEC_H264 && encoderSpsParsed) {
            // SPS Unification: extract encoder PPS from first packet, rewrite all slices
            if (!encPpsParsed) {
                // Extract encoder PPS for slice header field layout.
                // The PPS(id=1) is written INLINE before the first encoder slice
                // (not at ES start — that corrupts MKV NAL parsing).
                QByteArray encPpsNal = extractPpsFromPacket(rawData);
                if (!encPpsNal.isEmpty()) {
                    encPpsForRewrite = parseH264PpsInfo(encPpsNal);
                    encPpsParsed = true;

                    // PPS(id=1) is now kept inside each encoder packet
                    // by rewriteEncoderPacketForSourceSps (patches pps_id inline).
                    // No separate PPS write needed.
                    qDebug() << "      SPS Unification: encoder PPS parsed, pps_id=1 kept inline";
                }
            }

            // Rewrite encoder packet: strip SPS/PPS, rewrite slice NALs
            if (encPpsParsed) {
                encodedData = rewriteEncoderPacketForSourceSps(
                    encodedData,
                    mEncoderLog2MaxFrameNum, mEncoderLog2MaxPocLsb, mEncoderFrameMbsOnly,
                    mLog2MaxFrameNum, mLog2MaxPocLsb, mFrameMbsOnly,
                    encPpsForRewrite, 1, mEncoderPacketsWritten);  // newPpsId=1
            }
        } else if (mReorderDelay > 0 && mParser.codecType() == NALU_CODEC_H264) {
            // Standard path: just patch SPS reorder frames
            QByteArray patched = patchSpsNalsInAccessUnit(encodedData, mReorderDelay);
            if (patched != encodedData)
                encodedData = patched;
        }

        // Write previously buffered packet, buffer current one
        if (!pendingPacket.isEmpty()) {
            if (outFile.write(pendingPacket) != pendingPacket.size()) {
                setError("Failed to write encoded data");
                return false;
            }
        }
        pendingPacket = encodedData;
        packetsReceived++;
        mEncoderPacketsWritten++;
        return true;
    };

    for (AVFrame* frame : framesToEncode) {
        if (firstFrame) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 0, 0)
            frame->key_frame = 1;
#endif
            firstFrame = false;
        } else {
            frame->pict_type = AV_PICTURE_TYPE_NONE;
        }

        frame->pts = framesSent;

        int ret = avcodec_send_frame(mEncoder, frame);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_send_frame failed:" << errbuf;
            setError(QString("Encoding failed: %1").arg(errbuf));
            for (AVFrame* f : framesToEncode) av_frame_free(&f);
            return false;
        }
        framesSent++;

        AVPacket* packet = av_packet_alloc();
        if (!packet) { qDebug() << "av_packet_alloc failed"; break; }
        while (true) {
            ret = avcodec_receive_packet(mEncoder, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                qDebug() << "TTESSmartCut: avcodec_receive_packet failed:" << errbuf;
                av_packet_free(&packet);
                for (AVFrame* f : framesToEncode) av_frame_free(&f);
                return false;
            }
            QByteArray rawData(reinterpret_cast<char*>(packet->data), packet->size);

            // Parse encoder SPS from first packet's inline SPS NAL
            // (without GLOBAL_HEADER, x264 puts SPS/PPS inline, not in extradata)
            if (!encoderSpsParsed && mParser.codecType() == NALU_CODEC_H264) {
                H264SpsInfo encSps;
                if (findH264SpsInPacket(rawData, encSps)) {
                    mEncoderLog2MaxFrameNum = encSps.log2MaxFrameNumMinus4 + 4;
                    mEncoderLog2MaxPocLsb = (encSps.pocType == 0)
                        ? encSps.log2MaxPocLsbMinus4 + 4 : 0;
                    mEncoderPocType = encSps.pocType;
                    mEncoderFrameMbsOnly = encSps.frameMbsOnly;
                    qDebug() << "      Encoder SPS: log2_fn=" << mEncoderLog2MaxFrameNum
                             << "log2_poc=" << mEncoderLog2MaxPocLsb
                             << "poc_type=" << mEncoderPocType;
                    encoderSpsParsed = true;
                }
            }

            if (!writeEncoderPacket(rawData)) {
                av_packet_free(&packet);
                for (AVFrame* f : framesToEncode) av_frame_free(&f);
                return false;
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
    }
    framesToEncode.clear();

    // Flush encoder
    avcodec_send_frame(mEncoder, nullptr);
    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;
    while (true) {
        int ret = avcodec_receive_packet(mEncoder, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_receive_packet (flush) failed:" << errbuf;
            break;
        }
        QByteArray rawData(reinterpret_cast<char*>(packet->data), packet->size);

        // Parse encoder SPS from first packet's inline SPS NAL (flush path)
        if (!encoderSpsParsed && mParser.codecType() == NALU_CODEC_H264) {
            H264SpsInfo encSps;
            if (findH264SpsInPacket(rawData, encSps)) {
                mEncoderLog2MaxFrameNum = encSps.log2MaxFrameNumMinus4 + 4;
                mEncoderLog2MaxPocLsb = (encSps.pocType == 0)
                    ? encSps.log2MaxPocLsbMinus4 + 4 : 0;
                mEncoderPocType = encSps.pocType;
                mEncoderFrameMbsOnly = encSps.frameMbsOnly;
                qDebug() << "      Encoder SPS: log2_fn=" << mEncoderLog2MaxFrameNum
                         << "log2_poc=" << mEncoderLog2MaxPocLsb
                         << "poc_type=" << mEncoderPocType;
                encoderSpsParsed = true;
            }
        }

        if (!writeEncoderPacket(rawData)) {
            av_packet_free(&packet);
            return false;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    // POC domain mismatch fix: patch poc_lsb in the last encoder packet
    // to prevent PicOrderCntMsb wrap at re-encode→stream-copy transition.
    // Only needed for H.264 with poc_type=0 when stream-copy follows.
    // With SPS Unification, poc_lsb is already widened to source domain,
    // so we use source params for both reading and patching.
    int actualScStart = (adjustedStreamCopyStart && *adjustedStreamCopyStart >= 0)
        ? *adjustedStreamCopyStart : streamCopyStartFrame;

    // Determine which params to use for the pending packet's poc_lsb
    int pendingFnWidth = mSpsUnification ? mLog2MaxFrameNum : mEncoderLog2MaxFrameNum;
    int pendingPocWidth = mSpsUnification ? mLog2MaxPocLsb : mEncoderLog2MaxPocLsb;
    bool pendingFmOnly = mSpsUnification ? mFrameMbsOnly : mEncoderFrameMbsOnly;

    if (!pendingPacket.isEmpty() && mEncoderPocType == 0 &&
        pendingPocWidth > 0 && mLog2MaxPocLsb > 0 &&
        actualScStart >= 0 && mParser.codecType() == NALU_CODEC_H264) {

        // Read source's first stream-copy frame poc_lsb
        QByteArray srcAU = mParser.readAccessUnitData(actualScStart);
        int srcPocLsb = readPocLsbFromAU(srcAU, mLog2MaxFrameNum,
                                          mLog2MaxPocLsb, mFrameMbsOnly);

        // Read encoder's last slice poc_lsb (already in source domain if SPS unification)
        int encPocLsb = readPocLsbFromAU(pendingPacket, pendingFnWidth,
                                          pendingPocWidth, pendingFmOnly);

        if (srcPocLsb >= 0 && encPocLsb >= 0) {
            int srcMaxPocLsb = 1 << mLog2MaxPocLsb;
            int diff = qAbs(srcPocLsb - encPocLsb);
            if (diff > srcMaxPocLsb / 2) {
                // Compute safe poc_lsb that avoids wrap
                int patchMaxPocLsb = 1 << pendingPocWidth;
                int target = srcPocLsb - srcMaxPocLsb / 2;
                if (target < 0) target += srcMaxPocLsb;
                int newPocLsb = target % patchMaxPocLsb;

                // Post-patch validation
                int newDiff = qAbs(srcPocLsb - newPocLsb);
                if (newDiff > srcMaxPocLsb / 2) {
                    int bestPocLsb = newPocLsb;
                    int bestDiff = newDiff;
                    for (int v = 0; v < patchMaxPocLsb; ++v) {
                        int d = qAbs(srcPocLsb - v);
                        if (d <= srcMaxPocLsb / 2 && d < bestDiff) {
                            bestPocLsb = v;
                            bestDiff = d;
                        }
                    }
                    if (bestDiff <= srcMaxPocLsb / 2) {
                        newPocLsb = bestPocLsb;
                        qDebug() << "      POC domain fix: modulo re-wrap detected,"
                                 << "using search result" << newPocLsb;
                    } else {
                        qWarning() << "      POC domain fix: WARNING no safe poc_lsb"
                                   << "found in range [0," << patchMaxPocLsb << ")";
                    }
                }

                qDebug() << "      POC domain fix: encoder poc_lsb=" << encPocLsb
                         << "source poc_lsb=" << srcPocLsb
                         << "diff=" << diff << "> MaxPocLsb/2=" << srcMaxPocLsb / 2
                         << "-> patching to" << newPocLsb;

                pendingPacket = patchPocLsbInPacket(pendingPacket,
                    pendingFnWidth, pendingPocWidth,
                    pendingFmOnly, static_cast<uint32_t>(newPocLsb));
            }
        }
    }

    // Write the (possibly patched) last encoder packet
    if (!pendingPacket.isEmpty()) {
        if (outFile.write(pendingPacket) != pendingPacket.size()) {
            setError("Failed to write last encoded packet");
            return false;
        }
    }

    qDebug() << "      Encoding complete: sent" << framesSent << "frames, received" << packetsReceived << "packets";
    mFramesReencoded += packetsReceived;

    if (mTotalFrames > 0) {
        int percent = ((mFramesStreamCopied + mFramesReencoded) * 100) / mTotalFrames;
        emit progressChanged(qMin(percent, 99),
            tr("Processing segment %1/%2").arg(mCurrentSegment).arg(mTotalSegments));
    }

    return true;
}

// ----------------------------------------------------------------------------
// Setup decoder
// ----------------------------------------------------------------------------
bool TTESSmartCut::setupDecoder()
{
    freeDecoder();

    AVCodecID codecId;
    if (mParser.codecType() == NALU_CODEC_H264) {
        codecId = AV_CODEC_ID_H264;
    } else if (mParser.codecType() == NALU_CODEC_H265) {
        codecId = AV_CODEC_ID_HEVC;
    } else {
        setError("Unsupported codec type");
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec) {
        setError("Cannot find decoder");
        return false;
    }

    mDecoder = avcodec_alloc_context3(codec);
    if (!mDecoder) {
        setError("Cannot allocate decoder context");
        return false;
    }

    // Feed all parameter sets (VPS/SPS/PPS) to decoder.
    // H.265 streams may contain multiple parameter sets with different IDs;
    // only feeding the first one would cause decode errors after a parameter
    // set change mid-stream.
    QByteArray extradata;
    if (mParser.codecType() == NALU_CODEC_H265) {
        for (int i = 0; i < mParser.vpsCount(); i++)
            extradata.append(mParser.getVPS(i));
    }
    for (int i = 0; i < mParser.spsCount(); i++)
        extradata.append(mParser.getSPS(i));
    for (int i = 0; i < mParser.ppsCount(); i++)
        extradata.append(mParser.getPPS(i));

    if (!extradata.isEmpty()) {
        mDecoder->extradata_size = extradata.size();
        mDecoder->extradata = static_cast<uint8_t*>(
            av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        memcpy(mDecoder->extradata, extradata.constData(), extradata.size());
    }

    // Disable frame threading to prevent PTS misassignment.
    // Frame-threaded H.264 decoders can mismap content to PTS values
    // when starting mid-stream, causing wrong frames to be re-encoded.
    mDecoder->thread_count = 1;

    int ret = avcodec_open2(mDecoder, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        setError(QString("Cannot open decoder: %1").arg(errbuf));
        avcodec_free_context(&mDecoder);
        return false;
    }

    qDebug() << "TTESSmartCut: Decoder setup complete";
    return true;
}

// ----------------------------------------------------------------------------
// Setup encoder
// ----------------------------------------------------------------------------
bool TTESSmartCut::setupEncoder()
{
    freeEncoder();
    mEncoderPts = 0;

    const char* encoderName;
    if (mParser.codecType() == NALU_CODEC_H264) {
        encoderName = "libx264";
    } else if (mParser.codecType() == NALU_CODEC_H265) {
        encoderName = "libx265";
    } else {
        setError("Unsupported codec type for encoding");
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName);
    if (!codec) {
        setError(QString("Cannot find encoder: %1").arg(encoderName));
        return false;
    }

    mEncoder = avcodec_alloc_context3(codec);
    if (!mEncoder) {
        setError("Cannot allocate encoder context");
        return false;
    }

    // Get parameters from decoded frame (mDecodedWidth/Height/PixFmt)
    // These are set after decoding the first frame in reencodeFrames()
    if (mDecodedWidth > 0 && mDecodedHeight > 0 && mDecodedPixFmt != AV_PIX_FMT_NONE) {
        mEncoder->width = mDecodedWidth;
        mEncoder->height = mDecodedHeight;
        mEncoder->pix_fmt = static_cast<AVPixelFormat>(mDecodedPixFmt);
        qDebug() << "  Using decoded frame parameters:" << mDecodedWidth << "x" << mDecodedHeight
                 << "pix_fmt=" << mDecodedPixFmt;
    } else if (mDecoder) {
        // Fallback to decoder context (may not work if no frames decoded yet)
        mEncoder->width = mDecoder->width;
        mEncoder->height = mDecoder->height;
        mEncoder->pix_fmt = mDecoder->pix_fmt;
    } else {
        // Defaults - should not reach here if called correctly
        setError("Encoder setup called without decoded frame parameters");
        return false;
    }

    // Copy SAR, color space and profile/level from decoder context
    // These must be set regardless of which path provided width/height
    if (mDecoder) {
        mEncoder->sample_aspect_ratio = mDecoder->sample_aspect_ratio;
        mEncoder->color_primaries     = mDecoder->color_primaries;
        mEncoder->color_trc           = mDecoder->color_trc;
        mEncoder->colorspace          = mDecoder->colorspace;
        mEncoder->color_range         = mDecoder->color_range;
        mEncoder->profile             = mDecoder->profile;
        mEncoder->level               = mDecoder->level;
        qDebug() << "  Copied from decoder: SAR=" << mDecoder->sample_aspect_ratio.num
                 << "/" << mDecoder->sample_aspect_ratio.den
                 << "profile=" << mDecoder->profile << "level=" << mDecoder->level;
    }

    // Validate parameters
    if (mEncoder->width <= 0 || mEncoder->height <= 0 || mEncoder->pix_fmt == AV_PIX_FMT_NONE) {
        setError(QString("Invalid encoder parameters: %1x%2 pix_fmt=%3")
                 .arg(mEncoder->width).arg(mEncoder->height).arg(mEncoder->pix_fmt));
        avcodec_free_context(&mEncoder);
        return false;
    }

    // Time base and frame rate
    mEncoder->time_base = (AVRational){1, static_cast<int>(mFrameRate * 1000)};
    mEncoder->framerate = (AVRational){static_cast<int>(mFrameRate * 1000), 1000};

    // No qmin/qmax override — let CRF control quality for minimal
    // mismatch between re-encoded and stream-copied sections

    // No B-frames in re-encoded section. The reorder buffer issue at the
    // re-encode/stream-copy boundary is solved by patching the original SPS
    // (written before stream-copy) to signal max_num_reorder_frames=4.
    mEncoder->max_b_frames = 0;

    // Interlace support for MBAFF/PAFF content
    if (mInterlaced) {
        mEncoder->flags |= AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME;
        mEncoder->field_order = mTopFieldFirst ? AV_FIELD_TT : AV_FIELD_BB;
        qDebug() << "  Encoder: interlaced mode" << (mTopFieldFirst ? "TFF" : "BFF");
    }

    // Thread count
    mEncoder->thread_count = 0;  // Auto

    // Use codec-specific settings directly (not the generic encoderCrf/encoderPreset
    // which may still hold MPEG-2 values if encoderCodec was not updated)
    int crf, presetIdx, profileIdx;

    static const char* presetNames[] = {
        "ultrafast", "superfast", "veryfast", "faster", "fast",
        "medium", "slow", "slower", "veryslow"
    };

    AVDictionary* opts = nullptr;

    if (mParser.codecType() == NALU_CODEC_H264) {
        crf        = TTCut::h264Crf;
        presetIdx  = (mPresetOverride >= 0) ? qBound(0, mPresetOverride, 8)
                                            : qBound(0, TTCut::h264Preset, 8);
        profileIdx = qBound(0, TTCut::h264Profile, 5);

        static const char* h264Profiles[] = {
            "baseline", "main", "high", "high10", "high422", "high444"
        };

        // Auto-detect bit depth from pixel format and override profile if needed
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(mEncoder->pix_fmt);
        if (desc) {
            int bitDepth = desc->comp[0].depth;
            if (bitDepth >= 10 && profileIdx < 3) {
                profileIdx = 3;  // high10
                qDebug() << "TTESSmartCut: Auto-selected high10 profile for" << bitDepth << "bit source";
            }
        }

        av_dict_set(&opts, "profile", h264Profiles[profileIdx], 0);

        // Force IDR frames when requesting I-frames. Without this, x264
        // produces Non-IDR I-frames (NAL type 1) which don't flush the
        // decoder's delayed_pic[] reorder buffer, causing frame interleaving
        // at segment boundaries.
        av_dict_set(&opts, "forced-idr", "1", 0);


    } else {
        // H.265
        crf        = TTCut::h265Crf;
        presetIdx  = (mPresetOverride >= 0) ? qBound(0, mPresetOverride, 8)
                                            : qBound(0, TTCut::h265Preset, 8);
        profileIdx = qBound(0, TTCut::h265Profile, 4);

        static const char* h265Profiles[] = {
            "main", "main10", "main12", "main422-10", "main444-10"
        };

        // Auto-detect bit depth from pixel format and override profile if needed
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(mEncoder->pix_fmt);
        if (desc) {
            int bitDepth = desc->comp[0].depth;
            if (bitDepth >= 12 && profileIdx < 2) {
                profileIdx = 2;  // main12
                qDebug() << "TTESSmartCut: Auto-selected main12 profile for" << bitDepth << "bit source";
            } else if (bitDepth >= 10 && profileIdx < 1) {
                profileIdx = 1;  // main10
                qDebug() << "TTESSmartCut: Auto-selected main10 profile for" << bitDepth << "bit source";
            }
        }

        av_dict_set(&opts, "profile", h265Profiles[profileIdx], 0);
    }

    av_dict_set(&opts, "preset", presetNames[presetIdx], 0);
    av_dict_set(&opts, "crf", QString::number(crf).toUtf8().constData(), 0);

    qDebug() << "TTESSmartCut: Encoder settings -"
             << "codec:" << (mParser.codecType() == NALU_CODEC_H264 ? "H.264" : "H.265")
             << "preset:" << presetNames[presetIdx]
             << "crf:" << crf
             << "profile:" << profileIdx
             << "decoder profile:" << (mDecoder ? mDecoder->profile : -1)
             << "decoder level:" << (mDecoder ? mDecoder->level : -1);

    int ret = avcodec_open2(mEncoder, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        setError(QString("Cannot open encoder: %1").arg(errbuf));
        avcodec_free_context(&mEncoder);
        return false;
    }

    qDebug() << "TTESSmartCut: Encoder setup complete";
    qDebug() << "  Size:" << mEncoder->width << "x" << mEncoder->height;
    qDebug() << "  No B-frames for clean transitions";

    return true;
}

// ----------------------------------------------------------------------------
// Free decoder
// ----------------------------------------------------------------------------
void TTESSmartCut::freeDecoder()
{
    if (mDecoder) {
        avcodec_free_context(&mDecoder);
        mDecoder = nullptr;
    }
}

// ----------------------------------------------------------------------------
// Free encoder
// ----------------------------------------------------------------------------
void TTESSmartCut::freeEncoder()
{
    if (mEncoder) {
        avcodec_free_context(&mEncoder);
        mEncoder = nullptr;
    }
}

// ----------------------------------------------------------------------------
// Decode frame from NAL data
// ----------------------------------------------------------------------------
bool TTESSmartCut::decodeFrame(const QByteArray& nalData, AVFrame* frame)
{
    if (!mDecoder) return false;

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    if (!nalData.isEmpty()) {
        av_new_packet(packet, nalData.size());
        memcpy(packet->data, nalData.constData(), nalData.size());

        int ret = avcodec_send_packet(mDecoder, packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_packet_free(&packet);
            return false;
        }
    } else {
        // Flush
        avcodec_send_packet(mDecoder, nullptr);
    }

    av_packet_free(&packet);

    int ret = avcodec_receive_frame(mDecoder, frame);
    return (ret >= 0);
}

// ----------------------------------------------------------------------------
// Encode frame to NAL units
// ----------------------------------------------------------------------------
QByteArray TTESSmartCut::encodeFrame(AVFrame* frame, bool forceKeyframe)
{
    if (!mEncoder) {
        qDebug() << "TTESSmartCut::encodeFrame: Encoder not initialized";
        return QByteArray();
    }

    if (frame) {
        // Update encoder parameters if needed
        if (frame->width != mEncoder->width || frame->height != mEncoder->height) {
            qDebug() << "TTESSmartCut: Frame size mismatch:" << frame->width << "x" << frame->height
                     << "vs encoder" << mEncoder->width << "x" << mEncoder->height;
            freeEncoder();
            mEncoder = nullptr;
            return QByteArray();
        }

        // Check pixel format
        if (frame->format != mEncoder->pix_fmt) {
            qDebug() << "TTESSmartCut: Frame pixel format mismatch:" << frame->format
                     << "vs encoder" << mEncoder->pix_fmt;
            // Try to convert or just log warning and continue
        }

        // Set PTS (member variable, reset in cleanup/setupEncoder)
        frame->pts = mEncoderPts++;

        // Force keyframe if requested
        if (forceKeyframe) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 0, 0)
            frame->flags |= AV_FRAME_FLAG_KEY;
#else
            frame->key_frame = 1;
#endif
        }

        int ret = avcodec_send_frame(mEncoder, frame);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_send_frame failed:" << errbuf;
            return QByteArray();
        }
    } else {
        // Flush
        int ret = avcodec_send_frame(mEncoder, nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_send_frame (flush) failed:" << errbuf;
        }
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return QByteArray();
    int ret = avcodec_receive_packet(mEncoder, packet);

    if (ret < 0) {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_receive_packet failed:" << errbuf;
        }
        av_packet_free(&packet);
        return QByteArray();
    }

    QByteArray result(reinterpret_cast<char*>(packet->data), packet->size);
    av_packet_free(&packet);

    return result;
}

// ----------------------------------------------------------------------------
// Filter encoder output - remove SPS/PPS NALs, keep only slices
// This is needed because x264/x265 embed their own SPS/PPS which have
// different parameters than the original stream. We use the original
// SPS/PPS for both re-encoded and stream-copied sections.
// ----------------------------------------------------------------------------
QByteArray TTESSmartCut::filterEncoderOutput(const QByteArray& data)
{
    QByteArray result;
    int pos = 0;
    int dataLen = data.size();

    while (pos < dataLen - 4) {
        // Find next start code
        int scLen = 0;
        int scPos = -1;

        for (int i = pos; i < dataLen - 3; i++) {
            if (data[i] == '\0' && data[i+1] == '\0') {
                if (data[i+2] == '\x01') {
                    scPos = i;
                    scLen = 3;
                    break;
                } else if (i < dataLen - 4 && data[i+2] == '\0' && data[i+3] == '\x01') {
                    scPos = i;
                    scLen = 4;
                    break;
                }
            }
        }

        if (scPos < 0) break;

        // Find end of this NAL (next start code or end of data)
        int nalStart = scPos + scLen;
        int nalEnd = dataLen;

        for (int i = nalStart + 1; i < dataLen - 3; i++) {
            if (data[i] == '\0' && data[i+1] == '\0') {
                if (data[i+2] == '\x01' || (i < dataLen - 4 && data[i+2] == '\0' && data[i+3] == '\x01')) {
                    nalEnd = i;
                    break;
                }
            }
        }

        // Check NAL type
        if (nalStart < dataLen) {
            uint8_t nalByte = static_cast<uint8_t>(data[nalStart]);
            int nalType;

            if (mParser.codecType() == NALU_CODEC_H264) {
                nalType = nalByte & 0x1F;
                // H.264: Keep slices (1, 5), skip SPS (7), PPS (8), SEI (6), AUD (9)
                if (nalType == 1 || nalType == 5) {
                    result.append(data.mid(scPos, nalEnd - scPos));
                }
            } else {
                // H.265: NAL type is in bits 1-6 of first byte
                nalType = (nalByte >> 1) & 0x3F;
                // H.265: Keep slices (0-21), skip VPS (32), SPS (33), PPS (34), SEI (39, 40)
                if (nalType <= 21) {
                    result.append(data.mid(scPos, nalEnd - scPos));
                }
            }
        }

        pos = nalEnd;
    }

    return result;
}

// ----------------------------------------------------------------------------
// Write NAL unit with start code
// ----------------------------------------------------------------------------
bool TTESSmartCut::writeNalUnit(QFile& outFile, const QByteArray& nalData)
{
    // Check if data already has start code
    bool hasStartCode = false;
    if (nalData.size() >= 4) {
        if (nalData[0] == '\0' && nalData[1] == '\0') {
            if (nalData[2] == '\x01' || (nalData[2] == '\0' && nalData[3] == '\x01')) {
                hasStartCode = true;
            }
        }
    }

    if (!hasStartCode) {
        // Add 4-byte start code
        static const char startCode[4] = {0x00, 0x00, 0x00, 0x01};
        if (outFile.write(startCode, 4) != 4) {
            setError("Failed to write start code");
            return false;
        }
    }

    if (outFile.write(nalData) != nalData.size()) {
        setError("Failed to write NAL data");
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// H.264 SPS bitstream helpers for patching max_num_reorder_frames
// ----------------------------------------------------------------------------

// Remove emulation prevention bytes (00 00 03 -> 00 00) from NAL to get RBSP
static QByteArray removeEmulationPrevention(const QByteArray& nal)
{
    QByteArray rbsp;
    rbsp.reserve(nal.size());
    for (int i = 0; i < nal.size(); ++i) {
        if (i + 2 < nal.size() &&
            (uint8_t)nal[i] == 0x00 && (uint8_t)nal[i+1] == 0x00 && (uint8_t)nal[i+2] == 0x03) {
            rbsp.append(nal[i]);
            rbsp.append(nal[i+1]);
            i += 2;  // skip the 0x03
        } else {
            rbsp.append(nal[i]);
        }
    }
    return rbsp;
}

// Add emulation prevention bytes (00 00 XX -> 00 00 03 XX for XX <= 0x03) in RBSP
static QByteArray addEmulationPrevention(const QByteArray& rbsp)
{
    QByteArray nal;
    nal.reserve(rbsp.size() + rbsp.size() / 128);
    for (int i = 0; i < rbsp.size(); ++i) {
        if (i + 2 < rbsp.size() &&
            (uint8_t)rbsp[i] == 0x00 && (uint8_t)rbsp[i+1] == 0x00 &&
            ((uint8_t)rbsp[i+2] <= 0x03)) {
            nal.append(rbsp[i]);      // first 00
            nal.append(rbsp[i+1]);    // second 00
            nal.append((char)0x03);   // emulation prevention byte
            i += 1;  // skip one; for-loop's i++ skips another → consumed both 00s
            // Next iteration writes rbsp[i+2] (the original third byte)
        } else {
            nal.append(rbsp[i]);
        }
    }
    return nal;
}

// Read bits from RBSP byte array with bounds checking
static uint32_t spsReadBits(const uint8_t* data, int dataSize, int& bitPos, int numBits)
{
    uint32_t value = 0;
    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        if (byteIndex >= dataSize) return value;  // OOB guard
        int bitIndex = 7 - (bitPos % 8);
        value <<= 1;
        value |= (data[byteIndex] >> bitIndex) & 1;
        bitPos++;
    }
    return value;
}

// Write bits to RBSP byte array with bounds checking
static void spsWriteBits(uint8_t* data, int dataSize, int& bitPos, uint32_t value, int numBits)
{
    for (int i = numBits - 1; i >= 0; i--) {
        int byteIndex = bitPos / 8;
        if (byteIndex >= dataSize) return;  // OOB guard
        int bitIndex = 7 - (bitPos % 8);
        if (value & (1u << i))
            data[byteIndex] |= (1 << bitIndex);
        else
            data[byteIndex] &= ~(1 << bitIndex);
        bitPos++;
    }
}

// Read Exp-Golomb unsigned value from RBSP
static uint32_t spsReadUE(const uint8_t* data, int dataSize, int& bitPos)
{
    int leadingZeros = 0;
    while (spsReadBits(data, dataSize, bitPos, 1) == 0 && leadingZeros < 31)
        leadingZeros++;
    if (leadingZeros == 0) return 0;
    uint32_t value = spsReadBits(data, dataSize, bitPos, leadingZeros);
    return (1u << leadingZeros) - 1 + value;
}

// Read Exp-Golomb signed value from RBSP
static int32_t spsReadSE(const uint8_t* data, int dataSize, int& bitPos)
{
    uint32_t ue = spsReadUE(data, dataSize, bitPos);
    if (ue & 1) return static_cast<int32_t>((ue + 1) / 2);
    return -static_cast<int32_t>(ue / 2);
}

// Write Exp-Golomb unsigned value to RBSP
static void spsWriteUE(uint8_t* data, int dataSize, int& bitPos, uint32_t value)
{
    // Exp-Golomb: codeNum = value, code = (leadingZeros zeros)(1)(value bits)
    uint32_t codeNum = value + 1;
    int numBits = 0;
    uint32_t tmp = codeNum;
    while (tmp > 0) { numBits++; tmp >>= 1; }
    int leadingZeros = numBits - 1;
    // Write leading zeros
    for (int i = 0; i < leadingZeros; i++)
        spsWriteBits(data, dataSize, bitPos, 0, 1);
    // Write 1 followed by value bits
    spsWriteBits(data, dataSize, bitPos, codeNum, numBits);
}

// Write Exp-Golomb signed value to RBSP
static void spsWriteSE(uint8_t* data, int dataSize, int& bitPos, int32_t value)
{
    uint32_t ue;
    if (value > 0)
        ue = 2 * static_cast<uint32_t>(value) - 1;
    else
        ue = static_cast<uint32_t>(-2 * value);
    spsWriteUE(data, dataSize, bitPos, ue);
}

// ----------------------------------------------------------------------------
// SPS Unification: Rewrite a single encoder slice NAL to be compatible with
// the source SPS. Changes pps_id, widens frame_num and poc_lsb bit fields,
// inserts field_pic_flag=0 if needed. CABAC data is realigned.
// Input: NAL body (after start code, WITH emulation prevention bytes).
// Returns: rewritten NAL body, or empty on error.
// ----------------------------------------------------------------------------
static QByteArray rewriteEncoderSliceForSourceSps(
    const QByteArray& nalBody,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const H264PpsInfo& encPps, uint32_t newPpsId, int frameIndex)
{
    if (nalBody.isEmpty()) return QByteArray();

    uint8_t nalHeader = static_cast<uint8_t>(nalBody[0]);
    uint8_t nalType = nalHeader & 0x1F;
    if (nalType != 1 && nalType != 5) return QByteArray();  // only slice NALs

    uint8_t nalRefIdc = (nalHeader >> 5) & 0x03;

    // Demote subsequent IDRs (frameIndex > 0) to non-IDR I-slices.
    // The encoder produces IDRs at regular intervals (keyint). With linear
    // frame_num, a mid-sequence IDR at fn=54 causes a frame_num gap (the spec
    // requires fn=0 for IDR, but we write fn=54). This gap creates gray DPB
    // frames → artifacts. Converting to non-IDR keeps the linear fn sequence
    // intact and prevents unwanted DPB flushes within the re-encode.
    uint8_t origNalType = nalType;
    bool demoteIdr = (nalType == 5 && frameIndex > 0);
    if (demoteIdr) {
        nalType = 1;  // non-IDR slice
    }

    // Remove emulation prevention → RBSP
    QByteArray oldRbsp = removeEmulationPrevention(nalBody);
    const uint8_t* oldData = reinterpret_cast<const uint8_t*>(oldRbsp.constData());
    int oldSize = oldRbsp.size();

    // Allocate new RBSP (generous: old size + room for widened fields)
    QByteArray newRbsp(oldSize + 64, '\0');
    uint8_t* newData = reinterpret_cast<uint8_t*>(newRbsp.data());
    int newSize = newRbsp.size();

    int readPos = 0;
    int writePos = 0;

    // 1. NAL header — write with potentially changed nalType (IDR→non-IDR)
    uint32_t header = spsReadBits(oldData, oldSize, readPos, 8);
    if (demoteIdr) {
        // Rewrite header: keep forbidden_zero_bit + nal_ref_idc, change nal_unit_type
        header = (header & 0xE0) | (nalType & 0x1F);
    }
    spsWriteBits(newData, newSize, writePos, header, 8);

    // 2. first_mb_in_slice (UE) — copy
    uint32_t firstMb = spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, firstMb);

    // 3. slice_type (UE) — copy, save for later
    uint32_t sliceType = spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, sliceType);
    uint32_t sliceTypeMod = sliceType % 5;
    // 0=P, 1=B, 2=I, 3=SP, 4=SI

    // 4. pps_id (UE) — read old, write new
    spsReadUE(oldData, oldSize, readPos);  // skip old pps_id
    spsWriteUE(newData, newSize, writePos, newPpsId);

    // 5. frame_num — read encoder fn, write linear frameIndex to eliminate gaps.
    // The encoder cycles fn 0..MaxFN-1 (e.g., 0..15 with MaxFN=16). After SPS
    // rewriting to source MaxFN (e.g., 512), this cycling creates frame_num gaps
    // at every wrap (15→0 with MaxFN=512 = gap of 496 frames). At the re-encode
    // → stream-copy transition, the gap between the last encoder fn (15) and the
    // first stream-copy fn (e.g., 181) causes the decoder to create gray gap
    // frames in the DPB. Open-GOP B-frames then reference these gray frames as
    // L0 → block artifacts. Fix: use the linear frame index (0,1,2,...,N-1) as
    // frame_num. This produces a monotonic sequence that directly precedes the
    // stream-copy frame_nums, eliminating all gaps.
    spsReadBits(oldData, oldSize, readPos, encLog2MaxFN);  // consume encoder fn
    uint32_t srcMaxFrameNum = 1u << srcLog2MaxFN;
    uint32_t newFrameNum = static_cast<uint32_t>(frameIndex) % srcMaxFrameNum;
    spsWriteBits(newData, newSize, writePos, newFrameNum, srcLog2MaxFN);

    // 6. field_pic_flag (if !frame_mbs_only in SOURCE SPS)
    bool fieldPicFlag = false;
    if (!srcFrameMbsOnly) {
        if (!encFrameMbsOnly) {
            // Encoder also has field_pic_flag — read and copy
            fieldPicFlag = (spsReadBits(oldData, oldSize, readPos, 1) != 0);
            spsWriteBits(newData, newSize, writePos, fieldPicFlag ? 1 : 0, 1);
            if (fieldPicFlag) {
                uint32_t bottomFlag = spsReadBits(oldData, oldSize, readPos, 1);
                spsWriteBits(newData, newSize, writePos, bottomFlag, 1);
            }
        } else {
            // Encoder has frame_mbs_only=1, INSERT field_pic_flag=0 (frame-coded)
            spsWriteBits(newData, newSize, writePos, 0, 1);
        }
    }

    // 7. idr_pic_id (only in original IDR NALs, NAL type 5)
    if (origNalType == 5) {
        uint32_t idrPicId = spsReadUE(oldData, oldSize, readPos);
        if (!demoteIdr) {
            // Keep as IDR (first frame): write idr_pic_id
            spsWriteUE(newData, newSize, writePos, idrPicId);
        }
        // Demoted IDR: skip writing — non-IDR slices don't have idr_pic_id
    }

    // 8. poc_lsb (if poc_type 0) — linearize, not just widen.
    // The encoder's poc_lsb wraps at encMaxPocLsb (e.g. 16), producing
    // values 0,2,4,...,14,0,2,... When widened to srcMaxPocLsb (e.g. 256),
    // the decoder sees frequent backward jumps (14→0 is only -14, which
    // doesn't trigger PicOrderCntMsb increment at MaxPocLsb=256).
    // Fix: compute linear poc_lsb = (frameIndex * 2) % srcMaxPocLsb,
    // which only wraps at the source's MaxPocLsb boundary.
    if (encLog2MaxPocLsb > 0 && srcLog2MaxPocLsb > 0) {
        spsReadBits(oldData, oldSize, readPos, encLog2MaxPocLsb);  // skip old
        int srcMaxPocLsb = 1 << srcLog2MaxPocLsb;
        uint32_t newPocLsb = (static_cast<uint32_t>(frameIndex) * 2) % srcMaxPocLsb;
        spsWriteBits(newData, newSize, writePos, newPocLsb, srcLog2MaxPocLsb);

        // delta_pic_order_cnt_bottom (if PPS flag && !field_pic_flag) — copy
        if (encPps.bottomFieldPicOrderPresent && !fieldPicFlag) {
            int32_t deltaBottom = spsReadSE(oldData, oldSize, readPos);
            spsWriteSE(newData, newSize, writePos, deltaBottom);
        }
    }

    // 9. redundant_pic_cnt (if PPS flag) — copy
    if (encPps.redundantPicCntPresent) {
        uint32_t rpc = spsReadUE(oldData, oldSize, readPos);
        spsWriteUE(newData, newSize, writePos, rpc);
    }

    // 10. For P/B slices: additional fields before dec_ref_pic_marking
    if (sliceTypeMod == 1) {
        // B-slice: direct_spatial_mv_pred_flag (1 bit)
        uint32_t dsmpf = spsReadBits(oldData, oldSize, readPos, 1);
        spsWriteBits(newData, newSize, writePos, dsmpf, 1);
    }

    if (sliceTypeMod == 0 || sliceTypeMod == 1 || sliceTypeMod == 3) {
        // P, B, or SP: num_ref_idx_active_override_flag
        uint32_t overrideFlag = spsReadBits(oldData, oldSize, readPos, 1);
        spsWriteBits(newData, newSize, writePos, overrideFlag, 1);
        int numRefL0 = encPps.numRefIdxL0DefaultActiveMinus1;
        if (overrideFlag) {
            uint32_t numRefL0Override = spsReadUE(oldData, oldSize, readPos);
            spsWriteUE(newData, newSize, writePos, numRefL0Override);
            numRefL0 = numRefL0Override;
            if (sliceTypeMod == 1) {
                uint32_t numRefL1Override = spsReadUE(oldData, oldSize, readPos);
                spsWriteUE(newData, newSize, writePos, numRefL1Override);
            }
        }

        // 11. ref_pic_list_modification
        // For P/SP slices: ref_pic_list_modification_flag_l0
        uint32_t rplmFlag0 = spsReadBits(oldData, oldSize, readPos, 1);
        spsWriteBits(newData, newSize, writePos, rplmFlag0, 1);
        if (rplmFlag0) {
            uint32_t idc;
            do {
                idc = spsReadUE(oldData, oldSize, readPos);
                spsWriteUE(newData, newSize, writePos, idc);
                if (idc == 0 || idc == 1) {
                    uint32_t v = spsReadUE(oldData, oldSize, readPos);
                    spsWriteUE(newData, newSize, writePos, v);
                } else if (idc == 2) {
                    uint32_t v = spsReadUE(oldData, oldSize, readPos);
                    spsWriteUE(newData, newSize, writePos, v);
                }
            } while (idc != 3);
        }
        if (sliceTypeMod == 1) {
            // B-slice: ref_pic_list_modification_flag_l1
            uint32_t rplmFlag1 = spsReadBits(oldData, oldSize, readPos, 1);
            spsWriteBits(newData, newSize, writePos, rplmFlag1, 1);
            if (rplmFlag1) {
                uint32_t idc;
                do {
                    idc = spsReadUE(oldData, oldSize, readPos);
                    spsWriteUE(newData, newSize, writePos, idc);
                    if (idc == 0 || idc == 1) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    } else if (idc == 2) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    }
                } while (idc != 3);
            }
        }

        // 12. pred_weight_table (if weighted pred for P, or explicit bipred for B)
        bool needWeightTable = (encPps.weightedPredFlag && (sliceTypeMod == 0 || sliceTypeMod == 3))
                            || (encPps.weightedBipredIdc == 1 && sliceTypeMod == 1);
        if (needWeightTable) {
            uint32_t lumaLog2WeightDenom = spsReadUE(oldData, oldSize, readPos);
            spsWriteUE(newData, newSize, writePos, lumaLog2WeightDenom);
            // ChromaArrayType=1 for 4:2:0 (standard DVB/x264)
            uint32_t chromaLog2WeightDenom = spsReadUE(oldData, oldSize, readPos);
            spsWriteUE(newData, newSize, writePos, chromaLog2WeightDenom);

            // L0 weights
            for (int i = 0; i <= numRefL0; i++) {
                uint32_t lumaFlag = spsReadBits(oldData, oldSize, readPos, 1);
                spsWriteBits(newData, newSize, writePos, lumaFlag, 1);
                if (lumaFlag) {
                    int32_t w = spsReadSE(oldData, oldSize, readPos);
                    int32_t o = spsReadSE(oldData, oldSize, readPos);
                    spsWriteSE(newData, newSize, writePos, w);
                    spsWriteSE(newData, newSize, writePos, o);
                }
                uint32_t chromaFlag = spsReadBits(oldData, oldSize, readPos, 1);
                spsWriteBits(newData, newSize, writePos, chromaFlag, 1);
                if (chromaFlag) {
                    for (int j = 0; j < 2; j++) {
                        int32_t w = spsReadSE(oldData, oldSize, readPos);
                        int32_t o = spsReadSE(oldData, oldSize, readPos);
                        spsWriteSE(newData, newSize, writePos, w);
                        spsWriteSE(newData, newSize, writePos, o);
                    }
                }
            }
            // L1 weights for B-slices omitted (bf=0, no B-slices)
        }
    }

    // 13. dec_ref_pic_marking — copy (with IDR→non-IDR conversion if needed)
    if (nalRefIdc != 0) {
        if (origNalType == 5 && demoteIdr) {
            // Demoted IDR: READ IDR format (2 bits), WRITE non-IDR format
            spsReadBits(oldData, oldSize, readPos, 1);  // no_output_of_prior_pics
            spsReadBits(oldData, oldSize, readPos, 1);  // long_term_reference
            // Write adaptive_ref_pic_marking_mode_flag = 0 (sliding window)
            spsWriteBits(newData, newSize, writePos, 0, 1);
        } else if (origNalType == 5) {
            // First IDR (frameIndex=0): keep IDR format
            uint32_t noOutput = spsReadBits(oldData, oldSize, readPos, 1);
            uint32_t longTerm = spsReadBits(oldData, oldSize, readPos, 1);
            spsWriteBits(newData, newSize, writePos, noOutput, 1);
            spsWriteBits(newData, newSize, writePos, longTerm, 1);
        } else {
            // Non-IDR: adaptive_ref_pic_marking_mode_flag + possible MMCO
            uint32_t modeFlag = spsReadBits(oldData, oldSize, readPos, 1);
            spsWriteBits(newData, newSize, writePos, modeFlag, 1);
            if (modeFlag) {
                uint32_t mmco;
                do {
                    mmco = spsReadUE(oldData, oldSize, readPos);
                    spsWriteUE(newData, newSize, writePos, mmco);
                    if (mmco == 1 || mmco == 3) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    }
                    if (mmco == 2) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    }
                    if (mmco == 3 || mmco == 6) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    }
                    if (mmco == 4) {
                        uint32_t v = spsReadUE(oldData, oldSize, readPos);
                        spsWriteUE(newData, newSize, writePos, v);
                    }
                } while (mmco != 0);
            }
        }
    }

    // 14. cabac_init_idc (for P/B slices with CABAC) — copy
    if (encPps.entropyCodingModeFlag && sliceTypeMod != 2 && sliceTypeMod != 4) {
        uint32_t cabacInitIdc = spsReadUE(oldData, oldSize, readPos);
        spsWriteUE(newData, newSize, writePos, cabacInitIdc);
    }

    // 15. slice_qp_delta (SE) — copy
    int32_t sliceQpDelta = spsReadSE(oldData, oldSize, readPos);
    spsWriteSE(newData, newSize, writePos, sliceQpDelta);

    // 16. Deblocking filter params (if PPS flag) — copy
    if (encPps.deblockingFilterControlPresent) {
        uint32_t ddfIdc = spsReadUE(oldData, oldSize, readPos);
        spsWriteUE(newData, newSize, writePos, ddfIdc);
        if (ddfIdc != 1) {
            int32_t alphaOff = spsReadSE(oldData, oldSize, readPos);
            int32_t betaOff = spsReadSE(oldData, oldSize, readPos);
            spsWriteSE(newData, newSize, writePos, alphaOff);
            spsWriteSE(newData, newSize, writePos, betaOff);
        }
    }

    // 17. CABAC alignment + slice data
    if (encPps.entropyCodingModeFlag) {
        // Old: skip cabac_alignment_one_bit + padding to byte boundary
        spsReadBits(oldData, oldSize, readPos, 1);  // cabac_alignment_one_bit (=1)
        int oldPad = (8 - (readPos % 8)) % 8;
        if (oldPad > 0) spsReadBits(oldData, oldSize, readPos, oldPad);

        // New: write cabac_alignment_one_bit + padding to byte boundary
        // All alignment bits must be 1 per H.264 spec (cabac_alignment_one_bit = f(1) = 1)
        spsWriteBits(newData, newSize, writePos, 1, 1);
        int newPad = (8 - (writePos % 8)) % 8;
        if (newPad > 0) spsWriteBits(newData, newSize, writePos, (1 << newPad) - 1, newPad);

        // Copy CABAC data bytes (byte-aligned in both streams)
        int oldCabacByte = readPos / 8;
        int newCabacByte = writePos / 8;
        int cabacLen = oldSize - oldCabacByte;
        if (cabacLen > 0) {
            int needed = newCabacByte + cabacLen;
            if (needed > newSize) {
                newRbsp.resize(needed);
                newData = reinterpret_cast<uint8_t*>(newRbsp.data());
                newSize = newRbsp.size();
            }
            memcpy(newData + newCabacByte, oldData + oldCabacByte, cabacLen);
            writePos = (newCabacByte + cabacLen) * 8;
        }
    } else {
        // CAVLC: copy remaining bits
        while (readPos < oldSize * 8) {
            uint32_t bit = spsReadBits(oldData, oldSize, readPos, 1);
            if (writePos / 8 >= newSize) {
                newRbsp.resize(newSize + 64);
                newData = reinterpret_cast<uint8_t*>(newRbsp.data());
                newSize = newRbsp.size();
            }
            spsWriteBits(newData, newSize, writePos, bit, 1);
        }
    }

    // Trim to actual size
    newRbsp.resize((writePos + 7) / 8);

    // Re-add emulation prevention bytes
    return addEmulationPrevention(newRbsp);
}

// ----------------------------------------------------------------------------
// Extract the first PPS NAL (with start code) from an encoder packet.
// Returns the complete PPS NAL including start code, or empty if not found.
// ----------------------------------------------------------------------------
static QByteArray extractPpsFromPacket(const QByteArray& packetData)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());
    int size = packetData.size();
    int pos = 0;

    while (pos + 4 < size) {
        int scStart = -1, scLen = 0;
        for (int i = pos; i + 2 < size; i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < size && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; scLen = 4; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; scLen = 3; break;
                }
            }
        }
        if (scStart < 0) break;

        int nalStart = scStart + scLen;
        int nalEnd = size;
        for (int i = nalStart + 1; i + 2 < size; i++) {
            if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                nalEnd = i; break;
            }
        }

        if (nalStart < size) {
            int nalType = data[nalStart] & 0x1F;
            if (nalType == 8) {  // PPS
                return packetData.mid(scStart, nalEnd - scStart);
            }
        }
        pos = nalEnd;
    }
    return QByteArray();
}

// ----------------------------------------------------------------------------
// Patch pps_id in a PPS NAL via RBSP reconstruction.
// Input: PPS NAL WITH start code. Returns patched PPS NAL WITH start code.
// ----------------------------------------------------------------------------
static QByteArray patchPpsId(const QByteArray& ppsNal, uint32_t newPpsId)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ppsNal.constData());
    int startCodeLen = 0;
    if (ppsNal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
        startCodeLen = 4;
    else if (ppsNal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
        startCodeLen = 3;
    else
        return QByteArray();

    QByteArray startCode = ppsNal.left(startCodeLen);
    QByteArray nalBody = ppsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 8)
        return QByteArray();

    QByteArray oldRbsp = removeEmulationPrevention(nalBody);
    const uint8_t* oldData = reinterpret_cast<const uint8_t*>(oldRbsp.constData());
    int oldSize = oldRbsp.size();

    QByteArray newRbsp(oldSize + 16, '\0');
    uint8_t* newData = reinterpret_cast<uint8_t*>(newRbsp.data());
    int newSize = newRbsp.size();

    int readPos = 0, writePos = 0;

    // NAL header (8 bits) — copy
    spsWriteBits(newData, newSize, writePos,
                 spsReadBits(oldData, oldSize, readPos, 8), 8);

    // old pps_id — skip; write new
    spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, newPpsId);

    // Copy all remaining bits
    while (readPos < oldSize * 8) {
        uint32_t bit = spsReadBits(oldData, oldSize, readPos, 1);
        if (writePos / 8 >= newSize) {
            newRbsp.resize(newSize + 32);
            newData = reinterpret_cast<uint8_t*>(newRbsp.data());
            newSize = newRbsp.size();
        }
        spsWriteBits(newData, newSize, writePos, bit, 1);
    }

    newRbsp.resize((writePos + 7) / 8);
    QByteArray result = startCode;
    result.append(addEmulationPrevention(newRbsp));
    return result;
}

// ----------------------------------------------------------------------------
// Rewrite all NALs in an encoder packet for source SPS compatibility.
// Strips SPS/PPS/SEI/AUD NALs, rewrites slice NALs.
// Returns rewritten packet, or original on error.
// ----------------------------------------------------------------------------
static QByteArray rewriteEncoderPacketForSourceSps(
    const QByteArray& packetData,
    int encLog2MaxFN, int encLog2MaxPocLsb, bool encFrameMbsOnly,
    int srcLog2MaxFN, int srcLog2MaxPocLsb, bool srcFrameMbsOnly,
    const H264PpsInfo& encPps, uint32_t newPpsId, int frameIndex)
{
    QByteArray result;
    result.reserve(packetData.size() + 128);
    bool modified = false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(packetData.constData());
    int pos = 0;

    while (pos < packetData.size()) {
        int scStart = -1, scLen = 0;
        for (int i = pos; i + 2 < packetData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < packetData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; scLen = 4; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; scLen = 3; break;
                }
            }
        }
        if (scStart < 0) {
            result.append(packetData.mid(pos));
            break;
        }

        if (scStart > pos)
            result.append(packetData.mid(pos, scStart - pos));

        int nalStart = scStart + scLen;
        int nalEnd = packetData.size();
        for (int i = nalStart + 1; i + 2 < packetData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                nalEnd = i; break;
            }
        }

        if (nalStart < packetData.size()) {
            int nalType = data[nalStart] & 0x1F;

            if (nalType == 7 || nalType == 6 || nalType == 9) {
                // Strip SPS(7), SEI(6), AUD(9) — but KEEP PPS(8)!
                // PPS must stay in the packet so the MKV muxer includes it
                // in the same block as the encoder slices. This ensures the
                // decoder finds PPS(id=1) during both sequential and seek playback.
                modified = true;
            } else if (nalType == 8) {
                // PPS — patch pps_id from 0→newPpsId and keep in packet
                QByteArray ppsNal = packetData.mid(scStart, nalEnd - scStart);
                QByteArray patched = patchPpsId(ppsNal, newPpsId);
                if (!patched.isEmpty()) {
                    result.append(patched);
                } else {
                    result.append(ppsNal);  // keep original if patch fails
                }
                modified = true;
            } else if (nalType == 1 || nalType == 5) {
                // Slice — rewrite
                QByteArray nalBody = packetData.mid(nalStart, nalEnd - nalStart);
                QByteArray rewritten = rewriteEncoderSliceForSourceSps(
                    nalBody, encLog2MaxFN, encLog2MaxPocLsb, encFrameMbsOnly,
                    srcLog2MaxFN, srcLog2MaxPocLsb, srcFrameMbsOnly,
                    encPps, newPpsId, frameIndex);
                if (!rewritten.isEmpty()) {
                    result.append(packetData.mid(scStart, scLen));  // start code
                    result.append(rewritten);
                    modified = true;
                } else {
                    // Rewrite failed — keep original NAL
                    result.append(packetData.mid(scStart, nalEnd - scStart));
                }
            } else {
                // Other NAL types — copy as-is
                result.append(packetData.mid(scStart, nalEnd - scStart));
            }
        }
        pos = nalEnd;
    }

    return modified ? result : packetData;
}

// ----------------------------------------------------------------------------
// Parse H.264 PPS for fields needed by IDR conversion.
// Input: raw PPS NAL data WITH start code prefix.
// ----------------------------------------------------------------------------
static H264PpsInfo parseH264PpsInfo(const QByteArray& ppsNal)
{
    H264PpsInfo info = { true, false, true, false, false, 0, 0, 0, false };  // safe defaults

    // Find and strip start code
    int startCodeLen = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ppsNal.constData());
    if (ppsNal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
        startCodeLen = 4;
    else if (ppsNal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
        startCodeLen = 3;
    else
        return info;

    QByteArray nalBody = ppsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 8)
        return info;  // not PPS

    QByteArray rbsp = removeEmulationPrevention(nalBody);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(rbsp.constData());
    int dataSize = rbsp.size();
    int bitPos = 8;  // skip NAL header

    spsReadUE(data, dataSize, bitPos);   // pps_id
    spsReadUE(data, dataSize, bitPos);   // sps_id
    info.entropyCodingModeFlag = (spsReadBits(data, dataSize, bitPos, 1) != 0);
    info.bottomFieldPicOrderPresent = (spsReadBits(data, dataSize, bitPos, 1) != 0);
    uint32_t numSliceGroupsMinus1 = spsReadUE(data, dataSize, bitPos);
    if (numSliceGroupsMinus1 > 0) {
        // Complex slice group map — bail, use defaults (very rare in DVB)
        qDebug() << "  PPS: num_slice_groups > 1 (" << numSliceGroupsMinus1+1
                 << "), using default PPS flags for IDR conversion";
        info.valid = true;
        return info;
    }
    info.numRefIdxL0DefaultActiveMinus1 = spsReadUE(data, dataSize, bitPos);
    info.numRefIdxL1DefaultActiveMinus1 = spsReadUE(data, dataSize, bitPos);
    info.weightedPredFlag = (spsReadBits(data, dataSize, bitPos, 1) != 0);
    info.weightedBipredIdc = spsReadBits(data, dataSize, bitPos, 2);
    spsReadSE(data, dataSize, bitPos);   // pic_init_qp_minus26
    spsReadSE(data, dataSize, bitPos);   // pic_init_qs_minus26
    spsReadSE(data, dataSize, bitPos);   // chroma_qp_index_offset
    info.deblockingFilterControlPresent = (spsReadBits(data, dataSize, bitPos, 1) != 0);
    spsReadBits(data, dataSize, bitPos, 1);  // constrained_intra_pred_flag
    info.redundantPicCntPresent = (spsReadBits(data, dataSize, bitPos, 1) != 0);
    info.valid = true;

    qDebug() << "  PPS parsed: entropy=" << (info.entropyCodingModeFlag ? "CABAC" : "CAVLC")
             << "deblocking=" << info.deblockingFilterControlPresent
             << "redundant_pic_cnt=" << info.redundantPicCntPresent
             << "weighted_pred=" << info.weightedPredFlag
             << "bottomFieldPicOrder=" << info.bottomFieldPicOrderPresent;
    return info;
}

// ----------------------------------------------------------------------------
// Convert a single Non-IDR I-slice NAL to IDR via slice header reconstruction.
// Input: NAL body (after start code, WITH emulation prevention bytes).
// Returns: modified NAL body with IDR slice header, or empty on error.
//
// Modifications:
// - NAL type 1→5, nal_ref_idc >= 1
// - frame_num set to 0 (IDR requirement)
// - idr_pic_id=0 inserted after field flags
// - dec_ref_pic_marking replaced with IDR syntax (2 bits)
// - CABAC alignment adjusted for new header length
// - Slice data (CABAC/CAVLC) copied byte-exact
// ----------------------------------------------------------------------------
static QByteArray convertSliceNalToIDR(
    const QByteArray& nalBody,
    int log2MaxFrameNum,
    int pocType,
    int log2MaxPocLsb,
    bool frameMbsOnly,
    const H264PpsInfo& pps)
{
    if (nalBody.isEmpty()) return QByteArray();

    uint8_t nalHeader = static_cast<uint8_t>(nalBody[0]);
    uint8_t nalType = nalHeader & 0x1F;
    if (nalType != 1) return QByteArray();  // only convert non-IDR slices

    uint8_t nalRefIdc = (nalHeader >> 5) & 0x03;

    // Remove emulation prevention → RBSP
    QByteArray oldRbsp = removeEmulationPrevention(nalBody);
    const uint8_t* oldData = reinterpret_cast<const uint8_t*>(oldRbsp.constData());
    int oldSize = oldRbsp.size();

    // Allocate new RBSP (generous: old size + room for inserted fields)
    QByteArray newRbsp(oldSize + 32, '\0');
    uint8_t* newData = reinterpret_cast<uint8_t*>(newRbsp.data());
    int newSize = newRbsp.size();

    int readPos = 0;
    int writePos = 0;

    // 1. NAL header: set type=5 (IDR), ensure nal_ref_idc >= 1
    spsReadBits(oldData, oldSize, readPos, 8);  // skip old header
    uint8_t newRefIdc = (nalRefIdc > 0) ? nalRefIdc : 2;
    uint8_t newHeader = (nalHeader & 0x80) | (newRefIdc << 5) | 5;
    spsWriteBits(newData, newSize, writePos, newHeader, 8);

    // 2. first_mb_in_slice (ue) — copy
    uint32_t firstMb = spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, firstMb);

    // 3. slice_type (ue) — copy
    uint32_t sliceType = spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, sliceType);

    // Verify this is an I-slice (type 2 or 7)
    uint32_t sliceTypeMod = sliceType % 5;
    if (sliceTypeMod != 2) {
        qDebug() << "  convertSliceNalToIDR: not an I-slice (type=" << sliceType << "), skipping";
        return QByteArray();
    }

    // 4. pps_id (ue) — copy
    uint32_t ppsId = spsReadUE(oldData, oldSize, readPos);
    spsWriteUE(newData, newSize, writePos, ppsId);

    // 5. frame_num: read old (skip), write 0 (same bit width — IDR requires frame_num=0)
    spsReadBits(oldData, oldSize, readPos, log2MaxFrameNum);
    spsWriteBits(newData, newSize, writePos, 0, log2MaxFrameNum);

    // 6. field_pic_flag + bottom_field_flag (if !frame_mbs_only)
    if (!frameMbsOnly) {
        uint32_t fieldPicFlag = spsReadBits(oldData, oldSize, readPos, 1);
        spsWriteBits(newData, newSize, writePos, fieldPicFlag, 1);
        if (fieldPicFlag) {
            uint32_t bottomFlag = spsReadBits(oldData, oldSize, readPos, 1);
            spsWriteBits(newData, newSize, writePos, bottomFlag, 1);
        }
    }

    // 7. idr_pic_id = 0 — INSERT (not present in original non-IDR slice)
    spsWriteUE(newData, newSize, writePos, 0);

    // 8. pic_order_cnt_lsb (if poc_type == 0)
    if (pocType == 0 && log2MaxPocLsb > 0) {
        uint32_t pocLsb = spsReadBits(oldData, oldSize, readPos, log2MaxPocLsb);
        spsWriteBits(newData, newSize, writePos, pocLsb, log2MaxPocLsb);
        // delta_pic_order_cnt_bottom: only if !field_pic_flag — skipped for PAFF fields
    }
    // poc_type 1: delta_pic_order_cnt would need handling — not used by source stream
    // poc_type 2: no POC fields in slice header

    // 9. redundant_pic_cnt (if PPS flag set — extremely rare in DVB)
    if (pps.redundantPicCntPresent) {
        uint32_t rpc = spsReadUE(oldData, oldSize, readPos);
        spsWriteUE(newData, newSize, writePos, rpc);
    }

    // 10. ref_pic_list_modification — empty for I-slices (nothing in bitstream)
    // 11. pred_weight_table — not present for I-slices

    // 12. dec_ref_pic_marking — SKIP old, WRITE new IDR syntax
    // Old (non-IDR, if nal_ref_idc > 0):
    if (nalRefIdc != 0) {
        uint32_t modeFlag = spsReadBits(oldData, oldSize, readPos, 1);
        if (modeFlag) {
            // Skip MMCO commands
            uint32_t mmco;
            do {
                mmco = spsReadUE(oldData, oldSize, readPos);
                if (mmco == 1 || mmco == 3)
                    spsReadUE(oldData, oldSize, readPos);  // difference_of_pic_nums_minus1
                if (mmco == 2)
                    spsReadUE(oldData, oldSize, readPos);  // long_term_pic_num
                if (mmco == 3 || mmco == 6)
                    spsReadUE(oldData, oldSize, readPos);  // long_term_frame_idx
                if (mmco == 4)
                    spsReadUE(oldData, oldSize, readPos);  // max_long_term_frame_idx_plus1
            } while (mmco != 0);
        }
    }
    // New (IDR): always present, 2 bits
    spsWriteBits(newData, newSize, writePos, 0, 1);  // no_output_of_prior_pics_flag = 0
    spsWriteBits(newData, newSize, writePos, 0, 1);  // long_term_reference_flag = 0

    // 13. slice_qp_delta (se) — copy
    int32_t sliceQpDelta = spsReadSE(oldData, oldSize, readPos);
    spsWriteSE(newData, newSize, writePos, sliceQpDelta);

    // 14. Deblocking filter (if PPS flag set)
    if (pps.deblockingFilterControlPresent) {
        uint32_t ddfIdc = spsReadUE(oldData, oldSize, readPos);
        spsWriteUE(newData, newSize, writePos, ddfIdc);
        if (ddfIdc != 1) {
            int32_t alphaOff = spsReadSE(oldData, oldSize, readPos);
            int32_t betaOff = spsReadSE(oldData, oldSize, readPos);
            spsWriteSE(newData, newSize, writePos, alphaOff);
            spsWriteSE(newData, newSize, writePos, betaOff);
        }
    }

    // 15. CABAC alignment + slice data
    if (pps.entropyCodingModeFlag) {
        // Old: skip cabac_alignment_one_bit + padding zeros to byte boundary
        spsReadBits(oldData, oldSize, readPos, 1);  // cabac_alignment_one_bit (must be 1)
        int oldPad = (8 - (readPos % 8)) % 8;
        if (oldPad > 0) spsReadBits(oldData, oldSize, readPos, oldPad);
        // readPos is now at CABAC byte boundary

        // New: write cabac_alignment_one_bit + padding to byte boundary
        // All alignment bits must be 1 per H.264 spec (cabac_alignment_one_bit = f(1) = 1)
        spsWriteBits(newData, newSize, writePos, 1, 1);
        int newPad = (8 - (writePos % 8)) % 8;
        if (newPad > 0) spsWriteBits(newData, newSize, writePos, (1 << newPad) - 1, newPad);
        // writePos is now at byte boundary

        // Copy CABAC data bytes (byte-aligned in both streams)
        int oldCabacByte = readPos / 8;
        int newCabacByte = writePos / 8;
        int cabacLen = oldSize - oldCabacByte;
        if (cabacLen > 0) {
            int needed = newCabacByte + cabacLen;
            if (needed > newSize) {
                newRbsp.resize(needed);
                newData = reinterpret_cast<uint8_t*>(newRbsp.data());
                newSize = newRbsp.size();
            }
            memcpy(newData + newCabacByte, oldData + oldCabacByte, cabacLen);
            writePos = (newCabacByte + cabacLen) * 8;
        }
    } else {
        // CAVLC: copy remaining bits one by one (no alignment issues)
        while (readPos < oldSize * 8) {
            uint32_t bit = spsReadBits(oldData, oldSize, readPos, 1);
            if (writePos / 8 >= newSize) {
                newRbsp.resize(newSize + 64);
                newData = reinterpret_cast<uint8_t*>(newRbsp.data());
                newSize = newRbsp.size();
            }
            spsWriteBits(newData, newSize, writePos, bit, 1);
        }
    }

    // Trim to actual size
    newRbsp.resize((writePos + 7) / 8);

    // Re-add emulation prevention bytes
    QByteArray result = addEmulationPrevention(newRbsp);
    qDebug() << "    IDR conversion: NAL" << nalBody.size() << "→" << result.size()
             << "bytes (RBSP" << oldSize << "→" << newRbsp.size() << ")";
    return result;
}

// ----------------------------------------------------------------------------
// Convert all Non-IDR I-slice NALs in an access unit to IDR.
// Iterates over NALs in AU, converts type 1 I-slices via convertSliceNalToIDR.
// Non-slice NALs (SPS, PPS, SEI) are kept unchanged.
// Returns modified AU, or original if no conversion was made.
// ----------------------------------------------------------------------------
static QByteArray convertAUToIDR(
    const QByteArray& auData,
    int log2MaxFrameNum,
    int pocType,
    int log2MaxPocLsb,
    bool frameMbsOnly,
    const H264PpsInfo& pps)
{
    QByteArray result;
    result.reserve(auData.size() + 64);
    bool converted = false;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(auData.constData());
    int pos = 0;

    while (pos < auData.size()) {
        // Find next start code
        int scStart = -1;
        for (int i = pos; i + 2 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0) {
                if (i + 3 < auData.size() && data[i+2] == 0 && data[i+3] == 1) {
                    scStart = i; break;
                }
                if (data[i+2] == 1) {
                    scStart = i; break;
                }
            }
        }
        if (scStart < 0) {
            result.append(auData.mid(pos));
            break;
        }

        // Copy data before start code
        if (scStart > pos)
            result.append(auData.mid(pos, scStart - pos));

        int scLen = (data[scStart+2] == 0) ? 4 : 3;

        // Find end of NAL
        int nalEnd = auData.size();
        for (int i = scStart + scLen + 1; i + 2 < auData.size(); i++) {
            if (data[i] == 0 && data[i+1] == 0 && (data[i+2] == 0 || data[i+2] == 1)) {
                nalEnd = i; break;
            }
        }

        QByteArray nalBody = auData.mid(scStart + scLen, nalEnd - scStart - scLen);
        if (!nalBody.isEmpty()) {
            uint8_t nalType = static_cast<uint8_t>(nalBody[0]) & 0x1F;

            if (nalType == 1) {
                // Try full IDR conversion (works for I-slices)
                QByteArray idrBody = convertSliceNalToIDR(
                    nalBody, log2MaxFrameNum, pocType, log2MaxPocLsb,
                    frameMbsOnly, pps);

                if (!idrBody.isEmpty()) {
                    result.append(auData.mid(scStart, scLen));  // start code
                    result.append(idrBody);
                    converted = true;
                    pos = nalEnd;
                    continue;
                }

                // IDR conversion failed (e.g., P-slice bottom field).
                // Still need to patch frame_num=0 so both fields match the IDR.
                QByteArray rbsp = removeEmulationPrevention(nalBody);
                if (!rbsp.isEmpty()) {
                    writeFrameNumInSlice(
                        reinterpret_cast<uint8_t*>(rbsp.data()),
                        rbsp.size(), log2MaxFrameNum, 0);
                    QByteArray patchedNal = addEmulationPrevention(rbsp);
                    result.append(auData.mid(scStart, scLen));  // start code
                    result.append(patchedNal);
                    qDebug() << "    IDR AU: patched companion field frame_num→0"
                             << "(NAL type 1," << patchedNal.size() << "bytes)";
                    converted = true;
                    pos = nalEnd;
                    continue;
                }
            }
        }

        // Not converted — copy as-is
        result.append(auData.mid(scStart, nalEnd - scStart));
        pos = nalEnd;
    }

    if (converted) {
        qDebug() << "    convertAUToIDR: AU" << auData.size() << "→" << result.size() << "bytes";
    }
    return converted ? result : auData;
}

// Skip H.264 scaling list in SPS
static void skipScalingList(const uint8_t* data, int dataSize, int& bitPos, int sizeOfScalingList)
{
    int nextScale = 8;
    for (int j = 0; j < sizeOfScalingList; j++) {
        if (nextScale != 0) {
            int32_t delta = spsReadSE(data, dataSize, bitPos);
            nextScale = (nextScale + delta + 256) % 256;
        }
    }
}

// Skip H.264 HRD parameters in VUI
static void skipHrdParameters(const uint8_t* data, int dataSize, int& bitPos)
{
    uint32_t cpb_cnt_minus1 = spsReadUE(data, dataSize, bitPos);
    spsReadBits(data, dataSize, bitPos, 4);  // bit_rate_scale
    spsReadBits(data, dataSize, bitPos, 4);  // cpb_size_scale
    for (uint32_t i = 0; i <= cpb_cnt_minus1; i++) {
        spsReadUE(data, dataSize, bitPos);   // bit_rate_value_minus1
        spsReadUE(data, dataSize, bitPos);   // cpb_size_value_minus1
        spsReadBits(data, dataSize, bitPos, 1); // cbr_flag
    }
    spsReadBits(data, dataSize, bitPos, 5);  // initial_cpb_removal_delay_length_minus1
    spsReadBits(data, dataSize, bitPos, 5);  // cpb_removal_delay_length_minus1
    spsReadBits(data, dataSize, bitPos, 5);  // dpb_output_delay_length_minus1
    spsReadBits(data, dataSize, bitPos, 5);  // time_offset_length
}

// Patch H.264 SPS NAL to set bitstream_restriction with max_num_reorder_frames.
// Input: SPS NAL data WITH start code prefix.
// Returns patched SPS NAL data WITH start code prefix, or empty on error.
static QByteArray patchH264SpsReorderFrames(const QByteArray& spsNal, int maxReorderFrames)
{
    // Find and strip start code
    int startCodeLen = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(spsNal.constData());
    if (spsNal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
        startCodeLen = 4;
    else if (spsNal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
        startCodeLen = 3;
    else
        return QByteArray();  // no start code

    QByteArray nalBody = spsNal.mid(startCodeLen);

    // Verify NAL type = 7 (SPS)
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 7)
        return QByteArray();

    // Remove emulation prevention bytes to get RBSP
    QByteArray rbsp = removeEmulationPrevention(nalBody);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(rbsp.constData());
    int dataSize = rbsp.size();
    int bitPos = 0;

    // Parse NAL header (8 bits)
    spsReadBits(data, dataSize, bitPos, 8);  // forbidden_zero_bit + nal_ref_idc + nal_unit_type

    // Parse SPS fields
    uint32_t profile_idc = spsReadBits(data, dataSize, bitPos, 8);
    spsReadBits(data, dataSize, bitPos, 8);   // constraint flags + reserved
    spsReadBits(data, dataSize, bitPos, 8);   // level_idc
    spsReadUE(data, dataSize, bitPos);        // seq_parameter_set_id

    // High profile extensions
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        uint32_t chroma_format_idc = spsReadUE(data, dataSize, bitPos);
        if (chroma_format_idc == 3)
            spsReadBits(data, dataSize, bitPos, 1);  // separate_colour_plane_flag
        spsReadUE(data, dataSize, bitPos);    // bit_depth_luma_minus8
        spsReadUE(data, dataSize, bitPos);    // bit_depth_chroma_minus8
        spsReadBits(data, dataSize, bitPos, 1); // qpprime_y_zero_transform_bypass_flag

        uint32_t seq_scaling_matrix_present = spsReadBits(data, dataSize, bitPos, 1);
        if (seq_scaling_matrix_present) {
            int limit = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < limit; i++) {
                uint32_t present = spsReadBits(data, dataSize, bitPos, 1);
                if (present)
                    skipScalingList(data, dataSize, bitPos, (i < 6) ? 16 : 64);
            }
        }
    }

    spsReadUE(data, dataSize, bitPos);  // log2_max_frame_num_minus4
    uint32_t poc_type = spsReadUE(data, dataSize, bitPos);
    if (poc_type == 0) {
        spsReadUE(data, dataSize, bitPos);  // log2_max_pic_order_cnt_lsb_minus4
    } else if (poc_type == 1) {
        spsReadBits(data, dataSize, bitPos, 1);  // delta_pic_order_always_zero_flag
        spsReadSE(data, dataSize, bitPos);       // offset_for_non_ref_pic
        spsReadSE(data, dataSize, bitPos);       // offset_for_top_to_bottom_field
        uint32_t num_ref = spsReadUE(data, dataSize, bitPos);
        for (uint32_t i = 0; i < num_ref; i++)
            spsReadSE(data, dataSize, bitPos);   // offset_for_ref_frame
    }

    int maxRefReadPos = bitPos;  // bit position of max_num_ref_frames in RBSP
    uint32_t max_num_ref_frames = spsReadUE(data, dataSize, bitPos);
    // Increase for PAFF transition: prevent DPB overflow from stale MMCO references
    uint32_t patched_max_ref = qMax(8u, max_num_ref_frames);
    spsReadBits(data, dataSize, bitPos, 1);  // gaps_in_frame_num_allowed_flag
    spsReadUE(data, dataSize, bitPos);       // pic_width_in_mbs_minus1
    spsReadUE(data, dataSize, bitPos);       // pic_height_in_map_units_minus1

    uint32_t frame_mbs_only = spsReadBits(data, dataSize, bitPos, 1);
    int mbAdaptiveBitPos = -1;  // bit position of mb_adaptive_frame_field_flag in RBSP
    if (!frame_mbs_only) {
        mbAdaptiveBitPos = bitPos;  // record position before reading
        spsReadBits(data, dataSize, bitPos, 1);  // mb_adaptive_frame_field_flag
    }

    spsReadBits(data, dataSize, bitPos, 1);  // direct_8x8_inference_flag

    uint32_t frame_cropping = spsReadBits(data, dataSize, bitPos, 1);
    if (frame_cropping) {
        spsReadUE(data, dataSize, bitPos);  // crop_left
        spsReadUE(data, dataSize, bitPos);  // crop_right
        spsReadUE(data, dataSize, bitPos);  // crop_top
        spsReadUE(data, dataSize, bitPos);  // crop_bottom
    }

    uint32_t vui_present = spsReadBits(data, dataSize, bitPos, 1);
    if (!vui_present) {
        qDebug() << "  SPS patch: no VUI, cannot add bitstream_restriction";
        return QByteArray();
    }

    // Parse VUI parameters to find bitstream_restriction_flag
    uint32_t aspect_ratio_present = spsReadBits(data, dataSize, bitPos, 1);
    if (aspect_ratio_present) {
        uint32_t aspect_ratio_idc = spsReadBits(data, dataSize, bitPos, 8);
        if (aspect_ratio_idc == 255) {  // Extended_SAR
            spsReadBits(data, dataSize, bitPos, 16);  // sar_width
            spsReadBits(data, dataSize, bitPos, 16);  // sar_height
        }
    }

    uint32_t overscan_present = spsReadBits(data, dataSize, bitPos, 1);
    if (overscan_present)
        spsReadBits(data, dataSize, bitPos, 1);  // overscan_appropriate_flag

    uint32_t video_signal_present = spsReadBits(data, dataSize, bitPos, 1);
    if (video_signal_present) {
        spsReadBits(data, dataSize, bitPos, 3);  // video_format
        spsReadBits(data, dataSize, bitPos, 1);  // video_full_range_flag
        uint32_t colour_desc = spsReadBits(data, dataSize, bitPos, 1);
        if (colour_desc) {
            spsReadBits(data, dataSize, bitPos, 8);  // colour_primaries
            spsReadBits(data, dataSize, bitPos, 8);  // transfer_characteristics
            spsReadBits(data, dataSize, bitPos, 8);  // matrix_coefficients
        }
    }

    uint32_t chroma_loc_present = spsReadBits(data, dataSize, bitPos, 1);
    if (chroma_loc_present) {
        spsReadUE(data, dataSize, bitPos);  // chroma_sample_loc_type_top_field
        spsReadUE(data, dataSize, bitPos);  // chroma_sample_loc_type_bottom_field
    }

    uint32_t timing_present = spsReadBits(data, dataSize, bitPos, 1);
    if (timing_present) {
        spsReadBits(data, dataSize, bitPos, 32);  // num_units_in_tick
        spsReadBits(data, dataSize, bitPos, 32);  // time_scale
        spsReadBits(data, dataSize, bitPos, 1);   // fixed_frame_rate_flag
    }

    uint32_t nal_hrd_present = spsReadBits(data, dataSize, bitPos, 1);
    if (nal_hrd_present) skipHrdParameters(data, dataSize, bitPos);

    uint32_t vcl_hrd_present = spsReadBits(data, dataSize, bitPos, 1);
    if (vcl_hrd_present) skipHrdParameters(data, dataSize, bitPos);

    if (nal_hrd_present || vcl_hrd_present)
        spsReadBits(data, dataSize, bitPos, 1);  // low_delay_hrd_flag

    spsReadBits(data, dataSize, bitPos, 1);  // pic_struct_present_flag

    // Now at bitstream_restriction_flag position
    int bsrFlagPos = bitPos;

    // Build new RBSP: copy up to max_num_ref_frames, write patched value,
    // then copy from after max_num_ref_frames to bsrFlagPos, then write new
    // bitstream_restriction section.
    int bytesNeeded = (bsrFlagPos + 7) / 8 + 32;  // generous buffer
    QByteArray newRbsp(bytesNeeded, '\0');
    uint8_t* writeData = reinterpret_cast<uint8_t*>(newRbsp.data());
    int writeDataSize = newRbsp.size();

    // Copy bits [0..maxRefReadPos) verbatim
    int writeBitPos = 0;
    for (int b = 0; b < maxRefReadPos; b++) {
        uint32_t bit = (data[b/8] >> (7 - (b%8))) & 1;
        spsWriteBits(writeData, writeDataSize, writeBitPos, bit, 1);
    }

    // Write patched max_num_ref_frames (may be different bit-length than original)
    spsWriteUE(writeData, writeDataSize, writeBitPos, patched_max_ref);

    // Skip the original max_num_ref_frames UE value — readPos after it was stored
    // We need the bit position right after the original max_num_ref_frames.
    // Re-read it to find the end position.
    int tempPos = maxRefReadPos;
    spsReadUE(data, dataSize, tempPos);  // skip original value
    int afterMaxRefPos = tempPos;

    // Copy bits [afterMaxRefPos..bsrFlagPos) verbatim (gaps_flag, dimensions, VUI etc.)
    for (int b = afterMaxRefPos; b < bsrFlagPos; b++) {
        uint32_t bit = (data[b/8] >> (7 - (b%8))) & 1;
        spsWriteBits(writeData, writeDataSize, writeBitPos, bit, 1);
    }

    // PAFF→MBAFF: set mb_adaptive_frame_field_flag=1 in the NEW RBSP
    if (mbAdaptiveBitPos >= 0) {
        // Calculate the new position: offset by the UE size difference
        int sizeOrigUE = afterMaxRefPos - maxRefReadPos;
        // Count bits of patched_max_ref UE
        int sizePatchedUE = 1;
        { uint32_t tmp = patched_max_ref + 1; while (tmp > 1) { tmp >>= 1; sizePatchedUE += 2; } }
        int newMbAdaptivePos = mbAdaptiveBitPos + (sizePatchedUE - sizeOrigUE);
        int byteIdx = newMbAdaptivePos / 8;
        int bitIdx = 7 - (newMbAdaptivePos % 8);
        if (byteIdx < writeDataSize && !(writeData[byteIdx] & (1 << bitIdx))) {
            writeData[byteIdx] |= (1 << bitIdx);
            qDebug() << "  SPS patched: mb_adaptive_frame_field_flag 0->1 (PAFF->MBAFF signaling)";
        }
    }
    // Refresh pointers (newRbsp may have been resized)
    writeData = reinterpret_cast<uint8_t*>(newRbsp.data());
    writeDataSize = newRbsp.size();

    // Write bitstream_restriction_flag = 1
    spsWriteBits(writeData, writeDataSize, writeBitPos, 1, 1);

    // Write bitstream_restriction fields
    spsWriteBits(writeData, writeDataSize, writeBitPos, 1, 1);  // motion_vectors_over_pic_boundaries_flag
    spsWriteUE(writeData, writeDataSize, writeBitPos, 0);        // max_bytes_per_pic_denom
    spsWriteUE(writeData, writeDataSize, writeBitPos, 0);        // max_bits_per_mb_denom
    spsWriteUE(writeData, writeDataSize, writeBitPos, 16);       // log2_max_mv_length_horizontal
    spsWriteUE(writeData, writeDataSize, writeBitPos, 16);       // log2_max_mv_length_vertical
    spsWriteUE(writeData, writeDataSize, writeBitPos, maxReorderFrames);  // max_num_reorder_frames
    // max_dec_frame_buffering: increase to at least 8 to prevent DPB overflow at
    // MBAFF re-encode → PAFF stream-copy transitions where MMCO references
    // non-existent frames (harmless unref failures, but DPB must not overflow)
    uint32_t maxDecBuf = qMax(8u, qMax((uint32_t)maxReorderFrames, max_num_ref_frames));
    spsWriteUE(writeData, writeDataSize, writeBitPos, maxDecBuf);  // max_dec_frame_buffering

    // RBSP stop bit + byte alignment
    spsWriteBits(writeData, writeDataSize, writeBitPos, 1, 1);  // rbsp_stop_one_bit
    int padding = (8 - (writeBitPos % 8)) % 8;
    if (padding > 0)
        spsWriteBits(writeData, writeDataSize, writeBitPos, 0, padding);

    // Trim to actual size
    newRbsp.resize(writeBitPos / 8);

    // Re-add emulation prevention bytes
    QByteArray patchedNal = addEmulationPrevention(newRbsp);

    // Re-add start code
    QByteArray result;
    result.append(spsNal.constData(), startCodeLen);
    result.append(patchedNal);

    qDebug() << "  SPS patched: bitstream_restriction_flag=1, max_num_reorder_frames="
             << maxReorderFrames << "max_dec_frame_buffering=" << maxDecBuf
             << "(original" << rbsp.size() << "bytes, patched" << newRbsp.size() << "bytes)";

    return result;
}

// ----------------------------------------------------------------------------
// Write parameter sets (SPS/PPS/VPS)
// If patchReorderFrames > 0, patches H.264 SPS to signal max_num_reorder_frames
// to prevent backward timestamps at re-encode/stream-copy boundaries.
// ----------------------------------------------------------------------------
bool TTESSmartCut::writeParameterSets(QFile& outFile, int patchReorderFrames)
{
    // For H.265, write VPS first
    if (mParser.codecType() == NALU_CODEC_H265) {
        for (int i = 0; i < mParser.vpsCount(); ++i) {
            QByteArray vps = mParser.getVPS(i);
            if (!vps.isEmpty()) {
                if (outFile.write(vps) != vps.size()) {
                    setError("Failed to write VPS");
                    return false;
                }
            }
        }
    }

    // Write SPS (patched for H.264 if requested)
    for (int i = 0; i < mParser.spsCount(); ++i) {
        QByteArray sps = mParser.getSPS(i);
        if (!sps.isEmpty()) {
            if (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264) {
                QByteArray patched = patchH264SpsReorderFrames(sps, patchReorderFrames);
                if (!patched.isEmpty())
                    sps = patched;
                else
                    qDebug() << "  WARNING: SPS patch failed, using original SPS";
            }
            if (outFile.write(sps) != sps.size()) {
                setError("Failed to write SPS");
                return false;
            }
        }
    }

    // Write PPS
    for (int i = 0; i < mParser.ppsCount(); ++i) {
        QByteArray pps = mParser.getPPS(i);
        if (!pps.isEmpty()) {
            if (outFile.write(pps) != pps.size()) {
                setError("Failed to write PPS");
                return false;
            }
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTESSmartCut::setError(const QString& error)
{
    mLastError = error;
    qDebug() << "TTESSmartCut error:" << error;
}
