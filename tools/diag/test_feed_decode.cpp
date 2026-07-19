// Diag (Befund E, 2026-07-19): minimal reproduction of the TTESSmartCut decode
// feed. Feeds parser AUs [from..to] to a decoder configured exactly like
// TTESSmartCut::setupDecoder (extradata = all SPS+PPS, thread_count=1) and
// prints the luma mean of every output frame, so a "decoder outputs flat gray"
// condition is measurable without the encoder in the loop.
//
// Usage: test_feed_decode <es> <from> <to> [noextra] [threads=N]
//   noextra    do not set decoder extradata (rely on inline SPS/PPS only)
//   threads=N  thread_count override (default 1, like the smart cut)
#include <QCoreApplication>
#include <QByteArray>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "avstream/ttnaluparser.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

static double lumaMean(const AVFrame* f)
{
    long long sum = 0;
    for (int y = 0; y < f->height; ++y) {
        const uint8_t* row = f->data[0] + y * f->linesize[0];
        for (int x = 0; x < f->width; ++x) sum += row[x];
    }
    return (double)sum / ((double)f->width * f->height);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        fprintf(stderr, "usage: %s <es> <from> <to> [noextra] [threads=N]\n", argv[0]);
        return 2;
    }
    QString es = argv[1];
    int from = atoi(argv[2]);
    int to   = atoi(argv[3]);
    bool useExtra = true;
    int threads = 1;
    for (int a = 4; a < argc; ++a) {
        if (!strcmp(argv[a], "noextra")) useExtra = false;
        else if (!strncmp(argv[a], "threads=", 8)) threads = atoi(argv[a] + 8);
    }

    TTNaluParser parser;
    if (!parser.openFile(es) || !parser.parseFile()) {
        fprintf(stderr, "parse failed: %s\n", qPrintable(parser.lastError()));
        return 1;
    }
    fprintf(stderr, "parsed: %d AUs, sps=%d pps=%d codec=%s\n",
            parser.accessUnitCount(), parser.spsCount(), parser.ppsCount(),
            qPrintable(parser.codecName()));

    AVCodecID cid = parser.codecType() == NALU_CODEC_H265 ? AV_CODEC_ID_HEVC
                                                          : AV_CODEC_ID_H264;
    const AVCodec* codec = avcodec_find_decoder(cid);
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    if (useExtra) {
        QByteArray extradata;
        if (parser.codecType() == NALU_CODEC_H265)
            for (int i = 0; i < parser.vpsCount(); i++) extradata.append(parser.getVPS(i));
        for (int i = 0; i < parser.spsCount(); i++) extradata.append(parser.getSPS(i));
        for (int i = 0; i < parser.ppsCount(); i++) extradata.append(parser.getPPS(i));
        dec->extradata = (uint8_t*)av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        dec->extradata_size = extradata.size();
        memcpy(dec->extradata, extradata.constData(), extradata.size());
        fprintf(stderr, "extradata: %d bytes\n", dec->extradata_size);
    } else {
        fprintf(stderr, "extradata: none\n");
    }
    dec->thread_count = threads;
    if (avcodec_open2(dec, codec, nullptr) < 0) {
        fprintf(stderr, "open decoder failed\n");
        return 1;
    }

    int outCount = 0;
    auto drain = [&](bool flush) {
        while (true) {
            AVFrame* f = av_frame_alloc();
            int r = avcodec_receive_frame(dec, f);
            if (r < 0) { av_frame_free(&f); break; }
            printf("out %3d pts=%lld type=%c luma=%.2f%s\n", outCount++,
                   (long long)f->pts,
                   av_get_picture_type_char(f->pict_type),
                   lumaMean(f), flush ? " (flush)" : "");
            av_frame_free(&f);
        }
    };

    for (int i = from; i <= to && i < parser.accessUnitCount(); ++i) {
        QByteArray au = parser.readAccessUnitData(i);
        AVPacket* pkt = av_packet_alloc();
        av_new_packet(pkt, au.size());
        memcpy(pkt->data, au.constData(), au.size());
        pkt->pts = pkt->dts = i;
        while (true) {
            int r = avcodec_send_packet(dec, pkt);
            if (r == 0) break;
            if (r == AVERROR(EAGAIN)) { drain(false); continue; }
            fprintf(stderr, "send_packet au=%d failed: %d\n", i, r);
            break;
        }
        av_packet_free(&pkt);
        drain(false);
    }
    avcodec_send_packet(dec, nullptr);
    drain(true);
    fprintf(stderr, "total output frames: %d\n", outCount);
    avcodec_free_context(&dec);
    return 0;
}
