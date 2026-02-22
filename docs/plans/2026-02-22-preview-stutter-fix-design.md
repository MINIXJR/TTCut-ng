# Preview Stutter Fix & Preset Override Bugfix

**Date:** 2026-02-22
**Status:** Approved

## Problem

1. **Preview stutter**: Every preview clip shows a brief freeze (~100-200ms) at ~1-2 seconds.
   The final cut does not have this issue. Root cause: preview clip start positions land on
   arbitrary I-frames which may be non-IDR. Smart Cut detects non-IDR I-frames and re-encodes
   the first GOP, causing a decoder stall at the re-encode/stream-copy boundary (EOS NAL flush).

2. **Missing preset override**: `regeneratePreviewClip()` in `ttcutpreview.cpp` creates a new
   `TTESSmartCut` instance without calling `setPresetOverride(TTCut::previewPreset)`, so
   burst-shift regeneration uses the slow final cut preset instead of ultrafast.

## Analysis

### Preview Cut List Creation (ttcutpreviewtask.cpp:418-460)

For each cut point, `createPreviewCutList()` creates two segments:
- **CutIn half**: starts at `cutItem.cutInIndex()` (always an I-frame) → no problem
- **CutOut half**: starts at `cutOutIndex - previewFrames`, searched backward to I-frame (line 452-456)

The backward search uses `frameType != 1` which finds any I-frame (IDR or non-IDR).
For non-IDR I-frames, `TTESSmartCut::analyzeCutPoints()` sets `needsReencodeAtStart = true`,
causing re-encoding of the first GOP → EOS NAL at boundary → decoder flush → visible freeze.

### Why final cut is unaffected

CutIn positions in the main cut list are always IDR keyframes (enforced by the UI).
No re-encoding at segment start → no EOS NAL boundary → no stutter.

## Design

### Fix 1: Prefer IDR frames in preview cut list

**File:** `data/ttcutpreviewtask.cpp`, `createPreviewCutList()`

Change the backward I-frame search (lines 452-456) to prefer IDR frames:
1. First pass: search backward for IDR frame (NAL type 5 for H.264, NAL type 19/20 for HEVC)
2. If no IDR found within search range, fall back to any I-frame (existing behavior)
3. Smart Cut's existing non-IDR re-encode logic handles the fallback case

**Requires:** A method on `TTVideoStream` (or subclasses) to check if a frame is IDR.
The H.264/H.265 video header lists already store NAL unit types. Need to expose
`isIDRFrame(int index)` or similar.

### Fix 2: Settings hint

**File:** UI settings for Preview (ttcutsettingscommon or equivalent)

Add info label near "Preview seconds" setting:
"Die Vorschau beginnt für jeden Schnitt bei einem I-Frame."

### Fix 3: Preset override in regeneratePreviewClip()

**File:** `gui/ttcutpreview.cpp`, `regeneratePreviewClip()` (line 539)

Add `smartCut.setPresetOverride(TTCut::previewPreset);` after creating the TTESSmartCut instance.

## Known Limitations (documented in TODO.md)

- Multi-frame audio bursts at cut boundaries (detected but only single-frame shift offered)
- Isolated burst frames in silence regions between segments (not detected by edge-based algorithm)
