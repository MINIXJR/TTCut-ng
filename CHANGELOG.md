# Changelog

All notable changes to TTCut-ng are documented in this file.

## v0.62.0 (2026-03-18)

**Landezonen, Zeitsprung & Uebersetzungen**

### Features
- Landezonen (Stream Point Detection): Automatische Erkennung von Schwarzbildern, Stille,
  Audioformatwechsel (AC3 acmod), Szenenwechsel und Seitenverhaeltnisaenderungen (MPEG-2 4:3/16:9)
  mit Schnittvorschlaegen und Projektpersistenz
- Zeitsprung: Keyframe-basierter Thumbnail-Browser mit Seitennavigation, dynamischen
  Seitenverhaeltnis-Thumbnails, Intervallfilter und Fenstergeometrie-Persistenz
- Interaktive Schwarzbild- und Szenenwechsel-Navigation ueber Buttons im Navigationswidget
- Histogramm- und Szenenwechselanalyse in Video-Decodern (MPEG-2, H.264, H.265)

### Fixes
- Fix: ttcut-demux Audio-Padding zerstoerte AC3 per-Frame acmod (Stereo/5.1 Wechsel gingen verloren)
  - Padding nutzt jetzt anullsrc + concat stream-copy statt Vollre-Encoding
- Fix: B/F Navigationsbuttons sprangen zurueck zur Cut-In Position

### Changes
- Deutsche Uebersetzungen vollstaendig aktualisiert (165 fehlende Strings ergaenzt)
- TODO bereinigt: erledigte Eintraege entfernt (Quick Jump, AC3 Demux)

## v0.61.7 (2026-03-11)

**MPEG-2 MKV Muxing Fix + Settings Migration**

- Fix: MPEG-2 finaler Schnitt erzeugte MKV ohne Video (nur Audio)
  - Root Cause: `setDefaultDuration()` fehlte im MPEG-2 MKV-Mux-Pfad (`onCutFinished`)
  - Matroska-Muxer verwarf alle Video-Packets wegen fehlender Timestamps
- Fix: MPEG-2 MKV hatte falsches Seitenverhältnis (16:9 als 4:3 dargestellt)
  - Root Cause: Matroska-Muxer nutzt `stream->sample_aspect_ratio`, nicht `codecpar->sample_aspect_ratio`
  - SAR wird jetzt auf Stream-Level kopiert (ES-Mux und Container-Remux Pfade)
- Settings-Pfad von `~/.config/TriTime/TTCut.conf` nach `~/.config/TTCut-ng/TTCut-ng.conf` migriert

## v0.61.6 (2026-03-09)

**Audio Drift Fix bei B-Frame Reorder**

- Fix: Akkumulierender A/V-Drift (bis 448ms bei 4 Segmenten) bei H.264 Streams mit B-Frames
- Root Cause: B-Frame Display-Order-Mapping verschiebt CutIn-AU nach vorn, Video hat weniger
  Frames als die Audio-Schnittbereiche vorgeben
- Smart Cut meldet jetzt tatsaechliche Start-AUs pro Segment via `actualOutputFrameRanges()`
- Audio keepList-Startzeiten werden nach Video-Smart-Cut an tatsaechliche Video-Ausgabe angepasst
- Restdrift: 32ms (= 1 AC3 Frame, physikalisches Minimum bei Audio-Stream-Copy)

## v0.61.5 (2026-03-08)

**H.264 POC Domain Mismatch Fix**

- Fix: POC-Domain-Mismatch am Re-Encode/Stream-Copy Uebergang wenn Encoder-SPS (MaxPocLsb=16)
  und Source-SPS (MaxPocLsb=64) unterschiedliche Parameter haben
- Patch: poc_lsb im letzten Encoder-Slice wird korrigiert um PicOrderCntMsb-Wrap zu verhindern
- Case A/B vereinheitlicht: beide erweitern Re-Encode zum naechsten Keyframe
- Encoder-SPS wird aus Inline-NAL im ersten Encoder-Paket geparst (x264 ohne GLOBAL_HEADER)
- `findH264SpsInPacket()` Helper eliminiert Code-Duplikation
- Post-Patch-Validation mit Brute-Force-Fallback falls Modulo-Clamping Wrap re-introduziert

## v0.61.4 (2026-03-01)

**Smart Cut B-Frame Reorder Boundary Fix**

