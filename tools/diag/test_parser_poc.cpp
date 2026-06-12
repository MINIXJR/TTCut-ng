// test_parser_poc — empirical check for display-order option (d):
// derive display order from libav's AVCodecParserContext::output_picture_number
// (parser-only pass, no pixel decode) and verify it against the decoder's
// real output order (ground truth).
//
// Pass A (parser): av_read_frame -> av_parser_parse2 -> record POC per AU.
//                  Display order = DPB-style bumping sort on POC.
// Pass B (decode): av_read_frame -> decoder (thread_count=1, pts=AU index)
//                  -> record output order of AU tags. Ground truth.
//
// Build:
//   g++ -O2 -std=gnu++17 -o test_parser_poc test_parser_poc.cpp \
//       $(pkg-config --cflags --libs libavformat libavcodec libavutil)
//
// Usage:
//   ./test_parser_poc <file> [maxAUs] [winStart winEnd]
//     maxAUs   - stop after N access units (default: whole file)
//     win*     - print per-AU detail for AU index range

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <deque>
#include <algorithm>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

struct AUInfo {
    int     index;       // decode-order AU index (packet order)
    int     poc;         // parser output_picture_number
    int     pictType;    // AV_PICTURE_TYPE_*
    bool    key;
    int     picStruct;   // AVPictureStructure
};

