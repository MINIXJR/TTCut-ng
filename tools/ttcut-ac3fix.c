/*
 * ttcut-ac3fix - AC3 Header Repair Tool for TTCut
 *
 * Fixes corrupted AC3 headers in DVB recordings where the channel count
 * (acmod field) is incorrectly set to stereo while the bitrate indicates
 * 5.1 surround sound (384kbps).
 *
 * Detection: High bitrate (384kbps+) + stereo acmod = likely corrupt
 * Fix: Patch acmod field from stereo (010) to 5.1 (111)
 *
 * This is a common issue with DVB recordings where the broadcaster
 * incorrectly sets the channel configuration in the AC3 header.
 *
 * Copyright (C) 2026 TTCut-ng Project
 * License: GPL v2 or later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* AC3 Sync Word */
#define AC3_SYNC_WORD 0x0B77

/* AC3 acmod values */
#define AC3_ACMOD_DUALMONO  0  /* 1+1 - dual mono */
#define AC3_ACMOD_MONO      1  /* 1/0 - center */
#define AC3_ACMOD_STEREO    2  /* 2/0 - L, R */
#define AC3_ACMOD_3F        3  /* 3/0 - L, C, R */
#define AC3_ACMOD_2F1R      4  /* 2/1 - L, R, S */
#define AC3_ACMOD_3F1R      5  /* 3/1 - L, C, R, S */
#define AC3_ACMOD_2F2R      6  /* 2/2 - L, R, SL, SR */
#define AC3_ACMOD_3F2R      7  /* 3/2 - L, C, R, SL, SR (5.0/5.1) */

/* AC3 frame sizes for 48kHz (words, multiply by 2 for bytes) */
static const uint16_t ac3_frame_sizes_48k[] = {
    64, 64, 80, 80, 96, 96, 112, 112,
    128, 128, 160, 160, 192, 192, 224, 224,
    256, 256, 320, 320, 384, 384, 448, 448,
    512, 512, 640, 640, 768, 768, 896, 896,
    1024, 1024, 1152, 1152, 1280, 1280
};

/* Bitrates in kbps for each frmsizecod/2 */
static const uint16_t ac3_bitrates[] = {
    32, 40, 48, 56, 64, 80, 96, 112,
    128, 160, 192, 224, 256, 320, 384, 448,
    512, 576, 640
};

/* Number of channels for each acmod */
static const uint8_t ac3_channels[] = {
    2, 1, 2, 3, 3, 4, 4, 5
};

/* acmod names */
static const char *ac3_acmod_names[] = {
    "1+1 (dual mono)",
    "1/0 (mono)",
    "2/0 (stereo)",
    "3/0 (L,C,R)",
    "2/1 (L,R,S)",
    "3/1 (L,C,R,S)",
    "2/2 (L,R,SL,SR)",
    "3/2 (L,C,R,SL,SR)"
};

/* Statistics */
typedef struct {
    uint64_t total_frames;
    uint64_t stereo_frames;
    uint64_t surround_frames;
    uint64_t other_frames;
    uint64_t inconsistent_frames;
    uint64_t fixed_frames;
    /* Format change tracking */
    uint64_t format_changes;
    int last_acmod;
    uint64_t segment_start_frame;
    double segment_start_time;
} ac3fix_stats_t;

/* AC3 frame info */
typedef struct {
    uint8_t fscod;        /* Sample rate code (0=48kHz, 1=44.1kHz, 2=32kHz) */
    uint8_t frmsizecod;   /* Frame size code */
    uint8_t bsid;         /* Bitstream ID */
    uint8_t bsmod;        /* Bitstream mode */
    uint8_t acmod;        /* Audio coding mode (channel config) */
    uint8_t lfeon;        /* LFE channel on */
    uint16_t frame_size;  /* Frame size in bytes */
    uint16_t bitrate;     /* Bitrate in kbps */
    uint8_t channels;     /* Number of full-bandwidth channels */
} ac3_frame_info_t;

/* Command line options */
typedef struct {
    const char *input_file;
    const char *output_file;
    bool analyze_only;
    bool verbose;
    bool force;
    bool force_fix;      /* Fix all inconsistent frames without verification */
    bool show_segments;  /* Show format change segments */
    uint16_t min_bitrate; /* Minimum bitrate to consider for fixing (default: 384) */
} ac3fix_options_t;

