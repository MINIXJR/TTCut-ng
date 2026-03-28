# Quality-Check-Tool: Extra-Frame-Korrektur

**Datum:** 2026-03-28
**Datei:** `tools/ttcut-quality-check/ttcut-quality-check.py`

## Problem

Das Quality-Check-Tool rechnet Frame-Indizes naiv in Zeiten um (`frame / fps`), ohne Extra-Frames aus defekten MPEG-2 DVB-Aufnahmen zu berücksichtigen. TTCut-ng korrigiert die Audio-Schnittzeiten mit `(frame - extras_vor_frame) / fps`. Das Tool muss dieselbe Korrektur machen, sonst misst es falsche A/V-Sync-Offsets und vergleicht falsche Referenz-Positionen.

**Konkretes Symptom:** -428ms A/V-Offset bei manuell verifiziert synchronem Material.

## Lösung

### 1. `parse_info_file()` erweitern

`es_extra_frames=...` aus der `[warnings]`-Sektion parsen. Neuer Key im Result-Dict:

```python
result["extra_frames"] = []  # default: leere Liste
# In [warnings]-Sektion:
# es_extra_frames=7009,10983,11218,...
# → result["extra_frames"] = [7009, 10983, 11218, ...]  (sortierte int-Liste)
```

### 2. Neue Hilfsfunktion `count_extras_before()`

Binary Search mit `bisect_left` — Python-Pendant zu `TTESInfo::countExtraFramesBefore()` und `TTAVData::countExtraFramesBefore()` in TTCut-ng.

```python
from bisect import bisect_left

def count_extras_before(frame: int, extra_frames: list[int]) -> int:
    """Count extra frames with index < frame (binary search)."""
    if not extra_frames:
        return 0
    return bisect_left(extra_frames, frame)
```

### 3. `frames_to_seconds()` erweitern

Optionaler Parameter für Extra-Frame-Liste:

```python
def frames_to_seconds(frame: int, fps: float, extra_frames: list[int] = None) -> float:
    if extra_frames:
        frame -= count_extras_before(frame, extra_frames)
    return frame / fps
```

### 4. Betroffene Stellen

Alle direkten `frame / fps` Berechnungen und `frames_to_seconds()`-Aufrufe müssen die Extra-Frame-Liste erhalten. Betrifft:

| Test | Stelle | Was wird korrigiert |
|------|--------|---------------------|
| Test 4 (Visual) | Referenz-Start `start_frame / fps` (Zeile 602) | Position im Referenz-MKV |
| Test 4 (Visual) | Cut-Offset-Akkumulation `seg_frames / fps` (Zeile 612) | Zeitposition im Cut-MKV |
| Test 4 (Visual) | Alle `ref_eff_start`, `mid_frame_offset/fps`, `ref_end` (Zeilen 629-662) | Vergleichspositionen |
| Test 5 (A/V Sync) | Segment-Start/End `cuts[0][0] / fps`, `cuts[0][1] / fps` (Zeilen 829-830) | Audio-Extrakt-Zeitfenster |
| Test 6 (Audio Waveform) | Boundary-Positionen `(end - start + 1) / fps` (Zeile 977) | Cut-Boundary-Zeiten |

### 5. Nicht geändert

- **Test 1 (Frame Count):** `expected_frames = sum(end - start + 1)` bleibt unverändert. Das MKV enthält alle Frames inklusive Extra-Frames. Der Frame-Count-Check ist korrekt.
- **Test 2 (PTS Consistency):** Arbeitet direkt auf MKV-Paket-PTS, keine Frame-Index-Umrechnung.
- **Test 3 (Duration Match):** Vergleicht MKV-interne Video- vs Audio-Dauer, keine Frame-Index-Umrechnung.

### 6. Report-Header

Wenn Extra-Frames erkannt werden, eine Info-Zeile im Report-Header:

```
Extra Frames: 370 (from .info, audio time correction active)
```

Wenn `--info` nicht angegeben oder keine Extra-Frames vorhanden: keine Zeile (kein Noise).

### 7. Durchreichung der Extra-Frame-Liste

Die Extra-Frame-Liste wird aus `parse_info_file()` gelesen und als Parameter an die betroffenen Test-Funktionen durchgereicht. Kein globaler State, kein extra CLI-Parameter.

