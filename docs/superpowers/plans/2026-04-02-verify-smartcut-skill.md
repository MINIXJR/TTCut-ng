# verify-smartcut Skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a Claude Code skill that objectively verifies Smart Cut output quality at segment transitions using SSIM frame analysis and decoder error detection.

**Architecture:** Single SKILL.md file in `~/.claude/skills/verify-smartcut/` containing step-by-step instructions for Claude Code to execute via Bash (ffprobe, ffmpeg, python3). No separate scripts — the skill IS the procedure.

**Tech Stack:** ffmpeg, ffprobe, python3 (stdlib only), bash

---

### Task 1: Create the Skill File

**Files:**
- Create: `~/.claude/skills/verify-smartcut/SKILL.md`

- [ ] **Step 1: Create the skill directory**

```bash
mkdir -p ~/.claude/skills/verify-smartcut
```

- [ ] **Step 2: Write SKILL.md**

Create `~/.claude/skills/verify-smartcut/SKILL.md` with the following content:

````markdown
---
name: verify-smartcut
description: >
  Verify Smart Cut output quality at segment transitions. Extracts frames
  around keyframes, computes SSIM (temporal + vs original), checks decoder
  errors. Reports PASS/WARN/FAIL per transition. Use after generating
  preview MKVs to objectively assess cut quality.
---

# Smart Cut Transition Quality Verification

Verify the Smart Cut output: "$ARGUMENTS"

Parse arguments: the first path is the **preview MKV**, the second is the **original ES file**,
the optional third is the **original frame offset** (integer) for the stream-copy start in the original.

Example: `/tmp/preview_004.mkv /media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264 84221`

## Prerequisites

- `ffmpeg` and `ffprobe` must be installed
- `python3` must be installed (stdlib only, no pip packages)
- Working directory for temp files: `/usr/local/src/CLAUDE_TMP/TTCut-ng/verify/`

## Step 1: Setup and Input Validation

```bash
mkdir -p /usr/local/src/CLAUDE_TMP/TTCut-ng/verify
```

Verify both input files exist. If the MKV or ES file is missing, report the error and stop.

## Step 2: Find Keyframe Positions

Run ffprobe to find all keyframe packet positions in the MKV:

```bash
ffprobe -v error -select_streams v:0 -show_packets \
  -show_entries packet=pts_time,flags -of csv=p=0 "$MKV" \
  | awk -F',' '$2 ~ /K/ {print NR, $1}'
```

This produces lines like `498 19.880000` — packet number and PTS of each keyframe.

**Important:** The FIRST keyframe (packet 1) is the stream start, not a transition. Skip it.
Only analyze keyframes AFTER the first one — these are segment boundaries.

If there is only one keyframe (pure stream-copy, no re-encode), report "No transitions to analyze" and stop with PASS.

## Step 3: Extract Frames Around Each Transition

For each transition keyframe at frame number F with PTS T:

```bash
cd /usr/local/src/CLAUDE_TMP/TTCut-ng/verify
rm -f mkv_*.png orig_*.png

# Extract 10 frames before and 10 after the keyframe from MKV
FIRST=$((F - 10))
LAST=$((F + 10))
ffmpeg -i "$MKV" \
  -vf "select='between(n\,$FIRST\,$LAST)'" \
  -vsync 0 -frame_pts 1 mkv_%04d.png -y 2>/dev/null
```

If the original frame offset was provided, also extract from the original:

```bash
# Original: offset maps MKV keyframe F to original frame ORIG_OFFSET
# Frames before keyframe in MKV = re-encoded, so original frames start
# 10 frames before ORIG_OFFSET
ORIG_FIRST=$((ORIG_OFFSET - 10))
ORIG_LAST=$((ORIG_OFFSET + 10))
ffmpeg -i "$ORIGINAL_ES" \
  -vf "select='between(n\,$ORIG_FIRST\,$ORIG_LAST)'" \
  -vsync 0 -frame_pts 1 orig_%04d.png -y 2>/dev/null
```

## Step 4: Temporal Analysis (Stutter/Freeze/Jump-Back Detection)

Run this Python script to analyze consecutive MKV frames:

