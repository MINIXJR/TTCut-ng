# H.264/H.265 Smart-Cut Tracking

> **ARCHIV:** Dieses Dokument ist ein historisches Entwicklungstagebuch (Januar 2026).
> Es dokumentiert die Entwicklung des H.264/H.265 Smart Cut Features und alle
> getesteten Ansätze (A-Z13). Die finale Implementierung basiert auf dem
> "ES Smart Cut V2"-Ansatz (TTNaluParser + TTESSmartCut), der am Ende beschrieben wird.
> Für die aktuelle Dokumentation siehe `CLAUDE.md` und `ES_SMART_CUT_DESIGN.md`.

## Ziel
Frame-genaues Schneiden von H.264/H.265 Videos mit Smart-Rendering (nur Grenzen re-encoden, Mitte stream-copy).

---

## ARCHITEKTUR-PROBLEM (2026-01-25)

### Aktueller Zustand (FALSCH)

Während der Smart-Cut Experimente wurde TTCut-ng auf **Container-Ebene** umgebaut:

```
MPEG-2 (korrekt):          H.264/H.265 (FALSCH):

Container                   Container
    │                           │
    ▼                           ▼
  Demux                    TTCut-ng arbeitet
    │                      DIREKT auf Container!
    ▼                           │
Elementary Streams              ▼
    │                      avcut-Ansatz
    ▼                      (smartCut in ttffmpegwrapper.cpp)
TTCut-ng                        │
    │                           ▼
    ▼                      Container
   Mux
    │
    ▼
Container
```

**Probleme mit Container-Ansatz:**
- Kein einheitlicher Workflow
- h264bitstream wird nicht genutzt
- Analyse/Reparatur nicht möglich
- Stutter an Schnittpunkten (ungelöstes Problem)

### Geplanter Zustand (RICHTIG)

**Einheitlicher Workflow für ALLE Codecs:**

```
Container (TS/MKV/MP4)
        │
        ▼
┌───────────────────────────────────────────────┐
│  1. DEMUX (ts_demux_normalize.sh)             │
│     - ffmpeg/mkvextract                       │
│     - Trennung: Video + Audio + Subtitles     │
└───────────────────────────────────────────────┘
        │
        ├── video.m2v / video.264 / video.265  (Elementary Stream)
        ├── audio_0.mp2 / audio_0.ac3
        ├── audio_1.ac3
        └── subtitles.srt
        │
        ▼
┌───────────────────────────────────────────────┐
│  2. ANALYZE + REPAIR (h264bitstream)          │
│     - Nur für H.264/H.265                     │
│     - Filler NALUs entfernen                  │
│     - MMCO/RPLR Fehler erkennen               │
│     - Recovery Point SEI einfügen (optional)  │
└───────────────────────────────────────────────┘
        │
        ▼
┌───────────────────────────────────────────────┐
│  3. TTCUT-NG                                  │
│     - Lädt Elementary Streams                 │
│     - Frame-Navigation                        │
│     - Schnittmarken setzen                    │
│     - Schneiden (mit Re-Encoding an Grenzen)  │
└───────────────────────────────────────────────┘
        │
        ▼
┌───────────────────────────────────────────────┐
│  4. MUX                                       │
│     - mkvmerge / ffmpeg                       │
│     - Video + Audio + Subtitles               │
│     - Chapter-Marker                          │
└───────────────────────────────────────────────┘
        │
        ▼
Container (MKV/TS)
```

---

## IMPLEMENTIERUNGSPLAN

### Phase 1: Demux-Script erweitern

**Datei:** `tools/ts_demux_normalize.sh`

**Aktuell:**
- Gibt MKV aus (Video + Audio zusammen)
- Nutzt mkvmerge --fix-bitstream-timing-information

**Änderungen:** ✅ ABGESCHLOSSEN (2026-01-25)
- [x] Option für Elementary Stream Output (`-e` Flag)
- [x] Video als .264/.265 extrahieren (nicht MKV)
- [x] Audio als separate .mp2/.ac3/.aac Dateien
- [x] Info-Datei mit Frame-Rate, Codec-Details, Filler-Statistik

**Beispiel:**
```bash
# Aktuell:
ts_demux_normalize.sh input.ts output_dir/
# → output_dir/input_normalized.mkv

# Mit -e (Elementary Stream):
ts_demux_normalize.sh -e input.ts output_dir/
# → output_dir/input_video.264
# → output_dir/input_audio_deu.ac3
# → output_dir/input_audio_eng.ac3
# → output_dir/input.info  (Metadaten)

# Mit -ep (ES + Filler-Stripping):
ts_demux_normalize.sh -ep input.ts output_dir/
# → Wie oben, aber Filler-NALUs werden entfernt
```

### Phase 2: h264bitstream Integration ✅ ABGESCHLOSSEN (2026-01-25)

**Integration in:** `tools/ts_demux_normalize.sh` (Option `-p`)

**Workflow:**
```bash
# Demux + Filler entfernen in einem Schritt:
ts_demux_normalize.sh -ep input.ts output_dir/
# → Filler-NALUs automatisch entfernt
# → MMCO-Fehler erkannt (in Analyse-Output)
# → Statistik in .info Datei gespeichert
```

**Implementierungsdetails:**
- Ruft h264_dpb_analyze (H.264) oder h265_analyze (H.265) auf
- Filler-NALUs werden mit `-f` Option entfernt
- Ersparnis typisch: 0.1% (DVB HD) bis 8% (DVB SD)
- .info Datei enthält `filler_stripped=true/false` und `filler_saved_bytes=N`

**Optional (Zukunft):**
- Recovery Point SEI einfügen an I-Frames
- Broken Link signalisieren

### Phase 3: TTCut-ng auf Elementary Streams (WIP)

**Dateien:**
- `avstream/tth264videostream.cpp`
- `avstream/tth265videostream.cpp`
- `extern/ttffmpegwrapper.cpp`
- `avstream/ttesinfo.h` / `.cpp` (NEU)

**Änderungen:**
- [x] TTESInfo-Klasse zum Parsen von .info-Dateien (2026-01-25)
- [x] ES-Erkennung in buildFrameIndex() (PTS/DTS = AV_NOPTS_VALUE)
- [x] Timestamp-Berechnung aus Frame-Rate für ES-Dateien
- [x] Frame-Rate aus .info-Datei laden wenn verfügbar
- [x] cutElementaryStream() für Byte-Level Video-ES Schnitt (2026-01-25)
- [x] smartCut() erkennt automatisch ES und verwendet cutElementaryStream()

**Aktueller Stand (2026-01-25): VOLLSTÄNDIG**

TTCut-ng unterstützt jetzt den vollständigen ES-Workflow:
1. ES-Datei wird erkannt (Format "h264" oder "hevc")
2. .info-Datei wird automatisch gesucht
3. Frame-Rate wird aus .info-Datei gelesen (korrigiert FFmpegs fehlerhafte Erkennung)
4. PTS/DTS werden aus Frame-Index berechnet
5. Byte-Level ES-Schnitt vermeidet Timestamp-Probleme

**Vorteil des ES-Ansatzes:**
- Keine PTS/DTS Diskontinuitäten (ES hat keine Timestamps!)
- Saubere Timestamps werden erst beim Muxen generiert
- Vermeidet das Stutter-Problem an Schnittpunkten

### Phase 4: Audio, Untertitel und Muxing

**Status:** IMPLEMENTIERT (2026-01-25)

**Neue Funktionen in ttffmpegwrapper.cpp:**
- `cutAudioStream()` - Zeit-basiertes Audio-ES Schneiden mit FFmpeg
- `cutSrtSubtitle()` - Text-basiertes SRT-Schneiden mit Zeit-Offset
- `cutAndMuxElementaryStreams()` - Kompletter Workflow: Video ES + Audio ES + SRT → MKV

**Workflow:**
```bash
# 1. Video ES schneiden (Byte-Level)
cutElementaryStream(video.264, cut_video.264, cutList)

# 2. Audio ES schneiden (FFmpeg concat)
cutAudioStream(audio.ac3, cut_audio.ac3, cutList)

# 3. SRT schneiden (Text-basiert)
cutSrtSubtitle(subs.srt, cut_subs.srt, cutList)

# 4. Alles zusammen muxen
mkvmerge -o output.mkv \
  --default-duration 0:50fps cut_video.264 \
  cut_audio.ac3 \
  cut_subs.srt
```

