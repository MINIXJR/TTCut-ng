# Library Migration Design: CLI Tools → libav API

## Goal

Replace external CLI tools (ffprobe, ffmpeg, mkvmerge, mplex) with direct
libav/libavformat/libavcodec API calls to eliminate QProcess overhead (~200-700ms
per call) and remove external binary dependencies.

## Current State

| Tool | CLI Calls | Purpose | Library Available |
|------|-----------|---------|-------------------|
| ffprobe | 2 (C++) + 20+ (bash/python) | Stream metadata, keyframes | libavformat (already linked) |
| ffmpeg | 8 (C++) | Audio cut, frame extraction, muxing | libavformat/libavcodec (already linked) |
| mkvmerge | 6 (C++) + 2 (test) + 1 (bash) | MKV container muxing | libav matroska muxer |
| mplex | 1 (C++) | MPEG-2 TS muxing | libav mpegts muxer |
| mpv | 2 (C++) | Video playback (UI) | **Kept as-is** |

**Already migrated to libav:**
- `detectAudioBurst()` — uses avformat/avcodec directly (no CLI)
- `TTESSmartCut` — native NAL parser + libav encode/decode
- Frame decoding for preview — libav decode + QImage

## Migration Strategy: 4 Phases (Modulweise)

### Phase 1: ffprobe → libav Metadata (C++)

**Files:** `extern/ttffmpegwrapper.cpp` lines 1637-1671

**Current:** Two `QProcess` calls to ffprobe via `bash -c`:
1. `ffprobe -show_entries stream=width,height -of csv=p=0` → parse CSV
2. `ffprobe -show_entries packet=pts_time,flags -of csv=p=0` → parse CSV for keyframes

**New:** Single `avformat_open_input()` + `avformat_find_stream_info()`:
```cpp
AVFormatContext* fmtCtx = nullptr;
avformat_open_input(&fmtCtx, wrappedES, nullptr, nullptr);
avformat_find_stream_info(fmtCtx, nullptr);

// Dimensions (replaces ffprobe call 1)
int videoIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
videoWidth = fmtCtx->streams[videoIdx]->codecpar->width;
videoHeight = fmtCtx->streams[videoIdx]->codecpar->height;

// Keyframe timestamps (replaces ffprobe call 2)
AVPacket pkt;
int frameNum = 0;
while (av_read_frame(fmtCtx, &pkt) >= 0) {
    if (pkt.stream_index == videoIdx) {
        if (pkt.flags & AV_PKT_FLAG_KEY) {
            double pts = pkt.pts * av_q2d(fmtCtx->streams[videoIdx]->time_base);
            keyframeTimestamps[frameNum] = pts;
        }
        frameNum++;
    }
    av_packet_unref(&pkt);
}
avformat_close_input(&fmtCtx);
```

**Gain:** ~700ms saved (2× QProcess start eliminated)

---

### Phase 2: Audio Cutting → libav Stream-Copy

**Files:** `extern/ttffmpegwrapper.cpp` lines 2235-2377

**Current:** ffmpeg CLI for:
- Single segment: `ffmpeg -ss start -to end -c:a copy output`
- Multi-segment: N× ffmpeg per segment + `ffmpeg -f concat -c:a copy`
- Per preview clip + final cut = up to 20 QProcess calls

**New:** Direct libav stream-copy:
```cpp
// Open input
AVFormatContext* inFmt = nullptr;
avformat_open_input(&inFmt, inputFile, nullptr, nullptr);
avformat_find_stream_info(inFmt, nullptr);
int audioIdx = av_find_best_stream(inFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

// Open output
AVFormatContext* outFmt = nullptr;
avformat_alloc_output_context2(&outFmt, nullptr, nullptr, outputFile);
AVStream* outStream = avformat_new_stream(outFmt, nullptr);
avcodec_parameters_copy(outStream->codecpar, inFmt->streams[audioIdx]->codecpar);
avio_open(&outFmt->pb, outputFile, AVIO_FLAG_WRITE);
avformat_write_header(outFmt, nullptr);

// For each segment: seek + copy packets with PTS filter
for (auto& seg : cutList) {
    av_seek_frame(inFmt, audioIdx, seg.first / av_q2d(timeBase), AVSEEK_FLAG_BACKWARD);
    AVPacket pkt;
    while (av_read_frame(inFmt, &pkt) >= 0) {
        if (pkt.stream_index != audioIdx) { av_packet_unref(&pkt); continue; }
        double pts = pkt.pts * av_q2d(inFmt->streams[audioIdx]->time_base);
        if (pts >= seg.second) { av_packet_unref(&pkt); break; }
        if (pts >= seg.first) {
            av_packet_rescale_ts(&pkt, inFmt->streams[audioIdx]->time_base, outStream->time_base);
            pkt.pts += ptsOffset;  // Adjust for multi-segment concatenation
            pkt.dts += ptsOffset;
            av_write_frame(outFmt, &pkt);
        }
        av_packet_unref(&pkt);
    }
    ptsOffset = ...; // Calculate from last written PTS
}

av_write_trailer(outFmt);
```

**Key difference from CLI:** Multi-segment handled in single pass — no temp files,
no concat demuxer. PTS offset managed manually.

**Gain:** ~500ms per cut, no temp files for multi-segment

---

### Phase 3: MKV Muxing → libav Matroska Muxer

