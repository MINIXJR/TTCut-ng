#define _DEFAULT_SOURCE  /* madvise, MADV_SEQUENTIAL */

/*
 * ttcut-esrepair - Elementary Stream Repair Tool for TTCut-ng
 *
 * Removes defective segments from MPEG-2/H.264/H.265 elementary streams.
 * Defective GOPs (segments with decode errors above a configurable threshold)
 * are excised from the stream, producing a clean ES file suitable for
 * frame-accurate cutting.
 *
 * Copyright (C) 2026 TTCut-ng Project
 * License: GPL v2 or later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

enum CodecType {
    CODEC_MPEG2,
    CODEC_H264,
    CODEC_H265,
    CODEC_UNKNOWN
};

typedef struct {
    int64_t offset;
    int64_t size;
    int     frame_count;
    int     error_count;
    bool    defective;
} Segment;

typedef struct {
    const uint8_t *base;
    int64_t size;
    int64_t pos;
} MmapIOContext;

static const char *codec_names[] = {
    "mpeg2", "h264", "h265", "unknown"
};

static int detect_codec(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return CODEC_UNKNOWN;

    if (strcmp(dot, ".m2v") == 0)
        return CODEC_MPEG2;
    if (strcmp(dot, ".264") == 0 || strcmp(dot, ".h264") == 0)
        return CODEC_H264;
    if (strcmp(dot, ".265") == 0 || strcmp(dot, ".h265") == 0)
        return CODEC_H265;

    return CODEC_UNKNOWN;
}

static int open_mmap(const char *path, const uint8_t **data, int64_t *size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size == 0) {
        fprintf(stderr, "Error: cannot stat '%s' or file is empty: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    *size = st.st_size;
    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  /* fd can be closed after mmap */
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed for '%s': %s\n", path, strerror(errno));
        *data = NULL;
        return -1;
    }
    *data = (const uint8_t *)mapped;
    madvise(mapped, st.st_size, MADV_SEQUENTIAL);
    return 0;
}

static void close_mmap(const uint8_t *data, int64_t size)
{
    if (data) munmap((void *)data, size);
}

static int scan_segments(const uint8_t *data, int64_t size, int codec,
                         Segment **out, int *count)
{
    int capacity = 256;
    Segment *segments = (Segment *)malloc(capacity * sizeof(Segment));
    if (!segments)
        return -1;

    int num = 0;
    bool in_segment = false;

    int64_t limit = size - 3;
    for (int64_t i = 0; i < limit; ) {
        /* Find 0x00 0x00 0x01 start code prefix */
        if (data[i] != 0x00 || data[i + 1] != 0x00 || data[i + 2] != 0x01) {
            i++;
            continue;
        }

        /* Determine the start of this NAL (include leading 0x00 for 4-byte codes) */
        int64_t sc_offset = (i > 0 && data[i - 1] == 0x00) ? i - 1 : i;
        uint8_t sc_byte = data[i + 3];

        bool is_boundary = false;
        bool is_frame = false;

        switch (codec) {
        case CODEC_MPEG2:
            if (sc_byte == 0xB3)
                is_boundary = true;
            else if (sc_byte == 0x00)
                is_frame = true;
            break;
        case CODEC_H264: {
            int nal_type = sc_byte & 0x1F;
            if (nal_type == 7)
                is_boundary = true;
            else if (nal_type == 1 || nal_type == 5)
                is_frame = true;
            break;
        }
        case CODEC_H265: {
            int nal_type = (sc_byte >> 1) & 0x3F;
            if (nal_type == 33)
                is_boundary = true;
            else if (nal_type <= 21)
                is_frame = true;
            break;
        }
        default:
            break;
        }

        if (is_boundary) {
            /* Finalize previous segment */
            if (in_segment)
                segments[num - 1].size = sc_offset - segments[num - 1].offset;

            /* Grow array if needed */
            if (num >= capacity) {
                capacity *= 2;
                Segment *tmp = (Segment *)realloc(segments, capacity * sizeof(Segment));
                if (!tmp) {
                    free(segments);
                    return -1;
                }
                segments = tmp;
            }

            segments[num].offset      = sc_offset;
            segments[num].size        = 0;
            segments[num].frame_count = 0;
            segments[num].error_count = 0;
            segments[num].defective   = false;
            num++;
            in_segment = true;
        } else if (is_frame && in_segment) {
            segments[num - 1].frame_count++;
        }

        i += 3;  /* Skip past the start code prefix */
    }

    /* Finalize last segment */
    if (in_segment && num > 0)
        segments[num - 1].size = size - segments[num - 1].offset;

    *out = segments;
    *count = num;
    return 0;
}