**UI-Änderung:**
- Datei-Dialog zeigt nur noch Elementary Stream Formate
- Entfernt: .ts, .mkv, .mp4, .m2ts, .m4v
- Hinzugefügt: .264, .h264, .265, .h265, .hevc

---

## OFFENE FRAGEN

1. **PTS in Elementary Streams:**
   - H.264 ES enthält keine PTS (nur in Container)
   - ✅ GELÖST: Frame-Counter × Frame-Duration (berechnet aus .info-Datei)
   - ES-Ansatz vermeidet das Problem: Timestamps erst beim Muxen

2. **Audio-Sync:**
   - Audio-ES hat eigene Timestamps
   - ✅ GELÖST: Zeit-basiertes Schneiden mit cutAudioStream()
   - mkvmerge erstellt sauberen Sync beim Muxen

3. **Re-Encoding an Grenzen:**
   - ⚠️ IN ARBEIT: Smart-Cut für ES-Dateien
   - ES-Dateien müssen zuerst in Container gewrappt werden (für Seeking)
   - Dann: Re-encode nur an Grenzen, Stream-Copy für Mitte

---

## Getestete Ansätze

### 1. Separates Video/Audio Schneiden + Mux
- **Status:** FEHLGESCHLAGEN
- **Problem:** Audio 400ms zu spät
- **Ursache:** `-ss` vor `-i` seeked Video zu Keyframe, Audio zu Audio-Frame-Grenze (unterschiedliche Positionen)

### 2. Video+Audio zusammen schneiden (aktueller Stand)
- **Status:** TEILWEISE ERFOLGREICH
- **Ergebnis:** A/V Sync OK, aber Video stockt an Schnittpunkten
- **Ursache:** Codec-Konfiguration (SPS/PPS) unterschiedlich zwischen libx264 und Original-Encoder

### 3. Encoder-Optionen `-g 1 -bf 0` (I-Frame only)
- **Status:** FEHLGESCHLAGEN
- **Problem:** Video stockt an Übergängen re-encode↔stream-copy

### 4. Encoder-Optionen ohne `-g 1 -bf 0` (natürliche GOP)
- **Status:** FEHLGESCHLAGEN
- **Problem:** Gleiche Symptome wie #3

## Kernproblem (noch ungelöst)

mkvmerge warnt:
> "Die privaten Codec-Konfigurationsdaten stimmen nicht überein"

Das passiert bei:
- part_0 (re-encode) → part_1 (stream-copy)
- part_2 (stream-copy) → part_3 (re-encode)

**Ursache:** libx264 erzeugt andere SPS/PPS als der Original-Encoder.

## Noch nicht getestete Ansätze

### A. ~~Alles re-encoden~~ NICHT AKZEPTABEL
- **VERWORFEN**: Widerspricht dem Ziel von Smart-Cut
- Smart-Cut bedeutet: Nur Grenzen re-encoden, Mitte stream-copy

### B. SPS/PPS vom Original extrahieren und für libx264 verwenden
- Komplex, möglicherweise nicht möglich

### C. ffmpeg concat filter statt mkvmerge
- Re-encodet alles, aber möglicherweise bessere Übergänge

### D. Nur an Keyframes schneiden (kein Re-Encoding)
- Einfachste Lösung, aber nicht frame-genau

### E. Original-Encoder-Parameter auslesen und nachahmen
- ffprobe kann SPS/PPS analysieren
- libx264 entsprechend konfigurieren

## Aktueller Code-Stand

**Datei:** `/usr/local/src/TTCut-ng/data/ttavdata.cpp`
**Funktion:** `doH264Cut()` (ab Zeile ~1007)

**Aktuelle Implementierung:**
```
Segment-Struktur (4 Teile bei einem Schnitt):
- part_0: Pre-boundary (re-encode) - cutIn bis erster Keyframe
- part_1: Middle-start (stream-copy) - erster Keyframe bis letzter Keyframe vor cutOut
- part_2: (nicht immer vorhanden)
- part_3: Post-boundary (re-encode) - letzter Keyframe bis cutOut

Concatenation: mkvmerge mit "+" Syntax
```

**Encoder-Optionen (aktuell):**
```cpp
-c:v libx264 -g 1 -bf 0 -b:v 8M -maxrate 10M -bufsize 16M -profile:v high -level:v 4.0 -preset slow
```

## Projektziel (bestätigt)

**Frame-genaues Schneiden mit maximalem Stream-Copy und minimalem Re-Encoding.**

- Mittelteil: IMMER stream-copy (Qualität erhalten, schnell)
- Grenzen: NUR die wenigen Frames zwischen Schnittpunkt und Keyframe re-encoden
- Nahtlos verbinden ohne Stocken

## Offene Lösungsansätze (noch nicht getestet)

### B. SPS/PPS vom Original extrahieren und für libx264 verwenden
- Ziel: Re-encodete Frames kompatibel mit Original machen

### C. ffmpeg concat demuxer statt mkvmerge
- Anderer Concat-Mechanismus, behandelt SPS/PPS evtl. anders
- Kein zusätzliches Re-Encoding

### E. Original-Encoder-Parameter auslesen und nachahmen
- ffprobe kann Codec-Details analysieren
- libx264 entsprechend konfigurieren

### F. Einheitlich PTS statt gemischt PTS/DTS
- **Status:** FEHLGESCHLAGEN
- Alle Segmente verwenden PTS
- Alle Segmente verwenden `-ss` nach `-i` (frame-genau)
- **Ergebnis:** Stockt weiterhin

### G. Einheitlich DTS statt PTS
- **Status:** TEILWEISE ERFOLGREICH
- Alle Keyframe-Referenzen auf DTS umgestellt
- **Ergebnis:** Mitte OK, Anfang/Ende stocken noch
- **Erkenntnis:** Problem ist B-Frame Mismatch (re-encode: 0, original: 3)

### H. B-Frames in re-encode aktivieren (-bf 3)
- **Status:** TEILWEISE ERFOLGREICH
- Encoder-Option: `-bf 3` statt `-bf 0`
- **Ergebnis:** has_b_frames=2 (statt 3), Anfang stockt noch, Mitte+Ende OK

### I. ffmpeg concat demuxer statt mkvmerge
- **Status:** FEHLGESCHLAGEN
- Verwendet `-f concat -safe 0 -c copy`
- **Ergebnis:** Non-monotonic DTS Warnungen, Anfang stockt noch

### J. Doppeltes Re-Encoding
- **Status:** FEHLGESCHLAGEN
- Pass 1: ALL-Intra (`-g 1 -bf 0`) für präzisen Schnitt
- Pass 2: Original-kompatibel (`-bf 3`) für nahtlose Übergänge
- **Ergebnis:** has_b_frames immer noch 2 vs 3, Anfang stockt weiterhin

### K. Erweiterte GOP-Recodierung
- **Status:** FEHLGESCHLAGEN
- Re-encode nicht nur die Grenz-Frames, sondern auch 1+ zusätzliche GOPs
- Struktur:
  - Pre-boundary: cutIn → ZWEITER Keyframe (statt erster)
  - Middle: zweiter Keyframe → vorletzter Keyframe (stream-copy, kleiner)
  - Post-boundary: VORLETZTER Keyframe → cutOut (statt letzter)
- **Ergebnis:** Stockt weiterhin an Übergängen

### L. Zeit-basierte erweiterte Grenzen (2 Sekunden)
- **Status:** FEHLGESCHLAGEN
- Re-encode 2 Sekunden über Keyframe-Grenze hinaus
- **Ergebnis:** Video nicht abspielbar, hängt am Anfang/Mitte
- **Problem:** Mehr Übergänge (6 Segmente), jeder Übergang verursacht Probleme

### M. TsMuxeR-vorverarbeitete Datei
- **Status:** FEHLGESCHLAGEN
- Test: `/media/Daten/Video_Tmp/ProjectX_Temp/Test/Petrocelli_TsMuxeR_GUI.ts`
- **Ergebnis:** Gleiche SPS/PPS wie Original, kein Unterschied

