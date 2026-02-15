# TTCut-ng TODO / Feature Requests

## High Priority

- **Smart Cut Quality Test Suite**
  - Automated test to verify cut quality by comparing input and output material
  - Must run after each Smart Cut change to catch regressions
  - **Test dimensions:**
    1. **Stream metadata**: FPS, GOP count, frame count, codec params (input vs output)
    2. **Frame timing**: PTS consistency check (expected interval vs actual, flag anomalies)
    3. **Visual comparison**: Extract frames at specific positions (cut boundaries, mid-segment,
       segment transitions) from both input and output, compare with PSNR/SSIM
       - Use `ffmpeg -ss X -i file -frames:v 1 -f image2 frame.png` for extraction
       - Compare corresponding frames: `ffmpeg -i ref.png -i cut.png -lavfi ssim -f null -`
       - Flag if SSIM < 0.95 (re-encoded sections) or < 0.99 (stream-copied sections)
    4. **Audio sync measurement**: Use [syncstart](https://github.com/rpuntaie/syncstart) to
       measure A/V offset between original and cut material via cross-correlation
       - Extract matching segments from original (by time) and from cut output
       - `syncstart original_segment.mkv cut_segment.mkv` → reports offset in seconds
       - Also compare audio waveforms at cut boundaries for glitches/gaps
       - `ffmpeg -i file -af astats=metadata=1:reset=1 -f null -` for audio level analysis
    5. **Duration check**: Total video duration vs total audio duration (must match within 50ms)
    6. **Cut-point integrity**: Verify first/last frame of each segment matches expected content
  - **Reference workflow:**
    1. Mux original ES + audio with mkvmerge (same params as cutting) → reference.mkv
    2. Run Smart Cut → cut.mkv
    3. Compare both at equivalent time positions
  - **Output:** Machine-readable report (pass/fail per test, values, expected ranges)
  - **Tool:** Standalone Python script `tools/ttcut-quality-check.py`
  - **Dependencies:** ffmpeg, ffprobe, python3, syncstart (pip install syncstart)

- **H.264/H.265 A/V Sync improvements in ttcut-demux**
  - Initial B-frame detection before first IDR frame (like MPEG-2)
  - Open GOP handling (B-frames referencing previous GOPs)
  - PTS/DTS offset correction for complex reordering
  - Test with DVB H.264/H.265 recordings with markad markers
  - Verify audio padding works correctly for all codecs

- **Quick Jump: Keyframe thumbnail browser**
  - New window showing keyframes as thumbnails in a grid (ca. 6x5 = 30 per page)
  - Paginated navigation, starting from current position
  - Double-click thumbnail → close window and jump to that frame
  - Existing "Quick Jump" button in main window should open this

- **Replace ffmpeg CLI with libav for MPEG-2 encoding**
  - Currently: MPEG-2 uses TTTranscodeProvider → spawns /usr/bin/ffmpeg
  - Goal: Use libav directly for consistent architecture across all codecs
  - Benefits: No external CLI dependency, better error handling, progress reporting
  - Could extend TTESSmartCut or create new generic encoder class
  - Low priority since current MPEG-2 re-encoding is rare (only at B-frame cuts)

- **Automatic ad detection integration**
  - Load markad/comskip markers and convert to cut points
  - VDR marks are already loaded from .info file
  - Add "Auto-Cut from Markers" button to apply all marker pairs as cuts

- **Built-in stream point detection ("Stream Points" button)**
  - TTCut analyzes the stream and creates markers automatically
  - Detection methods:
    - Black frames (common at ad boundaries)
    - Audio silence detection
    - Scene changes (frame difference analysis)
    - Logo detection (presence/absence of channel logo)
  - Results are added to the Marker list for review
  - User can then convert selected markers to cut points
  - Could use ffmpeg's blackdetect/silencedetect filters or native implementation

## Medium Priority

- **Manual audio delay/offset per track**
  - Allow user to enter a delay value (in ms) for each audio track in the Audio Files list
  - The "Delay" column already exists but is currently unused (always shows "0")
  - Use case: Manual A/V sync correction when automatic detection is wrong or unavailable
  - Apply delay during muxing (mplex -O, mkvmerge --sync)

- Display the resulting stream lengths after cut
- Make the current frame position clickable (enter current frame position)
- Prepare long term processes for user cancellation (abort button)
- Internationalisation (i18n) - translate UI to other languages
- Undo/Redo for cut list operations
- Direct VDR .rec folder support (open recording without manual demux)

### Audio Format Support

**Status:** Open
**Priority:** Medium
**Created:** 2026-01-31

TTCut currently only supports AC3 (Dolby Digital) and MPEG-2 Audio (MP2) formats. Modern DVB broadcasts and streaming sources often use other audio codecs.

#### Requested Audio Formats

| Format | Sync Word | Use Case |
|--------|-----------|----------|
| **AAC** (ADTS) | `0xFFF` | DVB-T2, streaming, modern broadcasts |
| **EAC3** (Dolby Digital Plus) | `0x0B77` + extended header | HD broadcasts, streaming |
| **DTS** | `0x7FFE8001` | Blu-ray, some broadcasts |

#### Current Implementation

Audio detection is in `avstream/ttavtypes.cpp` (lines 180-260), which only checks for:
- AC3: Sync word `0x0B77`
- MPEG Audio: Sync word `0xFFE0`

**E-AC3 (Dolby Digital Plus) status:** `ttcut-demux` correctly demuxes E-AC3 streams with `.eac3` extension. The AC3 header parser (`TTAC3AudioStream`) detects E-AC3 (bsid > 10) and skips it with a warning. A native E-AC3 header parser is needed for frame-accurate cutting within TTCut-ng.

#### Required Changes

For each new format:
1. Add sync word detection in `TTAudioType::getAudioStreamType()`
2. Create new stream class (e.g., `TTEAC3AudioStream`, `TTAacAudioStream`)
3. Create header class (e.g., `TTEAC3AudioHeader`, `TTAacAudioHeader`)
4. Add to `TTAVTypes` enum
5. Update file dialogs in `ttcutmainwindow.cpp`

**E-AC3 specifics:** Same sync word as AC3 (`0x0B77`) but `bsid >= 11`. Frame size is encoded as 11-bit `frmsiz` field (not via lookup table). The existing `AC3FrameLength` table does not apply.

#### Workaround

Convert unsupported audio to AC3:
```bash
ffmpeg -i input.eac3 -c:a ac3 -b:a 384k output.ac3
ffmpeg -i input.aac -c:a ac3 -b:a 384k output.ac3
```

### DVB Subtitle Support

- Support DVB-SUB (bitmap subtitles) and Teletext subtitles
- Extract and convert to SRT or keep as PGS for MKV output

## Low Priority

- **Rename TTMPEG2Window2 → TTVideoFrameWidget**
  - Class name and files (`mpeg2window/ttmpeg2window2.*`) are misleading — the widget handles MPEG-2, H.264, and H.265
  - Rename class, files, and directory (e.g., `videoframe/ttvideoframewidget.*`)
  - Update all includes, .pro file, .ui references, and moc references
- Remove some unused settings and buttons without function
- Implement plugin interface for external tools (encoders, muxers, players)
- Write a FAQ / user documentation
- Find a logo for TTCut-ng
- GPU-accelerated encoding (NVENC, VAAPI, QSV) for faster Smart Cut

## Completed

- [x] H.264/H.265 Smart Cut support (TTESSmartCut)
- [x] SRT subtitle support
- [x] Replace mplayer with mpv for preview
- [x] Replace transcode with ffmpeg for MPEG-2 encoding
- [x] Connect encoder UI settings to actual encoders
- [x] MKV output via mkvmerge
- [x] MKV chapter marks support
- [x] A/V sync offset support for demuxed streams
- [x] New GUI layout with TreeView widgets and multi-input-stream support
- [x] Batch muxing via mux script generation
- [x] Preview: Next/Previous cut navigation buttons
- [x] Current Frame: Play button with audio (via mpv)
- [x] User warning when clicking "New Project"
- [x] Keyboard shortcuts (j/k for frame, g/G for home/end, [ ] for cut-in/out)
- [x] Warning if audio and video length differ
- [x] ttcut-demux: Audio trim at start for A/V offset correction
- [x] ttcut-demux: Audio padding at end (like ProjectX) - reduces drift from 372ms to 8ms
- [x] ttcut-demux: Duration mismatch detection and reporting in .info file
- [x] Preview widget: Corrected button order (Zurück/Start/Vor)
- [x] Fix thread-pool completion race condition (processEvents from worker threads → deadlock)
- [x] Fix AC3 parser infinite loop on E-AC3 streams (bsid > 10 detection + zero frame length guard)
- [x] ttcut-demux: E-AC3 streams get `.eac3` extension (was incorrectly mapped to `.ac3`)
