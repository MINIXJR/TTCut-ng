/* ttcut-audiofix - structural sanitizer for MP2/AC3/E-AC3 elementary streams.
 * Walks the ES frame by frame, drops junk bytes between valid frames,
 * reports CRC-damaged frames. No time insertion (that is ttcut-demux Rev 3's
 * job) and no re-encode. Exit: 0 clean, 1 defects found/fixed, 2 error.
 *
 * Copyright (C) 2026 TTCut-ng Project
 * License: GPL v2 or later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define RESYNC_CONFIRM 3        /* aufeinanderfolgende gueltige Rahmen fuer Wiedereinstieg */
#define BASELINE_FRAMES 3       /* Rahmen fuer die Codec-Baseline */
#define MAX_REPORT_ITEMS 100000 /* Deckel wie TTESInfo maxExtraFrames */

typedef struct {
    uint32_t frame_size;      /* bytes */
    uint32_t samples;         /* samples this frame represents (0 = dependent substream, no time) */
    uint32_t samplerate;
    uint8_t  layer, version, bsid, acmod;
    bool     crc_present;
} frame_info_t;

typedef struct {
    const char *name;
    /* >0 = frame size; 0 = no valid frame at p; parses header only, len is remaining bytes */
    ssize_t (*parse_frame)(const uint8_t *p, size_t len, frame_info_t *out);
    /* true = CRC ok or no CRC present. MP2 real since Task 3, AC3 real
     * since Task 4; E-AC3 stub (always true) until Task 5. */
    bool (*check_crc)(const uint8_t *p, size_t frame_len, const frame_info_t *fi);
    /* Folge-/Resync-Regel gegen Baseline */
    bool (*params_plausible)(const frame_info_t *ref, const frame_info_t *cur);
} codec_ops_t;

/* ---------- CRC-16, ISO/IEC 11172-3 Annex A (x^16+x^15+x^2+1, 0x8005) ----------
 * Bit-serial, MSB-first, no reflection. Shared by MP2 (this file, Task 3) and
 * AC3/E-AC3 (Task 4/5, same polynomial per ATSC A/52 Annex A). */
static uint16_t crc16_msb_bits(uint16_t crc, const uint8_t *d, size_t nbits)
{
    for (size_t i = 0; i < nbits; i++) {
        int bit    = (d[i >> 3] >> (7 - (i & 7))) & 1;
        int outbit = (crc >> 15) & 1;
        crc = (uint16_t)(crc << 1);
        if (outbit ^ bit) crc ^= 0x8005;
    }
    return crc;
}

static uint16_t crc16_msb(uint16_t crc, const uint8_t *d, size_t nbytes)
{
    return crc16_msb_bits(crc, d, nbytes * 8);
}

/* ---------- MP2 (MPEG-1 Audio Layer II) ---------- */
static const int mp2_bitrate_kbps[] = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384};
static const int mp2_samplerate[]   = {44100,48000,32000,0};

/* Allocation tables transcribed from libtwolame 0.4.0 (deb-multimedia source
 * package twolame-dmo, `apt source libtwolame0`), libtwolame/encode.c: nbal[],
 * line[5][SBLIMIT], table_sblimit[] (== ISO 11172-3 Tables B.2a-d + the MPEG-2
 * LSF table, unreachable here since mp2_parse_frame accepts MPEG-1 only) and
 * get_js_bound()'s jsb_table[]. Table selection rule below mirrors
 * twolame_encode_init()'s MPEG-1 branch (encode.c ~195-210). Verified against
 * real libtwolame output: encoding the Tux test clip with
 * `-c:a libtwolame -error_protection 1` and recomputing the CRC for 400
 * consecutive frames reproduced the stored CRC in every frame (see
 * task-3-report.md). */
static const int mp2_crc_nbal[9] = { 4, 4, 3, 2, 4, 3, 4, 3, 2 };
static const int mp2_crc_line[5][32] = {
    {  0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,-1,-1,-1,-1,-1 },
    {  0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3,-1,-1 },
    {  4, 4, 5, 5, 5, 5, 5, 5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 },
    {  4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 },
    {  6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 },
};
static const int mp2_crc_sblimit[5] = {27,30,8,12,30};
static const int mp2_crc_jsb[4]     = {4,8,12,16};

