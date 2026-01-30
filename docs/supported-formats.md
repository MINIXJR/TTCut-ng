# TTCut-ng Supported Formats

This document describes the video, audio, and subtitle formats supported by TTCut-ng, along with their capabilities and limitations.

---

## Supported Input Video Codecs

| Codec | Stream Types | Container Formats | Decoder |
|-------|-------------|-------------------|---------|
| **MPEG-2** | Elementary (.m2v) | ES, TS, PS | libmpeg2 (native) |
| **H.264/AVC** | Elementary (.h264, .264) | ES, TS, MKV, MP4 | FFmpeg/libav |
| **H.265/HEVC** | Elementary (.h265, .265) | ES, TS, MKV, MP4 | FFmpeg/libav |

---

## Supported Input Container Formats

| Container | Extensions | Auto-Demux | Notes |
|-----------|-----------|------------|-------|
| **Elementary Stream** | .m2v, .h264, .h265 | No | Direct input, preferred |
| **Transport Stream** | .ts, .m2ts | Yes | DVB recordings |
| **Program Stream** | .mpg, .mpeg | Yes | DVD-style |
| **Matroska** | .mkv | Yes | Via FFmpeg demux |
| **MP4/ISOBMFF** | .mp4, .m4v | Yes | Via FFmpeg demux |

### Notes on Container Input

- **Elementary streams** are the preferred input format as they allow direct stream manipulation
- **Container formats** (TS, MKV, MP4) are automatically demuxed to elementary streams before processing
- The demuxing process extracts video and audio tracks separately

---

## Codec-Specific Capabilities

### MPEG-2

MPEG-2 is the traditional format for DVB recordings and DVD content. TTCut-ng has native support via libmpeg2.

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | ✓ | Native libmpeg2 decoder |
| I/P/B frame navigation | ✓ | Full frame type support |
| Cut at I-frame (in) | ✓ | No re-encoding needed |
| Cut at P-frame (out) | ✓ | No re-encoding needed |
| Cut at any frame | ✓ | Requires FFmpeg re-encode |
| Quality setting | qscale 2-31 | Lower = better quality |
| Output muxer | mplex (TS/PS) | Default for MPEG-2 |

**Cut Point Restrictions (without re-encoding):**
- Cut-in: I-frames only
- Cut-out: P-frames or I-frames

With FFmpeg installed, cutting at any frame position is possible by re-encoding 2-4 frames around the cut point.

### H.264/AVC

H.264 is the most common modern video codec. TTCut-ng uses FFmpeg for decoding and encoding.

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | ✓ | FFmpeg decoder |
| I/P/B frame navigation | ✓ | Via frame index |
| Cut at keyframe | ✓ | IDR frames |
| Cut at any frame | ✓ | Requires re-encode |
| Quality setting | CRF 0-51 | Default 23, lower = better |
| Presets | ultrafast → veryslow | Speed/quality tradeoff |
| Profiles | Baseline, Main, High | High recommended |
| Output muxer | mkvmerge (MKV) | Default for H.264 |

**Encoder Presets:**
- `ultrafast`, `superfast`, `veryfast`, `faster`, `fast`
- `medium` (default)
- `slow`, `slower`, `veryslow`

**Quality (CRF) Guidelines:**
- 0 = Lossless
- 18 = Visually lossless
- 23 = Default (good quality)
- 28 = Acceptable quality
- 51 = Worst quality

### H.265/HEVC

H.265 offers better compression than H.264 at the cost of encoding speed.

| Feature | Status | Notes |
|---------|--------|-------|
| Frame preview | ✓ | FFmpeg decoder |
| I/P/B frame navigation | ✓ | Via frame index |
| Cut at keyframe | ✓ | IDR frames |
| Cut at any frame | ✓ | Requires re-encode |
| Quality setting | CRF 0-51 | Default 28 (≈ H.264 CRF 23) |
| Presets | ultrafast → veryslow | Speed/quality tradeoff |
| Profiles | Main, Main10 | Main recommended |
| Output muxer | mkvmerge (MKV) | Default for H.265 |

