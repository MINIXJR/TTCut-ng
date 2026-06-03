# Changelog

All notable changes to TTCut-ng are documented in this file.

## v0.71.0 (2026-06-03)

**libmpv in-process render backend (native Wayland), new playback player, H.264/H.265 playback fixes**

### Features

- **libmpv in-process render backend** — playback now renders through
  the MPV_RENDER_API (OpenGL) inside a `QOpenGLWidget` instead of
  launching mpv as a child process embedded via `--wid`. TTCut-ng runs
  natively on Wayland without the `QT_QPA_PLATFORM=xcb` workaround. The
  IPC-socket path is gone entirely. New classes: `ITTMpvBackend`,
  `TTMpvLibBackend`, `TTMpvRenderWidget`, `TTMpvWrapper`; the old
  `TTVideoPlayer`/`TTMplayerWidget` were removed.
- **Playback controls** — combined Play/Stop toggle button, fast
  forward / reverse (±2×/±4×) with auto-mute above 1×, and a live
  timecode that runs with the mpv clock during playback.
- **Faster re-play** — the temporary playback MKV (H.264/H.265 ES has no
  timestamps, so it is muxed into an MKV before playback) is now cached
  across STOP→PLAY cycles. Re-playing the same source starts instantly
  instead of re-muxing the whole stream (~5 s). The cache is invalidated
  by a source fingerprint (video + audio path) and cleared on source
  change / close. The temp file is now uniquely named
  `ttcut-ng_playback_temp.mkv`.

### Fixes

- **Advertising flash on PLAY** (H.264/H.265) — the app frame index is
  decode-order while mpv seeks by display time; PLAY landed on the GOP
  keyframe before the cut-in (typically an ad frame). The display
  position is now derived from the frame actually decoded, so playback
  starts on the selected frame.
- **First PLAY no longer fails** — the render context is created at
  stream open so the very first PLAY no longer hit "No render context
  set" and stayed black.
- **PAFF playback jump** — the decode-order tag now counts per frame
  instead of per packet (PAFF has two field packets per frame), removing
  a large position jump on PAFF streams.
- **Frame "flicker" on play↔still switch** — the still-frame widget lost
  its stray ~2 px white frame border (a stylesheet overrode
  `setFrameShape(NoFrame)`), so the still and the mpv frame are now
  pixel-congruent.
- **MPEG-2 field-picture stop position** — play/stop position drift
  corrected via field-picture index correction.

### Changes

- The still-frame on STOP is taken from the last actually rendered mpv
  frame instead of the (ahead-running) playback clock, reducing the
  stop jump from ~16 to ~5 frames. The ~5-frame remainder is an inherent
  `vo=libmpv` pipeline depth (documented in TODO.md / Known Limitations).
- `hwdec` defaults to `no` (env-overridable via `MPV_HWDEC`) to avoid a
  Mesa/RDNA4 VA-API decode bug; CPU decode is fine at 1080p25.
- Build dependencies: `libmpv-dev`, `libqt5opengl5-dev`.

## v0.70.0 (2026-05-20)

**Settings dialog and Cut-Dialog overhaul, persistent/transient settings split, English UI source strings**

### Features

- **Settings dialog redesign** — the former 4-tab dialog is now a
  7-category sidebar: Navigation, Search & Preview, Audio & Language,
  Encoder, Multiplexing, Paths, Logging. Sidebar labels and tooltips
  reworked, the last-used category is restored on reopen.
- **Reset-to-defaults buttons** in each Settings category and in the
  Cut-Dialog. Each button restores the compile-time defaults for its
  own page only.
- **Persistent/transient settings split** — the encoder pipeline now
  reads transient working values (`encoderXxx()`), so a one-off change
  in the Cut-Dialog no longer overwrites the app-wide codec defaults.
  Seven `working*` variants added for mux/audio settings.
- **Cut-Dialog** reduced from 3 tabs to 2; the output group box gained
  a per-codec container choice, persisted in the `.ttcut` project file
  (`MuxMode`, `Mpeg2Target`, `AudioOnlyFormat`).
- **Configurable default output directory** in the Paths category.
- **libav log routing** — `qDebug`/`qWarning` and libav's own log
  output are routed through `TTMessageLogger`, gated by a `logLibav`
  toggle.

### Fixes

- **Burst detection** ran on the wrong audio track when a project was
  loaded; it now targets the first audio track consistently.
- **Cut-Dialog dialog semantics** — closing via the window X now
  cancels instead of silently accepting, an overwrite confirmation was
  added for existing output files, and the spurious UI auto-connect
  `okButton → accept()` was removed so cancel paths are honoured.
- **Cut-Dialog Encoder tab** hides the empty preview-settings group box
  instead of showing a blank frame.
- **MPEG-2 preview clip regeneration** applies `planAudioCut` for
  parity with the full preview path, and applies the field-picture
  extras correction so the regenerated clip matches the full cut.
- **Infinite loop** in `transferCutObjects` at end-of-stream fixed with
  an end-of-stream guard.
- **TTMessageLogger** made thread-safe; a `qDebug` recursion via the
  installed message handler fixed.

### Changes

- **English UI source strings** — the Settings dialog (7 category
  widgets) and the Cut-Dialog are fully converted to English source
  strings, with German translations in `trans/ttcut-ng_de_DE.ts`. The
  unused, 96%-empty `ttcut-ng_en_US.ts` stub was removed.
- **`.prj` → `.ttcut` migration** — legacy `.prj` projects are migrated
  read-only to `.ttcut` on save. Old `.prj` files still load.
- **Dead code removal** — legacy `<Marker>` stream-point
  reconstruction, obsolete MPEG2Schnitt IDD-Files support, the unused
  `mpeg2_mplexed_video` stream-type enum value, and 11 dead settings
  removed; `stepArrowKeys` and `stepSliderClick` reanimated.
- Sidebar reordered (UI interaction → processing → output → system),
  the "Allgemein" category dropped, the fast-slider setting moved to
  the Navigation category.

