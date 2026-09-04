/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFRAMEINDEXER
// Builds a TTFrameIndexBundle from a video file: one libav packet scan, the
// PAFF field merge, the GOP table. Split out of TTFFmpegWrapper so an index
// can be produced without a decoder instance.
// ----------------------------------------------------------------------------

#include "ttframeindexer.h"
#include "ttavutil.h"
#include "ttdisplayordermap.h"
#include "ttesinfo.h"
#include "ttnaluparser.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <QDebug>
#include <QFileInfo>
#include <QObject>

// Include libav headers (C libraries)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

// ----------------------------------------------------------------------------
// Destructor — build() closes the context on every path it returns from, so
// this only catches an abandoned instance (no-op when mFormatCtx is null).
// ----------------------------------------------------------------------------
TTFrameIndexer::~TTFrameIndexer()
{
    if (mFormatCtx)
        avformat_close_input(&mFormatCtx);
}

// ----------------------------------------------------------------------------
// Build the frame index bundle for one file
// Returns true even when the scan found no video packet (frames=0), as
// buildFrameIndex() did; callers check frameCount().
// ----------------------------------------------------------------------------
bool TTFrameIndexer::build(const QString& filePath, int videoStreamIndex,
                           const ProgressFn& progress)
{
    mBundle = TTFrameIndexBundle();

    QString err;
    if (!ttOpenInput(&mFormatCtx, filePath, &err)) {
        setError(err);
        return false;
    }

    if (videoStreamIndex < 0) {
        videoStreamIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_VIDEO,
                                               -1, -1, nullptr, 0);
    }

    if (!setupIndexingPass(videoStreamIndex)) {
        avformat_close_input(&mFormatCtx);
        return false;
    }

    scanPacketsIntoRawIndex(videoStreamIndex, progress);
    mergePAFFFieldsInIndex();
    finalizeFrameIndex();

    if (!mBundle.index.isEmpty() && mBundle.index[0].pts == AV_NOPTS_VALUE) {
        assignPtsFromFrameRate(videoStreamIndex);
    }

    buildGops();
    buildDisplayMap(mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id);

    // Summary logging: TTFFmpegWrapper printed these from rewindContext(),
    // which the indexer has no use for (it closes the file instead).
    const bool isES = ttIsElementaryStreamPath(QString::fromUtf8(mFormatCtx->url));

    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "Frame index built:" << mBundle.index.size() << "frames in"
                 << (mBundle.index.isEmpty() ? 0 : mBundle.index.last().gopIndex + 1) << "GOPs";
    }

    // Debug: Check first frame's fileOffset for ES files
    if (isES && !mBundle.index.isEmpty()) {
        if (TTSettings::instance()->logFFmpegDecoder()) {
            qDebug() << "First frame fileOffset:" << mBundle.index[0].fileOffset
                     << "packetSize:" << mBundle.index[0].packetSize;
        }
    }

    avformat_close_input(&mFormatCtx);

    if (progress)
        progress(100, QObject::tr("Indexed %1 frames").arg(mBundle.index.size()));

    return true;
}

// ----------------------------------------------------------------------------
// Set error message
// ----------------------------------------------------------------------------
void TTFrameIndexer::setError(const QString& error)
{
    mLastError = error;
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("TTFrameIndexer error: %1").arg(error));
}

