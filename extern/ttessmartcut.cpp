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

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
TTESSmartCut::TTESSmartCut()
    : QObject()
    , mIsInitialized(false)
    , mFrameRate(25.0)
    , mDecoder(nullptr)
    , mEncoder(nullptr)
    , mDecodedWidth(0)
    , mDecodedHeight(0)
    , mDecodedPixFmt(AV_PIX_FMT_NONE)
    , mReorderDelay(0)
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
    emit progressChanged(0, "Parsing ES file...");

    if (!mParser.parseFile()) {
        setError(QString("Cannot parse ES file: %1").arg(mParser.lastError()));
        mParser.closeFile();
        return false;
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

    // Analyze cut points
    QList<TTCutSegmentInfo> segments = analyzeCutPoints(cutFrames);

    // Open output file
    QFile outFile(outputFile);
    if (!outFile.open(QIODevice::WriteOnly)) {
        setError(QString("Cannot create output file: %1").arg(outputFile));
        return false;
    }

    // Check if first segment is pure stream-copy (starts at IDR)
    // If so, write original parameter sets at the beginning
    // Otherwise, the encoder will provide its own SPS/PPS
    if (!segments.isEmpty() && segments[0].streamCopyStartFrame >= 0 &&
        segments[0].reencodeStartFrame < 0) {
        qDebug() << "First segment is pure stream-copy - writing original parameter sets";
        if (!writeParameterSets(outFile)) {
            outFile.close();
            return false;
        }
    } else {
        qDebug() << "First segment needs re-encoding - encoder will provide SPS/PPS";
    }

    // Process each segment
    int totalFrames = 0;
    for (const auto& seg : segments) {
        totalFrames += (seg.endFrame - seg.startFrame + 1);
    }

    int framesProcessed = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const TTCutSegmentInfo& seg = segments[i];

        qDebug() << "  Processing segment" << i << ":"
                 << "frames" << seg.startFrame << "->" << seg.endFrame;

        if (seg.needsReencodeAtStart) {
            qDebug() << "    Re-encode:" << seg.reencodeStartFrame
                     << "->" << seg.reencodeEndFrame;
        }
        qDebug() << "    Stream-copy:" << seg.streamCopyStartFrame
                 << "->" << seg.streamCopyEndFrame;

        if (!processSegment(outFile, seg)) {
            outFile.close();
            return false;
        }

        // Write EOS NAL between segments to force decoder DPB flush
        // This prevents reference frames from the previous segment bleeding
        // into the next segment's display at the transition point
        if (i < segments.size() - 1) {
            QByteArray eosNal;
            if (mParser.codecType() == NALU_CODEC_H265) {
                // H.265 EOS_NUT (type 36): start code + 2-byte NAL header
                // byte1 = (36 << 1) = 0x48, byte2 = 0x01 (temporal_id_plus1=1)
                eosNal = QByteArray::fromHex("000000014801");
            } else {
                // H.264 end_of_seq (type 10): start code + 1-byte NAL header
                eosNal = QByteArray::fromHex("000000010A");
            }
            outFile.write(eosNal);
            qDebug() << "    Wrote EOS NAL between segments" << i << "and" << i + 1;
        }

        framesProcessed += (seg.endFrame - seg.startFrame + 1);
        int percent = (framesProcessed * 100) / totalFrames;
        emit progressChanged(percent, QString("Processing segment %1/%2")
            .arg(i + 1).arg(segments.size()));
    }

    outFile.close();
    mBytesWritten = QFileInfo(outputFile).size();

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
        seg.needsReencodeAtStart = (keyframeBefore != seg.startFrame);

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
// Process a single segment - Smart Cut using pure libav
// Strategy: Both re-encoded and stream-copied sections are self-contained
// Each section starts with its own SPS/PPS + IDR, allowing clean decoder reset
// ----------------------------------------------------------------------------
bool TTESSmartCut::processSegment(QFile& outFile, const TTCutSegmentInfo& segment)
{
    // If only stream-copy (no re-encoding), write directly
    if (segment.reencodeStartFrame < 0) {
        qDebug() << "    Pure stream-copy segment";
        if (!writeParameterSets(outFile)) {
            return false;
        }
        return streamCopyFrames(outFile, segment.streamCopyStartFrame, segment.streamCopyEndFrame);
    }

    // If only re-encoding (no stream-copy), write directly
    // x264 will include its own SPS/PPS
    if (segment.streamCopyStartFrame < 0) {
        qDebug() << "    Pure re-encode segment";
        return reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame);
    }

    // Mixed segment: Re-encode partial GOP + stream-copy from IDR
    // Both sections are self-contained with their own parameter sets
    qDebug() << "    Smart Cut: Re-encode" << segment.reencodeStartFrame << "->" << segment.reencodeEndFrame
             << "then stream-copy" << segment.streamCopyStartFrame << "->" << segment.streamCopyEndFrame;

    // 1. Re-encode section (x264 includes SPS/PPS automatically)
    if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame)) {
        return false;
    }

    // 2. Stream-copy section starting from IDR
    // Write original SPS/PPS before the stream-copied IDR
    // This overwrites x264's parameters in the decoder, preparing it for original stream
    qDebug() << "    Writing original SPS/PPS before stream-copy IDR";
    if (!writeParameterSets(outFile)) {
        return false;
    }
    if (!streamCopyFrames(outFile, segment.streamCopyStartFrame, segment.streamCopyEndFrame)) {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Stream-copy frames (no re-encoding)
// ----------------------------------------------------------------------------
bool TTESSmartCut::streamCopyFrames(QFile& outFile, int startFrame, int endFrame)
{
    qDebug() << "    Stream-copying frames" << startFrame << "->" << endFrame;

    for (int i = startFrame; i <= endFrame; ++i) {
        // Read access unit (frame) data
        QByteArray auData = mParser.readAccessUnitData(i);
        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1").arg(i));
            return false;
        }

        // Write directly to output
        if (outFile.write(auData) != auData.size()) {
            setError(QString("Failed to write frame %1").arg(i));
            return false;
        }

        mFramesStreamCopied++;
    }

    return true;
}

