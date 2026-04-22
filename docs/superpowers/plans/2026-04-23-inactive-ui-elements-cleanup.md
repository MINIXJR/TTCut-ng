# Inactive UI Elements Cleanup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove four dead UI elements from TTCut-ng: two Chapters tabs (spumux-legacy), the non-functional "Configure..." muxer button, and the hidden `videoFileInfo` widget. Also delete their backing files and dead slots.

**Architecture:** Pure UI cleanup. No behavior changes. Each task removes one element end-to-end (UI file + C++ sources + .pro entries) so each task leaves the tree in a buildable state. Final task does a full clean rebuild and manual smoke test. Work happens in a dedicated git worktree.

**Tech Stack:** Qt 5, qmake, C++17. Build: `qmake ttcut-ng.pro && make -j$(nproc)`. qmake's dependency tracking is known broken — a `make clean` is required after any UI/header change.

**Spec:** `docs/superpowers/specs/2026-04-23-inactive-ui-elements-cleanup-design.md`

---

### Task 1: Create feature worktree

**Files:** none (setup)

- [ ] **Step 1: Create worktree from master**

```bash
cd /usr/local/src/TTCut-ng
git worktree add /usr/local/src/TTCut-ng.ui-cleanup -b fix/inactive-ui-cleanup master
```

- [ ] **Step 2: Verify clean baseline build**

```bash
cd /usr/local/src/TTCut-ng.ui-cleanup
qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -5
```

Expected: `ttcut-ng` binary builds, no errors, warnings unchanged from master.

- [ ] **Step 3: Commit point**

No commit yet (build artifacts not tracked). From here on, all edits happen in `/usr/local/src/TTCut-ng.ui-cleanup`.

---

### Task 2 (Part A): Remove Chapters tab — Settings Dialog

**Files:**
- Modify: `gui/ttcutsettingsdlg.cpp`
- Modify: `ui/ttsettingsdialog.ui`
- Delete: `gui/ttcutsettingschapter.h`
- Delete: `gui/ttcutsettingschapter.cpp`
- Delete: `ui/ttcutsettingschapter.ui`
- Modify: `ttcut-ng.pro`

- [ ] **Step 1: Remove `removeTab(4)` and commented references in `ttcutsettingsdlg.cpp`**

Remove these three lines (line 41 and two comment lines):

```cpp
  // not implemented yet
  settingsTab->removeTab(4);
```

```cpp
  //chaptersPage->setTabData();
```

```cpp
  //chaptersPage->getTabData();
```

- [ ] **Step 2: Remove chapters tab from `ui/ttsettingsdialog.ui`**

Delete the block starting with `<widget class="QWidget" name="tabChapters">` and ending with the matching `</widget>` (the full `tabChapters` QWidget including its `TTCutSettingsChapter chaptersPage` child, about 15 lines).

Also delete the `<customwidget>` entry for `TTCutSettingsChapter`:

```xml
  <customwidget>
   <class>TTCutSettingsChapter</class>
   <extends>QGroupBox</extends>
   <header>../gui/ttcutsettingschapter.h</header>
   <container>1</container>
  </customwidget>
```

- [ ] **Step 3: Delete backing files**

```bash
rm gui/ttcutsettingschapter.h gui/ttcutsettingschapter.cpp ui/ttcutsettingschapter.ui
```

- [ ] **Step 4: Remove `.pro` entries**

In `ttcut-ng.pro`, delete these three lines:
- `ui/ttcutsettingschapter.ui\` from the `FORMS` section
- `gui/ttcutsettingschapter.h\` from the `HEADERS` section
- `gui/ttcutsettingschapter.cpp\` from the `SOURCES` section

Preserve the backslash-continuation on the line that now becomes the last one in each section.

- [ ] **Step 5: Clean-build verification**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -20
```