static ssize_t mp2_parse_frame(const uint8_t *p, size_t len, frame_info_t *out)
{
    if (len < 4) return 0;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return 0;
    int version = (p[1] >> 3) & 0x03;   /* 3 = MPEG-1 */
    int layer   = (p[1] >> 1) & 0x03;   /* 2 = Layer II */
    if (version != 3 || layer != 2) return 0;   /* nur MPEG-1 Layer II (DVB) */
    int prot    = p[1] & 0x01;          /* 0 = CRC vorhanden */
    int br_idx  = (p[2] >> 4) & 0x0F;
    int sr_idx  = (p[2] >> 2) & 0x03;
    int padding = (p[2] >> 1) & 0x01;
    if (br_idx == 0 || br_idx == 15 || sr_idx == 3) return 0; /* free format/invalid */
    int bitrate = mp2_bitrate_kbps[br_idx] * 1000;
    int sr      = mp2_samplerate[sr_idx];
    out->frame_size  = 144 * bitrate / sr + padding;
    out->samples     = 1152;
    out->samplerate  = sr;
    out->layer       = 2;
    out->version     = 1;
    out->bsid        = 0;
    out->acmod       = (p[3] >> 6) & 0x03;  /* channel mode */
    out->crc_present = (prot == 0);
    if (out->frame_size < 24 || out->frame_size > len) return 0; /* Rahmen ragt ueber EOF */
    return (ssize_t)out->frame_size;
}

/* MSB-first bit reader bounded to [0, len*8). Runs off the end of the
 * frame -> *ok=false (truncated/corrupted bit-allocation area, not a crash). */
static unsigned mp2_read_bits(const uint8_t *p, size_t len, size_t *bitpos, int n, bool *ok)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) {
        size_t bytepos = *bitpos >> 3;
        if (bytepos >= len) { *ok = false; return 0; }
        v = (v << 1) | (unsigned)((p[bytepos] >> (7 - (*bitpos & 7))) & 1);
        (*bitpos)++;
    }
    return v;
}

static bool mp2_check_crc(const uint8_t *p, size_t fs, const frame_info_t *fi)
{
    if (!fi->crc_present) return true;

    int mode     = (p[3] >> 6) & 0x03;   /* 0 stereo, 1 joint stereo, 2 dual, 3 mono */
    int mode_ext = (p[3] >> 4) & 0x03;
    int br_idx   = (p[2] >> 4) & 0x0F;
    int sr_idx   = (p[2] >> 2) & 0x03;
    int bitrate  = mp2_bitrate_kbps[br_idx];
    int sr       = mp2_samplerate[sr_idx];
    if (bitrate <= 0 || sr <= 0) return false;   /* mp2_parse_frame already excludes this */

    int nch       = (mode == 3) ? 1 : 2;
    int br_per_ch = bitrate / nch;
    int sfrq      = sr / 1000;

    /* Table selection, ISO 11172-3 / libtwolame twolame_encode_init() MPEG-1 branch. */
    int tablenum;
    if ((sfrq == 48 && br_per_ch >= 56) || (br_per_ch >= 56 && br_per_ch <= 80))
        tablenum = 0;
    else if (sfrq != 48 && br_per_ch >= 96)
        tablenum = 1;
    else if (sfrq != 32 && br_per_ch <= 48)
        tablenum = 2;
    else
        tablenum = 3;

    int sblimit = mp2_crc_sblimit[tablenum];
    int jsbound = (mode == 1) ? mp2_crc_jsb[mode_ext] : sblimit;   /* 1 = joint stereo */

    /* Walk the bit-allocation area to find N, the length of the CRC-protected
     * data region (bit-alloc + SCFSI bits only — the scale factors themselves
     * are NOT covered, ISO 11172-3 Annex A). */
    bool ok = true;
    size_t bitpos = 6 * 8;
    int bit_alloc[2][32] = {{0}};
    for (int sb = 0; sb < sblimit && ok; sb++) {
        int chan_count = (mode == 1 && sb >= jsbound) ? 1 : nch;
        for (int ch = 0; ch < chan_count; ch++)
            bit_alloc[ch][sb] = (int)mp2_read_bits(p, fs, &bitpos,
                mp2_crc_nbal[mp2_crc_line[tablenum][sb]], &ok);
        if (mode == 1 && sb >= jsbound)
            bit_alloc[1][sb] = bit_alloc[0][sb];   /* shared allocation above jsbound */
    }
    if (!ok) return false;
    for (int sb = 0; sb < sblimit; sb++)
        for (int ch = 0; ch < nch; ch++)
            if (bit_alloc[ch][sb] != 0) bitpos += 2;   /* SCFSI: counted, not decoded */

    if ((bitpos >> 3) > fs) return false;   /* protected area runs past this frame */

    size_t nbits = bitpos - 6 * 8;
    uint16_t crc = crc16_msb(0xFFFF, p + 2, 2);
    crc = crc16_msb_bits(crc, p + 6, nbits);
    uint16_t stored = (uint16_t)(((unsigned)p[4] << 8) | p[5]);
    return crc == stored;
}