/* Parse AC3 frame header */
static bool parse_ac3_header(const uint8_t *data, size_t len, ac3_frame_info_t *info)
{
    if (len < 7)
        return false;

    /* Check sync word */
    if (data[0] != 0x0B || data[1] != 0x77)
        return false;

    /* Byte 4: fscod (bits 7-6), frmsizecod (bits 5-0) */
    info->fscod = (data[4] >> 6) & 0x03;
    info->frmsizecod = data[4] & 0x3F;

    /* Only support 48kHz for now */
    if (info->fscod != 0) {
        /* 44.1kHz and 32kHz have different frame size tables */
        return false;
    }

    if (info->frmsizecod >= 38)
        return false;

    /* Frame size in bytes */
    info->frame_size = ac3_frame_sizes_48k[info->frmsizecod] * 2;

    /* Bitrate */
    info->bitrate = ac3_bitrates[info->frmsizecod / 2];

    /* Byte 5: bsid (bits 7-3), bsmod (bits 2-0) */
    info->bsid = (data[5] >> 3) & 0x1F;
    info->bsmod = data[5] & 0x07;

    /* Byte 6: acmod (bits 7-5), then other fields depending on acmod */
    info->acmod = (data[6] >> 5) & 0x07;
    info->channels = ac3_channels[info->acmod];

    /* LFE is more complex to parse - depends on acmod and other fields */
    /* For simplicity, assume LFE is present if acmod indicates surround */
    info->lfeon = (info->acmod == AC3_ACMOD_3F2R) ? 1 : 0;

    return true;
}

/* Check if frame has inconsistent header (high bitrate + stereo) */
static bool is_inconsistent_header(const ac3_frame_info_t *info, uint16_t min_bitrate)
{
    /* High bitrate with stereo acmod is suspicious */
    return (info->bitrate >= min_bitrate && info->acmod == AC3_ACMOD_STEREO);
}

/* Patch AC3 frame header to set correct acmod
 * Note: We only patch the acmod field. The CRC might become invalid,
 * but most players ignore CRC errors in AC3 streams.
 * For a proper fix, CRC recalculation would be needed.
 */
static bool patch_ac3_header(uint8_t *data, size_t frame_size, uint8_t new_acmod)
{
    (void)frame_size;  /* Unused for now */

    if (frame_size < 7)
        return false;

    /* Get current byte 6 */
    uint8_t byte6 = data[6];

    /* Clear acmod bits (7-5) and set new value */
    byte6 = (byte6 & 0x1F) | ((new_acmod & 0x07) << 5);
    data[6] = byte6;

    return true;
}

/* Format time in HH:MM:SS.ms format */
static void format_time(double seconds, char *buf, size_t bufsize)
{
    int h = (int)(seconds / 3600);
    int m = (int)((seconds - h * 3600) / 60);
    double s = seconds - h * 3600 - m * 60;
    snprintf(buf, bufsize, "%02d:%02d:%06.3f", h, m, s);
}

