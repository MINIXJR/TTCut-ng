/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2026 MINIXJR                                                 */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttdisplayordermap.h"
#include "ttnaluparser.h"

#include "../common/ttmessagelogger.h"

#include <QList>
#include <QPair>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// H.264/HEVC level limits cap the DPB at 16 frames; no conforming stream can
// reorder further than that.
static const int REORDER_DEPTH = 16;

QVector<int> TTDisplayOrderMap::displayRanksFromPoc(const QVector<TTPocEntry>& entries)
{
    const int n = entries.size();
    QVector<int> decodeToDisplay(n, -1);
    QList<QPair<int, int>> dpb;   // (poc, decodeIdx)
    int nextRank = 0;

    auto emitMin = [&]() {
        int best = 0;
        for (int j = 1; j < dpb.size(); ++j)
            if (dpb[j].first < dpb[best].first) best = j;
        decodeToDisplay[dpb[best].second] = nextRank++;
        dpb.removeAt(best);
    };

    for (int i = 0; i < n; ++i) {
        // Dropped leading pictures (RASL of a NoRaslOutputFlag IRAP) are never
        // output by a conforming decoder: no display rank, never enter the DPB.
        if (entries[i].isDroppedLeading) continue;   // decodeToDisplay[i] stays -1
        // IDR: nothing after it (decode order) displays before it, and POC
        // restarts — flush the reorder buffer first. Non-IDR I (open GOP/CRA)
        // does NOT flush: its leading B/RASL pictures display before it.
        if (entries[i].isIDR) {
            while (!dpb.isEmpty()) emitMin();
        }
        dpb.append(qMakePair(entries[i].poc, i));
        if (dpb.size() > REORDER_DEPTH) emitMin();
    }
    while (!dpb.isEmpty()) emitMin();

    return decodeToDisplay;
}

void TTDisplayOrderMap::build(const QVector<TTPocEntry>& entries)
{
    buildFromRanks(displayRanksFromPoc(entries));
}

void TTDisplayOrderMap::buildFromRanks(const QVector<int>& decodeToDisplay)
{
    mDecodeToDisplay = decodeToDisplay;

    // Count decodable (non-dropped) entries -> display dimension size m.
    int m = 0;
    for (int rank : decodeToDisplay)
        if (rank >= 0) ++m;

    mDisplayToDecode = QVector<int>(m, -1);
    for (int i = 0; i < decodeToDisplay.size(); ++i) {
        const int rank = decodeToDisplay[i];
        if (rank < 0) continue;                 // dropped leading pic — no display slot
        if (rank >= m) {                        // not a permutation of 0..m-1
            mDecodeToDisplay.clear();
            mDisplayToDecode.clear();
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-order map rejected: rank %1 out of range (m=%2)").arg(rank).arg(m));
            return;
        }
        if (mDisplayToDecode[rank] != -1) {     // duplicate rank
            mDecodeToDisplay.clear();
            mDisplayToDecode.clear();
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-order map rejected: duplicate rank %1").arg(rank));
            return;
        }
        mDisplayToDecode[rank] = i;
    }
}

int TTDisplayOrderMap::decodeToDisplay(int decodeIdx) const
{
    if (decodeIdx < 0 || decodeIdx >= mDecodeToDisplay.size()) return decodeIdx;
    return mDecodeToDisplay[decodeIdx];
}

int TTDisplayOrderMap::displayToDecode(int displayPos) const
{
    if (displayPos < 0 || displayPos >= mDisplayToDecode.size()) return displayPos;
    return mDisplayToDecode[displayPos];
}

// ----------------------------------------------------------------------------
// TTPocCollector
// ----------------------------------------------------------------------------
TTPocCollector::TTPocCollector(int avCodecId, bool trackIDR)
    : mTrackIDR(trackIDR)
{
    const AVCodec* codec = avcodec_find_decoder(static_cast<AVCodecID>(avCodecId));
    if (!codec) return;
    mCtx = avcodec_alloc_context3(codec);
    if (!mCtx) return;
    mParser = av_parser_init(avCodecId);
    if (!mParser) {
        avcodec_free_context(&mCtx);
        mCtx = nullptr;
    }
}

