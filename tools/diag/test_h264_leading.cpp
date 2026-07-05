// test_h264_leading — gate for H.264 cold-start leading-picture handling in
// TTDisplayOrderMap (spec 2026-07-05-h264-coldstart-leading-pics-design.md).
//
// Builds the REAL production map via TTDisplayOrderMap::buildFromFile() and
// compares it against decoder ground truth (thread_count=1, pts=AU index —
// the Pass-B pattern from test_parser_poc). The whole bug lives at the cold
// start, so a head-slice clip exercises it fully and decodes fast.
//
// Asserts:
//   - drop count (= count() - displayCount()) == expected
//   - displayToDecode(0) == first decoder-output AU (the I, not a dropped pic)
//   - displayToDecode(d) == gt[d] for d in [0, min(displayCount, gt, CAP))
//
// Build (via tools/diag/Makefile): make test_h264_leading
//
// Usage:
//   ./test_h264_leading <es-file> [expectedDrops]
//     expectedDrops  - if given, asserted exactly; else only reported

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

#include <QString>
#include "../../avstream/ttdisplayordermap.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

static int failures = 0;

static void expectEq(const char* name, long got, long expected)
{
    if (got == expected) { printf("PASS  %s = %ld\n", name, got); return; }
    ++failures;
    printf("FAIL  %s: got %ld, expected %ld\n", name, got, expected);
}

// Decoder ground truth: AU tags in decoder output (display) order.
static std::vector<int> decodeGroundTruth(const char* path)
{
    std::vector<int> out;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return out; }
    int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) { avformat_close_input(&fmt); return out; }

    AVCodecParameters* par = fmt->streams[vIdx]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) { avformat_close_input(&fmt); return out; }
    AVCodecContext* dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, par);
    dctx->thread_count = 1;                        // deterministic, matches app decoder
    if (avcodec_open2(dctx, dec, nullptr) < 0) {
        avcodec_free_context(&dctx); avformat_close_input(&fmt); return out;
    }

    AVFrame*  frame = av_frame_alloc();
    AVPacket* pkt   = av_packet_alloc();
    long auCounter = 0;
    auto drain = [&](bool flush) {
        if (flush) avcodec_send_packet(dctx, nullptr);
        while (avcodec_receive_frame(dctx, frame) == 0) {
            out.push_back((int)frame->pts);        // pts carries the tagged AU index
            av_frame_unref(frame);
        }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) {
            pkt->pts = auCounter;
            pkt->dts = auCounter;
            ++auCounter;
            avcodec_send_packet(dctx, pkt);
            drain(false);
        }
        av_packet_unref(pkt);
    }
    drain(true);

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dctx);
    avformat_close_input(&fmt);
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <es-file> [expectedDrops]\n", argv[0]);
        return 2;
    }
    const char* file = argv[1];
    const bool haveExpDrops = (argc > 2);
    const int  expDrops     = haveExpDrops ? atoi(argv[2]) : -1;

    printf("=== %s ===\n", file);

    TTDisplayOrderMap map = TTDisplayOrderMap::buildFromFile(QString::fromUtf8(file));
    if (!map.isValid()) { printf("FAIL  buildFromFile invalid\n"); return 1; }

    std::vector<int> gt = decodeGroundTruth(file);
    if (gt.empty()) { printf("FAIL  decoder ground truth empty\n"); return 1; }

    const int rawAUs   = map.count();
    const int dispCnt  = map.displayCount();
    const int drops    = rawAUs - dispCnt;
    printf("INFO  raw AUs = %d, displayCount = %d, drops = %d, decoder frames = %zu\n",
           rawAUs, dispCnt, drops, gt.size());

    if (haveExpDrops) expectEq("cold-start drops", drops, expDrops);

    // displayToDecode(0) must be the decoder's first output AU (the keyframe),
    // NOT a dropped leading pic. This is the exact property that hung decodeFrame.
    expectEq("displayToDecode(0)", map.displayToDecode(0), gt[0]);

    // Prefix alignment: map display->decode must equal decoder output order.
    // Head-slice clips truncate the last GOP, so compare only the common prefix
    // (the whole regression is at the cold start; a truncated tail is expected).
    const int CAP = 2000;
    const int K = std::min({dispCnt, (int)gt.size(), CAP});
    int mismatches = 0, firstBad = -1;
    for (int d = 0; d < K; ++d) {
        if (map.displayToDecode(d) != gt[d]) {
            if (firstBad < 0) firstBad = d;
            ++mismatches;
        }
    }
    if (mismatches == 0) {
        printf("PASS  display->decode prefix aligned over [0,%d)\n", K);
    } else {
        ++failures;
        printf("FAIL  %d/%d prefix mismatches (first at display %d: map=%d gt=%d)\n",
               mismatches, K, firstBad,
               map.displayToDecode(firstBad), gt[firstBad]);
    }

    printf(failures ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
