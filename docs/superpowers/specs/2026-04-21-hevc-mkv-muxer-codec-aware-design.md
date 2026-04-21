# HEVC MKV Muxer — Codec-Aware NAL Parsing Design

**Date:** 2026-04-21
**Branch:** master (to be branched for implementation)
**Status:** Design approved

## Problem

H.265/HEVC Smart Cuts produce MKV files that contain **only audio, no video**.

User-visible symptom: The cut finishes without error, TTCut-ng reports success, but the resulting MKV plays back as a black screen with sound only. No error is shown in the UI; the failure is silent. The Debug log reveals repeated `MKV PAFF: skip non-VCL video packet` messages for every video packet.

Root cause: `extern/ttmkvmergeprovider.cpp` parses NAL unit types assuming H.264 format. In the ES mux loop two places apply the H.264 convention — 1-byte NAL header, 5-bit type via `d[s] & 0x1F`, VCL types 1 (slice) and 5 (IDR):

- **Lines 811-829 (non-PAFF video VCL check):** the only VCL filter for non-PAFF streams. HEVC always lands here because HEVC streams never have `mIsPAFF=true`.
- **Lines 860-876 (PAFF field-pair SEI skip):** inner loop that skips non-VCL NALs between a field pair. Reached only with `mIsPAFF=true`, so H.264-only in practice, but uses the same hardcoded H.264 pattern.

HEVC uses a 2-byte NAL header with `nal_unit_type` in bits 1-6 of the first byte — `(d[s] >> 1) & 0x3F`, VCL types 0-31. The H.264 check never matches correctly on HEVC, so every video packet is treated as non-VCL and dropped.

Pipeline context:
- **Loading** in `TTOpenVideoTask::run()` correctly accepts H.265 (`ttopenvideotask.cpp:123-126`).
- **Smart Cut** via `TTESSmartCut` produces correct HEVC ES output.
- **Mux step** is the single broken link in an otherwise H.265-capable pipeline.

## Design Decision

Make the muxer codec-aware for NAL parsing. The caller already knows the codec at `data/ttavdata.cpp:1581` (from `videoStream->streamType()`); hand that knowledge to the muxer via an explicit setter, symmetric to the existing `setIsPAFF`. A single codec-aware helper replaces the hardcoded H.264 check at both sites.

No auto-detection in the muxer. The pipeline validates the codec once at the entry point (`TTOpenVideoTask::run()`) and should not re-discover it downstream.

### API change

`extern/ttmkvmergeprovider.h`:

```cpp
extern "C" {
#include <libavcodec/codec_id.h>
}

// Existing:
void setIsPAFF(bool paff, int log2MaxFrameNum = 4) { /* ... */ }

// New — analogous setter, default AV_CODEC_ID_NONE must be overridden by caller
void setVideoCodecId(enum AVCodecID codecId) { mVideoCodecId = codecId; }

private:
    // ... existing members ...
    enum AVCodecID mVideoCodecId;   // initialized to AV_CODEC_ID_NONE in ctor
```

Constructor initializes `mVideoCodecId = AV_CODEC_ID_NONE` alongside the existing PAFF defaults.

### Caller change

`data/ttavdata.cpp:1581` (after `setIsPAFF(...)` in the MKV mux branch):

```cpp
mkvProvider->setIsPAFF(videoStream->isPAFF(), videoStream->paffLog2MaxFrameNum());
AVCodecID codecId = (videoStream->streamType() == TTAVTypes::h265_video)
                  ? AV_CODEC_ID_HEVC
                  : AV_CODEC_ID_H264;
mkvProvider->setVideoCodecId(codecId);
```

Only H.264 and H.265 reach the MKV ES mux path — MPEG-2 goes through mplex, not this provider. No MPEG-2 branch needed.

### Codec-aware VCL helper

File-local helper in `extern/ttmkvmergeprovider.cpp`:

```cpp
// Returns true if the byte at `b` starts a Video Coding Layer NAL unit
// for the given codec. Caller guarantees `b` points to the first NAL
// payload byte (after the start code).
static bool isVclNalByte(enum AVCodecID codec, const uint8_t* b)
{
    switch (codec) {
        case AV_CODEC_ID_H264: {
            uint8_t nt = b[0] & 0x1F;
            return nt == H264::NAL_SLICE || nt == H264::NAL_IDR_SLICE;
        }
        case AV_CODEC_ID_HEVC: {
            uint8_t nt = (b[0] >> 1) & 0x3F;
            return nt <= 31;  // VCL types 0-31 per HEVC spec
        }
        default:
            Q_ASSERT_X(false, "isVclNalByte",
                       "unexpected video codec in MKV ES mux path");
            return false;
    }
}
```

Include `avstream/ttnaluparser.h` for `H264::NAL_SLICE` / `H264::NAL_IDR_SLICE` constants; replaces magic numbers 1/5.

### Call-site updates

Two sites inside the ES mux loop change from the hardcoded H.264 pattern to the helper:

**Site 1 — Non-PAFF VCL check (lines 811-829):**

```cpp
// Before:
for (int p = 0; p < sz - 3; p++) {
    if (d[p] == 0 && d[p+1] == 0) {
        int s = -1;
        if (d[p+2] == 1) s = p + 3;
        else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
        if (s >= 0 && s < sz) {
            uint8_t nt = d[s] & 0x1F;
            if (nt == 1 || nt == 5) { hasVclNal = true; break; }
        }
    }
}
if (!hasVclNal && sz >= 1) {
    uint8_t nt = d[0] & 0x1F;
    if (nt == 1 || nt == 5) hasVclNal = true;
}

// After:
for (int p = 0; p < sz - 3; p++) {
    if (d[p] == 0 && d[p+1] == 0) {
        int s = -1;
        if (d[p+2] == 1) s = p + 3;
        else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
        if (s >= 0 && s < sz && isVclNalByte(mVideoCodecId, d + s)) {
            hasVclNal = true; break;
        }
    }
}
if (!hasVclNal && sz >= 1 && isVclNalByte(mVideoCodecId, d)) {
    hasVclNal = true;
}
```

**Site 2 — PAFF field-pair inner SEI-skip loop (lines 860-876):**

The `nt = nd[s] & 0x1F; if (nt == 1 || nt == 5)` pattern there becomes `isVclNalByte(mVideoCodecId, nd + s)`. This path only runs with `mIsPAFF=true` — H.264-exclusive — so the switch dispatch collapses to the H.264 branch, preserving current behavior.

### Logging

Add one `qDebug()` line at the top of `mux()` reporting the codec:

```cpp
qDebug() << "MKV mux: videoCodecId =" << avcodec_get_name(mVideoCodecId);
```

Helps post-hoc debugging if the setter is forgotten (value stays `AV_CODEC_ID_NONE`, `Q_ASSERT_X` fires on the first packet).

## Files Touched

- `extern/ttmkvmergeprovider.h` — add `setVideoCodecId()` setter and `mVideoCodecId` member.
- `extern/ttmkvmergeprovider.cpp` — add `isVclNalByte()` helper, initialize `mVideoCodecId` in constructor, replace hardcoded H.264 pattern at two sites, add entry log, include `avstream/ttnaluparser.h` and `<libavcodec/codec_id.h>`.
- `data/ttavdata.cpp` — call `setVideoCodecId()` after existing `setIsPAFF()` at line 1581.

## Invariants After Fix

- H.264 PAFF streams: byte-identical output to current behavior (H.264 switch branch produces the same result as the hardcoded pattern).
- H.264 MBAFF / progressive streams: byte-identical output to current behavior.
- H.265 streams: video packets are now correctly identified as VCL and written to the MKV. Resulting file contains both video and audio streams.
- MPEG-2 remains untouched — it uses the mplex path, not `TTMkvMergeProvider`.
- If the caller forgets `setVideoCodecId()` (programmer error), `Q_ASSERT_X` fires on the first video packet in a debug build. In release builds, `Q_ASSERT` is a no-op and `isVclNalByte` returns false — all video is dropped and the entry-log line reveals `AV_CODEC_ID_NONE`. The existing silent-failure mode is preserved as the worst case; no new failure mode is introduced.

## Out of Scope

- **HEVC CRA-only Smart Cut verification** — separate TODO, concerns Re-Encode path in `TTESSmartCut`, not the muxer.
- **MPEG-2** — uses a different muxer path, not affected.
- **Adding a third codec** — the switch accommodates it with a new case; no change beyond that case is required.
- **Removing the Q_ASSERT** — kept as defense in depth against future regressions in the entry-point validation.

## Testing

Manual verification:

1. **H.265 end-to-end cut.**
   Load `Ausdrucksstarke_Designermode.265` (HEVC 4K 3840x2160, 50fps — referenced in memory). Perform a Smart Cut → MKV.
   Expected: `ffprobe output.mkv` lists both video (hevc) and audio streams; `mpv output.mkv` plays picture and sound without artifacts at segment boundaries.

2. **H.264 PAFF regression.**
   Re-run an existing PAFF Smart Cut (e.g., `Moon_Crash_(2022).264` from the v0.65.0 release). Quality-check should still produce 7/7 PASS.

3. **H.264 non-PAFF regression.**
   Re-run a standard H.264 cut (DVB recording, progressive or MBAFF). No visible change.

4. **Caller-forgot-setter defense.**
   Debug build: remove or comment out the `setVideoCodecId()` call in `data/ttavdata.cpp`, run a cut. `Q_ASSERT_X` must fire on the first video packet with the "unexpected video codec" message.

5. **Entry log check.**
   Debug log should contain `MKV mux: videoCodecId = h264` or `hevc` at the start of each cut.

No automated tests — the muxer path has no existing coverage.

## Rollback

Three files touched, no schema or persistence changes. Revert with `git checkout -- <paths>`.
