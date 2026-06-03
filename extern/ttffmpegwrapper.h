/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFFMPEGWRAPPER
// Wrapper class for libav/ffmpeg functionality
// Used for H.264/H.265 stream analysis and frame-accurate cutting
// ----------------------------------------------------------------------------

#ifndef TTFFMPEGWRAPPER_H
#define TTFFMPEGWRAPPER_H

#include <QString>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QObject>
#include <QImage>

#include "../mpeg2decoder/ttmpeg2decoder.h"

// Forward declarations for libav types (avoid including C headers in .h)
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct AVFrame;
struct AVInputFormat;
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
    bool isFieldCoded;      // true if merged from two PAFF field packets
    // True decode-order index of the frame that decodeFrame() delivers for this
    // (decode-order) position. Differs from frameIndex when B-frame reorder
    // shifts decode vs display order. Lazily filled on first decode; -1 = unknown.
    // Used by playback to seek mpv to the actually-displayed frame's time.
    int deliveredDecodeIndex = -1;

    // Internal scratch for buildFrameIndex's PAFF post-processing pass:
    int  paffFrameNum  = -1;     // -1 = not a field; else frame_num for matching
    bool isBottomField = false;  // valid only when isFieldCoded == true
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
    void setAnalysisMode(bool enabled) { mAnalysisMode = enabled; }
    void setSearchMode(bool enabled) { mSearchMode = enabled; }
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
    void setFrameIndex(const QList<TTFrameInfo>& index) { mFrameIndex = index; }
    int frameCount() const { return mFrameIndex.size(); }
    bool isPAFF() const { return mIsPAFF; }
    int h264Log2MaxFrameNum() const { return mH264Log2MaxFrameNum; }

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

    /**
     * Decode a frame and expose its YUV420P planes via TFrameInfo.
     *
     * Sequential mode: monoton-forward calls (frameIndex+1 after frameIndex)
     * skip Re-Seek+DPB-Prefill. The first call (or any non-N+1 jump)
     * triggers full seek.
     *
     * Output TFrameInfo points to internal tight-packed buffers; valid
     * until next decode or closeVideoFile().
     *
     * Returns false on decode error or unsupported pixel format
     * (8-bit YUV420P only — 10-bit YUV420P10LE deferred).
     *
     * The Y/U/V plane pointers in outInfo are tightly packed
     * (no stride padding), unlike the libav data[] pointers.
     */
    bool decodeFrameYUV(int frameIndex, TFrameInfo& outInfo);

    QImage decodeCurrentFrame();
    bool skipCurrentFrame();

    // Lightweight black frame check (no RGB conversion, no QImage)
    bool isFrameBlack(int frameIndex, int pixelThreshold, float ratioThreshold);

    // Scene change detection via luma histogram comparison
    bool isSceneChange(int indexA, int indexB, float threshold);

    // Build luma histogram for a single frame (for cached scene change search)
    bool buildHistogram(int frameIndex, int hist[256], int& totalPixels);
    int videoWidth() const;
    int videoHeight() const;

    // Frame cache management
    void setFrameCacheSize(int maxFrames);
    void clearFrameCache();

    // Audio ES cutting - time-based stream-copy (ms-accurate)
    // If normalizeAcmod is true and targetAcmods is provided, frames with wrong acmod
    // at segment boundaries are re-encoded to match the target channel layout.
    bool cutAudioStream(const QString& inputFile, const QString& outputFile,
                        const QList<QPair<double, double>>& cutList,
                        bool normalizeAcmod = false,
                        const QList<int>& targetAcmods = QList<int>());

    // Detect audio burst near a boundary (returns true if burst found)
    // Sets burstRmsDb and contextRmsDb if burst detected
    static bool detectAudioBurst(const QString& audioFile, double boundaryTime,
                                  bool isCutOut, double& burstRmsDb, double& contextRmsDb);

    // AC3 acmod analysis - detect channel format changes at cut boundaries
    struct AcmodInfo {
        int mainAcmod;            // Majority acmod of segment (-1 if not AC3)
        int cutInAcmod;           // acmod at CutIn position
        int cutOutAcmod;          // acmod at CutOut position
        double cutInChangeTime;   // Time where acmod changes to mainAcmod (0 if no change)
        double cutOutChangeTime;  // Time where acmod changes from mainAcmod (0 if no change)
    };
    static AcmodInfo analyzeAcmod(const QString& audioFile,
                                   double cutInTime, double cutOutTime);

    // SRT subtitle cutting - text-based time filtering
    bool cutSrtSubtitle(const QString& inputFile, const QString& outputFile,
                        const QList<QPair<double, double>>& cutList);

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
    int mDecoderFrameIndex;     // Actual decoder position (last decoded frame)
    bool mDecoderDrained;       // True if decoder was flushed for EOF drain
    // Decode-order tag counter for decodeFrame(): each sent packet is tagged with
    // pts = mDecodeOrderTag++ (decode order from the seek keyframe). Since
    // avcodec_receive_frame() delivers in display order, the delivered frame
    // carries its decode-order index in mDecodedFrame->pts. Used to fill
    // TTFrameInfo::deliveredDecodeIndex.
    int64_t mDecodeOrderTag = 0;
    bool mIsElementaryStream;   // Cached: true if file is raw ES (byte-seeking)
    bool mAnalysisMode;         // True: use multi-threaded decoding for analysis
    bool mSearchMode;           // True: skip DPB prefill in seekToFrame (I-frame-only access)

    // YUV-plane tight-packed buffers for decodeFrameYUV()
    quint8* mYBuffer = nullptr;       // size = mYUVBufferWidth * mYUVBufferHeight
    quint8* mUBuffer = nullptr;       // size = (mYUVBufferWidth/2) * (mYUVBufferHeight/2)
    quint8* mVBuffer = nullptr;       // size = (mYUVBufferWidth/2) * (mYUVBufferHeight/2)
    int     mYUVBufferWidth  = 0;     // Allocated buffer dimensions; re-alloc on change
    int     mYUVBufferHeight = 0;
    // Slow-path swscale context for non-YUV420P inputs (e.g. YUV420P10LE
    // from HEVC Main 10). Lazy-init on first non-YUV420P frame; rebuilt
    // when source format or dimensions change.
    SwsContext* mSwsCtxYUV = nullptr;
    int         mSwsCtxYUVSrcFmt = -1;
    int         mSwsCtxYUVWidth  = 0;
    int         mSwsCtxYUVHeight = 0;

    bool mIsPAFF;                       // PAFF stream detected
    int mH264Log2MaxFrameNum;           // from SPS, for frame_num parsing
    bool mH264FrameMbsOnlyFlag;         // from SPS, true = no field coding

    // H.264 PAFF field info from packet data
    struct TTFieldInfo {
        bool isField;        // field_pic_flag
        bool isBottomField;  // bottom_field_flag
        int frameNum;        // frame_num from slice header
    };
    TTFieldInfo parseH264FieldInfoFromPacket(const uint8_t* data, int size);

    // Decode-order tag for a packet (frame units, PAFF-aware). See .cpp.
    int64_t decodeOrderTagForPacket(const AVPacket* packet);
    void parseH264SpsFromExtradata(const uint8_t* data, int size);
    // Validate format ctx, clear mFrameIndex, seek to byte 0 (ES) or PTS 0
    // (container), parse SPS extradata for H.264 PAFF detection. Returns
    // false on validation/seek failure.
    bool setupIndexingPass(int videoStreamIndex);

    // Seek context back to the beginning of the stream after indexing.
    // ES path: avio_seek + avformat_flush. Container path: av_seek_frame.
    void rewindContext(int videoStreamIndex);

    // For elementary streams whose first frame has no PTS: walk mFrameIndex and
    // assign sequential PTS/DTS values from frame rate (read from .info file or
    // stream metadata). Validates and falls back to 25 fps. Halves PAFF rate.
    void assignPtsFromFrameRate(int videoStreamIndex);

    // Outer av_read_frame loop. Appends one TTFrameInfo per video packet
    // (top fields, bottom fields, and normal frames are all separate
    // entries). Sets mIsPAFF = true when a field packet is found. Leaves
    // gopIndex and frameIndex at -1 (filled in by finalizeFrameIndex).
    // Emits progressChanged.
    void scanPacketsIntoRawIndex(int videoStreamIndex);

    // PAFF post-processing: walk mFrameIndex, collapse adjacent
    // top+bottom field pairs (matching paffFrameNum) into a single entry
    // (top's fields + summed packetSize). No-op if !mIsPAFF. In-place.
    void mergePAFFFieldsInIndex();

    // Walk mFrameIndex assigning gopIndex (incremented at each keyframe)
    // and frameIndex (= position) to every entry.
    void finalizeFrameIndex();

    // Frame and GOP indices
    QList<TTFrameInfo> mFrameIndex;
    QList<TTGOPInfo> mGOPIndex;

    // LRU frame cache
    QMap<int, QImage> mFrameCache;
    QList<int> mFrameCacheLRU;  // Most recently used at back
    int mFrameCacheMaxSize;

    // Error handling
    QString mLastError;
    void setError(const QString& error);

    // Helper functions
    static QString avErrorToString(int errnum);
    int getFrameType(AVPacket* packet, AVCodecContext* codecCtx);

public:
    // Public ES-detection helpers shared with TTMkvMergeProvider.
    // Recognises raw H.264/H.265/MPEG-2 elementary streams by extension.
    static bool isElementaryStreamPath(const QString& filePath);
    // Returns the libav input format ('h264', 'hevc', 'mpegvideo') matching
    // the file extension, or nullptr for non-ES paths.
    static const AVInputFormat* esInputFormatForPath(const QString& filePath);
};

#endif // TTFFMPEGWRAPPER_H
