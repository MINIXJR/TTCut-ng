# PAFF Stutter Fix: SPS Harmonization at Re-Encode/Stream-Copy Boundary

**Date:** 2026-03-31
**Status:** Approved
**Priority:** Critical (blocks feature/paff-support merge)
**Branch:** feature/paff-support

## Problem

H.264 PAFF streams stutter at cut points in preview and final output. The re-encoded frames (x264, MBAFF) and stream-copied frames (source, PAFF) have incompatible SPS parameters, causing decoder DPB management failures at the transition.

**Evidence:**
- 1228 video packets → only 587 decoded frames (expected: 626, 39 lost)
- `mmco: unref short failure` at transition
- `number of reference frames (0+5) exceeds max (4)` → DPB overflow
- PTS assignment verified correct via ffprobe — problem is content, not timing

## Root Cause

Three SPS parameter mismatches at the re-encode → stream-copy boundary:

| Parameter | x264 Encoder SPS | Source PAFF SPS | Impact |
|---|---|---|---|
| `mb_adaptive_frame_field_flag` | 1 (MBAFF) | 0 (PAFF) | Decoder DPB mode switch |
| `log2_max_frame_num_minus4` | 0 (4 bits) | 5 (9 bits) | frame_num bit width changes |
| Slice structure | 1 NAL/frame (MBAFF) | 2 NALs/frame (PAFF fields) | Decoder field pairing changes |

The EOS NAL before stream-copy flushes the DPB, but the SPS parameter change causes the decoder to mismanage references when rebuilding the DPB with the new parameters.

## Background

### Why x264 produces MBAFF, not PAFF

Confirmed from `x264.h` line 758:
> `"TOP" and "BOTTOM" are not supported in x264 (PAFF only)"`

x264 with `AV_CODEC_FLAG_INTERLACED_DCT` produces MBAFF (`mb_adaptive_frame_field_flag=1`). True field-based PAFF encoding is not implemented. The source DVB stream uses a hardware broadcast encoder (no SEI identification, typical for ProSiebenSat.1/kabel eins headend equipment).

### H.264 Spec: MBAFF is a superset of PAFF

When `mb_adaptive_frame_field_flag=1`, the decoder allows both:
- Frame-coded macroblocks (as x264 MBAFF produces)
- Field-coded pictures via `field_pic_flag=1` in slice headers (as PAFF source has)

Setting `mb_adaptive_frame_field_flag=1` on the source SPS makes PAFF slices valid within the MBAFF signaling. The decoder doesn't need to switch modes.

## Design

### Approach: Patch source SPS to MBAFF signaling

Patch `mb_adaptive_frame_field_flag` from 0 to 1 in the source SPS before it's written to the output ES. This eliminates the MBAFF↔PAFF mode switch at the transition.

### Implementation

**Single function to modify:** `patchH264SpsReorderFrames()` in `ttessmartcut.cpp`

This function already parses through the entire SPS to reach `bitstream_restriction` at the end. The `mb_adaptive_frame_field_flag` bit is at a known position (immediately after `frame_mbs_only_flag=0`). The function must:

1. While parsing, record the bit position of `mb_adaptive_frame_field_flag`
2. If `frame_mbs_only_flag == 0` and `mb_adaptive_frame_field_flag == 0`: set it to 1
3. This is a single-bit change at a fixed position — no RBSP rebuild needed (unlike Exp-Golomb value changes which shift all subsequent bits)

**Affected call sites (all already covered):**
- `writeParameterSets()` → patches SPS before stream-copy section
- `patchSpsNalsInAccessUnit()` → patches inline SPS NALs in stream-copied AUs
- Both call `patchH264SpsReorderFrames()` internally

**No new call sites needed.** The existing patching infrastructure handles both the transition SPS and all inline SPS NALs.

### What about log2_max_frame_num mismatch?

The `log2_max_frame_num_minus4` difference (encoder: 0, source: 5) is already handled by the existing frame_num delta patching in `streamCopyFrames()`. The source SPS declares 9-bit frame_num, and stream-copied slices use 9-bit frame_num values. The encoder SPS declares 4-bit frame_num for re-encoded slices. Since EOS+SPS is written between sections, each section has its own consistent SPS. No additional patching needed.

### Not affected

- Progressive H.264: `frame_mbs_only_flag=1` → no `mb_adaptive_frame_field_flag` present → patch not applicable
- MBAFF H.264: Already has `mb_adaptive_frame_field_flag=1` → patch is no-op
- H.265/HEVC: Different interlace mechanism, not affected
- MPEG-2: No H.264 SPS, not affected
- MKV muxer PTS logic: Already correct (per-packet field detection)
- Frame counting/navigation: Already fixed in prior commits

## Testing

1. **Moon_Crash preview**: No stutter at cut points, no repeated/dropped frames
2. **ffprobe frame count**: `ffprobe -count_frames` on output MKV should show expected frame count (no lost frames)
3. **No mmco errors**: `ffprobe -v error` should show no `mmco: unref short failure` at cut boundaries
4. **Progressive H.264 regression**: Identical behavior, no SPS modification
5. **MBAFF H.264 regression**: If available, verify no-op behavior
