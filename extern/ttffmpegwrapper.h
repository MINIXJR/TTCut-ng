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

#include <atomic>
#include <QString>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QObject>
#include <QImage>

#include "../avstream/ttdisplayordermap.h"

#include "../avstream/ttframeinfo.h"
#include "../avstream/ttframeindex.h"
#include "../avstream/ttavutil.h"

// Forward declarations for libav types (avoid including C headers in .h)
// AVFormatContext and AVInputFormat come from ttavutil.h.
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct AVFrame;
struct SwsContext;

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

    // Open/close media file
    void setAnalysisMode(bool enabled) { mAnalysisMode = enabled; }
    void setSearchMode(bool enabled) { mSearchMode = enabled; }
    bool openFile(const QString& filePath);
    void closeFile();
    bool isOpen() const { return mFormatCtx != nullptr; }

    // Get stream information
    TTStreamInfo getStreamInfo(int streamIndex) const;
    int findBestVideoStream() const;
    int findBestAudioStream() const;

    // Detect video codec type
    TTVideoCodecType detectVideoCodec() const;

    // Detect container type (used by Smart Cut)

    // The frame index this wrapper works on. Built by TTFrameIndexer and
    // installed through setFrameIndex(); the wrapper no longer scans itself.
    const QList<TTFrameInfo>& frameIndex() const { return mFrameIndex; }
    // Adopt index AND the owner's stream metadata in one step. This is the
    // only public way to install an index; the bare list variant is private.
    // Ordering contract: the wrapper must be open (openFile()) before this is
    // called — the display-order map rebuilt here reads the codec context for
    // the H.264 cold-start marking.
    void setFrameIndex(const TTFrameIndexBundle& bundle);
    // This wrapper's index plus the metadata it measured, ready for adoption.
    TTFrameIndexBundle frameIndexBundle() const;
    // Optional abort flag, owned by the caller (a worker's mIsAborted). Checked
    // once per skip-loop iteration; when set, decodeFrame() returns a null
    // QImage without logging an error and without a neighbour attempt — a
    // cancel is not a failure. nullptr (default) disables the check.
    void setCancelToken(const std::atomic<bool>* flag) { mCancelToken = flag; }
    bool isCancelled() const
    { return mCancelToken && mCancelToken->load(std::memory_order_relaxed); }
    const TTDisplayOrderMap& displayOrderMap() const { return mDisplayOrderMap; }
    int frameCount() const { return mFrameIndex.size(); }
    bool isPAFF() const { return mIsPAFF; }
    int h264Log2MaxFrameNum() const { return mH264Log2MaxFrameNum; }
    bool h264FrameMbsOnlyFlag() const { return mH264FrameMbsOnlyFlag; }

    // Sample aspect ratio (SAR) of the video stream as width/height factor.
    // Codec context first (populated once a frame was decoded), stream
    // codecpar as fallback; 1.0 when unset/invalid.
    double sampleAspectRatio() const;

    // Frame access
    TTFrameInfo frameAt(int index) const;

    // Utility functions

    // Frame decoding for preview
    bool seekToFrame(int frameIndex);
    QImage decodeFrame(int frameIndex);

    // Slider-drag preview: decode ONLY the keyframe at/before the given
    // display position, without the previous-GOP DPB prefill (safe for
    // keyframes - intra-coded, self-decodable; the search path has used the
    // same shortcut since the setSearchMode() work). Cost is one seek plus a
    // handful of packets instead of up to two GOPs; measured on UHD HEVC that
    // is the difference between ~0.15 s and ~2.6 s per slider event. The
    // exact target frame is NOT decoded here - the caller shows the keyframe
    // while dragging and requests the precise frame (full prefill, WYSIWYG)
    // once the slider is released. shownDisplayPos receives the display
    // position of the frame actually delivered.
    QImage decodeNearestKeyframe(int displayPos, int* shownDisplayPos);

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

    // Convert the already-decoded mDecodedFrame to a QImage (lazy-inits
    // mRgbFrame/mSwsCtx + sws_scale). Does NOT read or decode a packet.
    QImage convertDecodedFrameToImage();
    bool skipCurrentFrame();

    // Lightweight black frame check (no RGB conversion, no QImage)
    bool isFrameBlack(int frameIndex, int pixelThreshold, float ratioThreshold);

    // Scene change detection via luma histogram comparison

    // Build luma histogram for a single frame (for cached scene change search)
    bool buildHistogram(int frameIndex, int hist[256], int& totalPixels);

    // Frame cache management
    void clearFrameCache();

    // Error handling
    QString lastError() const { return mLastError; }