### N. h264_metadata Bitstream Filter
- **Status:** FEHLGESCHLAGEN
- `ffmpeg -bsf:v "h264_metadata=level=4.0"`
- **Ergebnis:** Ändert extradata_size, aber SPS/PPS bleibt inkompatibel

### O. dump_extra Bitstream Filter
- **Status:** FEHLGESCHLAGEN
- Fügt SPS/PPS zu jedem Keyframe hinzu
- **Ergebnis:** mkvmerge-Warnung bleibt, kein Unterschied beim Playback

### P. ffmpeg concat demuxer statt mkvmerge
- **Status:** FEHLGESCHLAGEN
- Auto-Insert h264_mp4toannexb, Non-monotonic DTS Warnung
- **Ergebnis:** Kein Unterschied beim Playback

### Q. MPEG-TS Container + Binary Concat
- **Status:** FEHLGESCHLAGEN
- Segmente als .ts, dann `cat` zum Zusammenfügen
- **Ergebnis:** "Packet corrupt", "timestamp discontinuity"

### R. x264 --stitchable Option
- **Status:** FEHLGESCHLAGEN
- Speziell für nahtloses Zusammenfügen gedacht
- **Ergebnis:** SPS/PPS-Warnung bleibt, kein Unterschied

## Analyse: avcut-Ansatz (Lösung gefunden!)

**Quelle:** `/usr/local/src/avcut/avcut.c`

**Schlüsselerkenntnisse:**

1. **Keine separaten Segmente** - schreibt direkt in EINE Ausgabedatei
2. **Kein Global Header** - SPS/PPS wird IN-STREAM geschrieben (bei jedem Keyframe)
   ```c
   // we do not want a global header, we need the data "in stream"
   if (0)  // DEAKTIVIERT!
       enc_cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
   ```
3. **GOP-basierte Verarbeitung:**
   - `BUF_COPY_COMPLETE`: Ganzer GOP wird stream-copied
   - `BUF_CUT_IN_BETWEEN`: Cut liegt im GOP → re-encoden
   - `BUF_DROP_COMPLETE`: Ganzer GOP wird verworfen
4. **Encoder-Restart** nach jedem Encoding-Block
5. **dump_extra BSF** für Keyframes beim Encoding

---

## avcut-Ansatz IMPLEMENTIERT ✓

**Implementierung:** `extern/ttffmpegwrapper.cpp` → `smartCut()` Methode
**Commit:** a7f6031

### Funktionsweise

1. Video bleibt im Container-Format (MKV, TS, MP4) - kein Demuxen
2. GOP-Analyse bestimmt Behandlung jedes GOPs:
   - **Mode 0**: Drop (GOP wird verworfen)
   - **Mode 1**: Full Copy (GOP wird stream-kopiert)
   - **Mode 2**: Partial (GOP muss encodiert werden)
3. Audio wird stream-kopiert mit Timestamp-Anpassung
4. Ausgabe als MKV via mkvmerge

### Encoder-Einstellungen

```cpp
av_opt_set(encoder->priv_data, "preset", "fast", 0);
av_opt_set(encoder->priv_data, "crf", "18", 0);
encoder->max_b_frames = 0;  // Vermeidet B-Frame-Komplexität
```

### Timestamp-Verarbeitung

```cpp
// PTS adjustment
outPkt->pts = srcPkt->pts - firstKeptPts - droppedVideoDuration;

// Sequential DTS assignment
outPkt->dts = lastVideoDts;
lastVideoDts += frameDurationInStreamTB;
```

### Was funktioniert ✓

- [x] Container-Format-Erkennung (MKV, TS, MP4, Elementary)
- [x] GOP-Analyse und Frame-Index-Aufbau
- [x] Video-Anzeige via libav-Dekodierung
- [x] Multi-Segment-Schneiden (Anfang + Mitte + Ende)
- [x] Audio stream-copy mit Timestamp-Anpassung
- [x] Ausgabe als MKV via mkvmerge
- [x] Korrekte Gesamtdauer des geschnittenen Videos
- [x] Anfang und Ende spielen sauber ab

---

## Bekannte Limitierung ⚠️

**Kleiner Ruckler (~0.14 Sekunden / ~7 Frames) an MITTLEREN Schnittpunkten**

### Ursache

B-Frame-Reordering verursacht Timestamp-Diskontinuität beim Übergang von encodierten zu stream-kopierten Sektionen:

- Encodierte Frames: `max_b_frames=0`, daher PTS=DTS (linear)
- Stream-kopierte Frames: Enthalten B-Frames, daher PTS ≠ DTS (komplex)

Der Übergang zwischen diesen beiden Timestamp-Schemata verursacht den Ruckler.

### Versuchte Lösungsansätze (Session 2026-01-21)

| Ansatz | Ergebnis |
|--------|----------|
| Gap-Filling mit duplizierten Frames | Artefakte im gesamten Video |
| droppedVideoDuration-Anpassung | Keine Verbesserung |
| DTS-Alignment (lastVideoDts vorspringen) | Artefakte im gesamten Video |
| Original DTS-Beziehung beibehalten | Artefakte an Schnittstellen |
| DTS = AV_NOPTS_VALUE (Muxer generiert) | Schlimmerer Ruckler |
| mkvmerge --fix-bitstream-timing-information | Keine Verbesserung |
| ffmpeg +genpts+igndts Post-Processing | Keine Verbesserung |

### Mögliche zukünftige Ansätze

1. **Nur an GOP-Grenzen schneiden** - Verliert einige Frames, aber saubere Übergänge
2. **Größere Sektionen um Schnittpunkte neu encodieren** - Mehr Qualitätsverlust, aber saubere Timestamps
3. **LosslessCut-Ansatz implementieren** - Siehe Analyse unten
4. **B-Frame-Delay kompensieren** - Exakt berechnen wie viele Frames Verzögerung durch B-Frames entstehen

---

## LosslessCut Analyse (2026-01-21)

**Quelle:** `/usr/local/src/lossless-cut/src/renderer/src/`

### Architektur-Unterschied zu TTCut-ng

| Aspekt | LosslessCut | TTCut-ng (aktuell) |
|--------|-------------|-------------------|
| Segmente | Separate Dateien | Direktes Schreiben |
| Concat | FFmpeg concat demuxer | Mkvmerge |
| GOP-Kontrolle | Keine (`-g` nicht gesetzt) | `max_b_frames=0` |
| Timeline | Reset mit `-ss 0` nach Input | PTS-Offset-Berechnung |

### LosslessCut Smart Cut Workflow

1. **Keyframe-Erkennung** (10s Fenster, erweitert auf 60s wenn nötig)
2. **Zwei Dateien erstellen:**
   - Encoded: `desiredCutFrom` → `losslessCutFrom - frameDuration`
   - Copied: `losslessCutFrom` → `cutTo`
3. **Concat mit FFmpeg demuxer** (nicht filter, nicht mkvmerge)

### Kritische FFmpeg-Optionen

```bash
# Encoding (Boundary)
ffmpeg -ss <cutFrom> -i <input> -ss 0 -t <duration> \
       -c:v <codec> -b:v <bitrate*1.2> -c:a copy <encoded.mp4>

# Concat
ffmpeg -f concat -safe 0 -protocol_whitelist file,pipe,fd \
       -i <list.txt> -map 0 -c copy <output>
```

**Wichtig:**
- **Zwei `-ss` Parameter**: Erst vor `-i` (fast seek), dann `-ss 0` nach `-i` (Timeline-Reset!)
- **Kein `-g` oder `-bf`**: FFmpeg entscheidet GOP-Struktur selbst
- **Bitrate + 20%**: Qualitätserhalt durch höhere Bitrate
- **Boundary Safety**: `keyframe - 1 frame` verhindert Duplikate

### Potentielle Lösung für TTCut-ng

Statt direktem Schreiben in eine Datei:
1. Temporäre Segmentdateien erstellen (wie LosslessCut)
2. FFmpeg concat demuxer für Zusammenfügen verwenden
3. Timeline-Reset (`-ss 0` nach `-i`) für saubere Timestamps
4. Kein `max_b_frames=0` - FFmpeg natürliche GOP-Struktur erlauben

### LosslessCut User-Erfahrungen (Web-Recherche)

**Smart Cut ist experimentell** - auch bei LosslessCut mit gemischten Ergebnissen:

