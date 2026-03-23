# ttcut-esrepair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone C tool that detects and removes defective segments from MPEG-2/H.264/H.265 elementary streams, fixing A/V desync caused by garbage frames.

**Architecture:** mmap-based start-code scanner finds segment boundaries, libavcodec decode-tests each segment, valid segments are stream-copied to output, defective segments are dropped. Single C file, no Qt dependency.

**Tech Stack:** C17, libavcodec, libavformat, libavutil, POSIX mmap

**Spec:** `docs/superpowers/specs/2026-03-23-ttcut-esrepair-design.md`

**Test file (MPEG-2 with defective GOPs):** `/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `tools/ttcut-esrepair.c` | Create | Complete tool implementation |
| `tools/ttcut-esrepair.pro` | Create | qmake build file |

## Implementation Notes

- **Makefile conflict**: The tools/ directory has an existing Makefile from ttcut-strip-filler.pro. Use `qmake -o Makefile.esrepair ttcut-esrepair.pro && make -f Makefile.esrepair` to avoid overwriting it.
- **Partial writes**: `write()` may not write the full buffer on large segments. Use a `write_all()` helper that loops on partial writes.
- **madvise**: Call `madvise(data, size, MADV_SEQUENTIAL)` after mmap for optimal page fault behavior on multi-GB files.
- **Pre-boundary data**: Any data before the first segment boundary (SPS/Sequence Header) is discarded. This is correct for a repair tool — partial GOPs at file start are not independently decodable.
- **0 segments**: If the scanner finds 0 segments, exit 2 with error "No valid segments found".
- **const-correctness**: Use `const uint8_t*` for mmap data throughout.
- **check-only exit codes**: Exit 0 if no defects found, exit 1 if defects found.
- **ttcut-demux integration**: Out of scope for this plan. Will be a separate task after the tool is verified.

---

### Task 1: Build system and program skeleton

**Files:**
- Create: `tools/ttcut-esrepair.pro`
- Create: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Create qmake project file**

Create `tools/ttcut-esrepair.pro`:
```makefile
TARGET = ttcut-esrepair
CONFIG += console
CONFIG -= qt app_bundle
QMAKE_CFLAGS += -std=c17 -Wall -Wextra
PKGCONFIG += libavcodec libavformat libavutil
SOURCES = ttcut-esrepair.c
```

- [ ] **Step 2: Create program skeleton with argument parsing and help**

Create `tools/ttcut-esrepair.c` with:
- Includes: stdio, stdlib, string, stdint, stdbool, getopt
- `enum CodecType { CODEC_MPEG2, CODEC_H264, CODEC_H265 }`
- `typedef struct { ... } Segment;` and `typedef struct { ... } MmapIOContext;`
- `detect_codec(const char *filename)` — auto-detect from file extension
- `print_usage(const char *prog)` — help text matching spec CLI interface
- `main()` — parse args with getopt_long: `--check-only`, `--codec`, `--threshold`, `--log`, `--verbose`, `--help`
- After parsing: print parsed options to stderr if verbose, then `fprintf(stderr, "Not yet implemented\n"); return 2;`

- [ ] **Step 3: Build and verify**

```bash
cd /usr/local/src/TTCut-ng/tools && qmake -o Makefile.esrepair ttcut-esrepair.pro && make -f Makefile.esrepair
./ttcut-esrepair --help
./ttcut-esrepair --verbose --codec mpeg2 /dev/null
```
Expected: help text prints correctly, verbose run shows parsed options then "Not yet implemented".

- [ ] **Step 4: Commit**

```bash
git add tools/ttcut-esrepair.pro tools/ttcut-esrepair.c
git commit -m "Add ttcut-esrepair skeleton with CLI argument parsing"
```

---

### Task 2: mmap file handling

**Files:**
- Modify: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Add mmap open/close functions**

Add includes: `sys/mman.h`, `sys/stat.h`, `fcntl.h`, `unistd.h`, `errno.h`

```c
static int open_mmap(const char *path, const uint8_t **data, int64_t *size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size == 0) {
        fprintf(stderr, "Error: cannot stat '%s': %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    *size = st.st_size;
    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  // fd can be closed after mmap
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
```

- [ ] **Step 2: Wire into main(), verify mmap works**

Replace the "Not yet implemented" block in main() with:
```c
const uint8_t *data = NULL;
int64_t file_size = 0;
if (open_mmap(input_file, &data, &file_size) < 0)
    return 2;
if (verbose)
    fprintf(stderr, "Mapped %s: %lld bytes\n", input_file, (long long)file_size);
close_mmap(data, file_size);
return 0;
```

- [ ] **Step 3: Build and test with real file**

```bash
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair
./ttcut-esrepair --verbose "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v"
```
Expected: prints mapped size (~1.2 GB), exits 0.

- [ ] **Step 4: Commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Add mmap file handling to ttcut-esrepair"
```

---

### Task 3: Start-code scanner

**Files:**
- Modify: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Implement scan_segments()**

```c
static int scan_segments(const uint8_t *data, int64_t size, int codec,
                         Segment **out, int *count)
```

Algorithm:
- Scan `data[0..size-3]` for start code prefix `0x00 0x00 0x01`
- On each start code, check the type byte `data[i+3]`:
  - MPEG-2: `0xB3` = segment boundary, `0x00` = frame (picture_start_code)
  - H.264: `data[i+3] & 0x1F` — type 7 = SPS = boundary, type 1 or 5 = frame
  - H.265: `(data[i+3] >> 1) & 0x3F` — type 33 = SPS = boundary, types 0-21 = frame
- When a new boundary is found, finalize the previous segment (calculate size, store frame_count)
- Use dynamic array with realloc (initial capacity 256, double when full)
- After loop: finalize last segment (size extends to end of file)
- Return 0 on success, -1 on error (malloc failure)

- [ ] **Step 2: Add verbose segment summary in main()**

After `scan_segments()`, print summary:
```c
if (verbose)
    fprintf(stderr, "Found %d segments, %d total frames\n", seg_count, total_frames);
```

- [ ] **Step 3: Build and test with MPEG-2 file**

```bash
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair
./ttcut-esrepair --verbose "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v"
```
Expected: reports number of segments (should be in the hundreds for a 56-min file with GOPs every ~0.5s).

- [ ] **Step 4: Commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Add start-code scanner for MPEG-2/H.264/H.265 segment detection"
```

---

### Task 4: Decode tester with Custom AVIOContext

**Files:**
- Modify: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Add libav includes and Custom AVIOContext callbacks**

Add includes:
```c
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
```

Implement `mmap_read_packet()`:
```c
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
```

Implement `mmap_seek()`:
```c
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
```

- [ ] **Step 2: Implement Custom AVIOContext helper functions**

```c
// Create AVIOContext + AVFormatContext on mmap region, return via out params
// Returns 0 on success, -1 on error
static int open_segment_io(const uint8_t *file_data, const Segment *seg,
                           int codec, MmapIOContext *io_ctx,
                           AVIOContext **avio_out, AVFormatContext **fmt_out);

// Close segment IO (respects AVFMT_FLAG_CUSTOM_IO — frees avio buffer + context manually)
static void close_segment_io(AVIOContext *avio, AVFormatContext *fmt);
```

`open_segment_io()` lifecycle:
1. Set up `io_ctx` with `base = file_data + seg->offset`, `size = seg->size`, `pos = 0`
2. Allocate AVIOContext buffer (32768 bytes), create `avio_alloc_context()` with `mmap_read_packet` + `mmap_seek`
3. `avformat_alloc_context()`, set `pb = avio_ctx`, set `flags |= AVFMT_FLAG_CUSTOM_IO`
4. `avformat_open_input()` with `av_find_input_format(format_name)` (`"mpegvideo"` / `"h264"` / `"hevc"`)

`close_segment_io()` cleanup:
1. `avformat_close_input()` (frees format context, NOT avio due to CUSTOM_IO flag)
2. `av_freep(&avio->buffer)`, `avio_context_free(&avio)`

- [ ] **Step 3: Implement test_segment() using helpers**

```c
static int test_segment(const uint8_t *file_data, const Segment *seg, int codec)
```

1. Call `open_segment_io()`, return -1 on failure
2. Find video stream via `av_find_best_stream()`
3. Create decoder: `avcodec_alloc_context3()` + `avcodec_parameters_to_context()` + `avcodec_open2()`
4. Decode loop: `av_read_frame()` → `avcodec_send_packet()` → drain `avcodec_receive_frame()` — count errors (exclude EAGAIN and EOF)
5. Flush: `avcodec_send_packet(NULL)` → drain loop
6. Cleanup: `avcodec_free_context()`, `close_segment_io()`
7. Return error_count

- [ ] **Step 3: Implement test_segments() that loops over all segments**

```c
static int test_segments(const uint8_t *data, Segment *segs, int count,
                         int codec, int threshold, int verbose)
```

Loop over segments, call `test_segment()`, set `seg->error_count` and `seg->defective = (error_count >= threshold)`. With verbose, print per-segment line to stderr per spec format.

- [ ] **Step 4: Wire into main() and test**

After `scan_segments()`, call `test_segments()`. Print defective count.

```bash
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair
./ttcut-esrepair --verbose "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v"
```
Expected: segments are tested, defective ones are identified (should match the ~7 error regions from the decode check).

- [ ] **Step 5: Commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Add per-segment decode testing with Custom AVIOContext"
```

---

### Task 5: Writer and statistics output

**Files:**
- Modify: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Implement write_repaired()**

```c
static int write_repaired(const char *path, const uint8_t *data,
                          Segment *segs, int count)
```

- Add `write_all()` helper that loops on partial writes:
  ```c
  static int write_all(int fd, const void *buf, size_t count)
  ```
- Build tmp path: `snprintf(tmp_path, sizeof(tmp_path), "%s.repair.tmp", path)`
- Open tmp file with `open(O_WRONLY | O_CREAT | O_TRUNC, 0644)`
- Loop: for each segment where `!seg->defective`, call `write_all(fd, data + seg->offset, seg->size)`
- Check return for errors, `unlink(tmp_path)` on failure
- `fsync(fd)`, `close(fd)`, `rename(tmp_path, path)`
- Return 0 on success, -1 on error

- [ ] **Step 2: Implement statistics output and wire into main()**

In main(), after test_segments:
- Count defective segments and frames
- If `check_only`: print stats to stdout, exit 0 or 1
- If not check_only and defective > 0: call `write_repaired()`, print stats, exit 1
- If no defective: print stats, exit 0
- If all defective: print error, exit 2

Stdout format per spec:
```c
printf("codec=%s\n", codec_name);
printf("repaired=%d\n", !check_only && defective_count > 0);
printf("total_segments=%d\n", seg_count);
printf("defective_segments=%d\n", defective_count);
printf("removed_frames=%d\n", removed_frames);
printf("total_frames_before=%d\n", total_frames);
printf("total_frames_after=%d\n", total_frames - removed_frames);
```

- [ ] **Step 3: Build and test repair on MPEG-2 file**

First make a copy to avoid modifying the original:
```bash
mkdir -p /usr/local/src/CLAUDE_TMP/TTCut-ng
cp "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
   /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair.m2v
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair

# Check-only first
./ttcut-esrepair --check-only --verbose /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair.m2v

# Then repair
./ttcut-esrepair --verbose /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair.m2v
```
Expected: check-only reports defective segments without modifying file. Repair modifies file, reports removed frames. File size should decrease.

- [ ] **Step 4: Verify repaired file is valid**

```bash
# Frame count comparison
ffprobe -v error -count_frames -show_entries stream=nb_read_frames -of csv=p=0 \
    /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair.m2v

# Decode error check (should be 0 or near-0)
ffmpeg -v error -err_detect +careful -i /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair.m2v \
    -f null - 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Add repair writer and statistics output to ttcut-esrepair"
```

---

### Task 6: Log file support

**Files:**
- Modify: `tools/ttcut-esrepair.c`

- [ ] **Step 1: Add log file handling**

- Add global `FILE *log_fp = NULL;`
- In main(): if `--log` specified, `log_fp = fopen(log_path, "w")`
- Add helper: `static void log_msg(const char *fmt, ...)` — writes to log_fp if open
- In `test_segments()`: log each segment result with detail (offset, size, frame count, error count, verdict)
- In `write_repaired()`: log each written/skipped segment
- At end of main(): log summary, fclose

- [ ] **Step 2: Build and test**

```bash
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair
cp "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
   /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair2.m2v
./ttcut-esrepair --log /usr/local/src/CLAUDE_TMP/TTCut-ng/repair.log --verbose \
    /usr/local/src/CLAUDE_TMP/TTCut-ng/test_repair2.m2v
head -30 /usr/local/src/CLAUDE_TMP/TTCut-ng/repair.log
```
Expected: log file contains per-segment details.

- [ ] **Step 3: Commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Add log file support to ttcut-esrepair"
```

---

### Task 7: Test with H.264 Non-IDR stream

**Files:**
- No code changes expected (validation task)

- [ ] **Step 1: Extract H.264 ES from test file and run check**

```bash
cd /usr/local/src/TTCut-ng/tools
ffmpeg -v error -i "/media/Daten/Video_Tmp/temp/Test-Schnitt-TTCut-ng/Inspektor_Clouseau_-_Der_irre_Flic_mit_dem_heißen_Blick_(1978)/2022-10-25.22.15.16-0.rec/00001.ts" \
    -map 0:v -c copy -bsf:v h264_mp4toannexb -t 30 /usr/local/src/CLAUDE_TMP/TTCut-ng/test_h264.264 -y
./ttcut-esrepair --check-only --verbose --codec h264 /usr/local/src/CLAUDE_TMP/TTCut-ng/test_h264.264
```
Expected: segments detected via SPS boundary, decode test runs without crashes, no false-positive defective segments (clean stream should have 0 defective).

- [ ] **Step 2: If issues found, fix and rebuild**

Address any H.264-specific issues (SPS detection, NAL type parsing for HEVC type extraction, threshold behavior).

- [ ] **Step 3: Commit if changes were needed**

```bash
git add tools/ttcut-esrepair.c
git commit -m "Fix H.264/H.265 segment detection in ttcut-esrepair"
```

---

### Task 8: Final integration test and cleanup

**Files:**
- Modify: `tools/ttcut-esrepair.c` (cleanup only)

- [ ] **Step 1: Full end-to-end test with MPEG-2**

```bash
# Fresh copy
cp "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
   /usr/local/src/CLAUDE_TMP/TTCut-ng/test_e2e.m2v

# Repair
./ttcut-esrepair --verbose --log /usr/local/src/CLAUDE_TMP/TTCut-ng/e2e.log \
    /usr/local/src/CLAUDE_TMP/TTCut-ng/test_e2e.m2v

# Verify: load in TTCut-ng should show correct frame count
# Verify: repaired file has 0 decode errors
ffmpeg -v error -err_detect +careful+explode -i /usr/local/src/CLAUDE_TMP/TTCut-ng/test_e2e.m2v \
    -f null - 2>&1 | wc -l
```
Expected: 0 decode errors after repair.

- [ ] **Step 2: Test edge cases**

```bash
# Truly empty file (0 bytes)
: > /usr/local/src/CLAUDE_TMP/TTCut-ng/empty.m2v
./ttcut-esrepair /usr/local/src/CLAUDE_TMP/TTCut-ng/empty.m2v
# Expected: exit 2 (empty file)

# Tiny non-ES file (no valid segments)
echo "garbage" > /usr/local/src/CLAUDE_TMP/TTCut-ng/tiny.m2v
./ttcut-esrepair /usr/local/src/CLAUDE_TMP/TTCut-ng/tiny.m2v
# Expected: exit 2 (no valid segments found)

# Clean file (no defects)
ffmpeg -v error -i "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
    -t 5 -c:v mpeg2video -q:v 2 /usr/local/src/CLAUDE_TMP/TTCut-ng/clean.m2v -y 2>/dev/null
./ttcut-esrepair --verbose /usr/local/src/CLAUDE_TMP/TTCut-ng/clean.m2v
# Expected: exit 0, file untouched

# Unknown extension
./ttcut-esrepair /usr/local/src/CLAUDE_TMP/TTCut-ng/empty.m2v
# Expected: exit 2 with error (0 segments or bad file)
```

- [ ] **Step 3: Code cleanup**

Remove any debug prints, verify all error paths free resources, check for compiler warnings:
```bash
cd /usr/local/src/TTCut-ng/tools && make -f Makefile.esrepair clean && make 2>&1 | grep -i warn
```

- [ ] **Step 4: Final commit**

```bash
git add tools/ttcut-esrepair.c
git commit -m "ttcut-esrepair: final cleanup and edge case handling"
```
