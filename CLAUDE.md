# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TTCut-ng is a Qt5-based video editing application for MPEG-2, H.264, and H.265 streams (Linux only). It allows frame-accurate cutting without re-encoding the entire stream - only the frames around cut points are re-encoded. The primary use case is removing advertisements from DVB recordings.

**Supported Codecs:**
- MPEG-2 (fully supported, original TTCut functionality)
- H.264/AVC (Smart Cut via TTESSmartCut)
- H.265/HEVC (Smart Cut via TTESSmartCut)

**Input Format:**
- Elementary streams only (.m2v, .264, .h264, .265, .h265)
- Separate audio files (.ac3, .mp2, .mp3, .aac)
- Optional .info metadata file (for frame rate, etc.)

**Output Format:**
- MKV (via libav matroska muxer) with optional chapters

**Preprocessing Workflow:**
- MPEG-2: Use ProjectX to demux TS → ES files
- H.264/H.265: Use `tools/ttcut-demux -e` to demux TS → ES files

**Key Constraint for MPEG-2**: Cuts without re-encoding are only possible at:
- Cut-in: I-frames only
- Cut-out: P-frames or I-frames

**Key Constraint for H.264/H.265**: Smart cut re-encodes partial GOPs at cut points. For streams without IDR frames (only Non-IDR I-slices), Smart Cut forces IDR generation via `forced-idr=1` to ensure clean decoder resets at segment boundaries.

## Build System

This project uses Qt's qmake build system:

```bash
# Generate Makefile from .pro file
qmake ttcut-ng.pro

# Build the project
make

# Clean build artifacts
make clean

# Full rebuild
make clean && qmake ttcut-ng.pro && make
```

The generated executable is `ttcut-ng` in the project root.

## Architecture

### Stream Hierarchy

The codebase uses a class hierarchy for AV streams (defined in avstream/):

```
TTAVStream
├── TTAudioStream
│   ├── TTMpegAudioStream
│   └── TTAC3AudioStream
├── TTVideoStream
│   ├── TTMpeg2VideoStream
│   ├── TTH264VideoStream
│   └── TTH265VideoStream
└── TTSubtitleStream
    └── TTSrtSubtitleStream
```

### Module Organization

