#define _DEFAULT_SOURCE
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
#include <limits.h>
#include <libgen.h>

/*
 * ttcut-pts-analyze - PTS Grid Analysis Tool for TTCut-ng
 *
 * Analyzes MPEG Transport Stream files for extra frames caused by TS
 * corruption.  Detects them via PTS grid analysis (frames with half-duration
 * PTS spacing are extras).
 *
 * Extracted from ttcut-esrepair.c — functions copied verbatim.
 *
 * Copyright (C) 2026 TTCut-ng Project
 * License: GPL v2 or later
 */

/* TS constants */
#define TS_PACKET_SIZE   188
#define TS_SYNC_BYTE     0x47
#define PAT_PID          0x0000

/* Stream types in PMT */
#define ST_MPEG2_VIDEO   0x02
#define ST_H264_VIDEO    0x1B
#define ST_H265_VIDEO    0x24

typedef struct {
    int64_t pts;          /* PTS from PES header (-1 if absent) */
    int64_t dts;          /* DTS from PES header (-1 if absent) */
    int64_t es_offset;    /* Cumulative byte offset in ES file */
    int64_t es_size;      /* Payload bytes contributed to ES */
    int     cc_errors;    /* Continuity counter errors in this AU's packets */
    bool    extra;        /* true if identified as extra frame */
} AccessUnit;

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

/* ---- TS packet parsing ---- */

/* Extract payload from a TS packet, accounting for adaptation field.
 * Returns pointer to payload start and sets *payload_len.
 * Returns NULL if no payload, packet is scrambled, or sync byte invalid. */
static const uint8_t *ts_payload(const uint8_t *pkt, int *payload_len)
{
    if (pkt[0] != TS_SYNC_BYTE)
        return NULL;

    /* transport_scrambling_control: bits 6-7 of byte 3 */
    if ((pkt[3] >> 6) & 0x03)
        return NULL;

    /* adaptation_field_control: bits 4-5 of byte 3 */
    int afc = (pkt[3] >> 4) & 0x03;
    int hdr_len;

    switch (afc) {
    case 0x01:  /* 01: payload only */
        hdr_len = 4;
        break;
    case 0x03:  /* 11: adaptation field + payload */
        hdr_len = 5 + pkt[4];  /* 4 header + 1 AF length byte + AF data */
        if (hdr_len >= TS_PACKET_SIZE)
            return NULL;
        break;
    case 0x02:  /* 10: adaptation field only, no payload */
    case 0x00:  /* 00: reserved */
    default:
        return NULL;
    }

    *payload_len = TS_PACKET_SIZE - hdr_len;
    if (*payload_len <= 0)
        return NULL;
    return pkt + hdr_len;
}

/* Parse PAT to find PMT PID, then PMT to find video stream PID.
 * Returns video PID or -1 on failure. */
