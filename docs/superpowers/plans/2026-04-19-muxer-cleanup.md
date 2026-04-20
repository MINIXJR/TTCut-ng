# Muxer UI Cleanup + Codec Compatibility Guard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relabel the Muxing combo to be container-first and honest, remove the misleading MP4 option entirely, and disable the MPG option when the source codec is H.264/H.265.

**Architecture:** Display order decouples from stored values via `QComboBox::itemData()`. Internal `TTCut::outputContainer` values remain `0=mplex`, `1=MKV`; the combo shows MKV first and MPG second. Legacy QSettings values of `2` (removed MP4 option) migrate to `1` (MKV) on load. The `case 2: MP4` block in `ttavdata.cpp` and the corresponding dead branch in `ttcutavcutdlg.cpp` are deleted.

**Tech Stack:** Qt 5 C++ (`QComboBox`, `QStandardItemModel`, `QSettings`), qmake build.

**Spec:** `docs/superpowers/specs/2026-04-19-muxer-cleanup-design.md`

**Branch:** `fix/muxer-cleanup` (to be created from master).

---

## File Structure

Five files touched, no new files:

- **`gui/ttcutsettingsmuxer.h`** — add two private helper declarations (`muxerValueAt`, `indexForMuxerValue`).
- **`gui/ttcutsettingsmuxer.cpp`** — helper implementations, combo population with `userData`, `setTabData()` current-index lookup via helper, `onMuxerProgChanged()` reads value via helper, `onEncoderCodecChanged()` disables MPG row for non-MPEG-2 codecs and clamps the preferred muxer.
- **`data/ttavdata.cpp`** — delete `case 2: MP4` block and update the preceding comment about container values.
- **`gui/ttcutavcutdlg.cpp`** — delete the dead `TTCut::outputContainer == 2` branch in the extension-derivation block; the remaining `else if` reduces to a plain `else` for the mplex default path.
- **`gui/ttcutsettings.cpp`** — migration block: any of `outputContainer`, `mpeg2Muxer`, `h264Muxer`, `h265Muxer` holding the legacy `2` → remap to `1`.

No unit-test files: all UI-layer state without existing automated coverage. Verification is manual (Task 7).

---

## Task 1: Create feature branch

- [ ] **Step 1: Branch from current master**

Run:
```bash
git status
```
Expected: `On branch master`, working tree shows only the uncommitted `TODO.md`. If anything else is staged or modified — stop and investigate.

Run:
```bash
git checkout -b fix/muxer-cleanup
git status
```
Expected: `Switched to a new branch 'fix/muxer-cleanup'`, `TODO.md` still modified, nothing else.

---

## Task 2: Update `gui/ttcutsettingsmuxer.h` — declare helpers

**Files:**
- Modify: `gui/ttcutsettingsmuxer.h:52-57`

- [ ] **Step 1: Read the current `private` section**

Run:
```bash
grep -n -A 6 "private:" gui/ttcutsettingsmuxer.h
```
Expected:
```
52:  private:
53-    void initMuxProgList();
54-    void initMuxTargetList();
55-    void initOutputContainerList();
56-    void updateMuxerVisibility();
57-
```

- [ ] **Step 2: Add the two helper declarations**

Edit `gui/ttcutsettingsmuxer.h`. Replace:
```cpp
  private:
    void initMuxProgList();
    void initMuxTargetList();
    void initOutputContainerList();
    void updateMuxerVisibility();
```
with:
```cpp
  private:
    void initMuxProgList();
    void initMuxTargetList();
    void initOutputContainerList();
    void updateMuxerVisibility();
    int  muxerValueAt(int displayIndex) const;
    int  indexForMuxerValue(int outputContainerValue) const;
```

- [ ] **Step 3: Verify**

Run:
```bash
grep -n "muxerValueAt\|indexForMuxerValue" gui/ttcutsettingsmuxer.h
```
Expected: two matches, both in the `private:` block.

---

## Task 3: Update `gui/ttcutsettingsmuxer.cpp` — combo population, helpers, slot logic

**Files:**
- Modify: `gui/ttcutsettingsmuxer.cpp` (several edits in one task because the changes are mutually dependent; splitting would leave intermediate non-compiling states)

- [ ] **Step 1: Add `<QStandardItemModel>` include**