static bool mp2_params_plausible(const frame_info_t *ref, const frame_info_t *cur)
{
    /* Konstant je Spur: Samplerate, Layer, Version. Bitrate darf variieren. */
    return cur->samplerate == ref->samplerate
        && cur->layer      == ref->layer
        && cur->version    == ref->version;
}

static const codec_ops_t mp2_ops = { "mp2", mp2_parse_frame, mp2_check_crc, mp2_params_plausible };

/* ---------- AC3 (ATSC A/52) ---------- */

/* Frame size table (words; bytes = words*2), indexed [frmsizecod][fscod]
 * (fscod 0=48kHz, 1=44.1kHz, 2=32kHz). Verified against
 * /usr/local/src/ffmpeg-src/libavcodec/ac3tab.c ff_ac3_frame_size_tab[38][3]
 * (local ffmpeg 9.0.1 source checkout, deb-multimedia ffmpeg-dmo package
 * source) -- that table is itself ATSC A/52 Table 5.18. The 48kHz column
 * also matches tools/ttcut-ac3fix/ttcut-ac3fix.c ac3_frame_sizes_48k[].
 * The 44.1kHz column carries the +1-word padding on odd frmsizecod
 * (69/70, 87/88, 104/105, ...). */
static const uint16_t ac3_frame_size_tab[38][3] = {
    { 64,   69,   96   }, { 64,   70,   96   },
    { 80,   87,   120  }, { 80,   88,   120  },
    { 96,   104,  144  }, { 96,   105,  144  },
    { 112,  121,  168  }, { 112,  122,  168  },
    { 128,  139,  192  }, { 128,  140,  192  },
    { 160,  174,  240  }, { 160,  175,  240  },
    { 192,  208,  288  }, { 192,  209,  288  },
    { 224,  243,  336  }, { 224,  244,  336  },
    { 256,  278,  384  }, { 256,  279,  384  },
    { 320,  348,  480  }, { 320,  349,  480  },
    { 384,  417,  576  }, { 384,  418,  576  },
    { 448,  487,  672  }, { 448,  488,  672  },
    { 512,  557,  768  }, { 512,  558,  768  },
    { 640,  696,  960  }, { 640,  697,  960  },
    { 768,  835,  1152 }, { 768,  836,  1152 },
    { 896,  975,  1344 }, { 896,  976,  1344 },
    { 1024, 1114, 1536 }, { 1024, 1115, 1536 },
    { 1152, 1253, 1728 }, { 1152, 1254, 1728 },
    { 1280, 1393, 1920 }, { 1280, 1394, 1920 },
};

static const int ac3_samplerate[] = { 48000, 44100, 32000 };

static uint32_t ac3_frame_size_words(int fscod, int frmsizecod)
{
    return ac3_frame_size_tab[frmsizecod][fscod];
}