## v0.69.0 (2026-05-14)

**Logging refactor, MPEG-2 A/V drift fix, ttcut-demux multi-file recovery, internal cleanup wave**

### Features

- **Logging subsystem toggles** — six TTSettings booleans gate `qDebug`
  trace logging per subsystem (Files-Tab → "Erweiterte Logging-Optionen"):
  `logFFmpegDecoder`, `logSmartCut`, `logMkvMux`, `logCutPipeline`,
  `logAVStream`, `logUI`. All trace sites default off; failure paths are
  rewritten as `TTMessageLogger::warningMsg` with `[warning][file:line]`
  format so they remain visible regardless of toggle state. ~310 qDebug
  sites across `extern/`, `data/`, `avstream/`, `gui/` and ~40 warnings
  triaged. Bit-identical MKV output verified after each phase via
  ffprobe show_packets diff on MBAFF synthetic test fixture.
- **H.264/H.265 equal-frame search** — `TTFrameSearchTask` now routes
  H.264 and H.265 streams through `TTFFmpegWrapper::decodeFrameYUV`
  instead of falling back to MPEG-2 only. Equal-frame lookup in the
  CutOut widget works on H.264/H.265 ES files.
- **10-bit / non-YUV420P slow-path** in `TTFFmpegWrapper::decodeFrameYUV`
  for HEVC Main 10 content that earlier returned a black image.

### Fixed

- **MPEG-2 field-picture A/V drift in gap-recordings** (11.85 s drift on
  affected files). `TTMpeg2VideoStream::createIndexList` now detects
  field-picture pairs in the index, and `TTAVData` loads the extra
  frame indices so audio cuts apply the correct per-segment offset via
  `countExtraFramesBefore()`. Verified on real DVB recordings: A/V sync
  perfect; the residual 104 ms end-PTS diff is a frame-duration
  asymmetry artefact, not real drift.
- **ttcut-demux multi-file VDR recordings**: audio extraction recovered
  from 40.9 s to full duration (2981 s on Two_Part_File). Concat-demuxer
  list file with absolute paths replaces the broken concat: protocol.
  Per-segment-boundary silence-insertion (or audio truncation) corrects
  the inhaltlicher A/V drift that surfaced after the audio-loss fix:
  new `detect_segment_boundaries()` bash function emits 4-field entries
  in the same CLASSIFIED_FILE format the existing audio-gap-fix uses;
  `repair_audio_with_silence_inserts` extended with signed silence_ms
  (positive = insert silence, negative = truncate audio).
- **Latent for-loop-i leak** in `repair_audio_with_silence_inserts`
  surfaced by the synthetic single-track multifile test. Three
  `for ((i = 0; i < n; i++))` without `local i` leaked `i = n` to the
  caller; the caller's outer `for i in "${!AUDIO_FILES[@]}"` then
  indexed `${CLASSIFIED_FILES[$i]}` past array bounds and aborted
  the script. Single-line `local i` fix.
- **Black-frame and scene-change search on HEVC 10-bit content**
  (Main 10). `TTFFmpegWrapper::isFrameBlack` and `buildHistogram` cast
  `mDecodedFrame->data[0]` to `uint8_t*` and indexed by column, so for
  yuv420p10le frames the byte-wise read aliased low/high bytes of
  consecutive 10-bit samples and the early-exit threshold (avg byte ≈
  32 for TV-range black Y=64) discarded every black frame. Both
  functions now detect luma bit depth via `av_pix_fmt_desc_get` and
  read 10/12-bit samples as `uint16_t` with right-shift to 8-bit space.
- **Frame rate detection for raw H.264/H.265 elementary streams.**
  `TTFFmpegWrapper::getStreamInfo` preferred libav's `avg_frame_rate`,
  which on raw ES files reports real/2 because the first GOP loses
  `bframes` display frames at the front to the B-frame reorder window.
  Now `r_frame_rate` (from SPS VUI timing) is preferred. PAFF/MBAFF
  streams keep their final progressive rate via the existing
  `frame_rate>30` correction in `tth26xvideostream.cpp:153`.

### Internal refactors

- **TTSettings God-Singleton refactor (Phase A+B)**: 30 commits
  (`6fa0e75..358cd32`). Six dead status vars deleted (Phase A); ~80
  persistent settings + ~318 call sites in 26 files migrated through
  TTSettings strangler-fig (Phase B). Legacy `gui/ttcutsettings.{h,cpp}`
  (~410 lines) removed. TTCut shrunk 281→127h / 367→213cpp. New
  `--auto-cut <out.mkv>` CLI flag enables headless QC regression.
  Bit-identical verified vs pre-refactor master (MBAFF/PAFF/H.265).
- **TTMessageLogger redesign**: lazy file open (constructor no longer
  touches filesystem), XDG default path (`~/.cache/ttcut-ng/logfile.log`
  instead of CWD), `enableLogFile(false)` honest (just suppresses file
  writes instead of side-effecting `logLevel = NONE`), 1024-byte stack
  buffer in eight overloads replaced with `QString` builders, thread-safe
  `getInstance()` via `std::call_once`.
- **reencodeFrames split** (`9f31ede`, squash-merged): 673-line function
  → 48-line orchestrator + 13 focused helpers (`computeDecodeRange`,
  `resetDecoderForSegment`, `selectFramesPAFF`/`selectFramesNonPAFF`,
  `parseEncoderSpsFromPacket`, `transformEncoderPacket`, `applyPocDomainFix`,
  …). Per-call state in `ReencodeContext` POD with RAII destructor.
  Bit-identical encoder packet output verified on MBAFF/PAFF/HEVC.
- **buildFrameIndex split** (`38bb6ea`): ~300 lines → ~15-line
  orchestrator + 6 helpers. PAFF-merging moved from inline-while-scan
  to post-process on raw index. Bit-identical CSV-validated.