**Files:**
- `extern/ttmkvmergeprovider.cpp` — entire class reimplemented
- `extern/ttffmpegwrapper.cpp` lines 1629-1632 (system() call), 2040-2061
- `gui/ttcurrentframe.cpp` lines 707-721

**Current:** mkvmerge CLI with features:
- `--default-duration 0:Xns` (frame timing)
- `--sync 0:Xms` (A/V sync offset)
- `--language 0:deu` (track language)
- `--title "..."` (container title)
- `--chapters chapters.txt` (OGM chapter format)
- Multiple audio/subtitle tracks

**New:** libav matroska muxer:
```cpp
AVFormatContext* outFmt = nullptr;
avformat_alloc_output_context2(&outFmt, nullptr, "matroska", outputFile);

// Title
av_dict_set(&outFmt->metadata, "title", title, 0);

// Video stream
AVStream* vStream = avformat_new_stream(outFmt, nullptr);
// Copy codec params from input
avcodec_parameters_copy(vStream->codecpar, inVideoStream->codecpar);
// Default duration → time_base + r_frame_rate
vStream->time_base = {1, 1000000000};  // nanoseconds
vStream->r_frame_rate = {25, 1};

// Audio streams (with language + sync offset)
for (auto& audioFile : audioFiles) {
    AVStream* aStream = avformat_new_stream(outFmt, nullptr);
    // Copy params from audio input
    av_dict_set(&aStream->metadata, "language", "deu", 0);
    // Sync offset applied as PTS shift during packet writing
}

// Subtitle streams
// ...

// Chapters (from OGM chapter file)
// Parse chapters.txt → outFmt->chapters array

avio_open(&outFmt->pb, outputFile, AVIO_FLAG_WRITE);
avformat_write_header(outFmt, nullptr);
// ... interleaved packet writing ...
av_write_trailer(outFmt);
```

**mkvmerge feature mapping:**

| mkvmerge Flag | libav Equivalent |
|---------------|------------------|
| `--default-duration 0:Xns` | `stream->time_base`, `stream->r_frame_rate` |
| `--sync 0:Xms` | PTS offset in `av_packet_rescale_ts()` |
| `--language 0:deu` | `av_dict_set(&stream->metadata, "language", ...)` |
| `--title "..."` | `av_dict_set(&fmtCtx->metadata, "title", ...)` |
| `--chapters file.txt` | `fmtCtx->chapters` array |
| Exit code 0/1 | n/a (direct error returns) |

**Gain:** ~300ms per mux, mkvmerge binary no longer required

---

### Phase 4: Remaining Tools

#### 4a: mplex → libav MPEG-TS Muxer

**File:** `extern/ttmplexprovider.cpp`

Replace `mplex -f8` with libav `mpegts` output format. Low priority (legacy MPEG-2).

#### 4b: system() mkvmerge call

**File:** `extern/ttffmpegwrapper.cpp` line 1632

Eliminated by Phase 3 — replaced by libav matroska muxer.

#### 4c: extractSegment() ffmpeg call

**File:** `extern/ttffmpegwrapper.cpp` lines 1157-1219

Check if still used. If yes, replace with libav decode/encode (similar to
TTESSmartCut which already does this). If unused, remove.

#### 4d: ttcut-demux → C++ Binary

**Current:** 1100-line bash script using ffprobe (20+ calls) + ffmpeg (5+ calls) + kdialog.

**New:** Standalone C++ binary `ttcut-demux` using libav:
- `avformat_open_input()` for TS container parsing
- `av_read_frame()` for packet-level demuxing
- Stream selection via `av_find_best_stream()`
- Audio trim: PTS-based packet filtering
- .info file generation: direct from `AVStream` metadata
- kdialog calls kept via QProcess (UI interaction)

Build integration: Add to `ttcut-ng.pro` as separate target or create
`tools/ttcut-demux.pro`.

#### 4e: quality-check.py

Migrate ffprobe calls to PyAV (Python libav wrapper) or keep as-is (test code,
not deployed to users). Lower priority.

## Dependencies

**No new build dependencies** for Phases 1-3 — libavformat, libavcodec, libavutil,
libswscale are already linked.

**Phase 4d** (ttcut-demux C++) requires Qt5 Core (for QProcess/kdialog, QFileInfo).

## Files Changed per Phase

| Phase | Files Modified | Files Removed |
|-------|---------------|---------------|
| 1 | `extern/ttffmpegwrapper.cpp` | — |
| 2 | `extern/ttffmpegwrapper.cpp` | — |
| 3 | `extern/ttmkvmergeprovider.cpp`, `extern/ttffmpegwrapper.cpp`, `gui/ttcurrentframe.cpp` | — |
| 4a | `extern/ttmplexprovider.cpp` | — |
| 4d | `tools/ttcut-demux` (bash) | `tools/ttcut-demux` → `tools/ttcut-demux.cpp` |

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 | Low | Direct struct access, well-documented API |
| 2 | Medium | Audio PTS boundary precision critical. Test with AC3 + MP2 |
| 3 | Medium | Chapter support and language tags need verification |
| 4d | High | 1100 lines of bash with many edge cases (VDR marks, multi-audio) |

## mpv — Not Migrated

mpv remains as external video player. Rationale:
- Wayland support, subtitle rendering, A/V sync — hard to replicate
- Only 2 call sites, both for UI playback (not batch processing)
- libmpv alternative exists but adds build complexity with limited benefit