static int find_video_pid(const uint8_t *ts_data, int64_t ts_size)
{
    int pmt_pid = -1;
    int video_pid = -1;

    /* Step 1: Find PAT (PID 0) and extract first non-NIT PMT PID */
    for (int64_t off = 0; off + TS_PACKET_SIZE <= ts_size; off += TS_PACKET_SIZE) {
        const uint8_t *pkt = ts_data + off;
        if (pkt[0] != TS_SYNC_BYTE)
            continue;

        int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        if (pid != PAT_PID)
            continue;

        int plen;
        const uint8_t *payload = ts_payload(pkt, &plen);
        if (!payload || plen < 1)
            continue;

        /* If payload_unit_start_indicator is set, skip pointer_field */
        const uint8_t *p = payload;
        int remaining = plen;
        if (pkt[1] & 0x40) {
            int pointer = p[0];
            p += 1 + pointer;
            remaining -= 1 + pointer;
        }

        if (remaining < 8)
            continue;

        /* PAT section: table_id(1) + section_length(2) + tsid(2) + ver(1) + secnum(1) + lastsec(1) */
        uint8_t table_id = p[0];
        if (table_id != 0x00)
            continue;

        int section_length = ((p[1] & 0x0F) << 8) | p[2];
        p += 3;
        remaining -= 3;

        /* Skip transport_stream_id(2) + reserved/version/current(1) + section_number(1) + last_section_number(1) */
        if (remaining < 5)
            continue;
        p += 5;
        remaining -= 5;

        /* Subtract CRC32 (4 bytes) from section data */
        int data_len = section_length - 5 - 4;
        if (data_len < 4 || data_len > remaining)
            continue;

        /* Program entries: program_number(2) + PMT_PID(2) */
        for (int i = 0; i + 3 < data_len; i += 4) {
            int program_number = (p[i] << 8) | p[i + 1];
            if (program_number == 0)
                continue;  /* NIT, skip */
            pmt_pid = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
            break;  /* Take first non-NIT program */
        }
        break;
    }

    if (pmt_pid < 0)
        return -1;

    /* Step 2: Find PMT and extract video PID */
    for (int64_t off = 0; off + TS_PACKET_SIZE <= ts_size; off += TS_PACKET_SIZE) {
        const uint8_t *pkt = ts_data + off;
        if (pkt[0] != TS_SYNC_BYTE)
            continue;

        int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        if (pid != pmt_pid)
            continue;

        int plen;
        const uint8_t *payload = ts_payload(pkt, &plen);
        if (!payload || plen < 1)
            continue;

        /* If payload_unit_start_indicator is set, skip pointer_field */
        const uint8_t *p = payload;
        int remaining = plen;
        if (pkt[1] & 0x40) {
            int pointer = p[0];
            p += 1 + pointer;
            remaining -= 1 + pointer;
        }

        if (remaining < 12)
            continue;

        /* PMT section: table_id(1) + section_length(2) + program_number(2)
         * + reserved/version/current(1) + section_number(1) + last_section_number(1)
         * + PCR_PID(2) + program_info_length(2) */
        uint8_t table_id = p[0];
        if (table_id != 0x02)
            continue;

        int section_length = ((p[1] & 0x0F) << 8) | p[2];
        p += 3;
        remaining -= 3;

        /* Skip program_number(2) + ver(1) + secnum(1) + lastsec(1) + PCR_PID(2) */
        if (remaining < 9)
            continue;
        p += 7;
        remaining -= 7;

        int program_info_length = ((p[0] & 0x0F) << 8) | p[1];
        p += 2;
        remaining -= 2;

        /* Skip program-level descriptors */
        if (program_info_length > remaining)
            continue;
        p += program_info_length;
        remaining -= program_info_length;

        /* Subtract CRC32 and already-consumed header from section_length */
        int stream_data_len = section_length - 9 - program_info_length - 4;
        if (stream_data_len > remaining)
            stream_data_len = remaining;

        /* Stream entries: stream_type(1) + reserved+PID(2) + ES_info_length(2) + descriptors */
        const uint8_t *s = p;
        int srem = stream_data_len;
        while (srem >= 5) {
            uint8_t stream_type = s[0];
            int es_pid = ((s[1] & 0x1F) << 8) | s[2];
            int es_info_length = ((s[3] & 0x0F) << 8) | s[4];
            s += 5;
            srem -= 5;

            if (stream_type == ST_MPEG2_VIDEO ||
                stream_type == ST_H264_VIDEO  ||
                stream_type == ST_H265_VIDEO) {
                video_pid = es_pid;
                return video_pid;
            }

            /* Skip ES descriptors */
            if (es_info_length > srem)
                break;
            s += es_info_length;
            srem -= es_info_length;
        }
        break;
    }

    return video_pid;
}

/* ---- PES header parsing and access unit collection ---- */

/* Parse PTS or DTS from 5 bytes in PES optional header.
 * Format: 4-bit marker | 3 bits [32..30] | 1 marker | 15 bits [29..15] |
 *         1 marker | 15 bits [14..0] | 1 marker
 * Returns 33-bit timestamp in 90kHz units, or -1 on error. */
