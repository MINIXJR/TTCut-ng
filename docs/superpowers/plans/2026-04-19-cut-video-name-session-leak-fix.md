# Cut Video Name Session Leak Bugfix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop TTCut-ng from reusing the previous session's output filename when opening the first video of a new session.

**Architecture:** Remove the QSettings persistence of `TTCut::cutVideoName` (it was redundant with the per-project `.ttcut` storage). Reset the name on direct video opens when no AV-item exists yet so the Cut dialog always derives a fresh default from the current video filename.

**Tech Stack:** Qt 5 C++ (QSettings, QString), qmake build.

**Spec:** `docs/superpowers/specs/2026-04-19-cut-video-name-session-leak-fix-design.md`

**Branch:** master (already checked out; 4 local commits ahead of origin).

---

## File Structure

Three files touched, no new files:

- **`common/ttcut.cpp`** — change the default of the global `cutVideoName` from `"_cut.m2v"` to empty string. Ensures the `isEmpty()` check in `doCut()` triggers derivation on a fresh start.
- **`gui/ttcutsettings.cpp`** — remove the two lines that read/write `cutVideoName` from/to QSettings. Eliminates cross-session leakage. Old keys under `/CutOptions/VideoName/` become ignored dead keys; no migration required.
- **`gui/ttcutmainwindow.cpp`** — in `onReadVideoStream()`, clear `TTCut::cutVideoName` when `avCount() == 0` so direct video opens before any File → New still get a fresh derivation. Adding a second video to an already-loaded project keeps the project's stored name.

No unit-test file touched: output filename handling is UI-layer state without existing automated test coverage. Verification is manual (see Task 4).

---

## Task 1: Change global default of `cutVideoName`

**Files:**
- Modify: `common/ttcut.cpp:238`

- [ ] **Step 1: Open the file and locate the current default**

Run: `grep -n "cutVideoName" common/ttcut.cpp`
Expected output:
```
238:QString  TTCut::cutVideoName       = "_cut.m2v";
```

- [ ] **Step 2: Change the default to empty**

Replace line 238:

```cpp
// Before
QString  TTCut::cutVideoName       = "_cut.m2v";

// After
QString  TTCut::cutVideoName       = "";
```

- [ ] **Step 3: Verify the change**

Run: `grep -n "cutVideoName" common/ttcut.cpp`
Expected output:
```
238:QString  TTCut::cutVideoName       = "";
```

---

## Task 2: Remove QSettings read of `cutVideoName`

**Files:**
- Modify: `gui/ttcutsettings.cpp:213`

- [ ] **Step 1: Locate the read line**

Run: `grep -n 'cutVideoName' gui/ttcutsettings.cpp`
Expected output:
```
213:  TTCut::cutVideoName       = value( "VideoName/", TTCut::cutVideoName ).toString();
382:  setValue( "VideoName/",       TTCut::cutVideoName );
```

- [ ] **Step 2: Remove line 213**

Delete the line:

```cpp
  TTCut::cutVideoName       = value( "VideoName/", TTCut::cutVideoName ).toString();
```

The surrounding context (for reference) after the edit:

```cpp
  beginGroup( "/CutOptions" );
  TTCut::cutDirPath         = value( "DirPath/", TTCut::cutDirPath ).toString();
  TTCut::cutAddSuffix       = value( "AddSuffix/", TTCut::cutAddSuffix ).toBool();
  TTCut::cutWriteMaxBitrate = value( "WriteMaxBitrate/", TTCut::cutWriteMaxBitrate ).toBool();
```

- [ ] **Step 3: Verify the read is gone, write is still present**

Run: `grep -n 'cutVideoName\|VideoName/' gui/ttcutsettings.cpp`
Expected output (single line only):
```
<line_N>:  setValue( "VideoName/",       TTCut::cutVideoName );
```

---

## Task 3: Remove QSettings write of `cutVideoName`

**Files:**
- Modify: `gui/ttcutsettings.cpp` (the `setValue("VideoName/", ...)` line identified in Task 2)

- [ ] **Step 1: Remove the write line**

Delete this line:

```cpp
  setValue( "VideoName/",       TTCut::cutVideoName );
```

The surrounding context after the edit:

```cpp
  beginGroup( "/CutOptions" );
  setValue( "DirPath/",         TTCut::cutDirPath );
  setValue( "AddSuffix/",       TTCut::cutAddSuffix );
  setValue( "WriteMaxBitrate/", TTCut::cutWriteMaxBitrate );
```

- [ ] **Step 2: Verify no residual reference remains**

Run: `grep -n 'cutVideoName\|VideoName/' gui/ttcutsettings.cpp`
Expected output: (empty — no match)

---

## Task 4: Add `onReadVideoStream` reset guard

**Files:**
- Modify: `gui/ttcutmainwindow.cpp:711-714`

- [ ] **Step 1: Locate the current function**

Run: `grep -n 'onReadVideoStream' gui/ttcutmainwindow.cpp`
Expected output (identifying declaration and definition lines).

Read lines 708-715 to confirm the current body:

```cpp
void TTCutMainWindow::onReadVideoStream(QString fName)
{
  mpAVData->openAVStreams(fName);
}
```

- [ ] **Step 2: Add the guarded reset**

Replace the function body so it reads:

