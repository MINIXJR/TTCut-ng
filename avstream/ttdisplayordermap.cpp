/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2026 MINIXJR                                                 */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttdisplayordermap.h"

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
    mDisplayToDecode = QVector<int>(decodeToDisplay.size(), -1);
    for (int i = 0; i < decodeToDisplay.size(); ++i) {
        const int rank = decodeToDisplay[i];
        if (rank < 0 || rank >= mDisplayToDecode.size()) {  // not a permutation
            mDecodeToDisplay.clear();
            mDisplayToDecode.clear();
            TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
                QString("display-order map rejected: rank %1 out of range").arg(rank));
            return;
        }
        if (mDisplayToDecode[rank] != -1) {  // duplicate rank
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
TTPocCollector::TTPocCollector(int avCodecId)
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

void TTPocCollector::feedPacket(const uint8_t* data, int size)
{
    if (!mParser) return;
    while (size > 0) {
        uint8_t* outData = nullptr;
        int outSize = 0;
        int used = av_parser_parse2(mParser, mCtx, &outData, &outSize,
                                    data, size,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
        if (used <= 0) return;  // <= 0: error (<0) or stall (=0); both must break the loop
        data += used;
        size -= used;
        if (outSize > 0) mPocs.append(mParser->output_picture_number);
    }
}

void TTPocCollector::finish()
{
    if (!mParser) return;
    uint8_t* outData = nullptr;
    int outSize = 0;
    av_parser_parse2(mParser, mCtx, &outData, &outSize, nullptr, 0,
                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (outSize > 0) mPocs.append(mParser->output_picture_number);
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

    TTPocCollector collector(codecId);
    QVector<bool> idrFlags;
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: packet alloc failed"));
        avformat_close_input(&fmt);
        return map;
    }

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) {
            idrFlags.append(TTPocCollector::packetIsIDR(pkt->data, pkt->size, codecId));
            collector.feedPacket(pkt->data, pkt->size);
        }
        av_packet_unref(pkt);
    }
    collector.finish();
    av_packet_free(&pkt);
    avformat_close_input(&fmt);

    if (collector.pocs().size() != idrFlags.size()) {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("display-order map: POC/packet count mismatch (%1 vs %2) for %3")
                .arg(collector.pocs().size()).arg(idrFlags.size()).arg(filePath));
        return map;
    }

    QVector<TTPocEntry> entries(idrFlags.size());
    for (int i = 0; i < idrFlags.size(); ++i)
        entries[i] = {collector.pocs()[i], idrFlags[i]};
    map.build(entries);
    return map;
}