static int64_t parse_pes_timestamp(const uint8_t *p)
{
    int64_t ts = ((int64_t)(p[0] >> 1) & 0x07) << 30;
    ts |= ((int64_t)p[1]) << 22;
    ts |= ((int64_t)(p[2] >> 1)) << 15;
    ts |= ((int64_t)p[3]) << 7;
    ts |= ((int64_t)(p[4] >> 1));
    return ts;
}

/* Given a TS path like "/path/00001.ts", find all sibling VDR segments
 * (00002.ts, 00003.ts, ...) in the same directory.
 * Returns array of malloc'd path strings and count. Caller frees. */
static int find_vdr_segments(const char *first_ts, char ***segments, int *count)
{
    *segments = NULL;
    *count = 0;

    /* Work on copies since dirname/basename may modify their argument */
    char *path_copy1 = strdup(first_ts);
    char *path_copy2 = strdup(first_ts);
    if (!path_copy1 || !path_copy2) {
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    const char *dir = dirname(path_copy1);
    const char *base = basename(path_copy2);

    /* Check if filename matches VDR pattern: NNNNN.ts (5-digit number) */
    bool is_vdr = false;
    if (strlen(base) == 8 && strcmp(base + 5, ".ts") == 0) {
        is_vdr = true;
        for (int i = 0; i < 5; i++) {
            if (base[i] < '0' || base[i] > '9') {
                is_vdr = false;
                break;
            }
        }
    }

    if (!is_vdr) {
        /* Not a VDR recording — return single entry */
        *segments = (char **)malloc(sizeof(char *));
        if (!*segments) {
            free(path_copy1);
            free(path_copy2);
            return -1;
        }
        (*segments)[0] = strdup(first_ts);
        *count = 1;
        free(path_copy1);
        free(path_copy2);
        return 0;
    }

    /* VDR recording: iterate 00001.ts, 00002.ts, ... */
    int capacity = 16;
    char **segs = (char **)malloc(capacity * sizeof(char *));
    if (!segs) {
        free(path_copy1);
        free(path_copy2);
        return -1;
    }

    int num = 0;
    for (int n = 1; ; n++) {
        char filename[16];
        snprintf(filename, sizeof(filename), "%05d.ts", n);

        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, filename);

        /* Check if file exists */
        if (access(fullpath, R_OK) != 0)
            break;

        if (num >= capacity) {
            capacity *= 2;
            char **tmp = (char **)realloc(segs, capacity * sizeof(char *));
            if (!tmp) {
                for (int i = 0; i < num; i++) free(segs[i]);
                free(segs);
                free(path_copy1);
                free(path_copy2);
                return -1;
            }
            segs = tmp;
        }

        segs[num] = strdup(fullpath);
        num++;
    }

    free(path_copy1);
    free(path_copy2);

    if (num == 0) {
        free(segs);
        return -1;
    }

    *segments = segs;
    *count = num;
    return 0;
}

/* Scan TS file(s) for video PES packets, extract PTS/DTS, track ES byte offsets.
 * Handles VDR multi-file: mmaps each segment internally, processes sequentially.
 * Returns 0 on success, -1 on error. */
