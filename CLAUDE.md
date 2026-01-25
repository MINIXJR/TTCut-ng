# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TTCut is a Qt5-based video editing application for MPEG-2, H.264, and H.265 streams. It allows frame-accurate cutting without re-encoding the entire stream - only the frames around cut points are re-encoded. The primary use case is removing advertisements from DVB recordings.

**Supported Codecs:**
- MPEG-2 (fully supported, original TTCut functionality)
- H.264/AVC (smart cut via libav/ffmpeg)
- H.265/HEVC (smart cut via libav/ffmpeg)

**Supported Containers:**
- Elementary streams (.m2v, .h264, .h265)
- MPEG Transport Stream (.ts, .m2ts)
- Matroska (.mkv)
- MP4/ISOBMFF (.mp4, .m4v)

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

### H.264/H.265 Smart Cut Support (WIP)

Frame-accurate cutting for H.264/H.265 video streams using an avcut-style approach:

**How it works:**
1. Video is kept in container format (MKV, TS, MP4) - no demuxing to elementary streams
2. GOP (Group of Pictures) analysis determines which GOPs to copy, encode, or drop
3. GOPs fully inside kept segments are stream-copied (no quality loss)
4. GOPs at cut boundaries are decoded and re-encoded (only ~50 frames per cut point)
5. Audio is stream-copied with timestamp adjustment

**Key implementation details (extern/ttffmpegwrapper.cpp):**
- `smartCut()` method handles the cutting process
- GOP modes: 0=drop, 1=full copy, 2=partial (needs encoding)
- Encoder uses `max_b_frames=0` to avoid B-frame complexity in re-encoded sections
- DTS is assigned sequentially via `lastVideoDts` counter
- PTS is adjusted by subtracting `firstKeptPts` and `droppedVideoDuration`

**Known limitation:**
A small stutter (~0.14 seconds / ~7 frames) may occur at middle cut points. This is caused by B-frame reordering discontinuities when transitioning from encoded sections to stream-copied sections. Various approaches were tried (DTS alignment, gap filling, ffmpeg genpts, mkvmerge fix-bitstream) but none fully resolved this issue.

**IMPORTANT: See `H264_SMART_CUT_TRACKING.md` for detailed tracking of all tested approaches!**
This file documents 20+ approaches that have been tested, including what worked, what failed, and why. ALWAYS read this file before attempting new solutions to avoid repeating failed experiments.

**IMPORTANT - Lessons learned from experimentation (do NOT repeat these):**
- **Re-encode as little as possible** - Encoding more frames (e.g., to 2nd keyframe instead of 1st) makes stutter WORSE, not better
- **Do NOT try encoding larger sections** - This was tested and increases stutter at transitions
- **Do NOT try MMCO fix (encoding extra GOP after encode sections)** - Tested 2026-01-25, makes stutter significantly WORSE
- The best results come from encoding only the minimum required frames (from cut point to next keyframe)
- Using `-bf 0` (no B-frames) in encoded sections slightly improves transitions
- Various concat methods (FFmpeg concat demuxer, concat protocol, tsMuxeR) all have similar stutter issues

**Timestamp normalization helps:**
- DVB recordings have PTS starting at arbitrary times (e.g., 40409 seconds into multiplex)
- Normalizing timestamps to start near 0 BEFORE cutting significantly improves middle cut point transitions
- Normalize with: `ffmpeg -fflags +genpts -i input.ts -c copy -avoid_negative_ts make_zero output.ts`
- This is similar to what ProjectX does for MPEG-2 (repair/normalize before cutting)
- A helper script `tools/ts_demux_normalize.sh` is available for preprocessing H.264/H.265 TS files

**Potential future improvements:**
- Cut at GOP boundaries only (loses some frames but clean transitions)
- Investigate how other tools (LosslessCut, avidemux) handle this
- Consider a completely different approach (e.g., full re-encode with hardware acceleration)

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