**Quality (CRF) Guidelines:**
- H.265 CRF 28 ≈ H.264 CRF 23 (similar visual quality)
- Typical range: 24-34
- Lower values = better quality, larger files

---

## Supported Output Containers

| Container | Tool | Video Codecs | Chapters | Delete ES | Notes |
|-----------|------|--------------|----------|-----------|-------|
| **MPEG TS/PS** | mplex | MPEG-2 | No | Optional | Traditional DVB output |
| **Matroska (MKV)** | mkvmerge | All | Yes | Optional | Auto-generated chapters |
| **MP4** | FFmpeg | H.264, H.265 | No | Optional | ISOBMFF container |
| **Elementary** | Direct copy | All | N/A | No | Raw stream, no container |

### MKV-Specific Features

- **Automatic chapter generation**: Chapters can be created at regular intervals (default: every 5 minutes)
- **Chapter interval**: Configurable in settings (1-60 minutes)
- **Multiple audio tracks**: Supported
- **Subtitle embedding**: SRT subtitles can be muxed into MKV

### Recommended Output Container by Codec

| Input Codec | Recommended Output | Reason |
|-------------|-------------------|--------|
| MPEG-2 | MPEG TS (mplex) | Native format, best compatibility |
| H.264 | MKV (mkvmerge) | Modern container, chapter support |
| H.265 | MKV (mkvmerge) | Modern container, chapter support |

---

## Audio Support

| Format | Extensions | Codec | Notes |
|--------|-----------|-------|-------|
| **MPEG Audio** | .mp2, .mpa | MPEG-1 Layer 2 | Standard DVB audio |
| **AC3/Dolby Digital** | .ac3 | AC-3 | Dolby Digital 5.1 |

### Audio Handling

- Audio streams are cut in sync with video
- Frame-accurate audio cutting based on video frame rate
- Multiple audio tracks can be associated with a video

---

## Subtitle Support

| Format | Extensions | Features |
|--------|-----------|----------|
| **SRT (SubRip)** | .srt | Auto-load, preview overlay, cut with video |

### SRT Subtitle Features

- **Auto-loading**: SRT files matching the video filename are automatically loaded
- **Preview overlay**: Subtitles are displayed in the video preview window
- **mpv preview**: Subtitles are passed to mpv player via `--sub-file` parameter
- **Synchronized cutting**: Subtitles are cut along with video/audio
- **Time adjustment**: Subtitle times are adjusted to match cut segments

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
| **ffmpeg** | Frame-accurate cutting, transcoding | `ffmpeg` |
| **mplex** | MPEG multiplexing | `mjpegtools` |
| **mkvmerge** | MKV container creation | `mkvtoolnix` |
| **mpv** | Video preview playback | `mpv` |

---

## File Extension Reference

### Video Files

| Extension | Format | Notes |
|-----------|--------|-------|
| .m2v | MPEG-2 elementary | Demuxed video |
| .mpg, .mpeg | MPEG Program Stream | DVD-style container |
| .ts, .m2ts | MPEG Transport Stream | DVB recordings |
| .h264, .264 | H.264 elementary | Raw AVC stream |
| .h265, .265 | H.265 elementary | Raw HEVC stream |
| .mkv | Matroska | Modern container |
| .mp4, .m4v | MPEG-4 Part 14 | ISOBMFF container |

### Audio Files

| Extension | Format |
|-----------|--------|
| .mp2, .mpa | MPEG Audio Layer 2 |
| .ac3 | Dolby Digital AC-3 |

### Other Files

| Extension | Purpose |
|-----------|---------|
| .srt | SRT subtitles |
| .idd | TTCut index data |
| .prj | TTCut project file |

---

## Version Information

This documentation applies to TTCut-ng version 0.30.x and later.

Last updated: January 2025
