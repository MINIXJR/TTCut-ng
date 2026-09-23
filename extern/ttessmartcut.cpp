/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttessmartcut.h"
#include "tth264bitstream.h"
#include "../avstream/ttesinfo.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttencodernames.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttcalibrationstore.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QScopeGuard>

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

// Helper: libav error code to QString (mirrors avErrStr in ttmkvmergeprovider).
// Used by every error-path qDebug / setError site in this file.
static QString avErrStr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// End-of-stream / end-of-bitstream NAL units written between Smart Cut
// segments to flush the decoder DPB. Defined once at file scope so a
// future spec change (or a new codec) can be applied in a single place.
//   H.264: NAL type 11 (end_of_stream_rbsp), per ITU-T H.264 §7.3.2.5.
//   H.265: NAL type 37 (EOB_NUT, end_of_bitstream_rbsp), per ITU-T H.265 §7.3.2.6.
static constexpr char kEosNalH264[] = { 0x00, 0x00, 0x00, 0x01, 0x0B };
static constexpr char kEosNalH265[] = { 0x00, 0x00, 0x00, 0x01, 0x4A, 0x01 };

// libx264 with bf=0 emits SPS with log2_max_pic_order_cnt_lsb_minus4 = 0
// (log2_max_pic_order_cnt_lsb = 4). Fallback only: probeEncoderPocParams()
// measures the real value up front (throwaway libx264 open with
// GLOBAL_HEADER); this constant is used when the probe fails or reports an
// unexpected poc_type. parseEncoderSpsFromPacket cross-checks the real
// per-segment SPS afterwards. Per H.264 7.4.2.1.1 log2_max_poc_lsb >= 4, so
// the assumption could only ever be conservative (never call a real seam
// bridgeable when it is not).
static constexpr int kExpectedEncoderLog2PocLsb = 4;

// True when some poc_lsb value representable in patchLog2PocLsb bits keeps
// the decoder's PicOrderCntMsb continuous into srcPocLsb — the same linear
// distance rule (|src - v| <= srcMax/2) that applyPocDomainFix's post-patch
// search uses. When false, no patch value exists ("no safe poc_lsb"): the
// first stream-copied GOP would be discarded as out-of-order after the EOS,
// so the caller must widen the encoder POC domain via SPS unification.
static bool pocDomainBridgeable(int srcPocLsb, int patchLog2PocLsb, int srcLog2PocLsb)
{
    if (srcPocLsb < 0 || patchLog2PocLsb <= 0 || srcLog2PocLsb <= 0)
        return true;  // cannot judge — applyPocDomainFix's warning is the backstop
    int patchMax = 1 << patchLog2PocLsb;
    int srcHalf  = (1 << srcLog2PocLsb) / 2;
    return srcPocLsb <= srcHalf + patchMax - 1;
}

// ----------------------------------------------------------------------------
// ReencodeContext: per-call state for reencodeFrames
// ----------------------------------------------------------------------------
struct TTESSmartCut::ReencodeContext {
    ReencodeContext(QFile& f, int sf, int ef, int scsf, int* assc, int* asau, int sd,
                    int ed = -1, bool tm = false)
        : outFile(f), startFrame(sf), endFrame(ef), streamCopyStartFrame(scsf),
          startDisplay(sd), adjustedStreamCopyStart(assc), actualStartAU(asau),
          endDisplay(ed), tailMode(tm) {}

    // ---- Inputs (set by reencodeFrames before calling helpers) ----
    QFile& outFile;
    int    startFrame;
    int    endFrame;
    int    streamCopyStartFrame;
    int    startDisplay = -1;     // UI cut-in display position (Direction A anchor)

    // ---- Outputs (raw pointers from caller; may be nullptr) ----
    int*   adjustedStreamCopyStart;   // -1 = no adjustment
    int*   actualStartAU;             // -1 = no adjustment

    // ---- Decode-range phase ----
    int    decodeStart       = 0;
    int    decodeEnd         = 0;

    // ---- Decode phase ----
    QList<AVFrame*> allDecodedFrames;
    bool   encoderInitialized = false;

    // ---- Selection phase ----
    QList<AVFrame*> framesToEncode;
    int    streamCopyLimit   = 0;

    // ---- Encode phase ----
    // Source AU index per submitted frame, in submission order. Survives
    // runEncodePass (framesToEncode is freed there) so delayed encoder
    // packets (x264 lookahead: most arrive in flushEncoder) can still be
    // mapped to their display position: with bf=0 packets arrive 1:1 in
    // submission order.
    QVector<int> encodeAuOrder;
    bool   encoderSpsParsed  = false;
    bool   encPpsParsed      = false;
    TTH264PpsInfo encPpsForRewrite{ true, false, true, false, false, 0, 0, 0, false };
    bool   firstFrame        = true;
    int    framesSent        = 0;
    int    packetsReceived   = 0;
    QByteArray pendingPacket;

    // ---- Frame-accurate cut-OUT (tail / pure-reencode display upper bound) ----
    int    endDisplay = -1;     // cut-out display position (upper bound for selection)
    bool   tailMode   = false;  // true: tail re-encode (au >= startFrame, display <= endDisplay)

    // ---- RAII cleanup: free any AVFrames still in lists ----
    ~ReencodeContext() {
        for (AVFrame* f : allDecodedFrames) av_frame_free(&f);
        for (AVFrame* f : framesToEncode)   av_frame_free(&f);
    }

    ReencodeContext(const ReencodeContext&) = delete;
    ReencodeContext& operator=(const ReencodeContext&) = delete;
};

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
    , mSpsUnificationPocAnchor(-1)
    , mSpsUnificationPocBase(-1)
    , mEncoderPacketsWritten(0)
    , mHevcSeamFix(false)
    , mHevcSeamRewriteFailed(false)
    , mOutputDisplayOrderValid(true)
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

    // Only clearing point for mAbortRequested -- see the member declaration
    // in the header for the full flag-lifetime rationale.
    mAbortRequested.store(false, std::memory_order_relaxed);

    mInputFile = esFile;

    // Try to get frame rate from .info file if not provided
    if (frameRate <= 0) {
        const TTESInfoTiming info = TTESInfo::timingForVideo(esFile);
        if (info.frameRate > 0) {
            frameRate = info.frameRate;
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "TTESSmartCut: Using frame rate from .info:" << frameRate;
        }
    }

    // Default to 25fps if still no frame rate
    if (frameRate <= 0) {
        frameRate = 25.0;
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "TTESSmartCut: No frame rate found, using default:" << frameRate;
    }
    mFrameRate = frameRate;

    // Open and parse the ES file
    if (!mParser.openFile(esFile)) {
        setError(QString("Cannot open ES file: %1").arg(mParser.lastError()));
        return false;
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Parsing ES file...";
    emit progressChanged(0, tr("Parsing ES file..."));

    // Poll point for the ES parse itself (can run seconds on a real
    // recording): forwards to checkAbort() so a Cancel during initialize()
    // is recorded the same cooperative way as every other phase. Set fresh
    // every call -- mParser outlives a single initialize() (case (c)-style
    // reuse), but the lambda must always target the current instance.
    mParser.setAbortCallback([this]() { return checkAbort(); });

    if (!mParser.parseFile()) {
        mParser.closeFile();
        // Not wasAborted(): mWasAborted is only cleared at smartCutFrames()
        // entry (see its declaration), so on a reused engine whose PRIOR
        // run was aborted, it can still read true here even though THIS
        // call's parse failed for an unrelated reason. mAbortRequested was
        // just cleared at the top of this same call, so reading it directly
        // reliably answers "did a Cancel arrive during this call's parse" --
        // checkAbort() above still did the actual recording for the public
        // wasAborted()/lastError() contract.
        if (mAbortRequested.load(std::memory_order_relaxed)) return false;
        setError(QString("Cannot parse ES file: %1").arg(mParser.lastError()));
        return false;
    }

    // Correct frame rate if PAFF detected with old .info file
    if (mParser.isPAFF() && mFrameRate > 30) {
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "TTESSmartCut: PAFF detected, correcting frame rate from" << mFrameRate
                     << "to" << mFrameRate / 2.0;
        }
        mFrameRate /= 2.0;
    }

    // Parse H.264 SPS for frame_num patching and POC domain mismatch fix
    if (mParser.codecType() == NALU_CODEC_H264 && mParser.spsCount() > 0) {
        QByteArray sps = mParser.getSPS(0);
        TTH264SpsInfo spsInfo = ttParseH264SpsInfo(sps);
        if (spsInfo.log2MaxFrameNumMinus4 >= 0) {
            mLog2MaxFrameNum = spsInfo.log2MaxFrameNumMinus4 + 4;
            mPocType = spsInfo.pocType;
            mLog2MaxPocLsb = (spsInfo.pocType == 0) ? spsInfo.log2MaxPocLsbMinus4 + 4 : 0;
            mFrameMbsOnly = spsInfo.frameMbsOnly;
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "TTESSmartCut: Source SPS - log2_max_frame_num=" << mLog2MaxFrameNum
                         << "poc_type=" << mPocType << "log2_max_poc_lsb=" << mLog2MaxPocLsb
                         << "frame_mbs_only=" << mFrameMbsOnly;
            }
        } else {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("WARNING - could not parse SPS"));
        }
    }

    // Seed the encode/copy cost ratio k from the last run's measurement for
    // this codec (see weightedProgressPercent). Machine-relative ratio, not
    // an absolute-time calibration - see the mSeedK comment in the header.
    {
        TTSettingsCalibrationStore store;
        const QString codecKey = (mParser.codecType() == NALU_CODEC_H265) ? "h265" : "h264";
        mSeedK = store.factor(QStringLiteral("videok/") + codecKey);
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Initialization complete";
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  File:" << esFile;
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Codec:" << mParser.codecName();
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Frames:" << mParser.accessUnitCount();
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  GOPs:" << mParser.gopCount();
    if (TTSettings::instance()->logSmartCut())
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
    mSpsUnificationPocAnchor = -1;
    mSpsUnificationPocBase = -1;
    mEncoderPacketsWritten = 0;
    mHevcSeamFix = false;
    mHevcSeamRewriteFailed = false;
    mEncoderPocType = -1;
    mEncoderFrameMbsOnly = true;
    mEncoderPts = 0;
    mFramesStreamCopied = 0;
    mFramesReencoded = 0;
    mBytesWritten = 0;
}

// ----------------------------------------------------------------------------
// Get frame count
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Output display-order tracking (for MKV muxer display-PTS assignment).
// appendOutputDisplay records one written parser AU (= one frame; TTNaluParser
// merges PAFF field pairs). Any anomaly invalidates tracking; the muxer then
// keeps its legacy linear PTS (fallback by design, never worse than status quo).
// ----------------------------------------------------------------------------
void TTESSmartCut::appendOutputDisplay(int mapDisplayIndex, int srcAuIndex)
{
    if (!mOutputDisplayOrderValid) return;
    if (mapDisplayIndex < 0) {
        // e.g. HEVC dropped-RASL slot (decodeToDisplay == -1): no defined
        // display position -> whole list unusable for this run.
        mOutputDisplayOrderValid = false;
        mOutputDisplayOrder.clear();
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("output display-order tracking invalidated (negative "
                    "display index at source AU %1) - MKV muxer will use "
                    "legacy linear PTS").arg(srcAuIndex));
        return;
    }
    mOutputDisplayOrder.append(mapDisplayIndex);
}