- Fix: B-Frame Reorder Delay verschiebt CutIn ueber Stream-Copy-Grenze
- `needsIDR` Parameter durch `adjustedStreamCopyStart` Output-Parameter ersetzt
- EOS NAL wird immer vor Stream-Copy geschrieben (DPB-Flush)
- Pre-Extension der Decode-Range vor dem Decode-Loop

## v0.61.3 (2026-03-01)

**Navigation/Auto-Save Trennung**

- Navigation-Buttons (B/I/P) in der Navigation-Leiste speichern nicht mehr automatisch
- Trennung von Navigation und Schnittpunkt-Setzen: Navigieren ist frei, Speichern nur
  ueber explizite Buttons

## v0.61.2 (2026-02-27)

**Shared VideoStream Position Fix**

- Fix: Shared videoStream Position-Korruption bei Navigation und Schnittpunkt-Bearbeitung
- Expliziter Positions-Parameter in `updateCurrentPosition()` und `checkCutPosition()`

## v0.61.1 (2026-02-26)

**Frame Position Sync Fix**

- Fix: Slider/Positions-Label sprangen zum CutOut bei Navigation-Buttons
- Synchronisation zwischen Slider und Navigation-Buttons korrigiert

## v0.61.0 (2026-02-25)

**H.264 Smart Cut Inter-Segment Stutter Fix**

- `forced-idr=1` + `AV_FRAME_FLAG_KEY` im Encoder: IDR statt Non-IDR I-Frame
- First-Segment Override: Seg 0 = pure Stream-Copy (Decoder startet leer)
- SPS-Inline-Patching mit `max_num_reorder_frames`
- EOS NAL Typ 10->11 (H.264), Typ 36->37 (H.265)
- `computeReorderDelay()` verbessert (20 GOPs)
- MKV Duration Fix
- Separate Preview-Encoder-Preset-Einstellung (Standard: ultrafast)

## v0.60.0 (2026-02-21)

**CLI-to-Library Migration**

- Entfernt: 1.882 Zeilen Dead Code aus ttffmpegwrapper.cpp
- Audio-Cutting: ffmpeg CLI -> libav stream-copy API (verlustfrei)
- MKV-Muxing: mkvmerge CLI -> libav matroska muxer (Container-Remux + ES-Mux)
- Playback-MKV: QProcess/mkvmerge -> TTMkvMergeProvider
- macOS Support-Code entfernt (Linux-only)
- mplex ist das einzige verbleibende externe CLI-Tool

## v0.59.0 (2026-02-21)

**Audio Boundary Burst Detection**

- Burst-Icon + Text in Cut-Liste bei Audio-Bursts an Schnittpunkten
- Preview: Burst-Warnung + "Shift -1 Frame" Button
- Warndialog fuer verbleibende Bursts beim finalen Schnitt
- burstThresholdDb Einstellung (konfigurierbar)
- Audio-Burst-Erkennung via libav (vorher ffmpeg CLI)

## v0.58.0 (2026-02-19)

**Non-IDR I-Frame Fix + Audio Quality**

- `analyzeCutPoints()` erkennt Non-IDR I-Frames -> `needsReencodeAtStart = true`
- EOS NAL Upgrade: H.264 Typ 10->11, H.265 Typ 36->37
- `writeParameterSets()` nach EOS NAL und vor Stream-Copy
- SSIM-Verbesserung: 0.761 -> 0.995
- Audio: Click False Positive Fix (-80dBFS Silence Floor)
- Audio: Duration Mismatch 122ms->6ms (Off-by-One Fix)
- H.264 Smart Cut Display-Order Mapping und Interlace-Support
- A/V Sync Offset Vorzeichen-Konvention korrigiert
- ttcut-demux: Bitrate-Autoerkennung via ffprobe

## v0.57.0 (2026-02-15)

**HEVC B-Frame Detection + Audio Fixes**

- Fix: ttffmpegwrapper.cpp hardcoded alle Non-Keyframes als P-Frames
- Echte `slice_type` Erkennung aus HEVC Slice Header Bitstream
- Fix: Cut-List Navigation und Cut-In/Out Editing
- Fix: Cut-Edit Navigation und First-Click Frame Display

## v0.56.0 (2026-02-14)

**AC3 Header Repair + .info Languages**