static ssize_t ac3_parse_frame(const uint8_t *p, size_t len, frame_info_t *out)
{
    if (len < 8) return 0;
    if (p[0] != 0x0B || p[1] != 0x77) return 0;
    uint8_t bsid = (p[5] >> 3) & 0x1F;
    if (bsid <= 8) {
        int fscod = (p[4] >> 6) & 0x03;
        int frmsizecod = p[4] & 0x3F;
        if (fscod == 3 || frmsizecod >= 38) return 0;
        uint32_t words = ac3_frame_size_words(fscod, frmsizecod);
        out->frame_size  = words * 2;
        out->samples     = 1536;
        out->samplerate  = ac3_samplerate[fscod];
        out->bsid        = bsid;
        out->acmod       = (p[6] >> 5) & 0x07;
        out->crc_present = true;
        out->layer = 0; out->version = 0;
        if (out->frame_size > len) return 0;
        return (ssize_t)out->frame_size;
    }
    if (bsid >= 11 && bsid <= 16) {
        /* E-AC3 (ATSC A/52 Annex E). Byte 2: strmtyp(2) substreamid(3)
         * frmsiz[10:8](3) ; Byte 3: frmsiz[7:0]. Byte 4: fscod(2) then
         * fscod==3 ? fscod2(2) : numblkscod(2). */
        int strmtyp     = (p[2] >> 6) & 0x03;
        int substreamid = (p[2] >> 3) & 0x07;
        uint32_t frmsiz = ((uint32_t)(p[2] & 0x07) << 8) | p[3];
        out->frame_size = (frmsiz + 1) * 2;
        int fscod = (p[4] >> 6) & 0x03;
        int numblks;
        int sr;
        if (fscod == 3) {
            int fscod2 = (p[4] >> 4) & 0x03;
            if (fscod2 == 3) return 0;
            sr = (int[]){24000,22050,16000}[fscod2];
            numblks = 6;
        } else {
            sr = (int[]){48000,44100,32000}[fscod];
            numblks = (int[]){1,2,3,6}[(p[4] >> 4) & 0x03];
        }
        if (strmtyp == 3) return 0;
        out->samplerate  = sr;
        out->samples     = (strmtyp == 1) ? 0 : (uint32_t)numblks * 256; /* dependent: keine Zeit */
        out->bsid        = bsid;
        out->crc_present = true;
        out->acmod = 0; out->layer = 0; out->version = 0;
        (void)substreamid;
        if (out->frame_size > len) return 0;
        return (ssize_t)out->frame_size;
    }
    return 0;
}

/* E-AC3 CRC (ATSC A/52 Annex E, addr1.5 / crc note in Annex A): CRC-16
 * (poly 0x8005, init 0) over the WHOLE syncframe from byte 2 up to and
 * including the trailing CRC word (byte frame_size-2 .. frame_size-1) --
 * result 0 for an intact frame. Unlike legacy AC3's crc1/crc2 split
 * (checked twice over sub-regions), E-AC3 carries exactly one CRC word,
 * at the very end of the frame. Scope calibrated against the SES-UHD-Demo
 * E-AC3 track (gate Check 12): a wrong start byte or init value would flag
 * every frame as CRC-bad on that clean, real-world material. */
static bool eac3_check_crc(const uint8_t *p, size_t fs)
{
    return crc16_msb(0, p + 2, fs - 2) == 0;
}

static bool ac3_check_crc(const uint8_t *p, size_t fs, const frame_info_t *fi)
{
    if (fi->bsid >= 11) return eac3_check_crc(p, fs);  /* Task 5 */
    /* A/52 7.10: crc1 covers the first 5/8 of the syncframe (16-bit words),
     * starting right after the sync word and including the crc1 field
     * itself -- self-checking CRC-16 (poly 0x8005, init 0): result 0 for
     * an intact frame. crc2 is the same CRC over the whole frame (byte 2
     * .. frame_size-1). crc2 matches ffmpeg's own AC3 decoder check
     * (libavcodec/ac3dec.c: av_crc(AV_CRC_16_ANSI, 0, &buf[2],
     * frame_size-2)) -- AV_CRC_16_ANSI is exactly this poly/init/MSB-first,
     * non-reflected combination (libavutil/crc.c:
     * DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_16_ANSI, 0, 16, 0x8005)),
     * confirmed in the local ffmpeg source tree. ffmpeg's decoder does not
     * implement crc1 (early-abort check, obsolete for software decoders);
     * verified empirically here instead (gate Check 8: real DVB AC3
     * material, exit 0 -- a wrong crc1 scope would flag every frame). */
    size_t words = fs / 2;
    size_t w58   = (words >> 1) + (words >> 3);
    if (crc16_msb(0, p + 2, w58 * 2 - 2) != 0) return false;
    if (crc16_msb(0, p + 2, fs - 2) != 0) return false;
    return true;
}

