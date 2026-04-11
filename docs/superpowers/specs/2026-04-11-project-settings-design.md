# Project Settings Persistence in .ttcut

**Date:** 2026-04-11
**Status:** Design approved

## Overview

Store per-project output, muxing, and encoder settings in the `.ttcut` project file. When a project is loaded, its settings temporarily override the global TTCut defaults. When the project is closed, globals are restored from QSettings.

## Motivation

Currently, output path, encoder quality, and mux options are global settings. Users must reconfigure them when switching between projects with different requirements (e.g., Film with H.265/CRF 18 vs. Series with MPEG-2/CRF 4).

## Stored Settings

### Output
- `cutDirPath` (QString) — Output directory
- `cutVideoName` (QString) — Output filename (without extension)
- `cutAddSuffix` (bool) — Append "_cut" suffix

### Muxing
- `outputContainer` (int) — 0=TS, 1=MKV, 2=MP4, 3=Elementary
- `mkvCreateChapters` (bool) — Create chapter marks in MKV
- `mkvChapterInterval` (int) — Chapter interval in minutes
- `muxDeleteES` (bool) — Delete elementary streams after muxing

### Encoder (active codec only)
- `encoderPreset` (int) — Preset index (0-8: ultrafast to veryslow)
- `encoderCrf` (int) — Quality factor (0-51)
- `encoderProfile` (int) — Codec-specific profile

The codec is NOT stored — it is determined by the video stream type (MPEG-2, H.264, H.265). The encoder values are mapped to the codec-specific TTCut fields at load time:
- MPEG-2 video → `mpeg2Preset`, `mpeg2Crf`, `mpeg2Profile`
- H.264 video → `h264Preset`, `h264Crf`, `h264Profile`
- H.265 video → `h265Preset`, `h265Crf`, `h265Profile`

## XML Format

New `<Settings>` section in `.ttcut` XML, written after AV data sections:

```xml
<Settings>
  <CutDirPath>/media/Filme/</CutDirPath>
  <CutVideoName>Mein_Film</CutVideoName>
  <CutAddSuffix>true</CutAddSuffix>
  <OutputContainer>1</OutputContainer>
  <MkvCreateChapters>true</MkvCreateChapters>
  <MkvChapterInterval>5</MkvChapterInterval>
  <MuxDeleteES>false</MuxDeleteES>
  <EncoderPreset>5</EncoderPreset>
  <EncoderCrf>18</EncoderCrf>
  <EncoderProfile>2</EncoderProfile>
</Settings>
```

All elements are optional. Missing elements → global default remains unchanged.

## Load/Close Behavior

### On Project Load
1. Parse `<Settings>` section (if present)
2. Override TTCut globals with project values
3. Codec-specific encoder mapping: determine codec from video stream type, then set the corresponding `h264Crf`/`h265Crf`/`mpeg2Crf` (and preset/profile) fields
4. Update UI (Settings dialog, Cut dialog) to reflect new values

### On Project Close / New Project
1. Restore TTCut globals from `QSettings` (the persistent application settings)
2. Project-specific overrides are discarded
3. UI reflects restored global defaults

### On Project Save
1. Read current TTCut globals
2. Write `<Settings>` section with all values (always write all — simplifies code, and values are small)

## Backward Compatibility

- Old `.ttcut` files without `<Settings>` load without error or warning — globals remain unchanged
- The `<Settings>` section is ignored by older TTCut-ng versions (unknown XML elements are skipped during parsing)

## Affected Files

### Serialization
- `data/ttcutprojectdata.h` — add `serializeSettings()`, `parseSettingsSection()` declarations
- `data/ttcutprojectdata.cpp` — implement write/read of `<Settings>` XML section

### Global Settings Restore
- `common/ttcut.h` — add `restoreSettings()` method (reads from QSettings)
- `common/ttcut.cpp` — implement `restoreSettings()`; may reuse existing `readSettings()` logic

### Integration
- `data/ttavdata.cpp` — call `parseSettingsSection()` during project load, apply to TTCut globals
- `gui/ttcutmainwindow.cpp` — call `TTCut::restoreSettings()` in `closeProject()`
- `gui/ttcutsettingsdlg.cpp` — may need refresh after project load to reflect new values
- `gui/ttcutavcutdlg.cpp` — reads TTCut globals at dialog open (should pick up project values automatically)
