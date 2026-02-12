#!/usr/bin/env python3
"""
ttcut-quality-check.py - Smart Cut Quality Test Suite for TTCut-ng

Objective measurement of cut quality after Smart Cut (H.264/H.265/MPEG-2).
Runs up to 7 automated tests on a cut MKV and reports PASS/WARN/FAIL for each.

TESTS
=====

  1. Stream Metadata    - Verifies codec, frame rate, and frame count match
                          the expected values from the cut segments.

  2. PTS Consistency    - Reads all video and audio packets from the cut MKV
                          and checks that PTS intervals are uniform (tolerance
                          0.5ms). Detects timestamp jumps or gaps.

  3. Duration Match     - Compares video vs audio duration in the MKV.
                          Pass threshold: difference <= 50ms.

  4. Visual Comparison  - Extracts frames from a reference MKV (original ES
     (re-encoded)         wrapped with mkvmerge) and from the cut MKV at the
     (stream-copy)        same content positions, then computes SSIM.
                          Stream-copy frames should be near-identical (>=0.99).
                          Re-encoded frames have expected quality loss (>=0.50).
                          Uses -copyts + select filter for frame-accurate
                          extraction (avoids keyframe-snapping issues).

  5. A/V Sync           - Creates a reference MKV from original ES + audio,
                          extracts the first segment from both reference and
                          cut, then cross-correlates the audio tracks.
                          Uses syncstart if installed, otherwise numpy FFT.
                          Pass threshold: offset <= 50ms. Warn up to 150ms.

  6. Audio Waveform     - Extracts audio around each internal cut boundary
                          and checks for clicks/pops (impulse noise).
                          A click is a 1ms spike >30dB from both neighbors.
                          Natural level changes (program switch) are reported
                          as info but do not trigger failure.

WORKFLOW
========

  1. Cut your video with TTCut-ng (produces an MKV)
  2. Note the cut-in/cut-out frame numbers from TTCut's cut list
  3. Run this tool with the original ES, audio, cut MKV, and frame ranges
  4. Review the report — especially A/V Sync offset and Visual SSIM values

  The --cuts parameter uses TTCut's frame numbers (0-based, inclusive):
    "5127-24821"             single segment: frames 5127 through 24821
    "5127-24821,33328-42679" two segments (typical: remove one ad break)

DEPENDENCIES
============

  Required: ffmpeg (8.0+), ffprobe, mkvmerge, python3 (3.8+)
  Optional: numpy          - enables A/V sync (cross-correlation) and
                             audio waveform analysis
            syncstart       - more accurate A/V sync (pip install syncstart)

  Without numpy, tests 5 and 6 are skipped with WARN status.

DISK SPACE
==========

  Tests 4 and 5 create a reference MKV from the original ES (roughly the
  same size as the input file). Ensure sufficient free space in the temp
  directory. Use --tmpdir to redirect to a larger filesystem if needed.

USAGE
=====

  # Minimal (specify fps directly):
  python3 ttcut-quality-check.py \\
    --video original.265 --audio original.mp2 \\
    --cut cut.mkv --cuts "5127-24821,33328-42679" --fps 50

  # With .info file (reads fps automatically):
  python3 ttcut-quality-check.py \\
    --video original.265 --audio original.mp2 \\
    --cut cut.mkv --cuts "5127-24821,33328-42679" \\
    --info original.info

  # Run only specific tests:
  python3 ttcut-quality-check.py ... --tests metadata,timing,duration

  # Save machine-readable JSON report:
  python3 ttcut-quality-check.py ... --json report.json

  # Keep temp files for debugging:
  python3 ttcut-quality-check.py ... --keep-tmpdir

EXIT CODES
==========

  0 = all tests passed
  1 = at least one WARN (no FAIL)
  2 = at least one FAIL

EXAMPLE OUTPUT
==============

  === TTCut-ng Smart Cut Quality Report ===
  Test Material: Ausdrucksstarke_Designermode.265
  Cut Segments: 5127-24821,33328-42679
  Frame Rate: 50.0 fps

  [PASS] Stream Metadata: 29047 frames, 50.0fps, hevc
  [PASS] PTS Consistency: 0 anomalies in 29047 video + 24205 audio packets
  [PASS] Duration Match: video=580.940s, audio=580.920s, diff=0.020s
  [PASS] Visual (re-encoded): SSIM=0.608 min=0.582 (2 positions checked)
  [PASS] Visual (stream-copy): SSIM=0.998 min=0.990 (6 positions checked)
  [WARN] A/V Sync: offset=+90ms (method: numpy cross-correlation)
  [PASS] Audio Waveform: no clicks at 1 cut boundaries

  Result: 6/7 PASS, 1 WARN, 0 FAIL
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Optional


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class TestResult:
    name: str
    passed: bool
    value: Any = None
    expected: str = ""
    details: str = ""
    warn: bool = False  # True = soft failure (WARN instead of FAIL)

    def status_str(self) -> str:
        if self.passed:
            return "PASS"
        return "WARN" if self.warn else "FAIL"


@dataclass
class QualityReport:
    video_file: str = ""
    cut_file: str = ""
    cut_segments: str = ""
    fps: float = 0.0
    tests: list = field(default_factory=list)

    def summary(self) -> str:
        lines = []
        lines.append("=== TTCut-ng Smart Cut Quality Report ===")
        lines.append(f"Test Material: {os.path.basename(self.video_file)}")
        lines.append(f"Cut Segments: {self.cut_segments}")
        lines.append(f"Frame Rate: {self.fps} fps")
        lines.append("")
        n_pass = n_warn = n_fail = 0
        for t in self.tests:
            tag = t.status_str()
            if tag == "PASS":
                n_pass += 1
            elif tag == "WARN":
                n_warn += 1
            else:
                n_fail += 1
            detail = t.details if t.details else str(t.value)
            lines.append(f"[{tag}] {t.name}: {detail}")
        lines.append("")
        total = len(self.tests)
        lines.append(f"Result: {n_pass}/{total} PASS, {n_warn} WARN, {n_fail} FAIL")
        return "\n".join(lines)

    def to_json(self) -> dict:
        tests = []
        for t in self.tests:
            td = asdict(t)
            # Convert numpy types to native Python for JSON serialization
            for k, v in td.items():
                if hasattr(v, "item"):
                    td[k] = v.item()
                elif isinstance(v, bool):
                    pass  # already native
                elif isinstance(v, (int, float, str, type(None))):
                    pass  # already native
            tests.append(td)
        return {
            "video_file": self.video_file,
            "cut_file": self.cut_file,
            "cut_segments": self.cut_segments,
            "fps": self.fps,
            "tests": tests,
        }


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run_cmd(cmd: list[str], capture=True, timeout=120) -> subprocess.CompletedProcess:
    """Run external command, return CompletedProcess."""
    return subprocess.run(
        cmd, capture_output=capture, text=True, timeout=timeout
    )


def ffprobe_json(filepath: str, *extra_args) -> dict:
    """Run ffprobe and return parsed JSON."""
    cmd = [
        "ffprobe", "-v", "quiet", "-print_format", "json",
        *extra_args, filepath
    ]
    r = run_cmd(cmd)
    if r.returncode != 0:
        raise RuntimeError(f"ffprobe failed on {filepath}: {r.stderr}")
    return json.loads(r.stdout)


def ffprobe_packets(filepath: str, stream_type: str) -> list[dict]:
    """Read all packets for a given stream type (video/audio) from a file."""
    cmd = [
        "ffprobe", "-v", "quiet", "-print_format", "json",
        "-select_streams", "v:0" if stream_type == "video" else "a:0",
        "-show_packets", filepath
    ]
    r = run_cmd(cmd, timeout=300)
    if r.returncode != 0:
        raise RuntimeError(f"ffprobe packets failed: {r.stderr}")
    data = json.loads(r.stdout)
    return data.get("packets", [])


def parse_info_file(info_path: str) -> dict:
    """Parse TTCut .info file, return dict with fps, avOffset etc."""
    result = {"fps": None, "av_offset_ms": 0}
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
    return result


def parse_cuts(cuts_str: str) -> list[tuple[int, int]]:
    """Parse '5127-24821,33328-42679' into list of (start_frame, end_frame)."""
    segments = []
    for part in cuts_str.split(","):
        part = part.strip()
        if "-" not in part:
            raise ValueError(f"Invalid cut segment: {part}")
        a, b = part.split("-", 1)
        segments.append((int(a), int(b)))
    return segments


def frames_to_seconds(frame: int, fps: float) -> float:
    return frame / fps


def which(program: str) -> bool:
    return shutil.which(program) is not None


# ---------------------------------------------------------------------------
# Test 1: Stream Metadata
# ---------------------------------------------------------------------------

def test_metadata(original_es: str, cut_mkv: str, fps: float,
                  cuts: list[tuple[int, int]]) -> TestResult:
    """Verify codec, fps, and frame count in the cut MKV."""
    name = "Stream Metadata"
    try:
        # Original ES info
        orig_info = ffprobe_json(original_es, "-show_streams", "-select_streams", "v:0")
        orig_stream = orig_info["streams"][0]
        orig_codec = orig_stream.get("codec_name", "unknown")

        # Cut MKV info
        cut_info = ffprobe_json(cut_mkv, "-show_streams", "-select_streams", "v:0")
        cut_stream = cut_info["streams"][0]
        cut_codec = cut_stream.get("codec_name", "unknown")

        # FPS from cut MKV
        r_fps_str = cut_stream.get("r_frame_rate", "0/1")
        if "/" in r_fps_str:
            num, den = r_fps_str.split("/")
            cut_fps = int(num) / max(int(den), 1)
        else:
            cut_fps = float(r_fps_str)

        # Frame count: count packets since nb_frames is unreliable for MKV
        cut_packets = ffprobe_packets(cut_mkv, "video")
        cut_frames = len(cut_packets)

        expected_frames = sum(end - start + 1 for start, end in cuts)

        details_parts = []
        ok = True

        # Codec check
        if orig_codec != cut_codec:
            details_parts.append(f"codec mismatch: orig={orig_codec}, cut={cut_codec}")
            ok = False

        # FPS check (tolerance 0.5 fps)
        if abs(cut_fps - fps) > 0.5:
            details_parts.append(f"fps mismatch: expected={fps}, cut={cut_fps:.3f}")
            ok = False

        # Frame count check (tolerance ±5 frames for encoder overhead)
        frame_diff = abs(cut_frames - expected_frames)
        if frame_diff > 5:
            details_parts.append(
                f"frame count: expected={expected_frames}, got={cut_frames} (diff={frame_diff})"
            )
            ok = False

        if ok:
            details = f"{cut_frames} frames, {cut_fps}fps, {cut_codec}"
        else:
            details = "; ".join(details_parts)

        return TestResult(name=name, passed=ok, value=cut_frames,
                          expected=f"{expected_frames} frames, {fps}fps, {orig_codec}",
                          details=details)
    except Exception as e:
        return TestResult(name=name, passed=False, details=f"Error: {e}")


# ---------------------------------------------------------------------------
# Test 2: PTS Consistency
# ---------------------------------------------------------------------------

def test_pts_consistency(cut_mkv: str) -> TestResult:
    """Check PTS intervals for anomalies in video and audio streams."""
    name = "PTS Consistency"
    try:
        anomalies_total = 0
        details_parts = []

        for stream_type in ("video", "audio"):
            packets = ffprobe_packets(cut_mkv, stream_type)
            if not packets:
                details_parts.append(f"no {stream_type} packets found")
                anomalies_total += 1
                continue

            # Extract and sort PTS values
            pts_values = []
            for p in packets:
                pts_str = p.get("pts_time")
                if pts_str is not None:
                    pts_values.append(float(pts_str))

            if len(pts_values) < 2:
                details_parts.append(f"{stream_type}: insufficient PTS values ({len(pts_values)})")
                continue

            pts_values.sort()

            # Compute intervals
            intervals = [pts_values[i+1] - pts_values[i] for i in range(len(pts_values)-1)]

            if not intervals:
                continue

            # Determine expected interval (median)
            sorted_intervals = sorted(intervals)
            median_interval = sorted_intervals[len(sorted_intervals) // 2]

            # Count anomalies: intervals deviating > 0.5ms from median
            # (but allow the last packet to differ — container overhead)
            tolerance = 0.0005  # 0.5ms
            anomalies = 0
            for iv in intervals:
                if abs(iv - median_interval) > tolerance:
                    anomalies += 1

            anomalies_total += anomalies
            interval_ms = median_interval * 1000
            details_parts.append(
                f"{anomalies} anomalies in {len(pts_values)} {stream_type} packets "
                f"(interval={interval_ms:.1f}ms)"
            )

        passed = anomalies_total == 0
        details = "; ".join(details_parts)
        return TestResult(name=name, passed=passed, value=anomalies_total,
                          expected="0 anomalies", details=details)
    except Exception as e:
        return TestResult(name=name, passed=False, details=f"Error: {e}")


# ---------------------------------------------------------------------------
# Test 3: Duration Match
# ---------------------------------------------------------------------------

def test_duration_match(cut_mkv: str) -> TestResult:
    """Check that video and audio duration match within 50ms."""
    name = "Duration Match"
    try:
        info = ffprobe_json(cut_mkv, "-show_streams")
        streams = info.get("streams", [])
        video_dur = audio_dur = None
        for s in streams:
            ct = s.get("codec_type")
            dur = s.get("duration")
            if dur is None:
                # Fallback: compute from packets
                continue
            dur = float(dur)
            if ct == "video" and video_dur is None:
                video_dur = dur
            elif ct == "audio" and audio_dur is None:
                audio_dur = dur

        # Fallback: read duration from packets if stream duration unavailable
        if video_dur is None:
            pkts = ffprobe_packets(cut_mkv, "video")
            if pkts:
                pts = [float(p["pts_time"]) for p in pkts if p.get("pts_time")]
                if pts:
                    dur_p = [float(p.get("duration_time", "0")) for p in pkts if p.get("duration_time")]
                    last_dur = dur_p[-1] if dur_p else 0
                    video_dur = max(pts) + last_dur

        if audio_dur is None:
            pkts = ffprobe_packets(cut_mkv, "audio")
            if pkts:
                pts = [float(p["pts_time"]) for p in pkts if p.get("pts_time")]
                if pts:
                    dur_p = [float(p.get("duration_time", "0")) for p in pkts if p.get("duration_time")]
                    last_dur = dur_p[-1] if dur_p else 0
                    audio_dur = max(pts) + last_dur

        if video_dur is None or audio_dur is None:
            return TestResult(name=name, passed=False,
                              details=f"Could not determine duration (video={video_dur}, audio={audio_dur})")

        diff = abs(video_dur - audio_dur)
        threshold = 0.050  # 50ms
        passed = diff <= threshold
        details = f"video={video_dur:.3f}s, audio={audio_dur:.3f}s, diff={diff:.3f}s"
        return TestResult(name=name, passed=passed, value=diff,
                          expected=f"diff <= {threshold*1000:.0f}ms", details=details,
                          warn=not passed and diff <= 0.100)

    except Exception as e:
        return TestResult(name=name, passed=False, details=f"Error: {e}")


# ---------------------------------------------------------------------------
# Test 4: Visual Comparison (SSIM)
# ---------------------------------------------------------------------------

def _extract_frame(input_file: str, time_sec: float, output_png: str) -> bool:
    """Extract a single frame at the given time position.

    Uses -copyts + PTS-based select filter for frame-accurate extraction:
    1. Input-seek to (time - 15s) for speed (keyframe-based)
    2. -copyts preserves original timestamps through the pipeline
    3. select='gte(t, TARGET)' picks the first frame at the exact PTS

    This avoids both keyframe-snapping (plain input-seek) and decoding
    from the start (pure output-seek, too slow for 4K HEVC).
    """
    pre_seek = max(0, time_sec - 15.0)

    cmd = [
        "ffmpeg", "-y", "-v", "quiet",
        "-ss", f"{pre_seek:.4f}",
        "-copyts",
        "-i", input_file,
        "-vf", f"select='gte(t, {time_sec:.4f})'",
        "-update", "1",
        "-frames:v", "1",
        "-fps_mode", "vfr",
        "-f", "image2",
        output_png
    ]
    r = run_cmd(cmd, timeout=60)
    return r.returncode == 0 and os.path.exists(output_png)


def _compute_ssim(ref_png: str, cut_png: str) -> Optional[float]:
    """Compute SSIM between two images using ffmpeg."""
    cmd = [
        "ffmpeg", "-v", "quiet",
        "-i", ref_png, "-i", cut_png,
        "-lavfi", "ssim=stats_file=-",
        "-f", "null", "-"
    ]
    r = run_cmd(cmd, timeout=30)
    # Parse SSIM from stderr (ffmpeg outputs stats there)
    output = r.stderr + r.stdout
    m = re.search(r"All:([0-9.]+)", output)
    if m:
        return float(m.group(1))
    return None


def test_visual_comparison(original_es: str, cut_mkv: str, fps: float,
                           cuts: list[tuple[int, int]], tmpdir: str) -> TestResult:
    """Extract frames from reference and cut MKV, compare with SSIM."""
    name_sc = "Visual (stream-copy)"
    name_re = "Visual (re-encoded)"

    try:
        # Step 1: Create reference MKV from original ES
        ref_mkv = os.path.join(tmpdir, "reference.mkv")
        frame_dur = f"{round(1_000_000_000 / fps)}ns"
        cmd = [
            "mkvmerge", "-o", ref_mkv,
            "--default-duration", f"0:{frame_dur}",
            original_es
        ]
        r = run_cmd(cmd, timeout=120)
        if r.returncode not in (0, 1):  # mkvmerge returns 1 for warnings
            return TestResult(name="Visual Comparison", passed=False,
                              details=f"mkvmerge failed: {r.stderr[:200]}")

        # Step 2: Define comparison positions
        # For each segment: start+1 (re-encoded), start+20 (stream-copy),
        #                    mid (stream-copy), end-1 (stream-copy)
        re_encoded_positions = []  # (ref_time, cut_time)
        stream_copy_positions = []

        cut_offset = 0.0  # cumulative time offset in cut MKV
        for seg_idx, (start_frame, end_frame) in enumerate(cuts):
            seg_frames = end_frame - start_frame + 1
            seg_dur = seg_frames / fps

            # Times in reference MKV (= original timestamps)
            ref_start = start_frame / fps
            ref_end = end_frame / fps

            # Re-encoded: start + 1 frame
            if seg_frames > 2:
                re_encoded_positions.append((
                    ref_start + 1/fps,
                    cut_offset + 1/fps
                ))

            # Stream-copy: start + 20 frames
            if seg_frames > 25:
                stream_copy_positions.append((
                    ref_start + 20/fps,
                    cut_offset + 20/fps
                ))

            # Stream-copy: mid
            mid_frame_offset = seg_frames // 2
            stream_copy_positions.append((
                ref_start + mid_frame_offset/fps,
                cut_offset + mid_frame_offset/fps
            ))

            # Stream-copy: end - 1 frame
            if seg_frames > 2:
                stream_copy_positions.append((
                    ref_end - 1/fps,
                    cut_offset + (seg_frames - 2)/fps
                ))

            cut_offset += seg_dur

        # Step 3: Extract and compare
        results = []  # list of TestResult

        for label, positions, threshold in [
            (name_re, re_encoded_positions, 0.50),
            (name_sc, stream_copy_positions, 0.99),
        ]:
            ssim_values = []
            for i, (ref_time, cut_time) in enumerate(positions):
                ref_png = os.path.join(tmpdir, f"{label.replace(' ', '_')}_{i}_ref.png")
                cut_png = os.path.join(tmpdir, f"{label.replace(' ', '_')}_{i}_cut.png")

                if not _extract_frame(ref_mkv, ref_time, ref_png):
                    continue
                if not _extract_frame(cut_mkv, cut_time, cut_png):
                    continue

                ssim = _compute_ssim(ref_png, cut_png)
                if ssim is not None:
                    ssim_values.append(ssim)

            if not ssim_values:
                results.append(TestResult(
                    name=label, passed=False,
                    details="No frames could be compared"
                ))
                continue

            min_ssim = min(ssim_values)
            avg_ssim = sum(ssim_values) / len(ssim_values)
            passed = min_ssim >= threshold
            results.append(TestResult(
                name=label, passed=passed,
                value=avg_ssim,
                expected=f"SSIM >= {threshold}",
                details=f"SSIM={avg_ssim:.3f} min={min_ssim:.3f} ({len(ssim_values)} positions checked)",
                warn=not passed and min_ssim >= threshold * 0.95
            ))

        return results  # Return list — caller handles this

    except Exception as e:
        return [TestResult(name="Visual Comparison", passed=False, details=f"Error: {e}")]


# ---------------------------------------------------------------------------
# Test 5: A/V Sync
# ---------------------------------------------------------------------------

def _try_syncstart(file_a: str, file_b: str) -> Optional[float]:
    """Try using syncstart to measure A/V offset. Returns offset in seconds or None."""
    if not which("syncstart"):
        return None
    try:
        r = run_cmd(["syncstart", file_a, file_b, "-t", "20", "-q"], timeout=120)
        if r.returncode == 0:
            # syncstart output format: "file,offset"
            for line in r.stdout.strip().splitlines():
                if "," in line:
                    parts = line.rsplit(",", 1)
                    return float(parts[1])
        return None
    except Exception:
        return None


def _try_numpy_correlation(audio_a: str, audio_b: str, tmpdir: str) -> Optional[float]:
    """Fallback: cross-correlate audio with numpy to find offset."""
    try:
        import numpy as np
    except ImportError:
        return None

    try:
        # Extract raw PCM from both files (first 20 seconds)
        pcm_a = os.path.join(tmpdir, "sync_a.raw")
        pcm_b = os.path.join(tmpdir, "sync_b.raw")

        for src, dst in [(audio_a, pcm_a), (audio_b, pcm_b)]:
            cmd = [
                "ffmpeg", "-y", "-v", "quiet",
                "-t", "20",
                "-i", src,
                "-ac", "1", "-ar", "16000",
                "-f", "s16le", "-acodec", "pcm_s16le",
                dst
            ]
            r = run_cmd(cmd, timeout=30)
            if r.returncode != 0:
                return None

        # Load PCM data
        a = np.fromfile(pcm_a, dtype=np.int16).astype(np.float32)
        b = np.fromfile(pcm_b, dtype=np.int16).astype(np.float32)

        if len(a) < 1600 or len(b) < 1600:
            return None

        # Normalize
        a = a / (np.max(np.abs(a)) + 1e-10)
        b = b / (np.max(np.abs(b)) + 1e-10)

        # Cross-correlation via FFT
        n = len(a) + len(b) - 1
        fft_size = 1
        while fft_size < n:
            fft_size <<= 1

        fa = np.fft.rfft(a, fft_size)
        fb = np.fft.rfft(b, fft_size)
        corr = np.fft.irfft(fa * np.conj(fb), fft_size)

        # Find peak
        peak = np.argmax(np.abs(corr))
        if peak > fft_size // 2:
            peak -= fft_size

        offset_sec = peak / 16000.0
        return offset_sec

    except Exception:
        return None


def test_av_sync(original_es: str, audio_file: str, cut_mkv: str,
                 fps: float, cuts: list[tuple[int, int]], tmpdir: str) -> TestResult:
    """Measure A/V sync offset in the cut MKV.

    Method: Create a reference MKV from the original ES+audio, extract the same
    segment from both reference and cut, then cross-correlate the audio tracks.
    A well-synced cut should have near-zero offset vs the reference.

    Extraction uses re-encoding (not stream-copy) to ensure frame-accurate timing.
    """
    name = "A/V Sync"
    try:
        # Create reference MKV with audio
        ref_mkv = os.path.join(tmpdir, "ref_with_audio.mkv")
        frame_dur = f"{round(1_000_000_000 / fps)}ns"
        cmd = [
            "mkvmerge", "-o", ref_mkv,
            "--default-duration", f"0:{frame_dur}",
            original_es, audio_file
        ]
        r = run_cmd(cmd, timeout=120)
        if r.returncode not in (0, 1):
            return TestResult(name=name, passed=False,
                              details=f"mkvmerge reference creation failed: {r.stderr[:200]}")

        # Use the first segment, limited to 30 seconds for speed
        first_start = cuts[0][0] / fps
        first_end = cuts[0][1] / fps
        seg_dur = min(first_end - first_start, 30.0)

        # Extract audio as PCM from both files (re-encoding, not stream-copy!)
        # This avoids keyframe-snapping that causes false offsets
        sample_rate = 16000
        ref_pcm = os.path.join(tmpdir, "ref_audio.raw")
        cut_pcm = os.path.join(tmpdir, "cut_audio.raw")

        # Reference: extract audio starting at the same position as the first cut
        cmd = [
            "ffmpeg", "-y", "-v", "quiet",
            "-i", ref_mkv,
            "-ss", f"{first_start:.4f}", "-t", f"{seg_dur:.4f}",
            "-ac", "1", "-ar", str(sample_rate),
            "-f", "s16le", "-acodec", "pcm_s16le",
            ref_pcm
        ]
        run_cmd(cmd, timeout=120)

        # Cut: first segment starts at time 0
        cmd = [
            "ffmpeg", "-y", "-v", "quiet",
            "-i", cut_mkv,
            "-t", f"{seg_dur:.4f}",
            "-ac", "1", "-ar", str(sample_rate),
            "-f", "s16le", "-acodec", "pcm_s16le",
            cut_pcm
        ]
        run_cmd(cmd, timeout=120)

        # Method A: try syncstart on the MKV segments (needs actual files)
        offset = None
        method = ""

        if which("syncstart"):
            ref_seg = os.path.join(tmpdir, "ref_segment.mkv")
            cut_seg = os.path.join(tmpdir, "cut_segment.mkv")
            # Use re-encoding for accurate extraction
            cmd = [
                "ffmpeg", "-y", "-v", "quiet",
                "-i", ref_mkv,
                "-ss", f"{first_start:.4f}", "-t", f"{seg_dur:.4f}",
                "-c:v", "libx264", "-preset", "ultrafast", "-crf", "28",
                "-c:a", "aac", "-b:a", "128k",
                ref_seg
            ]
            run_cmd(cmd, timeout=120)
            cmd = [
                "ffmpeg", "-y", "-v", "quiet",
                "-i", cut_mkv,
                "-t", f"{seg_dur:.4f}",
                "-c:v", "libx264", "-preset", "ultrafast", "-crf", "28",
                "-c:a", "aac", "-b:a", "128k",
                cut_seg
            ]
            run_cmd(cmd, timeout=120)
            offset = _try_syncstart(ref_seg, cut_seg)
            method = "syncstart"

        # Method B: numpy cross-correlation on PCM audio
        if offset is None:
            try:
                import numpy as np
            except ImportError:
                np = None

            if np is not None and os.path.exists(ref_pcm) and os.path.exists(cut_pcm):
                a = np.fromfile(ref_pcm, dtype=np.int16).astype(np.float32)
                b = np.fromfile(cut_pcm, dtype=np.int16).astype(np.float32)

                if len(a) >= sample_rate and len(b) >= sample_rate:
                    # Normalize
                    a = a / (np.max(np.abs(a)) + 1e-10)
                    b = b / (np.max(np.abs(b)) + 1e-10)

                    # Cross-correlation via FFT
                    n = len(a) + len(b) - 1
                    fft_size = 1
                    while fft_size < n:
                        fft_size <<= 1

                    fa = np.fft.rfft(a, fft_size)
                    fb = np.fft.rfft(b, fft_size)
                    corr = np.fft.irfft(fa * np.conj(fb), fft_size)

                    # Find peak within +/- 1 second range
                    max_lag = sample_rate  # 1 second
                    # Positive lags: first max_lag samples
                    # Negative lags: last max_lag samples
                    search_region = np.concatenate([
                        corr[:max_lag],
                        corr[-max_lag:]
                    ])
                    peak_idx = np.argmax(np.abs(search_region))

                    if peak_idx < max_lag:
                        lag = peak_idx
                    else:
                        lag = -(2 * max_lag - peak_idx)

                    offset = lag / sample_rate
                    method = "numpy cross-correlation"

        if offset is None:
            return TestResult(name=name, passed=False, warn=True,
                              details="Could not measure (install syncstart or numpy)")

        threshold = 0.050  # 50ms
        # Convert numpy types to native Python
        offset_val = float(offset)
        abs_offset = abs(offset_val)
        passed = abs_offset <= threshold
        sign = "+" if offset_val >= 0 else ""
        details = f"offset={sign}{offset_val*1000:.0f}ms (method: {method}, threshold: +/-{threshold*1000:.0f}ms)"
        return TestResult(name=name, passed=passed, value=offset_val,
                          expected=f"offset <= +/-{threshold*1000:.0f}ms",
                          details=details, warn=not passed and abs_offset <= 0.150)

    except Exception as e:
        return TestResult(name=name, passed=False, details=f"Error: {e}")


# ---------------------------------------------------------------------------
# Test 6: Audio Waveform (cut boundary glitch detection)
# ---------------------------------------------------------------------------

def test_audio_waveform(cut_mkv: str, cuts: list[tuple[int, int]],
                        fps: float, tmpdir: str) -> TestResult:
    """Check for audio glitches (clicks/pops) at cut boundaries in the output MKV."""
    name = "Audio Waveform"
    try:
        try:
            import numpy as np
            has_numpy = True
        except ImportError:
            has_numpy = False

        if not has_numpy:
            return TestResult(name=name, passed=False, warn=True,
                              details="Skipped (numpy not installed)")

        # Calculate cut boundary positions in the output MKV
        # Boundaries are where one segment ends and the next begins
        boundary_times = []
        cumulative = 0.0
        for i, (start, end) in enumerate(cuts):
            seg_dur = (end - start + 1) / fps
            if i > 0:
                # This is a boundary: end of previous segment = start of this segment
                boundary_times.append(cumulative)
            cumulative += seg_dur

        if not boundary_times:
            return TestResult(name=name, passed=True,
                              details="No internal cut boundaries (single segment)")

        # Extract audio around each boundary and check for glitches
        sample_rate = 48000
        glitches = []
        boundary_info = []
        window = 0.05  # 50ms around boundary

        for bt_idx, bt in enumerate(boundary_times):
            pcm_file = os.path.join(tmpdir, f"boundary_{bt_idx}.raw")

            # Extract 100ms of audio centered on the boundary
            start_time = max(0, bt - window)
            cmd = [
                "ffmpeg", "-y", "-v", "quiet",
                "-ss", f"{start_time:.4f}", "-t", f"{window * 2:.4f}",
                "-i", cut_mkv,
                "-ac", "1", "-ar", str(sample_rate),
                "-f", "s16le", "-acodec", "pcm_s16le",
                pcm_file
            ]
            r = run_cmd(cmd, timeout=30)
            if r.returncode != 0 or not os.path.exists(pcm_file):
                continue

            data = np.fromfile(pcm_file, dtype=np.int16).astype(np.float32)
            if len(data) < 100:
                continue

            # Compute dB level in short windows (1ms = 48 samples)
            chunk_size = sample_rate // 1000  # 1ms chunks
            n_chunks = len(data) // chunk_size
            if n_chunks < 2:
                continue

            db_levels = []
            for c in range(n_chunks):
                chunk = data[c*chunk_size:(c+1)*chunk_size]
                rms = np.sqrt(np.mean(chunk**2) + 1e-10)
                db = 20 * np.log10(rms / 32768.0 + 1e-10)
                db_levels.append(db)

            # Detect sample-level discontinuities (clicks/pops), not gradual
            # level changes. A program change at the boundary (e.g. ad → content)
            # can cause a natural level difference which is not a glitch.
            #
            # Method: look for single-sample spikes (1ms chunks where the level
            # jumps >30dB from BOTH neighbors = impulse noise / click).
            max_jump = 0.0
            has_click = False
            for c in range(1, len(db_levels) - 1):
                jump_prev = abs(db_levels[c] - db_levels[c-1])
                jump_next = abs(db_levels[c] - db_levels[c+1])
                max_jump = max(max_jump, jump_prev)
                # A click: spike from both sides (not a step change)
                if jump_prev > 30.0 and jump_next > 30.0:
                    has_click = True

            if has_click:
                glitches.append(f"boundary {bt_idx+1} at {bt:.2f}s: click detected ({max_jump:.1f}dB)")
            else:
                # Report level changes for info but don't fail
                boundary_info.append(f"boundary {bt_idx+1} at {bt:.2f}s: max step={max_jump:.1f}dB")

        if glitches:
            passed = False
            details = "; ".join(glitches)
        elif boundary_info:
            passed = True
            details = f"no clicks at {len(boundary_times)} cut boundaries ({'; '.join(boundary_info)})"
        else:
            passed = True
            details = f"no glitches at {len(boundary_times)} cut boundaries"

        return TestResult(name=name, passed=passed, value=len(glitches),
                          expected="0 clicks at boundaries", details=details)

    except Exception as e:
        return TestResult(name=name, passed=False, details=f"Error: {e}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

ALL_TESTS = ["metadata", "timing", "duration", "visual", "avsync", "waveform"]


def main():
    parser = argparse.ArgumentParser(
        description="TTCut-ng Smart Cut Quality Test Suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  %(prog)s --video orig.265 --audio orig.mp2 --cut cut.mkv --cuts "5127-24821,33328-42679" --fps 50
  %(prog)s --video orig.265 --audio orig.mp2 --cut cut.mkv --cuts "5127-24821" --info orig.info
  %(prog)s ... --tests metadata,timing,duration
  %(prog)s ... --json output.json
"""
    )
    parser.add_argument("--video", required=True, help="Original video elementary stream (.265/.264/.m2v)")
    parser.add_argument("--audio", required=True, help="Original audio file (.mp2/.ac3/.aac)")
    parser.add_argument("--cut", required=True, help="Cut output MKV file")
    parser.add_argument("--cuts", required=True,
                        help="Cut segments as frame ranges: 'start-end,start-end,...'")
    parser.add_argument("--fps", type=float, default=None,
                        help="Frame rate (overridden by --info if provided)")
    parser.add_argument("--info", default=None,
                        help="TTCut .info file (reads fps, avOffset automatically)")
    parser.add_argument("--tests", default=None,
                        help=f"Comma-separated list of tests to run (default: all). "
                             f"Available: {','.join(ALL_TESTS)}")
    parser.add_argument("--json", default=None,
                        help="Write JSON report to this file")
    parser.add_argument("--keep-tmpdir", action="store_true",
                        help="Do not delete temporary directory after run")
    parser.add_argument("--tmpdir", default=None,
                        help="Use this temporary directory instead of auto-creating one")

    args = parser.parse_args()

    # Validate required files exist
    for path, label in [(args.video, "--video"), (args.audio, "--audio"), (args.cut, "--cut")]:
        if not os.path.isfile(path):
            print(f"Error: {label} file not found: {path}", file=sys.stderr)
            sys.exit(1)

    if args.info and not os.path.isfile(args.info):
        print(f"Error: --info file not found: {args.info}", file=sys.stderr)
        sys.exit(1)

    # Parse .info if provided
    fps = args.fps
    if args.info:
        info_data = parse_info_file(args.info)
        if info_data["fps"] is not None:
            fps = info_data["fps"]

    if fps is None:
        print("Error: --fps is required (or provide --info with frame_rate)", file=sys.stderr)
        sys.exit(1)

    # Parse cut segments
    try:
        cuts = parse_cuts(args.cuts)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Select tests
    if args.tests:
        selected = [t.strip() for t in args.tests.split(",")]
        for t in selected:
            if t not in ALL_TESTS:
                print(f"Error: unknown test '{t}'. Available: {','.join(ALL_TESTS)}", file=sys.stderr)
                sys.exit(1)
    else:
        selected = ALL_TESTS[:]

    # Check required tools
    for tool in ["ffmpeg", "ffprobe", "mkvmerge"]:
        if not which(tool):
            print(f"Error: required tool '{tool}' not found in PATH", file=sys.stderr)
            sys.exit(1)

    # Temporary directory
    if args.tmpdir:
        tmpdir = args.tmpdir
        os.makedirs(tmpdir, exist_ok=True)
    else:
        tmpdir = tempfile.mkdtemp(prefix="ttcut_qc_",
                                  dir=os.environ.get("TMPDIR", "/usr/local/src/CLAUDE_TMP"))

    try:
        report = QualityReport(
            video_file=args.video,
            cut_file=args.cut,
            cut_segments=args.cuts,
            fps=fps
        )

        print(f"=== TTCut-ng Smart Cut Quality Check ===")
        print(f"Video: {os.path.basename(args.video)}")
        print(f"Cut:   {os.path.basename(args.cut)}")
        print(f"FPS:   {fps}")
        print(f"Cuts:  {args.cuts}")
        print(f"Tmpdir: {tmpdir}")
        print()

        # Run selected tests
        if "metadata" in selected:
            print("Running: Stream Metadata...", flush=True)
            r = test_metadata(args.video, args.cut, fps, cuts)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")

        if "timing" in selected:
            print("Running: PTS Consistency...", flush=True)
            r = test_pts_consistency(args.cut)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")

        if "duration" in selected:
            print("Running: Duration Match...", flush=True)
            r = test_duration_match(args.cut)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")

        if "visual" in selected:
            print("Running: Visual Comparison...", flush=True)
            results = test_visual_comparison(args.video, args.cut, fps, cuts, tmpdir)
            if isinstance(results, list):
                for r in results:
                    report.tests.append(r)
                    print(f"  [{r.status_str()}] {r.details}")
            else:
                report.tests.append(results)
                print(f"  [{results.status_str()}] {results.details}")

        if "avsync" in selected:
            print("Running: A/V Sync...", flush=True)
            r = test_av_sync(args.video, args.audio, args.cut, fps, cuts, tmpdir)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")

        if "waveform" in selected:
            print("Running: Audio Waveform...", flush=True)
            r = test_audio_waveform(args.cut, cuts, fps, tmpdir)
            report.tests.append(r)
            print(f"  [{r.status_str()}] {r.details}")

        # Print final report
        print()
        print(report.summary())

        # JSON output
        if args.json:
            with open(args.json, "w") as f:
                json.dump(report.to_json(), f, indent=2, default=str)
            print(f"\nJSON report written to: {args.json}")

        # Exit code: 0 if all pass, 1 if any warn, 2 if any fail
        has_fail = any(not t.passed and not t.warn for t in report.tests)
        has_warn = any(not t.passed and t.warn for t in report.tests)
        if has_fail:
            sys.exit(2)
        elif has_warn:
            sys.exit(1)
        else:
            sys.exit(0)

    finally:
        if not args.keep_tmpdir and not args.tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