static int collect_access_units(const char *ts_path, int video_pid,
                                AccessUnit **out, int *count,
                                int64_t *total_es_bytes, int verbose)
{
    *out = NULL;
    *count = 0;
    *total_es_bytes = 0;

    char **seg_paths = NULL;
    int seg_count = 0;
    if (find_vdr_segments(ts_path, &seg_paths, &seg_count) < 0)
        return -1;

    if (verbose && seg_count > 1)
        fprintf(stderr, "VDR multi-file: %d segments detected\n", seg_count);

    int capacity = 4096;
    AccessUnit *aus = (AccessUnit *)malloc(capacity * sizeof(AccessUnit));
    if (!aus) {
        for (int i = 0; i < seg_count; i++) free(seg_paths[i]);
        free(seg_paths);
        return -1;
    }

    int num_aus = 0;
    int64_t cumulative_es = 0;
    int prev_cc = -1;           /* continuity counter tracking across segments */
    bool have_current_au = false;

    for (int s = 0; s < seg_count; s++) {
        const uint8_t *ts_data = NULL;
        int64_t ts_size = 0;

        if (open_mmap(seg_paths[s], &ts_data, &ts_size) < 0) {
            fprintf(stderr, "Warning: cannot open TS segment '%s', skipping\n", seg_paths[s]);
            continue;
        }

        if (verbose && seg_count > 1)
            fprintf(stderr, "  Processing segment %d/%d: %s (%lld bytes)\n",
                    s + 1, seg_count, seg_paths[s], (long long)ts_size);

        for (int64_t off = 0; off + TS_PACKET_SIZE <= ts_size; off += TS_PACKET_SIZE) {
            const uint8_t *pkt = ts_data + off;
            if (pkt[0] != TS_SYNC_BYTE)
                continue;

            int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
            if (pid != video_pid)
                continue;

            /* Continuity counter check */
            int cc = pkt[3] & 0x0F;
            if (prev_cc >= 0) {
                int expected = (prev_cc + 1) & 0x0F;
                if (cc != expected && have_current_au) {
                    aus[num_aus - 1].cc_errors++;
                }
            }
            prev_cc = cc;

            /* Get TS payload */
            int plen;
            const uint8_t *payload = ts_payload(pkt, &plen);
            if (!payload || plen <= 0)
                continue;

            bool pusi = (pkt[1] & 0x40) != 0;

            if (pusi) {
                /* Payload Unit Start Indicator — new PES packet = new Access Unit */

                /* Finalize previous AU's es_size */
                if (have_current_au) {
                    aus[num_aus - 1].es_size = cumulative_es - aus[num_aus - 1].es_offset;
                }

                /* Grow array if needed */
                if (num_aus >= capacity) {
                    capacity *= 2;
                    AccessUnit *tmp = (AccessUnit *)realloc(aus, capacity * sizeof(AccessUnit));
                    if (!tmp) {
                        free(aus);
                        close_mmap(ts_data, ts_size);
                        for (int i = 0; i < seg_count; i++) free(seg_paths[i]);
                        free(seg_paths);
                        return -1;
                    }
                    aus = tmp;
                }

                /* Parse PES header */
                int64_t pts = -1, dts = -1;
                int es_skip = 0;  /* bytes to skip in this packet's payload for PES header */

                /* Verify PES start code: 0x00 0x00 0x01 */
                if (plen >= 9 &&
                    payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                    uint8_t stream_id = payload[3];

                    /* Video stream IDs: 0xE0-0xEF */
                    if (stream_id >= 0xE0 && stream_id <= 0xEF) {
                        int pes_header_data_length = payload[8];
                        int pts_dts_flags = (payload[7] >> 6) & 0x03;

                        if (pts_dts_flags == 2 && plen >= 14) {
                            /* PTS only */
                            pts = parse_pes_timestamp(payload + 9);
                        } else if (pts_dts_flags == 3 && plen >= 19) {
                            /* PTS + DTS */
                            pts = parse_pes_timestamp(payload + 9);
                            dts = parse_pes_timestamp(payload + 14);
                        }

                        es_skip = 9 + pes_header_data_length;
                    }
                }

                /* Initialize new AU */
                aus[num_aus].pts = pts;
                aus[num_aus].dts = dts;
                aus[num_aus].es_offset = cumulative_es;
                aus[num_aus].es_size = 0;
                aus[num_aus].cc_errors = 0;
                aus[num_aus].extra = false;
                num_aus++;
                have_current_au = true;

                /* Count ES bytes in this first packet (after PES header) */
                int es_bytes = plen - es_skip;
                if (es_bytes > 0)
                    cumulative_es += es_bytes;
            } else {
                /* Continuation packet — all payload bytes are ES data */
                if (have_current_au)
                    cumulative_es += plen;
            }
        }

        close_mmap(ts_data, ts_size);
    }

    /* Finalize last AU's es_size */
    if (have_current_au && num_aus > 0) {
        aus[num_aus - 1].es_size = cumulative_es - aus[num_aus - 1].es_offset;
    }

    /* Free VDR segments array */
    for (int i = 0; i < seg_count; i++)
        free(seg_paths[i]);
    free(seg_paths);

    *out = aus;
    *count = num_aus;
    *total_es_bytes = cumulative_es;
    return 0;
}