Expected: build succeeds, no "TTCutSettingsChapter" errors. If any `#include "ttcutsettingschapter.h"` remnants exist elsewhere, the build will fail — search and remove them:

```bash
grep -rn "ttcutsettingschapter\|TTCutSettingsChapter" --include='*.cpp' --include='*.h' --include='*.ui' --include='*.pro' .
```

Expected after fix: 0 matches.

- [ ] **Step 6: Smoke test the Settings dialog**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
# Open Settings (menu: Bearbeiten → Einstellungen or equivalent)
# Verify: 4 tabs shown (Allgemein/Common, Dateien/Files, Encoder, Muxer)
# No Chapters tab
# Close app
```

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Remove dead Chapters tab from Settings dialog

The tab was immediately removed in the constructor via
removeTab(4) and contained only a single spumux checkbox
(DVD/dvdauthor legacy). MKV chapter generation lives in the
Muxer tab (cbMkvCreateChapters + leChapterInterval)."
```

---

### Task 3 (Part B): Remove Chapters tab — Cut Dialog

**Files:**
- Modify: `gui/ttcutavcutdlg.cpp`
- Modify: `ui/avcutdialog.ui`

- [ ] **Step 1: Remove `removeTab(3)` and commented reference in `ttcutavcutdlg.cpp`**

Remove these lines:

```cpp
  // not implemented yet
  tabWidget->removeTab(3);
```

```cpp
  //chaptersPage->setTabData();
```

Also check `setGlobalData()` / save method in the same file for any `//chaptersPage->getTabData();` comment — remove if present.

- [ ] **Step 2: Remove chapters tab from `ui/avcutdialog.ui`**

Delete the block starting with `<widget class="QWidget" name="tabChapters" >` (note: this file uses XML namespace-style with a space before `>`) and ending with the matching `</widget>`. The block is around 20 lines and contains a nested `TTCutSettingsChapter chaptersPage`.

Also delete the `<customwidget>` entry for `TTCutSettingsChapter`:

```xml
  <customwidget>
   <class>TTCutSettingsChapter</class>
   <extends>QGroupBox</extends>
   <header>../gui/ttcutsettingschapter.h</header>
   <container>1</container>
  </customwidget>
```

- [ ] **Step 3: Clean-build verification**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -10
```

Expected: build succeeds. If the generated `ui_avcutdialog.h` still references `TTCutSettingsChapter`, re-check the `.ui` file for missed entries.

- [ ] **Step 4: Smoke test the Cut dialog**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
# Open a demo video + audio, add at least one cut range
# Click the cut button to open the Cut/AV dialog
# Verify: 3 tabs (Common, Encoder, Muxer) — no Chapters tab
# Close dialog, close app
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Remove dead Chapters tab from Cut dialog

Same spumux-legacy content as in the Settings dialog, also
removed at constructor time via removeTab(3)."
```

---

### Task 4 (Part C): Remove "Configure..." muxer button

**Files:**
- Modify: `gui/ttcutsettingsmuxer.cpp`
- Modify: `gui/ttcutsettingsmuxer.h`
- Modify: `ui/ttcutsettingsmuxer.ui`

- [ ] **Step 1: Remove `pbConfigureMuxer` setup and connect in `ttcutsettingsmuxer.cpp`**

Delete line 50:

```cpp
  pbConfigureMuxer->setEnabled(false);
```

Delete the connect line (~line 54):

```cpp
  connect(pbConfigureMuxer,  SIGNAL(clicked()),         SLOT(onConfigureMuxer()));
```

Delete the empty slot definition (~lines 166-168):

```cpp
void TTCutSettingsMuxer::onConfigureMuxer()
{
}
```

- [ ] **Step 2: Remove slot declaration from `ttcutsettingsmuxer.h`**

Delete line 66:

```cpp
    void onConfigureMuxer();
```

- [ ] **Step 3: Remove `pbConfigureMuxer` widget from `ui/ttcutsettingsmuxer.ui`**

