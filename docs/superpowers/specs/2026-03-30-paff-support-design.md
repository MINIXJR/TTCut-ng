# PAFF (Separated Fields) Support for H.264 Streams

**Date:** 2026-03-30
**Status:** Approved
**Priority:** Critical

## Problem

H.264 PAFF (Picture-Adaptive Frame-Field) interlaced streams store each field (top/bottom) as a separate Access Unit. TTCut-ng treats every AU as a distinct frame, causing:

- **Frame counting ~2x off**: 317,062 AUs in `Moon_Crash_(2022).264` but only ~158,531 displayed frames
- **Navigation wrong**: Seeking to frame N actually displays field N (different content)
- **Smart Cut misaligned**: CutIn/CutOut frame numbers in cut list don't match actual video content in preview and final output
- **Frame rate wrong**: `.info` file reports 50fps (field rate) instead of 25fps (frame rate)

**Evidence (debug log):**
- 181 input AUs -> 89 decoded frames (factor 2.03)
- Display-order mapping shifts CutIn by 38 AUs instead of ~19 frames
- Test file: `Moon_Crash_(2022).ttcut` / `Moon_Crash_(2022).264`

## Background

### H.264 PAFF Coding

In PAFF mode (`frame_mbs_only_flag == 0` in SPS), each frame can be coded as:
- A single frame picture (`field_pic_flag == 0`) — standard, one AU per frame
- Two separate field pictures (`field_pic_flag == 1`) — PAFF, two AUs per frame

Each field has its own slice header with `field_pic_flag == 1`. The top field has `bottom_field_flag == 0`, the bottom field has `bottom_field_flag == 1`. Both fields share the same `frame_num`.

The decoder (libavcodec) combines two field pictures into one output frame automatically.

### Affected Components

1. **TTNaluParser** — `buildAccessUnits()` counts each field as a separate AU
2. **TTFFmpegWrapper** — `buildFrameIndex()` creates one index entry per packet (field)
3. **TTESSmartCut** — display-order mapping uses AU indices but counts decoder frames
4. **ttcut-demux** — reports field rate (50fps) instead of frame rate (25fps)

## Design

### Principle: Fix at the Source

All changes are guarded by PAFF detection. Progressive streams and MBAFF streams (which use `field_pic_flag == 0`) are completely unaffected.

Double validation: PAFF is only activated when BOTH conditions are met:
1. SPS has `frame_mbs_only_flag == 0`
2. At least one slice actually has `field_pic_flag == 1`

### Component 1: TTNaluParser — Field-Pair Merging

#### New Struct: TTSpsInfo

Extracted from SPS NAL data, stored per SPS ID:

```cpp
struct TTSpsInfo {
    int spsId;
    int log2MaxFrameNumMinus4;    // needed to parse frame_num
    bool frameMbsOnlyFlag;        // false = may contain fields
    int picWidthInMbs;
    int picHeightInMapUnits;
};
```

Parsed in a new `parseH264SpsData()` method, called when SPS NAL units are encountered.

#### TTNalUnit Extensions

```cpp
struct TTNalUnit {
    // ... existing fields ...
    bool isField;          // field_pic_flag == 1
    bool isBottomField;    // bottom_field_flag == 1
};
```

#### parseH264SliceHeader() Extension

After parsing `ppsId` (current endpoint), continue parsing:
1. `frame_num` — read `log2MaxFrameNumMinus4 + 4` bits (from SPS via ppsId lookup)
2. If `frameMbsOnlyFlag == 0`: read `field_pic_flag` (1 bit)
3. If `field_pic_flag == 1`: read `bottom_field_flag` (1 bit)

**Note:** This requires resolving PPS -> SPS chain. The PPS ID is already parsed; we need a `QMap<int, int> mPpsToSps` mapping (PPS ID -> SPS ID) populated during PPS parsing.

#### TTAccessUnit Extensions

```cpp
struct TTAccessUnit {
    // ... existing fields ...
    bool isFieldCoded;     // true if AU contains two merged field pictures
};
```