| Codec/Format | Status |
|--------------|--------|
| **H.264/MP4** | Am besten, meistens zuverlässig |
| **VP9/WebM** | Gute Kompatibilität |
| **H.265/HEVC** | Problematisch - häufige Fehler, Artefakte, schwarzes Video |
| **MKV Container** | Artefakte und Wiedergabeprobleme |

**Bekannte Probleme bei LosslessCut:**
- Audio-Stutter (10-50ms) an Schnittpunkten
- Audio-Stretching bei H264+AAC
- Non-IDR Keyframes bei H.265 → 2 Sekunden Flackern
- MKV: Artefakte am Anfang bis zum nächsten Keyframe
- Untertitel-Desynchronisation

**Wichtige Erkenntnis:** Selbst LosslessCut hat keine perfekte Lösung!
- Smart Cut ist "inherently experimental and codec-dependent"
- H.264 funktioniert besser als H.265
- MP4 Container funktioniert besser als MKV
- Qualitätsverlust an Schnittpunkten ist "by design"

**Quellen:**
- GitHub Issue #126 (Smart Cut Implementation)
- GitHub Discussion #1633 (x265 Problems)
- GitHub Discussion #1292 (MKV Issues)

---

## Schlüssel-Dateien

| Datei | Beschreibung |
|-------|--------------|
| `extern/ttffmpegwrapper.cpp` | `smartCut()` Methode, Haupt-Implementierung |
| `extern/ttffmpegwrapper.h` | Strukturen: TTStreamInfo, TTFrameInfo, TTGOPInfo |
| `extern/ttmkvmergeprovider.cpp` | MKV-Muxing via mkvmerge |
| `data/ttavdata.cpp` | Cut-Workflow-Integration |

---

## Status-Zusammenfassung

| Feature | Status |
|---------|--------|
| H.264 Smart Cut | ✓ Funktioniert (mit Limitierung) |
| H.265 Smart Cut | ✓ Funktioniert (mit Limitierung) |
| Anfang/Ende sauber | ✓ OK |
| Mittlere Übergänge | ⚠️ Kleiner Ruckler (~0.14s) |

---

## Externe Tool-Analyse (2026-01-24)

### smartcut (Python/PyAV) - `/usr/local/src/smartcut`

**Architektur:**
- Python mit PyAV (FFmpeg-Bindings)
- GOP-basierte Verarbeitung wie TTCut-ng
- Arbeitet auf Frame-Ebene (nicht nur Paket-Ebene)

**Interessante Techniken:**

| Technik | Beschreibung | Für TTCut-ng relevant? |
|---------|--------------|------------------------|
| **Hybrid CRA Re-encoding** | Bei H.265 CRA-GOPs mit RASL: nur führende Bilder re-encoden, Rest stream-copy | Hoch (H.265) |
| **Heap-basierte PTS-Sortierung** | Min-Heap für korrekte Frame-Ausgabe trotz B-Frame-Reordering | Mittel |
| **Decoder-Priming** | Decoder mit vorherigem GOP primen für RASL-Referenzen | Mittel |
| **Kontinuierliches Decoding** | Decoder NICHT flushen zwischen benachbarten GOPs | Mittel |
| **Strenge Timestamp-Validierung** | DTS monoton steigend, PTS >= DTS, Garbage-Filter | Bereits vorhanden |

**Schlüssel-Code:**
```python
# Decoder-Priming für RASL
decoder_priming_dts = gop_start_times_dts[gop_index - 1]

# Heap für PTS-Sortierung
for frame in decoder.decode(packet):
    heapq.heappush(heap, (frame.pts, frame))
```

**Erkenntnis:** smartcut akzeptiert Stutter auch als unvermeidbar.

---

### VidCutter (Python/FFmpeg CLI) - `/usr/local/src/vidcutter`

**Architektur:**
- Python mit FFmpeg CLI (kein libav direkt)
- Drei-Segment-Strategie: Start (encode) + Mitte (copy) + Ende (encode)
- MPEG-TS Concatenation für Zusammenfügen

**Interessante Techniken:**

| Technik | Beschreibung | Für TTCut-ng relevant? |
|---------|--------------|------------------------|
| **Closed GOP Flag** | `-flags +cgop` für unabhängige GOPs | **ZU TESTEN** |
| **MPEG-TS Concat** | `concat:file1.ts\|file2.ts` statt mkvmerge | Evtl. zu testen |
| **Ultrafast Preset** | Minimiert B-Frames automatisch | Bereits ähnlich |
| **avoid_negative_ts** | Automatische Timestamp-Normalisierung | Bereits via Script |

**Encoder-Optionen:**
```bash
# H.264
libx264 -tune film -preset ultrafast -x264-params crf=23 -qp 0 -flags +cgop

# H.265
libx265 -tune zerolatency -preset ultrafast -x265-params crf=23 -qp 4 -flags +cgop
```

**Erkenntnis:** VidCutter macht KEINE manuelle PTS/DTS-Manipulation - verlässt sich auf FFmpeg.

---

### Vergleich der Ansätze

| Aspekt | TTCut-ng | smartcut | VidCutter |
|--------|----------|----------|-----------|
| Sprache | C++ (libav) | Python (PyAV) | Python (FFmpeg CLI) |
| GOP-Analyse | Ja | Ja | Ja (ffprobe) |
| B-Frame-Kontrolle | `max_b_frames=0` | Keine | `+cgop` Flag |
| PTS/DTS | Manuell | Manuell + Heap | FFmpeg auto |
| Concat | mkvmerge | Direkt muxen | MPEG-TS concat |
| Stutter-Problem | Ja (~0.14s) | Ja (akzeptiert) | Ja (akzeptiert) |

---

## Noch zu testende Ansätze

### S. Closed GOP Flag (`AV_CODEC_FLAG_CLOSED_GOP`)
- **Status:** ZU TESTEN
- **Idee:** Closed GOPs haben keine externen Referenzen
- **Code-Änderung:**
  ```cpp
  encCtx->flags |= AV_CODEC_FLAG_CLOSED_GOP;
  ```
- **Erwartung:** Könnte Übergangs-Probleme reduzieren

### T. Hybrid CRA Re-encoding (H.265)
- **Status:** OFFEN
- **Idee:** Bei CRA-GOPs nur RASL/RADL-Bilder re-encoden
- **Komplexität:** Hoch - erfordert NAL-Typ-Erkennung

### U. MPEG-TS Concatenation statt mkvmerge
- **Status:** OFFEN
- **Idee:** Segmente als .ts, dann FFmpeg concat protocol
- **Bereits getestet:** Binary concat (Q) - FEHLGESCHLAGEN
- **Unterschied:** FFmpeg concat protocol vs. cat

### V. Decoder nicht flushen zwischen GOPs
- **Status:** OFFEN
- **Idee:** Referenz-Frame-Buffer erhalten
- **Komplexität:** Mittel - Decoder-State-Management

### X. ~~GOP-Boundary-Only Modus~~ NIEDRIGE PRIORITÄT
- **Status:** OPTIONAL (nur als Fallback)
- **Primäres Ziel bleibt Smart-Cut mit frame-genauem Schneiden**
- GOP-Boundary-Only nur als optionale UI-Einstellung für Nutzer, die Frame-Verlust akzeptieren

### W. MMCO-Fix (extra GOP nach Encode-Sektionen)
- **Status:** FEHLGESCHLAGEN (2026-01-25)
- **Idee:** MMCO-Befehle in kopierten Frames referenzieren originale Referenzbilder, die durch Re-Encoding ersetzt wurden. Lösung: Einen zusätzlichen GOP nach jeder Encode-Sektion ebenfalls re-encoden.
- **Implementierung:** `prevModeWasEncode` Flag tracken, bei ENCODE→COPY Transition den ersten COPY-GOP zu ENCODE forcen
- **Ergebnis:** Stutter DEUTLICH SCHLIMMER als vorher
- **Erkenntnis:** Bestätigt erneut: Mehr Re-Encoding = schlechtere Übergänge (siehe K, L)
- **Commit:** eb4e349 (implementiert), 40a2c9b (reverted)

