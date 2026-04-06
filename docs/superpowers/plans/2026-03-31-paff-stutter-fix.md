# PAFF Stutter Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate stutter at cut points in PAFF H.264 streams by patching `mb_adaptive_frame_field_flag` from 0→1 in source SPS.

**Architecture:** Extend the existing `patchH264SpsReorderFrames()` function to also set `mb_adaptive_frame_field_flag=1` when patching PAFF source SPS. The function already parses through this field (line 2581-2582) but doesn't modify it. Single-bit change in RBSP data that's already copied.

**Tech Stack:** C++ (Qt5), H.264 SPS bitstream manipulation

**Build:** `rm obj/ttessmartcut.o; bear -- make -j$(nproc)`

**Test file:** `/media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264` (PAFF, 1080i50 TFF)

---

### Task 1: Patch mb_adaptive_frame_field_flag in patchH264SpsReorderFrames()

**Files:**
- Modify: `extern/ttessmartcut.cpp:2578-2582`

**Why:** The function already parses `frame_mbs_only_flag` and `mb_adaptive_frame_field_flag` at lines 2580-2582 but doesn't modify the value. Since the entire RBSP up to `bsrFlagPos` is bulk-copied (line 2658), the `mb_adaptive_frame_field_flag` bit is already in `newRbsp`. We just need to record its bit position and flip it after the copy.

- [ ] **Step 1: Record mb_adaptive_frame_field_flag bit position and patch it**

In `extern/ttessmartcut.cpp`, find this block (around line 2578-2582):

```cpp
    spsReadUE(data, dataSize, bitPos);       // pic_height_in_map_units_minus1

    uint32_t frame_mbs_only = spsReadBits(data, dataSize, bitPos, 1);
    if (!frame_mbs_only)
        spsReadBits(data, dataSize, bitPos, 1);  // mb_adaptive_frame_field_flag
```

Replace it with:

```cpp
    spsReadUE(data, dataSize, bitPos);       // pic_height_in_map_units_minus1

    uint32_t frame_mbs_only = spsReadBits(data, dataSize, bitPos, 1);
    int mbAdaptiveBitPos = -1;  // bit position of mb_adaptive_frame_field_flag in RBSP
    if (!frame_mbs_only) {
        mbAdaptiveBitPos = bitPos;  // record position before reading
        spsReadBits(data, dataSize, bitPos, 1);  // mb_adaptive_frame_field_flag
    }
```

Then, find the block where `newRbsp` is built (around line 2655-2658):

```cpp
    int bytesNeeded = (bsrFlagPos + 7) / 8 + 16;  // generous buffer for added fields
    QByteArray newRbsp(bytesNeeded, '\0');
    // Copy existing data up to the flag position
    memcpy(newRbsp.data(), rbsp.constData(), (bsrFlagPos + 7) / 8);
```

Add the PAFF patch immediately after the `memcpy`:

```cpp
    int bytesNeeded = (bsrFlagPos + 7) / 8 + 16;  // generous buffer for added fields
    QByteArray newRbsp(bytesNeeded, '\0');
    // Copy existing data up to the flag position
    memcpy(newRbsp.data(), rbsp.constData(), (bsrFlagPos + 7) / 8);

    // PAFF→MBAFF: set mb_adaptive_frame_field_flag=1 so decoder accepts both
    // MBAFF re-encoded frames and PAFF stream-copied fields without mode switch
    if (mbAdaptiveBitPos >= 0) {
        uint8_t* w = reinterpret_cast<uint8_t*>(newRbsp.data());
        int byteIdx = mbAdaptiveBitPos / 8;
        int bitIdx = 7 - (mbAdaptiveBitPos % 8);
        if (!(w[byteIdx] & (1 << bitIdx))) {
            w[byteIdx] |= (1 << bitIdx);
            qDebug() << "  SPS patched: mb_adaptive_frame_field_flag 0->1 (PAFF->MBAFF signaling)";
        }
    }
```

- [ ] **Step 2: Build**

Run:
```bash
rm obj/ttessmartcut.o; bear -- make -j$(nproc)
```
Expected: Compiles without errors or warnings.

- [ ] **Step 3: Commit**

```bash
git add extern/ttessmartcut.cpp
git commit -m "Patch mb_adaptive_frame_field_flag in SPS for PAFF streams

At the re-encode/stream-copy boundary, x264 produces MBAFF frames
but stream-copied sections are PAFF. The SPS mode mismatch caused
decoder DPB failures (39 lost frames, mmco errors).

Fix: set mb_adaptive_frame_field_flag=1 in the source SPS when
patching bitstream_restriction. MBAFF signaling is a superset of
PAFF — the decoder accepts both coding modes without DPB reset.

Single-bit change in existing patchH264SpsReorderFrames(), no new
call sites needed."
```

---

### Task 2: Test with Moon_Crash PAFF stream

**Files:** None (manual testing)

- [ ] **Step 1: Open Moon_Crash in TTCut-ng**

Run TTCut-ng, open `/media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264`. Set cut points and generate preview.

Check debug output for:
```
SPS patched: mb_adaptive_frame_field_flag 0->1 (PAFF->MBAFF signaling)
```

This message should appear for each SPS patch (multiple times — once per inline SPS in stream-copy).

- [ ] **Step 2: Verify preview playback**

Play preview_003.mkv (or whichever preview contains a CutIn with re-encode). Check:
- No stutter at the cut point
- No repeated/frozen frames at the transition
- Normal playback speed
- A/V sync correct

- [ ] **Step 3: Verify with ffprobe**

```bash
ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames /tmp/preview_003.mkv
```

Expected: `nb_read_frames` should match expected frame count (no lost frames). No `mmco: unref short failure` messages.

- [ ] **Step 4: Regression test with progressive H.264**

Open a known progressive H.264 file. Verify:
- No `mb_adaptive_frame_field_flag` patch message in debug output (progressive has `frame_mbs_only_flag=1` → no `mb_adaptive_frame_field_flag` to patch)
- Preview and cut work identically to before
