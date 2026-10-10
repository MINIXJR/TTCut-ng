---
base_commit: 06329b14861eb502f94b2fafbb1c5a9b0c305c05
last_verified: 2026-10-10
sources:
  - build-package.sh
  - debian/rules
  - tools/ttcut-screenshots.sh
  - tools/ttcut-test.ttcut
  - tools/ttcut-repair-test.ttcut
  - tools/diag/gate_screenshots_repair.sh
  - tools/diag/run-gates.sh
  - gui/ttcutmain.cpp
  - gui/ttcutmainwindow_headless.cpp
  - gui/ttcutaboutdlg.cpp
  - gui/ttcutaboutdlg.h
  - tools/test-videos/make_test_video.sh
  - tools/test-videos/inject_fieldpictures.py
  - tools/test-videos/paff.cfg
  - tools/test-videos/EXPECTED.md
  - tools/burst-analysis/burst_analysis.py
  - tools/burst-analysis/README.md
  - tools/ttcut-burst-probe/main.cpp
  - tools/ttcut-burst-probe/CMakeLists.txt
  - tools/ttcut-burst-probe/README.md
---

# Code Map: Developer tools (package, screenshots, test material, burst aids)

**Scope:** the scripts and small programs a developer runs by hand and that
are not shipped: the Debian package build (`build-package.sh`), the wiki
screenshot run (`tools/ttcut-screenshots.sh` with the application's
`--screenshots` mode), the generator of test videos
(`tools/test-videos/`), and two measuring aids for the burst detector
(`burst_analysis.py`, `ttcut-burst-probe`). Four separate flows; they share
the Tux artwork and the two fixture directories that the gate runner reads.

**Neighbours, not part of this map:** the gates and harnesses themselves
(`tools/diag/`, table in `run-gates.sh`); the shipped tools
([ttcut-demux.md](ttcut-demux.md), [ttcut-ac3fix.md](ttcut-ac3fix.md),
[demux-helpers.md](demux-helpers.md), [quality-check.md](quality-check.md));
the detector the burst aids look at ([burst-detection.md](burst-detection.md));
what the screenshot mode does to the loaded project
([stream-open-project-load.md](stream-open-project-load.md),
[stream-points.md](stream-points.md)).

## Data flow

Legend: solid = data, dashed = trigger / configuration.