### Y. ES-Wrapping für Smart-Cut (2026-01-26)
- **Status:** ZU TESTEN
- **Problem:** FFmpeg kann nicht in rohen ES-Dateien suchen (keine Timestamps)
- **Idee:** ES-Datei zuerst in Container wrappen, dann Smart-Cut auf Container
- **Workflow:**
  1. `ffmpeg -r FPS -i video.264 -c copy wrapped.mkv`
  2. Smart-Cut auf wrapped.mkv (re-encode Grenzen, stream-copy Mitte)
  3. Audio separat schneiden und muxen
- **Unterschied zu Q:** Q war Binary-Concat von Segmenten, Y ist Pre-Processing der Eingabe
- **Erwartung:** Ermöglicht Seeking für Stream-Copy-Operationen

---

## VDR Analyse (2026-01-25)

### Quellcode-Analyse: `/usr/local/src/vdr/`

Untersucht wie VDR (Video Disk Recorder) H.264/H.265 Aufnahmen schneidet.

### VDR's Ansatz für verschiedene Codecs

**MPEG-2 (Vtype == 2):**
VDR hat `cMpeg2Fixer` Klasse in `cutter.c` (Zeile 106-218):
- `SetBrokenLink()` - Setzt "broken link" Flag im GOP-Header
- `SetClosedGop()` - Markiert GOP als selbstständig (keine externen Referenzen)
- `AdjTref()` - Passt Temporal Reference an für übersprungene Frames
- `AdjGopTime()` - Korrigiert GOP-Zeitstempel für Kontinuität

**H.264/H.265 (Vtype == 0x1B / 0x24):**
```cpp
// cutter.c Zeile 471-486
// Fix MPEG-2:
if (patPmtParser.Vtype() == 2) {  // <-- NUR für MPEG-2!
   cMpeg2Fixer Mpeg2fixer(Data, Length, patPmtParser.Vpid());
   ...
}
// Für H.264/H.265: KEIN Bitstream-Fix!
```

VDR macht für H.264/H.265 nur:
- ✅ PTS/DTS Offset-Berechnung
- ✅ TS Continuity Counter Anpassung
- ✅ PCR Anpassung
- ✅ "Dangling Packets" entfernen (Pakete mit PTS vor dem Schnittpunkt)
- ❌ **KEIN** Bitstream-Editing

### Schlüssel-Erkenntnis

**VDR schneidet H.264/H.265 NUR an I-Frame-Grenzen!**

```
VDR Ansatz:                    TTCut-ng Ansatz:

[I]--B--B--P--[I]--B--B       [I]--B--B--P--[I]--B--B
       ^                              ^
       Cut-Marker                     Cut-Marker
       |                              |
       v                              v
[I]--B--B--P--[I]...           [I]--B--[RE-ENCODE]--[I]...
  (verliert Frames)              (frame-genau, aber Stutter)
```

### Vergleich VDR vs. TTCut-ng

| Aspekt | VDR | TTCut-ng |
|--------|-----|----------|
| MPEG-2 Cut | Bitstream-Fix (BrokenLink, ClosedGop) | Re-Encoding an Grenzen |
| H.264/H.265 Cut | Nur an I-Frames (kein Re-Encoding) | Frame-genau mit Re-Encoding |
| Frame-Verlust | Ja (bis zu ~0.5s pro Schnitt) | Nein |
| Stutter-Problem | Nein | Ja (~0.14s an mittleren Schnittpunkten) |
| Komplexität | Einfach | Hoch |

### Warum VDR kein Stutter hat

1. **Kein Mischen von re-encoded und stream-copied Content**
   - VDR kopiert immer komplette GOPs
   - Keine Übergänge zwischen verschiedenen Encoder-Konfigurationen

2. **Trade-off: Präzision vs. Qualität**
   - VDR akzeptiert Frame-Verlust für saubere Übergänge
   - TTCut-ng versucht frame-genaues Schneiden, was Stutter verursacht

### Relevanz für TTCut-ng

**Primäres Ziel: Smart-Cut (Frame-Accurate)**
- Re-encode nur an Schnittpunkt-Grenzen
- Stream-copy für alle vollständigen GOPs in der Mitte
- Kein Frame-Verlust, minimaler Qualitätsverlust

**Für ES-Dateien:** Wrapper-Ansatz erforderlich
- ES-Datei zuerst in Container wrappen (MKV/TS)
- Dann Smart-Cut auf dem Container durchführen
- Ermöglicht FFmpeg-Seeking für Stream-Copy

---

## ES Smart Cut Implementierung (2026-01-27)

### Ziel

Frame-genaues Schneiden von Elementary Stream (ES) Dateien mit Smart-Cut:
- Re-encode NUR den partiellen GOP am Cut-In (von Cut-Frame bis nächstem Keyframe)
- Stream-Copy den Rest (von Keyframe bis Cut-Out)
- Kein Frame-Verlust

### Problem mit Byte-Level ES Cutting

Das bisherige `cutElementaryStream()` schneidet NUR an Keyframes:
- User wählt Frame 6000 (P-Frame) als Cut-In
- Output beginnt bei Frame 5979 (vorheriger Keyframe)
- **21 Frames gehen verloren!**

**User-Anforderung:** "Es kommt nur SmartCut infrage! Die GOPs die geschnitten werden können re-encodiert werden, aber kein ganzes Segment."

### Neue Funktion: smartCutElementaryStream()

**Datei:** `extern/ttffmpegwrapper.cpp`

**Workflow pro Segment:**
1. Finde K_before_in (Keyframe vor Cut-In) und K_after_in (Keyframe nach Cut-In)
2. Falls Cut-In != K_before_in: Re-encode von Cut-In bis K_after_in-1
3. Stream-Copy von K_after_in bis Cut-Out
4. Concateniere beide Teile

**Encoder-Parameter (passend zum Original):**
```cpp
QString x264Params = "-profile:v high -level:v 4.0 -refs 4 -bf 3 "
                     "-color_primaries bt709 -color_trc bt709 -colorspace bt709 "
                     "-pix_fmt yuv420p -preset fast -crf 18";
```

### Getestete Concatenation-Ansätze

#### Z1. mkvmerge "+" Append
- **Status:** FEHLGESCHLAGEN
- **Methode:** `mkvmerge -o output.mkv reencode.mkv + streamcopy.mkv`
- **Ergebnis:** Timestamp-Gap (0.180s → 1.000s zwischen Segmenten)
- **Ursache:** Stream-Copy behält Original-Timestamps, mkvmerge shiftet falsch

#### Z2. FFmpeg Concat Demuxer
- **Status:** FEHLGESCHLAGEN
- **Methode:** `ffmpeg -f concat -safe 0 -i list.txt -c copy output.mkv`
- **Ergebnis:** Gleicher Timestamp-Gap wie Z1
- **Ursache:** Stream-Copy-Dateien haben Original-Timestamps, nicht 0-basiert

#### Z3. `-avoid_negative_ts make_zero`
- **Status:** FEHLGESCHLAGEN
- **Methode:** Hinzufügen von `-avoid_negative_ts make_zero` zum Stream-Copy-Command
- **Ergebnis:** Timestamps immer noch nicht bei 0 startend
- **Ursache:** Option wirkt bei `-ss` nach `-i` nicht wie erwartet

#### Z4. `-ss` vor `-i` (Input Seeking)
- **Status:** FEHLGESCHLAGEN
- **Methode:**
  ```bash
  ffmpeg -y -ss TIME -i wrapped.mkv -t DUR -c:v copy output.mkv
  ```
  (Statt `-ss` nach `-i`)
- **Ergebnis:** Seeked zum **VORHERIGEN** Keyframe, verursacht Frame-Überlappung
- **Ursache:** FFmpeg Input-Seeking findet nearest keyframe BEFORE requested time
- **Beispiel:** `-ss 120.2` sucht zum Keyframe bei 120.0s, nicht 121.0s

#### Z5. mkvmerge --split
- **Status:** FEHLGESCHLAGEN
- **Methode:** `mkvmerge -o output.mkv --split timestamps:120.9s wrapped.mkv`
- **Ergebnis:** Split erfolgt am **NÄCHSTEN Keyframe nach** dem Zeitpunkt
- **Ursache:** mkvmerge kann nur an Keyframe-Positionen splitten
- **Erkenntnis:** Nicht geeignet für präzisen Stream-Copy-Start