/* ---- Custom AVIOContext callbacks for mmap-based segment I/O ---- */

static int mmap_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    MmapIOContext *ctx = (MmapIOContext *)opaque;
    int64_t remaining = ctx->size - ctx->pos;
    if (remaining <= 0)
        return AVERROR_EOF;
    int to_read = buf_size < remaining ? buf_size : (int)remaining;
    memcpy(buf, ctx->base + ctx->pos, to_read);
    ctx->pos += to_read;
    return to_read;
}

static int64_t mmap_seek(void *opaque, int64_t offset, int whence)
{
    MmapIOContext *ctx = (MmapIOContext *)opaque;
    int64_t new_pos;
    if (whence == AVSEEK_SIZE)
        return ctx->size;
    else if (whence == SEEK_SET)
        new_pos = offset;
    else if (whence == SEEK_CUR)
        new_pos = ctx->pos + offset;
    else if (whence == SEEK_END)
        new_pos = ctx->size + offset;
    else
        return AVERROR(EINVAL);
    if (new_pos < 0) new_pos = 0;
    if (new_pos > ctx->size) new_pos = ctx->size;
    ctx->pos = new_pos;
    return new_pos;
}

/* ---- Segment I/O open/close using Custom AVIOContext ---- */

static int open_segment_io(const uint8_t *file_data, const Segment *seg,
                           int codec, MmapIOContext *io_ctx,
                           AVIOContext **avio_out, AVFormatContext **fmt_out)
{
    /* Set up mmap region for this segment */
    io_ctx->base = file_data + seg->offset;
    io_ctx->size = seg->size;
    io_ctx->pos  = 0;

    /* Allocate AVIO buffer */
    uint8_t *avio_buf = (uint8_t *)av_malloc(32768);
    if (!avio_buf) {
        fprintf(stderr, "Error: av_malloc failed for avio buffer\n");
        return -1;
    }

    /* Create custom AVIOContext */
    *avio_out = avio_alloc_context(avio_buf, 32768, 0, io_ctx,
                                   mmap_read_packet, NULL, mmap_seek);
    if (!*avio_out) {
        fprintf(stderr, "Error: avio_alloc_context failed\n");
        av_free(avio_buf);
        return -1;
    }

    /* Allocate format context with custom I/O */
    *fmt_out = avformat_alloc_context();
    if (!*fmt_out) {
        fprintf(stderr, "Error: avformat_alloc_context failed\n");
        av_freep(&(*avio_out)->buffer);
        avio_context_free(avio_out);
        return -1;
    }
    (*fmt_out)->pb = *avio_out;
    (*fmt_out)->flags |= AVFMT_FLAG_CUSTOM_IO;

    /* Determine format name from codec type */
    const char *format_name;
    switch (codec) {
    case CODEC_MPEG2: format_name = "mpegvideo"; break;
    case CODEC_H264:  format_name = "h264";      break;
    case CODEC_H265:  format_name = "hevc";      break;
    default:
        avformat_free_context(*fmt_out);
        *fmt_out = NULL;
        av_freep(&(*avio_out)->buffer);
        avio_context_free(avio_out);
        return -1;
    }

    const AVInputFormat *ifmt = av_find_input_format(format_name);
    int ret = avformat_open_input(fmt_out, NULL, ifmt, NULL);
    if (ret < 0) {
        /* avformat_open_input frees *fmt_out on failure */
        *fmt_out = NULL;
        av_freep(&(*avio_out)->buffer);
        avio_context_free(avio_out);
        return -1;
    }

    return 0;
}

static void close_segment_io(AVIOContext **avio, AVFormatContext **fmt)
{
    if (*fmt) {
        avformat_close_input(fmt);
    }
    if (*avio) {
        av_freep(&(*avio)->buffer);
        avio_context_free(avio);
    }
}