- **mux() refactor** (`bb218bd`): setup/PAFF helpers extracted; audio
  input shared with `muxAudioOnly`; dead container-remux path removed.
- **TTH26xVideoStream base class** (`c95cc19`): TTH264VideoStream and
  TTH265VideoStream consolidated onto a common parent.
- **GUI threading + search performance** (`d20a070`): TTSearchTask
  base class moves black/scene/logo workers off the GUI thread.
  `setSearchMode` flag enables direct keyframe-seek (~28× less decode
  work). Coordinator + parallelMap with N sub-decoders
  (`TTSettings::searchWorkerCount`, default 4). MBAFF search jumped
  from ~1-2 fps to ~120 fps; HEVC 4K CRA-only sees modest improvement
  bounded by memory bandwidth.

### Security audit follow-up

Twenty-two of 25 findings from the 2026-05-01 audit fixed across
~30 commits (`eb04368..a01f11e`): path/list-size validation on
project/info input, hardened stream-parser bounds, libav return-value
checks on all media-IO paths, exception-by-value/catch-by-const-ref,
plug RGB frame buffer leak, plug MPEG audio probe leak, fix
searchTimeIndex precedence bug, guard cut/marker XML parsers, fix
four GUI bugs (header guard, double-free, dead cleanup, null deref),
drop broken-by-design `TTFileBuffer::readArray()`, route mux() error
paths through cleanup label.

### Tooling

- **Multi-codec test video generator** (`tools/test-videos/`):
  `make_test_video.sh` produces synthetic test files with known
  black-frame/scene-change/logo markers across HEVC 4K Main 10
  (CRA-only Open-GOP), H.264 1080p progressive, H.264 1080i MBAFF,
  H.264 1080i PAFF (via JM Reference Encoder), MPEG-2 PAL DVB-SD,
  MPEG-2 720p progressive, and MPEG-2 576i field-picture. Plus
  MPEG-2 576i multifile variant for VDR multi-file demux tests.
- **ttcut-demux improvements**: FFMPEG_INPUT_ARGS array unifies
  all ffmpeg invocations; A/V-sync-aware audio gap detection with
  marker emission; centralised audio codec→extension mapping.
- **`tools/vdr-demux-example.sh`**: decodes VFAT character escapes
  (`#3A → :`, `#3F → ?`) in path components.
- **`tools/test-videos/paff.cfg`** IDRPeriod tuned from 1500 to 25
  (every I-slice is now an IDR). Matches real DVB-PAFF cadence
  (Moon_Crash_(2022) IDR distribution analysis showed 1.2-1.3 s
  IDR cadence as dominant mode). The original IDRPeriod=1500 made
  the synthetic PAFF test file unusable for Smart Cut regression
  (only 2 IDRs in 120s).

### Tests

- All 170+ commits in this release built and run-tested with
  bit-identical MKV output verified at each major refactor checkpoint
  (TTSettings refactor, reencodeFrames split, logging refactor phases
  3-6). Smoke-tested on MBAFF synthetic, PAFF Moon_Crash_(2022), HEVC
  Designermode, and multiple VDR multi-file recordings.

## v0.68.0 (2026-05-01)

**Audio-only cut, audio-burst detection overhaul, cut-list/preview polish**

### Features
- Audio-only cut: the *Audio schneiden* button in the cut list now
  actually performs an audio-only cut. Previously it dispatched the
  same A/V cut path because the receiving slot dropped the
  `audioOnly` flag. New output-format selector with four presets:
  *Original codec (per track)* leaves per-track elementary streams
  in the cut directory; *Matroska Audio (.mka)* muxes all tracks
  into one MKA via the libav matroska muxer; *MP3* and *AAC* are
  reserved for re-encoding (currently fall back to ES with a
  warning until the encoding stage lands). The cut list also gains
  a fourth *Auswahl schneiden* button for selection-only audio cuts,
  symmetric to the existing A/V variant.
- Audio drift bounded to ±½ frame: each segment no longer loses
  0…2 audio-frame durations independently. The new feed-forward
  planning in `TTAVData::planAudioCut()` snaps once per segment
  and carries the per-segment residual into the next one, so
  cumulative drift stays bounded across the timeline. The cut
  list's *Audio-Drift* column now reflects what the actual cut
  produces, not a parallel estimate.
- Diagnostic tools (`make diag`): three small helper binaries —
  `check_idr`, `test_nalu_parser`, `test_au_types` — now build
  reproducibly under `tools/diag/` against the existing
  `obj/ttnaluparser.o`. Useful for inspecting GOP structure and
  Access-Unit types of arbitrary H.264/H.265 elementary streams.
- VDR demux example script (`tools/vdr-demux-example.sh`) now
  decodes VFAT character escapes (`#3A → :`, `#3F → ?`, etc.) in
  show names and neutralises stray slashes, so the demuxed file
  basenames are readable instead of `09x01_-_Ehekrise#3A_…#3F`.
- Cut entries can now be opened for editing by double-clicking the
  row in the cut list. Same effect as the existing context menu
  / toolbar Edit button.

### Fixes
- Audio-burst detection: same boundary-time at all three call
  sites (cut list, preview dialog, final-cut warning). Previously
  cut list and preview probed the raw frame index while the
  final-cut warning subtracted `countExtraFramesBefore`, so on
  streams with TS-corruption-induced extra frames a borderline
  burst could be flagged in one place but not the other. The
  preview dialog also missed CutIn warnings on the first cut and
  CutOut warnings on the last cut. New helpers
  `TTAVData::detectCutOut/InBurst()` apply the extra-frame
  correction and the threshold filter consistently in one place.
- Audio-burst detection window: post-boundary tail tightened from
  a hardcoded 48 ms to half an audio-frame (12 ms on MP2,
  16 ms on AC3). The prior tail plus extra reject-slack let the
  analyser inspect frames up to 72/80 ms past the cut, well
  beyond what frame-snapping can ever leak into the kept audio,
  and produced false-positive bursts on inaudible material.