#### buildAccessUnits() — Two-Pass Approach

**Pass 1:** Unchanged — build AUs as before (each field = separate AU).

**Pass 2:** Merge field pairs:
- Iterate through AUs sequentially
- If AU[i] has `isField == true` and `isBottomField == false` (top field), and AU[i+1] has `isField == true` and `isBottomField == true` (bottom field), and both share the same `frame_num`:
  - Merge AU[i+1]'s `nalIndices` into AU[i]
  - Update AU[i]'s `endOffset` to AU[i+1]'s `endOffset`
  - Set AU[i]'s `isFieldCoded = true`
  - Remove AU[i+1] from the list
  - Re-index all subsequent AUs

**Result:** `mAccessUnits.size()` equals the number of displayed frames.

#### Stream-Level Accessors

```cpp
bool isPAFF() const;                  // true if any AU is field-coded
double correctedFrameRate() const;    // field-rate / 2 if PAFF
```

### Component 2: TTFFmpegWrapper — Independent PAFF Detection

#### New Static Helper

```cpp
struct TTFieldInfo {
    bool isField;        // field_pic_flag
    bool isBottomField;  // bottom_field_flag
    int frameNum;        // frame_num from slice header
};

static TTFieldInfo parseH264FieldInfo(const uint8_t* data, int size, int log2MaxFrameNum);
```

Parses the same slice header fields as TTNaluParser but from AVPacket data.

#### SPS Extraction in buildFrameIndex()

When the first H.264 packet with `extradata` is encountered (or from `AVCodecParameters::extradata`), parse `log2_max_frame_num_minus4` and `frame_mbs_only_flag`. Store as member variables.

#### buildFrameIndex() — Field Merging

When `frame_mbs_only_flag == 0`:
- For each H.264 packet, call `parseH264FieldInfo()`
- If current packet is a top field and the next packet is a bottom field with the same `frame_num`:
  - Create one `TTFrameInfo` entry with:
    - `fileOffset` from top field
    - `packetSize` = sum of both packets
    - `isFieldCoded = true`
  - Read next packet immediately (consume the bottom field)
- Otherwise: create entry as before

#### TTFrameInfo Extension

```cpp
struct TTFrameInfo {
    // ... existing fields ...
    bool isFieldCoded;    // true if merged from two field packets
};
```

#### skipCurrentFrame() — PAFF Awareness

When `mIsPAFF`:
- Send 2 packets to the decoder before expecting 1 frame output
- This is needed because the current implementation sends 1 packet and expects 1 frame

#### Stream-Level

```cpp
bool mIsPAFF;                    // set in buildFrameIndex()
bool isPAFF() const;
double frameRate() const;        // corrected for PAFF (field-rate / 2)
```

### Component 3: TTESSmartCut — Minimal Changes

Since TTNaluParser now provides correct frame-indexed AUs, the Smart Cut engine needs only:

#### Encoder Flags for Interlaced Output

When `mParser.isPAFF()`:
```cpp
codecCtx->flags |= AV_CODEC_FLAG_INTERLACED_DCT;
// Set field order from source stream
codecCtx->field_order = AV_FIELD_TT;  // or AV_FIELD_BB based on source
```

This ensures re-encoded frames at cut boundaries maintain interlaced structure.

#### No Changes to Cut Logic

- `reencodeFrames()` display-order mapping: `displayOffset = startFrame - uiKeyframe` — after fix, both are frame indices (not field indices) -> correct
- `readAccessUnitData()`: merged AUs include both fields -> stream-copy works
- `processSegment()`: iterates by AU index -> frame index after fix -> correct
- `analyzeCutPoints()`: GOP detection uses keyframe AUs -> frame-indexed after fix -> correct

### Component 4: ttcut-demux — Frame Rate Correction

#### Detection