```cpp
void TTCutMainWindow::onReadVideoStream(QString fName)
{
  // Fresh video open (no existing AV-item): clear the output filename so
  // the Cut dialog derives a fresh default from the current video.
  // If an AV-item already exists (multi-video project or project-loaded
  // session), keep the current name so project-defined custom names are
  // preserved.
  if (mpAVData->avCount() == 0) {
    TTCut::cutVideoName = "";
  }
  mpAVData->openAVStreams(fName);
}
```

- [ ] **Step 3: Confirm `TTCut` header is already in scope**

Run: `grep -n '#include "../common/ttcut.h"\|#include "common/ttcut.h"' gui/ttcutmainwindow.cpp`
Expected: one or more matches. `TTCut::cutVideoName` is already referenced at lines 1038, 1041, 1043, 1065, 1076, 1138 of the same file, so the include is guaranteed to be present.

---

## Task 5: Full rebuild

**Files:** (build artefacts only)

- [ ] **Step 1: Clean and rebuild**

Run:

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: build completes with exit status 0, produces `./ttcut-ng` binary. Warnings acceptable; no errors.

- [ ] **Step 2: Verify binary was updated**

Run: `ls -la ttcut-ng && file ttcut-ng`
Expected: recent mtime, `ELF 64-bit LSB executable`.

---

## Task 6: Manual verification

Verification follows the four scenarios from the spec. Use `QT_QPA_PLATFORM=xcb ./ttcut-ng` to launch. Have two ES files ready (e.g. `VideoA.h264` + `VideoA.ac3`, `VideoB.h264` + `VideoB.ac3`).

- [ ] **Step 1: Optional — inspect stale QSettings key**

The `VideoName/` key from previous buggy sessions is harmless after the fix
(no longer read), but can be inspected for completeness:

```bash
find ~/.config -name 'ttcut-ng.conf' -o -name 'ttcut-ng*.ini' 2>/dev/null | \
  xargs grep -l 'VideoName' 2>/dev/null || echo "No stale key"
```

Not required; purely informational.

- [ ] **Step 2: Scenario 1 — Primary bug reproduction check (post-fix)**

1. Start: `QT_QPA_PLATFORM=xcb ./ttcut-ng`
2. File → Open Video → select `VideoA.h264`
3. Mark a cut-in/cut-out pair, click "Cut"
4. In the Cut dialog, observe the "Output file" field.

Expected: field shows `VideoA_cut` (not any previous-session filename).

Cancel the cut dialog (no need to actually run the cut).

- [ ] **Step 3: Scenario 2 — App-start case after a completed cut**

1. With `ttcut-ng` still running from Step 2, complete the cut for VideoA (click OK in the dialog, let it finish).
2. Close `ttcut-ng`.
3. Relaunch: `QT_QPA_PLATFORM=xcb ./ttcut-ng`
4. File → Open Video → select `VideoB.h264`
5. Mark a cut, click "Cut".

Expected: Cut dialog shows `VideoB_cut`, **not** `VideoA_cut.mkv` (the previous session's output).

Cancel the dialog.

- [ ] **Step 4: Scenario 3 — Project preservation**

1. In the running session, File → New (confirm if asked).
2. File → Open Video → `VideoA.h264`.
3. Open the Cut dialog, type a custom name e.g. `A_final`, cancel.
4. File → Save Project → save as `projectA.ttcut`.
5. Close `ttcut-ng`.
6. Relaunch, File → Open Project → `projectA.ttcut`.
7. Open the Cut dialog.

Expected: field shows `A_final` (the project-stored name, not a derived default).

8. With project A loaded, File → Open Video → add `VideoB.h264` as a second item.
9. Open Cut dialog again.

Expected: field still shows `A_final` (project name preserved across multi-video add).

- [ ] **Step 5: Scenario 4 — File → New reset**

1. After cutting `VideoA`, File → New.
2. File → Open Video → `VideoB.h264`.
3. Open Cut dialog.

Expected: field shows `VideoB_cut`.

- [ ] **Step 6: Record results**

If any scenario fails: stop, do not commit; return to the systematic debugging flow.

If all four scenarios pass: proceed to Task 7.

---

## Task 7: Commit

**Files:** (already staged for logical grouping)

- [ ] **Step 1: Inspect the diff once more**

Run: `git diff common/ttcut.cpp gui/ttcutsettings.cpp gui/ttcutmainwindow.cpp`
Expected: three small changes matching Tasks 1–4; no unrelated edits.

- [ ] **Step 2: Stage and commit**

Run:

```bash
git add common/ttcut.cpp gui/ttcutsettings.cpp gui/ttcutmainwindow.cpp
git commit -m "$(cat <<'EOF'
Fix cutVideoName session leak on first project after app start

Remove QSettings persistence of TTCut::cutVideoName and reset it on
direct video opens when no AV-item exists yet. Custom per-project
output names remain stored in the .ttcut project file.

Previously, the global QSettings value outlived the app session,
causing the Cut dialog to pre-fill the first cut of a new session
with the previous session's output filename instead of deriving one
from the current video.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Expected: commit succeeds on master.

- [ ] **Step 3: Verify repo state**

Run: `git log --oneline -3 && git status`
Expected: new commit at HEAD, working tree clean except for any pre-existing `TODO.md` edits.

---

## Rollback

If verification in Task 6 fails and a fix is not immediately apparent:

```bash
git diff common/ttcut.cpp gui/ttcutsettings.cpp gui/ttcutmainwindow.cpp  # inspect
git checkout -- common/ttcut.cpp gui/ttcutsettings.cpp gui/ttcutmainwindow.cpp  # revert
```

Then reinvoke systematic debugging on the failing scenario.