/* ---- Per-segment decode test ---- */

/* Suppress libav log output (stderr noise from decoder warnings) */
static void libav_log_quiet(void *ptr, int level, const char *fmt, va_list vl)
{
    (void)ptr; (void)level; (void)fmt; (void)vl;
}

static int test_segment(const uint8_t *file_data, const Segment *seg, int codec)
{
    MmapIOContext io_ctx;
    AVIOContext *avio = NULL;
    AVFormatContext *fmt = NULL;

    if (open_segment_io(file_data, seg, codec, &io_ctx, &avio, &fmt) < 0)
        return -1;

    avformat_find_stream_info(fmt, NULL);

    int vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vidx < 0) {
        close_segment_io(&avio, &fmt);
        return -1;
    }

    /* Create decoder */
    const AVCodec *dec = avcodec_find_decoder(fmt->streams[vidx]->codecpar->codec_id);
    if (!dec) {
        close_segment_io(&avio, &fmt);
        return -1;
    }
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        close_segment_io(&avio, &fmt);
        return -1;
    }
    avcodec_parameters_to_context(dec_ctx, fmt->streams[vidx]->codecpar);

    /* Enable strict error detection (equivalent to ffmpeg -err_detect +careful+explode) */
    dec_ctx->err_recognition = AV_EF_CAREFUL | AV_EF_EXPLODE;

    if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
        avcodec_free_context(&dec_ctx);
        close_segment_io(&avio, &fmt);
        return -1;
    }

    /* Decode loop */
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int errors = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vidx) {
            int ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN))
                errors++;
            while (1) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0)
                    errors++;
            }
        }
        av_packet_unref(pkt);
    }

    /* Flush decoder */
    avcodec_send_packet(dec_ctx, NULL);
    while (1) {
        int ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            errors++;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    close_segment_io(&avio, &fmt);

    return errors;
}

/* ---- Multi-threaded segment testing ---- */

typedef struct {
    const uint8_t *data;     /* Shared mmap (read-only) */
    Segment       *segs;     /* Segment array (each thread writes own range) */
    int            start;    /* First segment index for this thread */
    int            end;      /* One past last segment index */
    int            codec;
    int            threshold;
} ThreadArg;

static void *test_thread(void *arg)
{
    ThreadArg *ta = (ThreadArg *)arg;
    int effective_threshold = (ta->threshold == 0) ? 1 : ta->threshold;

    for (int i = ta->start; i < ta->end; i++) {
        int result = test_segment(ta->data, &ta->segs[i], ta->codec);
        ta->segs[i].error_count = (result >= 0) ? result : 0;
        ta->segs[i].defective   = (ta->segs[i].error_count >= effective_threshold);
    }
    return NULL;
}

static int get_num_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 4;
}

static int test_segments(const uint8_t *data, Segment *segs, int count,
                         int codec, int threshold, int verbose)
{
    /* Suppress libav stderr noise (global, before threads start) */
    if (!verbose)
        av_log_set_callback(libav_log_quiet);

    int num_threads = get_num_cpus();
    if (num_threads > count)
        num_threads = count;
    if (num_threads < 1)
        num_threads = 1;

    if (verbose)
        fprintf(stderr, "Decode testing %d segments with %d threads...\n", count, num_threads);

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    ThreadArg *args = (ThreadArg *)malloc(num_threads * sizeof(ThreadArg));

    /* Distribute segments evenly across threads */
    int per_thread = count / num_threads;
    int remainder = count % num_threads;
    int offset = 0;

    for (int t = 0; t < num_threads; t++) {
        int chunk = per_thread + (t < remainder ? 1 : 0);
        args[t].data      = data;
        args[t].segs      = segs;
        args[t].start     = offset;
        args[t].end       = offset + chunk;
        args[t].codec     = codec;
        args[t].threshold = threshold;
        pthread_create(&threads[t], NULL, test_thread, &args[t]);
        offset += chunk;
    }

    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    free(threads);
    free(args);

    /* Count results and print verbose output (sequential, after all threads done) */
    int defective_count = 0;
    for (int i = 0; i < count; i++) {
        if (segs[i].defective)
            defective_count++;

        if (verbose) {
            fprintf(stderr, "Segment %4d/%d: %4d frames, %8lld bytes — %s",
                    i + 1, count,
                    segs[i].frame_count,
                    (long long)segs[i].size,
                    segs[i].defective ? "DEFECTIVE" : "OK");
            if (segs[i].defective)
                fprintf(stderr, " (%d errors)", segs[i].error_count);
            fprintf(stderr, "\n");
        }
    }

    return defective_count;
}