/* Compare helper for qsort of PTS entries */
typedef struct {
    int64_t pts;
    int     orig_idx;    /* index into AccessUnit array */
} PtsSortEntry;

static int cmp_pts_entry(const void *a, const void *b)
{
    int64_t va = ((const PtsSortEntry *)a)->pts;
    int64_t vb = ((const PtsSortEntry *)b)->pts;
    return (va > vb) - (va < vb);
}

/* Analyze DTS/PTS sequence to identify extra frames.
 *
 * Three detection methods, applied in order:
 * 1. DTS monotonicity: marks AUs with backward/duplicate DTS (when DTS present)
 * 2. PTS duplicate: marks AUs with exact PTS match within sliding window
 * 3. PTS grid analysis (primary for MPEG-2): detects regions where PTS spacing
 *    drops to half the nominal frame duration, indicating doubled frames from
 *    TS corruption during demux. Frames off the normal grid are marked extra.
 *
 * Handles PTS/DTS wrap (>1s backward jump = epoch reset, not anomaly).
 * Returns number of extra frames detected. */
static int detect_extra_frames(AccessUnit *aus, int count, int verbose)
{
    if (count <= 1)
        return 0;

    int extra_count = 0;

    /* --- Method 1: DTS monotonicity check --- */
    {
        int64_t last_valid_dts = -1;
        int dts_extras = 0;

        for (int i = 0; i < count; i++) {
            if (aus[i].dts < 0)
                continue;

            if (last_valid_dts >= 0 && aus[i].dts <= last_valid_dts) {
                if ((last_valid_dts - aus[i].dts) > 90000) {
                    /* >1s backward jump = PTS/DTS wrap or segment boundary */
                    if (verbose)
                        fprintf(stderr, "  DTS epoch reset at AU #%d: %lld -> %lld\n",
                                i, (long long)last_valid_dts, (long long)aus[i].dts);
                    last_valid_dts = aus[i].dts;
                } else {
                    aus[i].extra = true;
                    dts_extras++;
                    if (verbose)
                        fprintf(stderr, "  Extra frame AU #%d: DTS=%lld (non-monotonic, prev=%lld)\n",
                                i, (long long)aus[i].dts, (long long)last_valid_dts);
                }
            } else {
                last_valid_dts = aus[i].dts;
            }
        }

        if (dts_extras > 0) {
            extra_count = dts_extras;
            if (verbose)
                fprintf(stderr, "PTS/DTS analysis: %d extra frames detected (DTS monotonicity method)\n",
                        extra_count);
            return extra_count;
        }
    }

    /* --- Method 2: PTS duplicate check (sliding window of 16) --- */
    {
        int dup_extras = 0;

        for (int i = 1; i < count; i++) {
            if (aus[i].pts < 0)
                continue;

            int window_start = (i - 16 > 0) ? i - 16 : 0;
            for (int j = i - 1; j >= window_start; j--) {
                if (aus[j].pts == aus[i].pts && aus[j].pts >= 0) {
                    aus[i].extra = true;
                    dup_extras++;
                    if (verbose)
                        fprintf(stderr, "  Extra frame AU #%d: PTS=%lld (duplicate of AU #%d)\n",
                                i, (long long)aus[i].pts, j);
                    break;
                }
            }
        }

        if (dup_extras > 0) {
            extra_count = dup_extras;
            if (verbose)
                fprintf(stderr, "PTS/DTS analysis: %d extra frames detected (PTS duplicate method)\n",
                        extra_count);
            return extra_count;
        }
    }

    /* --- Method 3: PTS grid analysis --- */
    /* Sort PTS values and detect regions where spacing drops to half the
     * nominal frame duration. In those regions, frames that fall off the
     * normal frame-duration grid are extra (doubled frames from TS corruption).
     *
     * Example: 25fps nominal = 3600 ticks. In corrupted regions, spacing
     * drops to 1800 ticks (50 fields/s). Every other frame in such a run
     * is extra — specifically the ones whose PTS doesn't align with the
     * 3600-tick grid established by the surrounding normal frames. */

    /* Collect AUs with valid PTS */
    int valid_count = 0;
    for (int i = 0; i < count; i++) {
        if (aus[i].pts >= 0)
            valid_count++;
    }

    if (valid_count < 2) {
        if (verbose)
            fprintf(stderr, "PTS/DTS analysis: insufficient PTS data for grid analysis\n");
        return 0;
    }

    PtsSortEntry *sorted = (PtsSortEntry *)malloc(valid_count * sizeof(PtsSortEntry));
    if (!sorted)
        return 0;

    int si = 0;
    for (int i = 0; i < count; i++) {
        if (aus[i].pts >= 0) {
            sorted[si].pts = aus[i].pts;
            sorted[si].orig_idx = i;
            si++;
        }
    }

    qsort(sorted, valid_count, sizeof(PtsSortEntry), cmp_pts_entry);

    /* Determine nominal frame duration from the most common PTS gap.
     * Sample gaps and find the mode. Common values: 3600 (25fps), 3003 (29.97fps),
     * 1800 (50fps), 1501 (59.94fps). */
    int64_t gap_counts[8] = {0};
    int64_t gap_values[8] = {3600, 3003, 1800, 1501, 3750, 7200, 6006, 0};
    int num_gap_types = 7;

    for (int i = 1; i < valid_count && i < 10000; i++) {
        int64_t gap = sorted[i].pts - sorted[i-1].pts;
        for (int g = 0; g < num_gap_types; g++) {
            if (gap == gap_values[g]) {
                gap_counts[g]++;
                break;
            }
        }
    }

    int64_t nominal_duration = 3600;  /* default: 25fps */
    int64_t best_count = 0;
    for (int g = 0; g < num_gap_types; g++) {
        if (gap_counts[g] > best_count) {
            best_count = gap_counts[g];
            nominal_duration = gap_values[g];
        }
    }

    int64_t half_duration = nominal_duration / 2;

    if (verbose)
        fprintf(stderr, "PTS grid analysis: nominal frame duration = %lld ticks (%.2f fps)\n",
                (long long)nominal_duration, 90000.0 / nominal_duration);

    /* If nominal is already the smallest unit (e.g., 1800 for 50fps progressive),
     * grid analysis cannot detect extras — there's no "half" to find */
    if (half_duration < 900) {
        if (verbose)
            fprintf(stderr, "PTS grid analysis: frame rate too high for half-duration detection\n");
        free(sorted);
        return 0;
    }

    /* Find runs of half-duration gaps and mark off-grid frames as extra */
    int grid_extras = 0;
    int i = 0;
    while (i < valid_count - 1) {
        int64_t gap = sorted[i + 1].pts - sorted[i].pts;

        if (gap == half_duration) {
            /* Start of a half-duration run */
            int run_start = i;
            int run_end = i + 1;
            while (run_end < valid_count - 1 &&
                   sorted[run_end + 1].pts - sorted[run_end].pts == half_duration) {
                run_end++;
            }

            /* Establish grid base: use the frame just before the run if it exists,
             * otherwise use the run's first frame */
            int64_t grid_base;
            if (run_start > 0)
                grid_base = sorted[run_start - 1].pts;
            else
                grid_base = sorted[run_start].pts;

            /* Mark frames in the run that don't align with the nominal grid */
            int run_extras = 0;
            for (int j = run_start; j <= run_end; j++) {
                int64_t offset = sorted[j].pts - grid_base;
                if (offset % nominal_duration != 0) {
                    int orig = sorted[j].orig_idx;
                    aus[orig].extra = true;
                    grid_extras++;
                    run_extras++;
                    if (verbose)
                        fprintf(stderr, "  Extra frame AU #%d: PTS=%lld (off-grid by %lld ticks)\n",
                                orig, (long long)sorted[j].pts,
                                (long long)(offset % nominal_duration));
                }
            }

            if (verbose && run_extras > 0) {
                int run_len = run_end - run_start + 1;
                fprintf(stderr, "  Half-duration run [%d..%d]: %d frames, %d extra, "
                        "PTS %lld..%lld\n",
                        run_start, run_end, run_len, run_extras,
                        (long long)sorted[run_start].pts,
                        (long long)sorted[run_end].pts);
            }

            i = run_end + 1;
        } else {
            i++;
        }
    }

    free(sorted);

    extra_count = grid_extras;
    if (verbose)
        fprintf(stderr, "PTS/DTS analysis: %d extra frames detected (PTS grid method, "
                "half-duration=%lld ticks)\n", extra_count, (long long)half_duration);

    return extra_count;
}

