# ES Smart Cut - Architektur-Design

## Ziel

Frame-genaues Schneiden von H.264/H.265 Elementary Streams ohne Container.
Re-Encoding nur an den Schnittgrenzen, Stream-Copy für den Rest.

**Vorteile gegenüber Container-Ansatz:**
- Keine PTS/DTS-Diskontinuitäten (ES hat keine Timestamps!)
- Volle Kontrolle über NAL-Unit-Struktur
- Timestamps werden erst beim finalen Muxen generiert
- Unabhängig von FFmpeg/mkvmerge-Quirks

---

## NAL Unit Struktur (H.264/H.265)

### H.264 NAL Types (nal_unit_type)
```
1  = Non-IDR slice (P/B frame)
5  = IDR slice (keyframe)
6  = SEI (Supplemental Enhancement Info)
7  = SPS (Sequence Parameter Set)
8  = PPS (Picture Parameter Set)
9  = AUD (Access Unit Delimiter)
12 = Filler data (kann entfernt werden)
```

### H.265 NAL Types (nal_unit_type)
```
0-9   = Trailing pictures (P/B)
16-21 = Leading pictures
19-20 = IDR pictures (keyframes)
32    = VPS (Video Parameter Set)
33    = SPS
34    = PPS
35    = AUD
38    = SEI prefix
39    = SEI suffix
```

---

## GOP-Struktur (typisch)

```
Display Order (POC):  0   1   2   3   4   5   6   7   8   9  10  11  12
Frame Type:           I   B   B   P   B   B   P   B   B   P   B   B   I
Decode Order (DTS):   I   P   B   B   P   B   B   P   B   B   I   ...

        GOP 0 (12 frames)              │       GOP 1
  ─────────────────────────────────────┼──────────────
```

**Wichtig:** B-Frames referenzieren sowohl vorherige als auch nachfolgende Frames.
Beim Schneiden müssen diese Referenzen berücksichtigt werden!

---

## Smart Cut Algorithmus

### Szenario 1: Cut-In an Keyframe (einfach)
```
Original:  [I B B P B B P B B P B B] [I B B P B B P ...]
           └─────── GOP 0 ─────────┘ └──── GOP 1 ────
Cut-In: ^

Ergebnis: Stream-Copy ab diesem I-Frame
```

### Szenario 2: Cut-In mitten im GOP (komplex)
```
Original:  [I B B P B B P B B P B B] [I B B P B B P ...]
           └─────── GOP 0 ─────────┘ └──── GOP 1 ────
                       ↑
                   Cut-In hier (Frame 5)

Problem: Frames 5,6,7,8,9,10,11 haben Referenzen zu Frames 0-4
         die wir nicht behalten wollen!

Lösung:
1. Decode Frames 0-11 (ganzer GOP)
2. Re-Encode Frames 5-11 als neuen Mini-GOP:
   - Frame 5 wird neues IDR (Keyframe)
   - Frames 6-11 bekommen neue Referenzen
3. Stream-Copy ab GOP 1
```

### Szenario 3: Cut-Out mitten im GOP
```
Original:  [I B B P B B P B B P B B] [I B B P B B P ...]
                       ↑
                   Cut-Out hier (Frame 5)

Einfacher als Cut-In:
- Kopiere Frames 0-5
- Das letzte B-Frame (5) kann problematisch sein wenn es
  zukünftige Frames referenziert

Saubere Lösung:
- Cut-Out immer auf P-Frame oder I-Frame (nicht B-Frame)
- Oder: Re-Encode des letzten B-Frames ohne Vorwärts-Referenz
```

---

## Implementierungs-Phasen

### Phase 1: NAL Parser
**Datei:** `avstream/ttnaluparser.h/.cpp`

```cpp
class TTNaluParser {
public:
    // NAL Unit Struktur
    struct NalUnit {
        int64_t fileOffset;      // Position in Datei
        int64_t size;            // Größe inkl. Start-Code
        uint8_t type;            // nal_unit_type
        bool isKeyframe;         // IDR oder CRA
        int poc;                 // Picture Order Count
        int temporalId;          // Temporal layer (H.265)

        // Slice-Informationen (aus Slice Header)
        int sliceType;           // I, P, B
        int frameNum;            // frame_num (H.264)
        QList<int> refPicList0;  // Referenz-Liste 0
        QList<int> refPicList1;  // Referenz-Liste 1
    };

    // Parsing
    bool parseFile(const QString& esFile);
    const QList<NalUnit>& nalUnits() const;

    // Suche
    int findKeyframeBefore(int frameIndex);
    int findKeyframeAfter(int frameIndex);
    int findGOPEnd(int keyframeIndex);

    // SPS/PPS Extraktion
    QByteArray getSPS(int index = 0);
    QByteArray getPPS(int index = 0);
    QByteArray getVPS(int index = 0);  // H.265 only
};
```

