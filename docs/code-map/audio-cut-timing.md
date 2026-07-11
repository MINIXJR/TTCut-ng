---
base_commit: f90d0ab9be2f10aac1e887373dcdef8a227d92b8
last_verified: 2026-07-11
sources:
  - data/ttavdata.cpp
  - data/ttavdata.h
  - extern/ttffmpegwrapper.cpp
  - extern/ttffmpegwrapper.h
  - data/ttcutpreviewtask.cpp
  - data/ttcutpreviewtask.h
  - gui/ttcutpreview.cpp
  - gui/ttcuttreeview.cpp
  - gui/ttcuttreeview.h
  - gui/ttcutmainwindow.cpp
  - avstream/ttesinfo.h
---

# Audio-Cut-Zeitkette (video-frame-index → audio-frame-aligned cut)

Wie ein Schnitt in **Video-Frame-Indizes** (Anzeige-Ordnung) zu einem
**tonrasteralignierten** Audio-Schnitt wird: Extra-Frame-Korrektur → Delay →
Raster-Snapping mit Feed-Forward-Drift → Einzeldurchlauf-Schnitt mit
fortlaufendem PTS und optionaler AC3-acmod-Umkodierung. Diese Kette hat uns beim
Benders-Burst 273 Frames ≈ 11 s verschoben (korrekt, weil Schnitt **und**
Burst-Prüfung dieselbe Formel nutzen — siehe `burst-detection.md`).

**Nicht** Teil dieser Karte: die Video-Schnitt-Semantik selbst (MPEG-2 →
`mpeg2-cut.md`, H.264/H.265 → `smart-cut.md`), die Burst-Erkennung
(`burst-detection.md`), die Anzeige-/Decode-Ordnung (`frame-order.md`).

## Diagramm

Durchgezogen = Datenfluss (Produzent → Konsument). Gestrichelt = Auslöser.

```mermaid
flowchart TD
  INFO[".info esExtraFrames()"]
  MP2X["MPEG-2 parser<br/>extraIndices()"]
  EXTRA["mExtraFrameIndices"]
  CEFB["countExtraFramesBefore"]
  PROD["Producers (6 call sites)<br/>onDoCut · doH264Cut · doAudioOnlyCut<br/>TTCutPreviewTask · TTCutPreview"]
  VKL["videoKeepList<br/>(sec, extra-corrected)"]
  DELAY["per-track delay<br/>getDelayMs"]
  PLAN["planAudioCut"]
  KEEP["keepList<br/>(audio-frame-aligned)"]
  ACMOD["targetAcmods<br/>(AC3 only)"]
  CUT["cutAudioStream"]
  OUT["cut audio file<br/>→ mux list / preview"]
  DRIFT["drifts (ms/segment)"]
  COL4["cut list drift column"]

  INFO --> EXTRA
  MP2X --> EXTRA
  EXTRA --> CEFB
  CEFB --> VKL
  PROD --> VKL
  DELAY --> PLAN
  VKL --> PLAN
  PLAN --> KEEP
  PLAN --> DRIFT
  KEEP --> CUT
  ACMOD --> CUT
  CUT --> OUT
  DRIFT --> COL4
```

## Kanten-Semantik (eine Zeile pro Grenze)

| von → nach | Daten / Reihenfolge / Invariante |
|---|---|
| `INFO → EXTRA` | `.info`-Feld `es_extra_frames` → `mExtraFrameIndices` (H.264/H.265). Aufsteigend sortiert. Geladen im Öffnen-Pfad (`openAVStreams`/`onOpenVideoFinished`) und erneut im Cut-Pfad, falls leer. |
| `MP2X → EXTRA` | Für MPEG-2 stattdessen aus dem Bitstream-Parser: `loadMpeg2FieldExtras` → `TTMpeg2VideoStream::extraIndices()`, **nur wenn `mExtraFrameIndices` leer ist**. Feldbild-Zweiteinträge (siehe `mpeg2-cut.md`). |
| `EXTRA → CEFB` | Sortierte Extra-Index-Liste; `countExtraFramesBefore(idx)` zählt per Binärsuche die Einträge `< idx`. Invariante: Liste aufsteigend sortiert. |
| `CEFB → VKL` | Extra-Anzahl `N`; Zeit = `(index − N)/fps`. Cut-Out nutzt `index+1` (Grenze **hinter** den letzten behaltenen Frame). Bildet den aufgeblähten Anzeige-Index auf echte Audiozeit ab. |
| `PROD → VKL` | Produzent baut die (start,end)-Sekundenliste pro Segment. **`doH264Cut` speist die schon B-Frame-korrigierte Video-Keep-List ein**; `onDoCut`/`doAudioOnlyCut`/Vorschau bauen roh aus den Cut-Indizes via `CEFB`. Ohne Delay. |
| `DELAY → PLAN` | Per-Track-Delay in ms (`TTAudioItem::getDelayMs`), als `delaySec` auf die Segmentzeiten addiert. Pro Tonspur eigener Wert. |
| `VKL → PLAN` | (start,end) Sekunden je Segment, extra-korrigiert, **noch ohne Delay**. Kontrakt: bereits anzeige-/B-Frame-korrekt — `planAudioCut` verschiebt nur, prüft nicht. |
| `PLAN → KEEP` | (start,end) auf das **Audio-Frame-Raster** gerundet (Vielfache der Frame-Dauer: MP2@48k = 24 ms, AC3@48k = 32 ms). Feed-Forward: `numFrames` je Segment so gewählt, dass die kumulierte Audiolänge der Videolänge folgt. |
| `PLAN → DRIFT` | Kumulierter A/V-Versatz in ms nach jedem Segment (Audiolänge − Videolänge, Summe aller vorherigen). Im eingeschwungenen Zustand ±½ Audioframe. |
| `KEEP → CUT` | Rasteralignierte (start,end). `cutAudioStream` behält nur Frames, die **komplett** ins Segment passen (`pktTime + frameDur > endTime` → stop) → verliert ≤1 Frame je Segmentende; genau das kompensiert `planAudioCut` per `numFrames`. |
| `ACMOD → CUT` | Ziel-`acmod` pro Segment (nur AC3, aus `analyzeAcmod` über die geplanten Fenster). Frames mit abweichendem `acmod` werden dekodiert → umkanaliert (`swr`) → neu kodiert; sonst Stream-Copy. |
| `CUT → OUT` | Einzeldurchlauf über alle Segmente. Fortlaufender PTS-Versatz (`ptsOffset = nextOutputPts − pkt->pts` je Segmentanfang) macht die Ausgabe lückenlos (entfernt die Zwischensegment-Lücke). Ausgabeformat aus Dateiendung. |
| `DRIFT → COL4` | Drift-ms pro Schnitt → Cut-Listen-Spalte 4 (`TTCutTreeView::onAudioDriftUpdated`, setzt Spalte 4). **Zwei** Signale speisen denselben Slot: `audioDriftCalculated` (Vorschau, `TTCutPreviewTask`) und `cutAudioDriftCalculated` (Final-Cut, `TTAVData`). Nur Track 0. |

