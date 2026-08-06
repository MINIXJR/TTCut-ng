# Changelog

All notable changes to TTCut-ng are documented in this file.

## Unreleased

### Changed

- **The application now builds against Qt 6 (6.7 minimum, developed against
  6.10) instead of Qt 5.15.** `find_package(Qt6 ...)` replaces the Qt5
  lookup in `CMakeLists.txt`; the linked components are Core, Widgets, Gui,
  Xml, OpenGL, and OpenGLWidgets. The `Network` component is gone — nothing
  in the tree used it. The deprecation gate
  (`QT_DISABLE_DEPRECATED_BEFORE`) is raised from `0x060000` to `0x060700`,
  closing off everything Qt deprecated between 6.0 and 6.7. Debian packaging
  (`debian/control`, `debian/rules`) builds against `qt6-base-dev` and
  `qt6-l10n-tools`.

### Fixed

- **"Frozen" window areas under KWin with fractional display scaling
  (150 %/175 %) and a large window no longer occur.** The mpv render
  widget — an OpenGL widget kept permanently visible (though covered)
  behind the still frame — turned out to be the in-app trigger of a KWin
  presentation bug: parts of the window stopped refreshing until the
  first playback. The widget stack now keeps the render widget truly
  hidden outside playback and only exposes it for the duration of a
  playback session. The previous workarounds (X11 via
  `QT_QPA_PLATFORM=xcb`, integer scaling, playing once) are no longer
  needed.

- **The whole main window was visibly rebuilt when the first video was
  opened.** The mpv render widget (a QOpenGLWidget) was created lazily at
  stream open; the first OpenGL widget entering an already visible window
  forces Qt to destroy and recreate the native top-level window, which
  looked like the entire UI reloading. The widget is now created before
  the window is first shown, so the window is GL-capable from the start
  and is never rebuilt; the mpv render context itself is still set up
  lazily at stream open.

- **4:3 MPEG-2 still frames were displayed too narrow (storage aspect
  instead of 4:3), unlike playback.** The still-frame path handled only the
  16:9 aspect code; 4:3 recordings showed the current frame at the raw
  720/576 storage shape while mpv playback showed the correct 4:3 picture.
  All MPEG-2 display aspect codes (4:3, 16:9, 2.21:1) are now corrected,
  in the upscale direction so no detail is lost before the widget scaling.
  Still frame and playback now show the same shape.

- **After stopping playback, still frames could take on the aspect of the
  file's beginning.** The aspect lookup used a display-cache marker that
  the stop path invalidates, falling back to the file's first sequence
  header — on recordings starting with a 4:3 ad block, a 16:9 still was
  squeezed to 4:3 by the next repaint (window resize, stop). The lookup is
  now anchored to the frame actually displayed.

