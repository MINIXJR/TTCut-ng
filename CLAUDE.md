# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TTCut is a Qt5-based video editing application for MPEG-2 streams. It allows frame-accurate cutting of MPEG-2 video without re-encoding the entire stream - only the frames around cut points are re-encoded. The primary use case is removing advertisements from DVB recordings.

**Key Constraint**: Due to MPEG-2 structure, cuts without re-encoding are only possible at:
- Cut-in: I-frames only
- Cut-out: P-frames or I-frames

If ffmpeg is installed, cutting is possible at any frame by re-encoding 2-4 frames around the cut point.

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
│   └── TTMpeg2VideoStream
└── TTSubtitleStream
    └── TTSrtSubtitleStream
```

### Module Organization

- **avstream/**: Core stream handling - MPEG-2 video/audio parsing, header lists, file buffers, index lists, AVI writing
- **gui/**: Qt widgets and main window (TTCutMainWindow is the main entry point)
- **common/**: Global settings (TTCut singleton), message logging, exception handling
- **data/**: Data structures for audio lists, cut lists, muxer lists, cut parameters
- **mpeg2decoder/**: MPEG-2 decoding using libmpeg2
- **mpeg2window/**: OpenGL-based MPEG-2 frame display
- **extern/**: External tool integration (ffmpeg for re-encoding, mplex for multiplexing)
- **ui/**: Qt Designer .ui files and resource files (.qrc)
- **avilib/**: AVI file writing library (C code)

### Key Classes

- **TTCutMainWindow** (gui/ttcutmainwindow.h): Main application window, coordinates all operations
- **TTMpeg2VideoStream** (avstream/ttmpeg2videostream.h): Core video stream class, handles cutting operations
- **TTCut** (common/ttcut.h): Singleton holding global settings and application state
- **TTTranscodeProvider** (extern/tttranscode.h): Wraps ffmpeg for re-encoding frames around cut points
- **TTMplexProvider** (extern/ttmplexprovider.h): Multiplexes video/audio after cutting

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
- OpenGL and GLU (frame display)
- ffmpeg (required for frame-accurate cutting at any position)
- mplex (optional, for multiplexing)

## Current Branch

Working on branch: `master`

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

The project now builds cleanly with Qt 5.15 on modern Linux systems and has full Wayland support.

## Running on Wayland

TTCut has full native Wayland support using the modern QOpenGLWidget class.

**Running TTCut:**

```bash
# Run with native Wayland (works perfectly)
./TTCut
```

No special configuration or workarounds are needed. TTCut works natively on Wayland without any OpenGL-related warnings.
