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
    (void)sizeof(Segment);
    (void)sizeof(MmapIOContext);

    fprintf(stderr, "Not yet implemented\n");
    return 2;
}
