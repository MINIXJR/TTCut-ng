# Cut Video Name Session Leak — Bugfix Design

**Date:** 2026-04-19
**Branch:** master (to be branched for implementation)
**Status:** Design approved

## Problem

After opening TTCut-ng fresh and working on a video without first clicking
"File → New Project", the Cut dialog pre-fills the output filename field with
the last output filename of the **previous session** instead of deriving it
from the currently opened video.

This leads to cuts being written under the wrong filename, often overwriting
or colliding with the previous project's output.

## Root Cause

`TTCut::cutVideoName` (global, `common/ttcut.h:240`) is persisted in QSettings
under key `/CutOptions/VideoName/`:

- `gui/ttcutsettings.cpp:213` — read at startup
- `gui/ttcutsettings.cpp:382` — written on every `writeSettings()`

After each cut, `data/ttavdata.cpp:1543` assigns the actual output filename
to `TTCut::cutVideoName`, and subsequent `writeSettings()` calls persist it.

In `gui/ttcutmainwindow.cpp:1038`, `doCut()` only derives the name from the
current video filename when `TTCut::cutVideoName.isEmpty()`. Because the value
loaded from QSettings is never empty, the derivation path is bypassed on the
first cut after application start.

`closeProject()` (line 1138) clears the name — which is why the bug only
appears on the **first** project of a session. From the second project
onward, the user has triggered a reset via File → New or Open Project.

## Design Decision

Remove QSettings persistence of `cutVideoName` entirely. The `.ttcut` project
file (`data/ttcutprojectdata.cpp:570`, `:610`) already preserves user-custom
names per project — this is the correct location. The global QSettings slot
was redundant and caused the cross-session leak.

Additionally: clear `cutVideoName` in `onReadVideoStream()` when no AV-item
exists yet (`avCount() == 0`). This ensures a fresh derivation for the very
first video of a session, including when the user opens a video directly
without first clicking "File → New".

### Semantic Table

| Scenario | `avCount` at entry | Behaviour |
|---|---|---|
| App start → Open Video A | 0 | Clear → `doCut` derives `A_cut` |
| File → New → Open Video B | 0 (closeProject already cleared) | Clear (idempotent) → derive `B_cut` |
| Open Project P1 with custom name `P1_final` | via `openProjectFile`, not `onReadVideoStream` | `readProjectFile` sets `P1_final` from `.ttcut` |
| P1 loaded → add second Video B via Open Video | > 0 | **Keep** `P1_final` — preserves project setting |
| Cut Video A done, add Video B without File → New | > 0 | Keep `A_cut.mkv` — ambiguous case; user edits in dialog |

The last row is a deliberate trade-off: distinguishing "user wants to switch"
from "user wants multi-video project" requires explicit user intent that the
UI does not currently capture. The Cut dialog (`leOutputFile`) remains the
correct place for manual override.

## Changes

### 1. `common/ttcut.cpp:238`

```cpp
// Before
QString  TTCut::cutVideoName       = "_cut.m2v";

// After
QString  TTCut::cutVideoName       = "";
```

### 2. `gui/ttcutsettings.cpp`

Remove these two lines:

- Line 213: `TTCut::cutVideoName = value( "VideoName/", TTCut::cutVideoName ).toString();`
- Line 382: `setValue( "VideoName/", TTCut::cutVideoName );`

No fallback / migration is needed. Old QSettings entries become ignored dead
keys; they neither break anything nor affect behaviour after the fix.

### 3. `gui/ttcutmainwindow.cpp:711`

```cpp
void TTCutMainWindow::onReadVideoStream(QString fName)
{
  if (mpAVData->avCount() == 0) {
    TTCut::cutVideoName = "";
  }
  mpAVData->openAVStreams(fName);
}
```

## Invariants After Fix

- `cutVideoName` is session-transient and never survives across app restarts.
- `.ttcut` project files remain the sole persistent carrier of user-custom
  output names.
- `doCut` derivation from `vStream->fileName()` runs reliably for every
  fresh video opened after app start or `closeProject()`.
- Multi-video project workflows (adding AV-items to an already-loaded
  project) retain the project's custom output name.

## Out of Scope

- Per-AV-item output names (Approach C in brainstorming) — architectural
  rework, not part of this bugfix.
- Source-tracking flag (Approach B) — unnecessary once QSettings persistence
  is removed.
- Dialog UX changes — `leOutputFile` in `TTCutAVCutDlg` remains the manual
  override for ambiguous scenarios.

## Testing

Manual verification steps:

1. **Primary bug reproduction (pre-fix):**
   - Cut Video A, close app
   - Start app, open Video B, open Cut dialog → observe A's output name
2. **Post-fix: app-start case**
   - Cut Video A, close app
   - Start app, open Video B, open Cut dialog → must show `B_cut`
3. **Post-fix: project preservation**
   - Save project P1 with custom name `P1_final`, close app
   - Start app, open P1 → Cut dialog shows `P1_final`
   - Add second video to P1 via "Open Video" → Cut dialog still shows `P1_final`
4. **Post-fix: File → New**
   - After cutting Video A, File → New, open Video B → Cut dialog shows `B_cut`

No automated tests — output filename handling is UI-layer state without
existing unit test coverage.