- Burst-shift in the preview: clicking *Shift -1 Frame* on an
  MPEG-2 source now actually reloads the regenerated clip. The
  output path was being constructed with `.mpg` extension while
  the helper muxed to `.mkv`, so mpv started, failed to open the
  dead path, and silently bounced back to *Play*. Also fixes a
  latent file-index mismatch when the preview was launched in
  *transitions only* mode (the regen overwrote the wrong file).
- Burst-shift label colour reset: navigating between cuts after a
  successful shift no longer renders the next cut's burst warning
  in green. The orange-warning style is now reset at the top of
  every check, not only after a regenerate.
- Edit re-entry: double-clicking a different cut while another
  one is in edit mode now clears the previous row's highlight
  brushes instead of leaving two rows visually highlighted with
  inconsistent `editItemIndex` state.

### Changes
- QGroupBox titles are now centred application-wide via a
  one-line stylesheet in `main()`. The current Breeze default
  is left-aligned; the stylesheet pins the centred look across
  styles. Per-widget alignment overrides in `.ui` files (only
  `gbProcessView` today) still win.
- Two `gridLayout` name collisions in `ui/currentframewidget.ui`
  and `ui/avcutdialog.ui` resolved (renamed to
  `gridLayoutCurrentFrame` / `gridLayoutOutputOptions`); uic no
  longer prints "name 'gridLayout' is already in use" warnings
  on every build.

### Build / repository
- `tools/ttcut-pts-analyze` is now buildable on a fresh clone:
  the C source and its Makefile were tracked properly (they had
  been excluded by the global `Makefile` ignore pattern).
- `tools/` cleanup: dead one-off test programs from the v0.60.0
  libav migration (`test_es_smartcut`, `test_prj_smartcut`,
  `test_smartcut`) are gone; the still-useful diagnostic sources
  are consolidated under `tools/diag/`. The Python NAL test
  harness (`tools/nal-test-harness/nal-verify.py`) is now
  tracked and uses standard tempdir resolution.
- Working notes (implementation plans and design specs that used
  to accumulate under `docs/plans/` and `docs/superpowers/`) are
  no longer tracked; `ttcut-quality-check.py` defaults to the
  standard `tempfile` location instead of a developer-machine
  fallback path.

## v0.67.0 (2026-04-23)

**HEVC MKV output, UI cleanup, various fixes**

### Fixes
- MKV muxer: codec-aware NAL parsing. H.265/HEVC MKV output now contains
  both video and audio; previously the muxer parsed all NAL units as H.264,
  dropped every HEVC video packet as non-VCL, and silently wrote MKV files
  with only an audio track. `TTMkvMergeProvider` now receives the codec
  ID via `setVideoCodecId()` and dispatches parsing per codec (H.264, HEVC,
  MPEG-2 pass-through).
- Cut dialog: filename field updates live when the suffix checkbox, video
  codec, or output container changes. Previously the suffix was applied
  only once on dialog open; switching container from MKV to MPG after
  opening left the wrong extension in the field.
- cutVideoName session leak: first cut after app start no longer writes
  to a stale output path from the previous session. The global
  `TTCut::cutVideoName` is now reset in the new-project flow.
- mplex muxer: filenames with non-ASCII characters (umlauts etc.) are now
  passed correctly through the mplex call; previously UTF-8 was not
  preserved and the multiplex step failed.

### Changes
- Muxing tab UI cleanup: the muxing-program combo was relabeled, the
  non-functional MP4 output option was removed, and the mplex selection is
  now guarded for MPEG-2 only (it cannot be picked together with H.264 or
  H.265, because mplex does not mux those).
- Removed four inactive UI elements: the Chapters tab in the Settings
  dialog and in the Cut dialog (both spumux/DVD-authoring legacy, always
  hidden at construction time), the empty Configure Muxer button
  (leftover from the mplex-CLI era before v0.60.0), and the
  `videoFileInfo` widget in the main window (pinned to 0 pixels height,
  fully redundant with the video tree view columns). The orphaned
  `chapter_18.xpm` pixmap and the unused `TTCut::imgChapter` singleton
  are cleaned up along with them. A new TODO tracks a future custom
  MKV chapter editor.

## v0.66.0 (2026-04-12)

**Per-Track Audio Delay, Audio-Drift Display, Project Settings Persistence, Audio Language Preference**

### Features
- Audio list: Editable per-track audio delay (±9999 ms) via QSpinBox. Applied
  during audio cutting (keepList PTS offset) and preview for all codecs
  (MPEG-2, H.264, H.265). Persisted in `.ttcut` project files.
- Cut list: Column renamed from "Audiooffset" to "Audio-Drift". Shows
  accumulated audio frame boundary drift per cut, calculated during preview
  (first audio track). Displays "—" placeholder until preview is run.
- TTESInfo now parses per-track `audio_N_trimmed_ms` and `audio_N_first_pts`
  from `.info` files generated by ttcut-demux v0.65.2+.
- Project settings persistence: Output path, filename, suffix option, muxing
  settings (container, chapters, interval, delete ES), and encoder settings
  (preset, CRF, profile) are now stored per-project in the `.ttcut` file.
  On project load, these override the global defaults. On project close,
  globals are restored from QSettings. Old `.ttcut` files without settings
  load without error.
- Audio language preference: New setting (Allgemein tab) to configure a
  comma-separated list of preferred audio languages. Replaces the hardcoded
  system-locale sort. Accepts 2-letter (`de`), canonical 3-letter (`deu`),
  and alternative ISO 639-2 forms (`ger`, `fre`, `nld`, ...). Empty list =
  system locale (previous behavior).

### Fixes
- Fix: Dangling pointer in `closeProject()` caused segfault when opening a
  new video after a project was previously loaded.

### Fixes
- Fix: Audio list UI not refreshed after locale-based sorting — system language
  audio track (e.g., "deu") now correctly appears first after async stream
  loading completes.

## v0.65.2 (2026-04-08)

