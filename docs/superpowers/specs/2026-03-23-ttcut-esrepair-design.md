# ttcut-esrepair — Design Spec

## Problem

Defective GOPs in MPEG-2 elementary streams (from DVB recordings with signal issues) contain
garbage frames that inflate the video frame count. When TTCut-ng cuts video and calculates
audio positions via `frame_index / fps`, the result is wrong — audio is cut from a position
that doesn't match the video content. This causes up to 5 seconds of A/V desync in preview
and final cut output.

The same issue can theoretically occur in H.264/H.265 streams, though it's less common.

ProjectX handles this for MPEG-2 by detecting and removing defective GOPs during demuxing.
ttcut-demux currently has no equivalent capability.

## Solution

A standalone C tool `ttcut-esrepair` that:

1. Parses MPEG-2/H.264/H.265 elementary streams to find segment boundaries
2. Decode-tests each segment with libavcodec
3. Removes defective segments via stream-copy of valid segments
4. Reports repair statistics for integration with ttcut-demux's .info file

## CLI Interface

```
ttcut-esrepair [OPTIONS] <input-file>

Options:
  --check-only         Report only, do not modify file
  --codec <type>       Codec override (mpeg2|h264|h265). Default: auto from extension
  --threshold <N>      Error threshold per segment (default: 0 for MPEG-2, 3 for H.264/H.265)
  --log <file>         Detailed log file
  -v, --verbose        Verbose output on stderr
  -h, --help           Usage help
```

### Exit Codes

- `0` — No defective segments found
- `1` — Repair performed (or defects found with --check-only)
- `2` — Fatal error (file unreadable, unknown codec, all segments defective, etc.)

### Stdout (machine-readable)

```
codec=mpeg2video
repaired=1
total_segments=142
defective_segments=5
removed_frames=127
total_frames_before=83862
total_frames_after=83735
```

Note: Frame counts are approximations. Multi-slice H.264/H.265 frames (multiple VCL NALUs
per frame) may be over-counted. Exact frame counts are verified downstream by TTCut-ng's
own parser after loading the repaired ES.

### In-Place Repair

The tool writes to `<input>.repair.tmp`. On success, `rename()` atomically replaces the
original. The original file is never opened for writing — `rename()` is the only moment
it's affected. On any error or with `--check-only`, the original remains untouched.

## Architecture

### Approach: Own Start-Code Parser + libavcodec Decode-Test

- mmap-based file I/O for zero-copy scanning and segment extraction
- Custom start-code scanner finds segment boundaries
- libavcodec validates each segment via decode-test
- Valid segments are written to output via `write()` from mmap

This was chosen over pure libavformat (unreliable for raw ES, no GOP boundary control)
and over hybrid approaches (mixing I/O paths adds complexity without benefit).

## Algorithm

```
Phase 1: mmap + Start-Code Scan
  ├── mmap() input file (read-only)
  ├── Sequential scan for 0x00 0x00 0x01 start codes
  ├── Identify segment boundaries by codec:
  │   MPEG-2: Sequence Header (0x000001B3)
  │   H.264:  SPS NAL (Type 7) — works for both IDR and Non-IDR I-frame streams
  │   H.265:  SPS NAL (Type 33) — works for both IDR and Non-IDR I-frame streams
  ├── Count frames per segment (approximate, see note below):
  │   MPEG-2: Picture Start Code (0x00000100)
  │   H.264:  NAL Type 1 (Non-IDR Slice) or Type 5 (IDR Slice)
  │   H.265:  NAL Types 0-21 (VCL NAL units)
  └── Result: Array of Segment structs {offset, size, frame_count}

Phase 2: Decode-Test per Segment
  ├── For each segment:
  │   ├── Create AVCodecContext (new decoder per segment, see rationale below)
  │   ├── Create Custom AVIOContext on mmap region [offset, offset+size]
  │   ├── avformat_open_input() with forced format (mpegvideo/h264/h265)
  │   ├── Loop: av_read_frame() → avcodec_send_packet() → avcodec_receive_frame()
  │   │   └── Count errors (ret < 0, excluding EAGAIN and EOF)
  │   ├── Flush decoder: avcodec_send_packet(NULL) + drain loop
  │   ├── Cleanup: avformat_close_input(), avcodec_free_context()
  │   └── Mark defective if error_count >= threshold
  └── Result: Each segment marked valid/defective

Phase 3: Output
  ├── If no defective segments → exit 0, no file written
  ├── If all segments defective → exit 2, error message, no file written
  ├── Open <input>.repair.tmp
  ├── Write valid segments (write() from mmap — zero-copy via page cache)
  ├── fsync() + rename() over original
  └── Print statistics to stdout
```