```bash
python3 << 'PYEOF'
import subprocess, re, os, sys, json

frames_dir = "/usr/local/src/CLAUDE_TMP/TTCut-ng/verify"
frames = sorted([f for f in os.listdir(frames_dir)
                 if f.startswith("mkv_") and f.endswith(".png")])

if len(frames) < 3:
    print("NOT ENOUGH FRAMES for temporal analysis")
    sys.exit(0)

results = []
overall = "PASS"

for i in range(1, len(frames)):
    f_cur = os.path.join(frames_dir, frames[i])
    f_prev = os.path.join(frames_dir, frames[i-1])

    # SSIM vs previous
    r = subprocess.run(
        ["ffmpeg", "-i", f_prev, "-i", f_cur, "-lavfi", "ssim", "-f", "null", "-"],
        capture_output=True, text=True, timeout=10)
    m = re.search(r"All:([0-9.]+)", r.stderr)
    ssim_prev = float(m.group(1)) if m else 0.0

    # SSIM vs 2-back (jump-back detection)
    ssim_prev2 = 0.0
    if i >= 2:
        f_prev2 = os.path.join(frames_dir, frames[i-2])
        r2 = subprocess.run(
            ["ffmpeg", "-i", f_prev2, "-i", f_cur, "-lavfi", "ssim", "-f", "null", "-"],
            capture_output=True, text=True, timeout=10)
        m2 = re.search(r"All:([0-9.]+)", r2.stderr)
        ssim_prev2 = float(m2.group(1)) if m2 else 0.0

    verdict = "PASS"
    note = ""
    if ssim_prev > 0.999:
        verdict = "FAIL"
        note = "DUPLICATE/FREEZE"
        overall = "FAIL"
    elif ssim_prev2 > ssim_prev + 0.05 and ssim_prev2 > 0.95:
        verdict = "FAIL"
        note = "JUMP-BACK"
        overall = "FAIL"
    elif ssim_prev < 0.80:
        if overall != "FAIL":
            overall = "WARN"
        verdict = "WARN"
        note = "BIG CHANGE (scene change?)"

    fnum = frames[i].replace("mkv_", "").replace(".png", "")
    results.append({"frame": fnum, "ssim_prev": ssim_prev,
                    "ssim_prev2": ssim_prev2, "verdict": verdict, "note": note})

print(f"Temporal: {overall}")
for r in results:
    line = f"  Frame {r['frame']}: SSIM(N-1)={r['ssim_prev']:.4f}"
    if r['ssim_prev2'] > 0:
        line += f" SSIM(N-2)={r['ssim_prev2']:.4f}"
    if r['note']:
        line += f" — {r['verdict']}: {r['note']}"
    print(line)
PYEOF
```

## Step 5: Original Comparison (if offset provided)

Only run this if original frames were extracted in Step 3. Compare each MKV frame
against the corresponding original frame:

```bash
python3 << 'PYEOF'
import subprocess, re, os, sys

frames_dir = "/usr/local/src/CLAUDE_TMP/TTCut-ng/verify"
mkv_frames = sorted([f for f in os.listdir(frames_dir)
                     if f.startswith("mkv_") and f.endswith(".png")])
orig_frames = sorted([f for f in os.listdir(frames_dir)
                      if f.startswith("orig_") and f.endswith(".png")])

if not orig_frames:
    print("vs Original: SKIPPED (no original frames)")
    sys.exit(0)

# Pair by index position (both lists should have same count)
count = min(len(mkv_frames), len(orig_frames))
# The keyframe is at index 10 (11th frame, 0-indexed) in the extracted set
keyframe_idx = 10

overall = "PASS"
results = []

for i in range(count):
    f_mkv = os.path.join(frames_dir, mkv_frames[i])
    f_orig = os.path.join(frames_dir, orig_frames[i])

    r = subprocess.run(
        ["ffmpeg", "-i", f_orig, "-i", f_mkv, "-lavfi", "ssim", "-f", "null", "-"],
        capture_output=True, text=True, timeout=10)
    m = re.search(r"All:([0-9.]+)", r.stderr)
    ssim = float(m.group(1)) if m else 0.0

    is_reencode = (i < keyframe_idx)
    frame_type = "re-encode" if is_reencode else "stream-copy"

    if is_reencode:
        if ssim >= 0.85: verdict = "PASS"
        elif ssim >= 0.75: verdict = "WARN"; overall = max(overall, "WARN", key=lambda x: ["PASS","WARN","FAIL"].index(x))
        else: verdict = "FAIL"; overall = "FAIL"
    else:
        if ssim >= 0.95: verdict = "PASS"
        elif ssim >= 0.90: verdict = "WARN"; overall = max(overall, "WARN", key=lambda x: ["PASS","WARN","FAIL"].index(x))
        else: verdict = "FAIL"; overall = "FAIL"

    fnum = mkv_frames[i].replace("mkv_", "").replace(".png", "")
    results.append({"frame": fnum, "ssim": ssim, "type": frame_type, "verdict": verdict})

print(f"vs Original: {overall}")
for r in results:
    marker = ""
    if r["verdict"] == "FAIL":
        marker = " <-- ARTIFACT"
    elif r["verdict"] == "WARN":
        marker = " <-- degraded"
    print(f"  Frame {r['frame']}: SSIM={r['ssim']:.4f} ({r['type']}) — {r['verdict']}{marker}")
PYEOF
```

## Step 6: Decoder Error Check