Locate the include block near the top (around line 32-35). Run:
```bash
grep -n "#include" gui/ttcutsettingsmuxer.cpp | head -10
```
If `<QStandardItemModel>` is not already included, add it. The target include section should end up containing:
```cpp
#include "ttcutsettingsmuxer.h"

#include "../common/ttcut.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QFileDialog>
#include <QStandardItemModel>
```

- [ ] **Step 2: Rewrite `initMuxProgList()` (line 66-79)**

Replace the entire function:
```cpp
void TTCutSettingsMuxer::initMuxProgList()
{
  cbMuxerProg->clear();
  cbMuxerProg->insertItem(0, "Mplex (MPEG-2)");
  cbMuxerProg->insertItem(1, "mkvmerge (MKV)");
  cbMuxerProg->insertItem(2, "FFmpeg (MP4/TS)");

  // Check availability and set default
  if (TTMkvMergeProvider::isMkvMergeInstalled()) {
    cbMuxerProg->setCurrentIndex(1);  // Default to mkvmerge if available
  } else {
    cbMuxerProg->setCurrentIndex(0);  // Fallback to mplex
  }
}
```
with:
```cpp
void TTCutSettingsMuxer::initMuxProgList()
{
  cbMuxerProg->clear();
  // Display order: MKV first (default/modern), MPG second.
  // userData holds the internal TTCut::outputContainer value
  // (0 = mplex, 1 = MKV) so the stored semantics stay stable.
  cbMuxerProg->insertItem(0, "MKV (libav)", 1);
  cbMuxerProg->insertItem(1, "MPG (mplex)", 0);

  // Default: MKV (always available via libav; isMkvMergeInstalled() is
  // kept for semantic compatibility but now always returns true).
  cbMuxerProg->setCurrentIndex(indexForMuxerValue(1));
}
```

- [ ] **Step 3: Add the two helper implementations**

Insert at the end of the file (after the last method), before the trailing newlines:
```cpp
int TTCutSettingsMuxer::muxerValueAt(int displayIndex) const
{
  return cbMuxerProg->itemData(displayIndex).toInt();
}

int TTCutSettingsMuxer::indexForMuxerValue(int outputContainerValue) const
{
  for (int i = 0; i < cbMuxerProg->count(); ++i) {
    if (cbMuxerProg->itemData(i).toInt() == outputContainerValue) return i;
  }
  return 0;  // fall back to first row (MKV)
}
```

- [ ] **Step 4: Fix `setTabData()` current-index lookup (line 114)**

Change line 114 from:
```cpp
  cbMuxerProg->setCurrentIndex(TTCut::outputContainer);
```
to:
```cpp
  cbMuxerProg->setCurrentIndex(indexForMuxerValue(TTCut::outputContainer));
```

- [ ] **Step 5: Rewrite `onMuxerProgChanged(int)` (line 224-242)**

Replace the entire function body:
```cpp
void TTCutSettingsMuxer::onMuxerProgChanged(int index)
{
  TTCut::outputContainer = index;

  // Save the muxer preference for the current codec
  switch (TTCut::encoderCodec) {
    case 0:  // MPEG-2
      TTCut::mpeg2Muxer = index;
      break;
    case 1:  // H.264
      TTCut::h264Muxer = index;
      break;
    case 2:  // H.265
      TTCut::h265Muxer = index;
      break;
  }

  updateMuxerVisibility();
}
```
with:
```cpp
void TTCutSettingsMuxer::onMuxerProgChanged(int index)
{
  int value = muxerValueAt(index);
  TTCut::outputContainer = value;

  // Save the muxer preference for the current codec
  switch (TTCut::encoderCodec) {
    case 0:  TTCut::mpeg2Muxer = value; break;
    case 1:  TTCut::h264Muxer  = value; break;
    case 2:  TTCut::h265Muxer  = value; break;
  }

  updateMuxerVisibility();
}
```

- [ ] **Step 6: Rewrite `onEncoderCodecChanged(int)` (line 255-280)**