// Returns the display position (frame units, output-local, 0-based) of each
// mux packet in write order; empty when tracking was invalidated.
// Entries are SOURCE display indices (one per parser AU = one per frame;
// TTNaluParser merges PAFF field pairs, and the MKV muxer does the same on
// read - counts match). Segments leave gaps in source display numbering, so
// the output-local position is the RANK of each source display index in the
// sorted set of all written ones: compact, gap-free, order-preserving.
// Duplicate source displays would make ranks ambiguous -> fallback.
QVector<int> TTESSmartCut::outputDisplayOrder() const
{
    if (!mOutputDisplayOrderValid || mOutputDisplayOrder.isEmpty())
        return QVector<int>();

    QVector<int> sorted = mOutputDisplayOrder;
    std::sort(sorted.begin(), sorted.end());
    QHash<int, int> rank;
    rank.reserve(sorted.size());
    for (int i = 0; i < sorted.size(); ++i) {
        if (i > 0 && sorted[i] == sorted[i - 1]) {
            // Duplicate source display index -> ranks ambiguous. Loud fallback:
            // gates treat this warning as FAIL (silent degradation is a bug).
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("output display-order has duplicate source display "
                        "index %1 - falling back to legacy linear PTS")
                    .arg(sorted[i]));
            return QVector<int>();
        }
        rank.insert(sorted[i], i);
    }

    QVector<int> result;
    result.reserve(mOutputDisplayOrder.size());
    for (int d : mOutputDisplayOrder) result.append(rank.value(d));
    return result;
}

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
// Smart Cut (frame-based)
// ----------------------------------------------------------------------------
bool TTESSmartCut::smartCutFrames(const QString& outputFile,
                                   const QList<QPair<int, int>>& cutFrames)
{
    if (!mIsInitialized) {
        setError("Not initialized - call initialize() first");
        return false;
    }

    // mWasAborted is an output, not an input: cleared at every run's entry
    // point (see the member declaration for the flag-lifetime rule).
    mWasAborted = false;
    if (checkAbort()) return false;  // pre-run abort request

    if (cutFrames.isEmpty()) {
        setError("Cut list is empty");
        return false;
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Starting smart cut";
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Input:" << mInputFile;
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Output:" << outputFile;
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Segments:" << cutFrames.size();

    // Reset statistics
    mFramesStreamCopied = 0;
    mFramesReencoded = 0;
    mBytesWritten = 0;
    mTotalFrames = 0;
    mCurrentSegment = 0;
    mTotalSegments = 0;
    mActualOutputRanges.clear();
    mOutputDisplayOrder.clear();
    mOutputDisplayOrderValid = true;
    mSeamNotes.clear();

    // ---- Display -> AU conversion (single source of truth) ----
    // UI/cut-list indices are display positions (Direction A). Below this point
    // everything works in decode-order AU indices. Build the map if not injected.
    if (!mDisplayMap.isValid())
        mDisplayMap = TTDisplayOrderMap::buildFromFile(mInputFile);
    if (!mDisplayMap.isValid() || mDisplayMap.count() != frameCount()) {
        setError(QString("display-order map unavailable or misaligned "
                         "(map %1 vs %2 AUs) - cannot cut accurately")
                     .arg(mDisplayMap.count()).arg(frameCount()));
        return false;
    }

    // Analyze cut points
    QList<TTCutSegmentInfo> segments = analyzeCutPoints(cutFrames);

    // First segment override: The decoder starts with an empty delayed_pic[]
    // reorder buffer, so no IDR barrier is needed. Force pure stream-copy
    // to avoid unnecessary re-encoding at non-IDR I-frame cut-ins.
    // Skip when the segment was folded to a pure re-encode by the cut-OUT logic
    // (streamCopyStartFrame < 0): un-folding it to full stream-copy would
    // re-include the display-late frames the fold exists to drop. The folded
    // pure re-encode is already frame-accurate at both ends.
    if (!segments.isEmpty() && segments[0].needsReencodeAtStart
            && segments[0].streamCopyStartFrame >= 0) {
        TTAccessUnit firstAU = mParser.accessUnitAt(segments[0].startFrame);
        // The cut-in keyframe may carry leading pictures: AUs that follow it
        // in decode order but display BEFORE the requested cut-in (open-GOP
        // B-frames, HEVC RASL). A pure stream-copy would include them — they
        // are undecodable at the segment start (their references precede the
        // cut) AND they stretch the muxer's AU-counted timeline by the
        // reorder depth, shifting A/V sync for the whole segment. Take the
        // shortcut only when no such AU exists; otherwise keep the
        // display-exact re-encode path from analyzeCutPoints.
        // decodeToDisplay() returns -1 for dropped (non-navigable) leading
        // pictures, which correctly counts as "displays before the cut-in".
        bool hasLeadingPics = false;
        if (firstAU.isKeyframe) {
            const int cutInDisplay = cutFrames.first().first;
            const int scanEnd = qMin(frameCount() - 1, segments[0].startFrame + 16);
            for (int au = segments[0].startFrame + 1; au <= scanEnd; ++au) {
                if (mDisplayMap.decodeToDisplay(au) < cutInDisplay) {
                    hasLeadingPics = true;
                    break;
                }
            }
        }
        if (firstAU.isKeyframe && hasLeadingPics
                && TTSettings::instance()->logSmartCut()) {
            qDebug() << "  First segment: cut-in keyframe has leading pictures"
                     << "- keeping display-exact re-encode (no stream-copy override)";
        }
        if (firstAU.isKeyframe && !hasLeadingPics) {
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "  First segment: overriding re-encode to pure stream-copy"
                         << "(decoder starts fresh, no delayed_pic[] barrier needed)";
            }
            segments[0].needsReencodeAtStart = false;
            segments[0].reencodeStartFrame = -1;
            segments[0].reencodeEndFrame = -1;
            segments[0].streamCopyStartFrame = segments[0].startFrame;
            // Preserve the frame-accurate cut-OUT decision from analyzeCutPoints:
            // when a tail re-encode is needed, stream-copy still ends at
            // tailStartFrame-1 (whole GOPs) and the tail GOP is re-encoded to drop
            // frames displaying after the cut-out. Resetting to endFrame here would
            // re-include the display-late frames AND duplicate the tail GOP. In the
            // override case tailStartFrame > startFrame always (the re-encode path
            // placed the stream-copy start at the next keyframe).
            segments[0].streamCopyEndFrame =
                (segments[0].needsReencodeAtEnd && segments[0].tailStartFrame > segments[0].startFrame)
                    ? segments[0].tailStartFrame - 1
                    : segments[0].endFrame;
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

    // Detect B-frame reorder delay from parsed stream structure.
    // This is needed for SPS patching so the decoder pre-allocates its
    // reorder buffer instead of increasing it on-the-fly (which causes stutter).
    if (mReorderDelay == 0) {
        mReorderDelay = mParser.computeReorderDelay();
        if (mReorderDelay > 0) {
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "  B-frame reorder delay from stream analysis:" << mReorderDelay;
        }
    }

    // Process each segment
    mTotalFrames = 0;
    for (const auto& seg : segments) {
        mTotalFrames += (seg.endFrame - seg.startFrame + 1);
    }

    // Planned copy/encode split for time-proportional progress weighting.
    mPlannedCopyFrames = 0;
    for (const auto& seg : segments) {
        if (seg.streamCopyStartFrame >= 0
            && seg.streamCopyEndFrame >= seg.streamCopyStartFrame)
            mPlannedCopyFrames += seg.streamCopyEndFrame - seg.streamCopyStartFrame + 1;
    }
    mPlannedEncodeFrames = qMax(0, mTotalFrames - mPlannedCopyFrames);
    mCopyMsAcc = mEncodeMsAcc = 0;
    mCopyFramesAcc = mEncodeFramesAcc = 0;
    mLastEmittedPercent = 0;

    // H.264 frame_num patching: track cumulative delta for inter-segment continuity.
    // Without this, frame_num gaps at segment boundaries cause the decoder to generate
    // dummy reference frames, resulting in visual stuttering/flashing.
    int cumulativeFrameNumDelta = 0;
    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

    mTotalSegments = segments.size();
    for (int i = 0; i < segments.size(); ++i) {
        if (checkAbort()) { outFile.close(); return false; }

        const TTCutSegmentInfo& seg = segments[i];
        mCurrentSegment = i + 1;

        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "  Processing segment" << i << ":"
                     << "frames" << seg.startFrame << "->" << seg.endFrame;
        }

        if (seg.needsReencodeAtStart) {
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "    Re-encode:" << seg.reencodeStartFrame
                         << "->" << seg.reencodeEndFrame;
            }
        }
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "    Stream-copy:" << seg.streamCopyStartFrame
                     << "->" << seg.streamCopyEndFrame;
        }

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
            writeEos(outFile);
            writeParameterSets(outFile, mReorderDelay);
            if (TTSettings::instance()->logSmartCut())
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
                int lastRefFN = ttReadFrameNumFromAU(lastAU, mLog2MaxFrameNum);

                // Next segment's first stream-copy frame
                const TTCutSegmentInfo& nextSeg = segments[i + 1];
                int nextStart = (nextSeg.streamCopyStartFrame >= 0)
                    ? nextSeg.streamCopyStartFrame : nextSeg.startFrame;
                QByteArray nextAU = mParser.readAccessUnitData(nextStart);
                int nextFirstFN = ttReadFrameNumFromAU(nextAU, mLog2MaxFrameNum);

                if (lastRefFN >= 0 && nextFirstFN >= 0) {
                    // Output frame_num of last ref frame (with current delta applied)
                    int outputLastRefFN = (lastRefFN + cumulativeFrameNumDelta) % maxFrameNum;
                    // Expected next frame_num for continuity
                    int expectedNext = (outputLastRefFN + 1) % maxFrameNum;
                    // New cumulative delta for next segment
                    cumulativeFrameNumDelta = (expectedNext - nextFirstFN + maxFrameNum) % maxFrameNum;

                    if (TTSettings::instance()->logSmartCut()) {
                        qDebug() << "    frame_num: seg" << i << "lastRef=" << lastRefFN
                                 << "output=" << outputLastRefFN
                                 << ", seg" << (i+1) << "first=" << nextFirstFN
                                 << "-> delta=" << cumulativeFrameNumDelta;
                    }
                } else {
                    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                        QString("WARNING: Could not read frame_num (lastRef=%1, nextFirst=%2) - skipping patching")
                            .arg(lastRefFN).arg(nextFirstFN));
                }
            }
        }

        // Progress is emitted granularly from streamCopyFrames/reencodeFrames
    }

    outFile.close();
    mBytesWritten = QFileInfo(outputFile).size();

    // Persist the measured encode/copy cost ratio k for the next run's seed
    // (mSeedK, see weightedProgressPercent). This is a machine-relative
    // RATIO between the two rates measured in THIS run, not an absolute-time
    // calibration - it does not fall under "video has no stored calibration"
    // (that rule targets ms-per-work-unit factors like mux/audio; k only
    // rebalances copy vs. encode frame weights within the video stage).
    // !mWasAborted is unreachable-true today: every abort return above this
    // point (segment loop, and every nested poll point via processSegment's
    // failure path) already exits before this code is reached. Kept anyway,
    // defensively, against a future reordering of the return points -- this
    // check is cheap and correct either way.
    if (!mWasAborted && mCopyFramesAcc > 0 && mEncodeFramesAcc > 0
        && mCopyMsAcc > 0 && mEncodeMsAcc > 0) {
        double copyMsPerFrame = double(mCopyMsAcc) / mCopyFramesAcc;
        double encMsPerFrame  = double(mEncodeMsAcc) / mEncodeFramesAcc;
        if (copyMsPerFrame > 0) {
            double k = encMsPerFrame / copyMsPerFrame;
            if (std::isfinite(k) && k > 0) {
                TTSettingsCalibrationStore store;
                const QString codecKey = (mParser.codecType() == NALU_CODEC_H265) ? "h265" : "h264";
                store.setFactor(QStringLiteral("videok/") + codecKey, k);
            }
        }
    }

    emit progressChanged(100, tr("Cut complete"));

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Complete";
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Frames stream-copied:" << mFramesStreamCopied;
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Frames re-encoded:" << mFramesReencoded;
    if (TTSettings::instance()->logSmartCut())
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
        seg.startDisplay = qBound(0, cut.first,  frameCount() - 1);
        seg.endDisplay   = qBound(0, cut.second, frameCount() - 1);

        if (seg.startDisplay >= seg.endDisplay) {
            continue;  // Skip empty segments
        }

        // AU of the frame DISPLAYED at the cut-in (Direction A anchor):
        seg.startFrame = mDisplayMap.displayToDecode(seg.startDisplay);

        // Last kept AU: a frame displaying <= endDisplay can sit at a later AU
        // (B-frame reorder). Take the max AU over the trailing reorder window.
        int endAU = mDisplayMap.displayToDecode(seg.endDisplay);
        for (int d = seg.endDisplay; d >= 0 && d > seg.endDisplay - 32; --d)
            endAU = qMax(endAU, mDisplayMap.displayToDecode(d));
        seg.endFrame = endAU;

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
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "    Cut-in at non-IDR I-frame" << seg.startFrame
                         << "- re-encode needed for IDR boundary";
            }
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
                if (TTSettings::instance()->logSmartCut())
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
                if (TTSettings::instance()->logSmartCut()) {
                    qDebug() << "    Smart Cut: Re-encode" << seg.reencodeStartFrame << "->" << seg.reencodeEndFrame
                             << ", Stream-copy from" << (usingIDR ? "IDR" : "I-slice") << nextKeyframe;
                }
            }
        } else {
            // Cut-in is at keyframe - pure stream copy
            TTAccessUnit au = mParser.accessUnitAt(seg.startFrame);
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "    Cut-in at" << (au.isIDR ? "IDR" : "I-slice") << "- pure stream copy";
            seg.reencodeStartFrame = -1;
            seg.reencodeEndFrame = -1;
            seg.streamCopyStartFrame = seg.startFrame;
            seg.streamCopyEndFrame = seg.endFrame;
        }

        // ---- Frame-accurate cut-OUT: decide tail re-encode ----
        // Default: no tail re-encode.
        seg.needsReencodeAtEnd = false;
        seg.tailStartFrame     = -1;

        if (seg.streamCopyStartFrame >= 0 && seg.streamCopyEndFrame >= 0) {
            // Find the earliest (decode-order) AU within the stream-copy range
            // that displays AFTER the cut-out, OR a kept frame (display<=endDisplay)
            // sitting beyond streamCopyEndFrame. If neither exists, the contiguous
            // stream-copy is already frame-accurate (optimization -> no tail).
            int firstLateAU = -1;
            for (int au = seg.streamCopyStartFrame; au <= seg.streamCopyEndFrame; ++au) {
                if (mDisplayMap.decodeToDisplay(au) > seg.endDisplay) { firstLateAU = au; break; }
            }
            // Also: a kept frame may sit just past streamCopyEndFrame (display<=endDisplay
            // at au>endFrame). Search a reorder-window beyond the boundary.
            int maxKeptAU = seg.streamCopyEndFrame;
            for (int au = seg.streamCopyEndFrame + 1;
                 au <= qMin(seg.streamCopyEndFrame + 64, frameCount() - 1); ++au) {
                if (mDisplayMap.decodeToDisplay(au) <= seg.endDisplay) maxKeptAU = au;
            }

            if (firstLateAU >= 0 || maxKeptAU > seg.streamCopyEndFrame) {
                // Tail re-encode needed. tailStart = last keyframe at/before the
                // earliest problem AU. Use firstLateAU if present, else the GOP of
                // the boundary. findKeyframeBefore accepts IDR/CRA/I-slice.
                int probe = (firstLateAU >= 0) ? firstLateAU : seg.streamCopyEndFrame;
                int tailStart = mParser.findKeyframeBefore(probe);
                if (tailStart < 0) tailStart = seg.streamCopyStartFrame;

                // Tail must not start before stream-copy start; if it would, the
                // whole copy region is the tail GOP -- handled below.
                if (tailStart <= seg.streamCopyStartFrame) {
                    // No stream-copy middle: the head GOP (if any) and the tail GOP
                    // are adjacent, so there is no whole GOP to copy between them.
                    // Collapse the whole segment into a single pure re-encode bounded
                    // by BOTH display limits (startDisplay..endDisplay): the display
                    // upper bound drops the display-late frames, so no separate tail
                    // pass is needed. reencodeEndFrame must span to seg.endFrame so the
                    // decode range covers every kept frame (incl. reorder-late AUs).
                    seg.reencodeStartFrame   = seg.startFrame;
                    seg.reencodeEndFrame     = seg.endFrame;
                    seg.streamCopyStartFrame = -1;
                    seg.streamCopyEndFrame   = -1;
                    seg.needsReencodeAtEnd   = false;  // handled by the display bound
                    seg.tailStartFrame       = -1;
                } else {
                    seg.tailStartFrame     = tailStart;
                    seg.needsReencodeAtEnd = true;
                    seg.streamCopyEndFrame = tailStart - 1;  // copy whole GOPs only
                }
                if (TTSettings::instance()->logSmartCut()) {
                    qDebug() << "    Cut-OUT needs tail re-encode: tailStart" << seg.tailStartFrame
                             << "endDisplay" << seg.endDisplay
                             << "(firstLateAU" << firstLateAU << "maxKeptAU" << maxKeptAU << ")";
                }
            } else if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "    Cut-OUT already frame-accurate (no tail re-encode)";
            }
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
// Defect A (2026-07-20): true when the keyframe at kfAu is followed by
// leading pictures — AUs that decode after it but display before it, within
// the probe window up to the next keyframe (capped at +16 AUs).
// decodeToDisplay() == -1 counts as "displays before" (cold-start/RASL
// convention, same as the first-segment override probe).
static bool kfHasLeadingPics(const TTNaluParser& parser,
                             const TTDisplayOrderMap& map,
                             int kfAu, int frameCount)
{
    int kfDisp = map.decodeToDisplay(kfAu);
    if (kfDisp < 0)
        return true;
    int nextKF = parser.findKeyframeAfter(kfAu + 1);
    if (nextKF < 0)
        nextKF = frameCount;
    int scanEnd = qMin(kfAu + 16, nextKF);
    for (int au = kfAu + 1; au < scanEnd; ++au) {
        if (map.decodeToDisplay(au) < kfDisp)   // -1 counts as "before"
            return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// HEVC seam preflight (Defekt A / H.265, spec 2026-07-21): decide per segment
// whether the RASL-preserving seam can run. All checks happen BEFORE any
// output is written; every failure falls back to the standard (EOB) seam.
// ----------------------------------------------------------------------------
bool TTESSmartCut::planHevcSeamFix(const TTCutSegmentInfo& segment)
{
    mHevcSeamFix = false;
    mHevcSeamX265Params.clear();
    mHevcSeamCtx = THevcSliceRewriteCtx();

    const int scStart = segment.streamCopyStartFrame;
    if (mParser.codecType() != NALU_CODEC_H265 || scStart < 0
        || segment.reencodeStartFrame < 0)
        return false;

    // P1a: copy-start must be a CRA (not IDR, not BLA).
    const TTAccessUnit au = mParser.accessUnitAt(scStart);
    if (au.isIDR || !au.isKeyframe)
        return false;                              // silent: nothing to fix
    int firstSliceType = -1;
    for (int ni : au.nalIndices) {
        TTNalUnit nu = mParser.nalUnitAt(ni);
        if (nu.isSlice) { firstSliceType = nu.type; break; }
    }
    if (firstSliceType != 21)
        return false;                              // silent: not CRA

    // P1b: RASL window after the CRA (decode order). No RASL -> seam is
    // already loss-free with the standard path.
    int numRasl = 0;
    for (int a = scStart + 1; a < mParser.accessUnitCount(); ++a) {
        int t = -1;
        const TTAccessUnit next = mParser.accessUnitAt(a);
        for (int ni : next.nalIndices) {
            TTNalUnit nu = mParser.nalUnitAt(ni);
            if (nu.isSlice) { t = nu.type; break; }
        }
        if (t == 8 || t == 9) ++numRasl;
        else break;
    }
    if (numRasl == 0)
        return false;                              // silent

    auto fallback = [this, scStart](const QString& why) {
        const QString note =
            tr("Seam at frame %1: RASL preservation not possible (%2) - "
               "using standard seam (short freeze)").arg(scStart).arg(why);
        mSeamNotes.append(note);
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__, note);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    HEVC seam fallback:" << note;
        return false;
    };

    // P0: uniform parameter sets in the source.
    if (mParser.spsCount() < 1)
        return fallback(QStringLiteral("no source SPS"));
    QByteArray sps0 = mParser.getSPS(0);
    for (int i = 1; i < mParser.spsCount(); ++i)
        if (mParser.getSPS(i) != sps0)
            return fallback(QStringLiteral("SPS changes mid-stream"));

    THevcSpsSeamInfo srcSps = parseHevcSpsSeamInfo(sps0);
    if (!srcSps.valid)
        return fallback(QString("source SPS: %1").arg(srcSps.invalidReason));
    if (srcSps.scalingListEnabled && !srcSps.scalingListFlat16)
        return fallback(QStringLiteral("non-flat scaling lists"));

    // P2: free pps_id + extra-bits table for the CRA probe.
    QVector<int> ppsExtraBitsById(64, -1);
    quint64 usedIds = 0;
    for (int i = 0; i < mParser.ppsCount(); ++i) {
        THevcPpsSeamInfo p = parseHevcPpsSeamInfo(mParser.getPPS(i));
        if (!p.valid || p.ppsId < 0 || p.ppsId > 63)
            return fallback(QStringLiteral("source PPS unparsable"));
        usedIds |= (1ULL << p.ppsId);
        ppsExtraBitsById[p.ppsId] = p.numExtraSliceHeaderBits;
    }
    int freeId = -1;
    for (int id = 1; id < 64; ++id)
        if (!(usedIds & (1ULL << id))) { freeId = id; break; }
    if (freeId < 0)
        return fallback(QStringLiteral("no free pps_id"));

    // P1c: CRA slice header -> craPoc + retain set.
    QByteArray craAu = mParser.readAccessUnitData(scStart);
    int craPoc = -1;
    QVector<int> retain;
    QString craErr;
    if (!parseHevcCraRpsInfo(craAu, srcSps.log2MaxPocLsb, ppsExtraBitsById,
                             &craPoc, &retain, &craErr))
        return fallback(QString("CRA probe: %1").arg(craErr));

    // P4 (coarse): POC window must not wrap inside the lsb cycle. Only the
    // structural minimum is testable here — the standin count N is decided by
    // frame selection, and no upper bound derived from the segment extent is
    // both safe and useful (a margin large enough to cover the boundary-
    // crossing extension rejects working seams: measured hevc_og 14..250 needs
    // base = 50 - 4 - 32 = 14, a +32 margin would demand 64). The exact check
    // runs in reencodeFrames before the first encoder packet is written, so a
    // late reject still rolls back without half-written output.
    if (craPoc - numRasl - 1 < 0)
        return fallback(QStringLiteral("POC window wraps lsb cycle"));
    for (int rp : retain)
        if (rp < 0)
            return fallback(QStringLiteral("retain set wraps lsb cycle"));

    // P3: encoder matching (measure, don't assume).
    QString params = deriveX265SeamParams(srcSps);
    THevcSpsSeamInfo encProbe;
    if (!probeHevcEncoderSeamSps(srcSps, params, &encProbe))
        return fallback(QStringLiteral("encoder SPS probe failed"));
    QString cmpReason;
    if (!hevcSpsSeamCompatible(srcSps, encProbe, &cmpReason))
        return fallback(QString("encoder SPS mismatch: %1").arg(cmpReason));

    mHevcSeamCtx.srcPocBits = srcSps.log2MaxPocLsb;
    mHevcSeamCtx.craPoc = craPoc;
    mHevcSeamCtx.numRasl = numRasl;
    mHevcSeamCtx.encPpsId = freeId;
    mHevcSeamCtx.retainPocs = retain;
    mHevcSeamX265Params = params;
    mHevcSeamFix = true;
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "    HEVC RASL-preserving seam: CRA AU" << scStart
                 << "poc" << craPoc << "rasl" << numRasl
                 << "retain" << retain << "encPpsId" << freeId
                 << "params" << mHevcSeamX265Params;
    return true;
}

// Each section starts with its own SPS/PPS + IDR, allowing clean decoder reset
// ----------------------------------------------------------------------------
bool TTESSmartCut::processSegment(QFile& outFile, const TTCutSegmentInfo& segment,
                                   int& frameNumDelta, int* actualStartAU)
{
    if (actualStartAU)
        *actualStartAU = -1;  // -1 = no adjustment

    // If only stream-copy (no re-encoding), write it, then fall through to the
    // frame-accurate cut-OUT tail re-encode below (the cut-out may still need it).
    if (segment.reencodeStartFrame < 0) {
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    Pure stream-copy segment";
        if (!streamCopyFrames(outFile, segment.streamCopyStartFrame,
                              segment.streamCopyEndFrame, mReorderDelay, frameNumDelta))
            return false;
        // fall through to the tail re-encode below (do NOT return)
    } else if (segment.streamCopyStartFrame < 0) {
        // Pure re-encode (short segment): selection already bounds display <=
        // endDisplay, so the cut-out is frame-accurate without a separate tail.
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    Pure re-encode segment";
        return reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                              -1, nullptr, actualStartAU, segment.startDisplay,
                              segment.endDisplay /*, tailMode=false default */);
    } else {
    // Mixed segment: Re-encode partial GOP + stream-copy from keyframe
    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "    Smart Cut: Re-encode" << segment.reencodeStartFrame << "->" << segment.reencodeEndFrame
                 << "then stream-copy" << segment.streamCopyStartFrame << "->" << segment.streamCopyEndFrame;
    }

    // PAFF H.264: SPS Unification — rewrite encoder output to match source SPS.
    // This eliminates the MBAFF→PAFF mode switch at the transition, allowing
    // seamless stream-copy without IDR/EOS DPB flush (which causes stutter).
    // Strategy:
    //   1. Write source SPS/PPS (id=0) before re-encode
    //   2. Write encoder PPS with id=1 (extracted from first encoder packet)
    //   3. Rewrite encoder slice NALs to use source SPS params + pps_id=1
    //   4. At transition: no IDR, no EOS — just continue with stream-copy
    //   5. Stream-copy frames use source PPS (id=0) naturally
    // SPS Unification is required for PAFF (separated fields: the encoder's
    // MBAFF output needs source-SPS signaling). For non-PAFF the encoder's
    // slice FORMAT is compatible with the source — but its POC domain is not
    // always: libx264 emits log2_max_poc_lsb=4 (16 values) while sources
    // typically use 6 (64 values). When the stream-copy start POC lies
    // outside the encoder-representable bridge window, applyPocDomainFix has
    // no safe patch value ("no safe poc_lsb") and the decoder discards the
    // first copied GOP as out-of-order after the EOS. For exactly those
    // seams, unification (slices rewritten into the source POC domain) makes
    // the bridge always possible; benign seams keep the unchanged fast path
    // (byte-identical output).
    bool pocBridgeable = true;
    int unificationSrcPocLsb = -1;
    if (mParser.codecType() == NALU_CODEC_H264 && !mParser.isPAFF()
            && segment.streamCopyStartFrame >= 0 && mLog2MaxPocLsb > 0) {
        // Anchor on the MINIMUM display-POC of the first stream-copy GOP, not
        // on the copy-start AU itself: the copy keyframe's leading B pictures
        // (decode after, display before it) carry SMALLER POCs. The rewritten
        // encoder POCs must end below the first DISPLAYED copy frame, or the
        // decoder's output buffer interleaves the seam (visible stutter).
        int anchorAu = segment.streamCopyStartFrame;
        int minDisp = mDisplayMap.decodeToDisplay(anchorAu);
        int nextKF = mParser.findKeyframeAfter(segment.streamCopyStartFrame + 1);
        if (nextKF < 0) nextKF = frameCount();
        int scanEnd = qMin(segment.streamCopyStartFrame + 16, nextKF);
        for (int au = segment.streamCopyStartFrame + 1; au < scanEnd; ++au) {
            int d = mDisplayMap.decodeToDisplay(au);
            if (d >= 0 && d < minDisp) { minDisp = d; anchorAu = au; }
        }
        // Anchor value: POC of the first DISPLAYED copy frame (min-display AU).
        QByteArray anchorAuData = mParser.readAccessUnitData(anchorAu);
        int anchorPocLsb = ttReadPocLsbFromAU(anchorAuData, mLog2MaxFrameNum,
                                            mLog2MaxPocLsb, mFrameMbsOnly);
        unificationSrcPocLsb = anchorPocLsb;
        // Classification: unchanged semantics - the copy-start AU's POC (the
        // value applyPocDomainFix bridges towards on the standard path).
        // Deliberately NOT extended to the min-display POC: leading-B POCs
        // wrap below the keyframe's and would flag seams the standard path
        // bridges fine POC-wise (2026-07-03 full-scan: 0 violations). Note
        // that POC bridging alone does NOT make a seam clean: defect A
        // (2026-07-20) showed non-IDR copy-starts with leading pictures are
        // corrupt on the standard path regardless — those are routed to
        // unification by the separate seamNeedsUnification trigger below.
        QByteArray scAU = mParser.readAccessUnitData(segment.streamCopyStartFrame);
        int scPocLsb = ttReadPocLsbFromAU(scAU, mLog2MaxFrameNum,
                                         mLog2MaxPocLsb, mFrameMbsOnly);
        // Measured encoder POC width when the probe succeeded with poc_type 0;
        // the constant stays as the fallback (probe failure or unexpected
        // poc_type — the latter is warned about loudly in the probe).
        probeEncoderPocParams();
        int encLog2PocLsb = (mProbedEncoderPocType == 0 && mProbedEncoderLog2PocLsb >= 4)
            ? mProbedEncoderLog2PocLsb : kExpectedEncoderLog2PocLsb;
        pocBridgeable = pocDomainBridgeable(scPocLsb, encLog2PocLsb,
                                            mLog2MaxPocLsb);
        if (!pocBridgeable && TTSettings::instance()->logSmartCut()) {
            qDebug() << "    POC domain not bridgeable (copy-start poc_lsb" << scPocLsb
                     << ", first-display poc_lsb" << anchorPocLsb
                     << "at AU" << anchorAu
                     << ") - enabling SPS unification for this segment";
        }
    }
    // Defect A (2026-07-20): a non-IDR copy-start keyframe with leading
    // pictures corrupts the standard seam — after the EOS flush the foreign
    // encoder POC domain inverts the seam output order and the leading-B
    // references resolve to wrong pictures (keyframe emitted N display slots
    // early, its N leading Bs silently corrupt; measured on synthetic bf=3
    // and ONE-HD material, see docs/code-map/smart-cut.md). The unification
    // seam (source POC domain + MMCO defusal) resolves those leading
    // pictures against the re-encode standins instead. IDR seams and
    // leading-pic-free seams keep the byte-identical standard path.
    bool seamNeedsUnification = false;
    if (mParser.codecType() == NALU_CODEC_H264
            && segment.streamCopyStartFrame >= 0
            && !mParser.accessUnitAt(segment.streamCopyStartFrame).isIDR
            && kfHasLeadingPics(mParser, mDisplayMap,
                                segment.streamCopyStartFrame, frameCount())) {
        seamNeedsUnification = true;
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    Non-IDR copy-start with leading pics at AU"
                     << segment.streamCopyStartFrame
                     << "- enabling SPS unification for this segment";
    }
    bool useSpsUnification = (mParser.codecType() == NALU_CODEC_H264)
        && (mParser.isPAFF() || !pocBridgeable || seamNeedsUnification);

    // HEVC seam fix (Defekt A / H.265): preflighted RASL-preserving seam.
    // On any later rewrite failure the segment output is rolled back and
    // re-written on the standard path (never a half-written segment).
    bool hevcSeamDone = false;
    if (planHevcSeamFix(segment)) {
        const qint64 segPos = outFile.pos();
        const int dispCount = mOutputDisplayOrder.size();
        const int rangeCount = mActualOutputRanges.size();
        const int reencBefore = mFramesReencoded;
        const int copiedBefore = mFramesStreamCopied;

        bool ok = false;
        mHevcSeamRewriteFailed = false;
        mEncoderPacketsWritten = 0;
        // 1. Source parameter sets rule the whole segment (prevents the
        //    DPB flush from an SPS content switch at the seam). For H.265
        //    writeParameterSets emits VPS/SPS/PPS verbatim (the reorder
        //    patch argument only ever applies to H.264).
        ok = writeParameterSets(outFile, 0);
        int adjustedStart = -1;
        if (ok)
            ok = reencodeFrames(outFile, segment.reencodeStartFrame,
                                segment.reencodeEndFrame,
                                segment.streamCopyStartFrame, &adjustedStart,
                                actualStartAU, segment.startDisplay);
        if (ok && !mHevcSeamRewriteFailed) {
            int scStart = (adjustedStart >= 0) ? adjustedStart
                                               : segment.streamCopyStartFrame;
            // 2. NO EOB, NO parameter-set re-write at the seam: the DPB must
            //    survive so the RASL window resolves against the standins.
            if (scStart <= segment.streamCopyEndFrame)
                ok = streamCopyFrames(outFile, scStart,
                                      segment.streamCopyEndFrame,
                                      mReorderDelay, frameNumDelta);
        }
        mHevcSeamFix = false;
        mHevcSeamX265Params.clear();

        if (ok && !mHevcSeamRewriteFailed) {
            hevcSeamDone = true;      // tail epilogue below is shared
        } else if (!mHevcSeamRewriteFailed) {
            return false;             // hard error (I/O, encoder) — abort
        } else {
            // P5 rollback: truncate segment output + restore counters, then
            // run the standard path below.
            const QString note =
                tr("Seam at frame %1: RASL preservation aborted (%2) - "
                   "using standard seam (short freeze)")
                .arg(segment.streamCopyStartFrame).arg(mHevcSeamFailReason);
            mSeamNotes.append(note);
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__, note);
            outFile.resize(segPos);
            outFile.seek(segPos);
            mOutputDisplayOrder.resize(dispCount);
            while (mActualOutputRanges.size() > rangeCount)
                mActualOutputRanges.removeLast();
            mFramesReencoded = reencBefore;
            mFramesStreamCopied = copiedBefore;
            mHevcSeamRewriteFailed = false;
        }
    }

    if (!hevcSeamDone) {
    if (useSpsUnification) {
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    PAFF SPS Unification: rewriting encoder output for source SPS";
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      Encoder: log2_fn=" << mEncoderLog2MaxFrameNum
                     << "log2_poc=" << mEncoderLog2MaxPocLsb
                     << "frame_mbs_only=" << mEncoderFrameMbsOnly;
        }
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      Source:  log2_fn=" << mLog2MaxFrameNum
                     << "log2_poc=" << mLog2MaxPocLsb
                     << "frame_mbs_only=" << mFrameMbsOnly;
        }

        // 1. Write source SPS/PPS (id=0) first — these are the "active" params
        writeParameterSets(outFile, mReorderDelay);

        // 2. Re-encode with SPS unification
        //    Encoder packets are rewritten on-the-fly in reencodeFrames.
        //    The encoder PPS (id=1) is written after extraction from first packet.
        mSpsUnification = true;
        mSpsUnificationOutFile = &outFile;
        // Non-PAFF POC seam: anchor the rewritten encoder POCs to the source
        // value at the copy start so they join monotonically (PAFF keeps the
        // legacy linear numbering: anchor stays -1).
        mSpsUnificationPocAnchor = mParser.isPAFF() ? -1 : unificationSrcPocLsb;
        mSpsUnificationPocBase = -1;
        mEncoderPacketsWritten = 0;

        int adjustedStart = -1;
        if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                            segment.streamCopyStartFrame, &adjustedStart, actualStartAU,
                            segment.startDisplay)) {
            mSpsUnification = false;
            mSpsUnificationOutFile = nullptr;
            mSpsUnificationPocAnchor = -1;
            mSpsUnificationPocBase = -1;
            return false;
        }
        mSpsUnification = false;
        mSpsUnificationOutFile = nullptr;
        mSpsUnificationPocAnchor = -1;
        mSpsUnificationPocBase = -1;

        int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
        int scEnd = segment.streamCopyEndFrame;

        if (scStart > scEnd) {
            // Do NOT return: the boundary-crossing extension can consume the whole
            // stream-copy range (scStart == tailStart) while a cut-out tail GOP is
            // still pending. Fall through to the tail re-encode epilogue.
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
        } else {

        // 3. EOS to flush DPB before stream-copy.
        // The re-encode produces MBAFF frames (x264 can't do PAFF). Without
        // EOS, these stay in the DPB and corrupt PAFF B-frame references at
        // stream-copy start (mmco failures, exceeds max, visual artifacts).
        // EOS flushes all MBAFF references so stream-copy starts clean.
        // The overlap extension (in reencodeFrames) ensures the re-encode
        // covers all frames up to the next keyframe, so no Open-GOP B-frames
        // need the flushed MBAFF references. (This branch is H.264-only, so
        // writeEos emits the same H.264 EOS the open-coded write did.)
        writeEos(outFile);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    PAFF SPS Unification: EOS before stream-copy at" << scStart;

        // Do NOT write SPS/PPS here — the first stream-copy keyframe AU has
        // inline SPS/PPS which ttPatchSpsNalsInAccessUnit will patch.
        // Writing duplicate SPS/PPS causes the h264 parser to combine them
        // with the first AU into one oversized packet → "Invalid NAL unit size".

        // Bridge encoder frame_nums to the stream-copy start. The encoder
        // slices were REWRITTEN into the SOURCE fn width by SPS unification
        // (mSpsUnification is already reset back to false at this point, so
        // do not consult it) — hence the encoder width here is
        // mLog2MaxFrameNum.
        frameNumDelta = bridgeFrameNum(scStart, mLog2MaxFrameNum);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    frameNumDelta:" << frameNumDelta;

        // MMCO neutralization: PAFF only. Its purpose is to stop source MMCOs
        // from operating on the flushed MBAFF standins (mmco failures, visual
        // artifacts); ~32 AUs cover the DPB refill. On frame-coded material
        // blanket-emptying the ops is actively harmful: streams whose adaptive
        // marking is load-bearing (measured: ONE-HD Petrocelli) end up with a
        // sliding window that evicts different pictures than the source
        // expected — display-order inversion and reference damage deep into
        // the copy. Without neutralization the only residue there is the
        // benign one-shot "mmco: unref short failure" chain reestablishment
        // (the op's target is already gone), pixels bit-identical (2026-07-20).
        int mmcoNeutralizeCount = mParser.isPAFF() ? 32 : 0;

        if (!streamCopyFrames(outFile, scStart, scEnd,
                              mReorderDelay, frameNumDelta, mmcoNeutralizeCount))
            return false;
        }   // end: stream-copy middle present (scStart <= scEnd)

    } else {
        // Standard path: re-encode + EOS + stream-copy
        mEncoderPacketsWritten = 0;

        int adjustedStart = -1;
        if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                            segment.streamCopyStartFrame, &adjustedStart, actualStartAU,
                            segment.startDisplay)) {
            return false;
        }

        int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
        int scEnd = segment.streamCopyEndFrame;

        if (scStart > scEnd) {
            // Do NOT return: fall through to the tail re-encode epilogue (a pending
            // cut-out tail GOP must still be written).
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
        } else {
        // Non-PAFF: use EOS to flush decoder DPB
        writeEos(outFile);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    Inserted EOS NAL - flushing DPB at" << scStart;

        // Write source parameter sets
        writeParameterSets(outFile, mReorderDelay);

        // Bridge encoder frame_nums to the stream-copy start. EOS flushes the
        // DPB but does NOT reset PrevRefFrameNum (only IDR does); without the
        // bridge a frame_num gap after PrevRefFrameNum overflows the DPB with
        // dummy references ("co located POCs unavailable" -> first copied GOP
        // dropped). This branch runs without SPS unification, so the encoder
        // slices carry the ENCODER SPS fn width. No outer guard: bridgeFrameNum
        // itself returns 0 for H.265/no-SPS, and frameNumDelta is only ever
        // consumed on H.264 paths (streamCopyFrames and the inter-segment
        // recompute are both H.264-gated).
        frameNumDelta = bridgeFrameNum(scStart, mEncoderLog2MaxFrameNum);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    frameNumDelta recalculated:" << frameNumDelta;

        // Stream-copy from keyframe
        if (!streamCopyFrames(outFile, scStart, scEnd,
                              mReorderDelay, frameNumDelta)) {
            return false;
        }
        }   // end: stream-copy middle present (scStart <= scEnd)
    }
    }   // end: standard/unification path (skipped when the HEVC seam fix ran)
    }   // end mixed-segment else

    // ---- Frame-accurate cut-OUT: re-encode the tail GOP if needed ----
    // Runs for pure-stream-copy and mixed segments (the pure-re-encode path
    // returned above, already display-bounded). The stream-copy -> tail-IDR
    // transition is clean by IDR flush: the tail's first frame is a forced IDR
    // (PrevRefFrameNum reset), so no frameNumDelta/MMCO/SPS-unification bridging
    // is needed across this boundary (unlike the head -> stream-copy boundary).
    if (segment.needsReencodeAtEnd && segment.tailStartFrame >= 0) {
        // EOS to flush the stream-copy DPB before the tail IDR (clean reset).
        writeEos(outFile);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "    Cut-OUT: EOS before tail re-encode at" << segment.tailStartFrame;

        if (!reencodeTail(outFile, segment.tailStartFrame, segment.endDisplay))
            return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Work-weighted progress: re-encoded frames cost far more than stream-copied
// ones. Weight both by their MEASURED per-frame cost so the reported percent
// is time-proportional (codec/resolution/machine all live in the measurement).
// Falls back to weight 1 (frame-count-proportional, previous behavior) until
// both rates have been measured.
// ----------------------------------------------------------------------------
int TTESSmartCut::weightedProgressPercent(int encodeInFlight) const
{
    if (mTotalFrames <= 0) return mLastEmittedPercent;

    // Fallback weight while this run has not yet measured both rates itself:
    // use the seeded k from the previous run (mSeedK) rather than assuming
    // encode and copy cost the same (k=1) - that assumption is what made the
    // initial stream-copy burst look too cheap and produced an over-optimistic
    // ETA (UAT 2026-08-09). No seed available yet -> k=1, previous behavior.
    double k = (mSeedK > 0) ? mSeedK : 1.0;
    if (mCopyFramesAcc > 0 && mEncodeFramesAcc > 0
        && mCopyMsAcc > 0 && mEncodeMsAcc > 0) {
        double copyMsPerFrame = double(mCopyMsAcc) / mCopyFramesAcc;
        double encMsPerFrame  = double(mEncodeMsAcc) / mEncodeFramesAcc;
        if (copyMsPerFrame > 0)
            k = encMsPerFrame / copyMsPerFrame;
    }

    double done  = mFramesStreamCopied + k * (mFramesReencoded + encodeInFlight);
    double total = mPlannedCopyFrames + k * mPlannedEncodeFrames;
    if (total <= 0) return mLastEmittedPercent;

    int pct = int(done * 100.0 / total);
    // Monotone despite k changing mid-run; cap at 99 (100 only at the end).
    return qBound(mLastEmittedPercent, pct, 99);
}

void TTESSmartCut::emitCutProgress(const QString& msg, int encodeInFlight)
{
    int pct = weightedProgressPercent(encodeInFlight);
    mLastEmittedPercent = pct;
    emit progressChanged(pct, msg);
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
    QElapsedTimer copyTimer; copyTimer.start();
    const int copiedAtEntry = mFramesStreamCopied;
    auto accountCopy = qScopeGuard([&] {
        mCopyMsAcc     += copyTimer.elapsed();
        mCopyFramesAcc += mFramesStreamCopied - copiedAtEntry;
    });

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "    Stream-copying frames" << startFrame << "->" << endFrame;
    if (frameNumDelta != 0) {
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "    frame_num delta:" << frameNumDelta
                     << "(MaxFrameNum=" << (1 << mLog2MaxFrameNum) << ")";
        }
    }
    if (neutralizeMmcoFrames > 0) {
        if (TTSettings::instance()->logSmartCut())
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
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "    Bulk-write:" << (endFrame - startFrame + 1) << "frames,"
                         << (totalSize / (1024*1024)) << "MB";
            }

            // Chunked so a user abort takes effect within one chunk and the
            // progress signal keeps flowing during multi-GB interior copies.
            static const int64_t kChunk = 8LL * 1024 * 1024;
            int64_t written = 0;
            while (written < totalSize) {
                if (checkAbort()) return false;
                int64_t n = qMin(kChunk, totalSize - written);
                if (outFile.write(reinterpret_cast<const char*>(startPtr) + written, n) != n) {
                    setError(QString("Bulk write failed for frames %1-%2").arg(startFrame).arg(endFrame));
                    return false;
                }
                written += n;
                // Progress: proportional frame count for the weighting model.
                int framesDone = int((endFrame - startFrame + 1) * (double(written) / totalSize));
                if (mTotalFrames > 0) {
                    int prev = mFramesStreamCopied;
                    mFramesStreamCopied = copiedAtEntry + framesDone;
                    if (mFramesStreamCopied != prev)
                        emitCutProgress(
                            tr("Processing segment %1/%2").arg(mCurrentSegment).arg(mTotalSegments), 0);
                }
            }

            for (int au = startFrame; au <= endFrame && mOutputDisplayOrderValid; ++au)
                appendOutputDisplay(mDisplayMap.decodeToDisplay(au), au);

            mFramesStreamCopied = copiedAtEntry + (endFrame - startFrame + 1);
            return true;
        }
        // Fall through to per-frame path if accessUnitPtr failed
    }

    // --- Per-frame path: patching required or mmap unavailable ---
    for (int i = startFrame; i <= endFrame; ++i) {
        if (checkAbort()) return false;

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
            TTH264PpsInfo ppsInfo = { true, false, true, false, false, 0, 0, 0, false };
            if (mParser.ppsCount() > 0)
                ppsInfo = ttParseH264PpsInfo(mParser.getPPS(0));
            auData = ttNeutralizeMmcoInAU(auData, mLog2MaxFrameNum,
                mLog2MaxPocLsb, mFrameMbsOnly, ppsInfo);
        }

        // Patch H.264 SPS NALs inline if requested
        if (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264) {
            auData = ttPatchSpsNalsInAccessUnit(auData, patchReorderFrames, mParser.isPAFF());
        }

        // Patch H.264 frame_num for inter-segment continuity
        if (frameNumDelta != 0 && mLog2MaxFrameNum > 0 &&
            mParser.codecType() == NALU_CODEC_H264) {
            auData = ttPatchFrameNumInAU(auData, mLog2MaxFrameNum, frameNumDelta, maxFrameNum);
        }

        // Write to output
        if (outFile.write(auData) != auData.size()) {
            setError(QString("Failed to write frame %1").arg(i));
            return false;
        }

        appendOutputDisplay(mDisplayMap.decodeToDisplay(i), i);

        mFramesStreamCopied++;

        // Granular progress update (every 50 frames to avoid signal overhead)
        if (mTotalFrames > 0 && (mFramesStreamCopied % 50 == 0 || i == endFrame)) {
            emitCutProgress(
                tr("Processing segment %1/%2").arg(mCurrentSegment).arg(mTotalSegments), 0);
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Re-encode frames (for partial GOPs)
// ----------------------------------------------------------------------------
bool TTESSmartCut::reencodeFrames(QFile& outFile, int startFrame, int endFrame,
                                  int streamCopyStartFrame, int* adjustedStreamCopyStart,
                                  int* actualStartAU, int startDisplay,
                                  int endDisplay, bool tailMode)
{
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "    Re-encoding frames" << startFrame << "->" << endFrame;

    if (adjustedStreamCopyStart)
        *adjustedStreamCopyStart = -1;  // -1 = no adjustment needed
    if (actualStartAU)
        *actualStartAU = -1;  // -1 = no adjustment (start AU == startFrame)

    ReencodeContext ctx(outFile, startFrame, endFrame, streamCopyStartFrame,
                        adjustedStreamCopyStart, actualStartAU, startDisplay,
                        endDisplay, tailMode);
    if (!computeDecodeRange(ctx)) return false;

    if (!resetDecoderForSegment(ctx)) return false;

    if (!decodeFramesIntoList(ctx)) return false;

    // ---- Display-order to AU-index mapping (unified PAFF + non-PAFF) ----
    selectFramesByDisplayOrder(ctx);

    // Preserve the submitted AU order for display tracking (framesToEncode
    // is consumed and cleared by runEncodePass; packets may arrive later).
    ctx.encodeAuOrder.reserve(ctx.framesToEncode.size());
    for (AVFrame* f : ctx.framesToEncode)
        ctx.encodeAuOrder.append(static_cast<int>(f->pts));

    // POC anchoring (non-PAFF unification): number the rewritten encoder POCs
    // so the LAST encoded frame lands directly below the copy-start POC —
    // base = anchor - 2*N (mod srcMax). The decoder's output order then runs
    // monotonically across the EOS into the stream-copy, instead of ending
    // ABOVE the copy-start POC and getting the first copied AU discarded as
    // out-of-order.
    if (mSpsUnification && mSpsUnificationPocAnchor >= 0 && mLog2MaxPocLsb > 0) {
        int srcMaxPocLsb = 1 << mLog2MaxPocLsb;
        int n = ctx.framesToEncode.size();
        int base = (mSpsUnificationPocAnchor - 2 * n) % srcMaxPocLsb;
        if (base < 0) base += srcMaxPocLsb;
        mSpsUnificationPocBase = base;
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      POC anchoring: copy-start poc_lsb" << mSpsUnificationPocAnchor
                     << "- encoding" << n << "frames from poc_lsb base" << base;
        }
    }

    // HEVC seam fix: anchor encoder POCs so the standins end directly below
    // the RASL window: base = craPoc - numRasl - N (source lsb domain).
    if (mHevcSeamFix) {
        const int n = ctx.framesToEncode.size();
        const int base = mHevcSeamCtx.craPoc - mHevcSeamCtx.numRasl - n;
        if (base < 0) {
            // P4 exact check failed (lsb wrap) -> trigger rollback before
            // any packet is written.
            mHevcSeamRewriteFailed = true;
            mHevcSeamFailReason = QStringLiteral("POC window wraps lsb cycle");
            return false;
        }
        mHevcSeamCtx.pocBase = base;
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "      HEVC seam POC base:" << base
                     << "(" << n << "standins )";
    }

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      Selected" << ctx.framesToEncode.size() << "frames for encoding"
                 << "(AU range" << startFrame << "-" << (ctx.streamCopyLimit - 1) << ")";
    }

    // Re-encode frames
    // Buffer the last encoder packet so we can patch poc_lsb before writing it,
    // preventing POC domain mismatch at the re-encode→stream-copy transition.

    QElapsedTimer encTimer; encTimer.start();
    const int reencodedAtEntry = mFramesReencoded;
    auto accountEncode = qScopeGuard([&] {
        mEncodeMsAcc     += encTimer.elapsed();
        mEncodeFramesAcc += mFramesReencoded - reencodedAtEntry;
    });

    if (!runEncodePass(ctx)) return false;

    if (!flushEncoder(ctx)) return false;

    applyPocDomainFix(ctx);

    if (!writePendingPacket(ctx)) return false;

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "      Encoding complete: sent" << ctx.framesSent << "frames, received" << ctx.packetsReceived << "packets";
    mFramesReencoded += ctx.packetsReceived;

    if (mTotalFrames > 0) {
        emitCutProgress(
            tr("Processing segment %1/%2").arg(mCurrentSegment).arg(mTotalSegments), 0);
    }

    return true;
}

