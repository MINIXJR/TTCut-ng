# Implementation Plan: H.264/H.265 Support for TTCut-ng

> **ARCHIV:** Dies ist der ursprüngliche Implementierungsplan (Januar 2026).
> Die tatsächliche Implementierung weicht davon ab — insbesondere wird jetzt ein
> ES-basierter Smart Cut (TTNaluParser + TTESSmartCut) statt Container-basiertem
> Ansatz verwendet. Container-Input wurde entfernt; TTCut arbeitet nur mit
> Elementary Streams. Für die aktuelle Architektur siehe `CLAUDE.md`.

## Zielgruppe und Anwendungsfall

**Primärer Use-Case**: Frame-genaues Schneiden von DVB-S2 Aufnahmen (VDR)
- H.264 (HD-Sender)
- H.265/HEVC (UHD-Sender)
- Transport Stream Container (.ts)
- Typische DVB-Charakteristika: Closed GOPs, CBR/Capped VBR

## Recherche-Ergebnisse

### Existierende Tools

| Tool | Funktion | Eignung |
|------|----------|---------|
| [avcut](https://github.com/anyc/avcut) | Frame-genaues H.264 Schneiden | Konzept-Beweis, aber experimentell |
| [TSDuck](https://tsduck.io/) | TS-Manipulation C++ Library | DVB-fokussiert, BSD-Lizenz |
| [DumpTS](https://github.com/wangf1978/DumpTS) | TS-Analyse | Nur CLI-Tool |
| ffmpeg/libav | Demux/Parse/Encode/Mux | Vollständig, LGPL |

### Technischer Ansatz von avcut

avcut beweist, dass das Konzept funktioniert:
1. GOP-Pufferung mit libavformat
2. Frame-Typ-Erkennung via `frame->pict_type` und `AV_FRAME_FLAG_KEY`
3. Entscheidung pro GOP: COPY / DROP / RE-ENCODE
4. Selektives Re-Encoding nur bei Schnitt innerhalb GOP

**Problem**: "Creates artifacts with others, depends on encoder settings"
→ Bei DVB-Quellen (standardisierte Encoder) weniger kritisch

## Architektur-Erweiterung

### Aktuelle Klassenhierarchie

```
TTAVStream (abstract)
├── TTAudioStream
│   ├── TTMpegAudioStream
│   └── TTAC3AudioStream
├── TTVideoStream
│   └── TTMpeg2VideoStream  ← Aktuell
└── TTSubtitleStream
    └── TTSrtSubtitleStream
```

### Geplante Erweiterung

```
TTAVStream (abstract)
├── TTAudioStream
│   ├── TTMpegAudioStream
│   ├── TTAC3AudioStream
│   └── TTAACStream         ← NEU (für H.264/H.265 Audio)
├── TTVideoStream
│   ├── TTMpeg2VideoStream
│   ├── TTH264VideoStream   ← NEU
│   └── TTH265VideoStream   ← NEU
└── TTSubtitleStream
    └── TTSrtSubtitleStream
```

### Neue Abhängigkeiten

```
libavformat-dev   # Demuxing, Container-Handling
libavcodec-dev    # Codec-Zugriff, Frame-Parsing
libavutil-dev     # Hilfsfunktionen
```

Diese sind bereits für avcut/ffmpeg auf dem System vorhanden.

## Implementierungs-Phasen

### Phase 1: Infrastruktur (Foundation)

**1.1 Build-System erweitern**
```pro
# ttcut-ng.pro ergänzen
unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += libavformat libavcodec libavutil
}
```

**1.2 Abstrakte Basisklasse erweitern**
- `TTVideoStream` um codec-agnostische Methoden erweitern
- Factory-Pattern für Stream-Typ-Erkennung

**1.3 FFmpeg-Wrapper-Klasse**
```cpp
// extern/ttffmpegwrapper.h
class TTFFmpegWrapper {
    // Demux TS → Elementary Streams
    bool demuxTS(const QString& tsFile, const QString& outputDir);

    // Stream-Info extrahieren
    StreamInfo getStreamInfo(const QString& file);

    // Frame-Index erstellen
    FrameIndex buildFrameIndex(const QString& esFile);
};
```

### Phase 2: H.264 Stream-Klasse

**2.1 Header-Strukturen**
```cpp
// avstream/tth264videoheader.h
class TTH264NALUnit;           // NAL Unit Basisklasse
class TTH264SPS;               // Sequence Parameter Set
class TTH264PPS;               // Picture Parameter Set
class TTH264SliceHeader;       // Slice Header (Frame-Info)
class TTH264AccessUnit;        // Access Unit (= 1 Frame)
```

**2.2 Stream-Klasse**
```cpp
// avstream/tth264videostream.h
class TTH264VideoStream : public TTVideoStream {
    int createHeaderList() override;   // NAL-Parsing via libav
    int createIndexList() override;    // Frame-Index aufbauen
    void cut(int start, int end, TTCutParameter* cp) override;

    bool isCutInPoint(int pos) override;   // IDR-Frame?
    bool isCutOutPoint(int pos) override;  // Nicht-referenziert?

private:
    void encodePartH264(int start, int end, TTCutParameter* cp);
    int findGOPBoundary(int framePos);
};
```

**2.3 Frame-Index Struktur**
```cpp
struct H264FrameInfo {
    int64_t pts;
    int64_t dts;
    int64_t fileOffset;
    int frameType;      // AV_PICTURE_TYPE_I, _P, _B
    bool isKeyframe;    // IDR frame
    int gopIndex;       // GOP-Zugehörigkeit
    int nalRefIdc;      // Referenz-Priorität
};
```

### Phase 3: Encoder-Integration

**3.1 H.264 Encoder Parameter**
```cpp
// extern/tth264encodeparameter.h
class TTH264EncodeParameter {
    QString preset;     // "medium", "fast", etc.
    int crf;           // Constant Rate Factor (18-23)
    QString profile;   // "main", "high"
    QString level;     // "4.0", "4.1"
    // DVB-kompatible Defaults
};
```

**3.2 FFmpeg Encoding Command**
```bash
# Re-encode GOP für H.264
ffmpeg -i input.h264 \
    -ss START -to END \
    -c:v libx264 \
    -preset medium \
    -crf 18 \
    -profile:v high \
    -level 4.1 \
    -x264-params keyint=50:min-keyint=25:scenecut=0 \
    -f h264 \
    output.h264
```

### Phase 4: H.265/HEVC Support

**4.1 Analog zu H.264**
- `TTH265VideoStream` mit HEVC-spezifischem Parsing
- VPS (Video Parameter Set) zusätzlich zu SPS/PPS
- CTU (Coding Tree Unit) statt Macroblock

**4.2 FFmpeg Encoding für HEVC**
```bash
ffmpeg -i input.h265 \
    -c:v libx265 \
    -preset medium \
    -crf 20 \
    -x265-params keyint=50:min-keyint=25 \
    -f hevc \
    output.h265
```

### Phase 5: Container & Muxing

**5.1 TS-Demuxing**
- Automatische Erkennung beim Öffnen
- Extraktion zu Elementary Streams
- PTS/DTS Mapping beibehalten

**5.2 Output-Optionen**
```cpp
enum OutputContainer {
    OUTPUT_TS,      // MPEG Transport Stream
    OUTPUT_MKV,     // Matroska (via mkvmerge)
    OUTPUT_MP4      // ISOBMFF (via ffmpeg)
};
```

**5.3 mkvtoolnix Integration**
```bash
# Muxing mit mkvmerge
mkvmerge -o output.mkv \
    --default-duration 0:25fps \
    video.h264 \
    --default-duration 1:48000Hz \
    audio.aac
```

### Phase 6: GUI-Erweiterung

**6.1 Datei-Dialoge**
- TS-Dateien als Eingabe akzeptieren
- Container-Auswahl für Output

**6.2 Codec-Einstellungen**
- Preset-Auswahl (quality vs. speed)
- CRF/Qualitäts-Slider
- Profil/Level für Kompatibilität

## Risiken und Mitigationen

| Risiko | Wahrscheinlichkeit | Mitigation |
|--------|-------------------|------------|
| Artefakte bei Re-Encoding | Mittel | Auf DVB-Quellen beschränken, ausgiebig testen |
| PTS/DTS Synchronisation | Hoch | libav für Timestamp-Handling nutzen |
| B-Frame Referenzen | Mittel | GOP-Grenzen korrekt erkennen |
| Build-Komplexität | Niedrig | pkg-config für libav Dependencies |

## Test-Strategie

1. **Unit Tests** für NAL-Parsing
2. **Integrationstests** mit DVB-Samples (verschiedene Sender)
3. **Vergleich** Original vs. geschnittenes Video (VMAF/SSIM)
4. **Edge Cases**: Schnitt am GOP-Anfang/Ende, sehr kurze Segmente

## Zeitlicher Rahmen (Meilensteine)

| Phase | Beschreibung |
|-------|--------------|
| 1 | Build-System, FFmpeg-Wrapper |
| 2 | H.264 Parsing und Frame-Index |
| 3 | H.264 Cut mit Re-Encoding |
| 4 | H.265 Support |
| 5 | TS-Handling und Muxing |
| 6 | GUI-Integration |

## Referenzen

- [avcut Source Code](https://github.com/anyc/avcut/blob/master/avcut.c) - Konzept-Implementierung
- [TSDuck Documentation](https://tsduck.io/docs/tsduck.html) - TS-Handling
- [FFmpeg libav Tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial) - libav API
- [H.264 Specification](https://www.itu.int/rec/T-REC-H.264) - Codec Details
- [VideoHelp Forum](https://forum.videohelp.com/threads/414634) - Community Diskussion

## Anhang: DVB-S2 Spezifika

### Typische H.264 Parameter (DVB HD)
- Profile: High
- Level: 4.0 oder 4.1
- GOP: 12-15 Frames (Closed GOP)
- B-Frames: 2-3
- Framerate: 25fps (PAL), 50fps (progressive)

### Typische H.265 Parameter (DVB UHD)
- Profile: Main 10
- Level: 5.1
- GOP: 24-48 Frames
- Framerate: 50fps, 60fps