- **Preview dialog: the Play button stayed on "Play" although playback was
  running.** Clicking Forward auto-plays the next clip; any mpv error-level
  log message arriving right after (typically "Can not open external file
  preview_00N.srt") reset the button while playback continued. Two causes,
  both fixed: the dialog no longer treats non-fatal mpv log messages as
  playback failures (it only logs them, like the main window's player), and
  a preview clip range without any subtitle entries no longer produces an
  empty `.srt` — `cutSubtitleTracks` removes the 0-byte file instead of
  handing it to mpv or the muxer. Empty subtitle files also no longer end
  up as empty subtitle tracks in the final MKV.

- **libav messages leaked to the console after the first mpv playback,
  despite disabled libav logging.** libmpv takes over the process-global
  av_log callback at `mpv_create()` and restores ffmpeg's *default* stderr
  callback — not the application's — when the mpv context is destroyed.
  After the first Play from the Current Frame widget, every later libav
  operation (frame decode, preview encode, audio probing) therefore logged
  raw to the terminal, bypassing the "Log libav messages" setting. The
  callback (now in `common/ttavlog.{h,cpp}`) is re-installed after every
  mpv teardown.

- **"Cannot load libcuda.so.1" was printed to the terminal once per
  playback session on non-NVIDIA systems.** libmpv eagerly loads all GPU
  hwdec interops when the render context is created, even with hardware
  decoding disabled; the failed CUDA probe is reported by ffnvcodec's
  loader directly on stderr, outside any logging mechanism. With hardware
  decoding off (the default), interop loading is now blocked entirely
  (`gpu-hwdec-interop=no`); an `MPV_HWDEC` override keeps the interop path
  available.

- **Esc in the audio-burst warning dialog now cancels the cut instead of
  starting it.** The dialog's old `QMessageBox::warning(parent, title, text,
  button0Text, button1Text)` overload is removed in Qt6; the replacement
  (a `QMessageBox` instance with `addButton(..., role)`) changes Esc's
  behaviour along the way. Previously Esc returned `-1`, and `ret != 1`
  mapped that to "Cut anyway" — Esc silently starting a warned cut was a
  trap, not a feature. Esc now maps to the button with `RejectRole`
  (Cancel), matching what a user pressing Esc actually expects. Deliberate
  behaviour change, not a side effect; see commit `b1e65bcf`.

- **Subtitles were neither cut nor muxed for H.264/H.265 (final cut and
  preview).** Subtitle streams opened alongside video and audio were ignored
  by the smart-cut pipeline and did not appear in the output MKV. Both
  preview and final cuts now cut and mux subtitle streams alongside video and
  audio.

- **"Delete ES files after muxing" setting was ignored by H.264/H.265 cuts.**
  The cleanup hook ran only after MPEG-2 multiplexing; for H.264/H.265 cuts
  the setting was never checked. Intermediate cut video segments are now
  cleaned up when the setting is enabled.

- **SRT markup is now rendered in the still-frame overlay instead of showing
  literal tags.** Subtitles containing formatted text (`<font color>`, `<i>`,
  etc.) were displayed with the markup visible; the overlay now decodes and
  applies the formatting. The default subtitle color is now white, the DVB
  SRT convention (previously yellow); tag colors still override it.

- **Cut subtitle files no longer end with a stray MPEG-2 sequence-end code.**
  SRT files are text and do not use NAL units; an accidental MPEG-2
  sequence-end was appended to every cut subtitle file. The erroneous append
  is removed.

- **Subtitles are now shown during main-window playback, and the still-frame
  overlay no longer depends on subtitle-load order.** PLAY never passed a
  subtitle to the player; it now passes the source SRT to mpv via
  `--sub-file` at load time. Separately, the still-frame overlay was wired
  only once, when the AV item changed — for small/fast videos the subtitle
  file (loaded asynchronously) could still be loading at that point, leaving
  the overlay permanently empty. The overlay is now also re-wired when the
  asynchronous subtitle load finishes, and the current still frame is
  refreshed immediately so it appears without requiring navigation.

- **A fast (cache-hot) MPEG-2 cut could mux an MKV without the subtitle
  track.** `TTAVData::onDoCut` started the MPEG-2 video task in the thread
  pool first, then ran the audio and subtitle cuts synchronously afterwards,
  appending their results to the video task's mux list item. Both
  synchronous cuts pump the event loop while reporting status; if the (tiny,
  cache-hot) video task finished during that pump, the queued pool-exit
  signal fired and muxed the item before the later subtitle append had
  landed. Audio and subtitles are now cut to completion before the video
  task is started, so all mux-list appends exist before the pool can ever
  trigger the mux.

- **UTF-8 SRT files were read as Latin-1, garbling non-ASCII characters.**
  `TTFileBuffer::readLine` maps raw bytes 1:1 onto `QChar`s; SRT files
  produced by the ttcut-demux workflow are UTF-8, so multi-byte sequences
  (e.g. German umlauts) showed up as mojibake in the overlay and were
  double-encoded in cut subtitle output (final MKV tracks, preview SRTs).
  `TTSrtSubtitleStream::createHeaderList` now recovers the original bytes
  and decodes subtitle text as UTF-8, falling back to Latin-1 for genuinely
  Latin-1-encoded legacy files.

## v0.79.0 (2026-08-03)

**The build system is CMake now — the last Qt5-based release before the Qt6 migration**

This release intentionally closes out the Qt5 era: the commit is additionally
tagged `qt5-final` as the immutable fallback point the Qt6 migration plan
calls for. A Qt6 feasibility probe against this tree already passed (three
one-line fixes to a full build, playback working); details in `TODO.md`.

### Changed

- **qmake is gone; TTCut-ng builds with CMake and Ninja.**
  `cmake -B build -G Ninja && cmake --build build` replaces the qmake/make
  invocation, and the Debian package builds through debhelper's cmake
  buildsystem. Building from source now needs `cmake` and `ninja-build`
  installed. `compile_commands.json` comes straight out of the configure
  step, so `bear` is no longer needed. The diagnostic tools and the
  burst-detector probe (`tools/ttcut-burst-probe`, the last qmake consumer
  in the tree) are CMake targets as well.

### Fixed

- **`build-package.sh` left the dch entry behind in `debian/changelog`.**
  The restore ran after the script had changed into the package build
  directory — an rsync copy without `.git` — so it failed silently and
  every run left the source tree modified. The cleanup is now anchored to
  the source tree.

## v0.78.0 (2026-08-02)

**The window remembers its size, an MPEG-2 cut reports when it is done, and a headless cut no longer hangs**

### Fixed

- **`--auto-cut` never ended by itself after an MPEG-2 cut.** The cut ran
  correctly, the MKV was finalised — and the process then sat idle until
  someone closed the window. The signal that announces a finished cut was
  simply never sent on that path: MPEG-2 finishes inside a slot triggered by
  the thread pool, while the H.264 and audio-only paths run synchronously and
  emit at their end. Anyone scripting a cut had to kill the process on a timer.

- **An MPEG-2 cut now shows the completion dialog.** This is the other half of
  the same defect: because the signal was missing, the "Cutting Complete"
  message with output file and lengths never appeared for MPEG-2, while H.264
  cuts showed it. It has been absent since the dialog was introduced.

- **A failed cut no longer hangs a headless run either.** The H.264 path had
  three exits — engine start, the cut itself, muxing — that returned without
  announcing anything, so a failure left the process running forever. All three
  now report, and because "finished" no longer implies "succeeded", the dialog
  distinguishes the two: a failed cut shows a warning naming the cause instead
  of claiming success.

### Changed

- **The stream point detection settings moved into the settings dialog**, into
  a category of their own ("Landezonen"), reachable directly through the icon
  next to the stream point list's heading. The navigation panel now shows only
  the list of detected points. That is what lets the main window be made
  smaller: its content demanded 1067 px of height before, the settings tab
  alone accounting for 325 of them, and the window now stops at 863.

### Fixed

- **The window no longer forgets its size.** Every start came up at 1024×768,
  no matter what size the window had when it was closed: `main()` called
  `resize(1024, 768)` two lines after showing the window, discarding the
  geometry that had just been restored. The line dates back to the very first
  commit and had the same effect on the previous implementation — it stayed
  unnoticed because the stored value was not readable. The window now opens at
  the size it was left at, or at 80% of the screen when there is no stored
  value.

  A hand-edited size that no longer fits is reduced to the screen it opens on.
  The window *position* can only be honoured under X11: a Wayland client is not
  permitted to place its own window, so `x` and `y` are recorded but only take
  effect with `QT_QPA_PLATFORM=xcb`.

- **The "go to frame" dialog stored its size in a second settings file** under
  `Unknown Organization`, because the application never set an organisation
  name. The value now lives in the application's own settings file, and the
  stray file is picked up and removed on the first start. Note that the
  directory is shared with other applications and therefore stays.

### Changed

- **Window position and size are stored as plain numbers** (`x`, `y`, `width`,
  `height`, `maximized` under `[MainWindow]`) instead of a serialised byte
  array, so they can be inspected and edited by hand. An existing entry is
  converted automatically on the first start.

- **The detection thresholds are readable too.** Black frame, scene change and
  logo threshold and the minimum silence duration were the only settings stored
  as binary blobs, because they were the only ones held as `float` — a type
  QSettings cannot render as text. They are plain decimals now. Values from an
  older configuration are rounded when read, so an inherited `0.98` does not
  reappear as `0.9800000190734863`.

- **677 lines of dead code and 16 unused images removed.** Nothing that ran:
  the largest item was a process-output window that was compiled into every
  build but never created — its pointer permanently null, two of its three
  methods already reduced to comments. Alongside it six exception classes that
  were never thrown, two unused parameter-set classes, three methods the linker
  had already discarded, and resource entries nothing referenced. The cut
  output is unchanged, verified packet by packet against the previous version.

## v0.77.0 (2026-07-31)

**Pillarbox detection works on H.264/H.265, and the search no longer crashes when cancelled**

### Features

- **Pillarbox detection now runs on H.264 and H.265.** The setting existed and
  could be switched on, but for those codecs the scan never started — the old
  implementation was tied to the MPEG-2 sequence headers, and its workers
  decoded into empty images without a frame index. It has been rebuilt as a
  full-stream scan on the same decode path the other searches use, so 4:3
  content inside a 16:9 frame is found for MPEG-2, H.264 and H.265 alike.
  The related "Aspect ratio (4:3/16:9)" detection is unchanged and remains
  MPEG-2 only — it reads the sequence headers, which H.264/H.265 do not have.
  The tooltips now say so instead of leaving it to be discovered.

  Two judgement calls are visible in the results. A frame that cannot support
  a statement — a black frame, a bar wider than 1.5× the nominal width (dark
  picture content at the edge), a decode error — is skipped rather than
  counted as "no pillarbox", so a dark night scene no longer ends the run.
  And a detected change is reported at the frame where it happens: the scan
  samples at intervals, then refines the transition to be frame-exact.

- **New setting "Sample distance (s)" for the pillarbox scan** (stream point
  settings). Smaller values scan more slowly; the reported position stays
  frame-exact either way. The four pillarbox controls now enable and disable
  independently of the aspect-ratio ones, and all four follow the check box
  state when the dialog is opened — previously two of them stayed greyed out
  until the box was toggled once.

- **The stream point settings tab is laid out in two levels.** Detection kinds
  are headings, their value fields are indented underneath, and the audio and
  video sections are separated. Nothing moved between tabs; it is the same
  settings, grouped so the value belonging to a check box is visible at a
  glance.

### Fixes

- **Cancelling a running search could crash the application.** Aborting a task
  pool freed tasks that were still being handed back by their worker threads
  (use-after-free, reproduced under ASAN). Task teardown now runs `cleanUp()`
  before the terminal signal rather than after it, and the queue is only
  touched from the pool's own thread — the previous code manipulated it from
  whichever worker happened to finish, which is a data race regardless of
  whether it was ever observed to bite.

- **Jumping to a marker landed on the following I frame.** The jump now lands
  on the marker itself. The frame type shown on the fast slider follows the
  jump instead of keeping the previous frame's type.

- **Aspect markers were placed by bitstream position, not display position.**
  On material with B frames the two differ, so a detected change was marked a
  few frames away from where it is seen. The detection now looks up the
  display position.

- **The progress dialog blocked the window while it was hidden**, and its
  window close button (X) did nothing where the Cancel button worked. The X
  now cancels like the button.

- **ttcut-demux aborted when the output directory was given as a relative
  path.** The audio padding step wrote a concat list whose entries the demuxer
  resolves relative to the list file's own directory, so `dir/file` was looked
  up as `dir/dir/file`. The run died with exit 254, no `.info` file and no
  error message on screen. Absolute paths — the VDR_Demux.sh workflow — were
  never affected.

### Changes

- **README: system requirements, and two missing build packages.**
  `libqt5opengl5-dev` and `libmpv-dev` were absent from the install line even
  though `qmake` fails without them; libmpv was still listed as an optional
  video preview although it has been linked in as a library since v0.71.0.
  The claim that Wayland needs `QT_QPA_PLATFORM=xcb` is gone — it is a
  fallback for a misbehaving compositor now, not a requirement.

- **`TODO.md` holds only open work.** Finished work moved to
  `docs/completed-work.md` together with the evidence it was closed on, so a
  question that has already been investigated can be recognised as such. Six
  topics that were being tracked twice are now tracked once.

- **A known limitation was identified as a compositor bug, not ours:** under
  KWin 6.7.2 with fractional display scaling and a maximized window, large
  painted areas are not refreshed on screen — the still frame keeps showing
  the previous picture while frame number and timecode advance. Workarounds
  (integer scaling, do not maximize, or `QT_QPA_PLATFORM=xcb`) and the
  evidence are in `TODO.md`.

## v0.76.1 (2026-07-26)

**Quality-Check liest wieder die richtigen Extra-Frame-Positionen**

### Fixes

- **ttcut-quality-check rechnete auf aktuellen Demux-Ergebnissen ohne
  Extra-Frame-Korrektur.** Das Werkzeug las weiterhin das `.info`-Feld
  `es_extra_frames`, das `ttcut-demux` seit v0.76.0 nicht mehr schreibt
  (ersetzt durch `es_doubled_pts_aus` + `es_total_aus`). Es bekam damit
  immer eine leere Liste, und die Zeitkorrektur fiel still aus — an realem
  MPEG-2-Material (150 Feldpaare, 25 fps) sind das 6 Sekunden Zeitfehler am
  Dateiende, die direkt in die A/V-Sync- und Dauer-Prüfungen gingen. Das
  neue Feld wird jetzt gelesen, das alte bleibt als Rückfall für ältere
  `.info`-Dateien. Auf PAFF-Material verwirft das Werkzeug die Liste mit
  einer Warnung, weil dort jedes Halbbild eine eigene Zugriffseinheit ist
  und die Positionen nicht zu den Schnitt-Frames passen.

## v0.76.0 (2026-07-26)

**H.265 RASL-preserving seam, damaged-recording repair in ttcut-demux, goto-frame dialog**

### Features

- **Jump to a frame or timecode from the position display.** Clicking the
  position readout in the "Current Frame" widget opens a small dialog with two
  synchronized fields: the frame number and the timecode. Editing one updates
  the other, so you can type either `63376` or `01:28:01` and land on the same
  picture; a German decimal comma is accepted as well. Values beyond the end of
  the recording are limited to the last frame, and the dialog is prefilled with
  the position you clicked on.

- **The cut result length is shown when cutting finishes.** The completion
  dialog now reports the source duration, the resulting duration and how much
  was removed, e.g. `Source: 1:30:00 / Result: 42:15 (47:45 removed)` — for
  video cuts and for audio-only cuts alike.

- **Cut preview: stays on the last frame, and Forward starts playback.**
  Reaching the end of a clip previously jumped the preview back to the first
  frame; it now holds on the last frame instead, and pressing Play restarts
  from the beginning in one click. Forward and the cut selector now start
  playback right away instead of leaving the preview paused on the newly
  loaded frame. Back is two-stage, like the skip-back button on a CD player:
  the first press returns to the start of the current clip, the second moves
  to the previous cut — both as a still image, so Back never starts playing
  on its own.

- **H.265 Smart Cut: the RASL window at cut-in seams is preserved** — cutting
  into broadcast HEVC (CRA-based open GOPs, e.g. UHD/HLG channels) silently
  dropped the leading pictures right after the seam, visible as a short freeze
  of roughly 100–200 ms per cut. Those frames are now kept: the seam runs
  without an end-of-bitstream NAL, uses the source parameter sets from the
  start of the segment and rewrites the re-encoded slices so the leading
  pictures resolve against them. When the source cannot be matched (encoder
  parameters, scaling lists, POC range), the previous behavior is used
  unchanged and the affected seam is reported in the cut progress window and
  the log. Material without leading pictures at the seam (IDR-only streams)
  and all H.264 cutting are byte-for-byte unaffected.

- **ttcut-demux detects and repairs damaged recordings** — TS-packet
  corruption and VDR signal-loss segment boundaries are now found by a
  frame-scale PTS-gap scan across ALL segments of a multi-file recording
  (not just the first) and repaired timeline-faithfully: audio-only gaps get
  codec-native, layout-faithful silence (correct AC3 acmod, no channel-count
  guessing), video-only gaps trim the matching amount of audio. Video gap
  detection uses decode-order DTS jumps rather than PTS (PTS is not
  monotonic under B-frame reorder); repair assembly is segment stream-copy,
  no re-encode of surviving audio, with overlapping/touching repair windows
  coalesced before splicing. Reported in new `.info` fields
  (`es_missing_frames`/`es_missing_ranges`, `corrupt_frame_ranges`,
  `audio_N_silence_ms`/`audio_N_removed_ms`) plus an independent loud
  count-check warning for silent (no-PTS-jump) frame loss. TTCut-ng shows
  the affected ranges as clustered error landing zones ("Videoverlust: X–Y
  (T s) — Audio angepasst", a distinct "Signalverlust-Ende" marker for
  losses over 2 s, "Bildstörungen: X–Y" for corrupt-but-retained frames).
  Measured on real material: a mildly damaged recording (07x11) repaired
  with 0 failures and −11 ms residual drift; a heavily damaged 5-segment
  recording with ≈7.6 min of combined signal loss (07x12) went from −35 s
  drift to −23 ms across 496 applied splices, with all 4 segment-boundary
  loss ranges reported. Undamaged recordings remain byte-identical.

### Changes

- **Consistent dialog buttons, and "OK" is now "Save" in the settings.** The
  primary action is the one Enter triggers, and it sits on the right: the cut
  dialog is now `[Reset to defaults] … [Cancel] [Start]` with Start as the
  default (previously Enter triggered "Reset to defaults"), the preview dialog
  triggers Start/Stop, and the about dialog OK. The settings dialog is
  deliberately different: it saves immediately, with no separate apply step, so
  its confirm button is now labelled **Save** and Enter deliberately does
  nothing — a stray Enter while editing a field can no longer close and commit
  the whole dialog. Escape still discards everywhere.

### Fixes

- **The stream-integrity warning no longer recommends ProjectX.** The dialog
  shown for a recording whose `.info` reports decode errors advised demuxing
  the file with ProjectX, which is not part of the workflow — `ttcut-demux`
  handles every codec, and since this version it detects and repairs exactly
  the kind of gaps the warning is about. It now says so.

- **MPEG-2 chapter marks no longer run past the end when cutting a selection.**
  With automatic chapters enabled, cutting only selected entries computed the
  chapter total from the full project cut list, so chapters could be placed
  beyond the end of the output file. The total now follows the entries actually
  cut.

- **Playback of H.264/H.265 no longer leaves a temporary file behind, and a
  stream without a usable frame rate is refused cleanly.** Closing the window
  during playback used to leave the temporary playback MKV in the temp
  directory; it is now removed. A stream whose frame rate cannot be determined
  reports that playback is unavailable instead of computing an undefined frame
  duration.

- **Preview dialog: the audio-burst warning is fully readable and its
  correction button points the right way.** The warning shared a row with the
  cut selector and four buttons, so the text was clipped mid-word without an
  ellipsis — the German translation hit the limit first. It now occupies its
  own full-width row, whose height is reserved even without a burst so the
  video frame no longer jumps between cuts. The button's arrow was set once at
  construction while its caption changed per burst type, so a burst at the
  cut-in showed "+1 Frame" next to a left arrow; caption, arrow and tooltip now
  come from one place and cannot drift apart. The button reads "1 Frame" and
  names the affected cut point and direction in its tooltip — the previous
  captions were never translated and showed English in the German interface.
  The cut selector may now grow to show its full range instead of being pinned
  to 220 px.

- **H.264 Smart Cut: frame-accurate cuts on IDR-free DVB material (e.g.
  ARD/ONE progressive HD) no longer corrupt the seam.** When the stream-copy
  started at a non-IDR keyframe with leading B-pictures, the standard seam
  silently emitted the keyframe several display slots early followed by the
  corrupted leading pictures (defect A). Such seams now use the
  SPS-unification path, whose single POC domain lets the leading pictures
  resolve against the re-encoded frames (correct content at re-encode
  quality); IDR seams and leading-pic-free seams keep the previous
  byte-identical path. Two latent unification defects this exposed were
  fixed along the way: ref-pic-list modification diffs are now translated
  from the encoder's modular PicNum domain into the linear source numbering
  (re-encodes longer than 16 frames referenced pictures a full frame_num
  cycle back — pixel-neutral "reference picture missing" floods), and the
  MMCO neutralization at the seam is PAFF-only now (blanket-emptying the
  marking commands damaged frame-coded material whose adaptive reference
  management is load-bearing). New quality gate:
  `tools/diag/gate_h264_seam.sh`.
- **H.264/H.265: anamorphic SD material is no longer shown distorted in the
  still-frame windows.** The CurrentFrame and CutOut windows displayed
  H.26x frames at storage resolution (720×576 = 5:4), ignoring the signaled
  sample aspect ratio — anamorphic widescreen (SAR 16:11, DAR 20:11) appeared
  ~45 % too narrow, 4:3 DVB (SAR 12:11) ~9 %, while mpv playback letterboxed
  correctly. Still frames now apply the SAR in the upscale direction (width
  for SAR > 1), matching the playback shape; square-pixel material and the
  MPEG-2 path are unchanged.
- **H.264 Smart Cut: re-encoded segments no longer come out as uniform gray
  frames.** The SPS-unification slice rewriter emitted (and consumed) the
  CABAC alignment bits unconditionally; per H.264 7.3.4 they exist only when
  the slice header does not already end on a byte boundary. Whenever the
  rewritten header (widened frame_num/poc_lsb fields plus the new pps_id)
  happened to land exactly on a byte boundary, a spurious 0xFF byte was
  inserted before the CABAC payload — the decoder silently discarded the
  slice and concealed the whole frame gray, and the following P-frames
  (mostly skip macroblocks) carried the gray until the next stream-copy IDR.
  Stream-dependent bit-length luck: measured on a mixed MBAFF+PAFF recording
  (first re-encoded IDR hit exactly 48 header bits → 161 gray frames), while
  neighbouring cut positions on the same recording were fine. Alignment is
  now conditional on both the read and write side; outputs whose headers
  never hit a byte boundary stay byte-identical (verified on progressive,
  MBAFF and full-PAFF material).
- **H.264 PAFF: navigating to a field-pair frame no longer hangs for ~2
  minutes.** The still-frame decoders of the CurrentFrame/CutOut windows
  adopt the frame index from the stream owner but never ran the index-build
  pass that detects PAFF, so their decode-order tagging counted field packets
  as frames — a field-pair target was never recognized as delivered and the
  decoder drained the whole file to EOF (twice, ~53 s each) before falling
  back to a neighbor frame. The owner's measured stream state (PAFF flag,
  SPS field parameters) now travels with the adopted index; the same decode
  is instant (13 ms measured). Affects mixed MBAFF+PAFF and full-PAFF
  recordings.
- **H.264/H.265: legitimate PAFF field pairs are no longer reported as
  defects.** ttcut-pts-analyze skips the PTS-grid heuristic for H.26x
  (field-rate PTS is normal there; measured on a mixed MBAFF+PAFF recording:
  1296 false positives, zero real defects). The .info contract is now
  `es_total_aus`/`es_doubled_pts_aus` (raw AU numbering; the old
  `es_extra_frames` key is retired and no longer parsed), and TTCut
  classifies the remaining candidates through the PAFF merge map with an
  AU-count guard — fixing false "defective frames" dialogs/markers and an
  audio-timing error that grew to ~29 s on mixed MBAFF+PAFF recordings.
  Re-demux affected recordings to refresh their .info.
- **ttcut-demux: fixed a buffering race that truncated the extra-frame list
  in the .info** (the analysis tool's stderr was interleaved into the
  captured stdout CSV, cutting the list mid-stream and appending garbage).

- **A recording too damaged to smart-cut no longer crashes the program.**
  When the display-order map cannot be built consistently on a badly
  corrupted stream, the preview previously kept attempting the doomed cut
  for every segment and then tried to play clips that were never written —
  on a large stream this exhausted threads/memory and aborted the whole GUI.
  The preview now stops on the first un-cuttable segment and shows a dialog
  explaining that the recording is too damaged for frame-accurate cutting,
  instead of failing silently or crashing.

- **Legitimate field pairs are no longer added as timeline markers.** When
  opening interlaced MPEG-2 material, parser-confirmed field-picture pairs
  (doubled-PTS positions that are normal encoder output, not a defect) were
  silently imported as `Error` stream points and cluttered the timeline
  without asking. They are now counted for logging but never turned into
  markers; only real defects, audio gaps, and demuxer-reported loss/
  corruption produce markers. The internal audio-drift correction, which
  reads the same field-pair positions, is unaffected.

## v0.75.0 (2026-07-18)

**Smart-cut corruption fix on progressive DVB, two MPEG-2 tail fixes, dead-code cleanup**

### Fixes

- **H.264 smart cut: SPS unification no longer corrupts progressive sources** —
  the unification slice rewriter skipped writing `pic_order_cnt_lsb` when the
  re-encode encoder used poc_type 2 (libx264's choice for progressive content),
  although the rewritten slices run under the source SPS (poc_type 0), which
  requires the field. Every rewritten slice header was bit-shifted from that
  point, mass-corrupting the output (measured on ONE-HD 720p50: 495 decoder
  errors, 13 of 1001 frames lost). The field is now inserted (anchored POC
  numbering) when the encoder slice has none. Reachable on progressive non-IDR
  DVB material whenever a cut lands on a POC seam outside the encoder bridge
  window (~1 in 3 probed cut positions). Interlaced unification cuts
  (MBAFF/PAFF, encoder poc_type 0) are byte-identical to before; the standard
  seam path is untouched (byte-identical). New diagnostic harness:
  `tools/diag/test_smartcut_seam`.

- **MPEG-2: no more phantom start codes past the end of file** —
  `TTFileBuffer::readByte()` never threw at EOF and returned stale ring-buffer
  bytes past `writePos`. The header parser could read a phantom
  `picture_start_code` a few bytes before the file end, truncating the last
  slice of a copied cut (visible as `ac-tex damaged` on the final macroblock).
  `readByte()` now throws `StreamEOF` (the contract the array variant already
  implemented) and the start-code scanner stops when fewer than 4 bytes remain.

- **MPEG-2: cutting the file tail no longer crashes** — a cut-in on a non-I
  frame with no I-frame following (recording truncated mid-GOP) drove
  `encodePart()` with an invalid range and aborted via an uncaught exception
  (SIGABRT). `getCutStartObject()` now re-encodes the whole remaining range in
  that case. All non-tail cuts verified byte-identical.

### Changes

- **Dead-code audit, first pass: ~2 200 lines removed** — orphaned files
  (including two unused dialogs and their `.ui` files), the unused
  `TTMessageBox`, dead marker CRUD, pre-refactor smart-cut helpers, the
  `TTStreamPointCutDerivation` subsystem, and dead methods across
  avstream/list/provider/GUI classes. Verified by clean rebuilds and a
  bit-identical package QC build. New reusable audit tooling lives in the
  global skill collection.

- **Tux attribution added** — `CREDITS.md` (Larry Ewing, The GIMP), a README
  credits pointer, and a `debian/copyright` paragraph for the bundled
  `ui/pixmaps/Tux.svg`. The application logo itself contains no Tux.

### Known issues

- **H.264/H.265 smart cut: the re-encode → stream-copy seam damages the
  copy-start keyframe's leading pictures** on IDR-free open-GOP material
  (all ARD/ONE HD): up to reorder-depth corrupt frames (H.264) or silently
  dropped RASL frames (H.265) per cut-in. Confirmed and measured this cycle
  (defect A in `docs/code-map/smart-cut.md`); the fix needs its own design
  round and is the next major work item. Pre-existing in all earlier releases.

## v0.74.0 (2026-07-12)

**MPEG-2 B-frame cut-out fix, ttcut-demux duration fix + ES-only mode, smart-cut seam consolidation**

### Changes

- **ttcut-demux: the normalized-MKV mode is gone** — elementary-stream demux
  (the TTCut workflow) is now the only mode and the default; `-e` is still
  accepted for compatibility. The MKV mode dated from the v0.52 initial
  import and required mkvmerge, which nothing else needs anymore.

- **H.264 smart cut: IDR copy-starts are no longer frame_num-patched** — when the
  stream-copy section after a re-encoded head begins with a true IDR, the old code
  patched `frame_num` into every copied AU including the IDR itself (`frame_num != 0`
  on an IDR violates H.264 7.4.3; decoders tolerated it). An IDR resets
  `PrevRefFrameNum`, so no bridging is needed there at all. Decoded output is
  byte-identical (verified via framemd5 on a purpose-built IDR-seam cut); all
  non-IDR material cuts bit-identically to before. Rare in practice: DVB broadcasts
  carry few true IDRs (ARD none at all), so most seams start on non-IDR I-slices
  and are unaffected. Also fixed: a non-IDR keyframe whose `frame_num` wrapped to 0
  is now bridged in the SPS-unification path (was skipped).

### Fixes

- **ttcut-demux: video duration is measured from the video PTS span**, not the
  container span — fixes an inflated frame count, over-padded audio, and a
  circular drift report on recordings whose audio leads the video (the span
  start uses the stream's first decodable frame, excluding open-GOP leading
  B-frames). The "defective regions" warning for doubled-PTS pictures is
  reworded (field pairs are legitimate). TTCut now prefers the MPEG-2 parser's
  field-pair list over the .info heuristic for audio time correction and labels
  confirmed field-pair clusters "Feldpaare:" (no warning dialog when every
  cluster is a confirmed field pair).
- **MPEG-2: cut-out on a B-frame no longer loses up to M-1 frames** at the end
  of the segment. A block in `getCutEndObject()` (introduced unnoticed in an
  i18n commit) suppressed the tail re-encode based on a display-index vs.
  bitstream-order mixup; the "duplicate frames" case it claimed to prevent was
  disproved empirically (structural argument + A/B runs over 8 cut-out
  positions, see `tools/diag/test_mpeg2_cutout`).

### Internal

- Smart cut: the SPS-unification branch decision now probes the encoder's
  real POC parameters (throwaway libx264 open, SPS from extradata) instead
  of assuming `log2_max_poc_lsb=4`; the constant remains only as a fallback
  and the real per-segment SPS is cross-checked against the probe. Measured
  along the way: libx264 uses poc_type 2 for progressive bf=0 encodes (the
  everyday case — seam continuity there is EOS + frame_num, unchanged).
  Verified byte-identical on MBAFF/IDR-seam/progressive/HEVC reference cuts.
- Audio-cut chain consolidated onto `TTAVData::buildVideoKeepList` +
  `cutAudioTracks` (five producers + the drift-only site; bit-identical audio
  payloads verified on MPEG-2/MP2 and H.264/AC3 cuts).
- Smart-cut seam helpers: the two encoder→copy `frame_num` bridges unified into
  `bridgeFrameNum`, the four EOS-emit sites into `writeEos`; removed the
  unreachable `processSegment` "PAFF fallback" branch (+ dead IDR-injection
  helpers) and the write-only `ReencodeContext::realStartAU` field.
- MPEG-2: `picture_coding_type` magic numbers replaced by `enum Mpeg2PicCoding`.
- New diagnostic `tools/diag/probe_copystart` (classify copy-start keyframes,
  list IDRs).

## v0.73.0 (2026-07-10)

**Burst detection: crash fix, a threshold slider that finally works, and honest limits**

### Fixes

- **Crash when updating a cut on AC3 with per-frame channel-mode changes** —
  `detectAudioBurst()` looped over the decoder context's channel count, but an AC3 stream
  may change `acmod` from frame to frame (e.g. 5.1 → 2.0). For a 2-channel frame in a
  6-channel context, `frame->data[2]` is legitimately `NULL` for planar formats, and the
  interleaved stride over-read past the frame. The per-frame channel count
  (`frame->ch_layout.nb_channels`) is now used. Moving a cut's in/out point and pressing
  Update no longer segfaults on such streams.

- **AC3 format-change hint vanished from the cut list after opening the settings dialog** —
  column 5 is written by two producers: `updateBurstIcon()` sets it (and clears it when
  there is no burst), `updateAcmodIcon()` appends to it. Their call order was an implicit
  contract, and `refreshBurstIcons()` — invoked unconditionally when the settings dialog
  closes, even on Cancel — called only the first one. The "AC3 start/end" hint therefore
  disappeared until the cut was edited again. Both now go through a single entry point,
  `updateHintColumn()`; `refreshBurstIcons()` is renamed `refreshHintIcons()` because it
  refreshes both hint kinds. Also fixes a stale text left behind in the hint column when a
  cut has no audio track.

- **Burst detection: threshold slider had no effect below 20 dB** — `detectAudioBurst()`
  enforced a hardcoded 20 dB relative threshold while `applyBurstDeltaFilter()` re-tested
  the same quantity against `burstMinDeltaDb`. Since a filter can only reject, never
  admit, any setting from 0 to 20 behaved identically. The threshold is now passed into
  the detector and the post-filter is gone; values 1–19 finally work. Verified on DVB
  reference material: a boundary with a 10.94 dB delta is reported at `--min-delta 10`
  and suppressed at 20, where before the rewrite it was structurally always missed.

- **Burst detection now reports the burst peak** — the detector returned the *first*
  chunk above the threshold; it now returns the **peak** of the tested chunks, so the
  level shown in the warning tooltip is the audible peak rather than a point on the
  onset ramp (which climbs 38–51 dB within a single 32 ms audio frame). This changes
  the *displayed* level only, not which cuts are flagged: both detection conditions are
  monotone in level, so peak and first-hit agree on detection. The absolute −40 dB
  audibility gate keeps its value and is now documented in the code.

### Behaviour change

- **`burstMinDeltaDb = 0` now disables burst detection.** Previously it skipped the
  post-filter, which left the detector's own 20 dB threshold in force — 0 therefore behaved
  exactly like 20. Anyone who had 0 configured will see no burst warnings until they set a
  value ≥ 1. The setting is now also short-circuited before the audio file is opened.

### Documentation

- **Burst detection is documented as approximate** (`TODO.md` → Known Limitations). It
  reliably flags a loud advertising burst reaching the cut boundary, but its resolution is
  limited by design and a missing warning does not mean the cut is clean. Every stated limit
  is measured: time resolution is one audio frame (32 ms, so short transients average away),
  only the outermost two chunks (~64 ms) of the 200 ms window are tested, an untested loud
  chunk raises the context median and thereby makes detection *less* sensitive, and the
  −40 dB audibility gate is cleared by under 4 dB by two of three real bursts.

### Internal

- New developer tools, not part of the application build: `tools/ttcut-burst-probe` calls
  `detectAudioBurst()` directly for a single cut boundary, and `tools/burst-analysis` scans
  a whole stream for candidate boundaries. Together they turned statements about the
  detector's thresholds into measurements — and disproved two of them.

## v0.72.2 (2026-07-06)

**Fixes: default audio track flag and P-frame navigation**

### Fixes

- **AC3 audio track not marked as default in the output MKV** — `addAudioInputs()`
  copied only the codec parameters (`avcodec_parameters_copy()` does not carry the
  stream disposition), so all audio output streams were muxed with an empty
  disposition. The libav matroska muxer then marked *no* audio track as default,
  leaving e.g. an AC3 track (or any track after the first) without the default
  flag. Every audio track now gets `AV_DISPOSITION_DEFAULT`, so the player selects
  a track according to its own language/codec preferences. Applies to both the ES
  cut and the audio-only mux via the shared helper.

- **P-button navigation stuck at the last I-frame** — jumping to the last I-frame
  and then pressing the P (next P/I frame) button did nothing. `moveToNextPIFrame()`
  chose the nearer of the next I- and P-frame with a plain minimum, but the index
  search returns `-1` when no such frame follows, and `-1` won every comparison —
  so with no further I-frame the result was `-1` and the position stayed put even
  though P-frames still followed. The nearer frame is now selected only among valid
  positions; `moveToPrevPIFrame()` was made symmetric for clarity.

## v0.72.1 (2026-07-05)

**Fix: H.264 open-GOP streams no longer hang on load**

### Fixes

- **H.264 open-GOP cold-start leading pictures** — a demuxed H.264 elementary
  stream whose first coded picture is a non-IDR open-GOP I-frame with leading
  pictures (`I B B B P …`) hung TTCut-ng at load ("Frames werden verarbeitet…
  99 %"). The leading B-frames display *before* the first I and reference a GOP
  before it that does not exist at stream start, so a conforming decoder drops
  them — but the display-order map still ranked them, so `decodeFrame(display 0)`
  waited for a frame the decoder never emits and drained the entire file to EOF,
  retrying forever. The map now marks these cold-start leading pictures as
  dropped (POC-based, mirroring the existing HEVC RASL handling), so loading,
  navigation, search, and the frame-accurate cut all agree with the decoder.
  Affects e.g. ZDF-neo and Das-Erste-HD 720p50 recordings. Regression introduced
  in v0.72.0.

## v0.72.0 (2026-07-05)

**Frame-accurate H.264/H.265 smart cutting, display-order correctness, and correct playback/output timestamps**

### Features

- **Context-relative audio-burst detection** — the burst filter now compares
  each candidate against its local surroundings (RMS median of the neighbourhood)
  instead of a fixed absolute dBFS threshold. This catches the quieter
  real-world advertising bursts that the old absolute filter silently missed,
  without flagging normal loud passages. A new setting *"Minimum burst jump
  above surroundings (dB, 0 = off)"* (default 20) in the audio settings tab
  controls the sensitivity, and the cut list's burst icons now refresh
  immediately when settings are saved — no preview run required.
- **Single frame-index scan** — the reference video stream now builds the frame
  index once and shares it with the still-image window and frame search
  (`TTH26xVideoStream::provideFrameIndexTo`), instead of each consumer
  re-scanning the stream. Fewer redundant passes, one authoritative index.
- **`--auto-cut` runs unattended on burst-containing projects** — the burst
  warning at the final cut is logged instead of blocking on a modal dialog when
  running headless, so batch cuts no longer hang waiting for a click.

### Fixes

- **HEVC frame numbers now match the decoder/player** — the display-order map
  ranked the first CRA's RASL leading pictures, which every conforming decoder
  (ffmpeg, mpv) drops because they reference frames before the random-access
  point. That inflated the HEVC frame count and shifted every frame number by a
  constant (e.g. +7 on a reference DVB stream) relative to playback. These
  undecodable leading pictures are now dropped from the display dimension, so
  the still image, frame search, the cut, and the navigable frame count all
  agree with ffmpeg/mpv display order. Detection is dynamic (NAL type +
  HEVC `NoRaslOutputFlag` rule: first IRAP / post-EOS / BLA); the count is never
  assumed. H.264 and MPEG-2 are unaffected (no RASL NAL types). Verified end to
  end: `decodeFrame(N)`/search/cut all land on ffmpeg display frame N
  (Pearson r ≈ 1.0), and a full HEVC cut starts exactly at the selected
  display frame (no leading advertising/logo frames).
- **Frame-accurate cut-out** — H.264/H.265 smart cut used to include frames
  that *display* after the cut-out point (B-frame reorder leaked them into the
  contiguous decode-order stream-copy), causing extra trailing frames at
  segment ends (e.g. a stray advertising frame at a film→ad boundary) and
  accumulating A/V drift. The cut-out is now frame-accurate via a tail-GOP
  re-encode (forced-IDR), symmetric to the frame-accurate cut-in. Each segment
  outputs exactly `cutOut − cutIn + 1` display frames, so A/V sync is automatic.
  Verified: MBAFF full cut frame-exact with ~1-frame end A/V; PAFF A/V drift
  reduced (376 → 232 ms vs the prior baseline).
- **Frame-accurate cut-in** — cut positions are now display positions end to
  end, converted to access-unit (decode) indices through an authoritative
  display↔decode map (`TTDisplayOrderMap`, built from the libav parser's
  `output_picture_number`, no decode pass). This removes the prior mixed-index
  cut-in offset where a cut started a few display frames late inside the
  programme.
- **Correct frame timestamps in the output MKV (display-PTS)** — smart-cut
  elementary-stream video was muxed with decode-order timestamps, so the
  resulting MKV carried non-monotonic display timing. The muxer now assigns
  display-order PTS for H.264/H.265 (preview and final cut) and for MPEG-2
  (derived from `temporal_reference`), while DTS stays in decode order. Players
  and downstream tools now see the correct frame timing.
- **Smoother H.264/H.265 playback (no ±140 ms jitter)** — the temporary
  playback MKV was muxed with decode-order PTS, so the current-frame player
  re-timed video against the correct audio clock and frames appeared up to
  ±140 ms early/late in a B-pyramid pattern. The temp MKV now carries
  display-order PTS, and the play-start / timecode / stop-position conversions
  are coupled to it. A/V sync verified objectively (lip-closure ↔ audio dip
  alignment: 0 ms).
- **POC-domain seam at re-encode → stream-copy transitions** — per-segment SPS
  unification plus POC anchoring on the minimum display-POC of the first
  stream-copy GOP removes the picture-order-count mismatch that could interleave
  frames at a segment seam.
- **First-segment stream-copy no longer leaks leading pictures** — the
  first-segment override could include display-order pictures from before the
  cut-in; it is now skipped when the segment is folded into a re-encode.
- **H.264/H.265 preview audio drift** — preview audio is now routed through the
  same `planAudioCut` path as the final cut, so preview A/V matches the result.
- **New Project no longer silently overwrites the previous project file** — a
  fresh project could reuse the previous file path and overwrite it without
  warning.

### Changes

- The absolute burst threshold setting (`burstThresholdDb`) is replaced by the
  context-relative `burstMinDeltaDb` (see Features). The obsolete key is cleaned
  up automatically on load.
- Burst UI strings converted to English source with a German translation, in
  line with the project-wide i18n convention.
- New maintained data-flow maps under `docs/code-map/` (frame-order,
  burst-detection); `docs/` is excluded from the Debian package build.

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