// ----------------------------------------------------------------------------
// Re-encode the tail GOP [tailStartFrame ..] keeping only frames that display
// <= endDisplay. Produces a forced-IDR closed sub-segment (the segment end).
// The preceding stream-copy -> tail-IDR transition is clean by IDR flush, so
// this needs none of the head->stream-copy machinery (frameNumDelta / MMCO /
// SPS-unification). startDisplay is unused (tailMode bounds by au>=tailStart).
// ----------------------------------------------------------------------------
bool TTESSmartCut::reencodeTail(QFile& outFile, int tailStartFrame, int endDisplay)
{
    int endAU = mDisplayMap.displayToDecode(endDisplay);
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "    Tail re-encode: tailStart" << tailStartFrame
                 << "endDisplay" << endDisplay << "(endAU" << endAU << ")";
    return reencodeFrames(outFile, tailStartFrame, endAU,
                          -1, nullptr, nullptr,
                          /*startDisplay*/ -1, endDisplay, /*tailMode*/ true);
}

// ----------------------------------------------------------------------------
// Compute decode range: decodeStart (with runway extension if too close to
// startFrame) and decodeEnd (with pre-extension to next keyframe after
// streamCopyStartFrame for B-frame reorder coverage).
// ----------------------------------------------------------------------------
bool TTESSmartCut::computeDecodeRange(ReencodeContext& ctx)
{
    if (ctx.tailMode) {
        // Tail re-encode: ctx.startFrame is the tail GOP keyframe. Decode from one
        // keyframe BEFORE it (DPB/runway prefill) so the decoder's reorder/init
        // delay consumes the prefill GOP instead of the first kept tail frames.
        // The selection predicate (au >= ctx.startFrame) excludes the prefill
        // frames, so only the tail GOP is encoded. Extend the decode end past the
        // cut-out (next keyframe after endFrame) so B-frames displaying <=
        // endDisplay get their forward references; cap at stream end.
        ctx.decodeStart = ctx.startFrame;
        int prevKF = mParser.findKeyframeBefore(ctx.startFrame - 1);
        if (prevKF >= 0) ctx.decodeStart = prevKF;
        int afterEnd = mParser.findKeyframeAfter(ctx.endFrame + 1);
        if (afterEnd < 0) afterEnd = frameCount() - 1;
        ctx.decodeEnd = qMin(afterEnd + 20, frameCount() - 1);
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "      Tail decode range:" << ctx.decodeStart << "->" << ctx.decodeEnd
                     << "(tailStart=" << ctx.startFrame << " endFrame=" << ctx.endFrame << ")";
        return true;
    }

    // Find the keyframe we need to decode from.
    // H.264/H.265 decoders with frame-threading have an initialization delay:
    // the first D display-order frames are consumed by the pipeline and never
    // appear in the output. If startFrame is close to the nearest keyframe
    // (within the delay window), the target frames may be "eaten" by this delay.
    // Solution: go back one additional keyframe if the runway is too short.
    // This adds ~1 GOP of extra decoding but ensures all target frames appear.
    ctx.decodeStart = mParser.findKeyframeBefore(ctx.startFrame);
    if (ctx.decodeStart < 0) ctx.decodeStart = 0;

    const int DECODER_RUNWAY = 10;  // safety margin for frame-threading delay
    if (ctx.startFrame - ctx.decodeStart < DECODER_RUNWAY && ctx.decodeStart > 0) {
        int prevKeyframe = mParser.findKeyframeBefore(ctx.decodeStart - 1);
        if (prevKeyframe >= 0) {
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "      Runway too short (" << (ctx.startFrame - ctx.decodeStart)
                         << "frames), going back from keyframe" << ctx.decodeStart
                         << "to previous keyframe" << prevKeyframe;
            }
            ctx.decodeStart = prevKeyframe;
        }
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "      Decoding from keyframe at frame" << ctx.decodeStart;

    // Extend decode range beyond endFrame to include forward reference frames.
    // B-frames near endFrame need a P-frame beyond endFrame as reference.
    // Decode up to the next keyframe (exclusive) so the decoder has all references.
    // The HEVC decoder with frame-threading has an internal delay of ~7 frames
    // that persists even through flush. By feeding extra AUs beyond endFrame,
    // our target frames (startFrame..endFrame) are safely within the output
    // range instead of stuck in the decoder's trailing buffer.
    ctx.decodeEnd = qMin(ctx.endFrame + 20, frameCount() - 1);

    // Pre-extend decode range to cover the next keyframe after streamCopyStartFrame.
    // When B-frame reorder delay shifts the display-order CutIn past the stream-copy
    // boundary, we need to re-encode up to the next keyframe. Decoding these frames
    // upfront avoids having to reset the decoder from EOF state later.
    if (ctx.streamCopyStartFrame >= 0) {
        int potentialNextKF = mParser.findKeyframeAfter(ctx.streamCopyStartFrame + 1);
        if (potentialNextKF > 0) {
            int potentialExtEnd = qMin(potentialNextKF + 20, frameCount() - 1);
            if (potentialExtEnd > ctx.decodeEnd) {
                if (TTSettings::instance()->logSmartCut()) {
                    qDebug() << "      Pre-extending decode range to cover potential next keyframe"
                             << potentialNextKF << "(decode end:" << potentialExtEnd << ")";
                }
                ctx.decodeEnd = potentialExtEnd;
            }
        }
    }

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      Decode range:" << ctx.decodeStart << "->" << ctx.decodeEnd
                 << "(endFrame=" << ctx.endFrame << ", extra=" << (ctx.decodeEnd - ctx.endFrame) << ")";
    }

    return true;
}

