# Fix: CutOut-Frame-Anzeige am Stream-Ende (H.264)

## Problem

Beim Durchklicken der Schnittliste wird fuer den letzten Schnitt der CutOut-Frame
nicht angezeigt. Das CutOut-Widget zeigt stattdessen den CutOut des vorherigen
Schnitts (stale image). Erst nach mehreren Klicks auf PREV im CutOut-Widget wird
ein Frame angezeigt.

**Betroffen**: H.264 Streams, letzter Schnitt in der Schnittliste.

## Root Cause Analyse

### Historischer Kontext

1. **Original (v0.52.0)**: `onCutSelectionChanged` zeigte `cutItemAt(index-1)` — den
   CutOut des VORHERIGEN Schnitts. Der letzte CutOut wurde nie direkt angezeigt.
2. **Commit 27412e1**: EOF-Drain-Fix fuer den H.264-Decoder (single `receive_frame`).
3. **Commit 7a84e24**: Entfernte `cutItemAt(index-1)`, zeigt jetzt immer den CutOut
   des SELEKTIERTEN Schnitts. Entbloesste damit das Decoder-Problem am Stream-Ende.

### Technisches Problem

`decodeFrame(cutOutPos)` in `extern/ttffmpegwrapper.cpp` schlaegt fehl wenn:

1. **Frame-Index-Mismatch**: TTNaluParser (CutOut-Index) und TTFFmpegWrapper
   (`mFrameIndex`) koennten unterschiedlich viele Frames zaehlen. Wenn der CutOut-Index
   `>= mFrameIndex.size()`, ist der Keyframe-Lookup ein OOB-Zugriff.

2. **EOF-Drain-Erschoepfung**: Bei B-Frame-Reordering puffert der Decoder Frames.
   Die Skip-Loop koennte alle Drain-Frames aufbrauchen, bevor `decodeCurrentFrame()`
   den Ziel-Frame abrufen kann.

### Warum der PREV-Button funktioniert

Nach mehreren PREV-Klicks ist die Frame-Position weit genug vom Stream-Ende entfernt.
Der Decoder kann den Frame ohne Drain dekodieren.

## Fix-Ansatz: Hybrid

### 1. Bounds-Check in `decodeFrame()`

- Pruefe `frameIndex` gegen `mFrameIndex.size()` VOR dem Zugriff
- Bei Out-of-Bounds: auf `mFrameIndex.size() - 1` clampen (letzter gueltiger Frame)
- Logging der Diskrepanz

### 2. Frame-Count-Vergleich beim Stream-Open

In `TTMPEG2Window2::openVideoStream()`: Logge TTFFmpegWrapper-Framecount vs.
VideoStream-Headercount, damit ein Mismatch sofort sichtbar wird.

### 3. Robustere Skip-Loop

Wenn `skipCurrentFrame()` waehrend Drain scheitert (`mDecoderDrained == true`):
- Nicht sofort aufgeben, sondern aus der Loop breaken
- `decodeCurrentFrame()` direkt versuchen (Decoder koennte Ziel-Frame gepuffert haben)

### 4. Fallback bei Decode-Failure

Wenn `decodeCurrentFrame()` nach Skip-Loop leer zurueckgibt:
1. Re-Seek + nochmal versuchen (frischer Decoder-State)
2. Wenn auch das fehlschlaegt: `frameIndex - 1` versuchen
3. Logging fuer alle Failure-Pfade

## Dateien

| Datei | Aenderung |
|-------|-----------|
| `extern/ttffmpegwrapper.cpp` | `decodeFrame()`: Bounds-Check, Skip-Loop Resilience, Fallback |
| `extern/ttffmpegwrapper.cpp` | `decodeCurrentFrame()`: Drain-Logging |
| `extern/ttffmpegwrapper.cpp` | `skipCurrentFrame()`: Drain-Logging |
| `mpeg2window/ttmpeg2window2.cpp` | Frame-Count Vergleich beim Stream-Open |
