# Quality-Check Extra-Frame-Korrektur Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix false -428ms A/V offset measurement in ttcut-quality-check.py by accounting for extra frames in all time calculations, and add a defect region report.

**Architecture:** Extend the existing `parse_info_file()` to read `es_extra_frames` from .info, add `count_extras_before()` helper (bisect_left), thread extra_frames list through all `frame / fps` calculations. New `test_defect_regions()` groups extra frames within cut segments using settings from `~/.config/TTCut-ng/TTCut-ng.conf`.

**Tech Stack:** Python 3.8+, bisect (stdlib), existing ffmpeg/ffprobe/mkvmerge dependencies unchanged.

---

### Task 1: Extend `parse_info_file()` and Add `count_extras_before()`

Add extra frame parsing from .info and the binary search helper.

**Files:**
- Modify: `tools/ttcut-quality-check/ttcut-quality-check.py` (lines 241-267 and after line 287)

- [ ] **Step 1: Add `bisect` import**

At line 7, add the import:

```python
from bisect import bisect_left
```

The existing imports block (lines 119-129) becomes:

```python
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from bisect import bisect_left
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional
```

- [ ] **Step 2: Extend `parse_info_file()`**

Replace the function at lines 241-267 with:

```python
def parse_info_file(info_path: str) -> dict:
    """Parse TTCut .info file, return dict with fps, avOffset, extra_frames."""
    result = {"fps": None, "av_offset_ms": 0, "extra_frames": []}
    section = None
    with open(info_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"\[(\w+)\]", line)
            if m:
                section = m.group(1)
                continue
            if "=" not in line:
                continue
            key, val = [x.strip() for x in line.split("=", 1)]

            if section == "video" and key == "frame_rate":
                if "/" in val:
                    num, den = val.split("/")
                    den = int(den) or 1
                    result["fps"] = int(num) / den
                else:
                    result["fps"] = float(val)
            elif section == "timing" and key == "av_offset_ms":
                result["av_offset_ms"] = int(val)
            elif section == "warnings" and key == "es_extra_frames":
                if val:
                    result["extra_frames"] = sorted(
                        int(x.strip()) for x in val.split(",") if x.strip()
                    )
    return result
```

- [ ] **Step 3: Add `count_extras_before()` helper**

Insert after `frames_to_seconds()` (after line 283):

```python
def count_extras_before(frame: int, extra_frames: list) -> int:
    """Count extra frames with index < frame (binary search)."""
    if not extra_frames:
        return 0
    return bisect_left(extra_frames, frame)
```

- [ ] **Step 4: Extend `frames_to_seconds()`**

Replace the function at lines 282-283 with:

```python
def frames_to_seconds(frame: int, fps: float, extra_frames: list = None) -> float:
    """Convert frame index to time in seconds, correcting for extra frames."""
    if extra_frames:
        frame -= count_extras_before(frame, extra_frames)
    return frame / fps
```

- [ ] **Step 5: Verify syntax**

Run:

```bash
python3 tools/ttcut-quality-check/ttcut-quality-check.py --help
```

Expected: Help text prints without errors.

- [ ] **Step 6: Commit**

```bash
git add tools/ttcut-quality-check/ttcut-quality-check.py
git commit -m "Add extra frame parsing and time correction helpers to quality check

Parse es_extra_frames from .info [warnings] section. Add
count_extras_before() binary search and extend frames_to_seconds()
with optional extra frame correction."
```

---

### Task 2: Thread Extra Frames Through Test 4 (Visual Comparison)

Pass extra_frames to all time calculations in `test_visual_comparison()`.

**Files:**
- Modify: `tools/ttcut-quality-check/ttcut-quality-check.py` (lines 572-718)

- [ ] **Step 1: Add `extra_frames` parameter to function signature**

Change line 572 from:

```python
def test_visual_comparison(original_es: str, cut_mkv: str, fps: float,
                           cuts: list[tuple[int, int]], tmpdir: str) -> TestResult:
```

to:

```python
def test_visual_comparison(original_es: str, cut_mkv: str, fps: float,
                           cuts: list[tuple[int, int]], tmpdir: str,
                           extra_frames: list = None) -> TestResult:
```

- [ ] **Step 2: Correct time calculations in Step 2 (boundary detection loop)**

Replace lines 600-612:

```python
        for seg_idx, (start_frame, end_frame) in enumerate(cuts):
            seg_frames = end_frame - start_frame + 1
            ref_start = start_frame / fps

            offset = _detect_boundary_offset(
                ref_mkv, cut_mkv, ref_start, cut_offset_time,
                fps, tmpdir, seg_idx)
            boundary_offsets.append(offset)
            if offset > 0:
                print(f"    Segment {seg_idx+1}: display-order offset detected: "
                      f"+{offset} frames (effective start: {start_frame+offset})")

            cut_offset_time += seg_frames / fps
```

with:

```python
        for seg_idx, (start_frame, end_frame) in enumerate(cuts):
            seg_frames = end_frame - start_frame + 1
            ref_start = frames_to_seconds(start_frame, fps, extra_frames)

            offset = _detect_boundary_offset(
                ref_mkv, cut_mkv, ref_start, cut_offset_time,
                fps, tmpdir, seg_idx)
            boundary_offsets.append(offset)
            if offset > 0:
                print(f"    Segment {seg_idx+1}: display-order offset detected: "
                      f"+{offset} frames (effective start: {start_frame+offset})")

            cut_offset_time += frames_to_seconds(end_frame + 1, fps, extra_frames) \
                             - frames_to_seconds(start_frame, fps, extra_frames)
```

- [ ] **Step 3: Correct time calculations in Step 3 (comparison positions)**

Replace lines 622-665:

```python
        cut_offset = 0.0  # cumulative time offset in cut MKV
        for seg_idx, (start_frame, end_frame) in enumerate(cuts):
            seg_frames = end_frame - start_frame + 1
            seg_dur = seg_frames / fps
            disp_offset = boundary_offsets[seg_idx]

            # Effective start in reference MKV (adjusted for display-order offset)
            eff_start_frame = start_frame + disp_offset
            ref_eff_start = eff_start_frame / fps

            # Effective segment duration in cut MKV
            eff_seg_frames = end_frame - eff_start_frame + 1

            # Re-encoded: start + 1 frame (only if offset == 0, i.e. re-encoding happened)
            if disp_offset == 0 and seg_frames > 2:
                re_encoded_positions.append((
                    ref_eff_start + 1/fps,
                    cut_offset + 1/fps
                ))
            elif disp_offset > 0:
                re_encoded_skipped += 1

            # Stream-copy: start + 20 frames (relative to effective start)
            if eff_seg_frames > 25:
                stream_copy_positions.append((
                    ref_eff_start + 20/fps,
                    cut_offset + 20/fps
                ))

            # Stream-copy: mid
            mid_frame_offset = eff_seg_frames // 2
            stream_copy_positions.append((
                ref_eff_start + mid_frame_offset/fps,
                cut_offset + mid_frame_offset/fps
            ))

            # Stream-copy: end - 1 frame
            if eff_seg_frames > 2:
                ref_end = end_frame / fps
                stream_copy_positions.append((
                    ref_end - 1/fps,
                    cut_offset + (eff_seg_frames - 2)/fps
                ))

            cut_offset += seg_dur
```

with:

```python
        cut_offset = 0.0  # cumulative time offset in cut MKV
        for seg_idx, (start_frame, end_frame) in enumerate(cuts):
            seg_dur = frames_to_seconds(end_frame + 1, fps, extra_frames) \
                    - frames_to_seconds(start_frame, fps, extra_frames)
            disp_offset = boundary_offsets[seg_idx]

            # Effective start in reference MKV (adjusted for display-order offset)
            eff_start_frame = start_frame + disp_offset
            ref_eff_start = frames_to_seconds(eff_start_frame, fps, extra_frames)

            # Effective segment duration in cut MKV
            seg_frames = end_frame - start_frame + 1
            eff_seg_frames = end_frame - eff_start_frame + 1

            # Re-encoded: start + 1 frame (only if offset == 0, i.e. re-encoding happened)
            if disp_offset == 0 and seg_frames > 2:
                re_encoded_positions.append((
                    ref_eff_start + 1/fps,
                    cut_offset + 1/fps
                ))
            elif disp_offset > 0:
                re_encoded_skipped += 1

            # Stream-copy: start + 20 frames (relative to effective start)
            if eff_seg_frames > 25:
                stream_copy_positions.append((
                    ref_eff_start + 20/fps,
                    cut_offset + 20/fps
                ))

            # Stream-copy: mid
            mid_frame_offset = eff_seg_frames // 2
            stream_copy_positions.append((
                ref_eff_start + mid_frame_offset/fps,
                cut_offset + mid_frame_offset/fps
            ))

            # Stream-copy: end - 1 frame
            if eff_seg_frames > 2:
                ref_end = frames_to_seconds(end_frame, fps, extra_frames)
                stream_copy_positions.append((
                    ref_end - 1/fps,
                    cut_offset + (eff_seg_frames - 2)/fps
                ))

            cut_offset += seg_dur
```

