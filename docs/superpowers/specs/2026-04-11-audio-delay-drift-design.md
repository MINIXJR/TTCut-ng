# Audio Delay per Track + Audio-Drift in Schnittliste

**Date:** 2026-04-11
**Status:** Design approved
**TODO Items:** 7 (Manual audio delay/offset per track) + 8 (Schnittliste "Audio-Versatz" Spalte)

## Overview

Two related but independent improvements to audio offset handling:

1. **Audio-Liste "Delay"-Spalte**: Editierbarer per-Track Delay-Wert, der beim Audio-Schnitt und MKV-Muxen angewendet wird.
2. **Schnittliste "Audio-Drift"-Spalte**: Read-only akkumulierter Boundary-Drift pro Schnitt, berechnet bei der Vorschau.

## Item 7: Audio-Liste "Delay"-Spalte

### Aktueller Zustand

- `TTAudioItem::setItemData()` setzt `audioDelay` hart auf `"0"` (FIXME in `ttaudiolist.cpp:85`)
- Die Spalte "Delay" (Column 6) existiert im `TTAudioTreeView`, zeigt aber immer "0"
- `TTESInfo` parst `audio_N_first_pts` und `audio_N_trimmed_ms` nicht, obwohl `ttcut-demux` diese Werte pro Track in die .info-Datei schreibt
- `TTAudioTrackInfo` hat nur `file`, `codec`, `language`

### Design

**Default-Wert:**
- Immer 0, unabhängig davon ob eine .info-Datei existiert
- Begründung: Wenn `ttcut-demux` einen `trimmed_ms`-Wert geschrieben hat, wurde das Padding bereits ausgeführt — die Audio-ES auf der Platte ist bereits synchron

**Editierbarkeit:**
- Immer editierbar (QSpinBox oder QLineEdit im TreeView)
- User kann einen positiven oder negativen ms-Wert eingeben
- Typischer Use Case: MPEG-2 mit ProjectX demuxed (kein Audio-Trim), oder wenn der automatische Trim von `ttcut-demux` nicht ausreichend war

**Wirkung des Delay-Werts:**
- **Audio-Schnitt** (`cutAudioStream()`): Der Delay wird als PTS-Offset eingerechnet, verschiebt die Audio-Schnittgrenzen um N ms
- **MKV-Muxen** (`TTMkvMergeProvider`): Track-Delay im Matroska-Container

**Wichtig:** Der Delay-Wert aus der Audio-Liste ist ein **globaler** Versatz für die gesamte Spur. Er wird NICHT in der Schnittliste "Audio-Drift"-Spalte eingerechnet — das sind zwei getrennte Ebenen.

### TTESInfo-Erweiterung

`TTAudioTrackInfo` erweitern um:
```cpp
struct TTAudioTrackInfo {
    QString file;
    QString codec;
    QString language;
    double  firstPts;     // audio_N_first_pts
    int     trimmedMs;    // audio_N_trimmed_ms
};
```

`TTESInfo::parseSection("audio")` erweitern um:
```cpp
track.firstPts   = values.value(QString("audio_%1_first_pts").arg(i), "0").toDouble();
track.trimmedMs  = values.value(QString("audio_%1_trimmed_ms").arg(i), "0").toInt();
```

Diese Werte werden informativ geparst (z.B. für Logging, Debug-Ausgabe), beeinflussen aber nicht den Default-Delay.

### UI-Änderung

Column 6 im `TTAudioTreeView` wird von statischem Text zu einem editierbaren Widget (QSpinBox):
- Range: -5000 bis +5000 ms
- Default: 0
- Suffix: " ms"
- Signal bei Änderung: Wert in `TTAudioItem::audioDelay` speichern

## Item 8: Schnittliste "Audio-Drift"-Spalte

### Aktueller Zustand