**ttcut-demux: Per-Track Audio Trim Fix**

### Fixes
- Fix: ttcut-demux used the first audio stream's PTS offset to trim all audio
  tracks. When tracks had different PTS offsets (e.g., MP2 at -384ms vs AC3 at
  -320ms from video), non-primary tracks were over-trimmed, causing A/V desync
  (64ms in typical SAT.1/RTL recordings with dual MP2+AC3 audio).
- Each audio track now gets its own PTS probe and individual trim value.
- The .info file includes per-track fields (`audio_N_first_pts`,
  `audio_N_trimmed_ms`) for diagnostics.
- Fix: Buffer overrun in `nextStartCodeTS()` when scanning past end of file
  during MPEG-2 TS start code search (Boyer-Moore loop missing EOF check).

## v0.65.1 (2026-04-07)

**H.264 Frame Display & Smart Cut Fixes**

### Fixes
- Fix: Cut-Out-Frame and Aktueller Frame showed different images for the same
  frame index due to Open-GOP B-frames decoding differently with warm vs cold
  DPB. seekToFrame() now seeks to the previous keyframe for DPB prefill, and
  the sequential decode optimization is disabled for consistent results.
- Fix: MBAFF SPS regression from PAFF commit — patchH264SpsReorderFrames()
  unconditionally inflated num_ref_frames and max_dec_frame_buffering to 8,
  causing "co located POCs unavailable" errors in MBAFF streams. Now
  conditional on isPAFF.
- Fix: Pre-existing frame_num gap after EOS in standard Smart Cut path caused
  "illegal short term buffer state" decoder errors, especially with small
  MaxFrameNum (32). frameNumDelta is now recalculated after re-encoding.

## v0.65.0 (2026-04-06)

**PAFF Smart Cut & Progress-Fix**

### Features
- H.264 PAFF (1080i50) Smart Cut: Frame-accurate cutting for DVB PAFF streams
  (separated field coding, used by kabel eins, DF1 HD, etc.)
- TTNaluParser: PAFF field-pair merging, independent PAFF detection
- SPS Unification: Rewrite encoder MBAFF output for source PAFF SPS compatibility
- EOS DPB flush + MMCO neutralization at re-encode/stream-copy transitions
  (all slice types I/P/B, first 32 AUs after EOS)
- PAFF-aware MKV muxing: field-pair PTS assignment, per-packet detection
- ttcut-demux: Correct frame rate detection for interlaced streams
- Audio file sorting by codec priority (AC3 first) then locale language

### Fixes
- Fix: Smart Cut progress bar was always 0% (task=0 bypassed TaskPool)
- Fix: Elapsed time display during Smart Cut (QElapsedTimer for non-task operations)
- Fix: MBAFF regression where ad frames appeared at cut boundaries (restore realStartAU filter)
- Fix: Frame repetitions at PAFF re-encode/stream-copy transition (overlap extension)
- Fix: Compiler warnings (unused variables, deprecated QLabel::pixmap, signed/unsigned)

### Changes
- Progress reporting standardized from permille (0-1000) to percent (0-100) throughout
- verify-smartcut skill: added top_block decoder error check

## v0.64.0 (2026-03-29)

**Logo-Erkennung, Pillarbox-Erkennung & UI-Verbesserungen**

### Features
- Logo-Erkennung: Senderlogo-Detektion via markad PGM-Import oder manueller ROI-Selektion
  mit Sobel-Edge-Profiling fuer Werbeblock-Navigation
- Logo-Profil-Persistenz in Projektdateien (.ttcut)
- Pillarbox-Erkennung: 4:3 Inhalt in 16:9 Containern (schwarze Balken links/rechts)
  mit 10-Sekunden-Hysterese, alle Codecs (MPEG-2, H.264, H.265)
- Fortschrittsdialog fuer Landezonen-Analyse (Prozentanzeige, Abbrechen-Button)
- Projektdatei-Endung: Neue Projekte speichern als `.ttcut` (`.prj` wird weiterhin geladen)
- ttcut-pts-analyze: mmap-basierter Start-Code-Scanner mit Multi-Thread Decode-Testing
  (ersetzt ttcut-esrepair)
- Smart Cut Performance: mmap Bulk-Write-Optimierung fuer Stream-Copy
- Extra-Frame-Korrektur fuer A/V-Sync und Quality-Check bei defekten MPEG-2 Streams

### Fixes
- Fix: Projektdatei-Parser behandelte LogoProfile/StreamPoint XML-Elemente als Video-Streams
- Fix: Zeitsprung zentriert auf aktuellen Frame mit Anker-basiertem Intervallfilter
- Fix: Alle 25 Security-Audit-Findings behoben (Bounds-Checks, Cleanup)
- Fix: H.265 false positives bei Decode-Testing (AV_EF_CAREFUL nur fuer H.264/H.265)

### Changes
- Redundante F-Buttons aus Navigation-Widget entfernt, Frame-Typ-Labels hinzugefuegt (I, P/I, B/P/I)
- Redundanter "Cut-Out setzen" Eintrag aus Schnittlisten-Kontextmenue entfernt, Eintraege neu sortiert
- Uebersetzungen aktualisiert (25 neue Strings)
- Tools in eigene Unterverzeichnisse verschoben, Debian-Build aktualisiert

## v0.63.0 (2026-03-22)

**Screenshot-Automation, Dirty-Tracking & Sicherheitsfixes**

### Features
- Screenshot-Automation: `--screenshots <dir> --project <prj>` CLI-Modus fuer automatisierte
  Wiki-Screenshots mit Testmedia-Generierung (`tools/ttcut-screenshots.sh`)
- Dirty-Tracking: Warnung bei ungespeicherten Projektaenderungen vor destruktiven Aktionen
- AC3 acmod Normalisierung beim MPEG-2 Schnitt (Stereo/5.1 Kanalwechsel)

