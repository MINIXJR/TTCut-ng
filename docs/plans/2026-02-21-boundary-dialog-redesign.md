# Boundary Burst Dialog Redesign

## Problem

The current pre-flight boundary check uses a blocking QMessageBox before
preview/cut with two issues:

1. **Inaudible bursts shown**: A burst at -36 dB (context -81 dB) is flagged
   even though it's inaudible. The relative delta is large but the absolute
   level is irrelevant.

2. **Single decision for all issues**: "Shift 1 Frame" applies to ALL detected
   bursts. User cannot shift one boundary and accept another.

3. **Blind decision**: User must decide before hearing the preview — they can't
   judge whether the burst is audible.

## Design

Replace the blocking dialog with an integrated workflow across three UI layers.

### 1. Cut List (TTCutTreeView) — Immediate Feedback

When "Set Cut-Out" or "Set Cut-In" is clicked, `detectAudioBurst()` runs
automatically (~5ms). A warning icon appears in a new column if a burst is
detected above the configurable threshold.

- New column 5: burst status icon (orange warning triangle or empty)
- Tooltip on icon: "Audio-Burst: -28 dB (Context: -77 dB)"
- Re-analyzed when cut points are modified (`onUpdateItem`)
- Icon cleared when burst is below threshold or cut point changes

### 2. Preview Dialog (TTCutPreview) — Shift Workflow

A QLabel + QPushButton appear below the cut selection combobox, visible only
when the currently displayed cut transition has a burst.

```
[Schnitt 3-4          ▼]
⚠ Audio-Burst am Ende    [Shift -1 Frame]

[<< Zurück] [▶ Play] [Weiter >>] [Schließen]
```

Clicking "Shift -1 Frame":
1. Shifts CutOut by -1 frame (or CutIn by +1 frame) via `cutList->update()`
2. Regenerates preview clip for this transition only
3. Re-runs burst analysis
4. Warning disappears if burst is resolved

### 3. Final Cut (onDoCut) — Warning Dialog

When clicking "AV Schnitt" with unresolved bursts, a simple QMessageBox:

```
⚠ Schnitt 3 hat einen erkannten Audio-Burst am Ende.
  Vorschau nutzen um zu prüfen ob Shift nötig ist.

[Trotzdem schneiden] [Abbrechen]
```

No shift functionality in this dialog — preview is the place for that.

### 4. Settings — Configurable Threshold

New parameter `TTCut::burstThresholdDb` (static int, default: -30):
- Bursts with absolute level below threshold are filtered out
- Value 0 disables the filter (show all bursts)
- Configurable in Settings dialog (Common tab), spinbox range -60 to 0

### 5. Remove Old Dialog

Remove `showBoundaryDialog()` and the pre-flight `checkAudioBoundaries()` calls
from `onDoCut()` and `doCutPreview()`. Replace with the new integrated approach.

## Data Flow

```
Set Cut-Out → appendCutEntry()
                → detectAudioBurst() → burst info stored per cut item
                                         ↓
                               TTCutTreeView: icon in column 5
                                         ↓
                               TTCutPreview: warning + shift button
                                         ↓
                               onDoCut(): warning if unresolved bursts
```

## Files Affected

| File | Change |
|------|--------|
| `common/ttcut.h` | Add `burstThresholdDb` static member |
| `common/ttcut.cpp` | Initialize default -30 |
| `gui/ttcutsettingscommon.*` | Add spinbox for threshold |
| `data/ttcutlist.h` | Add burst info fields to TTCutItem |
| `data/ttavdata.cpp` | Remove old dialog, add burst analysis on cut append |
| `gui/ttcuttreeview.cpp` | Add column 5, icon rendering |
| `gui/ttcutpreview.h/cpp` | Add burst warning label + shift button |
| `ui/previewwidget.ui` | Add label + button to layout |
| `ui/cutlistwidget.ui` | Add column header |