Delete the `<widget class="QPushButton" name="pbConfigureMuxer" >` block. The block starts at line ~116 and ends at the matching `</widget>` (check for any spacer/layout surrounding it — if the button sits alone in a layout cell that would become empty, remove the layout item too).

Inspect the result visually in `designer ui/ttcutsettingsmuxer.ui` if unsure.

- [ ] **Step 4: Clean-build verification**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -10
```

Expected: build succeeds, no references to `pbConfigureMuxer` or `onConfigureMuxer` remain. Confirm:

```bash
grep -rn "pbConfigureMuxer\|onConfigureMuxer" --include='*.cpp' --include='*.h' --include='*.ui' .
```

Expected: 0 matches.

- [ ] **Step 5: Smoke test**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
# Open Settings → Muxer tab
# Verify: no "Configure..." button, other controls unchanged
# Close app
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Remove non-functional Configure Muxer button

Leftover from the mplex-CLI era (before v0.60.0 libav
integration). The slot was empty and the button permanently
disabled. libav as built-in muxer has nothing to configure
from this dialog."
```

---

### Task 5 (Part D): Remove videoFileInfo widget

**Files:**
- Modify: `ui/ttcutmainwindow.ui`
- Modify: `gui/ttcutmainwindow.cpp`
- Modify: `gui/ttcutmainwindow.h`
- Delete: `gui/ttcutvideoinfo.h`
- Delete: `gui/ttcutvideoinfo.cpp`
- Delete: `ui/ttcutvideoinfowidget.ui`
- Delete: `ui/ttcutvideoinfowidget.qrc`
- Modify: `ttcut-ng.pro`

- [ ] **Step 1: Remove widget block + customwidget entry from `ui/ttcutmainwindow.ui`**

Delete the `<item row="1" column="0">` block containing `<widget class="TTCutVideoInfo" name="videoFileInfo">` (approximately lines 86-110 — from `<item row="1" column="0">` to the matching `</item>`).

Decrement the `row` attribute on subsequent `<item>` entries in the same `QGridLayout` so there is no gap: the item currently at `row="2"` becomes `row="1"`, the one at `row="3"` becomes `row="2"`, etc. Only items in the SAME grid layout (sibling of the deleted `<item>`).

Delete the customwidget entry (~line 595):

```xml
  <customwidget>
   <class>TTCutVideoInfo</class>
   <extends>QGroupBox</extends>
   <header>../gui/ttcutvideoinfo.h</header>
   <container>1</container>
  </customwidget>
```

- [ ] **Step 2: Remove 3 connects from `ttcutmainwindow.cpp`**

Delete lines 265-267:

```cpp
  connect(videoFileInfo,  SIGNAL(openFile()),       SLOT(onOpenVideoFile()));
  connect(videoFileInfo,  SIGNAL(nextAVClicked()),  SLOT(onNextAVData()));
  connect(videoFileInfo,  SIGNAL(prevAVClicked()),  SLOT(onPrevAVData()));
```

The comment immediately above ("// Connect signals from video and audio info") can stay; remove if it ends up orphaned.

- [ ] **Step 3: Remove method calls to `videoFileInfo->...`**

Delete the following 5 call sites in `ttcutmainwindow.cpp`:

Line ~806 (in a slot that updates UI after navigation):
```cpp
  videoFileInfo->refreshInfo(mpCurrentAVDataItem);
```

Line ~822 (in `onNewFramePos`):
```cpp
  videoFileInfo->refreshInfo(mpCurrentAVDataItem);
```

Line ~1128 (in `closeProject`):
```cpp
  videoFileInfo->onAVDataChanged(mpAVData, 0);
```

Line ~1159 (in `navigationEnabled`):
```cpp
	videoFileInfo->controlEnabled(true);
```

Line ~1248 (in `onAVItemChanged` or similar):
```cpp
  videoFileInfo->onAVDataChanged(mpAVData, avItem);
```

