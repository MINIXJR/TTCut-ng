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
total_segments=142
defective_segments=5
removed_frames=127
total_frames_before=83862
total_frames_after=83735
```

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
  ├── Count frames per segment:
  │   MPEG-2: Picture Start Code (0x00000100)
  │   H.264:  NAL Type 1 (Non-IDR Slice) or Type 5 (IDR Slice)
  │   H.265:  NAL Types 0-21 (VCL NAL units)
  └── Result: Array of Segment structs {offset, size, frame_count}

Phase 2: Decode-Test per Segment
  ├── Initialize libavcodec decoder (once)
  ├── For each segment:
  │   ├── Create Custom AVIOContext on mmap region [offset, offset+size]
  │   ├── avformat_open_input() with forced format (mpegvideo/h264/h265)
  │   ├── avcodec_flush_buffers() (decoder reset between segments)
  │   ├── Loop: av_read_frame() → avcodec_send_packet() → avcodec_receive_frame()
  │   │   └── Count errors (ret < 0 from receive_frame, excluding EAGAIN)
  │   ├── avformat_close_input() (Custom IO context only, not decoder)
  │   └── Mark defective if error_count > threshold
  └── Result: Each segment marked valid/defective

Phase 3: Output
  ├── If no defective segments → exit 0, no file written
  ├── If all segments defective → exit 2, error message, no file written
  ├── Open <input>.repair.tmp
  ├── Write valid segments (memcpy from mmap)
  ├── fsync() + rename() over original
  └── Print statistics to stdout
```

### Segment Boundary Detection

| Codec  | Boundary Marker                | Rationale                                      |
|--------|-------------------------------|-------------------------------------------------|
| MPEG-2 | Sequence Header (0xB3)        | Always precedes GOP                             |
| H.264  | SPS NAL (Type 7) before Slice | Works for IDR and Non-IDR I-frame streams       |
| H.265  | SPS NAL (Type 33) before Slice| Works for IDR and Non-IDR I-frame streams       |

Note: H.264/H.265 streams without IDR frames (only Non-IDR I-slices) are common in DVB
recordings. Using SPS as boundary marker handles both cases uniformly. This aligns with
the Non-IDR I-Frame fix from TTCut-ng v0.58.0.

### Codec Auto-Detection

| Extension          | Codec        |
|--------------------|--------------|
| `.m2v`             | mpeg2video   |
| `.264`, `.h264`    | h264         |
| `.265`, `.h265`    | h265         |

Override via `--codec` parameter.

### Error Threshold

| Codec  | Default | Rationale                                          |
|--------|---------|----------------------------------------------------|
| MPEG-2 | 0       | Any decode error = defective (reliable detection)  |
| H.264  | 3       | Filters false positives from strict error checking |
| H.265  | 3       | Same rationale as H.264                            |

Configurable via `--threshold N`.

## File Structure

```
tools/
├── ttcut-esrepair.c          # Single-file implementation (~400-500 lines)
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
- POSIX: mmap(), open(), write(), rename(), fsync()

## Code Structure

```c
// --- Data structures ---
typedef struct {
    int64_t offset;       // Byte position in mmap
    int64_t size;         // Segment size in bytes
    int     frame_count;  // Number of frames in segment
    int     error_count;  // Decode errors found
    bool    defective;    // Assessment result
} Segment;

// --- 1. Start-Code Scanner ---
// Scans mmap'd data for segment boundaries, builds segment list
static int scan_segments(const uint8_t *data, int64_t size,
                         int codec, Segment **out, int *count);

// --- 2. Decode Tester ---
// Tests each segment via libavcodec, sets error_count and defective flag
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
        step "Repaired: removed $REMOVED_SEGS defective segments ($REMOVED_FRAMES frames)"
    elif [ $REPAIR_EXIT -eq 2 ]; then
        warn "ES repair failed — file may contain defective GOPs"
    fi
fi
```

The existing `ffmpeg -err_detect` decode check in ttcut-demux can be replaced by
`ttcut-esrepair --check-only` when the tool is available.

## Error Handling

| Scenario                    | Behavior                                              |
|-----------------------------|-------------------------------------------------------|
| No defective segments       | Exit 0, file untouched, no tmp file created           |
| All segments defective      | Exit 2, error message, file untouched                 |
| First segment defective     | Remove it; next valid segment starts with SPS/SeqHdr  |
| Consecutive defective segs  | All removed, frame count gap reported                 |
| File too large for mmap     | mmap() error → exit 2 with message                    |
| Write error on tmp file     | Delete tmp file → exit 2, original untouched          |
| Unknown codec / bad file    | Exit 2 with descriptive error                         |

## Performance Estimate

- MPEG-2 SD (720x576, ~56 min): ~3-5 seconds (mmap scan + decode-test)
- H.264 HD (1920x1080, ~90 min): ~10-20 seconds
- H.265 HD: ~15-30 seconds (heavier decoder)

The mmap scan itself is near-instant. The decode-test dominates runtime.