static bool ac3_params_plausible(const frame_info_t *ref, const frame_info_t *cur)
{
    /* acmod DARF je Rahmen wechseln (DVB-Fall, siehe Spec). frame_size darf
     * variieren (E-AC3, Task 5). Konstant: Samplerate. */
    return cur->samplerate == ref->samplerate;
}

static const codec_ops_t ac3_ops = { "ac3", ac3_parse_frame, ac3_check_crc, ac3_params_plausible };

static const codec_ops_t *all_ops[] = { &mp2_ops, &ac3_ops, NULL };

/* ---------- Walk-Kern ---------- */
typedef struct {
    uint64_t total_frames, total_samples, junk_bytes, dropped_frames, crc_bad;
    uint32_t samplerate; /* aus dem ERSTEN gueltigen Rahmen; 0 solange keiner geparst wurde */
    uint32_t ref_frame_size; /* Groesse des ERSTEN gueltigen Rahmens; Schwelle fuer
                              * angeschnittene Raender. DVB liefert CBR, damit ist sie
                              * exakt; bei VBR ist sie eine Naeherung, was fuer ein
                              * "kleiner als ein Rahmen" genuegt. 0 = kein Rahmen geparst. */
    uint8_t  bsid;        /* aus dem ERSTEN gueltigen Rahmen (AC3: <=8 vs. E-AC3 11-16) */
    /* Report-Listen: dynamische Arrays (malloc/realloc), Deckel MAX_REPORT_ITEMS */
    struct { uint64_t frame_idx; uint64_t ms; uint32_t bytes; } *junk;  size_t njunk;
    struct { uint64_t frame_idx; uint64_t ms; } *crcbad;                size_t ncrc;
} walk_stats_t;

/* Prueft ab p eine Kette von n gueltigen, plausiblen Rahmen (fuer Resync
 * und Codec-Erkennung). ref==NULL: erster Rahmen der Kette wird Referenz. */
static bool confirm_chain(const codec_ops_t *ops, const uint8_t *p, size_t len,
                          const frame_info_t *ref, int n)
{
    frame_info_t local_ref, fi;
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        ssize_t fs = ops->parse_frame(p + pos, len - pos, &fi);
        /* Kette am EOF: weniger als n Rahmen Platz -> gelten lassen, wenn
         * alles bis EOF gueltig war (sonst ist ein sauberes Dateiende
         * faelschlich Muell). */
        if (fs == 0) return (pos > 0 && pos == len) ? true : false;
        if (ref == NULL && i == 0) { local_ref = fi; ref = &local_ref; }
        if (!ops->params_plausible(ref, &fi)) return false;
        pos += (size_t)fs;
        if (pos > len) return false;
    }
    return true;
}

/* Codec-Erkennung: erste Ops, deren Kette an einem Offset < PROBE_WINDOW
 * bestaetigt; bei mehreren gewinnt der fruehere Offset. */
#define PROBE_WINDOW (1024*1024)
static const codec_ops_t *detect_codec(const uint8_t *p, size_t len, size_t *start)
{
    size_t limit = len < PROBE_WINDOW ? len : PROBE_WINDOW;
    for (size_t off = 0; off < limit; off++)
        for (int i = 0; all_ops[i]; i++)
            if (confirm_chain(all_ops[i], p + off, len - off, NULL, RESYNC_CONFIRM)) {
                *start = off;
                return all_ops[i];
            }
    return NULL;
}

/* Haengt einen Muellbereich an. Ueber MAX_REPORT_ITEMS hinaus wird nur noch
 * die Summe (junk_bytes) fortgeschrieben, die Liste bleibt gedeckelt. */
static void record_junk(walk_stats_t *st, uint64_t frame_idx, uint64_t ms, uint32_t bytes)
{
    st->junk_bytes += bytes;
    if (st->njunk >= MAX_REPORT_ITEMS) return;
    void *tmp = realloc(st->junk, (st->njunk + 1) * sizeof(*st->junk));
    if (!tmp) return; /* Allokation fehlgeschlagen: Summe steht, Eintrag entfaellt */
    st->junk = tmp;
    st->junk[st->njunk].frame_idx = frame_idx;
    st->junk[st->njunk].ms        = ms;
    st->junk[st->njunk].bytes     = bytes;
    st->njunk++;
}

/* Haengt einen CRC-defekten Rahmen an. Gleicher Deckel-Mechanismus wie
 * record_junk. */