/* ---- main ---- */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [--verbose] <file.ts>\n\n"
        "Analyze TS file for extra frames via PTS grid analysis.\n\n"
        "Options:\n"
        "  --verbose, -v   Show progress on stderr\n"
        "  --help, -h      Show this help\n\n"
        "Output (stdout):\n"
        "  extra_frames=<comma-separated frame indices>\n"
        "  (empty if no extra frames detected)\n\n"
        "Exit codes:\n"
        "  0  Clean stream\n"
        "  1  Extra frames detected\n"
        "  2  Fatal error\n",
        prog);
}

int main(int argc, char *argv[])
{
    bool verbose = false;

    static struct option long_options[] = {
        {"verbose", no_argument, NULL, 'v'},
        {"help",    no_argument, NULL, 'h'},
        {NULL,      0,           NULL,  0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'v': verbose = true; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 2;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified.\n\n");
        print_usage(argv[0]);
        return 2;
    }

    const char *ts_file = argv[optind];

    const uint8_t *ts_data = NULL;
    int64_t ts_size = 0;
    if (open_mmap(ts_file, &ts_data, &ts_size) < 0)
        return 2;

    if (verbose)
        fprintf(stderr, "TS file: %s (%lld bytes)\n", ts_file, (long long)ts_size);

    int video_pid = find_video_pid(ts_data, ts_size);
    if (video_pid < 0) {
        fprintf(stderr, "Error: cannot detect video PID from TS.\n");
        close_mmap(ts_data, ts_size);
        return 2;
    }

    if (verbose)
        fprintf(stderr, "Video PID: 0x%04x (%d)\n", video_pid, video_pid);

    close_mmap(ts_data, ts_size);

    AccessUnit *aus = NULL;
    int au_count = 0;
    int64_t total_es_bytes = 0;

    if (collect_access_units(ts_file, video_pid,
                             &aus, &au_count, &total_es_bytes, verbose) < 0) {
        fprintf(stderr, "Error: PTS analysis failed\n");
        return 2;
    }

    if (verbose) {
        int with_dts = 0, with_pts = 0;
        for (int i = 0; i < au_count; i++) {
            if (aus[i].dts >= 0) with_dts++;
            if (aus[i].pts >= 0) with_pts++;
        }
        fprintf(stderr, "Access units: %d (DTS: %d, PTS: %d)\n",
                au_count, with_dts, with_pts);
    }

    int extra_count = detect_extra_frames(aus, au_count, verbose);

    if (extra_count <= 0) {
        if (verbose)
            fprintf(stderr, "No extra frames detected\n");
        free(aus);
        return 0;
    }

    printf("extra_frames=");
    bool first = true;
    for (int i = 0; i < au_count; i++) {
        if (aus[i].extra) {
            if (!first) printf(",");
            printf("%d", i);
            first = false;
        }
    }
    printf("\n");

    if (verbose)
        fprintf(stderr, "Extra frames detected: %d\n", extra_count);

    free(aus);
    return 1;
}