// ----------------------------------------------------------------------------
// Reset decoder (flush after EOF state) and recreate encoder. Called once
// per segment because libx264's lookahead thread can't be restarted after
// a previous segment's flush.
// ----------------------------------------------------------------------------
bool TTESSmartCut::resetDecoderForSegment(ReencodeContext& /*ctx*/)
{
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
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "      Decoder reset for new segment";
    }

    // For multi-segment handling: libx264's lookahead thread can't be restarted
    // after flush, so we need to recreate the encoder for each segment
    if (mEncoder) {
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "      Recreating encoder for new segment";
        freeEncoder();
        // encoderInitialized will be false, triggering setupEncoder() below
    }

    return true;
}

// ----------------------------------------------------------------------------
// One-shot encoder initialization based on a probed decoded frame's params.
// Detects width/height/pixfmt and interlace flags, then calls setupEncoder().
// Returns true if already initialized (idempotent), or after a successful
// setupEncoder. Returns false if setupEncoder fails.
// ----------------------------------------------------------------------------
bool TTESSmartCut::ensureEncoderInitialized(ReencodeContext& ctx, AVFrame* probeFrame)
{
    if (ctx.encoderInitialized) return true;

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      First decoded frame: " << probeFrame->width << "x" << probeFrame->height
                 << " pix_fmt=" << probeFrame->format;
    }

    mDecodedWidth = probeFrame->width;
    mDecodedHeight = probeFrame->height;
    mDecodedPixFmt = static_cast<AVPixelFormat>(probeFrame->format);

    // Detect interlaced content from first decoded frame
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 30, 0)
    // FFmpeg 6.1+: interlace info via frame flags
    mInterlaced = (probeFrame->flags & AV_FRAME_FLAG_INTERLACED);
    mTopFieldFirst = (probeFrame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
#else
    mInterlaced = (probeFrame->interlaced_frame != 0);
    mTopFieldFirst = (probeFrame->top_field_first != 0);
#endif
    if (mInterlaced) {
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      Interlaced source detected:"
                     << (mTopFieldFirst ? "TFF" : "BFF");
        }
    }

    // Encoder SPS is parsed later, after first avcodec_receive_packet()
    // (x264 doesn't populate extradata until first packet is produced)
    if (!setupEncoder()) {
        return false;
    }
    ctx.encoderInitialized = true;
    return true;
}