### Fixes
- Fix: MPEG-2 Preview-Freeze beim Burst-Shift (Segmentgrenzen-Behandlung)
- Fix: Security-Audit Findings — Bounds-Checks und Cleanup (2 Critical, 6 High)
- Fix: Schnittliste Spalte 5 Header von "Burst" auf tr("Notice")/"Hinweis"
- Fix: Zeitsprung zentriert auf aktuellen Frame mit Anker-basiertem Intervallfilter
  (vorher: feste Seitengrenzen, aktueller Keyframe oft nicht sichtbar)

### Changes
- i18n-Standardisierung: Q_OBJECT Makros, englische Source-Texte, QString(tr()) Fixes
- Visueller Abstand zwischen Navigationswidget-Gruppen
- Inaktive UI-Elemente dokumentiert (Chapters-Tabs, Configure-Button, videoFileInfo)

## v0.62.0 (2026-03-18)

**Landezonen, Zeitsprung & Uebersetzungen**

### Features
- Landezonen (Stream Point Detection): Automatische Erkennung von Schwarzbildern, Stille,
  Audioformatwechsel (AC3 acmod), Szenenwechsel und Seitenverhaeltnisaenderungen (MPEG-2 4:3/16:9)
  mit Schnittvorschlaegen und Projektpersistenz
- Zeitsprung: Keyframe-basierter Thumbnail-Browser mit Seitennavigation, dynamischen
  Seitenverhaeltnis-Thumbnails, Intervallfilter und Fenstergeometrie-Persistenz
- Interaktive Schwarzbild- und Szenenwechsel-Navigation ueber Buttons im Navigationswidget
- Histogramm- und Szenenwechselanalyse in Video-Decodern (MPEG-2, H.264, H.265)

### Fixes
- Fix: ttcut-demux Audio-Padding zerstoerte AC3 per-Frame acmod (Stereo/5.1 Wechsel gingen verloren)
  - Padding nutzt jetzt anullsrc + concat stream-copy statt Vollre-Encoding
- Fix: B/F Navigationsbuttons sprangen zurueck zur Cut-In Position

### Changes
- Deutsche Uebersetzungen vollstaendig aktualisiert (165 fehlende Strings ergaenzt)
- TODO bereinigt: erledigte Eintraege entfernt (Quick Jump, AC3 Demux)

## v0.61.7 (2026-03-11)

**MPEG-2 MKV Muxing Fix + Settings Migration**

- Fix: MPEG-2 finaler Schnitt erzeugte MKV ohne Video (nur Audio)
  - Root Cause: `setDefaultDuration()` fehlte im MPEG-2 MKV-Mux-Pfad (`onCutFinished`)
  - Matroska-Muxer verwarf alle Video-Packets wegen fehlender Timestamps
- Fix: MPEG-2 MKV hatte falsches Seitenverhältnis (16:9 als 4:3 dargestellt)
  - Root Cause: Matroska-Muxer nutzt `stream->sample_aspect_ratio`, nicht `codecpar->sample_aspect_ratio`
  - SAR wird jetzt auf Stream-Level kopiert (ES-Mux und Container-Remux Pfade)
- Settings-Pfad von `~/.config/TriTime/TTCut.conf` nach `~/.config/TTCut-ng/TTCut-ng.conf` migriert

## v0.61.6 (2026-03-09)

**Audio Drift Fix bei B-Frame Reorder**

- Fix: Akkumulierender A/V-Drift (bis 448ms bei 4 Segmenten) bei H.264 Streams mit B-Frames
- Root Cause: B-Frame Display-Order-Mapping verschiebt CutIn-AU nach vorn, Video hat weniger
  Frames als die Audio-Schnittbereiche vorgeben
- Smart Cut meldet jetzt tatsaechliche Start-AUs pro Segment via `actualOutputFrameRanges()`
- Audio keepList-Startzeiten werden nach Video-Smart-Cut an tatsaechliche Video-Ausgabe angepasst
- Restdrift: 32ms (= 1 AC3 Frame, physikalisches Minimum bei Audio-Stream-Copy)

## v0.61.5 (2026-03-08)

**H.264 POC Domain Mismatch Fix**

- Fix: POC-Domain-Mismatch am Re-Encode/Stream-Copy Uebergang wenn Encoder-SPS (MaxPocLsb=16)
  und Source-SPS (MaxPocLsb=64) unterschiedliche Parameter haben
- Patch: poc_lsb im letzten Encoder-Slice wird korrigiert um PicOrderCntMsb-Wrap zu verhindern
- Case A/B vereinheitlicht: beide erweitern Re-Encode zum naechsten Keyframe
- Encoder-SPS wird aus Inline-NAL im ersten Encoder-Paket geparst (x264 ohne GLOBAL_HEADER)
- `findH264SpsInPacket()` Helper eliminiert Code-Duplikation
- Post-Patch-Validation mit Brute-Force-Fallback falls Modulo-Clamping Wrap re-introduziert

## v0.61.4 (2026-03-01)

**Smart Cut B-Frame Reorder Boundary Fix**

- Fix: B-Frame Reorder Delay verschiebt CutIn ueber Stream-Copy-Grenze
- `needsIDR` Parameter durch `adjustedStreamCopyStart` Output-Parameter ersetzt
- EOS NAL wird immer vor Stream-Copy geschrieben (DPB-Flush)
- Pre-Extension der Decode-Range vor dem Decode-Loop

## v0.61.3 (2026-03-01)

**Navigation/Auto-Save Trennung**

- Navigation-Buttons (B/I/P) in der Navigation-Leiste speichern nicht mehr automatisch
- Trennung von Navigation und Schnittpunkt-Setzen: Navigieren ist frei, Speichern nur
  ueber explizite Buttons

## v0.61.2 (2026-02-27)

**Shared VideoStream Position Fix**

- Fix: Shared videoStream Position-Korruption bei Navigation und Schnittpunkt-Bearbeitung
- Expliziter Positions-Parameter in `updateCurrentPosition()` und `checkCutPosition()`