## Annahmen & Kontrakte

- **`planAudioCut`** setzt voraus, dass `videoKeepList` schon extra-korrigiert und
  (für H.264/H.265) B-Frame-korrigiert ist. Es addiert nur den Delay und snappt
  aufs Raster — es prüft die Eingabe nicht. Eine unkorrigierte Zeit landet direkt
  im Ton, ohne Warnung.
- **`cutAudioStream`** setzt rasteralignierte Grenzen voraus (garantiert `planAudioCut`).
  Seine „komplett passen"-Regel verwirft ≤1 Frame je Segmentende; die
  `numFrames`-Wahl in `planAudioCut` ist genau darauf ausgelegt.
- **`countExtraFramesBefore`** setzt `mExtraFrameIndices` aufsteigend sortiert voraus
  (Binärsuche).
- **Synchron mit der Burst-Prüfung:** `detectCutOutBurst`/`detectCutInBurst`
  (`data/ttavdata.cpp`) nutzen dieselbe Grenzformel `(index[+1] − extra)/fps`.
  Ändert sich die Korrektur hier, muss sie dort mitgehen (siehe `burst-detection.md`).

## Bekannte Fallstricke

- **Die Extra-Korrektur kann die Grenze um Sekunden verschieben.** Gemessen am
  Benders-Beispiel (MPEG-2 SD, Comedy Central): 273 Extra-Frames vor dem Cut-Out
  → −10,9 s. Kein Fehler: Schnitt und Burst-Prüfung nutzen dieselbe Formel, sind
  also einig. Ein Konsument, der die Korrektur vergäße, läge Sekunden daneben.
- **`mAudioGapIndices` ist NICHT Teil dieser Kette.** Diese zweite Liste
  (`.info audioGapFrames`, in `ttavdata.cpp` zu Clustern verarbeitet) dient nur der
  Defekt-Meldung, nicht der Cut-Zeitrechnung. `countExtraFramesBefore` liest allein
  `mExtraFrameIndices`. Nicht verwechseln.
- **Delay ist pro Track, Drift-Anzeige nur Track 0.** Bei unterschiedlichen
  Per-Track-Delays zeigt Spalte 4 nur die erste Spur.

## Redundanz / Konsolidierungskandidaten

- **6 Produzenten** bauen dieselbe Sequenz — `videoKeepList` (via
  `countExtraFramesBefore`) → `planAudioCut` → `targetAcmods` (AC3) →
  `cutAudioStream` — mit kleinen Abweichungen: `onDoCut` (MPEG-2/Container),
  `doH264Cut` (aus B-Frame-korrigierter Keep-List), `doAudioOnlyCut`,
  `TTCutPreviewTask` (2×: Vollcut + Segment), `TTCutPreview` (GUI-Vorschau).
  Kandidat für einen gemeinsamen Helfer (`buildAudioCutPlan` + `cut`). Solange
  getrennt, driften die Pfade auseinander, wenn nur einer geändert wird.
- **`(index − extra)/fps` ist an ≥6 Stellen offen kodiert** — die Audiozeit-Variante
  der in `frame-order.md` notierten Anzeige-Zeit-Konvertierungen. Gemeinsamer
  Helfer `videoIndexToAudioSeconds` wäre die eine Wahrheit.
- **Zwei Drift-Signale** (`audioDriftCalculated`, `cutAudioDriftCalculated`) auf
  denselben Slot `onAudioDriftUpdated`. Vereinheitlichbar.