- Fix: `ttcut-ac3fix` Heuristik ">=384kbps + stereo = 5.1" war falsch
- Decode-Test vor Reparatur: ffmpeg Validierung
- Audio-Sprachen aus `.info`-Datei laden
- Fix: Verlustfreies Audio-Cutting via libav stream-copy
- Fix: Thread-Pool Deadlock, AC3 Parser Hang bei E-AC3

## v0.55.0 (2026-02-13)

**Security + Quality Fixes**

- Fix: Kritische Sicherheits- und Qualitaetsprobleme aus Code-Audit

## v0.54.0 (2026-02-13)

**AC3 Repair + Decoder Fixes**

- AC3 Header Repair in ttcut-demux integriert
- Fix: Decoder EOF Drain und Cut-List Spalten-Navigation

## v0.53.0 (2026-02-12)

**Smart Cut Improvements**

- Fix: ttcut-demux A/V-Sync fuer H.264/H.265
- Smart Cut Display-Order Fix, A/V Sync und 10-bit Support
- Smart Cut Quality Test Suite

## v0.52.0 (2026-02-08)

**Initial TTCut-ng Release (git)**

- Erste Git-Version basierend auf TTCut-ng 0.51.0
- Projektstruktur bereinigt, Build-Artefakte entfernt
- Deutsche Uebersetzungen erweitert (de_DE)
- Multi-Stream Audio/Subtitle Support mit Sprachcodes

## v0.51.0

**H.264/H.265 Playback + UI**

- H.264/H.265 Video-Wiedergabe mit A/V-Sync (via mpv + temporaere MKV)
- Interlace-Erkennung fuer MPEG-2 Re-Encoding
- Deutsche Uebersetzungen fuer zentrale UI-Elemente (de_DE)
- Video-Editing Farbschema (I=blau, P=gruen, B=orange)
- Theme-Icons via QIcon::fromTheme() mit Fallback
- ttcut-demux Multi-Core Optimierung (parallele Extraktion)
- Frame-Position wird beim Videowechsel beibehalten
- Encoder-Modus standardmaessig aktiviert
- Fix: Frame-Suche mit korrekter Referenz und HD-skaliertem Schwellwert
- Fix: Preview Prev/Next Button-Reihenfolge (waren vertauscht)
- Fix: Uebersetzungsdateien fuer installierte Pakete

## v0.50.3

- Fix: MPEG-2 Schnitt bei kurzen Segmenten ohne I-Frames
- Fix: build-package.sh Git-Info in Version

## v0.50.2

- Play-Button im Current-Frame Widget (mpv mit Audio)
- Previous/Next Cut Navigation im Preview-Dialog
- A/V-Sync Offset fuer demuxte Streams (.info)
- ttcut-demux: Timestamp-Reparatur (ffmpeg +genpts+igndts)
- Vim-Style Tastaturkuerzel (j/k/g/G/[/])
- Tastaturkuerzel-Hilfe (Help-Menue)
- Benutzer-Warnungen (ungesicherte Aenderungen, A/V-Laengendifferenz)
- Elementary Streams als Pflichtformat (kein automatisches Container-Demuxing)
- Fix: MPEG-2 Decoder Crash beim Rueckwaerts-Scrollen nahe Videostart
- Fix: Cut-Liste weisser Hintergrund bei Dark Themes
- Fix: Veraltete Preview-Dateien zeigten falsches Video
- Fix: Container-Erkennungsreihenfolge (mpegvideo vs. mpeg)
- Fix: Doppelte ES-Datei-Loeschung im Mplex-Provider

## v0.50.1

- VDR markad Marks-Datei Support in ttcut-demux
- VDR Marker Integration (Auto-Load aus .info-Dateien)
- Marker in Cut-Tab und Marker-Tab sichtbar
- Fix: Audio-Dateiname-Zaehler in ttcut-demux

## v0.50.0

**H.264/H.265 Smart Cut + ttcut-demux**

- H.264/H.265 framegenauer Smart Cut (TTESSmartCut)
  - Nativer NAL-Unit-Parser (TTNaluParser) mit mmap I/O
  - Re-Encode partieller GOPs an Schnittpunkten (~0,5% der Frames)
  - Stream-Copy vollstaendiger GOPs (kein Qualitaetsverlust, ~99,5%)
  - Audio-Cut via libav stream-copy
  - MKV-Ausgabe mit Kapitelmarken
