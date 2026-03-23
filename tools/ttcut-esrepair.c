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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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

    /* Suppress unused warnings for structures defined for later tasks */
    (void)sizeof(MmapIOContext);

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

    /* TODO: test_segments, write_repaired */

    free(segments);
    close_mmap(data, file_size);
    return 0;
}