// ----------------------------------------------------------------------------
// Decode all frames in [ctx.decodeStart, ctx.decodeEnd] into
// ctx.allDecodedFrames. Triggers one-shot encoder init on the first
// received frame. Tracks mReorderDelay from decoder->has_b_frames.
// Pre-condition: decoder is reset and ready (resetDecoderForSegment ran).
// ----------------------------------------------------------------------------
bool TTESSmartCut::decodeFramesIntoList(ReencodeContext& ctx)
{
    // Decode ALL frames from keyframe to endFrame using correct FFmpeg API pattern.
    // The decoder outputs frames in DISPLAY ORDER (reordered by PTS),
    // but we feed AUs in DECODE ORDER (file order). With B-frames these differ.
    // We collect ALL decoded frames first, then select by display position.
    //
    // Important: avcodec_receive_frame() can return multiple frames per send_packet,
    // and decodeFrame() only retrieves one. So we call avcodec_receive_frame() in a
    // loop after each send_packet to drain all available output.

    // Drain helper (was the drainDecoder lambda)
    auto drainAvailable = [&]() -> bool {
        while (true) {
            AVFrame* frame = av_frame_alloc();
            if (!frame) break;
            int ret = avcodec_receive_frame(mDecoder, frame);
            if (ret < 0) {
                av_frame_free(&frame);
                break;  // EAGAIN (need more input) or EOF
            }

            if (!ensureEncoderInitialized(ctx, frame)) {
                av_frame_free(&frame);
                return false;
            }

            ctx.allDecodedFrames.append(frame);

            // Track B-frame reorder delay — the decoder updates has_b_frames
            // dynamically as it encounters B-frames. Source SPS may lack
            // bitstream_restriction, so has_b_frames starts at 0 and increases
            // only after actual B-frames are decoded.
            if (mReorderDelay == 0 && mDecoder->has_b_frames > 0) {
                mReorderDelay = mDecoder->has_b_frames;
                if (TTSettings::instance()->logSmartCut()) {
                    qDebug() << "      Decoder has_b_frames:" << mReorderDelay
                             << "(detected after" << ctx.allDecodedFrames.size() << "frames)";
                }
            }
        }
        return true;
    };

    // Feed all AUs from keyframe through extended range
    for (int i = ctx.decodeStart; i <= ctx.decodeEnd; ++i) {
        if (checkAbort()) return false;

        QByteArray auData = mParser.readAccessUnitData(i);
        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1 for decoding").arg(i));
            return false;
        }

        // Send packet to decoder, retry on EAGAIN
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            // A break here only left the feed loop: the function went on to
            // drain and reported success with frames missing from the list -
            // the segment was silently truncated. Fail like every other error
            // in this loop does.
            setError(QString("av_packet_alloc failed"));
            return false;
        }
        if (av_new_packet(packet, auData.size()) < 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("av_new_packet failed"));
            av_packet_free(&packet);
            return false;
        }
        memcpy(packet->data, auData.constData(), auData.size());

        // Tag packet with AU index so we can identify frames in decoder output.
        packet->pts = i;
        packet->dts = i;

        while (true) {
            int ret = avcodec_send_packet(mDecoder, packet);
            if (ret == 0) break;  // accepted
            if (ret == AVERROR(EAGAIN)) {
                // Decoder input full, drain output first then retry
                if (!drainAvailable()) { av_packet_free(&packet); return false; }
                continue;
            }
            // Other error, skip this AU
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("send_packet error at frame %1: %2").arg(i).arg(avErrStr(ret)));
            break;
        }
        av_packet_free(&packet);

        // Drain all available output frames
        if (!drainAvailable()) return false;
    }

    // Enter drain mode: send NULL once, then drain remaining buffered frames
    avcodec_send_packet(mDecoder, nullptr);

    // Drain loop for flush — call receive_frame until EOF
    while (true) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) break;
        int ret = avcodec_receive_frame(mDecoder, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            break;
        }

        // Initialize encoder if needed (edge case: all frames come from flush)
        if (!ensureEncoderInitialized(ctx, frame)) {
            av_frame_free(&frame);
            return false;
        }

        ctx.allDecodedFrames.append(frame);
    }

    int lostFrames = (ctx.decodeEnd - ctx.decodeStart + 1) - ctx.allDecodedFrames.size();
    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      Decoded" << ctx.allDecodedFrames.size() << "frames from"
                 << (ctx.decodeEnd - ctx.decodeStart + 1) << "input AUs"
                 << "(" << lostFrames << "lost to decoder delay)";
    }

    return true;
}