### Decoder Per Segment Rationale

The decoder (`AVCodecContext`) is recreated for each segment rather than flushed with
`avcodec_flush_buffers()`. Reason: some decoders (notably `mpeg2video`) retain partial
state from corrupted frames after flush, causing false-positive errors in subsequent
segments. Recreating the decoder is sub-millisecond and eliminates this risk.

### Custom AVIOContext Implementation

Each segment is fed to libavformat via a custom `AVIOContext` that reads from the mmap
region. This avoids file I/O and enables zero-copy decode testing.

```c
// Opaque context for custom I/O
typedef struct {
    const uint8_t *base;  // Start of mmap region for this segment
    int64_t size;         // Segment size in bytes
    int64_t pos;          // Current read position within region
} MmapIOContext;

// read_packet callback: copy from mmap region, return AVERROR_EOF at end
static int mmap_read_packet(void *opaque, uint8_t *buf, int buf_size);

// seek callback: clamp to [0, size], return new position
static int64_t mmap_seek(void *opaque, int64_t offset, int whence);
```

**Lifecycle per segment:**
1. Allocate `AVIOContext` via `avio_alloc_context()` with `mmap_read_packet` and `mmap_seek`
2. Allocate `AVFormatContext`, set `ctx->pb` to custom `AVIOContext`
3. Set `ctx->flags |= AVFMT_FLAG_CUSTOM_IO` (prevents `avformat_close_input` from freeing pb)
4. `avformat_open_input()` with forced format
5. Decode-test loop
6. `avformat_close_input()` (frees format context, NOT the AVIOContext due to flag)
7. Free AVIOContext buffer and context manually: `av_freep(&avio_ctx->buffer); avio_context_free(&avio_ctx)`

### Frame Count Approximation

Frame counts are approximate because multi-slice encoding uses multiple VCL NALUs per frame.
For H.264, a precise count would require parsing `first_mb_in_slice` from each slice header.
This complexity is not justified since the counts are only used for statistics — TTCut-ng's
own parser produces exact counts when loading the repaired ES file.

### Segment Boundary Detection

| Codec  | Boundary Marker                | Rationale                                      |
|--------|-------------------------------|-------------------------------------------------|
| MPEG-2 | Sequence Header (0xB3)        | Always precedes GOP                             |
| H.264  | SPS NAL (Type 7) before Slice | Works for IDR and Non-IDR I-frame streams       |
| H.265  | SPS NAL (Type 33) before Slice| Works for IDR and Non-IDR I-frame streams       |

**Assumption**: SPS is repeated before each keyframe in the ES. This is guaranteed for
streams demuxed by ffmpeg/ttcut-demux. Streams from other sources may have fewer segments
(larger GOPs between SPS occurrences), which means coarser granularity for defect removal
but not incorrect behavior — larger segments just mean more frames removed per defect.

### Codec Auto-Detection

| Extension          | Codec        |
|--------------------|--------------|
| `.m2v`             | mpeg2video   |
| `.264`, `.h264`    | h264         |
| `.265`, `.h265`    | h265         |

Override via `--codec` parameter.

### Error Threshold

| Codec  | Default | Semantics                                          |
|--------|---------|----------------------------------------------------|
| MPEG-2 | 0       | Any decode error marks segment as defective        |
| H.264  | 3       | Segment defective if `error_count >= 3`            |
| H.265  | 3       | Segment defective if `error_count >= 3`            |

Configurable via `--threshold N`. The `>=` semantics means threshold=0 flags on the first
error, threshold=3 tolerates up to 2 errors per segment.

### Verbose Output

With `--verbose`, one line per segment on stderr:

```
Segment   1/142:   15 frames,    234567 bytes — OK
Segment   2/142:   15 frames,    198234 bytes — OK
Segment   3/142:   12 frames,    187654 bytes — DEFECTIVE (7 errors)
...
```

## File Structure

```
tools/
├── ttcut-esrepair.c          # Single-file implementation (~500-600 lines)
├── ttcut-esrepair.pro        # qmake build file
└── ... (existing tools)
```

