/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2024                                                      */
/* FILE     : ttffmpegwrapper.h                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                           DATE: 01/2025  */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFFMPEGWRAPPER
// Wrapper class for libav/ffmpeg functionality
// Used for H.264/H.265 stream analysis and frame-accurate cutting
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

#ifndef TTFFMPEGWRAPPER_H
#define TTFFMPEGWRAPPER_H

#include <QString>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QImage>

// Forward declarations for libav types (avoid including C headers in .h)
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct AVFrame;
struct SwsContext;

// ----------------------------------------------------------------------------
// Stream information structure
// ----------------------------------------------------------------------------
struct TTStreamInfo {
    int streamIndex;
    int codecType;          // AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, etc.
    int codecId;            // AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, etc.
    QString codecName;      // "h264", "hevc", "mpeg2video", etc.

    // Video specific
    int width;
    int height;
    double frameRate;
    int64_t bitRate;
    int profile;
    int level;

    // Audio specific
    int sampleRate;
    int channels;
    int bitsPerSample;

    // Common
    int64_t duration;       // in stream timebase
    int64_t numFrames;      // estimated frame count
};

// ----------------------------------------------------------------------------
// Frame information for frame index
// ----------------------------------------------------------------------------
struct TTFrameInfo {
    int64_t pts;            // Presentation timestamp
    int64_t dts;            // Decode timestamp
    int64_t fileOffset;     // Byte offset in file
    int64_t packetSize;     // Packet size in bytes
    int frameType;          // AV_PICTURE_TYPE_I, _P, _B
    bool isKeyframe;        // IDR frame (H.264) or keyframe
    int gopIndex;           // Which GOP this frame belongs to
    int frameIndex;         // Sequential frame number
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

// ----------------------------------------------------------------------------
// Video codec types
// ----------------------------------------------------------------------------
enum TTVideoCodecType {
    CODEC_UNKNOWN = 0,
    CODEC_MPEG2,
    CODEC_H264,
    CODEC_H265
};

// ----------------------------------------------------------------------------
// Container types
// ----------------------------------------------------------------------------
enum TTContainerType {
    CONTAINER_UNKNOWN = 0,
    CONTAINER_ELEMENTARY,   // Elementary stream (.m2v, .h264, .h265)
    CONTAINER_TS,           // MPEG Transport Stream (.ts, .m2ts)
    CONTAINER_PS,           // MPEG Program Stream (.mpg, .mpeg)
    CONTAINER_MKV,          // Matroska (.mkv)
    CONTAINER_MP4           // ISOBMFF (.mp4, .m4v)
};

// ----------------------------------------------------------------------------
// Output container types for muxing
// ----------------------------------------------------------------------------
enum TTOutputContainer {
    OUTPUT_TS = 0,          // MPEG Transport Stream
    OUTPUT_MKV,             // Matroska (via mkvmerge)
    OUTPUT_MP4,             // ISOBMFF (via ffmpeg)
    OUTPUT_ELEMENTARY       // No container, just ES
};

// ----------------------------------------------------------------------------
// TTFFmpegWrapper class
// ----------------------------------------------------------------------------
class TTFFmpegWrapper : public QObject
{
    Q_OBJECT

public:
    TTFFmpegWrapper();
    ~TTFFmpegWrapper();

    // Initialize/cleanup libav
    static void initializeFFmpeg();
    static void cleanupFFmpeg();

    // Open/close media file
    bool openFile(const QString& filePath);
    void closeFile();
    bool isOpen() const { return mFormatCtx != nullptr; }

    // Get stream information
    int getStreamCount() const;
    TTStreamInfo getStreamInfo(int streamIndex) const;
    int findBestVideoStream() const;
    int findBestAudioStream() const;

    // Detect video codec type
    TTVideoCodecType detectVideoCodec() const;
    static QString codecTypeToString(TTVideoCodecType type);

    // Detect container type (used by Smart Cut)
    TTContainerType detectContainer() const;
    static QString containerTypeToString(TTContainerType type);

    // Build frame index (for H.264/H.265)
    bool buildFrameIndex(int videoStreamIndex = -1);
    const QList<TTFrameInfo>& frameIndex() const { return mFrameIndex; }
    int frameCount() const { return mFrameIndex.size(); }

