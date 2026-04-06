# TTCut-ng TODO / Feature Requests

## High Priority

- ~~**Security Audit Findings beheben**~~ → **25/25 FIXED** (2026-03-28, commits aea1809 + 66eacb2)
  - Siehe [docs/security-audit-2026-03-02.md](docs/security-audit-2026-03-02.md) für alle Findings

- ~~**Smart Cut Quality Test Suite**~~ → **DONE** (`tools/ttcut-quality-check.py` + `verify-smartcut` skill)





- **Logo für TTCut-ng**
  - Projekt braucht ein wiedererkennbares Logo/Icon für GitHub, Debian-Paket, Desktop-Launcher
  - Anforderungen: SVG (skalierbar), funktioniert als 16x16 bis 512x512, passt zu Video-Editing

- **HEVC CRA-only Stream: Smart Cut Verifikation**
  - Testfall: `Ausdrucksstarke_Designermode.265` (HEVC 4K 3840x2160, 50fps, CRA-only, has_b_frames=5)
  - CRA (Clean Random Access, NAL Typ 21) wird korrekt NICHT als IDR markiert → `analyzeCutPoints()` triggert Re-Encode
  - RASL-Bilder (ähnlich Open-GOP B-Frames) könnten DPB-Probleme verursachen wie bei H.264 Non-IDR
  - Verifizieren: Smart Cut ausführen → MKV erzeugen → mpv abspielen → keine Stutter/Artefakte an Segmentgrenzen
  - `ffprobe -v debug`: Keine "backward timestamps" oder "co located POCs" Meldungen
  - Code-Pfad: `ttnaluparser.cpp:495` (CRA nicht isIDR) → `ttessmartcut.cpp:407` (Re-Encode Trigger)

- ~~**Smart Cut Performance: mmap statt QFile für Stream-Copy**~~ → **IMPLEMENTIERT** (2026-03-28, commits d80b918 + 2f3bb69)
  - `accessUnitPtr()` für Zero-Copy mmap Frame-Zugriff, Bulk-Write für ungepatche Segmente
  - **Noch zu testen:** Funktionale Verifikation + Performance-Messung mit echten Dateien

## Medium Priority

- ~~**Decode error detection for H.264/H.265 streams during demux**~~ → **DONE** (v0.63.0)
  - Implemented as `ttcut-pts-analyze` (formerly `ttcut-esrepair`): mmap-based start-code scanner,
    per-segment decode testing with custom AVIOContext, multi-threaded, integrated into ttcut-demux and TTCut-ng
  - H.265 false positives fixed: `AV_EF_CAREFUL` only for H.264/H.265 (not MPEG-2)

- ~~**Projektdatei-Endung: .prj → .ttcut**~~ → **DONE** (v0.63.0)
  - Neue Dateien: `.ttcut`, bestehende `.prj` behalten Endung
  - File-Dialog Filter: `"TTCut Project (*.ttcut);;Legacy Project (*.prj)"`

- **CLI Interface for batch Smart Cut (headless mode)**
  - Standalone CLI tool based on `tools/test_prj_smartcut` architecture
  - Reads `.ttcut` project file, performs Smart Cut + audio cut + MKV mux
  - No X11/Wayland/Qt GUI dependency — runs on servers and in scripts
  - Use case: Automated cutting pipeline (VDR → demux → TTCut-ng CLI → archive)

- **Parallele Dekodierung mit mehreren FFmpegWrapper-Instanzen**
  - Schwarzbild- und Szenenwechsel-Suche: N Worker-Threads mit je eigenem FFmpegWrapper
  - Jeder Worker prüft andere I-Frames gleichzeitig → ~Nx Speedup
  - Hauptgewinn bei HEVC (I-Frame-Decode ~12-15ms bei 1080p, Seek+Flush ~4ms)
  - Erfordert: Thread-Pool, Ergebnis-Aggregation, Abbruch-Koordination

- **Projektdatei (.prj): Fehlende Einstellungen speichern**
  - Aktuell werden nicht alle relevanten Einstellungen in die .prj Datei geschrieben
  - Fehlend u.a.: Ausgabepfad inkl. Dateiname, Encoder-Einstellungen
  - Brainstorming nötig: Bestandsaufnahme was gespeichert wird vs. was fehlt
  - Rückwärtskompatibilität beachten (alte .prj Dateien müssen weiterhin ladbar sein)