static const char* typeChar(int t)
{
    switch (t) {
        case AV_PICTURE_TYPE_I: return "I";
        case AV_PICTURE_TYPE_P: return "P";
        case AV_PICTURE_TYPE_B: return "B";
        default:                return "?";
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file> [maxAUs] [winStart winEnd]\n", argv[0]);
        return 1;
    }
    const char* path  = argv[1];
    long maxAUs       = (argc > 2) ? atol(argv[2]) : -1;
    long winStart     = (argc > 4) ? atol(argv[3]) : -1;
    long winEnd       = (argc > 4) ? atol(argv[4]) : -1;

    av_log_set_level(AV_LOG_ERROR);

    // ---------- open input ----------
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    avformat_find_stream_info(fmt, nullptr);
    int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) { fprintf(stderr, "no video stream\n"); return 1; }
    AVCodecParameters* par = fmt->streams[vIdx]->codecpar;
    printf("codec: %s\n", avcodec_get_name(par->codec_id));

    // ================= Pass A: parser-only =================
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    AVCodecContext* pctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(pctx, par);
    AVCodecParserContext* parser = av_parser_init(par->codec_id);
    if (!parser) { fprintf(stderr, "no parser\n"); return 1; }

    std::vector<AUInfo> aus;
    AVPacket* pkt = av_packet_alloc();
    auto tA0 = std::chrono::steady_clock::now();

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) {
            // feed the complete packet; parser may emit 0..n frames
            const uint8_t* data = pkt->data;
            int size = pkt->size;
            while (size > 0) {
                uint8_t* outData = nullptr; int outSize = 0;
                int used = av_parser_parse2(parser, pctx, &outData, &outSize,
                                            data, size,
                                            AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
                if (used < 0) break;
                data += used; size -= used;
                if (outSize > 0) {
                    AUInfo a;
                    a.index     = (int)aus.size();
                    a.poc       = parser->output_picture_number;
                    a.pictType  = parser->pict_type;
                    a.key       = parser->key_frame == 1;
                    a.picStruct = parser->picture_structure;
                    aus.push_back(a);
                }
            }
            if (maxAUs > 0 && (long)aus.size() >= maxAUs) { av_packet_unref(pkt); break; }
        }
        av_packet_unref(pkt);
    }
    // flush parser
    {
        uint8_t* outData = nullptr; int outSize = 0;
        av_parser_parse2(parser, pctx, &outData, &outSize, nullptr, 0,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
        if (outSize > 0) {
            AUInfo a;
            a.index = (int)aus.size();
            a.poc = parser->output_picture_number;
            a.pictType = parser->pict_type;
            a.key = parser->key_frame == 1;
            a.picStruct = parser->picture_structure;
            aus.push_back(a);
        }
    }
    auto tA1 = std::chrono::steady_clock::now();
    double secA = std::chrono::duration<double>(tA1 - tA0).count();

    printf("\n=== Pass A (parser only): %zu AUs in %.2f s (%.0f AU/s)\n",
           aus.size(), secA, aus.size() / (secA > 0 ? secA : 1));

    // POC sanity: how many AUs have poc != 0 pattern / all-same?
    int pocAllSame = 1;
    for (size_t i = 1; i < aus.size(); i++)
        if (aus[i].poc != aus[0].poc) { pocAllSame = 0; break; }
    if (pocAllSame) {
        printf("WARNING: output_picture_number is constant (%d) -> parser does NOT fill POC for this codec!\n",
               aus.empty() ? -1 : aus[0].poc);
    }

    // optional detail window
    if (winStart >= 0) {
        printf("\nAU window %ld..%ld (decode order):\n", winStart, winEnd);
        printf("  AU      POC   type key picStruct\n");
        for (long i = winStart; i <= winEnd && i < (long)aus.size(); i++)
            printf("  %-7d %-5d %s    %d   %d\n",
                   aus[i].index, aus[i].poc, typeChar(aus[i].pictType),
                   aus[i].key ? 1 : 0, aus[i].picStruct);
    }

    // ---------- derive display order from POC (DPB-style bumping) ----------
    // Reorder window: generous fixed depth; flush on big backward POC jump (IDR reset).
    const int REORDER_DEPTH = 16;
    std::vector<int> displayFromPoc;             // AU indices in display order
    displayFromPoc.reserve(aus.size());
    {
        std::deque<AUInfo> dpb;
        long lastEmittedPoc = INT64_MIN;
        auto emitMin = [&]() {
            auto it = std::min_element(dpb.begin(), dpb.end(),
                [](const AUInfo& x, const AUInfo& y){ return x.poc < y.poc; });
            lastEmittedPoc = it->poc;
            displayFromPoc.push_back(it->index);
            dpb.erase(it);
        };
        for (const AUInfo& a : aus) {
            // IDR / sequence reset: POC jumps far backwards -> flush DPB first
            if (lastEmittedPoc != INT64_MIN && a.poc < lastEmittedPoc - 1000) {
                while (!dpb.empty()) emitMin();
                lastEmittedPoc = INT64_MIN;
            }
            dpb.push_back(a);
            if ((int)dpb.size() > REORDER_DEPTH) emitMin();
        }
        while (!dpb.empty()) emitMin();
    }

    // parser-only mode (timing runs): skip decode pass entirely
    if (argc > 2 && strcmp(argv[argc - 1], "parseronly") == 0) {
        printf("\n(parseronly: decode pass skipped)\n");
        av_packet_free(&pkt);
        avcodec_free_context(&pctx);
        av_parser_close(parser);
        avformat_close_input(&fmt);
        return 0;
    }

    // ================= Pass B: real decode (ground truth) =================
    avformat_close_input(&fmt);
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        fprintf(stderr, "reopen failed\n"); return 1;
    }
    avformat_find_stream_info(fmt, nullptr);
    vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    par = fmt->streams[vIdx]->codecpar;

    AVCodecContext* dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, par);
    dctx->thread_count = 1;                      // deterministic, matches app decoder
    if (avcodec_open2(dctx, dec, nullptr) < 0) {
        fprintf(stderr, "decoder open failed\n"); return 1;
    }
    AVFrame* frame = av_frame_alloc();
    std::vector<int> displayFromDecode;          // AU tags in decoder output order
    displayFromDecode.reserve(aus.size());

    long auCounter = 0;
    auto tB0 = std::chrono::steady_clock::now();
    auto drain = [&](bool flush) {
        if (flush) avcodec_send_packet(dctx, nullptr);
        while (avcodec_receive_frame(dctx, frame) == 0) {
            displayFromDecode.push_back((int)frame->pts);
            av_frame_unref(frame);
        }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) {
            pkt->pts = auCounter;                // tag AU index (packet order)
            pkt->dts = auCounter;
            auCounter++;
            avcodec_send_packet(dctx, pkt);
            drain(false);
            if (maxAUs > 0 && auCounter >= maxAUs) { av_packet_unref(pkt); break; }
        }
        av_packet_unref(pkt);
    }
    drain(true);
    auto tB1 = std::chrono::steady_clock::now();
    double secB = std::chrono::duration<double>(tB1 - tB0).count();

    printf("\n=== Pass B (full decode, 1 thread): %zu frames in %.2f s (%.0f fps)\n",
           displayFromDecode.size(), secB,
           displayFromDecode.size() / (secB > 0 ? secB : 1));

    // ================= Compare =================
    // PAFF: parser emits per-field AUs while the decoder merges fields into
    // frames. Collapse the POC display sequence: fields come out of the
    // bumping in display order (top,bottom,top,bottom...), so drop every
    // second entry and keep the first field's AU index (matches the decoder
    // frame tag, which carries the first field packet's pts).
    bool isFieldStream = false;
    for (const AUInfo& a : aus)
        if (a.picStruct == AV_PICTURE_STRUCTURE_TOP_FIELD ||
            a.picStruct == AV_PICTURE_STRUCTURE_BOTTOM_FIELD) { isFieldStream = true; break; }

    std::vector<int> pocDisplay;
    if (isFieldStream) {
        printf("\nfield-coded stream (PAFF): collapsing field pairs in POC sequence\n");
        for (size_t i = 0; i + 1 < displayFromPoc.size(); i += 2)
            pocDisplay.push_back(displayFromPoc[i]);
    } else {
        pocDisplay = displayFromPoc;
    }

    printf("\n=== Comparison: POC-derived display order vs decoder output order\n");
    printf("pass A display units: %zu, pass B frames: %zu\n",
           pocDisplay.size(), displayFromDecode.size());

    // Decoder may drop leading pictures at a broken-GOP stream start
    // (RASL after CRA / open-GOP B before first I). Align: locate the first
    // decoded AU tag in the POC sequence and compare from there.
    size_t offset = 0;
    if (!displayFromDecode.empty()) {
        for (size_t i = 0; i < pocDisplay.size() && i < 64; i++)
            if (pocDisplay[i] == displayFromDecode[0]) { offset = i; break; }
    }
    if (offset > 0)
        printf("alignment: decoder starts at POC display position %zu "
               "(%zu leading picture(s) dropped at cold start)\n", offset, offset);

    size_t n = std::min(pocDisplay.size() - offset, displayFromDecode.size());
    // ignore the tail where truncation (maxAUs) leaves the two passes uneven
    size_t tailSkip = 32;
    if (n > tailSkip) n -= tailSkip;
    size_t mismatches = 0, firstShown = 0;
    for (size_t i = 0; i < n; i++) {
        if (pocDisplay[i + offset] != displayFromDecode[i]) {
            mismatches++;
            if (firstShown < 10) {
                printf("  display[%zu]: POC-order says AU %d, decoder says AU %d\n",
                       i, pocDisplay[i + offset], displayFromDecode[i]);
                firstShown++;
            }
        }
    }
    printf("mismatches: %zu / %zu compared (tail of %zu skipped)\n", mismatches, n, tailSkip);
    printf(mismatches == 0 ? "RESULT: IDENTICAL — POC-derived display order is exact.\n"
                           : "RESULT: DIFFERS — POC-derived order is NOT a drop-in oracle.\n");

    // detail window in display order around winStart (display positions)
    if (winStart >= 0) {
        printf("\nDisplay window %ld..%ld (decoder positions, POC column offset-aligned):\n",
               winStart, winEnd);
        printf("  dispPos  AU(poc-order)  AU(decoder)\n");
        for (long i = winStart; i <= winEnd && i < (long)n; i++)
            printf("  %-8ld %-14d %d\n", i, pocDisplay[i + offset], displayFromDecode[i]);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dctx);
    avcodec_free_context(&pctx);
    av_parser_close(parser);
    avformat_close_input(&fmt);
    return 0;
}