- [ ] **Step 4: Remove dead slots `onNextAVData` / `onPrevAVData` from `ttcutmainwindow.cpp`**

Delete both full method bodies (lines ~1205-1229). For reference, the bodies are:

```cpp
void TTCutMainWindow::onNextAVData()
{
  if (mpCurrentAVDataItem == 0) return;

  int index = mpAVData->avIndexOf(mpCurrentAVDataItem);

  if (index+1 < 0 || index+1 >= mpAVData->avCount()) return;

  mpAVData->onChangeCurrentAVItem(mpAVData->avItemAt(index+1));
}

/* //////////////////////////////////////////////////////////////////////////////
 *
 */
void TTCutMainWindow::onPrevAVData()
{
  if (mpCurrentAVDataItem == 0) return;

  int index = mpAVData->avIndexOf(mpCurrentAVDataItem);

  if (index-1 < 0 || index-1 >= mpAVData->avCount()) return;

  mpAVData->onChangeCurrentAVItem(mpAVData->avItemAt(index-1));
}
```

(Including any comment separator `/* ... */` that becomes orphaned between them.)

- [ ] **Step 5: Remove slot declarations from `ttcutmainwindow.h`**

Delete lines 132-133 (in the `private slots:` section):

```cpp
		void onNextAVData();
    void onPrevAVData();
```

- [ ] **Step 6: Delete backing files**

```bash
rm gui/ttcutvideoinfo.h gui/ttcutvideoinfo.cpp ui/ttcutvideoinfowidget.ui ui/ttcutvideoinfowidget.qrc
```

- [ ] **Step 7: Remove `.pro` entries**

In `ttcut-ng.pro`, delete these four lines:
- `ui/ttcutvideoinfowidget.qrc\` from `RESOURCES`
- `ui/ttcutvideoinfowidget.ui\` from `FORMS`
- `gui/ttcutvideoinfo.h\` from `HEADERS`
- `gui/ttcutvideoinfo.cpp\` from `SOURCES`

Preserve the backslash-continuation on the line that becomes the last one in each section.

- [ ] **Step 8: Clean-build verification**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -20
```

Expected: build succeeds. Residual-reference check:

```bash
grep -rn "videoFileInfo\|TTCutVideoInfo\|ttcutvideoinfo\|onNextAVData\|onPrevAVData\|nextAVClicked\|prevAVClicked" --include='*.cpp' --include='*.h' --include='*.ui' --include='*.pro' .
```

Expected: 0 matches.

- [ ] **Step 9: Smoke test main window + AV navigation**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
# Open video A (File → Open Video...)
# Open a second video B (File → Open Video... again)
# Verify both appear in the video list (top-left tree)
# Click video A → main frames switch to A; click video B → switch to B
# Navigate frames with slider/keys — resolution/aspect info still visible in the video-list columns
# Verify: no crash on project close (File → Close Project or New Project)
# Close app
```

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "Remove hidden videoFileInfo widget

The widget was invisible (maximumSize height=0 since the
TreeView-based GUI overhaul) and fully redundant with
TTVideoTreeView, which already shows filename, resolution
and aspect ratio in columns. AV-item navigation remains
available via clicks in the video tree view, so the
onNextAVData/onPrevAVData slots (only triggered by this
widget) are removed together with it."
```

---

### Task 6 (Part E): Update TODO.md

**Files:**
- Modify: `TODO.md`

- [ ] **Step 1: Remove "Inaktive UI-Elemente" block**

In `TODO.md` (Low Priority section), delete the full block — the heading line plus the four sub-bullets:

