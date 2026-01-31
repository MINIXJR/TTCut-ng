# TTCut-ng TODO / Feature Requests

## High Priority

- **Replace ffmpeg CLI with libav for MPEG-2 encoding**
  - Currently: MPEG-2 uses TTTranscodeProvider → spawns /usr/bin/ffmpeg
  - Goal: Use libav directly for consistent architecture across all codecs
  - Benefits: No external CLI dependency, better error handling, progress reporting
  - See CLAUDE.md "Future Improvements" section for details

## Medium Priority

- Show a warning if audio and video length are different
- Display the resulting stream lengths after cut
- Make the current frame position clickable (enter current frame position)
- More keyboard shortcuts
- User warning when click "New Project" and video stream is loaded

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

## Low Priority

- Remove some unused settings and buttons without function
- Find a solution for realtime playback of demuxed video stream (play-button)

## Completed

- [x] H.264/H.265 Smart Cut support (TTESSmartCut)
- [x] SRT subtitle support
- [x] Replace mplayer with mpv for preview
- [x] Replace transcode with ffmpeg for MPEG-2 encoding
- [x] Connect encoder UI settings to actual encoders
- [x] MKV output via mkvmerge
- [x] A/V sync offset support for demuxed streams