Bestehende Signatur-Änderungen:
- `test_visual()`: neuer Parameter `extra_frames: list[int] = None`
- `test_av_sync()`: neuer Parameter `extra_frames: list[int] = None`
- `test_audio_waveform()`: neuer Parameter `extra_frames: list[int] = None`

### 8. Neuer Test: Defect Region Report

Ein neuer informativer Test der meldet, welche defekten Frames im finalen Schnitt enthalten sind und wo sie sich befinden.

#### Ablauf

1. Extra-Frame-Indizes aus `.info` gegen `--cuts`-Bereiche filtern (nur Frames innerhalb der Schnittbereiche)
2. Gefilterte Frames in Defect Regions gruppieren anhand von Gap- und Offset-Einstellungen
3. Ergebnis als INFO ausgeben (kein PASS/FAIL — beschreibt Quellmaterial-Zustand, nicht Schnittqualität)

#### Gruppierung (Defect Regions)

Benachbarte Extra-Frames werden zu einer Defect Region zusammengefasst, wenn der Abstand zwischen zwei aufeinanderfolgenden Extra-Frames ≤ `defect-gap` Sekunden beträgt. Der `defect-offset` definiert einen Vorlauf vor der ersten Frame-Position jeder Region (für Landezonen-Import relevant, hier nur informativ angezeigt).

#### Settings aus TTCut-ng lesen

Die Werte werden aus `~/.config/TTCut-ng/TTCut-ng.conf` gelesen (QSettings INI-Format):

```ini
[Common]
ExtraFrameClusterGap=5
ExtraFrameClusterOffset=2
```

Mapping auf Quality-Check-Tool:
- `ExtraFrameClusterGap` → `defect-gap` (Sekunden, Default: 5)
- `ExtraFrameClusterOffset` → `defect-offset` (Sekunden, Default: 2)

Fallback-Kette:
1. CLI-Parameter `--defect-gap` / `--defect-offset` (höchste Priorität)
2. `~/.config/TTCut-ng/TTCut-ng.conf`
3. Defaults: gap=5s, offset=2s

#### Hilfsfunktion

```python
def parse_ttcut_settings() -> dict:
    """Read defect region settings from TTCut-ng config file."""
    result = {"defect_gap_sec": 5, "defect_offset_sec": 2}
    conf = Path.home() / ".config" / "TTCut-ng" / "TTCut-ng.conf"
    if not conf.exists():
        return result
    section = None
    with open(conf) as f:
        for line in f:
            line = line.strip()
            m = re.match(r"\[(.+)\]", line)
            if m:
                section = m.group(1)
                continue
            if section == "Common" and "=" in line:
                key, val = line.split("=", 1)
                key = key.strip()
                val = val.strip()
                if key == "ExtraFrameClusterGap":
                    result["defect_gap_sec"] = int(val)
                elif key == "ExtraFrameClusterOffset":
                    result["defect_offset_sec"] = int(val)
    return result
```

#### Ausgabe

```
[INFO] Defect Regions: 46 defective frames in 8 regions within cut segments
       Region 1: frames 12644-12646 (3 frames, 0.1s) at 00:05:42
       Region 2: frames 15708-15730 (12 frames, 0.5s) at 00:09:51
       ...
       Settings: defect-gap=5s, defect-offset=2s (from ~/.config/TTCut-ng/TTCut-ng.conf)
```

Wenn keine Extra-Frames in den Schnittbereichen liegen:

```
[INFO] Defect Regions: no defective frames within cut segments
```

Wenn `--info` nicht angegeben oder keine Extra-Frames: Test wird übersprungen (wie bisherige optionale Tests).

#### CLI-Parameter

```
--defect-gap SEC       Defect region grouping gap in seconds (default: from TTCut-ng settings or 5)
--defect-offset SEC    Defect region start offset in seconds (default: from TTCut-ng settings or 2)
```

## Verifizierung

Testdatei: `04x03_-_Geschichten_von_Interesse_Nr._2` (MPEG-2, 25fps, 370 Extra-Frames)

1. Quality-Check ohne `.info` → Ergebnis identisch mit bisherigem Verhalten (keine Regression)
2. Quality-Check mit `.info` → A/V Sync Offset sollte von -428ms auf ~0ms (≤50ms) sinken
3. Frame-Count-Check bleibt PASS (keine Änderung)
4. Defect Region Report zeigt gruppierte Extra-Frames innerhalb der Schnittbereiche
5. `--defect-gap` / `--defect-offset` überschreiben TTCut-ng-Settings korrekt