/* Process AC3 file */
static int process_ac3_file(const ac3fix_options_t *opts)
{
    FILE *in_fp = NULL;
    FILE *out_fp = NULL;
    uint8_t *buffer = NULL;
    size_t buffer_size = 0;
    size_t buffer_pos = 0;
    size_t file_pos = 0;
    ac3fix_stats_t stats = {0};
    stats.last_acmod = -1;
    int ret = 0;

    /* Open input file */
    in_fp = fopen(opts->input_file, "rb");
    if (!in_fp) {
        fprintf(stderr, "Error: Cannot open input file: %s\n", opts->input_file);
        return 1;
    }

    /* Get file size */
    fseek(in_fp, 0, SEEK_END);
    long file_size = ftell(in_fp);
    fseek(in_fp, 0, SEEK_SET);

    /* Allocate read buffer (64KB) */
    buffer_size = 65536;
    buffer = malloc(buffer_size);
    if (!buffer) {
        fprintf(stderr, "Error: Cannot allocate buffer\n");
        ret = 1;
        goto cleanup;
    }

    /* Open output file if not analyze-only */
    if (!opts->analyze_only && opts->output_file) {
        out_fp = fopen(opts->output_file, "wb");
        if (!out_fp) {
            fprintf(stderr, "Error: Cannot open output file: %s\n", opts->output_file);
            ret = 1;
            goto cleanup;
        }
    }

    printf("TTCut AC3 Header Repair Tool\n");
    printf("============================\n");
    printf("Input:  %s\n", opts->input_file);
    if (!opts->analyze_only && opts->output_file)
        printf("Output: %s\n", opts->output_file);
    printf("Mode:   %s\n", opts->analyze_only ? "Analyze only" :
                          (opts->force_fix ? "Force fix all" : "Fix"));
    printf("Min bitrate for fix: %d kbps\n", opts->min_bitrate);
    printf("\n");

    /* Process file frame by frame */
    uint8_t frame_buffer[4096];  /* Max AC3 frame is ~3840 bytes */
    size_t bytes_read;
    int progress_last = -1;
    double frame_duration = 1536.0 / 48000.0;  /* AC3 frame duration at 48kHz */
    double current_time = 0;

    while ((bytes_read = fread(buffer + buffer_pos, 1, buffer_size - buffer_pos, in_fp)) > 0
           || buffer_pos > 0) {
        buffer_pos += bytes_read;
        size_t processed = 0;

        /* Find and process AC3 frames */
        while (processed + 7 <= buffer_pos) {
            /* Look for sync word */
            if (buffer[processed] != 0x0B || buffer[processed + 1] != 0x77) {
                processed++;
                continue;
            }

            /* Parse header */
            ac3_frame_info_t info;
            if (!parse_ac3_header(buffer + processed, buffer_pos - processed, &info)) {
                processed++;
                continue;
            }

            /* Check if we have complete frame */
            if (processed + info.frame_size > buffer_pos) {
                /* Need more data */
                break;
            }

            stats.total_frames++;

            /* Track format changes */
            if (stats.last_acmod != -1 && info.acmod != stats.last_acmod) {
                stats.format_changes++;
                if (opts->show_segments) {
                    char time_buf[32];
                    format_time(current_time, time_buf, sizeof(time_buf));
                    printf("Format change at %s (frame %lu): %s -> %s\n",
                           time_buf, stats.total_frames,
                           ac3_acmod_names[stats.last_acmod],
                           ac3_acmod_names[info.acmod]);
                }
            }
            stats.last_acmod = info.acmod;

            /* Copy frame to frame buffer */
            memcpy(frame_buffer, buffer + processed, info.frame_size);

            bool should_fix = false;

            if (info.acmod == AC3_ACMOD_STEREO) {
                stats.stereo_frames++;

                if (is_inconsistent_header(&info, opts->min_bitrate)) {
                    stats.inconsistent_frames++;

                    /* In force_fix mode, fix all inconsistent frames */
                    if (opts->force_fix) {
                        should_fix = true;
                        stats.fixed_frames++;

                        if (opts->verbose) {
                            char time_buf[32];
                            format_time(current_time, time_buf, sizeof(time_buf));
                            printf("Frame %lu @ %s: %d kbps stereo -> 5.1 (FIX)\n",
                                   stats.total_frames, time_buf, info.bitrate);
                        }
                    }
                }
            } else if (info.acmod == AC3_ACMOD_3F2R) {
                stats.surround_frames++;
            } else {
                stats.other_frames++;
            }

            /* Apply fix if needed */
            if (should_fix && out_fp) {
                patch_ac3_header(frame_buffer, info.frame_size, AC3_ACMOD_3F2R);
            }

            /* Write frame to output */
            if (out_fp) {
                fwrite(frame_buffer, 1, info.frame_size, out_fp);
            }

            processed += info.frame_size;
            file_pos += info.frame_size;
            current_time += frame_duration;

            /* Progress indicator */
            int progress = (int)((file_pos * 100) / file_size);
            if (progress != progress_last && progress % 10 == 0) {
                fprintf(stderr, "\rProgress: %d%%", progress);
                fflush(stderr);
                progress_last = progress;
            }
        }

        /* Move unprocessed data to beginning of buffer */
        if (processed > 0) {
            memmove(buffer, buffer + processed, buffer_pos - processed);
            buffer_pos -= processed;
        }

        /* If no progress and buffer is getting full, we have a problem */
        if (bytes_read == 0 && buffer_pos > 0) {
            fprintf(stderr, "\nWarning: %zu bytes at end of file could not be parsed\n", buffer_pos);
            break;
        }
    }

    fprintf(stderr, "\rProgress: 100%%\n\n");

    /* Print statistics */
    char duration_buf[32];
    format_time(current_time, duration_buf, sizeof(duration_buf));

    printf("Statistics:\n");
    printf("-----------\n");
    printf("Duration:            %s\n", duration_buf);
    printf("Total frames:        %lu\n", stats.total_frames);
    printf("5.1 surround frames: %lu (%.1f%%)\n", stats.surround_frames,
           100.0 * stats.surround_frames / stats.total_frames);
    printf("Stereo frames:       %lu (%.1f%%)\n", stats.stereo_frames,
           100.0 * stats.stereo_frames / stats.total_frames);
    if (stats.other_frames > 0)
        printf("Other frames:        %lu\n", stats.other_frames);
    printf("Format changes:      %lu\n", stats.format_changes);
    printf("\n");
    printf("Inconsistent frames: %lu (>=%d kbps + stereo header)\n",
           stats.inconsistent_frames, opts->min_bitrate);

    if (opts->force_fix) {
        printf("Fixed frames:        %lu\n", stats.fixed_frames);
    }

    if (stats.inconsistent_frames > 0 && opts->analyze_only) {
        printf("\nRecommendation: Run with --force-fix to repair %lu frames\n",
               stats.inconsistent_frames);
        printf("Example: %s --force-fix %s output.ac3\n",
               "ttcut-ac3fix", opts->input_file);
    }

cleanup:
    if (buffer)
        free(buffer);
    if (in_fp)
        fclose(in_fp);
    if (out_fp)
        fclose(out_fp);

    return ret;
}