- [ ] **Step 4: Update caller in `main()`**

Change line 1196 from:

```python
            results = test_visual_comparison(args.video, args.cut, fps, cuts, tmpdir)
```

to:

```python
            results = test_visual_comparison(args.video, args.cut, fps, cuts, tmpdir,
                                             extra_frames)
```

(The variable `extra_frames` will be defined in Task 5.)

- [ ] **Step 5: Commit**

```bash
git add tools/ttcut-quality-check/ttcut-quality-check.py
git commit -m "Apply extra frame correction to visual comparison test

Use frames_to_seconds() with extra_frames for all reference and cut
position calculations in test_visual_comparison()."
```

---

### Task 3: Thread Extra Frames Through Test 5 (A/V Sync) and Test 6 (Audio Waveform)

**Files:**
- Modify: `tools/ttcut-quality-check/ttcut-quality-check.py` (lines 803-950, 957-1064)

- [ ] **Step 1: Add `extra_frames` parameter to `test_av_sync()`**

Change line 803 from:

```python
def test_av_sync(original_es: str, audio_file: str, cut_mkv: str,
                 fps: float, cuts: list[tuple[int, int]], tmpdir: str) -> TestResult:
```

to:

```python
def test_av_sync(original_es: str, audio_file: str, cut_mkv: str,
                 fps: float, cuts: list[tuple[int, int]], tmpdir: str,
                 extra_frames: list = None) -> TestResult:
```

- [ ] **Step 2: Correct time calculations in `test_av_sync()`**

Replace lines 828-831:

```python
        # Use the first segment, limited to 30 seconds for speed
        first_start = cuts[0][0] / fps
        first_end = cuts[0][1] / fps
        seg_dur = min(first_end - first_start, 30.0)
```

with:

```python
        # Use the first segment, limited to 30 seconds for speed
        first_start = frames_to_seconds(cuts[0][0], fps, extra_frames)
        first_end = frames_to_seconds(cuts[0][1], fps, extra_frames)
        seg_dur = min(first_end - first_start, 30.0)
```

- [ ] **Step 3: Add `extra_frames` parameter to `test_audio_waveform()`**

Change line 957 from:

```python
def test_audio_waveform(cut_mkv: str, cuts: list[tuple[int, int]],
                        fps: float, tmpdir: str) -> TestResult:
```

to:

```python
def test_audio_waveform(cut_mkv: str, cuts: list[tuple[int, int]],
                        fps: float, tmpdir: str,
                        extra_frames: list = None) -> TestResult:
```

- [ ] **Step 4: Correct time calculation in `test_audio_waveform()`**

Replace lines 975-977:

```python
        cumulative = 0.0
        for i, (start, end) in enumerate(cuts):
            seg_dur = (end - start + 1) / fps
```

with:

```python
        cumulative = 0.0
        for i, (start, end) in enumerate(cuts):
            seg_dur = frames_to_seconds(end + 1, fps, extra_frames) \
                    - frames_to_seconds(start, fps, extra_frames)
```

- [ ] **Step 5: Update callers in `main()`**

Change line 1207 from:

```python
            r = test_av_sync(args.video, args.audio, args.cut, fps, cuts, tmpdir)
```

to:

```python
            r = test_av_sync(args.video, args.audio, args.cut, fps, cuts, tmpdir,
                             extra_frames)
```

Change line 1213 from:

```python
            r = test_audio_waveform(args.cut, cuts, fps, tmpdir)
```

to:

```python
            r = test_audio_waveform(args.cut, cuts, fps, tmpdir, extra_frames)
```

- [ ] **Step 6: Commit**

```bash
git add tools/ttcut-quality-check/ttcut-quality-check.py
git commit -m "Apply extra frame correction to A/V sync and waveform tests

Use frames_to_seconds() with extra_frames in test_av_sync() and
test_audio_waveform() for correct segment time calculations."
```

---

### Task 4: Add Defect Region Report Test

New test that reports which defective frames are in the final cut and groups them into regions.

**Files:**
- Modify: `tools/ttcut-quality-check/ttcut-quality-check.py` (insert before main, extend ALL_TESTS, extend main)

- [ ] **Step 1: Add `parse_ttcut_settings()` helper**

Insert after `count_extras_before()` (after the function added in Task 1):