// ----------------------------------------------------------------------------
// Validate stream + reset state + seek to start + parse SPS for PAFF detection
// ----------------------------------------------------------------------------
bool TTFrameIndexer::setupIndexingPass(int videoStreamIndex)
{
    if (videoStreamIndex < 0 ||
        videoStreamIndex >= static_cast<int>(mFormatCtx->nb_streams)) {
        setError("No video stream found");
        return false;
    }

    mBundle.index.clear();
    mBundle.isPAFF = false;
    mBundle.rawPacketCount = 0;
    mBundle.rawToMerged.clear();

    // For raw ES files, seek to byte 0 instead of using av_seek_frame.
    // av_seek_frame doesn't work well with raw h264/hevc demuxers.
    const bool isES = ttIsElementaryStreamPath(QString::fromUtf8(mFormatCtx->url));

    if (isES && mFormatCtx->pb) {
        avio_seek(mFormatCtx->pb, 0, SEEK_SET);
        avformat_flush(mFormatCtx);
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "ES file: seeked to byte 0";
    } else {
        av_seek_frame(mFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    // Parse SPS for PAFF detection (H.264 only)
    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
    if (codecId == AV_CODEC_ID_H264) {
        uint8_t* extradata = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata;
        int extradataSize = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata_size;
        if (extradata && extradataSize > 0) {
            parseH264SpsFromExtradata(extradata, extradataSize);
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Parse H.264 SPS from extradata for PAFF detection
// Sets mBundle.log2MaxFrameNum and mBundle.frameMbsOnlyFlag
// ----------------------------------------------------------------------------
void TTFrameIndexer::parseH264SpsFromExtradata(const uint8_t* data, int size)
{
    if (!data || size < 5) return;

    int nalStart = -1;
    for (int s = TTNaluParser::findStartCodePayload(data, size, 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(data, size, s)) {
        if ((data[s] & 0x1F) == 7) { nalStart = s; break; }
    }
    if (nalStart < 0) return;

    const uint8_t* sps = data + nalStart;
    int spsSize = size - nalStart;
    int bitPos = 8;

    int profileIdc = static_cast<int>(TTNaluParser::readBits(sps, spsSize, bitPos, 8));
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);  // constraint+reserved
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);  // level_idc
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);  // sps_id

    if (TTNaluParser::isH264HighProfile(static_cast<uint32_t>(profileIdc))) {
        int chromaFormatIdc = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        if (chromaFormatIdc == 3) TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        uint32_t scalingPresent = TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        if (scalingPresent) {
            int numLists = (chromaFormatIdc != 3) ? 8 : 12;
            for (int i = 0; i < numLists; i++) {
                if (TTNaluParser::readBits(sps, spsSize, bitPos, 1)) {
                    int listSize = (i < 6) ? 16 : 64;
                    int lastScale = 8, nextScale = 8;
                    for (int j = 0; j < listSize; j++) {
                        if (nextScale != 0) {
                            int delta = TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
                            nextScale = (lastScale + delta + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    mBundle.log2MaxFrameNum = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos)) + 4;

    int pocType = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
    if (pocType == 0) {
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    } else if (pocType == 1) {
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        int n = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        // Spec H.264 7.4.2.1.1: num_ref_frames_in_pic_order_cnt_cycle <= 255.
        if (n > 256) return;
        for (int i = 0; i < n; i++) TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
    }

    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    TTNaluParser::readBits(sps, spsSize, bitPos, 1);
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);

    mBundle.frameMbsOnlyFlag = (TTNaluParser::readBits(sps, spsSize, bitPos, 1) == 1);

    if (!mBundle.frameMbsOnlyFlag) {
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "FFmpegWrapper SPS: frame_mbs_only_flag=0, log2_max_frame_num=" << mBundle.log2MaxFrameNum;
    }
}

// ----------------------------------------------------------------------------
// Parse H.264 field info from packet data (field_pic_flag, bottom_field_flag)
// ----------------------------------------------------------------------------
TTFieldInfo TTFrameIndexer::parseH264FieldInfo(const uint8_t* data, int size,
                                               bool frameMbsOnlyFlag, int log2MaxFrameNum)
{
    TTFieldInfo result = {false, false, -1};
    if (!data || size < 4 || frameMbsOnlyFlag) return result;

    int nalStart = -1;
    for (int s = TTNaluParser::findStartCodePayload(data, size, 0); s >= 0;
         s = TTNaluParser::findStartCodePayload(data, size, s)) {
        uint8_t nalType = data[s] & 0x1F;
        if (nalType == 1 || nalType == 5) { nalStart = s; break; }
    }

    if (nalStart < 0 && size >= 3) {
        uint8_t nalType = data[0] & 0x1F;
        if (nalType == 1 || nalType == 5) nalStart = 0;
    }
    if (nalStart < 0) return result;

    const uint8_t* nal = data + nalStart;
    int nalSize = size - nalStart;
    int bitPos = 8;

    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // first_mb_in_slice
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // slice_type
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);  // pps_id

    result.frameNum = static_cast<int>(TTNaluParser::readBits(nal, nalSize, bitPos, log2MaxFrameNum));

    result.isField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    if (result.isField) {
        result.isBottomField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    }

    return result;
}

// ----------------------------------------------------------------------------
// Scan: append one TTFrameInfo per video packet (raw — no field merging here)
// ----------------------------------------------------------------------------
void TTFrameIndexer::scanPacketsIntoRawIndex(int videoStreamIndex,
                                            const ProgressFn& progressFn)
{
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        setError("Could not allocate packet");
        return;
    }

    TTStreamInfo streamInfo = ttStreamInfo(mFormatCtx, videoStreamIndex);
    int64_t estimatedFrames = streamInfo.numFrames > 0 ? streamInfo.numFrames : 10000;
    int64_t lastProgress = -1;
    int rawCount = 0;

    // Prefer byte-position progress over the frame-count estimate: raw ES
    // files report no frame count (estimatedFrames falls back to a fixed
    // 10000), so on real recordings (~360000 frames for a 2h capture) the
    // frame-based progress hits 100 at ~3% of the file and the `<= 100`
    // gate below then silences every further emission for the rest of the
    // scan. Byte position is monotonic and known up front for seekable
    // input, so it stays accurate for the whole scan.
    int64_t totalBytes = (mFormatCtx->pb) ? avio_size(mFormatCtx->pb) : -1;
    const bool useByteProgress = totalBytes > 0;
    int64_t lastValidPos = 0;

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Building frame index for stream" << videoStreamIndex;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Estimated frames:" << estimatedFrames << "totalBytes:" << totalBytes;

    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;

    // POC collection for the display-order map (H.26x only). POC arrives
    // emission-side (parser lags one packet); IDR is detected input-side.
    const bool collectPoc = (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC);
    TTPocCollector pocCollector(collectPoc ? codecId : AV_CODEC_ID_NONE);
    TTLeadingPicClassifier leadingClassifier(collectPoc ? codecId : AV_CODEC_ID_NONE);

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            TTFrameInfo info;
            info.pts        = packet->pts;
            info.dts        = packet->dts;
            info.fileOffset = packet->pos;
            info.packetSize = packet->size;
            info.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            info.frameIndex = -1;       // filled by finalizeFrameIndex
            info.gopIndex   = -1;       // filled by finalizeFrameIndex
            info.isFieldCoded = false;  // may be set true below

            if (collectPoc) {
                info.isIDR = TTPocCollector::packetIsIDR(packet->data, packet->size, codecId);
                info.isDroppedLeading = leadingClassifier.classifyPacket(packet->data, packet->size);
                pocCollector.feedPacket(packet->data, packet->size);
            }

            // Field detection (H.264 PAFF only)
            if (codecId == AV_CODEC_ID_H264 && !mBundle.frameMbsOnlyFlag) {
                TTFieldInfo fi = parseH264FieldInfo(packet->data, packet->size,
                                                    mBundle.frameMbsOnlyFlag,
                                                    mBundle.log2MaxFrameNum);
                if (fi.isField) {
                    mBundle.isPAFF = true;
                    info.isFieldCoded  = true;
                    info.isBottomField = fi.isBottomField;
                    info.paffFrameNum  = fi.frameNum;
                }
            }

            // Frame type
            if (info.isKeyframe) {
                info.frameType = AV_PICTURE_TYPE_I;
            } else if (codecId == AV_CODEC_ID_H264) {
                int slice = TTNaluParser::parseH264SliceTypeFromPacket(packet->data, packet->size);
                info.frameType = (slice == H264::SLICE_B) ? AV_PICTURE_TYPE_B
                               : (slice == H264::SLICE_I) ? AV_PICTURE_TYPE_I
                                                          : AV_PICTURE_TYPE_P;
            } else if (codecId == AV_CODEC_ID_HEVC) {
                int slice = TTNaluParser::parseH265SliceTypeFromPacket(packet->data, packet->size);
                info.frameType = (slice == H265::SLICE_B) ? AV_PICTURE_TYPE_B
                               : (slice == H265::SLICE_I) ? AV_PICTURE_TYPE_I
                                                          : AV_PICTURE_TYPE_P;
            } else {
                info.frameType = AV_PICTURE_TYPE_P;
            }

            mBundle.index.append(info);
            rawCount++;

            if (useByteProgress) {
                if (packet->pos >= 0)
                    lastValidPos = packet->pos;
                int64_t progress = qMin<int64_t>((lastValidPos * 100) / totalBytes, 100);
                if (progress != lastProgress) {
                    if (progressFn) progressFn(static_cast<int>(progress),
                        QObject::tr("Indexing frame %1...").arg(rawCount));
                    lastProgress = progress;
                }
            } else {
                // Non-seekable input: fall back to the frame-count estimate
                // (unchanged from before this fix — still subject to the
                // same >100% silencing when estimatedFrames underestimates,
                // but only reachable when byte-based progress is unavailable).
                int64_t progress = (rawCount * 100) / estimatedFrames;
                if (progress != lastProgress && progress <= 100) {
                    if (progressFn) progressFn(static_cast<int>(progress),
                        QObject::tr("Indexing frame %1...").arg(rawCount));
                    lastProgress = progress;
                }
            }
        }
        av_packet_unref(packet);
    }

    if (collectPoc) {
        pocCollector.finish();
        const QVector<int>& pocs = pocCollector.pocs();
        if (pocs.size() == mBundle.index.size()) {
            for (int i = 0; i < pocs.size(); ++i)
                mBundle.index[i].poc = pocs[i];
        } else {
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("POC collection mismatch: %1 emissions for %2 packets "
                        "- display map falls back to identity")
                    .arg(pocs.size()).arg(mBundle.index.size()));
        }
    }

    av_packet_free(&packet);
}

// ----------------------------------------------------------------------------
// PAFF post-processing: collapse adjacent top+bottom field pairs in-place
// ----------------------------------------------------------------------------
void TTFrameIndexer::mergePAFFFieldsInIndex()
{
    mBundle.rawPacketCount = mBundle.index.size();
    mBundle.rawToMerged.clear();
    if (!mBundle.isPAFF) return;                 // identity map (empty)

    mBundle.rawToMerged.resize(mBundle.rawPacketCount);

    int w = 0;  // write index
    for (int r = 0; r < mBundle.index.size(); ) {
        const TTFrameInfo& cur = mBundle.index[r];
        bool merged = false;

        if (cur.isFieldCoded && !cur.isBottomField && r + 1 < mBundle.index.size()) {
            const TTFrameInfo& next = mBundle.index[r + 1];
            if (next.isFieldCoded && next.isBottomField &&
                next.paffFrameNum == cur.paffFrameNum)
            {
                // Merge: keep top's PTS/DTS/offset/type/keyframe/POC, sum packetSize
                TTFrameInfo merged_info = cur;
                merged_info.packetSize += next.packetSize;
                mBundle.index[w] = merged_info;
                mBundle.rawToMerged[r]     = w;      // top field
                mBundle.rawToMerged[r + 1] = ~w;     // bottom field, collapsed
                w++;
                r += 2;
                merged = true;
            }
        }

        if (!merged) {
            if (w != r) mBundle.index[w] = mBundle.index[r];
            mBundle.rawToMerged[r] = w;
            w++;
            r++;
        }
    }
    while (mBundle.index.size() > w) mBundle.index.removeLast();
}

// ----------------------------------------------------------------------------
// Assign gopIndex (increments at each keyframe) and frameIndex (= position)
// ----------------------------------------------------------------------------
void TTFrameIndexer::finalizeFrameIndex()
{
    int currentGOP = 0;
    for (int i = 0; i < mBundle.index.size(); ++i) {
        if (i > 0 && mBundle.index[i].isKeyframe) {
            currentGOP++;
        }
        mBundle.index[i].gopIndex   = currentGOP;
        mBundle.index[i].frameIndex = i;
    }
}

// ----------------------------------------------------------------------------
// Assign sequential PTS/DTS to mBundle.index from frame rate (.info or stream)
// ----------------------------------------------------------------------------
void TTFrameIndexer::assignPtsFromFrameRate(int videoStreamIndex)
{
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Elementary stream detected - calculating PTS/DTS from frame rate";

    // Get frame rate from .info file if available, otherwise from stream
    TTStreamInfo streamInfo = ttStreamInfo(mFormatCtx, videoStreamIndex);
    double frameRate = streamInfo.frameRate;
    QString sourceFile = QString::fromUtf8(mFormatCtx->url);
    QString infoFile = TTESInfo::findInfoFile(sourceFile);

    if (!infoFile.isEmpty()) {
        TTESInfo esInfo(infoFile);
        if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
            frameRate = esInfo.frameRate();
            if (TTSettings::instance()->logFFmpegDecoder())
                qDebug() << "Using frame rate from .info file:" << frameRate;
        }
    }

    // Validate frame rate
    if (frameRate <= 0 || frameRate > 120) {
        frameRate = 25.0; // Default fallback
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("Invalid frame rate, using default: %1").arg(frameRate));
    }

    // PAFF: field-rate reported as frame-rate, correct to actual frame-rate
    if (mBundle.isPAFF && frameRate > 30) {
        if (TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "PAFF: correcting frame rate from" << frameRate << "to" << frameRate / 2.0;
        frameRate /= 2.0;
    }

    // Get time base from stream
    AVStream* videoStream = mFormatCtx->streams[videoStreamIndex];
    AVRational timeBase = videoStream->time_base;

    // Calculate frame duration in stream time base
    // pts_increment = time_base / frame_rate
    // For time_base = 1/90000 and frame_rate = 25, pts_increment = 3600
    int64_t frameDuration = av_rescale_q(1, av_make_q(1, static_cast<int>(frameRate * 1000)), timeBase) / 1000;
    if (frameDuration <= 0) {
        frameDuration = av_rescale_q(1, av_make_q(1, 25), timeBase); // Fallback to 25fps
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Time base:" << timeBase.num << "/" << timeBase.den;
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Frame rate:" << frameRate << "fps";
    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Frame duration:" << frameDuration << "ticks";

    // Assign sequential PTS/DTS values
    int64_t currentPts = 0;
    for (int i = 0; i < mBundle.index.size(); ++i) {
        mBundle.index[i].pts = currentPts;
        mBundle.index[i].dts = currentPts;
        currentPts += frameDuration;
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "Calculated timestamps for" << mBundle.index.size() << "frames";
    if (TTSettings::instance()->logFFmpegDecoder()) {
        qDebug() << "First PTS:" << mBundle.index.first().pts
                 << "Last PTS:" << mBundle.index.last().pts;
    }
}

// ----------------------------------------------------------------------------
// Build GOP table from the finalized frame index
// ----------------------------------------------------------------------------
void TTFrameIndexer::buildGops()
{
    mBundle.gops.clear();

    int currentGOP = -1;
    TTGOPInfo gopInfo;

    for (int i = 0; i < mBundle.index.size(); i++) {
        const TTFrameInfo& frame = mBundle.index[i];

        if (frame.gopIndex != currentGOP) {
            // Save previous GOP
            if (currentGOP >= 0) {
                gopInfo.endFrame = i - 1;
                gopInfo.endPts = mBundle.index[i - 1].pts;
                mBundle.gops.append(gopInfo);
            }

            // Start new GOP
            currentGOP = frame.gopIndex;
            gopInfo.gopIndex = currentGOP;
            gopInfo.startFrame = i;
            gopInfo.startPts = frame.pts;
            gopInfo.isClosed = true; // Assume closed, would need more analysis for accuracy
        }
    }

    // Save last GOP
    if (currentGOP >= 0 && !mBundle.index.isEmpty()) {
        gopInfo.endFrame = mBundle.index.size() - 1;
        gopInfo.endPts = mBundle.index.last().pts;
        mBundle.gops.append(gopInfo);
    }

    if (TTSettings::instance()->logFFmpegDecoder())
        qDebug() << "GOP index built:" << mBundle.gops.size() << "GOPs";
}

// ----------------------------------------------------------------------------
// Display-order map from the finalized index
// ----------------------------------------------------------------------------
void TTFrameIndexer::buildDisplayMap(int avCodecId)
{
    // Identity fallback covers: MPEG-2, missing POC data, degenerate parser
    // output. Identity == pre-map behavior.
    auto identity = [this](const char* reason) {
        QVector<int> ranks(mBundle.index.size());
        for (int i = 0; i < ranks.size(); ++i) ranks[i] = i;
        mBundle.displayMap.buildFromRanks(ranks);
        if (reason && TTSettings::instance()->logFFmpegDecoder())
            qDebug() << "display-order map: identity fallback -" << reason;
    };

    mBundle.displayMap = TTDisplayOrderMap();
    if (mBundle.index.isEmpty()) return;
    if (mBundle.index[0].poc == INT_MIN) { identity("no POC collected"); return; }

    QVector<TTPocEntry> entries(mBundle.index.size());
    bool allSame = true;
    for (int i = 0; i < mBundle.index.size(); ++i) {
        entries[i] = {mBundle.index[i].poc, mBundle.index[i].isIDR,
                      mBundle.index[i].isDroppedLeading, mBundle.index[i].isKeyframe};
        if (mBundle.index[i].poc != mBundle.index[0].poc) allSame = false;
    }
    if (allSame && mBundle.index.size() > 1) { identity("constant POC"); return; }

    // H.264 open-GOP cold start: mark leading pics libav drops (mirrors the HEVC
    // RASL handling done input-side via TTLeadingPicClassifier). No-op otherwise.
    TTDisplayOrderMap::markH264ColdStartLeadingPics(entries, avCodecId);

    mBundle.displayMap.build(entries);
    if (!mBundle.displayMap.isValid()) identity("rank validation failed");
}
