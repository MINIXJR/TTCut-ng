# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TTCut is a Qt5-based video editing application for MPEG-2, H.264, and H.265 streams. It allows frame-accurate cutting without re-encoding the entire stream - only the frames around cut points are re-encoded. The primary use case is removing advertisements from DVB recordings.

**Supported Codecs:**
- MPEG-2 (fully supported, original TTCut functionality)
- H.264/AVC (Smart Cut via TTESSmartCut)
- H.265/HEVC (Smart Cut via TTESSmartCut)

**Input Format:**
- Elementary streams only (.m2v, .264, .h264, .265, .h265)
- Separate audio files (.ac3, .mp2, .mp3, .aac)
- Optional .info metadata file (for frame rate, etc.)

**Output Format:**
- MKV (via mkvmerge) with optional chapters

**Preprocessing Workflow:**
- MPEG-2: Use ProjectX to demux TS → ES files
- H.264/H.265: Use `tools/ts_demux_normalize.sh -e` to demux TS → ES files

**Key Constraint for MPEG-2**: Cuts without re-encoding are only possible at:
- Cut-in: I-frames only
- Cut-out: P-frames or I-frames

**Key Constraint for H.264/H.265**: Smart cut re-encodes partial GOPs at cut points. There is a **known limitation**: a small stutter (~0.14 seconds) may occur at middle cut points due to B-frame reordering discontinuities between encoded and stream-copied sections.

## Build System

This project uses Qt's qmake build system:

```bash
# Generate Makefile from .pro file
qmake ttcut.pro

# Build the project
make

# Clean build artifacts
make clean

# Full rebuild
make clean && qmake ttcut.pro && make
```

The generated executable is `TTCut` in the project root.

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

