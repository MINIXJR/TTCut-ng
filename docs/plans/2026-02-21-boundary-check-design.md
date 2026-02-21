# Design: Pre-Flight Boundary Check

**Date**: 2026-02-21
**Status**: Approved

## Problem

DVB broadcasts often have audio/video timing mismatches at commercial transitions.
Commercial audio can start ~28ms before the corresponding commercial video frame.
With AC3 stream-copy (`-c:a copy -to cutOutTime`), the AC3 frame containing the
commercial audio onset gets included because its PTS < cutOutTime. Result: audible
2-frame burst (64ms) in the cut output.

Additionally, commercials may use a different aspect ratio (4:3 vs 16:9), which is
signaled via SPS parameter changes in H.264/H.265 streams.

## Solution

Pre-flight boundary check before every cut operation (preview + final cut).
Analyzes all cut boundaries for anomalies, shows a dialog, and lets the user
decide per boundary: shift the cut point or accept.

## Scope

- **Audio burst detection**: All codecs (H.264, H.265, MPEG-2)
- **SPS comparison**: H.264/H.265 only (MPEG-2 uses sequence headers, not SPS)
- **Checks both**: CutOut boundaries (end of kept segment) AND CutIn boundaries
  (start of kept segment)

## Architecture

### Insertion Points

All insertion points are in the main thread, before the actual cut begins.

```
onDoCut() (ttavdata.cpp:860)
  |-- Audio burst check (all codecs)        <-- before codec dispatch
  |-- showBoundaryDialog() if issues found
  |-- if H.264/H.265: doH264Cut()
  |     +-- SPS check after smartCut.initialize()
  +-- if MPEG-2: start thread pool

doCutPreview() (ttavdata.cpp:797)
  |-- Audio burst check (all codecs)        <-- before thread start
  |-- showBoundaryDialog() if issues found
  +-- start thread pool
```

### Components

| Component | File | Scope |
|-----------|------|-------|
| `BoundaryIssue` struct | `data/ttavdata.h` | all codecs |
| `checkAudioBoundaries()` | `data/ttavdata.cpp` (~60 lines) | all codecs |
| `detectAudioBurst()` | `extern/ttffmpegwrapper.cpp` (~60 lines) | all codecs |
| `compareSPSAtBoundary()` | `extern/ttessmartcut.cpp` (~25 lines) | H.264/H.265 |
| `showBoundaryDialog()` | `data/ttavdata.cpp` (~40 lines) | all codecs |

### Data Structures

```cpp
struct BoundaryIssue {
    int segmentIndex;        // Which segment (0-based)
    bool isCutOut;           // true=CutOut, false=CutIn
    int frameIndex;          // Current frame index

    // Audio burst
    bool hasAudioBurst;
    double burstRmsDb;       // RMS of burst chunk (dB)
    double contextRmsDb;     // Median RMS of surrounding chunks (dB)

    // SPS change (H.264/H.265 only)
    bool hasSPSChange;
};
```

### Audio Burst Detection

For each CutOut and CutIn boundary:
1. Extract ~200ms audio window around the boundary via ffmpeg astats
2. Parse RMS values from stderr output (per-frame, 1536-sample chunks)
3. If any chunk exceeds median by >20dB: burst detected

```
ffmpeg -v error -i audio.ac3 \
  -ss (boundary-0.2) -to (boundary+0.032) \
  -af astats=metadata=1:reset=1536 -f null - 2>&1
```

Duration: ~100ms per boundary, <1s for typical 4-segment recording.

Audio frame duration is NOT hardcoded. Determined dynamically:
- frame_duration = samples_per_frame / sample_rate
- AC3: 1536 samples, MP2: 1152, AAC: 1024
- Sample rate queried via ffprobe or getStreamInfo()

### SPS Comparison

TTNaluParser already indexes all SPS NALUs (`mSPSList`). For each cut boundary:
1. Find the SPS active at cutOut frame
2. Find the SPS active at cutOut+1 (first commercial frame)
3. Compare raw bytes -- difference indicates resolution/aspect ratio change

If `spsCount() == 1`: no SPS changes in entire stream, skip check.

Only runs inside `doH264Cut()` after `smartCut.initialize()` provides the parser.

### Dialog

```
+-- Schnittgrenzen-Pruefung -----------------------+
|                                                   |
| Segment 3 Ende (Frame 119661, 4786.5s):          |
|   ! Audio-Burst: -7dB (Umgebung: -55dB)          |
|                                                   |
| Segment 4 Anfang (Frame 124902, 4996.1s):        |
|   ! SPS-Wechsel erkannt (Aspekt-Verhaeltnis)     |
|                                                   |
| [Alle verschieben]  [Akzeptieren]  [Abbrechen]   |
+---------------------------------------------------+
```

- **Verschieben**: CutOut -= 1 frame, CutIn += 1 frame
- **Akzeptieren**: Continue without changes
- **Abbrechen**: Return to editor

### Why No SPS Check in Preview?

The SmartCut parser is initialized inside the worker thread
(`TTCutPreviewTask::operation()` line 138). Moving it to the main thread
would require a larger refactor. Visual aspect ratio changes are visible
in the preview anyway.

## Effort

~185 lines of new code, no new files, no new dependencies.
