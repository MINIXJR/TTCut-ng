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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
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
    double duration_s;   /* frames seen so far, as time at 48 kHz */
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

/* Header of the report: what is processed and how. */
static void print_ac3_banner(const ac3fix_options_t *opts)
{
    printf("TTCut AC3 Header Repair Tool\n");
    printf("============================\n");
    printf("Input:  %s\n", opts->input_file);
    if (!opts->analyze_only && opts->output_file)
        printf("Output: %s\n", opts->output_file);
    printf("Mode:   %s\n", opts->analyze_only ? "Analyze only" :
                          (opts->force_fix ? "Force fix all" : "Fix"));
    printf("Min bitrate for fix: %d kbps\n", opts->min_bitrate);
    printf("\n");
}

/* Files and read buffer of one run. */
typedef struct {
    FILE *in_fp;
    FILE *out_fp;        /* NULL in analyze-only mode */
    uint8_t *buffer;
    size_t buffer_size;
    long file_size;
} ac3fix_io_t;

static void close_ac3_files(ac3fix_io_t *io)
{
    if (io->buffer)
        free(io->buffer);
    if (io->in_fp)
        fclose(io->in_fp);
    if (io->out_fp)
        fclose(io->out_fp);
    memset(io, 0, sizeof(*io));
}

/* Opens the input, measures it, allocates the read buffer and opens the
 * output unless analyze-only. Returns 1 after a message on any failure,
 * with everything opened so far closed again. */
static int open_ac3_files(const ac3fix_options_t *opts, ac3fix_io_t *io)
{
    memset(io, 0, sizeof(*io));

    io->in_fp = fopen(opts->input_file, "rb");
    if (!io->in_fp) {
        fprintf(stderr, "Error: Cannot open input file: %s\n", opts->input_file);
        return 1;
    }

    fseek(io->in_fp, 0, SEEK_END);
    io->file_size = ftell(io->in_fp);
    fseek(io->in_fp, 0, SEEK_SET);

    if (io->file_size <= 0) {
        fprintf(stderr, "Error: Input file is empty or unreadable\n");
        close_ac3_files(io);
        return 1;
    }

    io->buffer_size = 65536;
    io->buffer = malloc(io->buffer_size);
    if (!io->buffer) {
        fprintf(stderr, "Error: Cannot allocate buffer\n");
        close_ac3_files(io);
        return 1;
    }

    if (!opts->analyze_only && opts->output_file) {
        io->out_fp = fopen(opts->output_file, "wb");
        if (!io->out_fp) {
            fprintf(stderr, "Error: Cannot open output file: %s\n", opts->output_file);
            close_ac3_files(io);
            return 1;
        }
    }
    return 0;
}

/* One complete frame: count it, report a channel-format change, classify it
 * (stereo / 5.1 / other), patch the header when the fix applies, and write it
 * to the output when there is one. The frame is copied into frame_buffer
 * first so the patch never touches the read buffer. Returns 1 after a write
 * error, 0 otherwise. */
static int process_one_frame(const ac3fix_options_t *opts, ac3fix_io_t *io, ac3fix_stats_t *stats,
                             const uint8_t *frame, const ac3_frame_info_t *info, uint8_t *frame_buffer)
{
    stats->total_frames++;

    /* Track format changes */
    if (stats->last_acmod != -1 && info->acmod != stats->last_acmod) {
        stats->format_changes++;
        if (opts->show_segments) {
            char time_buf[32];
            format_time(stats->duration_s, time_buf, sizeof(time_buf));
            printf("Format change at %s (frame %" PRIu64 "): %s -> %s\n",
                   time_buf, stats->total_frames,
                   ac3_acmod_names[stats->last_acmod],
                   ac3_acmod_names[info->acmod]);
        }
    }
    stats->last_acmod = info->acmod;

    /* Copy frame to frame buffer */
    memcpy(frame_buffer, frame, info->frame_size);

    bool should_fix = false;

    if (info->acmod == AC3_ACMOD_STEREO) {
        stats->stereo_frames++;

        if (is_inconsistent_header(info, opts->min_bitrate)) {
            stats->inconsistent_frames++;

            /* In force_fix mode, fix all inconsistent frames */
            if (opts->force_fix) {
                should_fix = true;
                stats->fixed_frames++;

                if (opts->verbose) {
                    char time_buf[32];
                    format_time(stats->duration_s, time_buf, sizeof(time_buf));
                    printf("Frame %" PRIu64 " @ %s: %d kbps stereo -> 5.1 (FIX)\n",
                           stats->total_frames, time_buf, info->bitrate);
                }
            }
        }
    } else if (info->acmod == AC3_ACMOD_3F2R) {
        stats->surround_frames++;
    } else {
        stats->other_frames++;
    }

    /* Apply fix if needed */
    if (should_fix && io->out_fp) {
        patch_ac3_header(frame_buffer, info->frame_size, AC3_ACMOD_3F2R);
    }

    /* Write frame to output */
    if (io->out_fp) {
        if (fwrite(frame_buffer, 1, info->frame_size, io->out_fp) != info->frame_size) {
            fprintf(stderr, "\nError: Write failed at frame %" PRIu64 "\n", stats->total_frames);
            return 1;
        }
    }
    return 0;
}