static void record_crc_bad(walk_stats_t *st, uint64_t frame_idx, uint64_t ms)
{
    st->crc_bad++;
    if (st->ncrc >= MAX_REPORT_ITEMS) return;
    void *tmp = realloc(st->crcbad, (st->ncrc + 1) * sizeof(*st->crcbad));
    if (!tmp) return;
    st->crcbad = tmp;
    st->crcbad[st->ncrc].frame_idx = frame_idx;
    st->crcbad[st->ncrc].ms        = ms;
    st->ncrc++;
}

/* Aktuelle Zeitposition in ms, aus den bereits gezaehlten Samples und der
 * Samplerate des ersten gueltigen Rahmens. Ganzzahlig gerundet. */
static uint64_t current_ms(const walk_stats_t *st)
{
    if (st->samplerate == 0) return 0;
    return (uint64_t)((double)st->total_samples * 1000.0 / st->samplerate + 0.5);
}

/* Der eigentliche Walk. sink==NULL: analyze. sink!=NULL: gueltige Rahmen
 * dorthin schreiben (Task 2). Rueckgabe wie Exit-Code (0/1/2). */
static int walk(const codec_ops_t *ops, const uint8_t *p, size_t len,
                size_t start, FILE *sink, walk_stats_t *st)
{
    frame_info_t baseline, fi;
    bool have_baseline = false;
    size_t pos = start;
    if (start > 0) { /* Muell am Dateianfang */
        record_junk(st, 0, 0, (uint32_t)start);
    }
    while (pos < len) {
        ssize_t fs;
        if (have_baseline) {
            fs = ops->parse_frame(p + pos, len - pos, &fi);
            if (fs > 0 && !ops->params_plausible(&baseline, &fi))
                fs = 0;
        } else {
            fs = ops->parse_frame(p + pos, len - pos, &fi);
        }
        if (fs > 0) {
            /* Zeitrechnung beginnt am ERSTEN gueltigen Rahmen, unabhaengig
             * von der Plausibilitaets-Baseline (die weiter BASELINE_FRAMES
             * braucht) - sonst melden Muell/CRC-Funde vor Rahmen 3 faelschlich
             * ms=0. */
            if (st->total_frames == 0) {
                st->samplerate = fi.samplerate; st->bsid = fi.bsid;
                st->ref_frame_size = (uint32_t)fs;
            }
            if (!have_baseline && st->total_frames + 1 >= BASELINE_FRAMES)
                { baseline = fi; have_baseline = true; }
            if (!ops->check_crc(p + pos, (size_t)fs, &fi))
                record_crc_bad(st, st->total_frames, current_ms(st));
            if (sink && fwrite(p + pos, 1, (size_t)fs, sink) != (size_t)fs)
                return 2;
            st->total_frames++;
            if (fi.samples) st->total_samples += fi.samples;  /* nur unabhaengige Frames */
            pos += (size_t)fs;
            continue;
        }
        /* Kein gueltiger Rahmen: Muell oder abgeschnittener letzter Rahmen */
        size_t junk_start = pos;
        size_t next = pos + 1;
        while (next < len
               && !confirm_chain(ops, p + next, len - next,
                                 have_baseline ? &baseline : NULL, RESYNC_CONFIRM))
            next++;
        if (next >= len) {
            /* Rest der Datei ist Muell/abgeschnitten */
            record_junk(st, st->total_frames, current_ms(st), (uint32_t)(len - junk_start));
            st->dropped_frames++;   /* angeschnittener letzter Rahmen */
            pos = len;
        } else {
            record_junk(st, st->total_frames, current_ms(st), (uint32_t)(next - junk_start));
            pos = next;
        }
    }
    return (st->njunk || st->ncrc) ? 1 : 0;
}

