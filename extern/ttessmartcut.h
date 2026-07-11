/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTESSMARTCUT
// Smart Cut engine for H.264/H.265 elementary streams
// Re-encodes only partial GOPs at cut boundaries, stream-copies the rest
// ----------------------------------------------------------------------------

#ifndef TTESSMARTCUT_H
#define TTESSMARTCUT_H

#include <QString>
#include <QList>
#include <QPair>
#include <QFile>
#include <QObject>

#include "../avstream/ttnaluparser.h"
#include "../avstream/ttdisplayordermap.h"

// Forward declarations for libav types
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// ----------------------------------------------------------------------------
// Cut segment information
// ----------------------------------------------------------------------------
struct TTCutSegmentInfo {
    int startFrame;     // First AU to keep (decode order; = displayToDecode(startDisplay))
    int endFrame;       // Last AU to keep (decode order)
    int startDisplay;   // First display position to keep (UI cut-in, Direction A)
    int endDisplay;     // Last display position to keep (UI cut-out)

    // Analyzed info
    int cutInGOP;                // GOP containing cut-in point
    int cutOutGOP;               // GOP containing cut-out point
    bool needsReencodeAtStart;   // Cut-in is not at keyframe
    bool needsReencodeAtEnd;     // Cut-out is at B-frame (optional)

    // Frame ranges
    int reencodeStartFrame;      // First frame to re-encode (-1 if none)
    int reencodeEndFrame;        // Last frame to re-encode (-1 if none)
    int streamCopyStartFrame;    // First frame to stream-copy
    int streamCopyEndFrame;      // Last frame to stream-copy

    // Frame-accurate cut-OUT (tail re-encode). When needsReencodeAtEnd is true,
    // stream-copy ends at streamCopyEndFrame (= tailStartFrame-1) and the tail
    // GOP [tailStartFrame .. ] is re-encoded keeping only display <= endDisplay.
    int tailStartFrame;          // AU/decode index: keyframe starting the re-encoded tail (-1 if none)
};

// ----------------------------------------------------------------------------
// TTESSmartCut class
// ----------------------------------------------------------------------------
class TTESSmartCut : public QObject
{
    Q_OBJECT

public:
    TTESSmartCut();
    ~TTESSmartCut();

    // Override encoder preset (0-8, -1 = use TTCut settings)
    void setPresetOverride(int presetIndex) { mPresetOverride = presetIndex; }

    // Inject the display-order <-> decode-order (AU) map. When set (Task 6),
    // smartCutFrames uses it instead of building its own from the input file.
    void setDisplayOrderMap(const TTDisplayOrderMap& map) { mDisplayMap = map; }

    // Initialize with ES file
    bool initialize(const QString& esFile, double frameRate = -1);
    void cleanup();
    bool isInitialized() const { return mIsInitialized; }

    // File info
    QString inputFile() const { return mInputFile; }
    TTNaluCodecType codecType() const;
    int frameCount() const;
    int gopCount() const;
    double frameRate() const { return mFrameRate; }

    // Smart Cut operation
    // cutList: pairs of (startTime, endTime) in seconds - segments to KEEP
    bool smartCut(const QString& outputFile,
                  const QList<QPair<double, double>>& cutList);

    // Frame-based version
    // cutFrames: pairs of (startFrame, endFrame) - frames to KEEP
    bool smartCutFrames(const QString& outputFile,
                        const QList<QPair<int, int>>& cutFrames);

    // Analyze cut points without performing cut
    QList<TTCutSegmentInfo> analyzeCutPoints(const QList<QPair<int, int>>& cutFrames);

    // Check if SPS changes between frame and frame+1 (or frame-1 for cutIn)
    bool hasSPSChangeAtBoundary(int frameIndex, bool isCutOut);

    // B-frame reorder delay (frames)
    int reorderDelay() const;

    // Statistics from last cut
    int framesStreamCopied() const { return mFramesStreamCopied; }
    int framesReencoded() const { return mFramesReencoded; }
    int64_t bytesWritten() const { return mBytesWritten; }

    // Actual output frame ranges from last smartCutFrames() call.
    // B-frame reorder delay may shift segment start AUs forward,
    // so actual start frames can differ from the requested cut-in frames.
    QList<QPair<int, int>> actualOutputFrameRanges() const { return mActualOutputRanges; }

    // Display position (frame units, output-local) of each mux packet in the
    // written ES, in write order. Empty when tracking was invalidated (any
    // anomaly) — callers then keep the muxer's legacy linear PTS behavior.
    QVector<int> outputDisplayOrder() const;

    // Error handling
    QString lastError() const { return mLastError; }

signals:
    void progressChanged(int percent, const QString& message);

private:
    struct ReencodeContext;  // forward; defined in ttessmartcut.cpp

    // State
    bool mIsInitialized;
    int mPresetOverride;     // -1 = use TTCut settings, 0-8 = override preset
    QString mInputFile;
    double mFrameRate;