- **avstream/**: Core stream handling - MPEG-2/H.264/H.265 video parsing, audio parsing, header lists, file buffers, index lists, AVI writing
- **gui/**: Qt widgets and main window (TTCutMainWindow is the main entry point)
- **common/**: Global settings (TTCut singleton), message logging, exception handling
- **data/**: Data structures for audio lists, cut lists, muxer lists, cut parameters
- **mpeg2decoder/**: MPEG-2 decoding using libmpeg2
- **mpeg2window/**: OpenGL-based MPEG-2 frame display (also used for H.264/H.265 via libav)
- **extern/**: External tool integration (ffmpeg/libav for H.264/H.265 smart cut, mplex for multiplexing, mkvmerge for MKV output)
- **ui/**: Qt Designer .ui files and resource files (.qrc)
- **avilib/**: AVI file writing library (C code)

### Key Classes

- **TTCutMainWindow** (gui/ttcutmainwindow.h): Main application window, coordinates all operations
- **TTMpeg2VideoStream** (avstream/ttmpeg2videostream.h): MPEG-2 video stream class, handles cutting operations
- **TTH264VideoStream** (avstream/tth264videostream.h): H.264/AVC video stream class
- **TTH265VideoStream** (avstream/tth265videostream.h): H.265/HEVC video stream class
- **TTFFmpegWrapper** (extern/ttffmpegwrapper.h): Libav/ffmpeg wrapper for H.264/H.265 smart cut, frame decoding, and container handling
- **TTCut** (common/ttcut.h): Singleton holding global settings and application state
- **TTTranscodeProvider** (extern/tttranscode.h): Wraps ffmpeg CLI for re-encoding frames around cut points (MPEG-2)
- **TTMplexProvider** (extern/ttmplexprovider.h): Multiplexes video/audio after cutting
- **TTMkvMergeProvider** (extern/ttmkvmergeprovider.h): MKV output via mkvmerge, chapter support

### Important Workflows

1. **Stream Opening**: Video/audio files are opened as elementary streams (demuxed). The app builds header lists and index lists for navigation.

2. **Cut Point Selection**: User navigates frames and marks cut-in/cut-out points. These are stored in TTCutListData.

3. **Cutting Process**:
   - For cuts at valid frame types (I/P frames), segments are copied directly
   - For cuts at other frames, TTTranscodeProvider re-encodes the surrounding frames
   - Audio streams are cut simultaneously to match video cuts

4. **Multiplexing**: After cutting, TTMplexProvider can re-multiplex the video and audio streams.

## Qt/C++ Conventions

- The project uses Qt 5 (QtCore, QtWidgets, QtGui, QtOpenGL)
- Moc files go to moc/, UI headers to ui_h/, objects to obj/, resources to res/
- Class names use TT prefix (e.g., TTCutMainWindow, TTMpeg2VideoStream)
- Qt signals/slots mechanism is used extensively for GUI communication

## Dependencies

- Qt5 (Core, Widgets, Gui, OpenGL)
- libmpeg2 and libmpeg2convert (MPEG-2 decoding)
- libavformat, libavcodec, libavutil, libswscale (H.264/H.265 handling via ffmpeg libraries)
- OpenGL and GLU (frame display)
- ffmpeg CLI (required for frame-accurate cutting at any position)
- mplex (optional, for multiplexing)
- mkvmerge (optional, for MKV output with chapters)

## Current Branch

Working on branch: `feature-h264-h265-support`

## Recent Fixes and Features

The following compatibility issues have been fixed:
- Fixed header guard typo in extern/tttranscode.h (TTTRASNCODE_H → TTTRANSCODE_H)
- Fixed deprecated Qt5 QFlags constructor usage in gui/ttprogressbar.h (using `{}` instead of `0`)
- Migrated from deprecated QGLWidget to modern QOpenGLWidget for full Wayland compatibility
- Replaced discontinued transcode with ffmpeg for frame-accurate cutting
- Migrated from mplayer to mpv for preview playback

### SRT Subtitle Support

Full SRT subtitle support has been added:
- **Auto-loading**: SRT files matching the video filename are automatically loaded
- **Preview overlay**: Subtitles are displayed in the main video frame using QPainter
- **mpv preview**: Subtitles are passed to mpv via `--sub-file` parameter
- **Cutting**: Subtitle streams are cut along with video/audio during editing
- **Preview clips**: Subtitle previews are generated for cut preview dialog

Key classes for subtitle handling:
- **TTSrtSubtitleStream** (avstream/ttsrtsubtitlestream.h): SRT file parser and stream handler
- **TTSubtitleHeaderList** (avstream/ttsubtitleheaderlist.h): Manages subtitle entries with time-based search
- **TTCutSubtitleTask** (data/ttcutsubtitletask.h): Task for cutting subtitle streams
- **TTSubtitleTreeView** (gui/ttsubtitletreeview.h): UI widget for subtitle file list

### H.264/H.265 Smart Cut Support

Frame-accurate cutting for H.264/H.265 elementary streams using TTESSmartCut:

**Workflow:**
```
DVB Recording (TS) → ts_demux_normalize.sh -e → ES files + .info → TTCut → MKV
```

**How it works:**
1. Pre-demux TS to elementary streams using `tools/ts_demux_normalize.sh -e`
2. Native NAL unit parser analyzes GOP structure (TTNaluParser)
3. GOPs fully inside kept segments are stream-copied (no quality loss, ~99.5%)
4. GOPs at cut boundaries are decoded and re-encoded (~0.5% of frames)
5. Audio is cut via libav stream-copy
6. Final MKV is created with mkvmerge

**Key implementation details (extern/ttessmartcut.cpp):**
- TTNaluParser: Memory-mapped native H.264/H.265 NAL unit parser
- TTESSmartCut: Frame-accurate Smart Cut engine
- Encoder uses `bf=0` (no B-frames) for clean segment transitions
- Encoder recreated between segments (libx264 lookahead limitation)
- GOP detection recognizes both IDR frames and I-slices (Open GOPs)

**Key classes:**
- **TTNaluParser** (avstream/ttnaluparser.h): Native NAL unit parser with mmap I/O
- **TTESSmartCut** (extern/ttessmartcut.h): Smart Cut engine for ES files
- **TTESInfo** (avstream/ttesinfo.h): .info file parser for metadata
- **TTMkvMergeProvider** (extern/ttmkvmergeprovider.h): MKV muxing with chapters

**Demux script (tools/ts_demux_normalize.sh -e):**
- Demuxes TS to elementary streams (similar to ProjectX for MPEG-2)
- Generates .info file with frame rate, resolution, audio tracks
- Optional: Strip filler NALUs with `-p` flag (requires h264bitstream tools)

**Known limitation:**
A small stutter (~0.14 seconds) may occur at middle cut points due to B-frame reordering discontinuities. This is inherent to the Smart Cut approach when transitioning from re-encoded to stream-copied sections.

### H.264/H.265 Stream Preparation Tools (Companion Project)

A set of analysis and preparation tools is being developed in the h264bitstream fork:
**Repository:** https://github.com/MINIXJR/h264bitstream (branch: `feature-fix-mmco-rplr-writing`)

**Available tools:**
- `h264_dpb_analyze` - H.264 bitstream analyzer (MMCO/RPLR errors, filler NALUs)
- `h265_analyze` - H.265/HEVC bitstream analyzer (filler NALUs, stream structure)
- `tools/h264_prepare.sh` - Workflow script for stream preparation

**Key findings:**
- DVB H.264 recordings typically contain 8% filler NALUs (can be safely removed)
- MMCO/RPLR reference errors from mid-GOP recording starts are common but tolerable
- H.265/HEVC recordings have minimal filler (~0.1%)

**Planned integration:**
The tools will provide ProjectX-style preprocessing for H.264/H.265:
1. Analyze stream for issues
2. Strip filler NALUs (space savings)
3. Demux to elementary streams with correct timing
4. Feed to TTCut for frame-accurate cutting

The project now builds cleanly with Qt 5.15 on modern Linux systems and has full Wayland support.

## Running on Wayland

TTCut requires XCB platform on Wayland systems due to OpenGL compatibility issues.

**Running TTCut:**

```bash
# Run with XCB backend (required for Wayland)
QT_QPA_PLATFORM=xcb ./ttcut

# With logging
QT_QPA_PLATFORM=xcb ./ttcut 2>&1 | tee /path/to/logfile.log
```

The executable name is `ttcut` (lowercase).

## Future Improvements (TODO)

### Unified libav Encoder for All Codecs

**Current state:**
- MPEG-2: Uses ffmpeg CLI via `TTTranscodeProvider` (spawns `/usr/bin/ffmpeg` as external process)
- H.264/H.265: Uses libav directly via `TTESSmartCut` (linked libraries)

**Proposed improvement:**
Create a unified `TTLibavEncoder` class that uses libav directly for all codecs (MPEG-2, H.264, H.265).

**Benefits:**
- Consistent architecture across all codecs
- No external ffmpeg CLI dependency (only libraries)
- Better error handling (direct error codes vs. parsing stdout/stderr)
- Better progress reporting (direct callbacks)
- Slightly better performance (no process spawning overhead)

**Implementation notes:**
- MPEG-2 has different stream structure (Sequence Headers, Picture Headers) vs. H.264/H.265 (NAL units)
- Could extend `TTESSmartCut` or create new generic encoder class
- Low priority since current code works (MPEG-2 re-encoding is rare, only at B-frame cuts)