Replace the entire function:
```cpp
void TTCutSettingsMuxer::onEncoderCodecChanged(int codecIndex)
{
  // Get the preferred muxer for this codec
  int preferredMuxer;
  switch (codecIndex) {
    case 0:  // MPEG-2
      preferredMuxer = TTCut::mpeg2Muxer;
      break;
    case 1:  // H.264
      preferredMuxer = TTCut::h264Muxer;
      break;
    case 2:  // H.265
      preferredMuxer = TTCut::h265Muxer;
      break;
    default:
      preferredMuxer = 1;  // Default to mkvmerge
      break;
  }

  // Update the muxer selection to the preferred muxer
  cbMuxerProg->setCurrentIndex(preferredMuxer);
  TTCut::outputContainer = preferredMuxer;

  // Update visibility (MPEG-2 Target only for mplex + MPEG-2)
  updateMuxerVisibility();
}
```
with:
```cpp
void TTCutSettingsMuxer::onEncoderCodecChanged(int codecIndex)
{
  // Disable the MPG (mplex) row for H.264/H.265; enable it for MPEG-2.
  bool mpgSupported = (codecIndex == 0);
  QStandardItemModel* model = qobject_cast<QStandardItemModel*>(cbMuxerProg->model());
  if (model) {
    int mpgRow = indexForMuxerValue(0);  // 0 = mplex
    QStandardItem* mpgItem = model->item(mpgRow);
    if (mpgItem) {
      Qt::ItemFlags f = mpgItem->flags();
      mpgItem->setFlags(mpgSupported ? (f |  Qt::ItemIsEnabled)
                                     : (f & ~Qt::ItemIsEnabled));
    }
  }

  // Fetch stored preference for this codec.
  int preferred;
  switch (codecIndex) {
    case 0:  preferred = TTCut::mpeg2Muxer; break;
    case 1:  preferred = TTCut::h264Muxer;  break;
    case 2:  preferred = TTCut::h265Muxer;  break;
    default: preferred = 1;  // MKV
  }
  if (!mpgSupported && preferred == 0) {
    preferred = 1;  // MPG invalid for this codec → fall back to MKV
  }

  cbMuxerProg->setCurrentIndex(indexForMuxerValue(preferred));
  TTCut::outputContainer = preferred;

  // Update visibility (MPEG-2 Target only for mplex + MPEG-2)
  updateMuxerVisibility();
}
```

- [ ] **Step 7: Fix `updateMuxerVisibility()` (line 209-222)**

Under Approach A the combo's `currentIndex()` returns the display position, not the stored `outputContainer` value. The existing code uses display-index comparisons (`muxerIndex == 0` = mplex, `muxerIndex == 1` = MKV) which no longer hold.

Current body:
```cpp
void TTCutSettingsMuxer::updateMuxerVisibility()
{
  int muxerIndex = cbMuxerProg->currentIndex();

  // MPEG-2 Target is only relevant when:
  // 1. Using mplex (muxerIndex == 0)
  // 2. Encoder codec is MPEG-2 (TTCut::encoderCodec == 0)
  bool enableMpeg2Target = (muxerIndex == 0 && TTCut::encoderCodec == 0);
  cbMuxTarget->setEnabled(enableMpeg2Target);

  // MKV chapter settings only visible when using mkvmerge (index 1)
  bool enableMkvChapters = (muxerIndex == 1);
  gbMkvChapters->setVisible(enableMkvChapters);
}
```

Replace with (reads the stored value directly — display-order-agnostic):
```cpp
void TTCutSettingsMuxer::updateMuxerVisibility()
{
  // MPEG-2 Target is only relevant when:
  // 1. Using mplex (outputContainer == 0)
  // 2. Encoder codec is MPEG-2 (encoderCodec == 0)
  bool enableMpeg2Target = (TTCut::outputContainer == 0 && TTCut::encoderCodec == 0);
  cbMuxTarget->setEnabled(enableMpeg2Target);

  // MKV chapter settings only visible when using MKV (outputContainer == 1)
  bool enableMkvChapters = (TTCut::outputContainer == 1);
  gbMkvChapters->setVisible(enableMkvChapters);
}
```

- [ ] **Step 8: Sanity check — grep for expected symbols**

Run:
```bash
grep -n "muxerValueAt\|indexForMuxerValue\|QStandardItemModel" gui/ttcutsettingsmuxer.cpp
```
Expected: at least 6 matches (1 include, 2 impls, 3 usages in `initMuxProgList`/`setTabData`/`onEncoderCodecChanged`).

Run:
```bash
grep -n "cbMuxerProg->currentIndex\|cbMuxerProg->setCurrentIndex" gui/ttcutsettingsmuxer.cpp
```
Expected: all remaining `setCurrentIndex` calls must be wrapped in `indexForMuxerValue(...)`. No raw `currentIndex()` reads should be used as logical values (the only surviving `currentIndex()` reads, if any, should be display-index uses like inside the `onMuxerProgChanged(int index)` slot which already takes the index as parameter).

