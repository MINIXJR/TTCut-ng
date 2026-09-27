---
base_commit: 6c0fe7de90a79b7160b5abc722be4e93f4c1eefa
last_verified: 2026-09-27
sources:
  - tools/ttcut-quality-check/ttcut-quality-check.py
  - debian/rules
  - debian/control
---

# Code Map: Quality check (ttcut-quality-check)

**Scope:** `tools/ttcut-quality-check/ttcut-quality-check.py`, installed as
`/usr/bin/ttcut-quality-check`: it takes the original elementary stream and
audio, a cut MKV and the kept frame ranges, runs up to seven tests and
reports PASS/WARN/FAIL per test, as text, optional JSON and exit code. It
measures a finished cut from outside; nothing in TTCut-ng calls it. Gate
`quality_check` runs it on synthetic material.

**Neighbours, not part of this map:** how TTCut-ng cuts
([smart-cut.md](smart-cut.md), [mpeg2-cut.md](mpeg2-cut.md),
[audio-cut-timing.md](audio-cut-timing.md)); what the `.info` keys mean and
who writes them ([ttcut-demux.md](ttcut-demux.md)); the author's
`verify-smartcut` skill, which drives this tool, lives outside the
repository.

## Data flow

Legend: solid = data, dashed = trigger / configuration.

```mermaid
flowchart TD
    ARGS["command line<br/>--video --audio --cut --cuts --fps"]
    INFO[".info<br/>parse_info_file"]
    PAFF["detect_paff"]
    CFG["run parameters<br/>fps, cuts, extra frames, is_paff"]
    CUT["cut MKV"]
    REF["reference MKV<br/>build_reference: mkvmerge ES + audio"]
    OWN["tests on the cut MKV<br/>metadata, PTS, duration, waveform"]
    CMP["tests against the reference<br/>visual SSIM, A/V sync"]
    CONF["TTCut-ng.conf<br/>parse_ttcut_settings"]
    DEF["defect regions"]
    REP["QualityReport<br/>summary, JSON, exit code"]

    ARGS --> CFG
    INFO -->|fps, extra frames| CFG
    PAFF -->|is_paff| CFG
    CFG --> OWN
    CFG --> CMP
    CFG --> DEF
    CUT --> OWN
    CUT --> CMP
    ARGS -->|ES, audio| REF
    REF --> CMP
    CONF -->|gap, offset| DEF
    OWN --> REP
    CMP --> REP
    DEF --> REP
```

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| `ARGS` → `CFG` | `--cuts "a-b,c-d"`: 0-based, inclusive frame ranges as in TTCut-ng's cut list (`parse_cuts`). `--fps` unless `--info` has a `frame_rate`. `--tests` picks a subset of `metadata,timing,duration,visual,avsync,waveform,defects`; `mkvmerge` is required only for `visual`/`avsync`. |
| `INFO` → `CFG` | `[video] frame_rate` (`parse_fraction`: fraction or number) overrides `--fps`. Extra frames from `[warnings] es_doubled_pts_aus` (raw access units, with `es_total_aus`), else the legacy `es_extra_frames`. `[timing] av_offset_ms` is parsed but not used. |
| `PAFF` → `CFG` | Only when ffprobe reports a non-progressive field order: the first second as Annex-B, access-unit delimiters per expected frame > 1.5 → PAFF. On PAFF the raw-AU extra-frame list is dropped with a warning (it does not match frame indices). |
| `CFG` → `OWN`, `CUT` → `OWN` | **metadata:** codec equal to the ES, fps within 0.5, video packet count within ±5 of Σ(segment length) minus extra frames inside the cuts. **timing:** per stream the sorted packet PTS; any interval more than one time-base tick (Matroska 1 ms, at least 0.5 ms) off the median counts as an anomaly, FAIL on any. **duration:** stream durations (or last PTS + duration), |video − audio| ≤ 50 ms PASS, ≤ 100 ms WARN. **waveform:** 100 ms of 48 kHz mono around each internal boundary (cumulative segment durations from `frames_to_seconds`), a 1 ms chunk more than 30 dB above both neighbours (and above −80 dBFS) is a click; needs numpy, else WARN. |
| `ARGS` → `REF` → `CMP`, `CFG`/`CUT` → `CMP` | `build_reference`: `mkvmerge --default-duration 0:<1e9/fps>ns` on ES and audio, once per run, shared by both tests. **visual:** skipped (WARN) when the `.info` lists extra frames. Per segment a display-order offset (0–10 frames, best SSIM of the first cut frame against the reference); then SSIM at start+1 frame (re-encoded zone, ≥ 0.50), start+20, middle, end−20 (stream copy, ≥ 0.99); each reference position probed ±3 frames (PAFF: ±12 fields). Frames come from `-ss (t−15 s) -copyts -vf select=gte(t,T)`. **avsync:** per segment a 10 s window 0.5 s after its start and one ending 0.5 s before its end (one window when the segment is shorter than 21 s, none below 2 s), each against the same stretch of the reference, 16 kHz mono PCM, numpy FFT cross-correlation within ±1 s. A window counts only when its peak is ≥ 1.25 × the best peak more than 2 ms away and neither side is silent; the largest counted offset decides (≤ 50 ms PASS, ≤ 150 ms WARN), no counted window → WARN “not measurable”. Real TV audio measured 2.98–6.84, a pure tone ≈ 1. |
| `CONF` → `DEF`, `CFG` → `DEF` | Extra frames inside the cuts grouped into regions (gap = `--defect-gap` or the setting, default 5 s), each with its time in the cut. `parse_ttcut_settings` reads `~/.config/TTCut-ng/TTCut-ng.conf` as TTCut-ng writes it: group `[Settings]`, keys `Common\ExtraFrameClusterGap`/`Common\ExtraFrameClusterOffset`; the report names the file only when a value came from it, “command line” when both options are given. The offset is not used in the grouping. Always PASS (a report, not a test). |
| `OWN`, `CMP`, `DEF` → `REP` | One `TestResult` per test (visual yields two: re-encoded and stream copy). Exit code 2 when any FAIL, 1 when any WARN, else 0; `--json` writes the same list. The temp directory (`mkdtemp` next to the `--cut` file) is removed unless `--keep-tmpdir` or `--tmpdir`. |

## Assumptions, contracts & pitfalls

- **Frame numbers are TTCut-ng's**, not the reference MKV's: the visual test
  searches offsets and neighbours because mkvmerge and TTCut-ng may disagree
  on display order at a boundary; a defective stream (extra frames) makes
  that comparison meaningless, so it is skipped.
- **Temp space:** the reference MKV is about as large as the ES and audio;
  it lives next to the cut (the disk with the large files) because `/tmp`
  may be a RAM-backed tmpfs (here: 46 GB). `--tmpdir` moves it.
- **Test audio must not be a steady tone:** the Tux fixture's audio is a
  pure 1 kHz sine, which correlates at every lag; the gate therefore uses
  pink noise, and the tool reports such audio as not measurable.
- **Packaging:** installed by `debian/rules`; `debian/control` recommends
  `python3-numpy` (A/V sync, waveform) and `mkvtoolnix` (reference MKV).

## Redundancy / consolidation candidates

None open: the reference MKV is built once (`build_reference`) and fractions
are parsed in one place (`parse_fraction`).