    // NAL parser
    TTNaluParser mParser;

    // Display-order <-> decode-order (AU) map. Injected via setDisplayOrderMap
    // (Task 6) or built standalone from mInputFile in smartCutFrames.
    TTDisplayOrderMap mDisplayMap;

    // Libav contexts for decode/encode
    AVCodecContext* mDecoder;
    AVCodecContext* mEncoder;

    // Decoded frame parameters (set after first successful decode)
    // These are used to initialize the encoder with correct parameters
    int mDecodedWidth;
    int mDecodedHeight;
    int mDecodedPixFmt;  // AVPixelFormat
    bool mInterlaced;    // true if source is interlaced (MBAFF/PAFF)
    bool mTopFieldFirst; // true if TFF, false if BFF

    // B-frame reorder delay (measured from decoder)
    int mReorderDelay;

    // H.264 frame_num field width in bits (log2_max_frame_num_minus4 + 4)
    int mLog2MaxFrameNum;

    // Source SPS: POC-related fields for poc_lsb domain mismatch fix
    int mLog2MaxPocLsb;       // log2_max_pic_order_cnt_lsb (0 if poc_type != 0)
    int mPocType;             // pic_order_cnt_type (0, 1, or 2; -1 if unknown)
    bool mFrameMbsOnly;       // frame_mbs_only_flag

    // Encoder SPS: parsed from encoder extradata for poc_lsb patching
    int mEncoderLog2MaxFrameNum;
    int mEncoderLog2MaxPocLsb;
    int mEncoderPocType;
    bool mEncoderFrameMbsOnly;

    // SPS Unification mode: rewrite encoder output to match source SPS
    // Set by processSegment() for PAFF H.264, checked in reencodeFrames()
    bool mSpsUnification;
    QFile* mSpsUnificationOutFile;  // output file for encoder PPS injection
    // poc_lsb of the stream-copy start AU when unification runs for a
    // non-bridgeable POC seam; -1 otherwise (PAFF keeps the legacy linear
    // POC numbering for byte-identical output).
    int mSpsUnificationPocAnchor;   // source poc_lsb at copy start, or -1
    int mSpsUnificationPocBase;     // poc_lsb for encoder frameIndex 0, or -1
    int mEncoderPacketsWritten;     // track encoder packets for PPS injection

    // Output display-order tracking for the MKV muxer. One entry per written
    // parser AU (= one frame; TTNaluParser merges PAFF field pairs), in write
    // order, holding the SOURCE display index; outputDisplayOrder() converts
    // to output-local rank. Invalidated on any anomaly — muxer then falls
    // back to legacy linear PTS.
    QVector<int> mOutputDisplayOrder;
    bool mOutputDisplayOrderValid;
    void appendOutputDisplay(int mapDisplayIndex, int srcAuIndex);

    // Encoder PTS counter (reset per segment in setupEncoder)
    int64_t mEncoderPts;

    // Statistics
    int mFramesStreamCopied;
    int mFramesReencoded;
    int64_t mBytesWritten;

    // Progress tracking
    int mTotalFrames;
    int mCurrentSegment;
    int mTotalSegments;

    // Actual output frame ranges (start AU may differ from requested due to B-frame reorder)
    QList<QPair<int, int>> mActualOutputRanges;

    // Error handling
    QString mLastError;
    void setError(const QString& error);

    // Internal methods

    // Setup decoder/encoder
    bool setupDecoder();
    bool setupEncoder();
    void freeDecoder();
    void freeEncoder();

    // Process a single segment
    // frameNumDelta: H.264 frame_num offset for inter-segment continuity
    // actualStartAU: output — actual first AU written (may differ from segment.startFrame)
    bool processSegment(QFile& outFile, const TTCutSegmentInfo& segment,
                        int& frameNumDelta, int* actualStartAU = nullptr);

    // Stream-copy NAL units (no re-encoding)
    // patchReorderFrames > 0: patch inline H.264 SPS NALs with max_num_reorder_frames
    // frameNumDelta != 0: patch H.264 slice frame_num for inter-segment continuity
    bool streamCopyFrames(QFile& outFile, int startFrame, int endFrame,
                          int patchReorderFrames = 0, int frameNumDelta = 0,
                          int neutralizeMmcoFrames = 0);

    // Re-encode frames (for partial GOPs)
    // streamCopyStartFrame: AU index where stream-copy begins after re-encode (-1 if none)
    // adjustedStreamCopyStart: output — if re-encode consumed frames past streamCopyStartFrame,
    //   this is set to the new stream-copy start (next keyframe). -1 if unchanged.
    // actualStartAU: output — the actual first AU that was encoded (may differ from startFrame
    //   due to B-frame display-order mapping). -1 if unchanged.
    bool reencodeFrames(QFile& outFile, int startFrame, int endFrame,
                        int streamCopyStartFrame, int* adjustedStreamCopyStart = nullptr,
                        int* actualStartAU = nullptr, int startDisplay = -1,
                        int endDisplay = -1, bool tailMode = false);