```mermaid
flowchart TD
    VER["CMakeLists.txt<br/>project VERSION"]
    PKG["build-package.sh"]
    BDIR["build copy<br/>../ttcut-ng-VERSION"]
    RULES["debian/rules"]
    DEB["Debian package"]

    SVG["ui/pixmaps/Tux.svg"]
    SHOT["ttcut-screenshots.sh"]
    TDATA["tools/testdata<br/>tux_test.*"]
    APP["ttcut-ng --screenshots<br/>runScreenshotMode"]
    PNG["ttcutng-*.png<br/>temporary directory"]
    WIKI["wiki images"]
    DOCPNG["docs/MainWindow.png"]

    MTV["make_test_video.sh"]
    INJ["inject_fieldpictures.py"]
    JM["JM lencod<br/>outside the repository"]
    CACHE["tools/test-videos/cache<br/>tux_*"]
    GATES["run-gates.sh<br/>gate scripts, harnesses"]

    BA["burst_analysis.py"]
    BP["ttcut-burst-probe"]
    DET["TTAudioCutter::detectBurst"]

    VER -->|"version"| PKG
    PKG -->|"rsync of the working tree"| BDIR
    BDIR -->|"dpkg-buildpackage"| RULES
    RULES --> DEB

    SVG -->|"artwork"| SHOT
    SHOT -->|"video, audio, .info, project"| TDATA
    TDATA -->|"--project"| APP
    SHOT -. "--screenshots DIR" .-> APP
    APP --> PNG
    APP -->|"copy of the main window"| DOCPNG
    PNG -->|"changed files only"| WIKI
    TDATA --> GATES

    SVG -->|"artwork"| MTV
    MTV -->|"frame-coded PAL stream"| INJ
    INJ -->|"field-picture stream"| CACHE
    MTV -->|"raw YUV"| JM
    JM -->|"PAFF stream"| CACHE
    MTV -->|"streams, audio, projects"| CACHE
    CACHE --> GATES

    BA -->|"candidate boundaries"| BP
    BP -. "one boundary" .-> DET
```

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| `VER` → `PKG` | The version is taken with `grep` from the single line `project(ttcut-ng VERSION …)`; an empty result ends the script. Package version = `VERSION+git<commit date>.<commit count>.<short hash>~<distribution>` — dots, no hyphen, because the source format is native. |
| `PKG` → `BDIR` | `rsync -a` of the **working tree**, not of the git index, into a sibling directory of the repository; an existing copy is removed first. Excluded by explicit rules: `.git`, objects, the three build directories, the test-video cache, `docs/`, editor and assistant state (`.qtc_clangd`, `.cache` anywhere, `.superpowers`, `.claude`), core dumps in the root, and of `tools/diag` the binaries named `test_*` plus three named ones — sources (`.cpp`, `.sh`, `.py`, `.txt`) stay because `CMakeLists.txt` adds the directory. Before the copy `dch` writes a new entry into `debian/changelog`; an `EXIT` trap restores that file in the repository. The changelog text is asked for on stdin (empty or no terminal → "Git snapshot"). |
| `BDIR` → `RULES` → `DEB` | `dpkg-buildpackage -b` (binary only, unsigned) in the copy; `debian/rules` builds with CMake and Ninja in `build-deb` and installs the program and the shipped tools. The script then expects `../ttcut-ng_<version>_<arch>.deb` and lists its content; a missing file is exit 1. |
| `SVG` → `SHOT` → `TDATA` | From the Tux artwork: 120 s H.264 at 720×576, 25 fps (`tux_test.264`), an AC3 track of seven segments that alternate tone and silence and stereo (192 kbit/s) and 5.1 (384 kbit/s) (`tux_test.ac3`), a hand-written `.info` (`frame_rate=25/1`, one audio track, language `deu`) and a project from the template `tools/ttcut-test.ttcut` (three cuts, placeholders replaced by absolute paths). Video and audio are rebuilt only when missing or older than the SVG; `.info` and project are rewritten on every run. For the repair dialog's pictures the script adds a second pair of tracks — `tux_repair.ac3`, 120 s of 5.1 with a chord in the centre, quiet tones around it and a silent LFE, carrying a C+LFE burst at 20 s, a stop at 50.003 s (0.5 s) and a hole of 17 ms at 80 s, and `tux_repair.mp2`, the stereo downmix of the undisturbed sound — and a project from `tools/ttcut-repair-test.ttcut` (the same video, both tracks, one cut); the pair is rebuilt when missing or older than the script. |
| `TDATA` → `APP`, `SHOT` ⇢ `APP` | Two runs into one temporary directory: `ttcut-ng --screenshots <dir> --project <file>` with the Tux project (the usual set; a run that leaves no picture ends the script), then the same with the repair project and `--screenshot-set repair`. `--repair-only` skips the first run. Both start with `QT_QPA_PLATFORM=xcb` (a window opens on the desktop) unless `TTCUT_QPA_PLATFORM` names another platform (`offscreen`: no window). `main` stores directory, project and set in `TTSettings` (the set is not persisted) and calls `TTCutMainWindow::runScreenshotMode` after 500 ms. Without `--project` the mode quits at once. The script keeps the run's output in a log, shows the lines containing "Screenshot", and ends with exit 1 when the run's exit status is not 0. `TTCUT_BINARY` replaces the program (gate `screenshots_script`). |
| `APP` → `PNG` | Window fixed to 1920×1080, project loaded (wait up to 30 s; a timeout is a warning, the run goes on), then one PNG per step: main window, frame pair, navigation, cut list, controls, landing zones after `onAnalyzeStreamPoints`, quick-jump dialog, every settings page, every cut-dialog tab, about dialog (`TTCutAboutDlg`), integrity warning, go-to-frame dialog, cut-complete box. The mode ends with `QApplication::quit()`. With the set `repair` none of these is taken: `captureRepairDialogs` runs the analysis (or waits for the scan started after loading), then opens `TTAudioRepairDialog` on the first anomaly marker of each kind — `LfeBurst` → `ttcutng-repair-mute.png`, `LastingStop` → `ttcutng-repair-fadeout.png`, `Hole` → `ttcutng-repair-fill.png` — and ends with exit status 1 when a kind has no marker or the project no AC3 track. |
| `APP` → `DOCPNG` | Last step of the usual set: `ttcutng-main.png` is copied to `docs/MainWindow.png` next to the binary's parent directory — a **tracked** file outside the output directory, on every such run. The set `repair` does not touch it (gate `screenshots_repair` checks that). |
| `PNG` → `WIKI` | The script compares each `ttcutng-*.png` byte for byte with the file of the same name in the output directory (default: the wiki's `images`) and copies only those that differ; the temporary directory is removed. No image at all is an error (exit 1). |
| `TDATA` → `GATES` | `run-gates.sh` (`TESTDATA`) and ten harnesses read `tools/testdata/tux_test.*`; the gate `screenshots_repair` runs the script with `--repair-only` and `offscreen` and expects the three repair pictures, all different, and nothing else; the directory is gitignored, so those gates need one screenshot-script run first. |
| `SVG` → `MTV` → `CACHE` | The artwork is found relative to the script. Per variant a video stream, a 1 kHz stereo audio track (AC3 or MP2, 192 kbit/s) and a video-plus-audio project, all on one 120 s timeline (three colour segments with a moving Tux, three 1 s black gaps, a logo in the third segment, a test pattern) or the 30 s "duplicate" timeline (the same 10 s sequence twice). Eight variants: HEVC 4K, H.264 1080p progressive, 1080i MBAFF, 1080i PAFF, MPEG-2 576i, 720p, 576i with field pictures, a two-segment `.rec` directory; six duplicates. An output is reused when it exists, is not empty and is **newer than the script**; `--force` regenerates. A reused variant still gets its project rewritten. |
| `MTV` → `INJ` → `CACHE` | The frame-coded PAL stream; every 50th picture block is written twice with `picture_structure` set to top and to bottom field. Bitstream-valid, visually broken (same payload for both fields). |
| `MTV` → `JM` → `CACHE` | PAFF only: raw YUV of the timeline (about 7 GB, deleted afterwards) through the JM reference encoder with `paff.cfg`, limit two hours; the result must carry VUI and `field_pic_flag`. Without the encoder the variant is skipped with a message and exit 0. |
| `CACHE` → `GATES` | The gate runner and the gate scripts name the cache files directly (`V264`, `A264`, `MBAFF`, `PAFF`, `MP2`, …); a missing file makes a gate SKIP. `EXPECTED.md` holds the expected frame numbers of the search markers for six of the files. The cache directory is gitignored and on this machine a symbolic link. |
| `BA` → `BP` | `burst_analysis.py scan` reads the RMS level per audio frame with ffmpeg `astats` and lists boundary times where the rebuilt window logic would or would not fire; `dump` prints the chunks around given cut indices. Window bounds, the two tested chunks and the context level (`context_median`: `sorted[n/2]`) follow the detector. Its numbers are candidates; the README says the C++ tool decides. |
| `BP` ⇢ `DET` | `ttcut-burst-probe <audio> <seconds> [--cutin] [--min-delta dB]` calls the detector once and prints `present=1 burstDb=… contextDb=… delta=…` or `present=0`; exit 0 = burst, 1 = none, 2 = usage error. Built only on request (`EXCLUDE_FROM_ALL`), from `extern/ttaudiocutter.cpp` and the sources it needs (re-encoder, bit reader, `ttavutil`, logger, settings). |

## Assumptions, contracts & pitfalls

- **Two fixture directories, two makers** — `tools/testdata` comes from the
  screenshot script, `tools/test-videos/cache` from `make_test_video.sh`;
  both are gitignored and both feed gates. Neither maker is called by the
  gate runner. (`ac3_reencode` and other audio gates generate their own
  material with ffmpeg into the run's work directory.)
- **`build-package.sh` copies what is on disk** — a file that is neither
  tracked nor named in an exclude rule ends up in the build copy (15.7 MB
  in 574 files on 2026-10-02). A change of the rules warrants a comparison
  of the package content; the last one (2026-10-02, before and after the
  editor-state rules): same 16 files, all but the program and the Debian
  changelog with equal checksums.
- **The screenshot mode has a side effect outside its output directory**
  (`docs/MainWindow.png`); after a run `git status` shows the file as
  modified unless the main window is unchanged.
- **Offscreen works, equality is not measured** — `QT_QPA_PLATFORM=offscreen`
  runs the mode to the end (21 images in 7 s, 2026-10-02) without a window;
  whether the images equal those of an `xcb` run is not measured, so the
  script stays on `xcb`. The images in the wiki are no reference for the
  script's own fixture: they were taken from other material.
- **Screenshots depend on the fixed window size, the fixture and the
  settings of the user who runs it**, not on the screen; the cut dialog
  shows free disk space, which changes two images between runs of the same
  tree.
- **`make_test_video.sh` invalidates its whole cache when the script
  changes** (the "newer than the script" rule) — the PAFF variant then costs
  the JM run again. Two `.info` files in the cache are made by hand, the
  script does not write them.
- **`burst_analysis.py` is a rebuild, not the detector** — it calls ffmpeg
  with `-drc_scale 0`, because the detector decodes without the stream's
  dynamic range compression (`burst-detection.md`). Measured 2026-10-02 at
  120 boundaries of a DVB AC3 track: all chunks equal to the detector's
  except the first one of each window (0.1–0.3 dB), which the detector
  decodes right after its seek.

## Redundancy / consolidation candidates

- **Tux test audio with tone, silence and a layout change**
  - sites: `tools/ttcut-screenshots.sh:gen_ac3_segment`, `tools/test-videos/make_test_video.sh:gen_audio`, `tools/diag/run-gates.sh:make_mixed_ac3`
  - shared purpose: synthetic AC3/MP2 tracks from ffmpeg `lavfi` sources
  - status: kept separate → three different tracks for three purposes (silence and layout switches for the landing-zone screenshots, a steady tone for the search fixtures, layout switches at one frame size for the audio gates)
- **Project file written by a script**
  - sites: `tools/ttcut-screenshots.sh` (template `tools/ttcut-test.ttcut` through `sed`), `tools/test-videos/make_test_video.sh:write_ttcut_project`
  - shared purpose: a `.ttcut` file with absolute paths for a generated fixture
  - status: kept separate → the screenshot project carries three cuts from a template, the cache projects none
- **Burst window logic**
  - sites: `extern/ttaudiocutter.cpp:TTAudioCutter::detectBurst`, `tools/burst-analysis/burst_analysis.py:window_bounds`, `tools/burst-analysis/burst_analysis.py:chunks_in_window`
  - shared purpose: which audio frames belong to the window at a cut boundary and which of them are tested
  - status: kept separate → the Python copy exists to scan a whole stream without the detector; window, median rule and levels were compared with the detector on 2026-10-02 (see pitfalls)