```python
def parse_ttcut_settings() -> dict:
    """Read defect region settings from TTCut-ng config file."""
    result = {"defect_gap_sec": 5, "defect_offset_sec": 2}
    conf = Path.home() / ".config" / "TTCut-ng" / "TTCut-ng.conf"
    if not conf.exists():
        return result
    section = None
    with open(conf) as f:
        for line in f:
            line = line.strip()
            m = re.match(r"\[(.+)\]", line)
            if m:
                section = m.group(1)
                continue
            if section == "Common" and "=" in line:
                key, val = line.split("=", 1)
                key = key.strip()
                val = val.strip()
                if key == "ExtraFrameClusterGap":
                    result["defect_gap_sec"] = int(val)
                elif key == "ExtraFrameClusterOffset":
                    result["defect_offset_sec"] = int(val)
    return result
```

- [ ] **Step 2: Add `test_defect_regions()` function**

Insert before `# Main` section (before `ALL_TESTS`):

```python
# ---------------------------------------------------------------------------
# Test 7: Defect Region Report
# ---------------------------------------------------------------------------

def test_defect_regions(cuts: list[tuple[int, int]], fps: float,
                        extra_frames: list, defect_gap_sec: int,
                        defect_offset_sec: int) -> TestResult:
    """Report defective frames within cut segments, grouped into regions."""
    name = "Defect Regions"

    if not extra_frames:
        return TestResult(name=name, passed=True, warn=False,
                          details="no extra frames in .info (clean stream)")

    # Filter extra frames that fall within any cut segment
    in_cut = []
    for ef in extra_frames:
        for start, end in cuts:
            if start <= ef <= end:
                in_cut.append(ef)
                break

    if not in_cut:
        return TestResult(name=name, passed=True, warn=False,
                          details="no defective frames within cut segments")

    # Group into regions: consecutive frames within defect_gap_sec
    gap_frames = int(defect_gap_sec * fps)
    regions = []
    region_start = in_cut[0]
    region_end = in_cut[0]

    for ef in in_cut[1:]:
        if ef - region_end <= gap_frames:
            region_end = ef
        else:
            regions.append((region_start, region_end))
            region_start = ef
            region_end = ef
    regions.append((region_start, region_end))

    # Format output
    lines = []
    for i, (rs, re_) in enumerate(regions):
        n_frames = sum(1 for ef in in_cut if rs <= ef <= re_)
        dur = (re_ - rs + 1) / fps
        # Position in cut MKV: cumulative time up to this frame
        cut_time = 0.0
        for start, end in cuts:
            if rs >= start:
                offset_in_seg = frames_to_seconds(min(rs, end), fps, extra_frames) \
                              - frames_to_seconds(start, fps, extra_frames)
                cut_time += offset_in_seg
                break
            cut_time += frames_to_seconds(end + 1, fps, extra_frames) \
                      - frames_to_seconds(start, fps, extra_frames)

        minutes = int(cut_time) // 60
        seconds = int(cut_time) % 60
        lines.append(
            f"Region {i+1}: frames {rs}-{re_} "
            f"({n_frames} frames, {dur:.1f}s) at {minutes:02d}:{seconds:02d}"
        )

    settings_source = "defaults"
    conf = Path.home() / ".config" / "TTCut-ng" / "TTCut-ng.conf"
    if conf.exists():
        settings_source = str(conf)

    detail_header = (
        f"{len(in_cut)} defective frames in {len(regions)} regions "
        f"within cut segments"
    )
    detail_body = "\n       ".join(lines)
    detail_footer = (
        f"Settings: defect-gap={defect_gap_sec}s, "
        f"defect-offset={defect_offset_sec}s (from {settings_source})"
    )
    details = f"{detail_header}\n       {detail_body}\n       {detail_footer}"

    return TestResult(name=name, passed=True, warn=False,
                      value=len(in_cut), details=details)
```

- [ ] **Step 3: Add "defects" to `ALL_TESTS`**

Change line 1071 from:

```python
ALL_TESTS = ["metadata", "timing", "duration", "visual", "avsync", "waveform"]
```

to:

```python
ALL_TESTS = ["metadata", "timing", "duration", "visual", "avsync", "waveform", "defects"]
```

- [ ] **Step 4: Add CLI parameters for defect region settings**

After line 1103 (`--tmpdir` argument), add:

```python
    parser.add_argument("--defect-gap", type=int, default=None,
                        help="Defect region grouping gap in seconds "
                             "(default: from TTCut-ng settings or 5)")
    parser.add_argument("--defect-offset", type=int, default=None,
                        help="Defect region start offset in seconds "
                             "(default: from TTCut-ng settings or 2)")
```

