# Dirty-Tracking Design

**Date:** 2026-03-20
**Type:** Bugfix / UI improvement

## Problem

`onFileNew()` warnt immer vor Datenverlust, auch wenn gerade gespeichert wurde. `closeEvent()` hat keine Warnung (auskommentierter Code).

## Design

### Flag

`bool mProjectModified` in `TTCutMainWindow`, initialisiert auf `false`.

### Setzen (`true`)

Ein Slot `onProjectModified()` verbunden mit diesen TTAVData-Signals:
- `cutItemAppended`, `cutItemRemoved`, `cutItemUpdated`, `cutOrderUpdated`
- `avItemAppended`, `avItemRemoved`
- `markerAppended`, `markerRemoved`

Der Slot setzt `mProjectModified = true` und aktualisiert den Fenstertitel.

### Zuruecksetzen (`false`)

- Nach erfolgreichem `onFileSave()`
- Nach `closeProject()`
- Nach erfolgreichem Projekt-Laden (`onFileOpen()`, `onFileRecent()`)

### Nutzen

- `onFileNew()`: Warnung nur wenn `mProjectModified == true`
- `closeEvent()`: Warnung wenn `mProjectModified == true` mit Save/Discard/Cancel

### Fenstertitel

- Normal: `TTCut-ng - 0.62.0`
- Modified: `TTCut-ng - 0.62.0 *`

Ueber eine Helper-Methode `updateWindowTitle()` die `mProjectModified` prueft.

## Dateien

| Datei | Aenderung |
|-------|-----------|
| `gui/ttcutmainwindow.h` | `mProjectModified` Member, `onProjectModified()` Slot, `updateWindowTitle()` Helper |
| `gui/ttcutmainwindow.cpp` | Signal-Connects, Slot-Implementation, Aenderungen in `onFileNew()`, `onFileSave()`, `closeEvent()`, `closeProject()` |