// ----------------------------------------------------------------------------
// Frame selection for re-encode, display-order based (PAFF and non-PAFF).
//
// allDecodedFrames is display-ordered; each frame's ->pts carries its AU index.
// The kept set is defined in DISPLAY space (Direction A): every frame whose
// display position >= ctx.startDisplay, up to the stream-copy AU boundary. The
// display predicate is exact even where the kept set is NOT contiguous in AU
// space (e.g. MBAFF: AU 36385 displays at 36384 = keep; AU 36384 displays at
// 36385... ). Open-GOP B-frames before the cut-in are excluded automatically
// (their display position < startDisplay).
// ----------------------------------------------------------------------------
void TTESSmartCut::selectFramesByDisplayOrder(ReencodeContext& ctx)
{
    if (ctx.tailMode) {
        // Tail re-encode: start at the tail GOP keyframe (ctx.startFrame) and
        // keep frames displaying <= endDisplay. There is no following stream-copy,
        // so streamCopyLimit is unused; the boundary-crossing logic (cut-in
        // specific) does not apply.
        ctx.streamCopyLimit = frameCount();
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "      Tail selection: startFrame" << ctx.startFrame
                     << "endDisplay" << ctx.endDisplay;
    } else {
        ctx.streamCopyLimit = (ctx.streamCopyStartFrame >= 0) ? ctx.streamCopyStartFrame
                                                              : (ctx.endFrame + 1);

        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      Display-order mapping: display" << ctx.startDisplay
                     << "-> AU" << mDisplayMap.displayToDecode(ctx.startDisplay)
                     << "(streamCopyLimit" << ctx.streamCopyLimit << ")";
        }

        // Boundary crossing (old Case A/B): pre-cut display content can hide inside
        // the stream-copy GOP as frames that DISPLAY before the cut-in. Exact with
        // the map: if any AU in [streamCopyLimit, nextKF) displays before
        // startDisplay, extend the re-encode to the next keyframe.
        if (ctx.streamCopyStartFrame >= 0) {
            int nextKF = mParser.findKeyframeAfter(ctx.streamCopyStartFrame + 1);
            if (nextKF < 0) nextKF = frameCount();
            bool crossing = false;
            for (int au = ctx.streamCopyLimit; au < nextKF; ++au) {
                if (mDisplayMap.decodeToDisplay(au) < ctx.startDisplay) { crossing = true; break; }
            }
            if (crossing) {
                ctx.streamCopyLimit = nextKF;
                if (ctx.adjustedStreamCopyStart) *ctx.adjustedStreamCopyStart = nextKF;
                if (TTSettings::instance()->logSmartCut())
                    qDebug() << "      Boundary crossing: pre-cut display content inside"
                             << "stream-copy GOP -> extending re-encode to keyframe" << nextKF;
            }
        }
    }

    // Selection: mode-aware predicate (head / pure-re-encode / tail).
    for (int i = 0; i < ctx.allDecodedFrames.size(); ++i) {
        const int au   = static_cast<int>(ctx.allDecodedFrames[i]->pts);
        const int disp = mDisplayMap.decodeToDisplay(au);
        bool keep;
        if (ctx.tailMode) {
            // Tail: AU lower bound (tail GOP start), display upper bound.
            keep = (au >= ctx.startFrame) && (disp <= ctx.endDisplay);
        } else if (ctx.streamCopyStartFrame < 0 && ctx.endDisplay >= 0) {
            // Pure re-encode (short segment): both display bounds.
            keep = (disp >= ctx.startDisplay) && (disp <= ctx.endDisplay);
        } else {
            // Head re-encode (mixed): display lower bound + AU stream-copy bound.
            keep = (disp >= ctx.startDisplay) && (au < ctx.streamCopyLimit);
        }
        if (keep) ctx.framesToEncode.append(ctx.allDecodedFrames[i]);
        else      av_frame_free(&ctx.allDecodedFrames[i]);
    }
    ctx.allDecodedFrames.clear();

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      Selected" << ctx.framesToEncode.size()
                 << "frames for re-encode (tailMode" << ctx.tailMode
                 << "startDisplay" << ctx.startDisplay
                 << "endDisplay" << ctx.endDisplay
                 << "streamCopyLimit" << ctx.streamCopyLimit << ")";
    }

    // actualStartAU stays -1 (the old reporting compensated the mixed-index
    // walk's deviation, which no longer exists). The video now starts exactly
    // at the user-chosen display frame, so the audio keepList needs no shift.
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
        mDecoder->extradata = static_cast<uint8_t*>(
            av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!mDecoder->extradata) {
            setError("av_mallocz failed for decoder extradata");
            return false;
        }
        mDecoder->extradata_size = extradata.size();
        memcpy(mDecoder->extradata, extradata.constData(), extradata.size());
    }

    // Disable frame threading to prevent PTS misassignment.
    // Frame-threaded H.264 decoders can mismap content to PTS values
    // when starting mid-stream, causing wrong frames to be re-encoded.
    mDecoder->thread_count = 1;

    int ret = avcodec_open2(mDecoder, codec, nullptr);
    if (ret < 0) {
        setError(QString("Cannot open decoder: %1").arg(avErrStr(ret)));
        avcodec_free_context(&mDecoder);
        return false;
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Decoder setup complete";
    return true;
}

// ----------------------------------------------------------------------------
// Setup encoder
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Probe the encoder's POC parameters before any segment is encoded.
// setupEncoder() cannot run yet (it needs decoded-frame parameters), so this
// mirrors its knobs from parser-derived sources and opens a throwaway libx264
// context with AV_CODEC_FLAG_GLOBAL_HEADER: the SPS is then available in
// extradata without encoding a single frame. x264's POC choice depends on
// bframes/interlace/keyint-class parameters, not on resolution — an
// assumption this probe does NOT rely on blindly: parseEncoderSpsFromPacket
// compares the probe against the real per-segment encoder SPS and warns
// loudly on mismatch.
// x264/x265 preset names by settings index (setupEncoder and the two probes).
int TTESSmartCut::effectivePresetIndex() const
{
    return (mPresetOverride >= 0) ? qBound(0, mPresetOverride, TTEncoderNames::kPresetCount - 1)
                                  : qBound(0, TTSettings::instance()->encoderPreset(), TTEncoderNames::kPresetCount - 1);
}

QByteArray TTESSmartCut::probeEncoderExtradata(const char* codecName, int width, int height,
                                               int bitDepthLuma, bool interlaced,
                                               AVDictionary** opts)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) { av_dict_free(opts); return QByteArray(); }
    AVCodecContext* probe = avcodec_alloc_context3(codec);
    if (!probe) { av_dict_free(opts); return QByteArray(); }

    probe->width  = width;
    probe->height = height;
    probe->pix_fmt = (bitDepthLuma >= 10) ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
    probe->time_base = (AVRational){1, static_cast<int>(mFrameRate * 1000)};
    probe->framerate = (AVRational){static_cast<int>(mFrameRate * 1000), 1000};
    probe->max_b_frames = 0;
    probe->thread_count = 1;
    probe->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (interlaced) {
        probe->flags |= AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME;
        probe->field_order = AV_FIELD_TT;
    }

    const int ret = avcodec_open2(probe, codec, opts);
    av_dict_free(opts);
    QByteArray extradata;
    if (ret >= 0 && probe->extradata && probe->extradata_size > 0)
        extradata = QByteArray(reinterpret_cast<const char*>(probe->extradata), probe->extradata_size);
    avcodec_free_context(&probe);
    return extradata;
}
// ----------------------------------------------------------------------------
bool TTESSmartCut::probeEncoderPocParams()
{
    if (mPocProbeDone) return mProbedEncoderLog2PocLsb > 0;
    mPocProbeDone = true;

    if (mParser.codecType() != NALU_CODEC_H264 || mParser.spsCount() == 0)
        return false;

    TTH264SpsInfo src = ttParseH264SpsInfo(mParser.getSPS(0));
    if (src.picWidth <= 0 || src.picHeight <= 0)
        return false;

    // Same option sources as setupEncoder (H.264 branch).
    TTSettings* s = TTSettings::instance();
    int crf        = s->encoderCrf();
    int profileIdx = qBound(0, s->encoderProfile(), TTEncoderNames::kH264ProfileCount - 1);
    if (src.bitDepthLuma >= 10 && profileIdx < 3) profileIdx = 3;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "profile", TTEncoderNames::kH264Profiles[profileIdx], 0);
    av_dict_set(&opts, "forced-idr", "1", 0);
    av_dict_set(&opts, "preset", TTEncoderNames::kPresets[effectivePresetIndex()], 0);
    av_dict_set(&opts, "crf", QString::number(crf).toUtf8().constData(), 0);

    // Interlace approximation from the source SPS: MBAFF/PAFF sources carry
    // frame_mbs_only_flag == 0 and are encoded with interlace flags set.
    const QByteArray extradata = probeEncoderExtradata("libx264", src.picWidth, src.picHeight,
                                                       src.bitDepthLuma, !src.frameMbsOnly, &opts);
    if (extradata.isEmpty())
        return false;

    TTH264SpsInfo enc;
    if (!ttFindH264SpsInPacket(extradata, enc) || enc.log2MaxFrameNumMinus4 < 0)
        return false;

    mProbedEncoderPocType = enc.pocType;
    mProbedEncoderLog2PocLsb = (enc.pocType == 0) ? enc.log2MaxPocLsbMinus4 + 4 : 0;
    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "    Probed encoder SPS: log2_poc=" << mProbedEncoderLog2PocLsb
                 << "poc_type=" << mProbedEncoderPocType
                 << "(expected constant" << kExpectedEncoderLog2PocLsb << ")";
    }
    TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
        QString("Encoder POC probe: log2_max_poc_lsb=%1 poc_type=%2")
            .arg(mProbedEncoderLog2PocLsb).arg(mProbedEncoderPocType));
    if (mProbedEncoderPocType != 0) {
        // Measured fact, not an anomaly: libx264 picks poc_type 2 for
        // progressive bf=0 encodes (poc_type 0 only with interlace flags).
        // No poc_lsb patch exists or is needed there — seam continuity is
        // carried by EOS + the frame_num bridge, both verified. The branch
        // classification keeps the legacy constant (unchanged routing).
        TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
            QString("Encoder uses poc_type %1 (libx264 default for progressive "
                    "bf=0); POC classification keeps the legacy constant "
                    "(log2_max_poc_lsb=%2), seam continuity via EOS + frame_num")
                .arg(mProbedEncoderPocType).arg(kExpectedEncoderLog2PocLsb));
    }
    return true;
}

// ----------------------------------------------------------------------------
// HEVC seam fix: derive x265 params so the encoder SPS matches the source SPS
// in every CABAC-/parse-relevant field. Explicit on/off for each knob so the
// user's preset cannot silently break conformance (spec decision 3).
// ----------------------------------------------------------------------------
QString TTESSmartCut::deriveX265SeamParams(const THevcSpsSeamInfo& src)
{
    QStringList p;
    p << QString("tu-intra-depth=%1").arg(src.tuDepthIntra + 1);
    p << QString("tu-inter-depth=%1").arg(src.tuDepthInter + 1);
    p << QString("amp=%1").arg(src.ampEnabled ? 1 : 0);
    p << QString("sao=%1").arg(src.saoEnabled ? 1 : 0);
    p << QString("tmvp=%1").arg(src.temporalMvpEnabled ? 1 : 0);
    p << QString("strong-intra-smoothing=%1").arg(src.strongIntraSmoothing ? 1 : 0);
    return p.join(':');
}