### Build

```makefile
# ttcut-esrepair.pro
TARGET = ttcut-esrepair
CONFIG += console c17
CONFIG -= qt
PKGCONFIG += libavcodec libavformat libavutil
SOURCES = ttcut-esrepair.c
```

```bash
cd tools && qmake ttcut-esrepair.pro && make
```

### Dependencies

- libavcodec, libavformat, libavutil (already project dependencies)
- No Qt dependency
- POSIX: mmap(), munmap(), open(), write(), rename(), fsync()

## Code Structure

```c
// --- Data structures ---
typedef struct {
    int64_t offset;       // Byte position in mmap
    int64_t size;         // Segment size in bytes
    int     frame_count;  // Number of frames in segment (approximate)
    int     error_count;  // Decode errors found
    bool    defective;    // Assessment result
} Segment;

typedef struct {
    const uint8_t *base;  // Mmap region base for this segment
    int64_t size;         // Region size
    int64_t pos;          // Current read position
} MmapIOContext;

// --- 1. Start-Code Scanner ---
// Scans mmap'd data for segment boundaries, builds segment list
static int scan_segments(const uint8_t *data, int64_t size,
                         int codec, Segment **out, int *count);

// --- 2. Decode Tester ---
// Tests each segment via libavcodec (new decoder per segment),
// sets error_count and defective flag
static int test_segments(const uint8_t *data, Segment *segs,
                         int count, int codec, int threshold);

// --- 3. Writer ---
// Writes valid segments to tmp file, renames over original
static int write_repaired(const char *path, const uint8_t *data,
                          Segment *segs, int count);

// --- 4. main() ---
// Argument parsing, orchestration, stdout reporting
```

## ttcut-demux Integration

Called after video demuxing, before .info file generation:

```bash
if [ -x "$(command -v ttcut-esrepair)" ]; then
    REPAIR_OUTPUT=$(ttcut-esrepair "$OUTPUT_VIDEO" --log "$LOGDIR/esrepair.log")
    REPAIR_EXIT=$?
    if [ $REPAIR_EXIT -eq 1 ]; then
        REMOVED_FRAMES=$(echo "$REPAIR_OUTPUT" | grep removed_frames | cut -d= -f2)
        REMOVED_SEGS=$(echo "$REPAIR_OUTPUT" | grep defective_segments | cut -d= -f2)
        FRAMES_AFTER=$(echo "$REPAIR_OUTPUT" | grep total_frames_after | cut -d= -f2)
        step "Repaired: removed $REMOVED_SEGS defective segments ($REMOVED_FRAMES frames)"
    elif [ $REPAIR_EXIT -eq 2 ]; then
        warn "ES repair failed — file may contain defective GOPs"
    fi
fi
```

### .info File Fields

When repair is performed, ttcut-demux writes to the `[warnings]` section:

```ini
[warnings]
es_repaired=true
es_removed_segments=5
es_removed_frames=127
es_frames_before=83862
es_frames_after=83735
```

This replaces the current `decode_errors` / `recommend_projectx` fields, since the
defective segments have been removed. If `--check-only` is used (no repair), the existing
`decode_errors` fields remain.

The existing `ffmpeg -err_detect` decode check in ttcut-demux can be replaced by
`ttcut-esrepair --check-only` when the tool is available.

## Error Handling

| Scenario                    | Behavior                                              |
|-----------------------------|-------------------------------------------------------|
| No defective segments       | Exit 0, file untouched, no tmp file created           |
| All segments defective      | Exit 2, error message, file untouched                 |
| First segment defective     | Remove it; next valid segment starts with SPS/SeqHdr  |
| Consecutive defective segs  | All removed, frame count gap reported                 |
| mmap() fails (MAP_FAILED)   | Exit 2 with `strerror(errno)` for diagnostics         |
| Write error on tmp file     | Delete tmp file → exit 2, original untouched          |
| Unknown codec / bad file    | Exit 2 with descriptive error                         |

## Performance Estimate

- MPEG-2 SD (720x576, ~56 min): ~3-5 seconds (mmap scan + decode-test)
- H.264 HD (1920x1080, ~90 min): ~10-20 seconds
- H.265 HD: ~15-30 seconds (heavier decoder)

The mmap scan itself is near-instant. The decode-test dominates runtime.
Decoder recreation per segment adds negligible overhead (<1ms per segment).