static ssize_t write_all(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, p, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += written;
        remaining -= written;
    }
    return (ssize_t)count;
}

static int write_repaired(const char *path, const uint8_t *data,
                          const Segment *segs, int count)
{
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.repair.tmp", path);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", tmp_path, strerror(errno));
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (!segs[i].defective) {
            if (write_all(fd, data + segs[i].offset, segs[i].size) < 0) {
                fprintf(stderr, "Error: write failed: %s\n", strerror(errno));
                close(fd);
                unlink(tmp_path);
                return -1;
            }
        }
    }

    if (fsync(fd) < 0) {
        fprintf(stderr, "Error: fsync failed: %s\n", strerror(errno));
        close(fd);
        unlink(tmp_path);
        return -1;
    }
    close(fd);

    if (rename(tmp_path, path) < 0) {
        fprintf(stderr, "Error: rename '%s' -> '%s' failed: %s\n",
                tmp_path, path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] <input-file>\n"
        "\n"
        "Remove defective segments from MPEG-2/H.264/H.265 elementary streams.\n"
        "\n"
        "Options:\n"
        "  --check-only         Report only, do not modify file\n"
        "  --codec <type>       Codec override (mpeg2|h264|h265). Default: auto from extension\n"
        "  --threshold <N>      Error threshold per segment (default: 0 for MPEG-2, 3 for H.264/H.265)\n"
        "  --log <file>         Detailed log file\n"
        "  -v, --verbose        Verbose output on stderr\n"
        "  -h, --help           Usage help\n"
        "\n"
        "Exit codes:\n"
        "  0  No defective segments found\n"
        "  1  Repair performed (or defects found with --check-only)\n"
        "  2  Fatal error\n",
        prog);
}