- ~~**Dirty-Tracking: "Neues Projekt" Warnung nur bei echten Änderungen**~~ → Completed (v0.62.1)

- **Manual audio delay/offset per track**
  - Allow user to enter a delay value (in ms) for each audio track in the Audio Files list
  - The "Delay" column already exists but is currently unused (always shows "0")
  - Use case: Manual A/V sync correction when automatic detection is wrong or unavailable
  - Apply delay during muxing (mplex -O, libav matroska muxer sync offset)

- Display the resulting stream lengths after cut
- Make the current frame position clickable (enter current frame position)
- Prepare long term processes for user cancellation (abort button)
- Internationalisation (i18n) - translate UI to other languages
  - **de_DE: DONE** (v0.62.0) — alle 165 Strings übersetzt, Q_OBJECT/English source texts standardisiert
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

- **Systemanforderungen dokumentieren**
  - Mindestanforderungen für README/Wiki: Architektur (x86_64), OS, Qt, ffmpeg/libav, libmpeg2
  - Optionale Abhängigkeiten: mplex, mpv, ttcut-pts-analyze
  - Empfehlungen für Speicher/Plattenplatz bei großen DVB-Aufnahmen

## Low Priority

- **Deprecated qSort() → std::sort()**
  - `avstream/ttsubtitleheaderlist.cpp:86` verwendet `qSort()` (deprecated seit Qt 5.2)
  - Ersetzen durch `std::sort()` mit dem gleichen Comparator
  - Ggf. weitere qSort-Vorkommen im Projekt prüfen

- **Live-Timecode bei mpv-Wiedergabe**
  - Im "Aktueller Frame" Widget den Timecode/Frame-Counter während der mpv-Wiedergabe mitlaufen lassen
  - mpv läuft als externer QProcess — kein direkter Zugriff auf die aktuelle Position
  - Ansatz 1: mpv IPC via JSON-Socket (`--input-ipc-server=`) + QTimer-Poll für `playback-time`
  - Ansatz 2: mpv als eingebettetes Widget (libmpv) statt externer Prozess
  - Ansatz 1 ist deutlich einfacher, Ansatz 2 ermöglicht langfristig mehr Kontrolle

- **Auto-Cut from Markers** (ohne .info-Datei, z.B. bei ProjectX-Demux)
  - VDR-Marks werden bei ttcut-demux bereits automatisch als Cut-Einträge übernommen
  - Für manuelle Marker-Listen: Button der Marker-Paare in Cut-Einträge konvertiert
- **Rename TTMPEG2Window2 → TTVideoFrameWidget**
  - Class name and files (`mpeg2window/ttmpeg2window2.*`) are misleading — the widget handles MPEG-2, H.264, and H.265
  - Rename class, files, and directory (e.g., `videoframe/ttvideoframewidget.*`)
  - Update all includes, .pro file, .ui references, and moc references
- **Inaktive UI-Elemente prüfen und ggf. entfernen oder implementieren**
  - Chapters-Tab im Settings-Dialog (`ttcutsettingsdlg.cpp:41`): `removeTab(4)` — "not implemented yet"
  - Chapters-Tab im Schnittdialog (`ttcutavcutdlg.cpp:53`): `removeTab(3)` — "not implemented yet"
  - "Configure..." Button im Muxer-Settings (`ttcutsettingsmuxer.cpp:50`): `setEnabled(false)` — keine Funktion
  - videoFileInfo Widget (`ttcutmainwindow.ui:103`): `maximumSize height=0` — permanent unsichtbar
- Implement plugin interface for external tools (encoders, muxers, players)
- GPU-accelerated encoding (NVENC, VAAPI, QSV) for faster Smart Cut

## Completed