- ttcut-demux: TS-Demuxer fuer H.264/H.265
  - Elementary-Stream-Output mit .info Metadaten
  - Audio Padding/Trimming fuer A/V-Sync
  - VDR Marks Support
  - Filler-NALU-Entfernung
- Encoder-UI-Einstellungen an Encoder angebunden (CRF, Preset, Profil)
- Neues GUI-Layout mit TreeView-Widgets und Multi-Input-Stream-Support
- Batch-Muxing via Mux-Script-Generierung
- Debian-Paketierung und Desktop-Integration

## v0.40.0

- UI-Umstrukturierung: Cut-Liste 3-Spalten-Layout, Copy-Button, Tabs umbenannt
- Separater Subtitles-Tab im Hauptfenster
- Fix: Stream-Navigator zeigte keine Cut-Marker
- Fix: FFmpeg Encoding-Parameter
- Fix: Audiooffset-Spalte zeigte GUID statt "0"
- Subtitle-Support im XML-Projektdatei-Format

## v0.30.0

**SRT Subtitle Support**

- SRT-Untertitel-Support mit Preview-Integration (von Minei3oat)
- Auto-Loading: SRT-Dateien passend zum Videonamen werden automatisch geladen
- Preview-Overlay: Untertitel im Videobild via QPainter
- mpv Preview: Untertitel via --sub-file Parameter
- Schnitt: Untertitel werden zusammen mit Video/Audio geschnitten
- Migration von QGLWidget zu QImage/QPixmap (volle Wayland-Kompatibilitaet)
- Migration von transcode zu libavcodec API fuer Frame-Encoding
- Migration von mplayer zu mpv fuer Preview

---

## Legacy Releases (TTCut original, B. Altendorf 2005-2008)

### v0.20.4

- Release Candidate fuer 0.21.0
- Thread-Support und Thread-Abort
- Kleinere Bugfixes

### v0.20.3

- Thread-Support und Multi-Thread Progress-Dialog
- Native Style unter OS X
- Phonon fuer Previews (Linux und OS X)
- Multiple Input-Video Grundfunktion
- Cut-Out/Current-Frame mit QImage (kein OpenGL mehr noetig)
- Marker-Positionen pro Video, gespeichert im Projektfile

### v0.20.2

- Qt >= 4.4.3 erforderlich
- Phonon-Libs erforderlich
- AVData-Controller fuer Multiple-Video-Support

### v0.20.1

- Stabil unter OS X und Linux
- Umfangreiches Refactoring
- Unit-Tests und Valgrind-Pruefungen
- Fix: Memory Leaks und Buffer Overflows
- Neues XML-Projektdatei-Format

### v0.20.0

- OS X Support
- Neuer TTFileBuffer (Qt-basiert)
- Video-Stream-Analyse beschleunigt
- Speicherverbrauch reduziert

### v0.19.6

- Fix: Layout unter Qt 4.3.x
- Speicherverbrauch reduziert
- IDD-Leseleistung verbessert

### v0.19.5

- Fix: Audio/Video asynchron nach mehreren Schnitten
- Fix: Projektdatei-Extension anhaengen

### v0.19.4

- Fix: Ressourcen-Freigabe nach neuem Video/Projekt
- Fix: Crash bei langem Video nach kurzem
- Fix: Segfault bei Preview/Cut mit Encoder

### v0.19.3

- Elementary Cut-Stream nach Mplex loeschen
- Alle Audio-Dateien zu Video automatisch lesen
- Audio-Only-Cut implementiert
- Fix: Crash bei Video ohne Audio

### v0.19.2

- Fix: Deutsche Umlaut-Behandlung
- Progressbar fuer AC3/DTS Audio-Headerlist
- User-Abort waehrend Video-Cut
- Fix: Percent/Progress bei grossen Dateien

### v0.19.1

- Qt3-Support entfernt
- Fix: Aspect-Ratio-Wechsel (4:3 -> 16:9) bei Closed GOPs
- Direktes Mplexing nach Cut
- Mplex-Optionen im Settings-Dialog

### v0.11.2 - v0.11.4

- Qt3-Support vollstaendig entfernt
- GUI in .ui-Dateien
- Stream-Navigator fuer Cut-Visualisierung
- Fix: Cut-Liste bei neuer Cut-Range