## v0.61.1 (2026-02-26)

**Frame Position Sync Fix**

- Fix: Slider/Positions-Label sprangen zum CutOut bei Navigation-Buttons
- Synchronisation zwischen Slider und Navigation-Buttons korrigiert

## v0.61.0 (2026-02-25)

**H.264 Smart Cut Inter-Segment Stutter Fix**

- `forced-idr=1` + `AV_FRAME_FLAG_KEY` im Encoder: IDR statt Non-IDR I-Frame
- First-Segment Override: Seg 0 = pure Stream-Copy (Decoder startet leer)
- SPS-Inline-Patching mit `max_num_reorder_frames`
- EOS NAL Typ 10->11 (H.264), Typ 36->37 (H.265)
- `computeReorderDelay()` verbessert (20 GOPs)
- MKV Duration Fix
- Separate Preview-Encoder-Preset-Einstellung (Standard: ultrafast)

## v0.60.0 (2026-02-21)

**CLI-to-Library Migration**

- Entfernt: 1.882 Zeilen Dead Code aus ttffmpegwrapper.cpp
- Audio-Cutting: ffmpeg CLI -> libav stream-copy API (verlustfrei)
- MKV-Muxing: mkvmerge CLI -> libav matroska muxer (Container-Remux + ES-Mux)
- Playback-MKV: QProcess/mkvmerge -> TTMkvMergeProvider
- macOS Support-Code entfernt (Linux-only)
- mplex ist das einzige verbleibende externe CLI-Tool

## v0.59.0 (2026-02-21)

**Audio Boundary Burst Detection**

- Burst-Icon + Text in Cut-Liste bei Audio-Bursts an Schnittpunkten
- Preview: Burst-Warnung + "Shift -1 Frame" Button
- Warndialog fuer verbleibende Bursts beim finalen Schnitt
- burstThresholdDb Einstellung (konfigurierbar)
- Audio-Burst-Erkennung via libav (vorher ffmpeg CLI)

## v0.58.0 (2026-02-19)

**Non-IDR I-Frame Fix + Audio Quality**

- `analyzeCutPoints()` erkennt Non-IDR I-Frames -> `needsReencodeAtStart = true`
- EOS NAL Upgrade: H.264 Typ 10->11, H.265 Typ 36->37
- `writeParameterSets()` nach EOS NAL und vor Stream-Copy
- SSIM-Verbesserung: 0.761 -> 0.995
- Audio: Click False Positive Fix (-80dBFS Silence Floor)
- Audio: Duration Mismatch 122ms->6ms (Off-by-One Fix)
- H.264 Smart Cut Display-Order Mapping und Interlace-Support
- A/V Sync Offset Vorzeichen-Konvention korrigiert
- ttcut-demux: Bitrate-Autoerkennung via ffprobe

## v0.57.0 (2026-02-15)

**HEVC B-Frame Detection + Audio Fixes**

- Fix: ttffmpegwrapper.cpp hardcoded alle Non-Keyframes als P-Frames
- Echte `slice_type` Erkennung aus HEVC Slice Header Bitstream
- Fix: Cut-List Navigation und Cut-In/Out Editing
- Fix: Cut-Edit Navigation und First-Click Frame Display

## v0.56.0 (2026-02-14)

**AC3 Header Repair + .info Languages**

- Fix: `ttcut-ac3fix` Heuristik ">=384kbps + stereo = 5.1" war falsch
- Decode-Test vor Reparatur: ffmpeg Validierung
- Audio-Sprachen aus `.info`-Datei laden
- Fix: Verlustfreies Audio-Cutting via libav stream-copy
- Fix: Thread-Pool Deadlock, AC3 Parser Hang bei E-AC3

## v0.55.0 (2026-02-13)

**Security + Quality Fixes**

- Fix: Kritische Sicherheits- und Qualitaetsprobleme aus Code-Audit

## v0.54.0 (2026-02-13)

**AC3 Repair + Decoder Fixes**

- AC3 Header Repair in ttcut-demux integriert
- Fix: Decoder EOF Drain und Cut-List Spalten-Navigation

## v0.53.0 (2026-02-12)

**Smart Cut Improvements**

- Fix: ttcut-demux A/V-Sync fuer H.264/H.265
- Smart Cut Display-Order Fix, A/V Sync und 10-bit Support
- Smart Cut Quality Test Suite

## v0.52.0 (2026-02-08)

**Initial TTCut-ng Release (git)**

- Erste Git-Version basierend auf TTCut-ng 0.51.0
- Projektstruktur bereinigt, Build-Artefakte entfernt
- Deutsche Uebersetzungen erweitert (de_DE)
- Multi-Stream Audio/Subtitle Support mit Sprachcodes

## v0.51.0

**H.264/H.265 Playback + UI**

- H.264/H.265 Video-Wiedergabe mit A/V-Sync (via mpv + temporaere MKV)
- Interlace-Erkennung fuer MPEG-2 Re-Encoding
- Deutsche Uebersetzungen fuer zentrale UI-Elemente (de_DE)
- Video-Editing Farbschema (I=blau, P=gruen, B=orange)
- Theme-Icons via QIcon::fromTheme() mit Fallback
- ttcut-demux Multi-Core Optimierung (parallele Extraktion)
- Frame-Position wird beim Videowechsel beibehalten
- Encoder-Modus standardmaessig aktiviert
- Fix: Frame-Suche mit korrekter Referenz und HD-skaliertem Schwellwert
- Fix: Preview Prev/Next Button-Reihenfolge (waren vertauscht)
- Fix: Uebersetzungsdateien fuer installierte Pakete

## v0.50.3

- Fix: MPEG-2 Schnitt bei kurzen Segmenten ohne I-Frames
- Fix: build-package.sh Git-Info in Version

## v0.50.2