```bash
DECODE_OUTPUT=$(ffmpeg -v debug -i "$MKV" -f null - 2>&1)

MMCO_COUNT=$(echo "$DECODE_OUTPUT" | grep -c "mmco: unref short failure" || true)
EXCEEDS=$(echo "$DECODE_OUTPUT" | grep -c "exceeds max" || true)
BACKWARD=$(echo "$DECODE_OUTPUT" | grep -c "backward" || true)
COLOCATED=$(echo "$DECODE_OUTPUT" | grep -c "co located POCs" || true)

# Frame count check
PACKETS=$(echo "$DECODE_OUTPUT" | grep -oP '(\d+) packets read' | head -1 | grep -oP '\d+')
DECODED=$(echo "$DECODE_OUTPUT" | grep -oP '(\d+) frames decoded' | head -1 | grep -oP '\d+')
FRAME_LOSS=0
if [ -n "$PACKETS" ] && [ -n "$DECODED" ]; then
    FRAME_LOSS=$((PACKETS - DECODED))
fi

DECODER_VERDICT="PASS"
DECODER_DETAIL=""

if [ "$EXCEEDS" -gt 0 ] || [ "$BACKWARD" -gt 0 ] || [ "$COLOCATED" -gt 0 ] || [ "$FRAME_LOSS" -gt 3 ]; then
    DECODER_VERDICT="FAIL"
fi
if [ "$MMCO_COUNT" -gt 0 ] && [ "$DECODER_VERDICT" = "PASS" ]; then
    DECODER_VERDICT="WARN"
fi

echo "Decoder: $DECODER_VERDICT"
echo "  mmco unref: ${MMCO_COUNT}x, exceeds max: ${EXCEEDS}x, backward: ${BACKWARD}x, colocated: ${COLOCATED}x"
echo "  Packets: $PACKETS, Decoded: $DECODED, Lost: $FRAME_LOSS (≤3 OK = DPB tail)"
```

## Step 7: Overall Verdict

Combine the three sub-verdicts (Temporal, Original Comparison, Decoder) into one overall result.
If ANY sub-verdict is FAIL, overall is FAIL.
If any is WARN (and none FAIL), overall is WARN.
Otherwise PASS.

Print a clear summary line:
```
=== OVERALL: PASS|WARN|FAIL ===
```

## Step 8: Cleanup

```bash
rm -rf /usr/local/src/CLAUDE_TMP/TTCut-ng/verify/
```

## Notes

- The skill logs all raw SSIM values so threshold calibration issues are visible.
- For interlaced content (PAFF/MBAFF), ffmpeg decodes to full frames — the SSIM comparison
  works on deinterlaced output, which is what the user sees during playback.
- The first keyframe in the MKV is always the stream start (not a transition). Skip it.
- Re-encode frames (before the keyframe) have inherently lower SSIM due to generation loss.
  The thresholds are calibrated from empirical measurements on DVB PAFF content (Moon Crash 2022).
````

- [ ] **Step 3: Verify the skill is discoverable**

Run TTCut-ng's Claude Code session and check that `/verify-smartcut` is recognized.
The skill should appear when typing `/verify-smartcut` in the Claude Code prompt.

- [ ] **Step 4: Commit**

```bash
cd /usr/local/src/TTCut-ng
git add docs/superpowers/specs/2026-04-02-verify-smartcut-skill-design.md
git add docs/superpowers/plans/2026-04-02-verify-smartcut-skill.md
git commit -m "Add verify-smartcut skill design and implementation plan"
```

---

### Task 2: Test the Skill with Known-Bad Preview

**Files:**
- Test with: `/tmp/preview_004.mkv` (known PAFF stutter at ~19.88s)
- Original: `/media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264`

- [ ] **Step 1: Run the skill without original offset (temporal analysis only)**

Invoke: `/verify-smartcut /tmp/preview_004.mkv /media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264`

Expected: Temporal analysis runs, may detect BIG CHANGE at frame 497. Decoder check detects mmco errors.
Original comparison is skipped (no offset).

- [ ] **Step 2: Run the skill with original offset (full analysis)**

The stream-copy keyframe for preview_004 segment 2 starts at original frame 84221.

Invoke: `/verify-smartcut /tmp/preview_004.mkv /media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264 84221`

Expected: FAIL on "vs Original" for frames around the transition (SSIM < 0.90 for stream-copy frames near the keyframe due to block artifacts).

- [ ] **Step 3: Verify known-good preview passes**

Run against preview_001 (pure stream-copy, no re-encode transition):

Invoke: `/verify-smartcut /tmp/preview_001.mkv /media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264`

Expected: PASS or WARN (only pre-existing mmco errors from source).

- [ ] **Step 4: Adjust thresholds if needed**

If the skill produces false positives or false negatives during testing, update the threshold values in SKILL.md. Document the reason for any changes.

---

### Task 3: Calibrate with Known-Good Non-PAFF Content

**Files:**
- Test with any non-PAFF H.264 preview MKV (if available from previous test sessions)

- [ ] **Step 1: Run against non-PAFF content**

If a non-PAFF test file is available, run the skill to verify it produces PASS for content without the MBAFF/PAFF transition issue.

- [ ] **Step 2: Document calibration results**

Add a brief note to the skill's SKILL.md with tested content types and threshold behavior.

- [ ] **Step 3: Final commit**

```bash
cd /usr/local/src/TTCut-ng
git add -A
git commit -m "Calibrate verify-smartcut skill thresholds"
```