### Phase 2: ES Smart Cut Engine
**Datei:** `extern/ttessmartcut.h/.cpp`

```cpp
class TTESSmartCut {
public:
    // Initialisierung
    bool initialize(const QString& esFile, double frameRate);

    // Smart Cut durchführen
    // cutList: Paare von (startFrame, endFrame) - Frames die BEHALTEN werden
    bool performSmartCut(const QString& outputFile,
                         const QList<QPair<int, int>>& cutList);

    // Für Segment:
    // 1. Analysiere ob Cut-In/Cut-Out an Keyframe
    // 2. Wenn nicht: Re-Encode notwendig
    // 3. Schreibe Output ES

private:
    // Byte-Level Copy (für saubere Segmente)
    bool copyNalUnits(QFile& outFile, int startNal, int endNal);

    // Re-Encode (für partielle GOPs)
    bool reencodeGOP(QFile& outFile,
                     int gopStartFrame,
                     int cutInFrame,
                     int cutOutFrame);

    // NAL Unit schreiben mit korrektem Start-Code
    void writeNalUnit(QFile& outFile, const QByteArray& data);

    TTNaluParser mParser;
    AVCodecContext* mDecoder;
    AVCodecContext* mEncoder;
};
```

### Phase 3: Integration in TTFFmpegWrapper

```cpp
// Neue Methode in TTFFmpegWrapper
bool smartCutElementaryStreamV2(const QString& inputFile,
                                 const QString& outputFile,
                                 const QList<QPair<double, double>>& cutList,
                                 double frameRate);
```

---

## Offene Fragen

1. **POC-Handling bei Re-Encode:**
   - Wie setzen wir POC im neu-kodierten Abschnitt?
   - Muss kontinuierlich zum stream-kopierten Teil sein

2. **SPS/PPS Konsistenz:**
   - Re-encoded und stream-copied Teile müssen kompatible SPS/PPS haben
   - Lösung: Gleichen Encoder-Settings wie Original verwenden

3. **B-Frame Referenzen:**
   - B-Frames am Ende des re-encoded Teils dürfen keine
     Vorwärts-Referenzen auf stream-copied Frames haben
   - Lösung: Re-encode mit `-bf 0` (keine B-Frames) ODER
     bis zum nächsten P/I-Frame re-encoden

4. **Audio Sync:**
   - Audio muss exakt zu den Video-Frames passen
   - Bei Re-Encode: Audio an genauer Frame-Grenze schneiden

---

## Test-Szenarien

1. **Single Segment, Cut-In an Keyframe:**
   - Sollte nur Stream-Copy sein
   - Referenz: Gleich wie Original

2. **Single Segment, Cut-In mitten im GOP:**
   - Re-Encode bis nächster Keyframe
   - Dann Stream-Copy

3. **Multiple Segments:**
   - Jedes Segment einzeln behandeln
   - Finale Konkatenation der ES-Teile

4. **Cut-Out an B-Frame:**
   - Re-Encode oder Rückverschiebung auf P-Frame

---

## Ressourcen

- **h264bitstream:** https://github.com/MINIXJR/h264bitstream
  - h264_dpb_analyze: MMCO/RPLR Analyse
  - h265_analyze: HEVC Struktur-Analyse

- **ITU-T H.264:** NAL Unit Spezifikation
- **ITU-T H.265:** HEVC Spezifikation

---

## Timeline

| Phase | Beschreibung | Geschätzte Komplexität |
|-------|--------------|----------------------|
| 1 | NAL Parser | Mittel (NAL-Struktur parsen) |
| 2 | Smart Cut Engine | Hoch (Decode/Encode/Referenzen) |
| 3 | Integration | Niedrig (API anpassen) |
| 4 | Testing & Debug | Hoch (Edge Cases) |

**Gesamt:** Dies ist ein substantielles Projekt, aber es gibt die volle Kontrolle
über das Schneiden und eliminiert Container-spezifische Probleme.