- Play-Button im Current-Frame Widget (mpv mit Audio)
- Previous/Next Cut Navigation im Preview-Dialog
- A/V-Sync Offset fuer demuxte Streams (.info)
- ttcut-demux: Timestamp-Reparatur (ffmpeg +genpts+igndts)
- Vim-Style Tastaturkuerzel (j/k/g/G/[/])
- Tastaturkuerzel-Hilfe (Help-Menue)
- Benutzer-Warnungen (ungesicherte Aenderungen, A/V-Laengendifferenz)
- Elementary Streams als Pflichtformat (kein automatisches Container-Demuxing)
- Fix: MPEG-2 Decoder Crash beim Rueckwaerts-Scrollen nahe Videostart
- Fix: Cut-Liste weisser Hintergrund bei Dark Themes
- Fix: Veraltete Preview-Dateien zeigten falsches Video
- Fix: Container-Erkennungsreihenfolge (mpegvideo vs. mpeg)
- Fix: Doppelte ES-Datei-Loeschung im Mplex-Provider

## v0.50.1

- VDR markad Marks-Datei Support in ttcut-demux
- VDR Marker Integration (Auto-Load aus .info-Dateien)
- Marker in Cut-Tab und Marker-Tab sichtbar
- Fix: Audio-Dateiname-Zaehler in ttcut-demux

## v0.50.0

**H.264/H.265 Smart Cut + ttcut-demux**

- H.264/H.265 framegenauer Smart Cut (TTESSmartCut)
  - Nativer NAL-Unit-Parser (TTNaluParser) mit mmap I/O
  - Re-Encode partieller GOPs an Schnittpunkten (~0,5% der Frames)
  - Stream-Copy vollstaendiger GOPs (kein Qualitaetsverlust, ~99,5%)
  - Audio-Cut via libav stream-copy
  - MKV-Ausgabe mit Kapitelmarken
- ttcut-demux: TS-Demuxer fuer H.264/H.265
  - Elementary-Stream-Output mit .info Metadaten
  - Audio Padding/Trimming fuer A/V-Sync
  - VDR Marks Support
  - Filler-NALU-Entfernung
- Encoder-UI-Einstellungen an Encoder angebunden (CRF, Preset, Profil)
- Neues GUI-Layout mit TreeView-Widgets und Multi-Input-Stream-Support
- Batch-Muxing via Mux-Script-Generierung
- Debian-Paketierung und Desktop-Integration

## v0.40.0

- UI-Umstrukturierung: Cut-Liste 3-Spalten-Layout, Copy-Button, Tabs umbenannt
- Separater Subtitles-Tab im Hauptfenster
- Fix: Stream-Navigator zeigte keine Cut-Marker
- Fix: FFmpeg Encoding-Parameter
- Fix: Audiooffset-Spalte zeigte GUID statt "0"
- Subtitle-Support im XML-Projektdatei-Format

## v0.30.0

**SRT Subtitle Support**

- SRT-Untertitel-Support mit Preview-Integration (von Minei3oat)
- Auto-Loading: SRT-Dateien passend zum Videonamen werden automatisch geladen
- Preview-Overlay: Untertitel im Videobild via QPainter
- mpv Preview: Untertitel via --sub-file Parameter
- Schnitt: Untertitel werden zusammen mit Video/Audio geschnitten
- Migration von QGLWidget zu QImage/QPixmap (volle Wayland-Kompatibilitaet)
- Migration von transcode zu libavcodec API fuer Frame-Encoding
- Migration von mplayer zu mpv fuer Preview

---

## Legacy Releases (TTCut original, B. Altendorf 2005-2008)

### v0.20.4

- Release Candidate fuer 0.21.0
- Thread-Support und Thread-Abort
- Kleinere Bugfixes

### v0.20.3

- Thread-Support und Multi-Thread Progress-Dialog
- Native Style unter OS X
- Phonon fuer Previews (Linux und OS X)
- Multiple Input-Video Grundfunktion
- Cut-Out/Current-Frame mit QImage (kein OpenGL mehr noetig)
- Marker-Positionen pro Video, gespeichert im Projektfile

### v0.20.2

- Qt >= 4.4.3 erforderlich
- Phonon-Libs erforderlich
- AVData-Controller fuer Multiple-Video-Support

### v0.20.1

- Stabil unter OS X und Linux
- Umfangreiches Refactoring
- Unit-Tests und Valgrind-Pruefungen
- Fix: Memory Leaks und Buffer Overflows
- Neues XML-Projektdatei-Format

### v0.20.0

- OS X Support
- Neuer TTFileBuffer (Qt-basiert)
- Video-Stream-Analyse beschleunigt
- Speicherverbrauch reduziert

### v0.19.6

- Fix: Layout unter Qt 4.3.x
- Speicherverbrauch reduziert
- IDD-Leseleistung verbessert

### v0.19.5

- Fix: Audio/Video asynchron nach mehreren Schnitten
- Fix: Projektdatei-Extension anhaengen

### v0.19.4

- Fix: Ressourcen-Freigabe nach neuem Video/Projekt
- Fix: Crash bei langem Video nach kurzem
- Fix: Segfault bei Preview/Cut mit Encoder

### v0.19.3

- Elementary Cut-Stream nach Mplex loeschen
- Alle Audio-Dateien zu Video automatisch lesen
- Audio-Only-Cut implementiert
- Fix: Crash bei Video ohne Audio

### v0.19.2

- Fix: Deutsche Umlaut-Behandlung
- Progressbar fuer AC3/DTS Audio-Headerlist
- User-Abort waehrend Video-Cut
- Fix: Percent/Progress bei grossen Dateien

### v0.19.1

- Qt3-Support entfernt
- Fix: Aspect-Ratio-Wechsel (4:3 -> 16:9) bei Closed GOPs
- Direktes Mplexing nach Cut
- Mplex-Optionen im Settings-Dialog

### v0.11.2 - v0.11.4

- Qt3-Support vollstaendig entfernt
- GUI in .ui-Dateien
- Stream-Navigator fuer Cut-Visualisierung
- Fix: Cut-Liste bei neuer Cut-Range