int main(int argc, char *argv[])
{
    bool check_only = false;
    int  codec      = -1;
    int  threshold  = -1;
    const char *log_file   = NULL;
    bool verbose    = false;

    static struct option long_options[] = {
        {"check-only", no_argument,       NULL, 'c'},
        {"codec",      required_argument, NULL, 'C'},
        {"threshold",  required_argument, NULL, 't'},
        {"log",        required_argument, NULL, 'l'},
        {"verbose",    no_argument,       NULL, 'v'},
        {"help",       no_argument,       NULL, 'h'},
        {NULL,         0,                 NULL,  0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            check_only = true;
            break;
        case 'C':
            if (strcmp(optarg, "mpeg2") == 0)
                codec = CODEC_MPEG2;
            else if (strcmp(optarg, "h264") == 0)
                codec = CODEC_H264;
            else if (strcmp(optarg, "h265") == 0)
                codec = CODEC_H265;
            else {
                fprintf(stderr, "Error: Unknown codec '%s'. Use mpeg2, h264, or h265.\n", optarg);
                return 2;
            }
            break;
        case 't':
            threshold = atoi(optarg);
            if (threshold < 0) {
                fprintf(stderr, "Error: Threshold must be >= 0.\n");
                return 2;
            }
            break;
        case 'l':
            log_file = optarg;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified.\n\n");
        print_usage(argv[0]);
        return 2;
    }

    const char *input_file = argv[optind];

    /* Auto-detect codec from extension if not overridden */
    if (codec < 0) {
        codec = detect_codec(input_file);
        if (codec == CODEC_UNKNOWN) {
            fprintf(stderr, "Error: Cannot detect codec from '%s'. Use --codec to specify.\n",
                    input_file);
            return 2;
        }
    }

    /* Apply codec-specific default threshold if not set */
    if (threshold < 0) {
        threshold = (codec == CODEC_MPEG2) ? 0 : 3;
    }

    if (verbose) {
        fprintf(stderr, "Input file:  %s\n", input_file);
        fprintf(stderr, "Codec:       %s\n", codec_names[codec]);
        fprintf(stderr, "Threshold:   %d\n", threshold);
        fprintf(stderr, "Check-only:  %s\n", check_only ? "yes" : "no");
        fprintf(stderr, "Log file:    %s\n", log_file ? log_file : "(none)");
    }


    const uint8_t *data = NULL;
    int64_t file_size = 0;
    if (open_mmap(input_file, &data, &file_size) < 0)
        return 2;
    if (verbose)
        fprintf(stderr, "Mapped %s: %lld bytes\n", input_file, (long long)file_size);

    Segment *segments = NULL;
    int seg_count = 0;
    if (scan_segments(data, file_size, codec, &segments, &seg_count) < 0) {
        fprintf(stderr, "Error: segment scan failed\n");
        close_mmap(data, file_size);
        return 2;
    }

    if (seg_count == 0) {
        fprintf(stderr, "Error: no valid segments found in '%s'\n", input_file);
        close_mmap(data, file_size);
        return 2;
    }

    int total_frames = 0;
    for (int i = 0; i < seg_count; i++)
        total_frames += segments[i].frame_count;

    if (verbose)
        fprintf(stderr, "Found %d segments, %d total frames\n", seg_count, total_frames);

    /* Decode-test all segments */
    int defective = test_segments(data, segments, seg_count, codec, threshold, verbose);
    if (verbose)
        fprintf(stderr, "Decode test complete: %d defective segments\n", defective);

    /* Count defective stats */
    int defective_count = defective;
    int removed_frames = 0;
    for (int i = 0; i < seg_count; i++) {
        if (segments[i].defective)
            removed_frames += segments[i].frame_count;
    }

    /* Determine codec name for output */
    const char *codec_name;
    switch (codec) {
    case CODEC_MPEG2: codec_name = "mpeg2video"; break;
    case CODEC_H264:  codec_name = "h264"; break;
    case CODEC_H265:  codec_name = "h265"; break;
    default:          codec_name = "unknown"; break;
    }

    /* Write log file if requested */
    if (log_file) {
        FILE *lf = fopen(log_file, "w");
        if (!lf) {
            fprintf(stderr, "Warning: cannot open log file '%s': %s\n",
                    log_file, strerror(errno));
        } else {
            fprintf(lf, "# ttcut-esrepair log for %s\n", input_file);
            fprintf(lf, "# codec=%s threshold=%d\n\n", codec_name, threshold);
            for (int i = 0; i < seg_count; i++) {
                fprintf(lf, "segment %4d: offset=%10lld size=%10lld frames=%4d errors=%4d %s\n",
                        i + 1,
                        (long long)segments[i].offset,
                        (long long)segments[i].size,
                        segments[i].frame_count,
                        segments[i].error_count,
                        segments[i].defective ? "DEFECTIVE" : "OK");
            }
            fclose(lf);
        }
    }

    int exit_code;

    if (defective_count == 0) {
        /* No defects found */
        exit_code = 0;
    } else if (defective_count == seg_count) {
        /* ALL segments defective — don't write empty file */
        fprintf(stderr, "Error: all %d segments are defective — cannot repair\n", seg_count);
        exit_code = 2;
    } else if (check_only) {
        /* Check-only mode: report but don't modify */
        exit_code = 1;
    } else {
        /* Repair mode: write repaired file */
        if (write_repaired(input_file, data, segments, seg_count) < 0) {
            fprintf(stderr, "Error: failed to write repaired file\n");
            exit_code = 2;
        } else {
            if (verbose)
                fprintf(stderr, "Repaired: removed %d segments (%d frames)\n",
                        defective_count, removed_frames);
            exit_code = 1;
        }
    }

    /* Machine-readable statistics on stdout */
    printf("codec=%s\n", codec_name);
    printf("repaired=%d\n", (exit_code == 1 && !check_only) ? 1 : 0);
    printf("total_segments=%d\n", seg_count);
    printf("defective_segments=%d\n", defective_count);
    printf("removed_frames=%d\n", removed_frames);
    printf("total_frames_before=%d\n", total_frames);
    printf("total_frames_after=%d\n", total_frames - removed_frames);

    free(segments);
    close_mmap(data, file_size);
    return exit_code;
}
