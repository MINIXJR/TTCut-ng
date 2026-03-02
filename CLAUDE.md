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

## Recent Fixes and Features

### Compatibility Fixes
- Fixed header guard typo in extern/tttranscode.h (TTTRASNCODE_H → TTTRANSCODE_H)
- Fixed deprecated Qt5 QFlags constructor usage in gui/ttprogressbar.h (using `{}` instead of `0`)
- Migrated from deprecated QGLWidget to QImage/QPixmap-based rendering for full Wayland compatibility
- Replaced discontinued transcode with libavcodec API for MPEG-2 frame-accurate cutting
- Migrated from mplayer to mpv for preview playback
- Interlace detection for MPEG-2 re-encoding
- Fixed MPEG-2 cutting for short segments without I-frames

### UI Improvements
- Theme icons and video editing color scheme
- Vim-style keyboard shortcuts (j/k, g/G, [/] for cut-in/out) with Help dialog
- Play button in Current Frame widget (H.264/H.265 via mpv with temporary MKV)
- Previous/Next cut navigation buttons in preview dialog
- Corrected preview button order (Back/Start/Forward)
- Frame position is remembered when switching between videos
- User warnings for destructive actions (New Project)
- German translations (de_DE)

### SRT Subtitle Support

Full SRT subtitle support:
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

Demux-Tool für H.264/H.265 TS-Dateien:
- Demuxes TS to elementary streams (similar to ProjectX for MPEG-2)
- Multi-core optimized (parallel audio/video demuxing)
- Generates .info file with frame rate, resolution, audio tracks
- Audio trim at start for A/V offset correction
- Audio padding at end (ProjectX-style) — reduces drift from 372ms to 8ms
- Duration mismatch detection and reporting in .info file
- VDR marks support (loads .marks file)
- Automatic filler NALU stripping for H.264/H.265 (via ffmpeg `filter_units` bitstream filter)

**Known limitations:**

1. **Video playback delay**: When playing H.264/H.265 video from the "Current Frame" widget, TTCut-ng must first create a temporary MKV file (muxing video + audio with libav matroska muxer). This causes a brief delay before playback starts. This is necessary because H.264/H.265 elementary streams lack timestamps required for seeking and A/V synchronization. MPEG-2 playback does not have this limitation.

The project is Linux-only, builds cleanly with Qt 5.15 and has full Wayland support.

### Navigation and Smart Cut Fixes (v0.61.1–v0.61.4)

- **v0.61.1**: Fix frame position sync between slider and navigation buttons
- **v0.61.2**: Fix shared videoStream position corruption — explicit position parameter passed through all navigation methods and `checkCutPosition()`
- **v0.61.3**: Separate navigation from auto-save in CurrentFrame widget — B/I/P buttons only navigate, Set Cut-In/Out buttons explicitly save
- **v0.61.4**: Fix Smart Cut segment boundary stutter for B-frame reorder crossing — Case A (extend re-encode to next keyframe) vs Case B (skip extension to avoid POC domain mismatch). Replaces `needsIDR` with `adjustedStreamCopyStart` output parameter. EOS NAL always written before stream-copy.

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