#### Z6. Erweiterte Re-Encoding (+0.16s / 8 Frames)
- **Status:** FEHLGESCHLAGEN
- **Methode:** Re-encode bis K_after_in + 0.16s (B-frame delay Kompensation)
- **Ergebnis:** Timestamp-Gap weiterhin vorhanden (~0.8s)
- **Ursache:** 0.16s nicht ausreichend für GOP mit 0.64s Intervall

#### Z7. Erweiterte Re-Encoding (+0.7s / 35 Frames = voller GOP)
- **Status:** TEILWEISE ERFOLGREICH
- **Methode:** Re-encode bis K_after_in + 0.7s (bis zum nächsten Keyframe)
- **Ergebnis:** Re-encode endet bei ~0.90s, Stream-Copy beginnt bei ~1.52s
- **Gap:** ~0.62s zwischen Re-encode-Ende und Stream-Copy-Anfang
- **Ursache:** Stream-Copy mit `-c:v copy` beginnt nicht bei 0.00s

#### Z8. `-avoid_negative_ts make_zero`
- **Status:** TEILWEISE ERFOLGREICH
- **Methode:**
  ```bash
  ffmpeg -y -i wrapped.mkv -ss TIME -t DUR -c:v copy -avoid_negative_ts make_zero output.mkv
  ```
- **Ergebnis:**
  - **Ohne Option:** Stream-Copy startet bei PTS 0.72s
  - **Mit Option:** Stream-Copy startet bei PTS 0.20s
- **Verbesserung:** Ja, aber immer noch nicht bei 0.00s
- **Ursache:** B-Frame-Reordering - früheste B-Frames haben PTS vor dem Keyframe-DTS

### Schlüssel-Erkenntnisse (2026-01-27)

#### B-Frame Delay Problem

**GOP-Struktur in H.264 mit B-Frames:**
```
Display Order (PTS): I  B  B  P  B  B  P  B  B  I
Decode Order (DTS):  I  P  B  B  P  B  B  I  B  B
```

**Stream-Copy Verhalten:**
- FFmpeg kopiert ab dem Keyframe (DTS-Reihenfolge)
- B-Frames haben PTS < Keyframe-PTS (frühere Anzeige)
- Daher beginnt Output-PTS nicht bei 0.00s, sondern beim frühesten B-Frame

**Messung im Test-Video:**
```
Keyframe DTS = 0.00s
Früheste B-Frame PTS = -0.08s (relativ zum Keyframe)
Output startet bei PTS = Keyframe-DTS + B-Frame-Offset
```

#### Frame-Rate Korrektur

**WICHTIG:** Test-Video (Petrocelli) hat 50 fps, nicht 25 fps!
- Aus .info-Datei: `frame_rate=50/1`
- MKV Duration: 20000000 ns (= 0.02s = 50 fps)
- Korrektur in ES-Wrapping war erforderlich

#### GOP-Intervall

- Keyframes bei: 120.36s, 121.00s, 121.64s
- GOP-Intervall: ~0.64s (~32 Frames bei 50 fps)
- Re-encode muss 0.7s abdecken um nächsten Keyframe sicher zu erreichen

### Code-Änderungen (2026-01-27)

1. **Neue Funktion `smartCutElementaryStream()`** - Re-encode + Stream-Copy Workflow
2. **`cutAndMuxElementaryStreams()` angepasst** - Nutzt jetzt smartCutElementaryStream()
3. **ES-Wrapping mit korrekter Frame-Rate** - `--default-duration 0:20000000ns` für 50 fps
4. **GOP-Buffer erweitert** - 0.7s (35 Frames) um vollen GOP abzudecken
5. **`-avoid_negative_ts make_zero`** - Reduziert B-Frame-Offset von 0.72s auf 0.20s

### Aktuelle Implementierung (smartCutElementaryStream)

```cpp
// Part 1: Re-encode partial GOP at start
if (needReencodeStart) {
    // Extend re-encode to cover the ENTIRE next GOP (up to 0.7s = 35 frames at 50fps)
    int gopBuffer = qRound(frameRate * 0.7);  // ~35 frames to cover full GOP
    int reencodeEnd = kAfterIn - 1 + gopBuffer;
    reencodeEnd = qMin(reencodeEnd, cutOutFrame);

    double reencStart = cutInFrame / frameRate;
    double reencDur = (reencodeEnd - cutInFrame + 1) / frameRate;

    ffmpegCmd = QString("ffmpeg -y -i \"%1\" -i \"%2\" -ss %3 -t %4 "
                       "-map 0:v -map 1:a -c:v libx264 %5 -c:a copy \"%6\"")
        .arg(wrappedES).arg(audioFile)
        .arg(reencStart).arg(reencDur)
        .arg(x264Params).arg(startPart);
}

// Part 2: Stream-copy the rest (from where re-encode ends)
if (streamCopyStart <= cutOutFrame) {
    double reencEndTime = (kAfterIn - 1 + qRound(frameRate * 0.7) + 1) / frameRate;
    double copyStartTime = reencEndTime;
    double copyEndTime = (cutOutFrame + 1) / frameRate;

    QString videoCmd = QString("ffmpeg -y -i \"%1\" -ss %2 -t %3 -c:v copy -an "
                              "-avoid_negative_ts make_zero \"%4\"")
        .arg(wrappedES)
        .arg(copyStartTime)
        .arg(copyEndTime - copyStartTime)
        .arg(videoOnly);
}
```

### Aktuelle Ausgabe (Z8 - mit 0.2s Offset)

```
Re-encode segment:
  Frame  1: PTS=0.000 (K) - Start of re-encoded section
  ...
  Frame 45: PTS=0.880      - Last re-encoded frame (~0.9s)

Stream-copy segment (PROBLEM):
  Frame  1: PTS=0.200 (B)  - Should be ~0.90s, not 0.20s!
  Frame  2: PTS=0.160 (B)
  Frame  3: PTS=0.240 (K)  - Keyframe
  ...
```

**Problem:** Stream-Copy beginnt mit B-Frames die PTS < Keyframe-PTS haben.
Die Timestamps sind zwar relativ korrekt (B-Frame vor Keyframe), aber nicht absolut an Re-encode angepasst.

### Offene Lösungsansätze

#### Z9. `-output_ts_offset` für manuellen Timestamp-Shift
- **Status:** ZU TESTEN
- **Methode:**
  ```bash
  # Berechne Offset: re-encode Ende - stream-copy B-Frame-Delay
  ffmpeg -y -i wrapped.mkv -ss TIME -t DUR -c:v copy \
         -output_ts_offset 0.7 output.mkv
  ```
- **Erwartung:** Manueller PTS-Shift um Re-encode-Ende + B-Frame-Delay

#### Z10. Zwei-Pass: Stream-Copy + Timestamp-Rewrite
- **Status:** ZU TESTEN
- **Methode:**
  1. Stream-Copy mit beliebigen Timestamps
  2. Separat Timestamps mit `setpts` Filter korrigieren (erfordert Re-Encode)
- **Problem:** Würde Stream-Copy-Vorteil aufheben

#### Z11. mkvmerge Append mit Timestamp-Adjustment
- **Status:** ZU TESTEN
- **Methode:**
  ```bash
  mkvmerge -o output.mkv reencode.mkv \
           --timestamp-adjustment 0:+900ms streamcopy.mkv
  ```
- **Erwartung:** Manueller Shift der Stream-Copy-Timestamps

#### Z12. Concatenation NACH Timestamp-Normalisierung
- **Status:** ZU TESTEN
- **Methode:**
  1. Stream-Copy mit Original-Timestamps
  2. `mkvmerge -o normalized.mkv --sync 0:OFFSET streamcopy.mkv`
  3. Concat mit normalisierten Dateien
- **Erwartung:** Beide Segmente starten bei 0.00s, dann append

### Zusammenfassung des Problems

```
Re-encode Output:        Stream-Copy Output:

PTS: 0.00 → 0.90        PTS: 0.20 → X.XX  (B-Frame Offset!)
                                ↑
                                +0.20s wegen B-Frame PTS < Keyframe DTS

Concat erwartet:
Part 1: 0.00 → 0.90
Part 2: 0.90 → Ende     ← ABER Part 2 beginnt bei 0.20, nicht 0.90!

Lücke: 0.90 - 0.20 = 0.70s (≈ 1 GOP)
```