/* Print usage */
static void print_usage(const char *progname)
{
    printf("Usage: %s [options] <input.ac3> [output.ac3]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  -a, --analyze      Analyze only, don't write output\n");
    printf("  -F, --force-fix    Fix all inconsistent frames (384kbps + stereo -> 5.1)\n");
    printf("  -s, --show-segments Show format changes (stereo <-> 5.1 transitions)\n");
    printf("  -b, --bitrate N    Minimum bitrate to fix (default: 384 kbps)\n");
    printf("  -v, --verbose      Verbose output (show each fixed frame)\n");
    printf("  -f, --force        Overwrite output file if exists\n");
    printf("  -h, --help         Show this help\n");
    printf("\n");
    printf("Description:\n");
    printf("  Fixes corrupted AC3 headers in DVB recordings where the channel\n");
    printf("  count (acmod) is incorrectly set to stereo (2/0) while the bitrate\n");
    printf("  indicates 5.1 surround sound (384 kbps).\n");
    printf("\n");
    printf("  This is a common issue with DVB broadcasts where the encoder\n");
    printf("  incorrectly sets the channel configuration in the AC3 header.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -a input.ac3                    # Analyze only\n", progname);
    printf("  %s -a -s input.ac3                 # Analyze with segment info\n", progname);
    printf("  %s --force-fix input.ac3 out.ac3  # Fix and save\n", progname);
    printf("  %s -F -v input.ac3 fixed.ac3      # Fix with verbose output\n", progname);
}

int main(int argc, char **argv)
{
    ac3fix_options_t opts = {
        .input_file = NULL,
        .output_file = NULL,
        .analyze_only = false,
        .verbose = false,
        .force = false,
        .force_fix = false,
        .show_segments = false,
        .min_bitrate = 384
    };

    /* Parse arguments */
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--analyze") == 0) {
                opts.analyze_only = true;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
                opts.verbose = true;
            } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                opts.force = true;
            } else if (strcmp(argv[i], "-F") == 0 || strcmp(argv[i], "--force-fix") == 0) {
                opts.force_fix = true;
            } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--show-segments") == 0) {
                opts.show_segments = true;
            } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bitrate") == 0) {
                if (i + 1 < argc) {
                    opts.min_bitrate = atoi(argv[++i]);
                }
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage(argv[0]);
                return 0;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return 1;
            }
        } else {
            if (positional == 0) {
                opts.input_file = argv[i];
            } else if (positional == 1) {
                opts.output_file = argv[i];
            }
            positional++;
        }
    }

    /* Validate arguments */
    if (!opts.input_file) {
        print_usage(argv[0]);
        return 1;
    }

    if (!opts.output_file && !opts.analyze_only) {
        opts.analyze_only = true;
        printf("Note: No output file specified, running in analyze mode\n\n");
    }

    if (opts.force_fix && opts.analyze_only) {
        opts.analyze_only = false;  /* force_fix implies writing */
    }

    /* Check if output exists */
    if (opts.output_file && !opts.force) {
        FILE *f = fopen(opts.output_file, "r");
        if (f) {
            fclose(f);
            fprintf(stderr, "Error: Output file exists. Use -f to overwrite.\n");
            return 1;
        }
    }

    return process_ac3_file(&opts);
}