    // Re-encode the tail GOP [tailStartFrame ..] keeping only frames displaying
    // <= endDisplay; forced-IDR closed sub-segment (frame-accurate cut-out).
    bool reencodeTail(QFile& outFile, int tailStartFrame, int endDisplay);

    // Compute decode range for re-encoding: decodeStart with runway extension,
    // decodeEnd with pre-extension to next keyframe after streamCopyStartFrame.
    bool computeDecodeRange(ReencodeContext& ctx);

    // Reset decoder (flush) and recreate encoder for a new segment.
    // libx264's lookahead can't be restarted after flush, hence the recreate.
    bool resetDecoderForSegment(ReencodeContext& ctx);

    // One-shot encoder init based on a probed decoded frame's parameters.
    // Idempotent: returns true immediately if ctx.encoderInitialized is already true.
    bool ensureEncoderInitialized(ReencodeContext& ctx, AVFrame* probeFrame);

    // Decode all frames in [ctx.decodeStart, ctx.decodeEnd] plus drain on EOF,
    // appending each decoded AVFrame* to ctx.allDecodedFrames. Calls
    // ensureEncoderInitialized on the first received frame. Tracks
    // mReorderDelay from decoder->has_b_frames.
    bool decodeFramesIntoList(ReencodeContext& ctx);

    // Display-order based frame selection (PAFF and non-PAFF, unified), with
    // three modes:
    //   head/mixed: DISPLAY >= ctx.startDisplay && AU < stream-copy boundary
    //   pure re-encode (streamCopyStartFrame<0, endDisplay>=0):
    //               ctx.startDisplay <= DISPLAY <= ctx.endDisplay
    //   tail (ctx.tailMode): AU >= ctx.startFrame && DISPLAY <= ctx.endDisplay
    // Sets ctx.framesToEncode, ctx.streamCopyLimit,
    // *ctx.adjustedStreamCopyStart. The cut-in boundary-crossing extension
    // applies only to head/mixed mode.
    void selectFramesByDisplayOrder(ReencodeContext& ctx);

    // One-shot parse of encoder's H.264 SPS from the first encoder packet.
    // Idempotent: returns immediately if ctx.encoderSpsParsed. No-op for HEVC.
    void parseEncoderSpsFromPacket(ReencodeContext& ctx, const QByteArray& rawData);

    // Apply SPS Unification rewrite (H.264 + mSpsUnification + parsed) OR
    // SPS Reorder Patch (H.264 + mReorderDelay > 0). HEVC and pre-parse:
    // returns input unchanged.
    QByteArray transformEncoderPacket(ReencodeContext& ctx, const QByteArray& rawData);

    // Buffer the previous packet to outFile and store transformedData as the
    // new pending packet. Increments ctx.packetsReceived and mEncoderPacketsWritten.
    bool bufferAndWriteEncoderPacket(ReencodeContext& ctx, const QByteArray& transformedData);

    // Iterate ctx.framesToEncode, send each to encoder, drain output packets.
    // Calls parseEncoderSpsFromPacket + transformEncoderPacket +
    // bufferAndWriteEncoderPacket per output packet.
    bool runEncodePass(ReencodeContext& ctx);

    // Send NULL frame to encoder, drain remaining packets via the same
    // parse → transform → buffer-and-write chain as runEncodePass.
    bool flushEncoder(ReencodeContext& ctx);

    // POC domain mismatch fix: patch poc_lsb in ctx.pendingPacket to prevent
    // PicOrderCntMsb wrap at the re-encode→stream-copy transition. Only
    // applies to H.264 with poc_type=0 when stream-copy follows re-encode.
    void applyPocDomainFix(ReencodeContext& ctx);

    // Final flush: write the buffered last packet (post-poc-patch) to outFile.
    bool writePendingPacket(ReencodeContext& ctx);

    // Helper: decode frame from NAL data
    bool decodeFrame(const QByteArray& nalData, AVFrame* frame);

    // Helper: encode frame to NAL units
    QByteArray encodeFrame(AVFrame* frame, bool forceKeyframe);

    // Helper: write NAL unit with start code
    bool writeNalUnit(QFile& outFile, const QByteArray& nalData);

    // Helper: filter encoder output (remove SPS/PPS, keep only slices)
    QByteArray filterEncoderOutput(const QByteArray& data);

    // Helper: write parameter sets (SPS/PPS/VPS)
    // patchReorderFrames > 0: patch H.264 SPS to add bitstream_restriction
    // with max_num_reorder_frames = patchReorderFrames
    bool writeParameterSets(QFile& outFile, int patchReorderFrames = 0);

    // Time/frame conversion
    int timeToFrame(double timeSeconds) const;
    double frameToTime(int frameIndex) const;
};

#endif // TTESSMARTCUT_H