**Kernproblem:** FFmpeg Stream-Copy kann Timestamps nicht auf einen beliebigen Wert setzen.
Die `-avoid_negative_ts` Option verschiebt zwar Richtung 0, aber B-Frames bleiben relativ zum Keyframe.

---

## Z13: RAW Decode Ansatz (2026-01-27/28) - IN ARBEIT

### Konzept

Statt direkt aus dem Container zu re-encodieren, werden Frames zu RAW YUV decodiert:
1. Decode Frames cutIn bis kAfterIn-1 → RAW YUV (keine Timestamps)
2. Encode RAW → MKV mit sauberen Timestamps (0.00s start)
3. Stream-copy ab kAfterIn (Keyframe) → MKV
4. Concat beide Teile

### Vorteile
- RAW hat keine Timestamps → beim Encode entstehen frische, lineare PTS/DTS
- Kein B-Frame-Reordering-Problem, da RAW in Display-Order vorliegt
- Stream-Copy beginnt sauber am I-Frame

### Implementierungsfortschritt

#### Phase 1: Grundfunktion (2026-01-27)
- [x] RAW Decode mit `select` Filter
- [x] Frame-Count Fix in ttavdata.cpp (`cutOutTime = endFrame / frameRate`)
- [x] `-t` Parameter für Decode-Geschwindigkeit

#### Phase 2: Performance-Optimierung (2026-01-28)
- [x] mkvmerge `--split` statt FFmpeg für Stream-Copy (Frame-genauer)
- [x] **Kleiner Clip zuerst extrahieren, dann RAW decode** (DRASTISCHE Verbesserung!)
  - Vorher: 37 Sekunden für 11 Frames (select muss ALLE Frames durchgehen)
  - Nachher: < 1 Sekunde für 11 Frames

#### Aktuelle Implementierung (2026-01-28)

**Part 1: Re-encode (RAW Decode Workflow)**
```cpp
// Step 1: Extract small segment with mkvmerge (instant, no decoding)
double extractStart = kBeforeIn / frameRate;
double extractEnd = (kAfterIn + 1) / frameRate;
QString extractCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 -A \"%4\"")
    .arg(smallClip).arg(extractTs1).arg(extractTs2).arg(wrappedES);

// Step 2: Decode small clip to RAW (fast, only ~32 frames)
int trimOffset = reencodeStart - kBeforeIn;
QString decodeCmd = QString(
    "ffmpeg -y -i \"%1\" -vf \"select='between(n\\,%2\\,%3)',setpts=N/FR/TB\" "
    "-vsync 0 -f rawvideo -pix_fmt yuv420p \"%4\"")
    .arg(smallClip).arg(trimOffset).arg(trimOffset + numFrames - 1).arg(rawFile);

// Step 3: Encode RAW to H.264
QString encodeCmd = QString(
    "ffmpeg -y -f rawvideo -pix_fmt yuv420p -s %1x%2 -r %3 -i \"%4\" "
    "-c:v libx264 %5 \"%6\"")
    .arg(videoWidth).arg(videoHeight).arg(frameRate).arg(rawFile)
    .arg(x264Params).arg(startPart);
```

**Part 2: Stream-Copy (mkvmerge)**
```cpp
// mkvmerge --split parts: für frame-genaues Extrahieren
double startTimeSec = streamCopyStart / frameRate;
double endTimeSec = (cutOutFrame + 1) / frameRate;

QString videoCmd = QString("mkvmerge -o \"%1\" --split parts:%2-%3 -A \"%4\"")
    .arg(videoOnly).arg(startTs).arg(endTs).arg(wrappedES);
```

### Aktuelle Probleme (2026-01-28)

#### Problem 1: RAW Decode liefert falsche Frame-Anzahl

**Symptom (Segment 0):**
- Erwartet: 11 Frames (Frame 6000 → 6010)
- Tatsächlich: **Nur 4 Frames** decodiert!

**Analyse:**
```
Extract: parts:00:01:59.580-00:02:00.240 (0.66s = ~33 Frames)
kBeforeIn = 5979, reencodeStart = 6000
trimOffset = 6000 - 5979 = 21
select='between(n,21,31)' sollte 11 Frames geben
```

**Vermutete Ursache:**
mkvmerge `--split` behält möglicherweise Original-Timestamps oder Frame-Nummerierung,
aber der `select` Filter im FFmpeg-Befehl erwartet Frame-Nummern ab 0.

**Konkret:** Wenn der extrahierte Clip intern Timestamps von 119.58s-120.24s behält,
zählt FFmpeg die Frames möglicherweise nicht ab 0, sondern nach PTS-Order.

#### Problem 2: Timing-Lücke zwischen Re-encode und Stream-Copy

**Symptom:**
- Re-encode endet bei Frame 6010 ≈ 120.20s
- Stream-Copy beginnt bei 120.22s
- **0.02s Lücke = 1 Frame fehlt**

**Log-Analyse:**
```
RAW Re-encode: 6000 -> 6010 (11 frames)
Stream-copy (mkvmerge): 6011 -> 9263 (3253 frames) time: 120.22 - 185.28
```

Die Zeit 120.22s für Frame 6011 ist korrekt (6011/50=120.22), aber wenn Re-encode
nur 4 statt 11 Frames liefert, fehlen Frames in der Mitte.

#### Problem 3: Gesamte Frame-Anzahl stimmt nicht

**Ergebnis:**
- Erwartet: 11 + 3253 + 7 + 1230 = **4501 Frames**
- Tatsächlich: **4511 Frames** (10 zu viel)

### Video-Stottern

Das Stottern tritt auf:
1. **Am Anfang** - Segment 0 beginnt mit nur 4 statt 11 re-encodierten Frames
2. **In der Mitte** - Übergang Segment 0 → Segment 1 (Timing-Lücke)

### Lösungsansätze (noch zu testen)

#### Z13a: FFmpeg statt mkvmerge für Clip-Extraktion
- **Status:** ZU TESTEN
- **Idee:** FFmpeg normalisiert Timestamps auf 0, mkvmerge nicht
- **Methode:**
  ```bash
  ffmpeg -y -ss START -i wrapped.mkv -t DUR -c:v copy -avoid_negative_ts make_zero small.mkv
  ```
- **Problem:** FFmpeg Stream-Copy hat B-Frame-Timing-Probleme (siehe Z4-Z8)

#### Z13b: Direkt aus großem MKV decodieren (ohne Clip-Extraktion)
- **Status:** ZURÜCK ZUM ALTEN ANSATZ
- **Problem:** Sehr langsam (37s für 11 Frames)
- **Alternative:** Akzeptieren dass RAW-Decode langsam ist

#### Z13c: Frame-Nummern im extrahierten Clip verifizieren
- **Status:** ZU TESTEN
- **Methode:**
  ```bash
  ffprobe -v error -select_streams v:0 -show_entries frame=pkt_pts_time \
          -of csv=p=0 small.mkv | head -40
  ```
- **Ziel:** Verstehen wie Frames im Clip nummeriert sind

#### Z13d: select Filter mit PTS statt Frame-Nummer
- **Status:** ZU TESTEN
- **Idee:** Statt `between(n,21,31)` verwende `between(pts,X,Y)`
- **Methode:**
  ```bash
  ffmpeg -y -i small.mkv -vf "select='between(pts,0.42,0.62)'" ...
  ```
  (wobei 0.42 = 21/50 und 0.62 = 31/50)

### Zusammenfassung

**Was funktioniert:**
- ✅ Geschwindigkeit drastisch verbessert (< 1s statt 37s pro Re-encode)
- ✅ mkvmerge für Stream-Copy funktioniert frame-genau
- ✅ Grundlegende Workflow-Struktur ist korrekt

**Was NICHT funktioniert:**
- ❌ Frame-Auswahl im RAW-Decode-Schritt (4 statt 11 Frames)
- ❌ Video stockt am Anfang und in der Mitte
- ❌ Gesamte Frame-Anzahl stimmt nicht (4511 statt 4501)

**Kernproblem:**
Der `select` Filter mit Frame-Nummern funktioniert nicht korrekt mit dem von
mkvmerge extrahierten Clip. Die Frame-Nummerierung oder PTS im Clip stimmt
nicht mit den erwarteten Werten überein.

---

## Session 2026-01-28 Abend: Concat-Problem Analyse

### Kernproblem identifiziert