// ----------------------------------------------------------------------------
// Re-encode frames (for partial GOPs)
// ----------------------------------------------------------------------------
bool TTESSmartCut::reencodeFrames(QFile& outFile, int startFrame, int endFrame)
{
    qDebug() << "    Re-encoding frames" << startFrame << "->" << endFrame;

    // Find the keyframe we need to decode from
    int decodeStart = mParser.findKeyframeBefore(startFrame);
    if (decodeStart < 0) decodeStart = 0;

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

                // Read decoder's reorder delay (has_b_frames) after first decode
                if (mReorderDelay == 0 && mDecoder->has_b_frames > 0) {
                    mReorderDelay = mDecoder->has_b_frames;
                    qDebug() << "      Decoder has_b_frames:" << mReorderDelay;
                }

                if (!setupEncoder()) {
                    av_frame_free(&frame);
                    for (AVFrame* f : allDecodedFrames) av_frame_free(&f);
                    allDecodedFrames.clear();
                    return false;
                }
                encoderInitialized = true;
            }

            allDecodedFrames.append(frame);
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
        packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(auData.constData()));
        packet->size = auData.size();

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

    int beforeFlush = allDecodedFrames.size();

    // Enter drain mode: send NULL once, then drain remaining buffered frames
    int flushRet = avcodec_send_packet(mDecoder, nullptr);
    qDebug() << "      Flush: send_packet(nullptr) returned" << flushRet
             << "(0=ok, AVERROR_EOF=" << AVERROR_EOF << ")";

    // Drain loop for flush - call receive_frame until EOF
    int flushCount = 0;
    while (true) {
        AVFrame* frame = av_frame_alloc();
        int ret = avcodec_receive_frame(mDecoder, frame);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            if (flushCount == 0) {
                qDebug() << "      Flush: first receive_frame returned" << ret << ":" << errbuf;
            }
            av_frame_free(&frame);
            break;
        }

        // Initialize encoder if needed (for edge case where all frames come from flush)
        if (!encoderInitialized) {
            qDebug() << "      First decoded frame (from flush): " << frame->width << "x" << frame->height
                     << " pix_fmt=" << frame->format;
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
        flushCount++;
    }

    qDebug() << "      Flush produced" << flushCount << "additional frames"
             << "(total:" << allDecodedFrames.size() << "from" << beforeFlush << "before flush)";

    int totalDecoded = allDecodedFrames.size();
    int totalInput = decodeEnd - decodeStart + 1;

    // Decoder output is in display order. We decoded from decodeStart..decodeEnd,
    // so the first (startFrame - decodeStart) output frames are before our cut range.
    int framesToSkip = startFrame - decodeStart;
    int expectedFrames = endFrame - startFrame + 1;
    QList<AVFrame*> framesToEncode;

    for (int i = 0; i < allDecodedFrames.size(); ++i) {
        if (i >= framesToSkip && framesToEncode.size() < expectedFrames) {
            framesToEncode.append(allDecodedFrames[i]);
        } else {
            av_frame_free(&allDecodedFrames[i]);
        }
    }
    allDecodedFrames.clear();

    qDebug() << "      Decoded" << totalDecoded << "frames from" << totalInput << "input AUs,"
             << "skipped" << framesToSkip << "leading frames,"
             << "keeping" << framesToEncode.size() << "(expected" << expectedFrames << ")";

    // Re-encode frames
    // Note: x264/x265 encoders buffer frames and may not return output immediately
    // We need to send all frames first, then flush to get all encoded output
    bool firstFrame = true;
    int framesSent = 0;
    int packetsReceived = 0;

    for (AVFrame* frame : framesToEncode) {
        // Force first frame to be keyframe
        if (firstFrame) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 0, 0)
            frame->key_frame = 1;