- Spalte 4 in `TTCutTreeView` heißt "Audio-Versatz"
- Zeigt `av_offset_ms` aus der .info-Datei — gleicher Wert für jeden Schnitt
- `localAudioOffset` in `TTAudioStream::getStartIndex/getEndIndex` akkumuliert die Audio-Frame-Boundary-Abweichung pro Schnitt, wird aber nirgends angezeigt

### Design

**Spaltenname:** "Audio-Drift" (statt "Audio-Versatz")

**Angezeigter Wert:**
- **Vor der Vorschau:** "—" (Platzhalter, nicht berechnet)
- **Nach der Vorschau:** Akkumulierter `localAudioOffset` in ms, gerundet (z.B. "12 ms")

**Berechnung:**
- `localAudioOffset` fällt bei der Vorschau bereits an (`TTAudioStream::getStartIndex/getEndIndex`)
- Der Wert muss aus dem Preview-Schnitt zurück-transportiert werden in die Schnittlisten-Anzeige
- Bezieht sich immer auf die **erste Audiospur** in der Audio-Liste

**Read-only:** Die Spalte ist nicht editierbar. Der Wert ergibt sich rein technisch aus der Position der Audio-Frame-Grenzen relativ zu den Video-Cut-Punkten. Der User kann ihn nur indirekt beeinflussen, indem er Schnittpunkte verschiebt.

**Tooltip:** Header-Tooltip erklärt, dass sich der Wert auf die erste Audiospur bezieht und die akkumulierte Audio-Frame-Boundary-Abweichung zeigt.

**Kein manueller Delay eingerechnet:** Der Delay aus der Audio-Liste (Item 7) wird hier NICHT eingerechnet. Audio-Drift und manueller Delay sind getrennte Ebenen:
- Audio-Liste Delay = globaler Versatz der gesamten Spur
- Schnittliste Audio-Drift = technisch bedingter Boundary-Drift pro Schnitt

## Datenfluss

```
.info Datei
  └─ audio_N_trimmed_ms → TTESInfo (informativ, Logging)
  └─ audio_N_first_pts  → TTESInfo (informativ, Logging)

Audio-Liste
  └─ Delay (editierbar, ms) → TTAudioItem::audioDelay
       ├─ → cutAudioStream(): PTS-Offset bei Schnittgrenzen
       └─ → TTMkvMergeProvider: Container Track-Delay

Schnittliste
  └─ Audio-Drift (read-only, ms) ← localAudioOffset aus Preview-Schnitt
       └─ Bezieht sich auf erste Audiospur
       └─ Zeigt "—" vor Vorschau
```

## Betroffene Dateien

### TTESInfo-Erweiterung
- `avstream/ttesinfo.h` — `TTAudioTrackInfo` struct erweitern
- `avstream/ttesinfo.cpp` — `parseSection("audio")` erweitern

### Audio-Liste Delay
- `data/ttaudiolist.h` — `audioDelay` Typ von QString auf int ändern (ms)
- `data/ttaudiolist.cpp` — `setItemData()` FIXME beheben, Default = 0
- `gui/ttaudiotreeview.cpp` — Column 6 als QSpinBox statt Text
- `gui/ttaudiotreeview.h` — Signal für Delay-Änderung
- `data/ttcutaudiotask.cpp` — `audioDelay` als PTS-Offset anwenden
- `extern/ttmkvmergeprovider.cpp` — Track-Delay beim Muxen anwenden
- `data/ttcutprojectdata.cpp` — Delay in .ttcut Projektdatei speichern/laden

### Schnittliste Audio-Drift
- `gui/ttcuttreeview.cpp` — Spalte 4: "Audio-Drift", Platzhalter "—", Wert nach Preview
- `gui/ttcuttreeview.h` — Methode zum Setzen des Drift-Werts nach Preview
- `data/ttcutpreviewtask.cpp` — `localAudioOffset` nach Preview zurückgeben
- `data/ttcutlistdata.h` — Drift-Wert pro Cut-Entry speichern