Das Stotter-/Timestamp-Problem liegt am **Concat von Re-encode (no B-frames) + Stream-Copy (B-frames)**:

1. **Re-encode** hat `-bf 0` → PTS=DTS (linear: 0.000, 0.020, 0.040...)
2. **Stream-Copy** hat B-frames → PTS≠DTS (reordered)
3. Beim Concat sieht FFmpeg DTS-Rücksprung und **klemmt DTS** → doppelte Timestamps

### Getestete Ansätze

| # | Ansatz | Timestamps | Decoder | Ergebnis |
|---|--------|------------|---------|----------|
| 1 | FFmpeg concat (alles) | ❌ Duplikate | ✅ OK | Stottert |
| 2 | mkvmerge concat (alles) | ✅ Korrekt | ❌ Grüne Blöcke | Artefakte |
| 3 | Hybrid: mkvmerge Segment + FFmpeg Final | ❌ Duplikate | ✅ OK | Stottert |
| 4 | refs=1 + mkvmerge concat | ✅ Korrekt | ❌ Artefakte | Schlechter in Mitte |
| 5 | FFmpeg + mkvmerge --fix-bitstream | ❌ Duplikate | ✅ OK | Keine Verbesserung |
| 6 | FFmpeg + ffmpeg genpts | ? | ? | Abgebrochen |

### Analyse der Timestamp-Probleme

**Original Wrapped MKV (mkvmerge):**
```
PTS         DTS         Flags
119.940000  119.920000  ___
120.000000  119.940000  ___   <- B-frame reordering korrekt
...
```

**Nach mkvmerge --split:**
```
0.140000    N/A         K__
0.060000    N/A         ___
0.020000    N/A         ___
0.000000    0.000000    ___   <- Timestamps korrekt normalisiert
```

**Nach FFmpeg concat (Re-encode + Stream-Copy):**
```
0.200000    0.200000    ___   <- Ende Re-encode
0.364000    0.364000    K__   <- Keyframe Stream-Copy
0.364000    0.364000    ___   <- ❌ DUPLIKAT!
0.364000    0.364000    ___   <- ❌ DUPLIKAT!
... (8x gleicher PTS)
```

**Nach mkvmerge concat:**
```
0.200000    0.200000    ___   <- Ende Re-encode
0.364000    0.364000    K__   <- Keyframe
0.284000    N/A         ___   <- ✅ Korrekt (aber Decoder-Probleme)
0.244000    0.244000    ___
```

### Aktueller Code-Stand

**extern/ttffmpegwrapper.cpp:**
- Wrapping: mkvmerge mit --default-duration
- Stream-Copy: mkvmerge --split
- Segment-Concat: FFmpeg (zuletzt geändert)
- Final-Concat: FFmpeg + ffmpeg genpts Post-Process (zuletzt geändert)
- x264 params: `-refs 1 -bf 0` (refs=1 passend zum Original)

### Erkenntnisse

1. **mkvmerge** erzeugt korrekte B-Frame-Timestamps beim Wrapping und --split
2. **FFmpeg concat** klemmt DTS bei no-B-frame → B-frame Übergang
3. **mkvmerge concat (+)** erhält Timestamps, aber SPS/PPS-Mismatch verursacht Decoder-Artefakte
4. **refs=1** in Re-encode hilft etwas, aber nicht genug
5. **--fix-bitstream-timing-information** kann FFmpeg-Concat-Schaden nicht reparieren

### Mögliche nächste Schritte

1. **B-frames im Re-encode aktivieren** (`-bf 3` wie Original) - komplexere Timestamp-Behandlung nötig
2. **SPS/PPS aus Original extrahieren** und in Re-encode injizieren
3. **Komplettes Re-encode der Segmente** statt Mixing (aber: mehr Qualitätsverlust)
4. **libav direkt verwenden** statt CLI-Tools (wie avcut es macht)
5. **tsMuxeR** für Concat testen

### Bester subjektiver Stand

Petrocelli_video_19.mkv (FFmpeg concat + FFmpeg genpts) - "viel besser" laut User, aber noch nicht perfekt.

---

## NEUER ANSATZ: ES Smart Cut V2 (2026-01-30)

### Motivation

Die bisherigen Container-basierten Ansätze scheiterten alle am gleichen Problem:
- **B-Frame Timestamp-Diskontinuitäten** beim Übergang von re-encoded zu stream-copied Abschnitten
- Egal welche Concat-Methode (FFmpeg, mkvmerge, tsMuxeR) - alle hatten Probleme
- Der Grund: CLI-Tools haben eingebaute "Quirks" bei der Timestamp-Behandlung

### Lösung: Direkte ES-zu-ES Verarbeitung

**Keine Container, keine Timestamps, keine Tool-Quirks!**

Elementary Streams haben keine PTS/DTS - sie werden erst beim Muxen generiert.
Wenn wir direkt auf ES-Ebene schneiden und erst am Ende muxen, umgehen wir
alle Timestamp-Probleme.

### Neue Komponenten

#### 1. TTNaluParser (avstream/ttnaluparser.h/.cpp)

NAL Unit Parser für H.264/H.265 Elementary Streams:
- Findet alle NAL Units (Start-Code Suche)
- Parst NAL Header (Typ, Referenz-Info)
- Parst Slice Header (Slice-Typ, PPS-ID)
- Gruppiert NAL Units zu Access Units (Frames)
- Baut GOP-Struktur auf

```cpp
TTNaluParser parser;
parser.openFile("video.264");
parser.parseFile();

// Zugriff auf Daten
int frameCount = parser.accessUnitCount();
int gopCount = parser.gopCount();
QByteArray frameData = parser.readAccessUnitData(frameIndex);
```

#### 2. TTESSmartCut (extern/ttessmartcut.h/.cpp)

ES Smart Cut Engine:
- Analysiert Cut-Punkte (Keyframe oder nicht?)
- Re-encoded partielle GOPs mit libav
- Stream-kopiert komplette GOPs (Byte-Level)
- Schreibt sauberes Output-ES

```cpp
TTESSmartCut smartCut;
smartCut.initialize("video.264", 50.0);  // 50 fps
smartCut.smartCut("output.264", cutList);

// Statistiken
qDebug() << "Stream-copied:" << smartCut.framesStreamCopied();
qDebug() << "Re-encoded:" << smartCut.framesReencoded();
```

#### 3. Integration in TTFFmpegWrapper

Neue Methode `smartCutElementaryStreamV2()`:
- Nutzt TTESSmartCut statt CLI-Tools
- Optionales Wrapping zu Container (MKV/TS/MP4) am Ende
- Volle Kontrolle über den gesamten Prozess

### Vorteile

| Aspekt | Bisheriger Ansatz | ES Smart Cut V2 |
|--------|-------------------|-----------------|
| Container | Erforderlich | Nur am Ende |
| CLI-Tools | FFmpeg, mkvmerge | Nur libav |
| Timestamps | Problemquelle | Nicht vorhanden |
| Kontrolle | Gering | Vollständig |
| Debugging | Schwierig | Einfach |

### Architektur

```
Video.264 (ES)
     │
     ▼
┌─────────────────────────────────────┐
│  TTNaluParser                       │
│  - Parse NAL Units                  │
│  - Build Frame Index                │
│  - Build GOP Structure              │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│  TTESSmartCut                       │
│  - Analyze Cut Points               │
│  - Re-encode Partial GOPs (libav)   │
│  - Stream-copy Complete GOPs        │
└─────────────────────────────────────┘
     │
     ▼
Output.264 (ES)
     │
     ▼
┌─────────────────────────────────────┐
│  Mux (mkvmerge)                     │
│  - Add timestamps                   │
│  - Add audio                        │
│  - Create container                 │
└─────────────────────────────────────┘
     │
     ▼
Output.mkv
```

### Status

- [x] TTNaluParser implementiert
- [x] TTESSmartCut implementiert
- [x] Integration in TTFFmpegWrapper
- [ ] Testen mit echten ES-Dateien
- [ ] Audio-Handling
- [ ] GUI-Integration

### Nächste Schritte

1. **Testen** mit den Petrocelli ES-Dateien
2. **Audio** synchron schneiden
3. **GUI** Option für V2 Smart Cut

---
*Letzte Aktualisierung: 2026-01-30 (ES Smart Cut V2)*
