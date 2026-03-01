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
static int parseLog2MaxFrameNumMinus4(const QByteArray& spsNal);
static int readFrameNumFromAU(const QByteArray& auData, int frameNumBitWidth);

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
    emit progressChanged(0, "Parsing ES file...");

    if (!mParser.parseFile()) {
        setError(QString("Cannot parse ES file: %1").arg(mParser.lastError()));
        mParser.closeFile();
        return false;
    }

    // Parse H.264 frame_num field width for inter-segment patching
    if (mParser.codecType() == NALU_CODEC_H264 && mParser.spsCount() > 0) {
        QByteArray sps = mParser.getSPS(0);
        int log2minus4 = parseLog2MaxFrameNumMinus4(sps);
        if (log2minus4 >= 0) {
            mLog2MaxFrameNum = log2minus4 + 4;
            qDebug() << "TTESSmartCut: log2_max_frame_num =" << mLog2MaxFrameNum
                     << "(MaxFrameNum =" << (1 << mLog2MaxFrameNum) << ")";
        } else {
            qDebug() << "TTESSmartCut: WARNING - could not parse log2_max_frame_num from SPS";
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
    int totalFrames = 0;
    for (const auto& seg : segments) {
        totalFrames += (seg.endFrame - seg.startFrame + 1);
    }

    // H.264 frame_num patching: track cumulative delta for inter-segment continuity.
    // Without this, frame_num gaps at segment boundaries cause the decoder to generate
    // dummy reference frames, resulting in visual stuttering/flashing.
    int cumulativeFrameNumDelta = 0;
    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

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

        if (!processSegment(outFile, seg, cumulativeFrameNumDelta)) {
            outFile.close();
            return false;
        }

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
                                   int frameNumDelta)
{
    // If only stream-copy (no re-encoding), write directly
    if (segment.reencodeStartFrame < 0) {
        qDebug() << "    Pure stream-copy segment";
        return streamCopyFrames(outFile, segment.streamCopyStartFrame,
                                segment.streamCopyEndFrame, mReorderDelay, frameNumDelta);
    }

    // If only re-encoding (no stream-copy), write directly
    if (segment.streamCopyStartFrame < 0) {
        qDebug() << "    Pure re-encode segment";
        return reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame, -1);
    }

    // Mixed segment: Re-encode partial GOP + stream-copy from keyframe
    qDebug() << "    Smart Cut: Re-encode" << segment.reencodeStartFrame << "->" << segment.reencodeEndFrame
             << "then stream-copy" << segment.streamCopyStartFrame << "->" << segment.streamCopyEndFrame;

    // 1. Re-encode section (x264/x265 includes SPS/PPS automatically)
    //    When B-frame reorder delay shifts the display-order CutIn past the
    //    stream-copy boundary, re-encode is extended to the next keyframe.
    //    adjustedStart receives the new stream-copy start if this happens.
    int adjustedStart = -1;
    if (!reencodeFrames(outFile, segment.reencodeStartFrame, segment.reencodeEndFrame,
                        segment.streamCopyStartFrame, &adjustedStart)) {
        return false;
    }

    // Use adjusted stream-copy start if re-encode was extended past original boundary
    int scStart = (adjustedStart >= 0) ? adjustedStart : segment.streamCopyStartFrame;
    int scEnd = segment.streamCopyEndFrame;

    // Check if stream-copy range is still valid after adjustment
    if (scStart > scEnd) {
        qDebug() << "    Re-encode consumed entire segment, no stream-copy needed";
        return true;
    }

    // 2. Transition from re-encoded to stream-copied section
    // Always write EOS NAL to flush decoder DPB, preventing co-located reference
    // mismatches when stream-copied B-frames use temporal_direct prediction.
    // Without DPB flush, stale re-encoded frames (different POC domain) remain
    // in the DPB, corrupting co-located MB lookup for B-frames.
    // Note: Between segments, EOS is also always written (smartCutFrames) and
    // works correctly with Non-IDR I-frames on all major decoders.
    if (mParser.codecType() == NALU_CODEC_H264) {
        static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x0B};
        outFile.write(eosNal, sizeof(eosNal));
        qDebug() << "    Inserted H.264 EOS NAL (type 11) - flushing DPB before stream-copy at" << scStart;
    } else if (mParser.codecType() == NALU_CODEC_H265) {
        static const char eosNal[] = {0x00, 0x00, 0x00, 0x01, 0x4A, 0x01};
        outFile.write(eosNal, sizeof(eosNal));
        qDebug() << "    Inserted H.265 EOS NAL (type 37) - flushing DPB before stream-copy at" << scStart;
    }

    // 3. Write source parameter sets for decoder transition
    // Patch max_num_reorder_frames to match encoder SPS for consistent signaling
    writeParameterSets(outFile, mReorderDelay);

    // 4. Stream-copy section starting from keyframe
    // Note: frameNumDelta only applies to stream-copied frames, not re-encoded ones
    // (x264 encoder produces its own frame_num sequence)
    // Patch inline SPS NALs with max_num_reorder_frames so the decoder doesn't
    // reset and dynamically grow its reorder buffer at each SPS NAL
    if (!streamCopyFrames(outFile, scStart,
                          scEnd, mReorderDelay, frameNumDelta)) {
        return false;
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
static uint32_t spsReadBits(const uint8_t* data, int& bitPos, int numBits);
static void spsWriteBits(uint8_t* data, int& bitPos, uint32_t value, int numBits);
static uint32_t spsReadUE(const uint8_t* data, int& bitPos);
static void skipScalingList(const uint8_t* data, int& bitPos, int sizeOfScalingList);

// ----------------------------------------------------------------------------
// Parse log2_max_frame_num_minus4 from raw H.264 SPS NAL data.
// Returns -1 on error.
// ----------------------------------------------------------------------------
static int parseLog2MaxFrameNumMinus4(const QByteArray& spsNal)
{
    // Find and strip start code
    int startCodeLen = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(spsNal.constData());
    if (spsNal.size() >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1)
        startCodeLen = 4;
    else if (spsNal.size() >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1)
        startCodeLen = 3;
    else
        return -1;

    QByteArray nalBody = spsNal.mid(startCodeLen);
    if (nalBody.isEmpty() || ((uint8_t)nalBody[0] & 0x1F) != 7)
        return -1;

    QByteArray rbsp = removeEmulationPrevention(nalBody);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(rbsp.constData());
    int bitPos = 0;

    spsReadBits(data, bitPos, 8);  // NAL header
    uint32_t profile_idc = spsReadBits(data, bitPos, 8);
    spsReadBits(data, bitPos, 8);  // constraint flags
    spsReadBits(data, bitPos, 8);  // level_idc
    spsReadUE(data, bitPos);       // seq_parameter_set_id

    // High profile extensions
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        uint32_t chroma_format_idc = spsReadUE(data, bitPos);
        if (chroma_format_idc == 3)
            spsReadBits(data, bitPos, 1);  // separate_colour_plane_flag
        spsReadUE(data, bitPos);    // bit_depth_luma_minus8
        spsReadUE(data, bitPos);    // bit_depth_chroma_minus8
        spsReadBits(data, bitPos, 1); // qpprime_y_zero_transform_bypass_flag
        uint32_t seq_scaling_matrix_present = spsReadBits(data, bitPos, 1);
        if (seq_scaling_matrix_present) {
            int limit = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < limit; i++) {
                uint32_t present = spsReadBits(data, bitPos, 1);
                if (present)
                    skipScalingList(data, bitPos, (i < 6) ? 16 : 64);
            }
        }
    }

    return static_cast<int>(spsReadUE(data, bitPos));
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
    spsReadUE(nalData, bitPos);   // first_mb_in_slice
    spsReadUE(nalData, bitPos);   // slice_type
    spsReadUE(nalData, bitPos);   // pic_parameter_set_id

    // frame_num is u(v) with v = frameNumBitWidth
    return static_cast<int>(spsReadBits(nalData, bitPos, frameNumBitWidth));
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
    spsReadUE(nalData, bitPos);   // first_mb_in_slice
    spsReadUE(nalData, bitPos);   // slice_type
    spsReadUE(nalData, bitPos);   // pic_parameter_set_id

    // Overwrite frame_num at current position
    spsWriteBits(nalData, bitPos, newFrameNum, frameNumBitWidth);
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
// Stream-copy frames (no re-encoding)
// If patchReorderFrames > 0, patches H.264 SPS NALs inline for correct
// decoder reorder buffer signaling.
// ----------------------------------------------------------------------------
bool TTESSmartCut::streamCopyFrames(QFile& outFile, int startFrame, int endFrame,
                                     int patchReorderFrames, int frameNumDelta)
{
    qDebug() << "    Stream-copying frames" << startFrame << "->" << endFrame;
    if (frameNumDelta != 0) {
        qDebug() << "    frame_num delta:" << frameNumDelta
                 << "(MaxFrameNum=" << (1 << mLog2MaxFrameNum) << ")";
    }

    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

    for (int i = startFrame; i <= endFrame; ++i) {
        // Read access unit (frame) data
        QByteArray auData = mParser.readAccessUnitData(i);
        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1").arg(i));
            return false;
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
    }

    return true;
}