TTPocCollector::~TTPocCollector()
{
    if (mParser) av_parser_close(mParser);
    if (mCtx)    avcodec_free_context(&mCtx);
}

void TTPocCollector::feedPacket(const uint8_t* data, int size, bool isIDR)
{
    if (!mParser) return;
    if (mTrackIDR) mIdrQueue.append(isIDR);

    while (size > 0) {
        uint8_t* outData = nullptr;
        int outSize = 0;
        int used = av_parser_parse2(mParser, mCtx, &outData, &outSize,
                                    data, size,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
        if (used < 0) return;  // error
        if (outSize > 0) onEmit();
        if (used == 0) {
            // Parser emitted output without consuming input (e.g. MBAFF alternating
            // pattern: packet N+1 triggers emission of packet N's AU with used=0).
            // If emission happened (outSize>0), loop again to try consuming the
            // same data. If no emission either (true stall), break to avoid infinite loop.
            if (outSize == 0) return;
            // outSize > 0 was already collected above; continue to consume the data
            continue;
        }
        data += used;
        size -= used;
    }
}

void TTPocCollector::onEmit()
{
    const int poc = mParser->output_picture_number;
    mPocs.append(poc);
    if (mTrackIDR) {
        const bool idr = mIdrQueue.isEmpty() ? false : mIdrQueue.takeFirst();
        mEntries.append({poc, idr});
    }
}

void TTPocCollector::finish()
{
    if (!mParser) return;
    uint8_t* outData = nullptr;
    int outSize = 0;
    av_parser_parse2(mParser, mCtx, &outData, &outSize, nullptr, 0,
                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (outSize > 0) onEmit();
}

bool TTPocCollector::packetIsIDR(const uint8_t* data, int size, int avCodecId)
{
    // Walk Annex-B start codes; check NAL type: H.264 IDR = 5,
    // HEVC IDR_W_RADL = 19 / IDR_N_LP = 20.
    for (int i = 0; i + 3 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 &&
            (data[i + 2] == 1 ||
             (data[i + 2] == 0 && i + 4 < size && data[i + 3] == 1))) {
            const int hdr = (data[i + 2] == 1) ? i + 3 : i + 4;
            if (hdr >= size) break;
            if (avCodecId == AV_CODEC_ID_H264) {
                if ((data[hdr] & 0x1F) == 5) return true;
            } else {
                const int nalType = (data[hdr] >> 1) & 0x3F;
                if (nalType == 19 || nalType == 20) return true;
            }
            i = hdr;  // continue after this start code
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Standalone build from ES file (own demux loop)
// ----------------------------------------------------------------------------
TTDisplayOrderMap TTDisplayOrderMap::buildFromFile(const QString& filePath)
{
    TTDisplayOrderMap map;

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, filePath.toUtf8().constData(), nullptr, nullptr) < 0) {
        // avformat_open_input already freed fmt on failure; do NOT call avformat_close_input.
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: cannot open %1").arg(filePath));
        return map;
    }

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: cannot read stream info for %1").arg(filePath));
        avformat_close_input(&fmt);
        return map;
    }

    const int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: no video stream in %1").arg(filePath));
        avformat_close_input(&fmt);
        return map;
    }

    const AVCodecID codecId = fmt->streams[vIdx]->codecpar->codec_id;
    if (codecId != AV_CODEC_ID_H264 && codecId != AV_CODEC_ID_HEVC) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: unsupported codec for %1").arg(filePath));
        avformat_close_input(&fmt);
        return map;
    }

    // Mode 1 (trackIDR=true): IDR flag queued per packet, dequeued at emission.
    // trackIDR MUST be true from construction so that pre-IDR packets are queued
    // correctly (activating mid-stream would misassign IDR flags).
    TTPocCollector collector(codecId, /*trackIDR=*/true);
    TTLeadingPicClassifier leadingClassifier(codecId);
    QVector<bool> droppedPerPacket;   // one entry per video packet (== per AU for HEVC)
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: packet alloc failed"));
        avformat_close_input(&fmt);
        return map;
    }

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) {
            const bool isIDR = TTPocCollector::packetIsIDR(pkt->data, pkt->size, codecId);
            droppedPerPacket.append(leadingClassifier.classifyPacket(pkt->data, pkt->size));
            collector.feedPacket(pkt->data, pkt->size, isIDR);
        }
        av_packet_unref(pkt);
    }
    collector.finish();
    av_packet_free(&pkt);
    avformat_close_input(&fmt);

    QVector<TTPocEntry> entries = collector.entries();   // copy (mutable)
    if (entries.isEmpty()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: no entries collected for %1").arg(filePath));
        return map;
    }
    // Per-AU emission is 1:1 with video packets for HEVC; pair drop flags by index.
    if (droppedPerPacket.size() == entries.size()) {
        for (int i = 0; i < entries.size(); ++i)
            entries[i].isDroppedLeading = droppedPerPacket[i];
    } else {
        // Size divergence would silently revert HEVC to the +k RASL misalignment.
        // Surface it instead of dropping the flags unnoticed.
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: drop-flag count %1 != entry count %2 for %3 "
                    "- RASL leading pics NOT excluded (map may be display-misaligned)")
                .arg(droppedPerPacket.size()).arg(entries.size()).arg(filePath));
    }

    map.build(entries);
    return map;
}