#endif
            firstFrame = false;
        }

        // Set PTS
        frame->pts = framesSent;

        // Send frame to encoder
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

        // Try to receive any available packets
        AVPacket* packet = av_packet_alloc();
        while (true) {
            ret = avcodec_receive_packet(mEncoder, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                qDebug() << "TTESSmartCut: avcodec_receive_packet failed:" << errbuf;
                av_packet_free(&packet);
                for (AVFrame* f : framesToEncode) av_frame_free(&f);
                return false;
            }

            // Write encoded data - keep everything including SPS/PPS
            // (the encoder output is self-contained)
            QByteArray encodedData(reinterpret_cast<char*>(packet->data), packet->size);
            if (outFile.write(encodedData) != encodedData.size()) {
                setError("Failed to write encoded data");
                av_packet_free(&packet);
                for (AVFrame* f : framesToEncode) av_frame_free(&f);
                return false;
            }
            packetsReceived++;
            av_packet_unref(packet);
        }
        av_packet_free(&packet);

        av_frame_free(&frame);
    }
    framesToEncode.clear();

    // Flush encoder - send null frame to signal end
    avcodec_send_frame(mEncoder, nullptr);

    // Receive all remaining packets
    AVPacket* packet = av_packet_alloc();
    while (true) {
        int ret = avcodec_receive_packet(mEncoder, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "TTESSmartCut: avcodec_receive_packet (flush) failed:" << errbuf;
            break;
        }

        QByteArray encodedData(reinterpret_cast<char*>(packet->data), packet->size);
        if (outFile.write(encodedData) != encodedData.size()) {
            setError("Failed to write encoded data (flush)");
            av_packet_free(&packet);
            return false;
        }
        packetsReceived++;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    qDebug() << "      Encoding complete: sent" << framesSent << "frames, received" << packetsReceived << "packets";
    mFramesReencoded += packetsReceived;

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

    // IMPORTANT: No B-frames in re-encoded sections
    // This ensures DTS == PTS for clean transitions
    mEncoder->max_b_frames = 0;

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
        presetIdx  = qBound(0, TTCut::h264Preset, 8);
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
    } else {
        // H.265
        crf        = TTCut::h265Crf;
        presetIdx  = qBound(0, TTCut::h265Preset, 8);
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

    if (!nalData.isEmpty()) {
        packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(nalData.constData()));
        packet->size = nalData.size();

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

        // Set PTS
        static int64_t pts = 0;
        frame->pts = pts++;

        // Force keyframe if requested
        if (forceKeyframe) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 0, 0)
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
// Write parameter sets (SPS/PPS/VPS)
// ----------------------------------------------------------------------------
bool TTESSmartCut::writeParameterSets(QFile& outFile)
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

    // Write SPS
    for (int i = 0; i < mParser.spsCount(); ++i) {
        QByteArray sps = mParser.getSPS(i);
        if (!sps.isEmpty()) {
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