// ----------------------------------------------------------------------------
// Probe the actual encoder SPS for the derived params (measure, don't assume —
// same pattern as probeEncoderPocParams for H.264). GLOBAL_HEADER puts the
// parameter sets into extradata without encoding a frame.
// ----------------------------------------------------------------------------
bool TTESSmartCut::probeHevcEncoderSeamSps(const THevcSpsSeamInfo& srcSps,
                                           const QString& x265Params,
                                           THevcSpsSeamInfo* encSps)
{
    TTSettings* s = TTSettings::instance();
    int crf       = s->encoderCrf();
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", TTEncoderNames::kPresets[effectivePresetIndex()], 0);
    av_dict_set(&opts, "crf", QString::number(crf).toUtf8().constData(), 0);
    av_dict_set(&opts, "profile",
                srcSps.bitDepthLuma >= 10 ? "main10" : "main", 0);
    av_dict_set(&opts, "x265-params", x265Params.toUtf8().constData(), 0);

    const QByteArray extradata = probeEncoderExtradata("libx265", srcSps.picWidth, srcSps.picHeight,
                                                       srcSps.bitDepthLuma, false, &opts);
    if (extradata.isEmpty())
        return false;

    // Find the SPS NAL (type 33) in the annex-b extradata.
    int sc = 0, type = 0;
    for (int i = 0; (i = ttHevcNextNal(extradata, i, &sc, &type)) >= 0; i += sc + 1) {
        if (type == 33) {
            *encSps = parseHevcSpsSeamInfo(extradata.mid(i));
            return encSps->valid;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// CABAC-/parse-relevant field comparison. Whitelisted (may differ):
// log2MaxPocLsb (rewrite writes source width), dpb/reorder/latency, VUI,
// profile constraint flags. Scaling: source disabled -> encoder must be
// disabled; source enabled -> only flat-16 acceptable (decode-neutral,
// encoder stays without lists).
// ----------------------------------------------------------------------------
bool TTESSmartCut::hevcSpsSeamCompatible(const THevcSpsSeamInfo& src,
                                         const THevcSpsSeamInfo& enc,
                                         QString* reason)
{
    auto fail = [reason](const QString& why) {
        if (reason) *reason = why;
        return false;
    };
    if (!src.valid) return fail(QString("source SPS: %1").arg(src.invalidReason));
    if (!enc.valid) return fail(QString("encoder SPS: %1").arg(enc.invalidReason));
    if (src.maxSubLayersMinus1 != 0 || enc.maxSubLayersMinus1 != 0)
        return fail(QStringLiteral("sub-layers"));
    if (src.chromaFormatIdc != enc.chromaFormatIdc)
        return fail(QStringLiteral("chroma_format"));
    if (src.picWidth != enc.picWidth || src.picHeight != enc.picHeight)
        return fail(QStringLiteral("dimensions"));
    if (src.bitDepthLuma != enc.bitDepthLuma
        || src.bitDepthChroma != enc.bitDepthChroma)
        return fail(QStringLiteral("bit depth"));
    if (src.log2MinCbSizeMinus3 != enc.log2MinCbSizeMinus3
        || src.log2DiffMaxMinCbSize != enc.log2DiffMaxMinCbSize)
        return fail(QStringLiteral("coding block sizes"));
    if (src.log2MinTbSizeMinus2 != enc.log2MinTbSizeMinus2
        || src.log2DiffMaxMinTbSize != enc.log2DiffMaxMinTbSize)
        return fail(QStringLiteral("transform block sizes"));
    if (src.tuDepthInter != enc.tuDepthInter
        || src.tuDepthIntra != enc.tuDepthIntra)
        return fail(QStringLiteral("transform hierarchy depth"));
    if (src.ampEnabled != enc.ampEnabled) return fail(QStringLiteral("amp"));
    if (src.saoEnabled != enc.saoEnabled) return fail(QStringLiteral("sao"));
    if (src.pcmEnabled || enc.pcmEnabled) return fail(QStringLiteral("pcm"));
    if (src.temporalMvpEnabled != enc.temporalMvpEnabled)
        return fail(QStringLiteral("temporal mvp"));
    if (src.strongIntraSmoothing != enc.strongIntraSmoothing)
        return fail(QStringLiteral("strong intra smoothing"));
    if (enc.scalingListEnabled)
        return fail(QStringLiteral("encoder scaling lists unexpected"));
    if (src.scalingListEnabled && !src.scalingListFlat16)
        return fail(QStringLiteral("source scaling lists not flat"));
    if (src.numShortTermRefPicSets != 0 || enc.numShortTermRefPicSets != 0)
        return fail(QStringLiteral("SPS RPS sets"));
    if (src.longTermRefPicsPresent || enc.longTermRefPicsPresent)
        return fail(QStringLiteral("long-term ref pics"));
    return true;
}

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
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "  Using decoded frame parameters:" << mDecodedWidth << "x" << mDecodedHeight
                     << "pix_fmt=" << mDecodedPixFmt;
        }
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
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "  Copied from decoder: SAR=" << mDecoder->sample_aspect_ratio.num
                     << "/" << mDecoder->sample_aspect_ratio.den
                     << "profile=" << mDecoder->profile << "level=" << mDecoder->level;
        }
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
        if (TTSettings::instance()->logSmartCut())
            qDebug() << "  Encoder: interlaced mode" << (mTopFieldFirst ? "TFF" : "BFF");
    }

    // Thread count
    mEncoder->thread_count = 0;  // Auto

    // Read transient working values (encoderCrf/Preset/Profile). These are
    // kept in sync with the codec-specific App-Defaults by
    // TTSettings::setEncoderCodec() and overwritten by Cut-Dialog overrides
    // or .ttcut project load. The generic profile range differs per codec,
    // so we still clamp against the codec-specific upper bound below.
    int crf, profileIdx;
    const int presetIdx = effectivePresetIndex();

    AVDictionary* opts = nullptr;

    if (mParser.codecType() == NALU_CODEC_H264) {
        TTSettings* s = TTSettings::instance();
        crf        = s->encoderCrf();
        profileIdx = qBound(0, s->encoderProfile(), TTEncoderNames::kH264ProfileCount - 1);

        // Auto-detect bit depth from pixel format and override profile if needed
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(mEncoder->pix_fmt);
        if (desc) {
            int bitDepth = desc->comp[0].depth;
            if (bitDepth >= 10 && profileIdx < 3) {
                profileIdx = 3;  // high10
                if (TTSettings::instance()->logSmartCut())
                    qDebug() << "TTESSmartCut: Auto-selected high10 profile for" << bitDepth << "bit source";
            }
        }

        av_dict_set(&opts, "profile", TTEncoderNames::kH264Profiles[profileIdx], 0);

        // Force IDR frames when requesting I-frames. Without this, x264
        // produces Non-IDR I-frames (NAL type 1) which don't flush the
        // decoder's delayed_pic[] reorder buffer, causing frame interleaving
        // at segment boundaries.
        av_dict_set(&opts, "forced-idr", "1", 0);


    } else {
        // H.265
        TTSettings* s = TTSettings::instance();
        crf        = s->encoderCrf();
        profileIdx = qBound(0, s->encoderProfile(), TTEncoderNames::kH265ProfileCount - 1);

        // Auto-detect bit depth from pixel format and override profile if needed
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(mEncoder->pix_fmt);
        if (desc) {
            int bitDepth = desc->comp[0].depth;
            if (bitDepth >= 12 && profileIdx < 2) {
                profileIdx = 2;  // main12
                if (TTSettings::instance()->logSmartCut())
                    qDebug() << "TTESSmartCut: Auto-selected main12 profile for" << bitDepth << "bit source";
            } else if (bitDepth >= 10 && profileIdx < 1) {
                profileIdx = 1;  // main10
                if (TTSettings::instance()->logSmartCut())
                    qDebug() << "TTESSmartCut: Auto-selected main10 profile for" << bitDepth << "bit source";
            }
        }

        av_dict_set(&opts, "profile", TTEncoderNames::kH265Profiles[profileIdx], 0);

        // HEVC seam fix: SPS-derived params (tu-depth, amp, sao, tmvp,
        // strong-intra-smoothing) so the re-encode slices stay CABAC-conform
        // under the SOURCE SPS. Matching wins over preset defaults
        // (spec 2026-07-21, decision 3). Empty when the fix is inactive.
        if (mHevcSeamFix && !mHevcSeamX265Params.isEmpty()) {
            av_dict_set(&opts, "x265-params",
                        mHevcSeamX265Params.toUtf8().constData(), 0);
            if (TTSettings::instance()->logSmartCut())
                qDebug() << "TTESSmartCut: seam x265-params:" << mHevcSeamX265Params;
        }
    }

    av_dict_set(&opts, "preset", TTEncoderNames::kPresets[presetIdx], 0);
    av_dict_set(&opts, "crf", QString::number(crf).toUtf8().constData(), 0);

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "TTESSmartCut: Encoder settings -"
                 << "codec:" << (mParser.codecType() == NALU_CODEC_H264 ? "H.264" : "H.265")
                 << "preset:" << TTEncoderNames::kPresets[presetIdx]
                 << "crf:" << crf
                 << "profile:" << profileIdx
                 << "decoder profile:" << (mDecoder ? mDecoder->profile : -1)
                 << "decoder level:" << (mDecoder ? mDecoder->level : -1);
    }

    int ret = avcodec_open2(mEncoder, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        setError(QString("Cannot open encoder: %1").arg(avErrStr(ret)));
        avcodec_free_context(&mEncoder);
        return false;
    }

    if (TTSettings::instance()->logSmartCut())
        qDebug() << "TTESSmartCut: Encoder setup complete";
    if (TTSettings::instance()->logSmartCut())
        qDebug() << "  Size:" << mEncoder->width << "x" << mEncoder->height;
    if (TTSettings::instance()->logSmartCut())
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
// Parse encoder SPS from first packet's inline SPS NAL.
// Without GLOBAL_HEADER, x264 puts SPS/PPS inline (not in extradata).
// HEVC: no-op (mEncoderLog2* fields are H.264-specific).
// Idempotent: returns immediately if already parsed.
// ----------------------------------------------------------------------------
void TTESSmartCut::parseEncoderSpsFromPacket(ReencodeContext& ctx, const QByteArray& rawData)
{
    if (ctx.encoderSpsParsed) return;
    if (mParser.codecType() != NALU_CODEC_H264) return;

    TTH264SpsInfo encSps;
    if (ttFindH264SpsInPacket(rawData, encSps)) {
        mEncoderLog2MaxFrameNum = encSps.log2MaxFrameNumMinus4 + 4;
        mEncoderLog2MaxPocLsb = (encSps.pocType == 0)
            ? encSps.log2MaxPocLsbMinus4 + 4 : 0;
        mEncoderPocType = encSps.pocType;
        mEncoderFrameMbsOnly = encSps.frameMbsOnly;
        if (TTSettings::instance()->logSmartCut()) {
            qDebug() << "      Encoder SPS: log2_fn=" << mEncoderLog2MaxFrameNum
                     << "log2_poc=" << mEncoderLog2MaxPocLsb
                     << "poc_type=" << mEncoderPocType;
        }
        // The per-segment unification decision used a value before the
        // encoder existed — a mismatch means benign seams may have been
        // misclassified; surface it loudly instead of degrading silently.
        // Compare against what the decision actually used: the probed value
        // when available, the legacy constant otherwise.
        int expectedLog2Poc = (mProbedEncoderPocType == 0 && mProbedEncoderLog2PocLsb >= 4)
            ? mProbedEncoderLog2PocLsb : kExpectedEncoderLog2PocLsb;
        if (mEncoderPocType == 0 && mEncoderLog2MaxPocLsb != expectedLog2Poc) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("Encoder log2_max_poc_lsb %1 differs from the value the "
                        "branch decision used (%2, probed %3) - POC-domain "
                        "bridge decision may be wrong")
                    .arg(mEncoderLog2MaxPocLsb).arg(expectedLog2Poc)
                    .arg(mProbedEncoderLog2PocLsb));
        }
        if (mProbedEncoderPocType >= 0 && mEncoderPocType != mProbedEncoderPocType) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("Encoder poc_type %1 differs from probed %2")
                    .arg(mEncoderPocType).arg(mProbedEncoderPocType));
        }
        ctx.encoderSpsParsed = true;
    }
}

// ----------------------------------------------------------------------------
// Apply SPS-related transforms to an encoder packet.
// Path 1 (SPS Unification, H.264 only): rewrite slice headers to use source
//   SPS parameters. PPS(id=1) is kept inline (patched by
//   ttRewriteEncoderPacketForSourceSps).
// Path 2 (Standard, H.264 only): patch SPS NALs in the access unit to add
//   bitstream_restriction with max_num_reorder_frames.
// Path 3 (HEVC or pre-parse): return input unchanged.
// ----------------------------------------------------------------------------
QByteArray TTESSmartCut::transformEncoderPacket(ReencodeContext& ctx, const QByteArray& rawData)
{
    QByteArray encodedData = rawData;

    if (mHevcSeamFix && mParser.codecType() == NALU_CODEC_H265) {
        // First packet: capture encoder SPS/PPS (x265 sends its parameter
        // sets inline with the first keyframe packet).
        if (!mHevcSeamCtx.encHeadersParsed) {
            int sc = 0, t = 0;
            for (int i = 0; (i = ttHevcNextNal(rawData, i, &sc, &t)) >= 0; i += sc + 1) {
                if (t == 33)
                    mHevcSeamCtx.encSps = parseHevcSpsSeamInfo(rawData.mid(i));
                else if (t == 34)
                    mHevcSeamCtx.encPps = parseHevcPpsSeamInfo(rawData.mid(i));
            }
            mHevcSeamCtx.encHeadersParsed =
                mHevcSeamCtx.encSps.valid && mHevcSeamCtx.encPps.valid
                && mHevcSeamCtx.encPps.numExtraSliceHeaderBits == 0;
            if (!mHevcSeamCtx.encHeadersParsed) {
                mHevcSeamRewriteFailed = true;
                mHevcSeamFailReason =
                    QStringLiteral("encoder parameter sets unparsable");
                return QByteArray();
            }
        }
        QString reason;
        QByteArray rewritten = rewriteHevcEncoderPacket(
            rawData, mHevcSeamCtx, mEncoderPacketsWritten, &reason);
        if (rewritten.isEmpty()) {
            mHevcSeamRewriteFailed = true;
            mHevcSeamFailReason = reason;
            return QByteArray();
        }
        return rewritten;
    }

    if (mSpsUnification && mParser.codecType() == NALU_CODEC_H264 && ctx.encoderSpsParsed) {
        // SPS Unification: extract encoder PPS from first packet, rewrite all slices
        if (!ctx.encPpsParsed) {
            // Extract encoder PPS for slice header field layout.
            // The PPS(id=1) is written INLINE before the first encoder slice
            // (not at ES start — that corrupts MKV NAL parsing).
            QByteArray encPpsNal = ttExtractPpsFromPacket(rawData);
            if (!encPpsNal.isEmpty()) {
                ctx.encPpsForRewrite = ttParseH264PpsInfo(encPpsNal);
                ctx.encPpsParsed = true;

                // PPS(id=1) is now kept inside each encoder packet
                // by ttRewriteEncoderPacketForSourceSps (patches pps_id inline).
                // No separate PPS write needed.
                if (TTSettings::instance()->logSmartCut())
                    qDebug() << "      SPS Unification: encoder PPS parsed, pps_id=1 kept inline";
            }
        }

        // Rewrite encoder packet: strip SPS/PPS, rewrite slice NALs
        if (ctx.encPpsParsed) {
            encodedData = ttRewriteEncoderPacketForSourceSps(
                encodedData,
                mEncoderLog2MaxFrameNum, mEncoderLog2MaxPocLsb, mEncoderFrameMbsOnly,
                mLog2MaxFrameNum, mLog2MaxPocLsb, mFrameMbsOnly,
                ctx.encPpsForRewrite, 1, mEncoderPacketsWritten,  // newPpsId=1
                mSpsUnificationPocBase);
        }
    } else if (mReorderDelay > 0 && mParser.codecType() == NALU_CODEC_H264) {
        // Standard path: just patch SPS reorder frames
        QByteArray patched = ttPatchSpsNalsInAccessUnit(encodedData, mReorderDelay, mParser.isPAFF());
        if (patched != encodedData)
            encodedData = patched;
    }

    return encodedData;
}