- [ ] **Step 5: Commit**

```bash
git add tools/ttcut-quality-check/ttcut-quality-check.py
git commit -m "Add defect region report test to quality check

New test groups extra frames within cut segments into defect regions.
Reads clustering settings from TTCut-ng config file with CLI override."
```

---

### Task 5: Wire Up Extra Frames and Defect Regions in `main()`

Connect all the pieces in the main function: read extra frames, print header, run defect report.

**Files:**
- Modify: `tools/ttcut-quality-check/ttcut-quality-check.py` (lines 1117-1215)

- [ ] **Step 1: Read extra frames and defect settings after info parsing**

Replace lines 1117-1126:

```python
    # Parse .info if provided
    fps = args.fps
    if args.info:
        info_data = parse_info_file(args.info)
        if info_data["fps"] is not None:
            fps = info_data["fps"]

    if fps is None:
        print("Error: --fps is required (or provide --info with frame_rate)", file=sys.stderr)
        sys.exit(1)
```

with:

```python
    # Parse .info if provided
    fps = args.fps
    extra_frames = []
    if args.info:
        info_data = parse_info_file(args.info)
        if info_data["fps"] is not None:
            fps = info_data["fps"]
        extra_frames = info_data.get("extra_frames", [])

    if fps is None:
        print("Error: --fps is required (or provide --info with frame_rate)", file=sys.stderr)
        sys.exit(1)

    # Defect region settings: CLI > TTCut-ng config > defaults
    ttcut_settings = parse_ttcut_settings()
    defect_gap_sec = args.defect_gap if args.defect_gap is not None \
        else ttcut_settings["defect_gap_sec"]
    defect_offset_sec = args.defect_offset if args.defect_offset is not None \
        else ttcut_settings["defect_offset_sec"]
```

- [ ] **Step 2: Add extra frames info to report header**

After line 1172 (`print(f"Tmpdir: {tmpdir}")`), add:

```python
        if extra_frames:
            print(f"Extra Frames: {len(extra_frames)} (from .info, audio time correction active)")
```

- [ ] **Step 3: Add defect regions test runner**

After the waveform test block (after the `if "waveform" in selected:` block), add:

```python
        if "defects" in selected and extra_frames:
            print("Running: Defect Regions...", flush=True)
            r = test_defect_regions(cuts, fps, extra_frames,
                                    defect_gap_sec, defect_offset_sec)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")
```

- [ ] **Step 4: Verify syntax**

Run:

```bash
python3 tools/ttcut-quality-check/ttcut-quality-check.py --help
```

Expected: Help text shows new `--defect-gap` and `--defect-offset` parameters, and "defects" in the test list.

- [ ] **Step 5: Commit**

```bash
git add tools/ttcut-quality-check/ttcut-quality-check.py
git commit -m "Wire up extra frame correction and defect regions in main

Read extra_frames from .info, pass to all test functions. Read defect
region settings from TTCut-ng config with CLI override fallback.
Print extra frame count in report header."
```

---

### Task 6: End-to-End Verification

Test with the defective MPEG-2 recording.

**Files:**
- No file changes

- [ ] **Step 1: Run quality check with .info (A/V sync only, fast)**

```bash
python3 tools/ttcut-quality-check/ttcut-quality-check.py \
  --video "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
  --audio "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2_deu.mp2" \
  --cut <CUT_MKV_PATH> \
  --cuts "<CUT_RANGES>" \
  --info "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.info" \
  --tests avsync,defects \
  --keep-tmpdir
```

Expected:
- A/V Sync: offset should be ≤50ms (PASS), not -428ms
- Defect Regions: INFO with grouped regions and frame ranges
- Extra Frames header line shows count

Note: `<CUT_MKV_PATH>` and `<CUT_RANGES>` must be filled in by the user — depends on which cut was previously made with TTCut-ng.

- [ ] **Step 2: Run quality check without .info (regression check)**

```bash
python3 tools/ttcut-quality-check/ttcut-quality-check.py \
  --video "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2.m2v" \
  --audio "/media/Daten/Video_Tmp/ProjectX_Temp/04x03_-_Geschichten_von_Interesse_Nr._2_deu.mp2" \
  --cut <CUT_MKV_PATH> \
  --cuts "<CUT_RANGES>" \
  --fps 25 \
  --tests metadata,timing,duration
```

Expected: Results identical to before (no regression in tests that don't use extra frames).

- [ ] **Step 3: Commit (only if changes were needed)**

If any fixes were needed during verification, commit them.