    // Build GOP index
    bool buildGOPIndex();
    const QList<TTGOPInfo>& gopIndex() const { return mGOPIndex; }
    int gopCount() const { return mGOPIndex.size(); }

    // Frame access
    TTFrameInfo frameAt(int index) const;
    int findFrameByPts(int64_t pts) const;
    int findGOPForFrame(int frameIndex) const;

    // Utility functions
    double ptsToSeconds(int64_t pts, int streamIndex) const;
    int64_t secondsToPts(double seconds, int streamIndex) const;
    QString formatTimestamp(int64_t pts, int streamIndex) const;

    // Frame decoding for preview
    bool seekToFrame(int frameIndex);
    QImage decodeFrame(int frameIndex);
    QImage decodeCurrentFrame();
    int videoWidth() const;
    int videoHeight() const;

    // Segment extraction (for cutting)
    bool extractSegment(const QString& outputFile, int startFrame, int endFrame, bool reencode = false);
    bool concatenateSegments(const QString& outputFile, const QStringList& segmentFiles);

    // Smart cut using avcut approach (direct writing, no global headers, GOP-based)
    // cutList contains pairs of (startTime, endTime) in seconds - segments to KEEP
    bool smartCut(const QString& outputFile, const QList<QPair<double, double>>& cutList);

    // Elementary stream support
    // Wrap ES in MKV container with proper timestamps (using mkvmerge CLI)
    QString wrapElementaryStream(const QString& esFile, double frameRate = -1);

    // Wrap ES in MKV using libav directly (no external tools)
    // This gives full control over timestamps and avoids CLI tool quirks
    QString wrapElementaryStreamLibav(const QString& esFile, double frameRate = -1);

    // Smart Cut for ES - re-encodes only partial GOPs at cut-in, stream-copies rest
    // Allows frame-accurate cutting with minimal re-encoding
    bool smartCutElementaryStream(const QString& inputFile, const QString& audioFile,
                                   const QString& outputFile,
                                   const QList<QPair<double, double>>& cutList,
                                   double frameRate);

    // Smart Cut for ES V2 - using native NAL parser (no external CLI tools)
    // This is the preferred method for ES Smart Cut
    bool smartCutElementaryStreamV2(const QString& inputFile, const QString& audioFile,
                                     const QString& outputFile,
                                     const QList<QPair<double, double>>& cutList,
                                     double frameRate);

    // Byte-level ES cutting (legacy - starts at keyframes only)
    // Returns cut ES file - caller must mux to container if needed
    bool cutElementaryStream(const QString& inputFile, const QString& outputFile,
                             const QList<QPair<double, double>>& cutList);

    // Audio ES cutting - time-based with FFmpeg (ms-accurate)
    bool cutAudioStream(const QString& inputFile, const QString& outputFile,
                        const QList<QPair<double, double>>& cutList);

    // SRT subtitle cutting - text-based time filtering
    bool cutSrtSubtitle(const QString& inputFile, const QString& outputFile,
                        const QList<QPair<double, double>>& cutList);

    // Complete ES cutting workflow: video + audio + srt + mux
    // Takes video ES + audio ES files, cuts both, muxes to final output
    bool cutAndMuxElementaryStreams(const QString& videoES, const QString& audioES,
                                    const QString& outputFile,
                                    const QList<QPair<double, double>>& cutList,
                                    double frameRate = -1);

    // Error handling
    QString lastError() const { return mLastError; }

signals:
    void progressChanged(int percent, const QString& message);

private:
    // Libav contexts
    AVFormatContext* mFormatCtx;
    AVCodecContext* mVideoCodecCtx;
    SwsContext* mSwsCtx;        // For pixel format conversion
    AVFrame* mDecodedFrame;     // Reusable decoded frame
    AVFrame* mRgbFrame;         // Reusable RGB frame for QImage

    // Cached stream info
    int mVideoStreamIndex;
    int mAudioStreamIndex;
    int mCurrentFrameIndex;

    // Frame and GOP indices
    QList<TTFrameInfo> mFrameIndex;
    QList<TTGOPInfo> mGOPIndex;

    // Error handling
    QString mLastError;
    void setError(const QString& error);

    // Helper functions
    static QString avErrorToString(int errnum);
    int getFrameType(AVPacket* packet, AVCodecContext* codecCtx);
};

#endif // TTFFMPEGWRAPPER_H