// ----------------------------------------------------------------------------
// Pending-buffer write: flush the previously buffered packet to outFile,
// then store the new transformedData as the pending packet. The last packet
// stays in ctx.pendingPacket for applyPocDomainFix.
// ----------------------------------------------------------------------------
bool TTESSmartCut::bufferAndWriteEncoderPacket(ReencodeContext& ctx,
                                                const QByteArray& transformedData)
{
    // Write previously buffered packet, buffer current one
    if (!ctx.pendingPacket.isEmpty()) {
        if (ctx.outFile.write(ctx.pendingPacket) != ctx.pendingPacket.size()) {
            setError("Failed to write encoded data");
            return false;
        }
    }
    ctx.pendingPacket = transformedData;

    // Display-order tracking: with bf=0 the encoder emits packets 1:1 in
    // submission order, so packet k belongs to ctx.framesToEncode[k] (whose
    // AVFrame::pts carries the source AU index). The pending-packet buffer
    // only delays writes by one packet - the write ORDER stays FIFO, so
    // recording at receive time matches write order.
    if (mOutputDisplayOrderValid) {
        if (ctx.packetsReceived < ctx.encodeAuOrder.size()) {
            const int au = ctx.encodeAuOrder[ctx.packetsReceived];
            appendOutputDisplay(mDisplayMap.decodeToDisplay(au), au);
        } else {
            // Encoder produced more packets than frames submitted - cannot map.
            mOutputDisplayOrderValid = false;
            mOutputDisplayOrder.clear();
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                "output display-order tracking invalidated (encoder packet "
                "count exceeds submitted frames)");
        }
    }

    ctx.packetsReceived++;
    mEncoderPacketsWritten++;
    return true;
}

// ----------------------------------------------------------------------------
// Main encode pass: iterate ctx.framesToEncode, send to encoder, drain
// output packets. Each output packet flows through parseEncoderSpsFromPacket
// (one-shot) -> transformEncoderPacket -> bufferAndWriteEncoderPacket.
// Frees each input frame after submission to encoder. Marks the first frame
// as a keyframe.
// ----------------------------------------------------------------------------
bool TTESSmartCut::runEncodePass(ReencodeContext& ctx)
{
    // Index-based iteration is deliberate: `frame` below is a REFERENCE into
    // the ctx.framesToEncode slot (not a value copy), so av_frame_free(&frame)
    // nulls the slot itself. ~ReencodeContext's cleanup loop later frees every
    // remaining pointer in this list; av_frame_free() is a no-op on a null
    // entry, so a slot freed here can never be double-freed by the destructor,
    // regardless of which iteration an early return happens on. A prior
    // version of this loop used `for (AVFrame* frame : ctx.framesToEncode)`,
    // which binds by value: freeing the local copy left the (now dangling)
    // pointer sitting in the QList slot, and any early return afterwards
    // (pre-existing avcodec_send_frame/receive_packet/write failures, or an
    // abort) handed that dangling pointer to the destructor a second time --
    // a real, reproduced double free / heap corruption, not just a leak.
    for (int fi = 0; fi < ctx.framesToEncode.size(); ++fi) {
        AVFrame*& frame = ctx.framesToEncode[fi];

        // Consistent with the other early returns in this loop below (none
        // of which free `frame` either): leave ownership of the current and
        // all not-yet-consumed entries to ~ReencodeContext. That is now safe
        // for every entry, not just this one, because of the null-after-free
        // pattern above.
        if (checkAbort()) return false;

        if (ctx.firstFrame) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 0, 0)
            frame->key_frame = 1;
#endif
            ctx.firstFrame = false;
        } else {
            frame->pict_type = AV_PICTURE_TYPE_NONE;
        }

        frame->pts = ctx.framesSent;

        int ret = avcodec_send_frame(mEncoder, frame);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            QString errStr = avErrStr(ret);
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("avcodec_send_frame failed: %1").arg(errStr));
            setError(QString("Encoding failed: %1").arg(errStr));
            return false;
        }
        ctx.framesSent++;

        // Progress inside the encode pass: without this a whole-GOP encode
        // is silent and the dialog looks frozen (observed stall 2026-08-09).
        if (ctx.framesSent % 10 == 0) {
            emitCutProgress(
                tr("Encoding segment %1/%2...").arg(mCurrentSegment).arg(mTotalSegments),
                ctx.framesSent);
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            // Same reasoning as in decodeFramesIntoList: a break truncated
            // the encode pass silently after part of the frame list was
            // already processed, and the function still returned true.
            setError(QString("av_packet_alloc failed"));
            return false;
        }
        while (true) {
            ret = avcodec_receive_packet(mEncoder, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                    QString("avcodec_receive_packet failed: %1").arg(avErrStr(ret)));
                av_packet_free(&packet);
                return false;
            }
            QByteArray rawData(reinterpret_cast<char*>(packet->data), packet->size);

            parseEncoderSpsFromPacket(ctx, rawData);
            QByteArray transformed = transformEncoderPacket(ctx, rawData);
            if (transformed.isEmpty() && mHevcSeamRewriteFailed) {
                av_packet_free(&packet);
                return false;    // seam rewrite failed -> rollback in caller
            }
            if (!bufferAndWriteEncoderPacket(ctx, transformed)) {
                av_packet_free(&packet);
                return false;
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
    }
    ctx.framesToEncode.clear();
    return true;
}

// ----------------------------------------------------------------------------
// Encoder flush: send NULL frame, drain remaining packets. Each packet
// flows through the same parseEncoderSpsFromPacket → transformEncoderPacket
// → bufferAndWriteEncoderPacket chain as runEncodePass.
// ----------------------------------------------------------------------------
bool TTESSmartCut::flushEncoder(ReencodeContext& ctx)
{
    avcodec_send_frame(mEncoder, nullptr);
    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;
    while (true) {
        if (checkAbort()) { av_packet_free(&packet); return false; }

        int ret = avcodec_receive_packet(mEncoder, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("avcodec_receive_packet (flush) failed: %1").arg(avErrStr(ret)));
            break;
        }
        QByteArray rawData(reinterpret_cast<char*>(packet->data), packet->size);

        parseEncoderSpsFromPacket(ctx, rawData);
        QByteArray transformed = transformEncoderPacket(ctx, rawData);
        if (transformed.isEmpty() && mHevcSeamRewriteFailed) {
            av_packet_free(&packet);
            return false;    // seam rewrite failed -> rollback in caller
        }
        if (!bufferAndWriteEncoderPacket(ctx, transformed)) {
            av_packet_free(&packet);
            return false;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return true;
}

// ----------------------------------------------------------------------------
// POC domain mismatch fix: patch poc_lsb in the last encoder packet to
// prevent PicOrderCntMsb wrap at the re-encode→stream-copy transition.
// Only needed for H.264 with poc_type=0 when stream-copy follows. With SPS
// Unification, poc_lsb is already widened to source domain, so we use source
// params for both reading and patching.
// ----------------------------------------------------------------------------
void TTESSmartCut::applyPocDomainFix(ReencodeContext& ctx)
{
    int actualScStart = (ctx.adjustedStreamCopyStart && *ctx.adjustedStreamCopyStart >= 0)
        ? *ctx.adjustedStreamCopyStart : ctx.streamCopyStartFrame;

    // Determine which params to use for the pending packet's poc_lsb
    int pendingFnWidth = mSpsUnification ? mLog2MaxFrameNum : mEncoderLog2MaxFrameNum;
    int pendingPocWidth = mSpsUnification ? mLog2MaxPocLsb : mEncoderLog2MaxPocLsb;
    bool pendingFmOnly = mSpsUnification ? mFrameMbsOnly : mEncoderFrameMbsOnly;

    if (ctx.pendingPacket.isEmpty() || mEncoderPocType != 0 ||
        pendingPocWidth <= 0 || mLog2MaxPocLsb <= 0 ||
        actualScStart < 0 || mParser.codecType() != NALU_CODEC_H264) {
        return;
    }

    // Read source's first stream-copy frame poc_lsb
    QByteArray srcAU = mParser.readAccessUnitData(actualScStart);
    int srcPocLsb = ttReadPocLsbFromAU(srcAU, mLog2MaxFrameNum,
                                      mLog2MaxPocLsb, mFrameMbsOnly);

    // Read encoder's last slice poc_lsb (already in source domain if SPS unification)
    int encPocLsb = ttReadPocLsbFromAU(ctx.pendingPacket, pendingFnWidth,
                                      pendingPocWidth, pendingFmOnly);

    if (srcPocLsb < 0 || encPocLsb < 0) return;

    int srcMaxPocLsb = 1 << mLog2MaxPocLsb;
    int diff = qAbs(srcPocLsb - encPocLsb);
    if (diff <= srcMaxPocLsb / 2) return;

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
            if (TTSettings::instance()->logSmartCut()) {
                qDebug() << "      POC domain fix: modulo re-wrap detected,"
                         << "using search result" << newPocLsb;
            }
        } else {
            // Must never happen anymore: the per-segment unification decision
            // (pocDomainBridgeable) routes exactly these seams through SPS
            // unification, where the patch domain equals the source domain.
            // Escalate as a real logger warning so diag gates treat it as FAIL
            // instead of degrading silently (first copied GOP would be lost).
            qWarning() << "      POC domain fix: WARNING no safe poc_lsb"
                       << "found in range [0," << patchMaxPocLsb << ")";
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("POC domain fix: no safe poc_lsb in range [0,%1) - "
                        "seam misclassified as bridgeable, first copied GOP will "
                        "be dropped by the decoder")
                    .arg(patchMaxPocLsb));
        }
    }

    if (TTSettings::instance()->logSmartCut()) {
        qDebug() << "      POC domain fix: encoder poc_lsb=" << encPocLsb
                 << "source poc_lsb=" << srcPocLsb
                 << "diff=" << diff << "> MaxPocLsb/2=" << srcMaxPocLsb / 2
                 << "-> patching to" << newPocLsb;
    }

    ctx.pendingPacket = ttPatchPocLsbInPacket(ctx.pendingPacket,
        pendingFnWidth, pendingPocWidth,
        pendingFmOnly, static_cast<uint32_t>(newPocLsb));
}

// ----------------------------------------------------------------------------
// Final flush: write the buffered last packet (post-poc-patch) to outFile.
// ----------------------------------------------------------------------------
bool TTESSmartCut::writePendingPacket(ReencodeContext& ctx)
{
    if (ctx.pendingPacket.isEmpty()) return true;
    if (ctx.outFile.write(ctx.pendingPacket) != ctx.pendingPacket.size()) {
        setError("Failed to write last encoded packet");
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Bridge the encoder AUs' frame_num sequence to the stream-copy start.
// Returns the delta to add to every copied AU's frame_num (0 = don't patch).
//  - IDR copy-start (parser's isIDR, NOT fn==0): 0. An IDR resets
//    PrevRefFrameNum and requires frame_num == 0 (H.264 7.4.3); patching it
//    violated the spec (the old standard path did; libav tolerated it).
//    A non-IDR keyframe that wrapped to fn 0 is now bridged correctly
//    (the old fn>0 guard in the unification path skipped it).
//  - Unreadable frame_num: 0 (don't guess).
//  - Otherwise: lastEncoderFn - firstCopyFn, where lastEncoderFn is
//    ((mEncoderPacketsWritten - 1) mod 2^encLog2Fn) + 1 — the wrap
//    correction that previously existed twice, one copy fixed, one stale.
//    The delta may be negative; the consumer (ttPatchFrameNumInAU)
//    normalizes modulo maxFrameNum.
// encLog2Fn: frame_num width of the ENCODER AUs as written to the output —
// source width when SPS unification rewrote the slices, encoder SPS width
// otherwise. The caller knows which; this is the one legitimate difference
// between the two call sites.
// ----------------------------------------------------------------------------
int TTESSmartCut::bridgeFrameNum(int scStartAU, int encLog2Fn)
{
    if (mLog2MaxFrameNum <= 0 || mParser.codecType() != NALU_CODEC_H264)
        return 0;

    TTAccessUnit au = mParser.accessUnitAt(scStartAU);
    if (au.isIDR)
        return 0;   // IDR resets PrevRefFrameNum; never patch an IDR's fn

    QByteArray firstAU = mParser.readAccessUnitData(scStartAU);
    int firstScFrameNum = ttReadFrameNumFromAU(firstAU, mLog2MaxFrameNum);
    if (firstScFrameNum < 0)
        return 0;   // unreadable — don't guess

    int lastEncFrameNum = mEncoderPacketsWritten;
    if (encLog2Fn > 0 && mEncoderPacketsWritten > 0) {
        int encMaxFrameNum = 1 << encLog2Fn;
        lastEncFrameNum = ((mEncoderPacketsWritten - 1) % encMaxFrameNum) + 1;
    }
    return lastEncFrameNum - firstScFrameNum;
}

// ----------------------------------------------------------------------------
// Write the codec's EOS NAL to flush the decoder DPB at a splice point
// (H.264 type 11, H.265 type 37). Single home for the codec dispatch that
// previously existed open-coded at four emit sites.
// ----------------------------------------------------------------------------
void TTESSmartCut::writeEos(QFile& outFile) const
{
    if (mParser.codecType() == NALU_CODEC_H265)
        outFile.write(kEosNalH265, sizeof(kEosNalH265));
    else
        outFile.write(kEosNalH264, sizeof(kEosNalH264));
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
                QByteArray patched = ttPatchH264SpsReorderFrames(sps, patchReorderFrames, mParser.isPAFF());
                if (!patched.isEmpty())
                    sps = patched;
                else
                    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                        QString("WARNING: SPS patch failed, using original SPS"));
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
    ttSetLastError(mLastError, __FILE__, __LINE__, "TTESSmartCut", error, true);
}

// ----------------------------------------------------------------------------
// Poll point for cooperative abort. Returns true (and records the abort)
// when a requestAbort() arrived; every caller returns false through its
// normal error path afterwards.
// ----------------------------------------------------------------------------
bool TTESSmartCut::checkAbort()
{
    if (!mAbortRequested.load(std::memory_order_relaxed)) return false;
    mWasAborted = true;
    // Deliberately not setError(): a user cancel is not a failure and must
    // not read as one in the log (setError() logs at ERROR level via
    // TTMessageLogger). Set mLastError directly so lastError() still
    // contains "aborted", without the error-level log line.
    mLastError = "aborted by user";
    return true;
}