After determining frame rate via ffprobe, check for interlaced:
```bash
FIELD_ORDER=$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=field_order \
    -of default=noprint_wrappers=1:nokey=1 "$INPUT")

if [[ "$FIELD_ORDER" != "progressive" && "$FIELD_ORDER" != "unknown" && -n "$FIELD_ORDER" ]]; then
    # Interlaced: halve the frame rate
    FRAME_RATE_NUM=$((FRAME_RATE_NUM / 2))
    info "  Interlaced ($FIELD_ORDER): corrected frame rate to $FRAME_RATE_NUM/$FRAME_RATE_DEN"
fi
```

#### TTESInfo Fallback

In TTCut-ng, when loading `.info` file:
```cpp
void TTESInfo::correctFrameRateForPAFF() {
    if (mFrameRateNum > 30 && mFrameRateDen == 1) {
        // Likely field rate, not frame rate — will be confirmed by parser
        mFrameRateNum /= 2;
    }
}
```

Called only when TTNaluParser confirms PAFF. This handles old `.info` files with 50fps.

### Not Affected

- **Audio Cutting**: Uses timestamps/PTS, not frame indices
- **MKV Muxing**: `setDefaultDuration()` uses frame rate -> automatically correct after fps fix
- **Subtitle Cutting**: Time-based, not frame-index-based
- **Project File Format**: No migration needed (old PAFF projects had wrong cuts anyway)
- **VDR Marker Import**: markad frame numbers are decoder-frames -> match new AU indices
- **MPEG-2 Streams**: No PAFF concept, completely unaffected
- **H.265/HEVC Streams**: HEVC uses a different interlace mechanism (not PAFF), unaffected
- **Progressive H.264**: `frame_mbs_only_flag == 1` -> no field parsing -> no change
- **MBAFF H.264**: `field_pic_flag == 0` -> no field merging -> no change

### Safety: Double Validation

PAFF mode is only activated when both conditions are true:
1. **SPS level**: `frame_mbs_only_flag == 0`
2. **Slice level**: At least one slice has `field_pic_flag == 1`

If the SPS claims fields are possible but no slice actually uses them, PAFF is NOT activated. This prevents false positives from SPS parsing errors.

## Testing

### Test Matrix

| Stream Type | Expected Behavior |
|---|---|
| Progressive H.264 (e.g. typical DVB HD) | No change — `frame_mbs_only_flag == 1` |
| MBAFF H.264 | No change — `field_pic_flag == 0` |
| PAFF H.264 (Moon_Crash) | AU count halved, navigation correct, cuts correct |
| H.265/HEVC | No change — different interlace mechanism |
| MPEG-2 | No change — not processed by TTNaluParser |

### Verification Steps

1. **Moon_Crash_(2022).264**: Open, verify frame count is ~158k (not ~317k)
2. **Navigation**: Frame N shows same content as before at frame N/2 in old version
3. **Smart Cut**: CutIn in cut list matches actual first frame in preview
4. **Final Output**: MKV has correct frame rate (25fps) and correct content at cuts
5. **Progressive H.264**: Verify identical behavior (regression test)
6. **Frame Rate**: `.info` shows 25fps after re-demux; TTCut-ng corrects old 50fps `.info`

## Risks

### SPS Parsing Complexity

H.264 SPS parsing involves multiple Exp-Golomb coded fields and conditional sections (chroma_format_idc, scaling lists, etc.). Incorrect bit position after these sections would give wrong `frame_mbs_only_flag`.

**Mitigation:** The Exp-Golomb decoder already exists and is tested (`readExpGolombUE/SE`). The SPS structure up to `frame_mbs_only_flag` is well-documented. Double validation (SPS + actual slices) catches parsing errors.

### PPS-to-SPS Resolution

Parsing `field_pic_flag` requires knowing `frame_mbs_only_flag` from the SPS referenced by the PPS referenced by the slice. This is a two-step lookup.

**Mitigation:** PPS parsing is simple (first field is `pps_id`, second is `sps_id`). Both are Exp-Golomb coded. The lookup chain is straightforward.
