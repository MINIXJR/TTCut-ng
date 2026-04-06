# PAFF Display-Order Mapping Fix (Ansatz A1)

**Date:** 2026-04-01
**Branch:** feature/paff-support
**Status:** Design approved

## Problem

The display-order mapping in `reencodeFrames()` produces negative shifts for PAFF
(H.264 Separated Fields) streams. Example: `startFrame=84039` maps to `realStartAU=84036`
(shift: -3), causing 3 advertisement frames to appear at the cut boundary.

### Root Cause

The navigation decoder (FFmpegWrapper) and Smart Cut decoder (TTESSmartCut) receive
**different packet streams** for PAFF:

- **Navigation**: FFmpeg h264 raw demuxer sends 2 individual field packets per frame
- **Smart Cut**: TTNaluParser `readAccessUnitData()` sends 1 merged AU (both fields concatenated)

This causes the H.264 decoder to produce different display-order sequences. The existing
mapping counts display-order frames and reads their AU index (PTS), but because the
reorder sequence differs, it lands on the wrong AU.

For non-PAFF streams, both decoders see identical packet boundaries, so the mapping works
correctly.

### Symptoms

| Symptom | Cause |
|---|---|
| Advertisement frames visible at CutIn | Re-encode starts 3 AUs too early |
| Wrong frame display order in segment 2 | POC mismatch + DPB error at re-encode→stream-copy boundary |
| A/V desync after cut | Frame count / PTS base shifted by extra frames |
| CutOut widget shows wrong frame | Same mapping mismatch in reverse direction |

## Solution: Navigation-Conformant Frame Selection (A1)

Instead of mapping display-order positions back to AU indices, replicate the navigation's
exact method: count decoder outputs from the keyframe.

### Change in `reencodeFrames()` (~line 1562-1608, ttessmartcut.cpp)

Add a PAFF-specific code path before the existing display-order mapping:

```cpp
if (mIsPAFF) {
    // PAFF: Navigation-conformant frame selection.
    // The navigation decoder and Smart Cut decoder produce different display-order
    // sequences because they receive different packet streams (individual field packets
    // vs merged AU packets). Instead of mapping display-order to AU index, count
    // decoder outputs from keyframe — identical to what navigation does.
    int skipCount = startFrame - decodeStart;  // decodeStart = keyframe before startFrame

    int realStartIdx = -1;
    for (int i = skipCount; i < allDecodedFrames.size(); i++) {
        int au = static_cast<int>(allDecodedFrames[i]->pts);
        if (au >= streamCopyLimit) break;
        if (realStartIdx < 0) realStartIdx = i;
        framesToEncode.append(allDecodedFrames[i]);
    }

    if (realStartIdx >= 0) {
        realStartAU = static_cast<int>(allDecodedFrames[realStartIdx]->pts);
    }

    qDebug() << "      PAFF frame selection: skip" << skipCount
             << "decoder outputs, realStartAU =" << realStartAU
             << "(startFrame =" << startFrame << ")";
} else {
    // Non-PAFF: existing display-order mapping (tested and stable since v0.61.4-v0.61.6)
    ... existing code unchanged ...
}
```

### What Does NOT Change

- **Stream-copy boundary**: `streamCopyLimit` remains the next keyframe (unchanged)
- **`actualStartAU` output**: Takes AU index from `allDecodedFrames[skipCount]->pts`
- **Non-PAFF streams**: Entire existing code path untouched, zero regression risk
- **Audio drift correction**: `actualOutputFrameRanges()` continues to work (based on actually encoded frames)
- **SPS patching**: `mb_adaptive_frame_field_flag` patch remains (needed for DPB mode)
- **MKV muxer PAFF fixes**: Field PTS/DTS assignment remains (needed for playback)

### CutOut Frame Offset

The CutOut is controlled by `endFrame` which determines the decode range, not the frame
selection (that's bounded by `streamCopyLimit`). If CutOut offset persists after this fix,
it's a separate bug in navigation/widget update — not in `reencodeFrames()`.

## Verification

1. Build and test with PAFF test file (Moon_Crash_(2022).264)
2. Check debug log: `PAFF frame selection: skip N decoder outputs, realStartAU = X`
3. Verify no advertisement frames at cut boundaries in preview
4. Verify correct display order in segment 2
5. Verify A/V sync
6. Test non-PAFF stream (regression check) — no behavior change expected

## Files Modified

- `extern/ttessmartcut.cpp` — PAFF branch in `reencodeFrames()` display-order mapping section