- **avstream/**: Core stream handling - MPEG-2/H.264/H.265 video parsing, audio parsing, header lists, file buffers, index lists
- **gui/**: Qt widgets and main window (TTCutMainWindow is the main entry point)
- **common/**: Global settings (TTCut singleton), message logging, exception handling
- **data/**: Data structures for audio lists, cut lists, muxer lists, cut parameters
- **mpeg2decoder/**: MPEG-2 decoding using libmpeg2
- **mpeg2window/**: QImage/QPixmap-based frame display (also used for H.264/H.265 via libav)
- **extern/**: External tool integration (libav for H.264/H.265 smart cut, audio cutting, MKV muxing; mplex for MPEG-2 multiplexing)
- **ui/**: Qt Designer .ui files and resource files (.qrc)
- **tools/**: Standalone tools (ttcut-demux, ttcut-ac3fix) and test programs

### Key Classes

- **TTCutMainWindow** (gui/ttcutmainwindow.h): Main application window, coordinates all operations
- **TTMpeg2VideoStream** (avstream/ttmpeg2videostream.h): MPEG-2 video stream class, handles cutting operations
- **TTH264VideoStream** (avstream/tth264videostream.h): H.264/AVC video stream class
- **TTH265VideoStream** (avstream/tth265videostream.h): H.265/HEVC video stream class
- **TTFFmpegWrapper** (extern/ttffmpegwrapper.h): Libav/ffmpeg wrapper for H.264/H.265 smart cut, frame decoding, and container handling
- **TTCut** (common/ttcut.h): Singleton holding global settings and application state
- **TTTranscodeProvider** (extern/tttranscode.h): MPEG-2 re-encoding at cut points via libavcodec API
- **TTMplexProvider** (extern/ttmplexprovider.h): Multiplexes video/audio after cutting
- **TTMkvMergeProvider** (extern/ttmkvmergeprovider.h): MKV output via libav matroska muxer, chapter support

### Important Workflows

1. **Stream Opening**: Video/audio files are opened as elementary streams (demuxed). The app builds header lists and index lists for navigation.

2. **Cut Point Selection**: User navigates frames and marks cut-in/cut-out points. These are stored in TTCutListData.

3. **Cutting Process**:
   - For cuts at valid frame types (I/P frames), segments are copied directly
   - For cuts at other frames, TTTranscodeProvider re-encodes the surrounding frames
   - Audio streams are cut simultaneously to match video cuts

4. **Multiplexing**: After cutting, TTMplexProvider can re-multiplex the video and audio streams.

## Qt/C++ Conventions

- The project uses Qt 5 (QtCore, QtWidgets, QtGui, QtNetwork, QtXml)
- Moc files go to moc/, UI headers to ui_h/, objects to obj/, resources to res/
- Class names use TT prefix (e.g., TTCutMainWindow, TTMpeg2VideoStream)
- Qt signals/slots mechanism is used extensively for GUI communication

## Dependencies

- Qt5 (Core, Widgets, Gui, Network, Xml)
- libmpeg2 and libmpeg2convert (MPEG-2 decoding)
- libavformat, libavcodec, libavutil, libswscale (H.264/H.265 smart cut, audio cutting, MKV muxing)
- ffmpeg CLI (optional, for MP4 output container muxing)
- mplex (optional, for MPEG-2 multiplexing)
- Note: mkvmerge/mkvtoolnix is no longer required (replaced by libav matroska muxer in v0.60.0)

## Version

Defined centrally in `ttcut-ng.pro` (`VERSION = ...`)

## Feature Overview

For version-specific changes, see `CHANGELOG.md`.

### Per-Track Audio Delay & Audio-Drift

- **Audio delay**: Editable per-track delay (±9999 ms, QSpinBox in audio list) applied during audio cutting (keepList PTS offset) for all codecs
- **Audio-Drift column**: Cut list shows accumulated audio frame boundary drift per cut, calculated during preview (first audio track only)
- **TTESInfo**: Parses per-track `audio_N_trimmed_ms` and `audio_N_first_pts` from `.info` files
- **Persistence**: Delay stored in `.ttcut` project file XML (`<Delay>` element, optional)

Key classes:
- **TTAudioItem** (data/ttaudiolist.h): `mAudioDelayMs` field, `getDelayMs()`/`setDelayMs()`
- **TTAudioTreeView** (gui/ttaudiotreeview.h): QSpinBox widget, `delayChanged` signal
- **TTCutTreeView** (gui/ttcuttreeview.h): `onAudioDriftUpdated()` slot for drift display
- **TTCutPreviewTask** (data/ttcutpreviewtask.h): `audioDriftCalculated` signal

### Audio Language Preference

- **Preference list**: `TTCut::audioLanguagePreference` (QStringList) — user-configurable sort priority, persisted in QSettings
- **Normalization**: `TTCut::normalizeLangCode()` accepts 2-letter, canonical 3-letter, and alternative ISO 639-2 forms (ger→deu, fre→fra, nld→dut, ces→cze, zho→chi, ell→gre, slk→slo, ron→rum, mkd→mac, fas→per); unknown codes return empty string
- **Sort logic**: `TTAudioItem::operator<` — AC3 > preference index (0..N-1, unmatched = INT_MAX) > discovery order. Empty list → fall back to system locale
- **UI**: QLineEdit in settings Allgemein tab, input normalized on save (unknown codes silently dropped)

### SRT Subtitle Support

- **Auto-loading**: SRT files matching the video filename are automatically loaded
- **Preview overlay**: Subtitles displayed in the main video frame using QPainter
- **mpv preview**: Subtitles passed to mpv via `--sub-file` parameter
- **Cutting**: Subtitle streams are cut along with video/audio during editing

Key classes:
- **TTSrtSubtitleStream** (avstream/ttsrtsubtitlestream.h): SRT file parser and stream handler
- **TTSubtitleHeaderList** (avstream/ttsubtitleheaderlist.h): Manages subtitle entries with time-based search
- **TTCutSubtitleTask** (data/ttcutsubtitletask.h): Task for cutting subtitle streams
- **TTSubtitleTreeView** (gui/ttsubtitletreeview.h): UI widget for subtitle file list

### H.264/H.265 Smart Cut Support

Frame-accurate cutting for H.264/H.265 elementary streams using TTESSmartCut:

**Workflow:**
```
DVB Recording (TS) → ttcut-demux -e → ES files + .info → TTCut-ng → MKV
```

**How it works:**
1. Pre-demux TS to elementary streams using `tools/ttcut-demux -e`
2. Native NAL unit parser analyzes GOP structure (TTNaluParser)
3. GOPs fully inside kept segments are stream-copied (no quality loss, ~99.5%)
4. GOPs at cut boundaries are decoded and re-encoded (~0.5% of frames)
5. Audio is cut via libav stream-copy
6. Final MKV is created with libav matroska muxer

**Key implementation details (extern/ttessmartcut.cpp):**
- TTNaluParser: Memory-mapped native H.264/H.265 NAL unit parser
- TTESSmartCut: Frame-accurate Smart Cut engine
- Encoder uses `bf=0` (no B-frames) and `forced-idr=1` for clean segment transitions
- Encoder recreated between segments (libx264 lookahead limitation)
- GOP detection recognizes both IDR frames and I-slices (Open GOPs)
- Non-IDR I-frame streams: `analyzeCutPoints()` forces re-encode to produce IDR at segment boundaries
- B-frame reorder boundary crossing: Case A/B differentiation via `adjustedStreamCopyStart` output parameter
- EOS NAL always written before stream-copy to flush decoder DPB
- Decoder: `thread_count=1` (single-threaded to prevent PTS misassignment)
- SPS inline patching with `max_num_reorder_frames` for correct decoder buffering

**Key classes:**
- **TTNaluParser** (avstream/ttnaluparser.h): Native NAL unit parser with mmap I/O
- **TTESSmartCut** (extern/ttessmartcut.h): Smart Cut engine for ES files
- **TTESInfo** (avstream/ttesinfo.h): .info file parser for metadata
- **TTMkvMergeProvider** (extern/ttmkvmergeprovider.h): MKV muxing via libav with chapters

### ttcut-demux (tools/ttcut-demux)

Demux tool for H.264/H.265 TS files:
- Demuxes TS to elementary streams (similar to ProjectX for MPEG-2)
- Multi-core optimized (parallel audio/video demuxing)
- Generates .info file with frame rate, resolution, audio tracks
- Audio trim at start for A/V offset correction
- Audio padding at end (ProjectX-style, concat stream-copy) — preserves per-frame AC3 acmod changes
- Duration mismatch detection and reporting in .info file
- VDR marks support (loads .marks file)
- Automatic filler NALU stripping for H.264/H.265 (via ffmpeg `filter_units` bitstream filter)

**VDR Multi-File Support:**
- VDR splits recordings into 2GB segments: 00001.ts, 00002.ts, ...
- ttcut-demux auto-detects siblings when given the first segment
- Uses ffmpeg concat protocol (no temporary concat file needed)
- `-n NAME` parameter sets output basename (replaces file renaming workflow)
- Duration calculation sums individual segment durations (ffprobe concat protocol bug workaround)
- See `tools/vdr-demux-example.sh` for a complete VDR demux workflow example

**Known limitations:**

1. **Video playback delay**: When playing H.264/H.265 video from the "Current Frame" widget, TTCut-ng must first create a temporary MKV file (muxing video + audio with libav matroska muxer). This causes a brief delay before playback starts. This is necessary because H.264/H.265 elementary streams lack timestamps required for seeking and A/V synchronization. MPEG-2 playback does not have this limitation.

**PAFF Smart Cut implementation notes:**
- x264 produces MBAFF output while the source stream is PAFF. An EOS NAL flushes the decoder DPB at the re-encode→stream-copy transition.
- MMCO commands in the first 32 stream-copy AUs are neutralized (`adaptive_ref_pic_marking_mode_flag` set to 0) to prevent DPB management errors on the empty DPB.
- SPS Unification rewrites encoder output to use source SPS parameters (log2_max_frame_num, log2_max_pic_order_cnt_lsb, frame_mbs_only_flag).
- Non-PAFF (MBAFF) streams use `realStartAU` filtering to exclude Open-GOP B-frames from before the cut-in.
- SPS patching (`patchH264SpsReorderFrames`) is stream-type-aware via `isPAFF` parameter: PAFF streams get increased `num_ref_frames`/`max_dec_frame_buffering` for MBAFF→PAFF DPB transitions; non-PAFF streams keep original values.
- After EOS in the standard (non-PAFF) path, `frameNumDelta` is recalculated from the encoder's last frame_num to the first stream-copy frame_num, ensuring seamless frame_num continuity across the DPB flush.

**Frame display (TTFFmpegWrapper):**
- `seekToFrame()` seeks to the keyframe BEFORE the target keyframe (DPB prefill) to ensure Open-GOP B-frames decode correctly after `avcodec_flush_buffers()`.
- The sequential decode optimization is disabled — `decodeFrame()` always seeks. This ensures identical DPB state across all decoder instances (CutOut and CurrentFrame widgets). The LRU frame cache mitigates the performance impact.

The project is Linux-only, builds cleanly with Qt 5.15 and has full Wayland support.

## Running on Wayland

TTCut-ng requires XCB platform on Wayland systems.

**Running TTCut-ng:**

```bash
# Run with XCB backend (required for Wayland)
QT_QPA_PLATFORM=xcb ./ttcut-ng

# With logging
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /path/to/logfile.log
```

The executable name is `ttcut-ng`.

## Future Improvements

See `TODO.md` for the full feature roadmap and completed items.
