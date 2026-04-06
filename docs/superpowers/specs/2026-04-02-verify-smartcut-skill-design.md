# Design: `verify-smartcut` Claude Code Skill

**Date:** 2026-04-02
**Status:** Approved

## Purpose

A Claude Code skill (`~/.claude/skills/verify-smartcut/`) that objectively verifies Smart Cut output quality at segment transitions. Eliminates the need to ask the user "does it look OK?" by providing automated PASS/WARN/FAIL metrics based on frame-by-frame SSIM analysis.

## Inputs

Minimal — the skill expects two positional arguments:

1. **`preview.mkv`** — The cut output MKV file
2. **`original.264`** — The original (uncut) H.264 elementary stream

Optional third argument:

3. **`original_frame_offset`** — Frame number in the original ES that corresponds to the first stream-copy keyframe in the MKV. When provided, enables SSIM comparison against the original. When omitted, only temporal analysis is performed.

## Analysis Pipeline

### Step 1: Keyframe Detection

Use `ffprobe` to find all keyframe positions in the MKV:
```
ffprobe -v error -select_streams v:0 -show_packets -show_entries packet=pts_time,flags
```
Keyframes (`K__` flag) mark segment boundaries (re-encode→stream-copy transitions).

### Step 2: Frame Extraction

For each keyframe at time T:
- Extract 10 frames before T and 10 frames after T from the MKV (21 frames per transition)
- If `original_frame_offset` provided: extract corresponding 21 frames from the original ES at the matching position

Use `ffmpeg -vf select` with `-vsync 0 -frame_pts 1` for exact frame extraction.

### Step 3: Temporal Analysis (always runs)

Compare consecutive MKV frames via SSIM:

| Metric | Detection | Threshold |
|--------|-----------|-----------|
| **Duplicate/Freeze** | SSIM(N, N-1) > 0.999 | FAIL |
| **Jump-Back** | SSIM(N, N-2) > SSIM(N, N-1) + 0.05 AND SSIM(N, N-2) > 0.95 | FAIL |
| **Big Change** | SSIM(N, N-1) < 0.80 | WARN (might be scene change) |

### Step 4: Original Comparison (when offset provided)

Compare each MKV frame against the corresponding original frame via SSIM:

| Frame Type | PASS | WARN | FAIL |
|------------|------|------|------|
| Stream-Copy (after keyframe) | ≥ 0.95 | 0.90–0.95 | < 0.90 |
| Re-Encode (before keyframe) | ≥ 0.85 | 0.75–0.85 | < 0.75 |

Re-encode frames inherently have lower SSIM due to generation loss — the thresholds reflect this.

### Step 5: Decoder Error Check

Decode the MKV with `ffmpeg -v debug` and check for:
- `mmco: unref short failure` — count occurrences
- `exceeds max` — FAIL (frame discarded by decoder)
- `backward timestamps` — FAIL
- `co located POCs unavailable` — FAIL
- Total decoded frames vs total packets — any mismatch beyond 3 (DPB tail) is FAIL

## Output Format

Text summary per transition, suitable for Claude Code conversation:

```
=== Transition at 19.88s (keyframe #498) ===
Temporal: PASS (no duplicates, no jumps)
vs Original: FAIL
  Frame 497: SSIM=0.83 (re-encode, threshold 0.75) — PASS
  Frame 498: SSIM=0.81 (stream-copy, threshold 0.90) — FAIL ← block artifacts
  Frame 499: SSIM=0.88 (stream-copy) — FAIL
  Frame 500: SSIM=0.91 (stream-copy) — WARN
  Frame 501: SSIM=0.94 (stream-copy) — WARN
  Frame 502: SSIM=0.96 (stream-copy) — PASS
Decoder: WARN — 3x mmco unref short, 0x exceeds max
VERDICT: FAIL
```

Overall summary at the end:
```
=== OVERALL: FAIL (1/3 transitions failed) ===
```

## Skill Structure

```
~/.claude/skills/verify-smartcut/
  SKILL.md          — Skill definition (instructions for Claude Code)
```

The skill itself contains instructions for Claude Code to execute the analysis using Bash (ffprobe, ffmpeg, python3 one-liners). No separate script file — the skill IS the procedure.

## Dependencies

- `ffmpeg` and `ffprobe` (already installed)
- `python3` (for SSIM parsing, already installed)
- No additional packages needed

## Calibration

Thresholds are based on empirical measurements from Moon Crash (2022) PAFF test file:
- Frame 496 (clean re-encode): SSIM vs neighbor = 0.94
- Frame 497 (block artifacts): SSIM vs neighbor = 0.83
- Frame 498 (moderate artifacts): SSIM vs neighbor = 0.94 but vs original = ~0.81
- Clean stream-copy frames: SSIM vs original ≥ 0.95

Thresholds may need adjustment for other content types. The skill should log raw values so calibration issues are visible.

## Scope Boundaries

**In scope:**
- Segment transition quality (±10 frames around keyframes)
- Temporal anomalies (stutter, freeze, jump-back)
- Decoder error detection
- SSIM comparison against original (when offset provided)

**Out of scope:**
- Full-stream quality analysis (handled by `ttcut-quality-check.py`)
- Audio quality verification
- A/V sync measurement
- Automated cut-point selection
