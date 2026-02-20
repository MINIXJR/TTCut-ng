# Audio Quality Fixes Design

Three fixes for audio-related issues found during Smart Cut quality checks.

## Fix 1: Quality-Check Audio Click False Positive

**File:** `tools/ttcut-quality-check.py` (~line 1040)

**Problem:** The click detector flags LSB quantization noise in digital silence
as a click. A 1ms chunk with RMS=0 (-188 dBFS) surrounded by chunks with
RMS=0.2 (-104 dBFS) triggers a 84dB "spike" detection, but the absolute
level is completely inaudible.

**Fix:** Add an absolute floor threshold. Chunks below -80 dBFS are excluded
from click detection. -80 dBFS corresponds to RMS ~3.3 at 16-bit — anything
below is quantization noise.

```python
SILENCE_FLOOR = -80.0
if jump_prev > 30.0 and jump_next > 30.0 and db_levels[c] > SILENCE_FLOOR:
    has_click = True
```

## Fix 2: 122ms Audio Duration Mismatch (Off-by-One)

**Files:** `data/ttavdata.cpp` (~line 997), `data/ttcutpreviewtask.cpp` (~line 355)

**Problem:** Frame-to-time conversion uses `endFrame / frameRate`, which gives
the START time of the last frame. The correct end time is `(endFrame + 1) / frameRate`
(after the last frame completes). This loses 1 frame duration (40ms at 25fps)
per segment. With 4 segments: 160ms systematic loss, observed as 122ms after
AC3 frame-boundary quantization partially compensates.

**Fix:** Change `cutOutTime` calculation in both files:

```cpp
// Before:
double cutOutTime = endFrame / frameRate;

// After:
double cutOutTime = (endFrame + 1) / frameRate;
```

**Scope:** Only H.264/H.265 Smart Cut and Preview paths. The MPEG-2 path uses
a different mechanism (audio frame indices + offset accumulator) and is not
affected.

## Fix 3: ttcut-demux Hardcoded Audio Bitrates

**File:** `tools/ttcut-demux` (lines 745-763)

**Problem:** Audio padding (triggered when video > audio by >20ms) re-encodes
with hardcoded bitrates: mp2=192k, ac3=384k, aac=192k. If the original
has different settings (e.g., MP2 at 160kbps), the re-encoded output has
wrong quality. Channel count is also not explicitly preserved.

**Fix:** Read original bitrate and channels via ffprobe before encoding:

```bash
ORIG_BITRATE=$(ffprobe -v error -select_streams a:0 \
    -show_entries stream=bit_rate \
    -of default=noprint_wrappers=1:nokey=1 "$AUDIO_FILE" 2>/dev/null)
ORIG_CHANNELS=$(ffprobe -v error -select_streams a:0 \
    -show_entries stream=channels \
    -of default=noprint_wrappers=1:nokey=1 "$AUDIO_FILE" 2>/dev/null)

case "$AUDIO_CODEC" in
    mp2)  ENCODER="-c:a mp2 -b:a ${ORIG_BITRATE:-192000} -ac ${ORIG_CHANNELS:-2}" ;;
    ac3)  ENCODER="-c:a ac3 -b:a ${ORIG_BITRATE:-384000} -ac ${ORIG_CHANNELS:-6}" ;;
    aac)  ENCODER="-c:a aac -b:a ${ORIG_BITRATE:-192000} -ac ${ORIG_CHANNELS:-2}" ;;
    *)    ENCODER="-c:a copy" ;;
esac
```

Bash parameter expansion (`${VAR:-default}`) provides fallback values if
ffprobe fails.

**Not affected:** `ttcut-ac3fix` does no audio re-encoding (header patching
only).

## Verification

1. Build: `make clean && qmake ttcut-ng.pro && make -j$(nproc)`
2. Re-cut Clouseau project with new binary
3. Run `ttcut-quality-check.py` — expect all 7 tests PASS
4. Test ttcut-demux padding with a file where video > audio by >20ms