---

## Task 4: Update `data/ttavdata.cpp` — remove MP4 case

**Files:**
- Modify: `data/ttavdata.cpp:1567-1571` (comment block)
- Modify: `data/ttavdata.cpp:1661-1737` (delete `case 2:` block)

- [ ] **Step 1: Update the comment block before the switch**

Locate lines 1567-1571:
```cpp
  // Select muxer based on outputContainer setting
  // 0 = TS (Transport Stream, mplex)
  // 1 = MKV (mkvmerge)
  // 2 = MP4 (FFmpeg)
  // 3 = Elementary (no muxing)
```
Replace with:
```cpp
  // Select muxer based on outputContainer setting
  // 0 = MPG (mplex)
  // 1 = MKV (libav matroska muxer)
  // 3 = Elementary (no muxing; not reachable from UI, kept as defensive default)
```

- [ ] **Step 2: Locate the `case 2: // MP4 - use FFmpeg` block**

Run:
```bash
grep -n "case 2:" data/ttavdata.cpp
```
Expected (relevant line):
```
1661:    case 2: // MP4 - use FFmpeg
```

Run (to see the full block's end):
```bash
grep -n "case 3:\|case 2:" data/ttavdata.cpp
```
Expected: line 1661 (case 2) and line 1739 (case 3). The block to delete runs from line 1661 to just before line 1739.

- [ ] **Step 3: Delete the entire `case 2:` block**

Open `data/ttavdata.cpp`. Remove the lines starting at `case 2: // MP4 - use FFmpeg` up to (but not including) `case 3: // Elementary - no muxing`. This removes approximately 77 lines (1661-1737 inclusive).

After the edit, the switch should look like:
```cpp
  switch (TTCut::outputContainer) {
    case 1: // MKV - use mkvmerge
      {
        // ... (unchanged MKV block) ...
      }
      break;

    case 3: // Elementary - no muxing
      qDebug() << "Elementary output selected, skipping muxing";
      break;

    case 0: // TS - use mplex (default, existing behavior)
    default:
      {
        // ... (unchanged mplex block) ...
      }
      break;
  }
```

- [ ] **Step 4: Verify**

Run:
```bash
grep -n "MP4\|FFmpeg\|\.mp4" data/ttavdata.cpp
```
Expected: empty output or only unrelated matches (e.g. in surrounding comments). No references to `case 2`, `mp4Output`, `ffmpegArgs`, or "MP4 - use FFmpeg" should remain.

Run:
```bash
grep -n "case [0-3]:" data/ttavdata.cpp | grep -v "outputContainer\|roiX\|DAR"
```
to confirm the switch now has only `case 0`, `case 1`, `case 3`, and `default`. (There are unrelated `case 2:` lines in `ttlogodetector.cpp` and `tttranscode.cpp` — those stay untouched.)

---

## Task 5: Update `gui/ttcutavcutdlg.cpp` — remove dead MP4 extension branch

**Files:**
- Modify: `gui/ttcutavcutdlg.cpp:196-202`

- [ ] **Step 1: Locate the dead block**

Run:
```bash
grep -n -A 7 "outputContainer == 2" gui/ttcutavcutdlg.cpp
```
Expected:
```
196:  } else if (TTCut::outputContainer == 2) {
197:    // MP4/TS output via ffmpeg - extension is based on codec
...
```

- [ ] **Step 2: Delete the dead branch**

Current block (lines ~187-206):
```cpp
  if (TTCut::outputContainer == 1) {
    // MKV output - extension is .mkv, video will be .h264/.h265/.m2v
    if (TTCut::encoderCodec == 1) {
      expectedExt = "h264";
    } else if (TTCut::encoderCodec == 2) {
      expectedExt = "h265";
    } else {
      expectedExt = "m2v";
    }
  } else if (TTCut::outputContainer == 2) {
    // MP4/TS output via ffmpeg - extension is based on codec
    if (TTCut::encoderCodec == 1 || TTCut::encoderCodec == 2) {
      expectedExt = "ts";  // TS container for H.264/H.265
    } else {
      expectedExt = "m2v";
    }
  } else {
    // mplex (MPEG-2 only) - always .m2v
    expectedExt = "m2v";
  }
```

Replace with:
```cpp
  if (TTCut::outputContainer == 1) {
    // MKV output - extension is .mkv, video will be .h264/.h265/.m2v
    if (TTCut::encoderCodec == 1) {
      expectedExt = "h264";
    } else if (TTCut::encoderCodec == 2) {
      expectedExt = "h265";
    } else {
      expectedExt = "m2v";
    }
  } else {
    // mplex (MPEG-2 only) - always .m2v
    expectedExt = "m2v";
  }
```

- [ ] **Step 3: Verify**

Run:
```bash
grep -n "outputContainer == 2\|ts.*TS container\|MP4/TS" gui/ttcutavcutdlg.cpp
```
Expected: empty output.

---

## Task 6: Update `gui/ttcutsettings.cpp` — legacy value migration

**Files:**
- Modify: `gui/ttcutsettings.cpp:192` (append migration right after)

- [ ] **Step 1: Locate the outputContainer read line**

Run:
```bash
grep -n "OutputContainer\|H264Muxer\|H265Muxer\|Mpeg2Muxer" gui/ttcutsettings.cpp
```
Expected read-side lines:
```
150:  TTCut::mpeg2Muxer     = value( "Mpeg2Muxer/",     TTCut::mpeg2Muxer ).toInt();
156:  TTCut::h264Muxer      = value( "H264Muxer/",      TTCut::h264Muxer ).toInt();
162:  TTCut::h265Muxer      = value( "H265Muxer/",      TTCut::h265Muxer ).toInt();
192:  TTCut::outputContainer = value( "OutputContainer/", TTCut::outputContainer ).toInt();
```

- [ ] **Step 2: Add migration block after line 192**

Insert directly after line 192 (before the next `value(...)` call on line 193):
```cpp
  // Legacy migration: the MP4 option (value 2) was removed.
  // Any of the four related preferences still holding 2 → remap to MKV (1).
  if (TTCut::outputContainer == 2) TTCut::outputContainer = 1;
  if (TTCut::mpeg2Muxer     == 2) TTCut::mpeg2Muxer     = 1;
  if (TTCut::h264Muxer      == 2) TTCut::h264Muxer      = 1;
  if (TTCut::h265Muxer      == 2) TTCut::h265Muxer      = 1;
```

- [ ] **Step 3: Verify**

Run:
```bash
grep -n "Legacy migration\|== 2" gui/ttcutsettings.cpp
```
Expected: at least 5 matches — one header comment and four migration lines.

---

## Task 7: Full rebuild

**Files:** (build artefacts only)

- [ ] **Step 1: Clean and rebuild**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: build completes with exit status 0, produces `./ttcut-ng`. No errors. Warnings acceptable.

- [ ] **Step 2: Verify binary**

Run:
```bash
ls -la ttcut-ng
```
Expected: recent modification time.

---

## Task 8: Manual GUI verification

Six scenarios from the spec. Use `QT_QPA_PLATFORM=xcb ./ttcut-ng` to launch. Use the same ES/AC3 test files as the prior cut-video-name fix (MPEG-2 `.m2v`, H.264 `.264`, H.265 `.265`).

- [ ] **Step 1: Baseline — current QSettings**

Before testing, optionally record existing state:
```bash
find ~/.config -name 'ttcut-ng.conf' -o -name 'ttcut-ng*.ini' 2>/dev/null | \
  xargs grep -E 'OutputContainer|H264Muxer|H265Muxer|Mpeg2Muxer' 2>/dev/null || echo "No stored prefs"
```

- [ ] **Step 2: Scenario 1 — MPEG-2 source**

1. Launch `QT_QPA_PLATFORM=xcb ./ttcut-ng`.
2. File → Open Video → select an MPEG-2 `.m2v`.
3. Open the Cut dialog → go to the Muxing tab.

Expected:
- Combo first row: `MKV (libav)` (highlighted or default).
- Combo second row: `MPG (mplex)`.
- Both rows selectable.

Cancel the dialog.

- [ ] **Step 3: Scenario 2 — H.264 source**

1. File → New (or close current project), then Open Video → select an H.264 `.264`.
2. Open Cut dialog → Muxing tab.

Expected:
- Combo shows both rows in the same display order.
- `MPG (mplex)` row is **greyed out** and cannot be selected (click attempt: nothing happens).
- Current selection: `MKV (libav)`.

Cancel.

- [ ] **Step 4: Scenario 3 — H.265 source**

Same as Scenario 2 but with an H.265 `.265` file. Expected: identical behaviour to Scenario 2 — `MPG (mplex)` greyed out.

- [ ] **Step 5: Scenario 4 — legacy QSettings migration**

1. Close `ttcut-ng`.
2. Edit the config file manually (use the path discovered in Step 1):
   ```bash
   sed -i 's|^OutputContainer=.*$|OutputContainer=2|' <conf-path>
   ```
   Also set `H264Muxer=2` if present, to exercise that migration line:
   ```bash
   sed -i 's|^H264Muxer=.*$|H264Muxer=2|' <conf-path>
   ```
3. Relaunch `QT_QPA_PLATFORM=xcb ./ttcut-ng`.
4. Open Video (H.264), open Cut dialog → Muxing tab.

Expected:
- Combo selection is `MKV (libav)` (migrated from `2`).
- No crash, no complaint in the log.

Cancel.

- [ ] **Step 6: Scenario 5 — end-to-end cut**

Run one full cut in each working combination:
1. MPEG-2 source + `MPG (mplex)` → expect `.mpg` file on disk in `cutDirPath`.
2. MPEG-2 source + `MKV (libav)` → expect `.mkv` on disk.
3. H.264 source + `MKV (libav)` (only selectable option) → expect `.mkv` on disk.

After each, check the output directory (`ls -la`) for the expected file.

- [ ] **Step 7: Scenario 6 — no MP4 output**

After all runs above, verify no `.mp4` files were created:
```bash
ls -la <cut-dir-path>/*.mp4 2>&1 | head
```
Expected: `ls: cannot access ...: No such file or directory` or similar — no MP4 output at all.

- [ ] **Step 8: Pass/fail**

If all scenarios pass, proceed to Task 9.
If any scenario fails: stop, do not commit; investigate.

---

## Task 9: Commit

**Files:** (all five modified files)

- [ ] **Step 1: Review the diff**

Run:
```bash
git diff --stat gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp data/ttavdata.cpp gui/ttcutavcutdlg.cpp gui/ttcutsettings.cpp
```
Expected: changes only in these five files; scale roughly matches the spec (muxer.cpp growing, ttavdata.cpp shrinking by ~77 lines).

Run:
```bash
git diff gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp data/ttavdata.cpp gui/ttcutavcutdlg.cpp gui/ttcutsettings.cpp
```
Inspect once more for typos, missed edits, unrelated hunks.

- [ ] **Step 2: Stage and commit**

Run:
```bash
git add gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp data/ttavdata.cpp gui/ttcutavcutdlg.cpp gui/ttcutsettings.cpp
git commit -m "$(cat <<'EOF'
Clean up Muxing tab: relabel combo, remove MP4 option, guard mplex

Combo entries are now "MKV (libav)" (display position 0) and
"MPG (mplex)" (display position 1). Display order decouples from the
stored TTCut::outputContainer via QComboBox::itemData(); internal values
remain 0=mplex / 1=MKV so no schema migration is needed for the switch
in ttavdata.cpp or the extension logic in ttcutavcutdlg.cpp.

The legacy "FFmpeg (MP4/TS)" option is removed because its behaviour
was inconsistent: for MPEG-2 sources it produced MP4; for H.264/H.265
sources the Smart-Cut path forces MKV regardless. MP4 conversion of an
MKV result is trivial post-hoc with ffmpeg -c copy.

When the current encoder codec is H.264 or H.265, the MPG (mplex) row
is disabled; any stored preference of 0 (mplex) for those codecs is
clamped to 1 (MKV) in onEncoderCodecChanged.

Legacy QSettings values of 2 (removed MP4 option) for outputContainer,
mpeg2Muxer, h264Muxer, h265Muxer are migrated to 1 (MKV) on load.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify repo state**

Run:
```bash
git log --oneline -3 && git status
```
Expected: new commit at HEAD on `fix/muxer-cleanup`, working tree clean except for pre-existing `TODO.md`.

---

## Rollback

If any step in Tasks 2-6 goes wrong and cannot be unwound inline:

```bash
git diff gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp data/ttavdata.cpp gui/ttcutavcutdlg.cpp gui/ttcutsettings.cpp
git checkout -- gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp data/ttavdata.cpp gui/ttcutavcutdlg.cpp gui/ttcutsettings.cpp
```

If verification in Task 8 fails after the build passed: return to the failing scenario with systematic debugging before attempting fixes.
