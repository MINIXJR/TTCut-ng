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

    // Statistics from last cut
    int framesStreamCopied() const { return mFramesStreamCopied; }
    int framesReencoded() const { return mFramesReencoded; }
    int64_t bytesWritten() const { return mBytesWritten; }

    // Error handling
    QString lastError() const { return mLastError; }

signals:
    void progressChanged(int percent, const QString& message);

private:
    // State
    bool mIsInitialized;
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

    // Statistics
    int mFramesStreamCopied;
    int mFramesReencoded;
    int64_t mBytesWritten;

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
    bool processSegment(QFile& outFile, const TTCutSegmentInfo& segment);

    // Stream-copy NAL units (no re-encoding)
    bool streamCopyFrames(QFile& outFile, int startFrame, int endFrame);

    // Re-encode frames (for partial GOPs)
    bool reencodeFrames(QFile& outFile, int startFrame, int endFrame);

    // Helper: decode frame from NAL data
    bool decodeFrame(const QByteArray& nalData, AVFrame* frame);

    // Helper: encode frame to NAL units
    QByteArray encodeFrame(AVFrame* frame, bool forceKeyframe);

    // Helper: write NAL unit with start code
    bool writeNalUnit(QFile& outFile, const QByteArray& nalData);

    // Helper: filter encoder output (remove SPS/PPS, keep only slices)
    QByteArray filterEncoderOutput(const QByteArray& data);

    // Helper: write parameter sets (SPS/PPS/VPS)
    bool writeParameterSets(QFile& outFile);

    // Time/frame conversion
    int timeToFrame(double timeSeconds) const;
    double frameToTime(int frameIndex) const;
};

#endif // TTESSMARTCUT_H