/* Walks the input frame by frame: counts frames per acmod, reports format
 * changes, patches inconsistent stereo headers in force-fix mode and
 * writes every frame to the output when there is one. Bytes that belong
 * to no frame are skipped and reported at the end. Returns 1 after a
 * write error, 0 otherwise. */
static int walk_ac3_frames(const ac3fix_options_t *opts, ac3fix_io_t *io, ac3fix_stats_t *stats)
{
    uint8_t *buffer = io->buffer;
    size_t buffer_pos = 0;
    size_t file_pos = 0;
    uint8_t frame_buffer[4096];  /* Max AC3 frame is ~3840 bytes */
    size_t bytes_read;
    int progress_last = -1;
    double frame_duration = 1536.0 / 48000.0;  /* AC3 frame duration at 48kHz */
    /* Size of the last frame parsed, used to tell a partial frame at the end
     * of the file from real trailing garbage. 0 until one has been seen. */
    size_t last_frame_size = 0;
    /* Bytes skipped because they belong to no valid frame. Counted because
     * the scan drops them one at a time, so the leftover in the buffer at EOF
     * is always under 7 bytes and says nothing about how much was garbage. */
    size_t skipped_junk = 0;

    while ((bytes_read = fread(buffer + buffer_pos, 1, io->buffer_size - buffer_pos, io->in_fp)) > 0
           || buffer_pos > 0) {
        buffer_pos += bytes_read;
        size_t processed = 0;

        /* Find and process AC3 frames */
        while (processed + 7 <= buffer_pos) {
            /* Look for sync word */
            if (buffer[processed] != 0x0B || buffer[processed + 1] != 0x77) {
                processed++;
                skipped_junk++;
                continue;
            }

            /* Parse header */
            ac3_frame_info_t info;
            if (!parse_ac3_header(buffer + processed, buffer_pos - processed, &info)) {
                processed++;
                skipped_junk++;
                continue;
            }

            /* Check if we have complete frame */
            if (processed + info.frame_size > buffer_pos) {
                /* Need more data */
                break;
            }

            if (process_one_frame(opts, io, stats, buffer + processed, &info, frame_buffer))
                return 1;

            last_frame_size = info.frame_size;
            processed += info.frame_size;
            file_pos += info.frame_size;
            stats->duration_s += frame_duration;

            /* Progress indicator */
            int progress = (int)((file_pos * 100) / io->file_size);
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

        /* If no progress and buffer is getting full, we have a problem.
         * Less than one frame left is the partial frame every recording ends
         * in - VDR cuts mid-frame - and not worth a warning. More than that
         * is real trailing garbage. */
        if (bytes_read == 0 && buffer_pos > 0) {
            size_t unusable = skipped_junk + buffer_pos;
            if (last_frame_size > 0 && unusable < last_frame_size)
                fprintf(stderr, "\nPartial frame at file edges (%zu bytes), recording cut mid-frame\n",
                        unusable);
            else
                fprintf(stderr, "\nWarning: %zu bytes could not be parsed (%zu of them at end of file)\n",
                        unusable, buffer_pos);
            break;
        }
    }
    return 0;
}

/* Statistics block and, in analyze mode, the repair recommendation. */
static void print_ac3_stats(const ac3fix_options_t *opts, const ac3fix_stats_t *stats)
{
    char duration_buf[32];
    format_time(stats->duration_s, duration_buf, sizeof(duration_buf));

    printf("Statistics:\n");
    printf("-----------\n");
    printf("Duration:            %s\n", duration_buf);
    printf("Total frames:        %" PRIu64 "\n", stats->total_frames);
    if (stats->total_frames > 0) {
        printf("5.1 surround frames: %" PRIu64 " (%.1f%%)\n", stats->surround_frames,
               100.0 * stats->surround_frames / stats->total_frames);
        printf("Stereo frames:       %" PRIu64 " (%.1f%%)\n", stats->stereo_frames,
               100.0 * stats->stereo_frames / stats->total_frames);
    }
    if (stats->other_frames > 0)
        printf("Other frames:        %" PRIu64 "\n", stats->other_frames);
    printf("Format changes:      %" PRIu64 "\n", stats->format_changes);
    printf("\n");
    printf("Inconsistent frames: %" PRIu64 " (>=%d kbps + stereo header)\n",
           stats->inconsistent_frames, opts->min_bitrate);

    if (opts->force_fix) {
        printf("Fixed frames:        %" PRIu64 "\n", stats->fixed_frames);
    }

    if (stats->inconsistent_frames > 0 && opts->analyze_only) {
        printf("\nRecommendation: Run with --force-fix to repair %" PRIu64 " frames\n",
               stats->inconsistent_frames);
        printf("Example: %s --force-fix %s output.ac3\n",
               "ttcut-ac3fix", opts->input_file);
    }
}

/* Process AC3 file */
static int process_ac3_file(const ac3fix_options_t *opts)
{
    ac3fix_io_t io;
    ac3fix_stats_t stats = {0};
    stats.last_acmod = -1;

    if (open_ac3_files(opts, &io))
        return 1;

    print_ac3_banner(opts);

    int ret = walk_ac3_frames(opts, &io, &stats);
    if (ret == 0) {
        fprintf(stderr, "\rProgress: 100%%\n\n");
        print_ac3_stats(opts, &stats);
    }

    close_ac3_files(&io);
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

/* Boolean switches: short form, long form, and the option field they set. */
typedef struct {
    const char *short_opt;
    const char *long_opt;
    size_t      field;       /* offsetof(ac3fix_options_t, <bool member>) */
} ac3fix_switch_t;

static const ac3fix_switch_t ac3fix_switches[] = {
    { "-a", "--analyze",       offsetof(ac3fix_options_t, analyze_only) },
    { "-v", "--verbose",       offsetof(ac3fix_options_t, verbose) },
    { "-f", "--force",         offsetof(ac3fix_options_t, force) },
    { "-F", "--force-fix",     offsetof(ac3fix_options_t, force_fix) },
    { "-s", "--show-segments", offsetof(ac3fix_options_t, show_segments) },
};

/* Sets the switch named by arg and returns true; false when arg is no
 * switch (an option with an argument, --help, or an unknown one). */
static bool set_ac3fix_switch(ac3fix_options_t *opts, const char *arg)
{
    for (size_t i = 0; i < sizeof(ac3fix_switches) / sizeof(ac3fix_switches[0]); i++) {
        const ac3fix_switch_t *sw = &ac3fix_switches[i];
        if (strcmp(arg, sw->short_opt) == 0 || strcmp(arg, sw->long_opt) == 0) {
            *(bool *)((char *)opts + sw->field) = true;
            return true;
        }
    }
    return false;
}

/* Command-line options into *opts. Returns -1 to continue, otherwise the
 * exit code to leave with (0 after --help, 1 after a usage error). */
static int parse_ac3fix_args(int argc, char **argv, ac3fix_options_t *opts)
{
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (positional == 0) {
                opts->input_file = argv[i];
            } else if (positional == 1) {
                opts->output_file = argv[i];
            }
            positional++;
            continue;
        }
        if (set_ac3fix_switch(opts, argv[i]))
            continue;
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bitrate") == 0) {
            if (i + 1 < argc) {
                int val = atoi(argv[++i]);
                if (val < 32 || val > 640) {
                    fprintf(stderr, "Error: Invalid bitrate '%s' (valid: 32-640 kbps)\n", argv[i]);
                    return 1;
                }
                opts->min_bitrate = (uint16_t)val;
            } else {
                fprintf(stderr, "Error: -b requires an argument\n");
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        return 1;
    }

    return -1;
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

    int rc = parse_ac3fix_args(argc, argv, &opts);
    if (rc >= 0)
        return rc;

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

    /* force_fix without an output file would silently produce no output:
     * process_ac3_file's write loop is gated on (output_file != NULL), so it
     * would read the whole file and report "Fixed frames: N" but write
     * nothing. Reject up front instead of misleading the user. */
    if (!opts.analyze_only && !opts.output_file) {
        fprintf(stderr, "Error: --force-fix requires an output file (-o).\n");
        return 1;
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