private:
    // Install the index entries and rebuild the display map. Private on purpose:
    // an index without its stream metadata makes decodeFrame() drain to EOF on
    // PAFF field-pair targets. Public adoption goes through the bundle overload.
    void setFrameIndexEntries(const QList<TTFrameInfo>& index);

    // Adopt the stream-level values the index owner measured during
    // the index build (SPS parse + packet scan). Private: reachable only via
    // setFrameIndex(const TTFrameIndexBundle&), so it can no longer be forgotten.
    void adoptStreamMetadata(bool isPAFF, bool frameMbsOnlyFlag, int log2MaxFrameNum)
    {
        mIsPAFF = isPAFF;
        mH264FrameMbsOnlyFlag = frameMbsOnlyFlag;
        mH264Log2MaxFrameNum = log2MaxFrameNum;
    }

    // decodeFrame's body. fallbackDepth limits the neighbour retry to a single
    // step: the comment always said "try one frame earlier", but the recursion
    // was unbounded and would walk down to frame 0 — harmless only as long as
    // each level took 40 s. It does not any more.
    QImage decodeFrameInternal(int frameIndex, int fallbackDepth);

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
    // A packet that was read and tagged but could not be sent because the
    // decoder's output queue was full (avcodec_send_packet == EAGAIN). It is
    // sent first on the next skipCurrentFrame() call. Dropping it instead -
    // which the old code did - loses the AU AND its decode-order tag, so the
    // skip loop's target tag never appears and every caller degrades into a
    // read-and-discard pass over the rest of the file (measured on UHD HEVC
    // with a B-hierarchy: minutes per decodeFrame call). Cleared on seek
    // (stale tag domain) and on closeFile().
    AVPacket* mPendingPacket = nullptr;

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

    const std::atomic<bool>* mCancelToken;   // not owned; nullptr = no cancelling
    bool mIsPAFF;                       // PAFF stream detected
    int mH264Log2MaxFrameNum;           // from SPS, for frame_num parsing
    bool mH264FrameMbsOnlyFlag;         // from SPS, true = no field coding

    // Decode-order tag for a packet (frame units, PAFF-aware). See .cpp.
    int64_t decodeOrderTagForPacket(const AVPacket* packet);

    // Frame index and the display order derived from it
    QList<TTFrameInfo> mFrameIndex;
    TTDisplayOrderMap mDisplayOrderMap;

    // Derive the display-order map from the poc/isIDR fields of mFrameIndex.
    // Falls back to the identity map (with a warning) when POC data is
    // missing or degenerate — identical to pre-map behavior.
    void buildDisplayOrderMap();

    // LRU frame cache
    QMap<int, QImage> mFrameCache;
    QList<int> mFrameCacheLRU;  // Most recently used at back
    int mFrameCacheMaxSize;

    // Error handling
    QString mLastError;
    void setError(const QString& error);

    // Helper functions
    static QString avErrorToString(int errnum);

public:
    // Public ES-detection helpers shared with TTMkvMergeProvider.
    // Recognises raw H.264/H.265/MPEG-2 elementary streams by extension.
    static bool isElementaryStreamPath(const QString& filePath);
    // Returns the libav input format ('h264', 'hevc', 'mpegvideo') matching
    // the file extension, or nullptr for non-ES paths.
    static const AVInputFormat* esInputFormatForPath(const QString& filePath);
};

#endif // TTFFMPEGWRAPPER_H