/* ---------- Report ---------- */
static void print_report(const codec_ops_t *ops, const walk_stats_t *st)
{
    /* ac3_ops covers both legacy AC3 (bsid <= 8) and E-AC3 (bsid 11-16);
     * the report distinguishes them by the first valid frame's bsid. */
    const char *name = (ops == &ac3_ops && st->bsid >= 11) ? "eac3" : ops->name;
    printf("codec=%s\n", name);
    printf("total_frames=%" PRIu64 "\n", st->total_frames);
    printf("total_ms=%" PRIu64 "\n", current_ms(st));

    /* Ein VDR-Mitschnitt beginnt und endet mitten im Rahmen; der angeschnittene
     * Rest ist kein gueltiger Rahmen und landete darum wie jeder Defekt in
     * junk_regions -> ttcut-demux machte daraus audio_N_corrupt_ranges und
     * TTCut-ng zeigte auf praktisch jeder Aufnahme einen "Tonstoerungen"-
     * Marker. Solche Raender werden hier getrennt ausgewiesen: entfernt werden
     * sie weiterhin (der Exit-Code bleibt unberuehrt, sonst repariert
     * ttcut-demux gar nicht mehr), gemeldet werden sie nicht mehr als Defekt.
     * Rand heisst: vor dem ersten oder nach dem letzten gueltigen Rahmen UND
     * kleiner als ein Rahmen. Ist mehr als ein Rahmen betroffen, war das kein
     * sauberer Schnitt, sondern Schaden - der bleibt sichtbar. */
    uint64_t edge_bytes = 0;

    printf("junk_regions=");
    bool first_junk = true;
    for (size_t i = 0; i < st->njunk; i++) {
        const uint64_t idx = st->junk[i].frame_idx;
        const bool at_edge = (idx == 0 || idx == st->total_frames)
                          && st->ref_frame_size > 0
                          && st->junk[i].bytes < st->ref_frame_size;
        if (at_edge) { edge_bytes += st->junk[i].bytes; continue; }
        if (!first_junk) printf(",");
        first_junk = false;
        printf("%" PRIu64 "@%" PRIu64 ":%u",
               st->junk[i].frame_idx, st->junk[i].ms, st->junk[i].bytes);
    }
    printf("\n");
    printf("edge_junk_bytes=%" PRIu64 "\n", edge_bytes);

    printf("dropped_frames=%" PRIu64 "\n", st->dropped_frames);

    /* Aufeinanderfolgende Rahmenindizes werden zu einem Bereich gefasst
     * (IDX@MS-IDX@MS); ein einzelner defekter Rahmen ohne Nachbarn druckt nur
     * IDX@MS (kein degeneriertes IDX@MS-IDX@MS). Ab Task 3 mit echten
     * CRC-Befunden erstmals exerciert. */
    printf("crc_bad_frames=");
    bool first = true;
    size_t i = 0;
    while (i < st->ncrc) {
        size_t j = i;
        while (j + 1 < st->ncrc && st->crcbad[j + 1].frame_idx == st->crcbad[j].frame_idx + 1)
            j++;
        if (!first) printf(",");
        if (j == i)
            printf("%" PRIu64 "@%" PRIu64, st->crcbad[i].frame_idx, st->crcbad[i].ms);
        else
            printf("%" PRIu64 "@%" PRIu64 "-%" PRIu64 "@%" PRIu64,
                   st->crcbad[i].frame_idx, st->crcbad[i].ms,
                   st->crcbad[j].frame_idx, st->crcbad[j].ms);
        first = false;
        i = j + 1;
    }
    printf("\n");
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s -a <file>          Analyze (report only)\n"
        "       %s -f <in> <out>      Fix (write sanitized copy)\n"
        "\n"
        "Exit codes:\n"
        "  0  clean\n"
        "  1  defects found\n"
        "  2  error\n",
        prog, prog);
}

/* Read-only mmap of a file. On any error prints a message to stderr and
 * returns NULL (nothing left mapped/open). Used for both the input file
 * and, in fix mode, the self-check re-read of the freshly written output. */
static const uint8_t *map_file_ro(const char *path, size_t *len_out, void **mapped_out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    struct stat st_buf;
    if (fstat(fd, &st_buf) < 0 || st_buf.st_size == 0) {
        fprintf(stderr, "Error: cannot stat '%s' or file is empty\n", path);
        close(fd);
        return NULL;
    }
    size_t len = (size_t)st_buf.st_size;
    void *mapped = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  /* fd can be closed after mmap */
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed for '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    *len_out = len;
    *mapped_out = mapped;
    return (const uint8_t *)mapped;
}

