/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttessmartcut.h                                                  */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2026  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTESSMARTCUT
// Smart Cut engine for H.264/H.265 elementary streams
// Re-encodes only partial GOPs at cut boundaries, stream-copies the rest
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#ifndef TTESSMARTCUT_H
#define TTESSMARTCUT_H

#include <QString>
#include <QList>
#include <QPair>
#include <QFile>
#include <QObject>

#include "../avstream/ttnaluparser.h"

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
    int startFrame;              // First frame to keep (display order)
    int endFrame;                // Last frame to keep (display order)

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

    // Error handling
    QString lastError() const { return mLastError; }

signals:
    void progressChanged(int percent, const QString& message);

private:
    // State
    bool mIsInitialized;
    int mPresetOverride;     // -1 = use TTCut settings, 0-8 = override preset
    QString mInputFile;
    double mFrameRate;

    // NAL parser
    TTNaluParser mParser;

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
    int mEncoderPacketsWritten;     // track encoder packets for PPS injection
    bool mSyntheticPpsNeeded;       // need encoder PPS at ES start
    qint64 mPpsReserveOffset;       // file offset of reserved PPS space

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
                        int* actualStartAU = nullptr);

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
