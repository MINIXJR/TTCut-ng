# TTCut-ng TODO / Feature Requests

## High Priority


- **Quick Jump: Keyframe thumbnail browser**
  - New window showing keyframes as thumbnails in a grid (ca. 6x5 = 30 per page)
  - Paginated navigation, starting from current position
  - Double-click thumbnail → close window and jump to that frame
  - Existing "Quick Jump" button in main window should open this

- **Replace ffmpeg CLI with libav for MPEG-2 encoding**
  - Currently: MPEG-2 uses TTTranscodeProvider → spawns /usr/bin/ffmpeg
  - Goal: Use libav directly for consistent architecture across all codecs
  - Benefits: No external CLI dependency, better error handling, progress reporting
  - See CLAUDE.md "Future Improvements" section for details

- **Automatic ad detection integration**
  - Load markad/comskip markers and convert to cut points
  - VDR marks are already loaded from .info file
  - Add "Auto-Cut from Markers" button to apply all marker pairs as cuts

## Medium Priority

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

#### Required Changes

For each new format:
1. Add sync word detection in `TTAudioType::getAudioStreamType()`
2. Create new stream class (e.g., `TTAacAudioStream`)
3. Create header class (e.g., `TTAacAudioHeader`)
4. Add to `TTAVTypes` enum
5. Update file dialogs in `ttcutmainwindow.cpp`

#### Workaround

Convert unsupported audio to AC3:
```bash
ffmpeg -i input.aac -c:a ac3 -b:a 384k output.ac3
```

### DVB Subtitle Support

- Support DVB-SUB (bitmap subtitles) and Teletext subtitles
- Extract and convert to SRT or keep as PGS for MKV output

## Low Priority

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