// ----------------------------------------------------------------------------
// Re-encode frames (for partial GOPs)
// ----------------------------------------------------------------------------
bool TTESSmartCut::reencodeFrames(QFile& outFile, int startFrame, int endFrame,
                                  int streamCopyStartFrame, int* adjustedStreamCopyStart)
{
    qDebug() << "    Re-encoding frames" << startFrame << "->" << endFrame;

    if (adjustedStreamCopyStart)
        *adjustedStreamCopyStart = -1;  // -1 = no adjustment needed

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

    int realStartAU = startFrame;  // fallback if mapping fails
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

    QList<AVFrame*> framesToEncode;
    int streamCopyLimit = (streamCopyStartFrame >= 0) ? streamCopyStartFrame : (endFrame + 1);

    // Check if display-order mapping moved CutIn past the stream-copy boundary.
    // This happens when the B-frame reorder delay shifts the real content to AUs
    // at or after the stream-copy keyframe.
    if (realStartAU >= streamCopyLimit) {
        if (realStartAU == streamCopyLimit) {
            // Case A: CutIn exactly at stream-copy boundary.
            // Full GOP of pre-CutIn content would leak into output.
            // Fix: extend re-encode to next keyframe, stream-copy from there.
            int nextKF = mParser.findKeyframeAfter(streamCopyStartFrame + 1);
            if (nextKF < 0 || (endFrame >= 0 && nextKF > endFrame + 50)) {
                nextKF = frameCount();
            }
            streamCopyLimit = nextKF;

            qDebug() << "      Case A: CutIn AU" << realStartAU
                     << "== stream-copy start" << streamCopyStartFrame
                     << "-> extending re-encode to next keyframe" << nextKF;

            if (adjustedStreamCopyStart) {
                *adjustedStreamCopyStart = nextKF;
            }
        } else {
            // Case B: CutIn a few frames past stream-copy boundary.
            // Small leak (≤ reorder_delay frames) is acceptable.
            // Do NOT extend re-encode: the re-encode→stream-copy transition
            // creates a POC domain change (encoder vs source) that causes
            // "co located POCs unavailable" when stream-copied B-frames use
            // temporal_direct prediction. EOS flushes DPB but cannot reset
            // POC state — only IDR can do that.
            qDebug() << "      Case B: CutIn AU" << realStartAU
                     << "is" << (realStartAU - streamCopyLimit)
                     << "frames past stream-copy start" << streamCopyStartFrame
                     << "- skipping re-encode to avoid POC mismatch";
        }
    }

    // Select frames by corrected AU index range [realStartAU, streamCopyLimit)
    for (int i = 0; i < allDecodedFrames.size(); ++i) {
        int auIndex = static_cast<int>(allDecodedFrames[i]->pts);
        if (auIndex >= realStartAU && auIndex < streamCopyLimit) {
            framesToEncode.append(allDecodedFrames[i]);
        } else {
            av_frame_free(&allDecodedFrames[i]);
        }
    }
    allDecodedFrames.clear();

    qDebug() << "      Selected" << framesToEncode.size() << "frames for encoding"
             << "(AU range" << startFrame << "-" << (streamCopyLimit - 1) << ")";

    // Re-encode frames
    // Note: x264/x265 encoders buffer frames and may not return output immediately
    // We need to send all frames first, then flush to get all encoded output
    bool firstFrame = true;
    int framesSent = 0;
    int packetsReceived = 0;

    for (AVFrame* frame : framesToEncode) {
        // Force first frame to I-frame (keyframe) so the re-encoded section
        // starts with a clean random access point.
        if (firstFrame) {
            frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58, 0, 0)
            frame->key_frame = 1;
#endif
            firstFrame = false;
        } else {
            frame->pict_type = AV_PICTURE_TYPE_NONE;
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

            // Write encoded data including SPS/PPS
            // Patch encoder SPS to signal correct max_num_reorder_frames
            // so the decoder initializes its reorder buffer for source B-frames
            QByteArray encodedData(reinterpret_cast<char*>(packet->data), packet->size);
            if (mReorderDelay > 0 && mParser.codecType() == NALU_CODEC_H264) {
                qDebug() << "      Patching encoder packet" << packetsReceived
                         << "(" << encodedData.size() << "bytes) with mReorderDelay=" << mReorderDelay;
                QByteArray patched = patchSpsNalsInAccessUnit(encodedData, mReorderDelay);
                if (patched != encodedData) {
                    qDebug() << "      -> SPS patched! Original" << encodedData.size()
                             << "bytes, patched" << patched.size() << "bytes";
                    encodedData = patched;
                } else {
                    qDebug() << "      -> No SPS found in this packet";
                }
            } else {
                qDebug() << "      Skipping SPS patch: mReorderDelay=" << mReorderDelay
                         << "codec=" << mParser.codecType();
            }
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
        if (mReorderDelay > 0 && mParser.codecType() == NALU_CODEC_H264)
            encodedData = patchSpsNalsInAccessUnit(encodedData, mReorderDelay);
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

// Read bits from RBSP byte array (same as TTNaluParser::readBits but standalone)
static uint32_t spsReadBits(const uint8_t* data, int& bitPos, int numBits)
{
    uint32_t value = 0;
    for (int i = 0; i < numBits; i++) {
        int byteIndex = bitPos / 8;
        int bitIndex = 7 - (bitPos % 8);
        value <<= 1;
        value |= (data[byteIndex] >> bitIndex) & 1;
        bitPos++;
    }
    return value;
}

// Write bits to RBSP byte array
static void spsWriteBits(uint8_t* data, int& bitPos, uint32_t value, int numBits)
{
    for (int i = numBits - 1; i >= 0; i--) {
        int byteIndex = bitPos / 8;
        int bitIndex = 7 - (bitPos % 8);
        if (value & (1u << i))
            data[byteIndex] |= (1 << bitIndex);
        else
            data[byteIndex] &= ~(1 << bitIndex);
        bitPos++;
    }
}

// Read Exp-Golomb unsigned value from RBSP
static uint32_t spsReadUE(const uint8_t* data, int& bitPos)
{
    int leadingZeros = 0;
    while (spsReadBits(data, bitPos, 1) == 0 && leadingZeros < 32)
        leadingZeros++;
    if (leadingZeros == 0) return 0;
    uint32_t value = spsReadBits(data, bitPos, leadingZeros);
    return (1u << leadingZeros) - 1 + value;
}

// Read Exp-Golomb signed value from RBSP
static int32_t spsReadSE(const uint8_t* data, int& bitPos)
{
    uint32_t ue = spsReadUE(data, bitPos);
    if (ue & 1) return static_cast<int32_t>((ue + 1) / 2);
    return -static_cast<int32_t>(ue / 2);
}

// Write Exp-Golomb unsigned value to RBSP
static void spsWriteUE(uint8_t* data, int& bitPos, uint32_t value)
{
    // Exp-Golomb: codeNum = value, code = (leadingZeros zeros)(1)(value bits)
    uint32_t codeNum = value + 1;
    int numBits = 0;
    uint32_t tmp = codeNum;
    while (tmp > 0) { numBits++; tmp >>= 1; }
    int leadingZeros = numBits - 1;
    // Write leading zeros
    for (int i = 0; i < leadingZeros; i++)
        spsWriteBits(data, bitPos, 0, 1);
    // Write 1 followed by value bits
    spsWriteBits(data, bitPos, codeNum, numBits);
}

// Skip H.264 scaling list in SPS
static void skipScalingList(const uint8_t* data, int& bitPos, int sizeOfScalingList)
{
    int nextScale = 8;
    for (int j = 0; j < sizeOfScalingList; j++) {
        if (nextScale != 0) {
            int32_t delta = spsReadSE(data, bitPos);
            nextScale = (nextScale + delta + 256) % 256;
        }
    }
}

// Skip H.264 HRD parameters in VUI
static void skipHrdParameters(const uint8_t* data, int& bitPos)
{
    uint32_t cpb_cnt_minus1 = spsReadUE(data, bitPos);
    spsReadBits(data, bitPos, 4);  // bit_rate_scale
    spsReadBits(data, bitPos, 4);  // cpb_size_scale
    for (uint32_t i = 0; i <= cpb_cnt_minus1; i++) {
        spsReadUE(data, bitPos);   // bit_rate_value_minus1
        spsReadUE(data, bitPos);   // cpb_size_value_minus1
        spsReadBits(data, bitPos, 1); // cbr_flag
    }
    spsReadBits(data, bitPos, 5);  // initial_cpb_removal_delay_length_minus1
    spsReadBits(data, bitPos, 5);  // cpb_removal_delay_length_minus1
    spsReadBits(data, bitPos, 5);  // dpb_output_delay_length_minus1
    spsReadBits(data, bitPos, 5);  // time_offset_length
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
    int bitPos = 0;

    // Parse NAL header (8 bits)
    spsReadBits(data, bitPos, 8);  // forbidden_zero_bit + nal_ref_idc + nal_unit_type

    // Parse SPS fields
    uint32_t profile_idc = spsReadBits(data, bitPos, 8);
    spsReadBits(data, bitPos, 8);   // constraint flags + reserved
    spsReadBits(data, bitPos, 8);   // level_idc
    spsReadUE(data, bitPos);        // seq_parameter_set_id

    // High profile extensions
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
        profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
        profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
        profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
        profile_idc == 135) {
        uint32_t chroma_format_idc = spsReadUE(data, bitPos);
        if (chroma_format_idc == 3)
            spsReadBits(data, bitPos, 1);  // separate_colour_plane_flag
        spsReadUE(data, bitPos);    // bit_depth_luma_minus8
        spsReadUE(data, bitPos);    // bit_depth_chroma_minus8
        spsReadBits(data, bitPos, 1); // qpprime_y_zero_transform_bypass_flag

        uint32_t seq_scaling_matrix_present = spsReadBits(data, bitPos, 1);
        if (seq_scaling_matrix_present) {
            int limit = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < limit; i++) {
                uint32_t present = spsReadBits(data, bitPos, 1);
                if (present)
                    skipScalingList(data, bitPos, (i < 6) ? 16 : 64);
            }
        }
    }

    spsReadUE(data, bitPos);  // log2_max_frame_num_minus4
    uint32_t poc_type = spsReadUE(data, bitPos);
    if (poc_type == 0) {
        spsReadUE(data, bitPos);  // log2_max_pic_order_cnt_lsb_minus4
    } else if (poc_type == 1) {
        spsReadBits(data, bitPos, 1);  // delta_pic_order_always_zero_flag
        spsReadSE(data, bitPos);       // offset_for_non_ref_pic
        spsReadSE(data, bitPos);       // offset_for_top_to_bottom_field
        uint32_t num_ref = spsReadUE(data, bitPos);
        for (uint32_t i = 0; i < num_ref; i++)
            spsReadSE(data, bitPos);   // offset_for_ref_frame
    }

    uint32_t max_num_ref_frames = spsReadUE(data, bitPos);
    spsReadBits(data, bitPos, 1);  // gaps_in_frame_num_allowed_flag
    spsReadUE(data, bitPos);       // pic_width_in_mbs_minus1
    spsReadUE(data, bitPos);       // pic_height_in_map_units_minus1

    uint32_t frame_mbs_only = spsReadBits(data, bitPos, 1);
    if (!frame_mbs_only)
        spsReadBits(data, bitPos, 1);  // mb_adaptive_frame_field_flag

    spsReadBits(data, bitPos, 1);  // direct_8x8_inference_flag

    uint32_t frame_cropping = spsReadBits(data, bitPos, 1);
    if (frame_cropping) {
        spsReadUE(data, bitPos);  // crop_left
        spsReadUE(data, bitPos);  // crop_right
        spsReadUE(data, bitPos);  // crop_top
        spsReadUE(data, bitPos);  // crop_bottom
    }

    uint32_t vui_present = spsReadBits(data, bitPos, 1);
    if (!vui_present) {
        qDebug() << "  SPS patch: no VUI, cannot add bitstream_restriction";
        return QByteArray();
    }

    // Parse VUI parameters to find bitstream_restriction_flag
    uint32_t aspect_ratio_present = spsReadBits(data, bitPos, 1);
    if (aspect_ratio_present) {
        uint32_t aspect_ratio_idc = spsReadBits(data, bitPos, 8);
        if (aspect_ratio_idc == 255) {  // Extended_SAR
            spsReadBits(data, bitPos, 16);  // sar_width
            spsReadBits(data, bitPos, 16);  // sar_height
        }
    }

    uint32_t overscan_present = spsReadBits(data, bitPos, 1);
    if (overscan_present)
        spsReadBits(data, bitPos, 1);  // overscan_appropriate_flag

    uint32_t video_signal_present = spsReadBits(data, bitPos, 1);
    if (video_signal_present) {
        spsReadBits(data, bitPos, 3);  // video_format
        spsReadBits(data, bitPos, 1);  // video_full_range_flag
        uint32_t colour_desc = spsReadBits(data, bitPos, 1);
        if (colour_desc) {
            spsReadBits(data, bitPos, 8);  // colour_primaries
            spsReadBits(data, bitPos, 8);  // transfer_characteristics
            spsReadBits(data, bitPos, 8);  // matrix_coefficients
        }
    }

    uint32_t chroma_loc_present = spsReadBits(data, bitPos, 1);
    if (chroma_loc_present) {
        spsReadUE(data, bitPos);  // chroma_sample_loc_type_top_field
        spsReadUE(data, bitPos);  // chroma_sample_loc_type_bottom_field
    }

    uint32_t timing_present = spsReadBits(data, bitPos, 1);
    if (timing_present) {
        spsReadBits(data, bitPos, 32);  // num_units_in_tick
        spsReadBits(data, bitPos, 32);  // time_scale
        spsReadBits(data, bitPos, 1);   // fixed_frame_rate_flag
    }

    uint32_t nal_hrd_present = spsReadBits(data, bitPos, 1);
    if (nal_hrd_present) skipHrdParameters(data, bitPos);

    uint32_t vcl_hrd_present = spsReadBits(data, bitPos, 1);
    if (vcl_hrd_present) skipHrdParameters(data, bitPos);

    if (nal_hrd_present || vcl_hrd_present)
        spsReadBits(data, bitPos, 1);  // low_delay_hrd_flag

    spsReadBits(data, bitPos, 1);  // pic_struct_present_flag

    // Now at bitstream_restriction_flag position
    int bsrFlagPos = bitPos;

    // Build new RBSP: copy everything up to (not including) bitstream_restriction_flag,
    // then write our modified bitstream_restriction section
    int bytesNeeded = (bsrFlagPos + 7) / 8 + 16;  // generous buffer for added fields
    QByteArray newRbsp(bytesNeeded, '\0');
    // Copy existing data up to the flag position
    memcpy(newRbsp.data(), rbsp.constData(), (bsrFlagPos + 7) / 8);

    int writeBitPos = bsrFlagPos;
    uint8_t* writeData = reinterpret_cast<uint8_t*>(newRbsp.data());

    // Write bitstream_restriction_flag = 1
    spsWriteBits(writeData, writeBitPos, 1, 1);

    // Write bitstream_restriction fields
    spsWriteBits(writeData, writeBitPos, 1, 1);  // motion_vectors_over_pic_boundaries_flag
    spsWriteUE(writeData, writeBitPos, 0);        // max_bytes_per_pic_denom
    spsWriteUE(writeData, writeBitPos, 0);        // max_bits_per_mb_denom
    spsWriteUE(writeData, writeBitPos, 16);       // log2_max_mv_length_horizontal
    spsWriteUE(writeData, writeBitPos, 16);       // log2_max_mv_length_vertical
    spsWriteUE(writeData, writeBitPos, maxReorderFrames);  // max_num_reorder_frames
    // max_dec_frame_buffering >= max_num_ref_frames and >= max_num_reorder_frames
    uint32_t maxDecBuf = qMax(maxReorderFrames, (int)max_num_ref_frames);
    spsWriteUE(writeData, writeBitPos, maxDecBuf);  // max_dec_frame_buffering

    // RBSP stop bit + byte alignment
    spsWriteBits(writeData, writeBitPos, 1, 1);  // rbsp_stop_one_bit
    int padding = (8 - (writeBitPos % 8)) % 8;
    if (padding > 0)
        spsWriteBits(writeData, writeBitPos, 0, padding);

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
