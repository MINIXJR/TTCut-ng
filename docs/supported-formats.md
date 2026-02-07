# TTCut-ng Supported Formats

This document describes the video, audio, and subtitle formats supported by TTCut-ng, along with their capabilities and limitations.

---

## Input Format: Elementary Streams Only

TTCut-ng works exclusively with **elementary streams** (demuxed A/V files). Container formats (TS, MKV, MP4) must be demuxed before loading.

**Preprocessing tools:**
- **MPEG-2:** Use ProjectX to demux TS → ES files
- **H.264/H.265:** Use `tools/ttcut-demux -e` to demux TS → ES files + `.info` metadata

---

## Supported Video Codecs

| Codec | Extensions | Decoder | Smart Cut |
|-------|-----------|---------|-----------|
| **MPEG-2** | .m2v | libmpeg2 (native) | Re-encode at cut boundaries (ffmpeg) |
| **H.264/AVC** | .h264, .264 | FFmpeg/libav | TTESSmartCut (native libav) |
| **H.265/HEVC** | .h265, .265, .hevc | FFmpeg/libav | TTESSmartCut (native libav) |

---

## Codec-Specific Capabilities

### MPEG-2

MPEG-2 is the traditional format for DVB recordings and DVD content. TTCut-ng has native support via libmpeg2.

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | Yes | Native libmpeg2 decoder |
| I/P/B frame navigation | Yes | Full frame type support |
| Cut at I-frame (in) | Yes | No re-encoding needed |
| Cut at P-frame (out) | Yes | No re-encoding needed |
| Cut at any frame | Yes | Requires ffmpeg re-encode |
| Quality setting | qscale 2-31 | Lower = better quality |
| Interlace detection | Yes | Auto-detected, passed to encoder |
| Output muxer | mplex (TS/PS) or mkvmerge (MKV) | |

**Cut Point Restrictions (without re-encoding):**
- Cut-in: I-frames only
- Cut-out: P-frames or I-frames

With the encoder enabled (default), cutting at any frame position is possible by re-encoding a few frames around the cut point.

### H.264/AVC

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | Yes | FFmpeg decoder |
| I/P/B frame navigation | Yes | Via native NAL parser (TTNaluParser) |
| Smart Cut at any frame | Yes | TTESSmartCut re-encodes partial GOPs |
| Quality setting | CRF 0-51 | Default 18 (visually lossless) |
| Presets | ultrafast to veryslow | Speed/quality tradeoff |
| Profiles | Baseline, Main, High | High recommended |
| Output muxer | mkvmerge (MKV) | With optional chapters |
| Video playback | Yes | Via mpv (temporary MKV created) |

**Smart Cut Details:**
- ~99.5% of frames are stream-copied (no quality loss)
- ~0.5% of frames at cut boundaries are re-encoded
- Encoder uses `bf=0` (no B-frames) for clean segment transitions

### H.265/HEVC

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | Yes | FFmpeg decoder |
| I/P/B frame navigation | Yes | Via native NAL parser (TTNaluParser) |
| Smart Cut at any frame | Yes | TTESSmartCut re-encodes partial GOPs |
| Quality setting | CRF 0-51 | Default 20 (similar to H.264 CRF 18) |
| Presets | ultrafast to veryslow | Speed/quality tradeoff |
| Profiles | Main, Main10 | Main recommended |
| Output muxer | mkvmerge (MKV) | With optional chapters |
| Video playback | Yes | Via mpv (temporary MKV created) |

**Known limitation:** A small stutter (~0.14 seconds) may occur at middle cut points due to B-frame reordering discontinuities between re-encoded and stream-copied sections.

---

## Supported Output Containers

| Container | Tool | Video Codecs | Chapters | Notes |
|-----------|------|--------------|----------|-------|
| **Matroska (MKV)** | mkvmerge | All | Yes | Recommended, auto-generated chapters |
| **MPEG TS/PS** | mplex | MPEG-2 | No | Traditional DVB output |
| **Elementary** | Direct copy | All | N/A | Raw stream, no container |

### MKV-Specific Features

- **Automatic chapter generation**: Chapters created at regular intervals (default: every 5 minutes)
- **Chapter interval**: Configurable in settings (1-60 minutes)
- **Multiple audio tracks**: Supported
- **Subtitle embedding**: SRT subtitles can be muxed into MKV

---

## Audio Support

| Format | Extensions | Sync Word | Status |
|--------|-----------|-----------|--------|
| **MPEG Audio (MP2)** | .mp2, .mpa | `0xFFE0` | Fully supported |
| **AC3 (Dolby Digital)** | .ac3 | `0x0B77` | Fully supported |

**Not yet implemented** (see TODO.md for status):
- AAC (ADTS) — listed in file dialog but no parser implemented
- EAC3 (Dolby Digital Plus)
- DTS

### Audio Handling

- Audio streams are cut in sync with video
- A/V sync offset from `.info` file is applied during muxing
- Multiple audio tracks can be associated with a video

**Workaround for unsupported audio formats:**
```bash
ffmpeg -i input.aac -c:a ac3 -b:a 384k output.ac3
```

---

## Subtitle Support

| Format | Extensions | Features |
|--------|-----------|----------|
| **SRT (SubRip)** | .srt | Auto-load, preview overlay, cut with video |

### SRT Subtitle Features

- **Auto-loading**: SRT files matching the video filename are automatically loaded
- **Preview overlay**: Subtitles displayed in the video preview window via QPainter
- **mpv preview**: Subtitles passed to mpv player via `--sub-file` parameter
- **Synchronized cutting**: Subtitles are cut along with video/audio
- **Time adjustment**: Subtitle times are adjusted to match cut segments

---

## Metadata Files

| Extension | Purpose | Created By |
|-----------|---------|------------|
| **.info** | Frame rate, resolution, audio tracks, A/V offset, VDR markers | `ttcut-demux` |
| **.idd** | TTCut frame index cache | TTCut |
| **.prj** | TTCut project file (XML) | TTCut |

---

## External Dependencies

### Required

| Tool | Purpose | Package |
|------|---------|---------|
| **libmpeg2** | MPEG-2 decoding | `libmpeg2-4-dev` |
| **FFmpeg libraries** | H.264/H.265 decoding, encoding | `libavformat-dev`, `libavcodec-dev`, `libswscale-dev` |

### Optional (but recommended)

| Tool | Purpose | Package |
|------|---------|---------|
| **ffmpeg** | MPEG-2 frame-accurate cutting | `ffmpeg` |
| **mplex** | MPEG multiplexing | `mjpegtools` |
| **mkvmerge** | MKV container creation, chapters | `mkvtoolnix` |
| **mpv** | Video preview playback | `mpv` |

---

## File Extension Reference

### Video Files

| Extension | Format |
|-----------|--------|
| .m2v | MPEG-2 elementary stream |
| .h264, .264 | H.264/AVC elementary stream |
| .h265, .265, .hevc | H.265/HEVC elementary stream |

### Audio Files

| Extension | Format |
|-----------|--------|
| .mp2, .mpa | MPEG Audio Layer 2 |
| .ac3 | Dolby Digital AC-3 |

---

## Version Information

This documentation applies to TTCut-ng version 0.51.0.

Last updated: February 2026