- [x] H.264/H.265 Smart Cut support (TTESSmartCut)
- [x] SRT subtitle support
- [x] Replace mplayer with mpv for preview
- [x] Replace transcode with ffmpeg for MPEG-2 encoding
- [x] Connect encoder UI settings to actual encoders
- [x] MKV output via libav matroska muxer (originally mkvmerge, migrated to libav in v0.60.0)
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
- [x] Replace mkvmerge CLI with libav matroska muxer (v0.60.0)
- [x] Replace ffmpeg CLI audio cutting with libav stream-copy (v0.60.0)
- [x] Remove macOS support code (v0.60.0)
- [x] Remove 1,882 lines dead code from ttffmpegwrapper.cpp (v0.60.0)
- [x] Audio boundary burst detection with shift-button in preview (v0.59.0)
- [x] Audio quality fixes: click false positive, off-by-one duration, bitrate autodetect (v0.58.0)
- [x] Fix H.264 Smart Cut inter-segment stutter via forced-idr (v0.61.0)
- [x] Fix preview stutter by preferring IDR keyframes for preview clip start (v0.61.0)
- [x] Restore CutIn/CutOut editing and burst detection in navigation buttons (v0.61.0)
- [x] Fix frame position sync between slider and navigation buttons (v0.61.1)
- [x] Fix shared videoStream position corruption in navigation and cut points (v0.61.2)
- [x] Separate navigation from auto-save in CurrentFrame widget (v0.61.3)
- [x] Fix Smart Cut segment boundary stutter for B-frame reorder crossing — Case A/B (v0.61.4)
- [x] Fix CutOut frame display for last cut entry — H.264 EOF drain (v0.61.4)
- [x] VDR multi-file support in ttcut-demux — auto-detect, concat protocol, `-n` parameter
- [x] VDR demux example script (`tools/vdr-demux-example.sh`)
- [x] Replace transcode CLI with libavcodec API for MPEG-2 encoding (TTTranscodeProvider)
- [x] H.264/H.265 A/V Sync in ttcut-demux: audio trim, padding, duration mismatch, bitrate autodetect, VDR multi-file
- [x] Zeitsprung (Quick Jump) thumbnail browser dialog with interval filter (v0.61.7)
- [x] Stream Point Detection: Landezonen widget with black frame, silence, audio format change, scene change detection via libavfilter; cut pair auto-derivation; .prj persistence (v0.62.0)
- [x] Dirty-tracking for unsaved project changes (v0.62.1)
- [x] Decode error detection for H.264/H.265 streams — ttcut-pts-analyze with mmap, multi-threaded decode testing (v0.63.0)
- [x] Security audit: all 25 findings fixed (v0.63.0)
- [x] German translations (de_DE): all 165 strings, Q_OBJECT standardization (v0.62.0)
- [x] Screenshot automation: `--screenshots` CLI mode with test media generation
- [x] MPEG-2 extra-frame correction for A/V sync and quality-check (v0.63.0)
- [x] Remove redundant F-buttons from navigation widget, add frame-type labels (I, P/I, B/P/I)
- [x] Remove redundant "Set Cut-Out" from cut list context menu, reorder entries
- [x] Logo detection: markad PGM import and manual ROI selection with Sobel edge profiling
- [x] Logo profile persistence in project file (.ttcut)
- [x] Project file extension change: .prj → .ttcut (with backward compatibility)
- [x] Pillarbox detection: 4:3 in 16:9 with 10s hysteresis (all codecs, I-frame analysis)
- [x] Progress dialog for Landezonen analysis

## Known Limitations

- **Multi-frame audio burst at cut boundaries**: DVB advertising audio can bleed 2-3+ audio frames before the video transition. The current burst detection checks only the last 2 audio frames at the CutOut boundary and offers single-frame shift (-1). For multi-frame bursts, the user must shift multiple times. Additionally, isolated burst frames can appear in the silence region between segments (mid-transition), which are not detected by the edge-based algorithm.

- **Cut point stutter (rare)**: For streams without any IDR frames (only Non-IDR I-slices), Smart Cut re-encodes 1 GOP at each segment boundary to produce an IDR. This is typically invisible but may cause minor quality differences at cut points (~0.5% of frames affected). When B-frame reorder delay shifts CutIn past the stream-copy keyframe (Case B), a small leak of ≤ reorder_delay pre-CutIn frames may occur to avoid POC domain mismatch.