```markdown
- **Inaktive UI-Elemente prüfen und ggf. entfernen oder implementieren**
  - Chapters-Tab im Settings-Dialog (`ttcutsettingsdlg.cpp:41`): `removeTab(4)` — "not implemented yet"
  - Chapters-Tab im Schnittdialog (`ttcutavcutdlg.cpp:53`): `removeTab(3)` — "not implemented yet"
  - "Configure..." Button im Muxer-Settings (`ttcutsettingsmuxer.cpp:50`): `setEnabled(false)` — keine Funktion
  - videoFileInfo Widget (`ttcutmainwindow.ui:103`): `maximumSize height=0` — permanent unsichtbar
```

- [ ] **Step 2: Add the new Medium-Priority TODO**

Insert the following block in the Medium Priority section of `TODO.md` (anywhere sensible, e.g., after "Einstellungsdialog neu strukturieren" or near other dialog/UI items):

```markdown
- **Custom MKV Chapter Editor**
  - Dialog mit Liste editierbarer Kapitel: Zeitstempel (hh:mm:ss.zzz), Name, Sprache
  - Vor-Populierung aus Cut-Ins (jeder Cut-In wird Default-Kapitel)
  - Persistenz in `.ttcut`-Projektdatei
  - Die Intervall-basierte Auto-Generierung (`cbMkvCreateChapters` + `leChapterInterval`) im Muxer-Tab bleibt als einfacher Default bestehen
```

- [ ] **Step 3: Add "Completed" entry**

In the `## Completed` section of `TODO.md`, append:

```markdown
- [x] Remove inactive UI elements: Chapters tabs (spumux-legacy), Configure Muxer button, hidden videoFileInfo widget
```

- [ ] **Step 4: Commit**

```bash
git add TODO.md
git commit -m "TODO: remove resolved inactive-UI entry, add chapter-editor TODO

The four inactive UI elements are now deleted. A replacement
for the chapter functionality (custom chapter editor) is
tracked as a new Medium-Priority TODO."
```

---

### Task 7: Full clean rebuild + end-to-end smoke test

**Files:** none (verification)

- [ ] **Step 1: Full clean rebuild**

```bash
cd /usr/local/src/TTCut-ng.ui-cleanup
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tee /tmp/ui-cleanup-build.log | tail -10
```

Expected: binary `ttcut-ng` builds without errors. Grep the log for new warnings:

```bash
grep -iE "warning:|error:" /tmp/ui-cleanup-build.log | grep -v "note:" | head
```

Expected: warnings unchanged compared to master baseline. Any *new* warning is a regression and must be investigated before proceeding.

- [ ] **Step 2: Full residual-reference sweep**

```bash
grep -rn "TTCutVideoInfo\|TTCutSettingsChapter\|pbConfigureMuxer\|onConfigureMuxer\|videoFileInfo\|onNextAVData\|onPrevAVData\|nextAVClicked\|prevAVClicked" --include='*.cpp' --include='*.h' --include='*.ui' --include='*.pro' .
```

Expected: 0 matches.

- [ ] **Step 3: Interactive smoke test**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /tmp/ui-cleanup-runtime.log &
```

Checklist (all must pass):

1. Main window opens without console errors.
2. Open a demo video (H.264 or MPEG-2 test clip), optionally an audio track.
3. **Settings dialog** (Bearbeiten → Einstellungen): 4 tabs visible, no Chapters, no Configure-Button in Muxer tab.
4. Close Settings with OK → no crash.
5. Select at least one cut range ([ ]).
6. **Cut dialog** (Ausschneiden...): 3 tabs visible, no Chapters tab.
7. Close Cut dialog with Cancel → no crash.
8. Open a second video file. Click each entry in the video list → main frames switch, resolution/aspect still visible in the tree-view columns.
9. File → New Project or Close Project → no crash.
10. Close app gracefully.

Skim `/tmp/ui-cleanup-runtime.log` for any new `QObject::connect` warnings or null-pointer warnings referring to removed widgets.

- [ ] **Step 4: No commit**

This task is verification only. If any step fails, diagnose and fix in-place (new commit on the feature branch), then re-run Task 7 from Step 1.

---

### Task 8: Squash and merge to master

**Files:** none (git operations)

- [ ] **Step 1: Review commit log on feature branch**

```bash
cd /usr/local/src/TTCut-ng.ui-cleanup
git log --oneline master..HEAD
```

Expected: 5 commits (Tasks 2, 3, 4, 5, 6). If any intermediate fix commits exist, that's fine — they will be squashed.

- [ ] **Step 2: Squash into a single commit**

```bash
git reset --soft master
git status --short
```

Expected: all changes staged. Now commit as one:

```bash
git commit -m "$(cat <<'EOF'
Remove inactive UI elements (chapters tabs, Configure button, videoFileInfo)