// ----------------------------------------------------------------------------
// TTLeadingPicClassifier
// ----------------------------------------------------------------------------
TTLeadingPicClassifier::TTLeadingPicClassifier(int avCodecId)
    : mIsHevc(avCodecId == AV_CODEC_ID_HEVC)
{}

bool TTLeadingPicClassifier::classifyPacket(const uint8_t* data, int size)
{
    if (!mIsHevc || !data || size < 4) return false;

    // Walk Annex-B start codes; find this AU's first VCL slice NAL type and
    // note any EOS/EOB NAL. HEVC NAL type = (header_byte >> 1) & 0x3F.
    int  vclType = -1;
    bool sawEos  = false;
    for (int i = 0; i + 2 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 &&
            (data[i + 2] == 1 ||
             (data[i + 2] == 0 && i + 3 < size && data[i + 3] == 1))) {
            const int hdr = (data[i + 2] == 1) ? i + 3 : i + 4;
            if (hdr >= size) break;
            const int t = (data[hdr] >> 1) & 0x3F;
            if (t == H265::NAL_EOS || t == H265::NAL_EOB) sawEos = true;
            else if (t <= 31 && vclType < 0) vclType = t;   // first VCL slice (0..31)
            i = hdr;   // resume after this NAL header (mirrors TTPocCollector::packetIsIDR);
                       // the next start code lies well past hdr+1 on Annex-B AU data
        }
    }

    if (sawEos) mPrevWasEos = true;
    if (vclType < 0) return false;   // non-VCL AU (standalone EOS / param sets)

    auto isIrap = [](int t){ return t >= H265::NAL_BLA_W_LP && t <= H265::NAL_CRA_NUT; };
    auto isBla  = [](int t){ return t >= H265::NAL_BLA_W_LP && t <= H265::NAL_BLA_N_LP; };
    auto isIdr  = [](int t){ return t == H265::NAL_IDR_W_RADL || t == H265::NAL_IDR_N_LP; };

    if (isIrap(vclType)) {
        // NoRaslOutputFlag == 1 for: the first IRAP in the bitstream, any IRAP
        // after an EOS, every BLA, and (per HEVC 7.4.2.4) every IDR. Conformant
        // streams never emit RASL after IDR, so arming for IDR is defensive only.
        const bool noRasl = (!mSeenFirstIrap) || mPrevWasEos
                          || isBla(vclType) || isIdr(vclType);
        mSeenFirstIrap   = true;
        mPrevWasEos      = false;
        mInNoRaslLeading = noRasl;     // arm/disarm dropping for this IRAP's leading pics
        return false;                  // the IRAP itself is always displayed
    }
    if (vclType == H265::NAL_RASL_N || vclType == H265::NAL_RASL_R)
        return mInNoRaslLeading;       // dropped iff inside a NoRaslOutput leading seq
    if (vclType == H265::NAL_RADL_N || vclType == H265::NAL_RADL_R)
        return false;                  // RADL displayed; stays in the leading sequence
    mInNoRaslLeading = false;          // first trailing pic ends the leading sequence
    return false;
}