int main(int argc, char *argv[])
{
    bool analyze = false, fix = false;
    int opt;

    while ((opt = getopt(argc, argv, "af")) != -1) {
        switch (opt) {
        case 'a': analyze = true; break;
        case 'f': fix = true; break;
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    if (analyze == fix) {   /* weder noch, oder beide zugleich */
        print_usage(argv[0]);
        return 2;
    }

    if (fix && optind + 1 >= argc) {
        fprintf(stderr, "Error: fix mode (-f) requires <in> and <out>.\n");
        print_usage(argv[0]);
        return 2;
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: no input file specified.\n");
        print_usage(argv[0]);
        return 2;
    }
    const char *in_path = argv[optind];
    const char *out_path = fix ? argv[optind + 1] : NULL;

    size_t len = 0;
    void *mapped = NULL;
    const uint8_t *p = map_file_ro(in_path, &len, &mapped);
    if (!p) return 2;

    size_t start = 0;
    const codec_ops_t *ops = detect_codec(p, len, &start);
    if (!ops) {
        fprintf(stderr, "Error: no recognized audio codec found in '%s'\n", in_path);
        munmap(mapped, len);
        return 2;
    }

    FILE *sink = NULL;
    if (fix) {
        sink = fopen(out_path, "wb");
        if (!sink) {
            fprintf(stderr, "Error: cannot open '%s' for writing: %s\n", out_path, strerror(errno));
            munmap(mapped, len);
            return 2;
        }
    }

    walk_stats_t st = {0};
    int rc = walk(ops, p, len, start, sink, &st);

    munmap(mapped, len);

    if (fix) {
        bool write_err = (rc == 2);
        if (fclose(sink) != 0) write_err = true;
        if (write_err) {
            fprintf(stderr, "Error: failed to write '%s'\n", out_path);
            unlink(out_path);
            free(st.junk);
            free(st.crcbad);
            return 2;
        }

        /* Selbstpruefung: die frisch geschriebene Ausgabe erneut einlesen und
         * mit denselben Ops verwalken. "Sauber" heisst hier STRUKTURELL
         * sauber, nicht defektfrei: CRC-defekte Rahmen werden bewusst
         * durchkopiert (Spec-Entscheidung 2) und sind deshalb KEIN
         * Selbstpruefungs-Fehler, solange ihre Anzahl gegenueber dem
         * Schreib-Durchlauf unveraendert ist. Fehler sind: die Ausgabe laesst
         * sich nicht zurueck-mappen/-walken (leer, kaputt), es blieb Muell
         * drin, oder die Zahl CRC-defekter Rahmen hat sich veraendert
         * (verloren oder neu entstanden). Nicht sauber -> Ausgabe loeschen. */
        size_t out_len = 0;
        void *out_mapped = NULL;
        const uint8_t *outp = map_file_ro(out_path, &out_len, &out_mapped);
        int check_rc = -1;
        walk_stats_t check_st = {0};
        if (outp) {
            check_rc = walk(ops, outp, out_len, 0, NULL, &check_st);
            munmap(out_mapped, out_len);
        }

        bool clean = outp && check_rc != 2
                   && check_st.njunk == 0
                   && check_st.ncrc == st.ncrc;

        if (!clean) {
            if (!outp) {
                fprintf(stderr, "Error: self-check could not read back '%s' "
                        "(empty or unreadable output), removing it\n", out_path);
            } else if (check_rc == 2) {
                fprintf(stderr, "Error: self-check failed for '%s' "
                        "(re-walk error), removing it\n", out_path);
            } else if (check_st.njunk != 0) {
                fprintf(stderr, "Error: self-check failed for '%s' "
                        "(junk bytes remained in output), removing it\n", out_path);
            } else {
                fprintf(stderr, "Error: self-check failed for '%s' (CRC-bad "
                        "frame count changed: wrote %" PRIu64 ", re-walk found "
                        "%" PRIu64 "), removing it\n",
                        out_path, st.ncrc, check_st.ncrc);
            }
            unlink(out_path);
            free(check_st.junk);
            free(check_st.crcbad);
            free(st.junk);
            free(st.crcbad);
            return 2;
        }

        free(check_st.junk);
        free(check_st.crcbad);

        free(st.junk);
        free(st.crcbad);
        return rc;
    }

    if (rc == 2) {
        fprintf(stderr, "Error: internal walk failure\n");
        free(st.junk);
        free(st.crcbad);
        return 2;
    }

    print_report(ops, &st);

    free(st.junk);
    free(st.crcbad);
    return rc;
}