Four UI elements had no user-facing function:

- Chapters tab in Settings dialog: removed at construction
  via settingsTab->removeTab(4). Contained only a spumux
  (DVD/dvdauthor legacy) checkbox.
- Chapters tab in Cut dialog: same legacy, same construction-time
  removal via tabWidget->removeTab(3).
- "Configure..." button in Muxer settings: connected to an
  empty slot, permanently disabled. Leftover from the mplex-CLI
  era (before the v0.60.0 libav integration).
- videoFileInfo (TTCutVideoInfo) widget: pinned to
  maximumSize height=0 in the main window UI. Fully redundant
  with TTVideoTreeView, which already shows filename,
  resolution, and aspect ratio in columns.

Backing files (.cpp/.h/.ui/.qrc) are deleted, and the dead
slots onNextAVData/onPrevAVData (only triggered by the hidden
widget) are removed along with the widget. AV-item navigation
remains available via clicks in the video list.

TODO.md gains a new Medium-Priority entry for a future custom
MKV chapter editor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Confirm the squashed state**

```bash
git log --oneline -1
git diff --stat master..HEAD
```

Expected: one commit, roughly 12 files changed + 7 deleted.

- [ ] **Step 4: Ask user for merge authorization**

STOP HERE. Ask the user explicitly: **"Squashed commit is ready on `fix/inactive-ui-cleanup`. Soll ich nach master mergen?"**

Do not merge without explicit "ja".

- [ ] **Step 5: Merge to master (only on explicit user approval)**

```bash
cd /usr/local/src/TTCut-ng
git merge --no-ff fix/inactive-ui-cleanup -m "Merge branch 'fix/inactive-ui-cleanup'"
git log --oneline -3
```

- [ ] **Step 6: Clean up the worktree**

```bash
git worktree remove /usr/local/src/TTCut-ng.ui-cleanup
git branch -d fix/inactive-ui-cleanup
```

- [ ] **Step 7: Final clean rebuild on master**

```bash
cd /usr/local/src/TTCut-ng
make clean && qmake ttcut-ng.pro && make -j$(nproc) 2>&1 | tail -5
ls -la ttcut-ng
```

Expected: binary present, recent timestamp, no errors.

- [ ] **Step 8: Do NOT push**

Per standing user policy: no `git push` without an explicit "ja" in response to an explicit push question. Just report that master is N commits ahead of origin/master.

---

## Notes for Implementers

- **qmake dependency tracking is broken in this project.** Between each task's edits and its build verification, you MUST `make clean`. An incremental `make` will often pass even with stale `.moc` files and then fail at runtime with undefined symbols.
- **Do not use `bear -- make`** on this machine. There is a known grpc library symbol-lookup error. Plain `make -j$(nproc)` is fine.
- **Line numbers in this plan are approximate** (from a snapshot at plan-writing time). Use the surrounding context (the quoted code snippets) to locate the exact edit, not the line number.
- **If a build fails with "moc-file out of date" or similar cascading issues**, run `make clean` and re-run qmake from scratch. Do not attempt targeted .moc-file deletion.
- **UI file edits:** prefer a text editor over Qt Designer for surgical deletions (Designer rewrites the entire file and may change unrelated formatting, polluting the diff).
