# AC3 acmod Normalisierung am Schnittuebergang

**Date:** 2026-03-21
**Type:** Feature

## Problem

DVB-Sender wechseln den AC3 Audio Coding Mode (acmod) an Werbegrenzen — z.B. 5.1 (acmod=7) waehrend der Sendung, Stereo (acmod=2) waehrend der Werbung. Beim framegenau geschnittenen Video bleiben einige Frames mit dem "falschen" acmod am Anfang/Ende des Segments uebrig. Der Player (mpv) muss bei jedem Wechsel den Audio-Output neu initialisieren, was zu hoerbarem Stocken fuehrt.

## Design

### Erkennung

Fuer jeden Schnitt (CutIn/CutOut) in der Schnittliste:

1. **Haupt-acmod** des Segments bestimmen: acmod der Mehrheit der AC3-Frames zwischen CutIn und CutOut
2. **CutIn-acmod** pruefen: acmod des AC3-Frames an der CutIn-Position
3. **CutOut-acmod** pruefen: acmod des AC3-Frames an der CutOut-Position
4. Wenn CutIn-acmod ≠ Haupt-acmod: Wechselposition bestimmen (erster Frame mit Haupt-acmod)
5. Wenn CutOut-acmod ≠ Haupt-acmod: Wechselposition bestimmen (letzter Frame mit Haupt-acmod)

Datenquelle: `TTAC3AudioHeader::acmod` aus der Audio-Header-Liste (bereits vorhanden).

### Anzeige in der Schnittliste

Analog zum Burst-Icon:
- Icon/Text in der Info-Spalte wenn am CutIn oder CutOut ein acmod-Wechsel erkannt wird
- Tooltip mit Details: "Audio 5.1 → Stereo am CutOut"
- Reine Information — kein Warndialog, da automatische Korrektur aktiv (wenn Setting eingeschaltet)

### Re-Encoding beim Schnitt

Wenn `TTCut::normalizeAcmod == true`:

1. `cutAudioStream()` erhaelt zusaetzlich die acmod-Normalisierungsinfo pro Segment
2. Nach dem Stream-Copy: pruefen ob Frames am Anfang/Ende den falschen acmod haben
3. Betroffene Frames extrahieren, mit libav re-encoden auf den Haupt-acmod:
   - Stereo → 5.1: Upmix (leere Surround-Kanaele)
   - 5.1 → Stereo: Downmix
4. Re-encodete Frames ersetzen die originalen per concat stream-copy

### Einstellung

- `TTCut::normalizeAcmod` (bool, default: true)
- Checkbox in Einstellungen (Gruppe Audio): "AC3 Kanalformat am Schnitt normalisieren"
- Persistenz in `.conf` Datei unter `[Audio]`

## Dateien

| Datei | Aenderung |
|-------|-----------|
| `common/ttcut.h/.cpp` | `normalizeAcmod` static bool |
| `gui/ttcutsettings.cpp` | Load/Save des Settings |
| `gui/ttcutsettingsencoder.cpp` | Checkbox UI |
| `data/ttcutlist.cpp` | acmod-Wechsel Erkennung + Icon in Schnittliste |
| `extern/ttffmpegwrapper.h/.cpp` | Re-Encode Logik (Extraktion, Upmix/Downmix, Concat) |
| `data/ttavdata.cpp` | acmod-Info an cutAudioStream uebergeben |

## Scope

### In Scope
- AC3 acmod-Wechsel erkennen und anzeigen
- Re-Encode der betroffenen Frames (nur wenige, minimal-invasiv)
- Setting mit Checkbox
- Funktioniert fuer MPEG-2 und H.264/H.265 Schneidpfade

### Out of Scope
- MP2/AAC/EAC3 (nur AC3)
- Gesamtes Audio re-encoden
- acmod-Wechsel mitten im Segment (nur CutIn/CutOut Grenzen)
