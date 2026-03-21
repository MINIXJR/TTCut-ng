# AC3 acmod Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-encode AC3 frames at cut boundaries when acmod changes (stereo/5.1) to prevent player stutter from audio output reconfiguration.

**Architecture:** Detect acmod mismatches at CutIn/CutOut using existing AC3 header list, display as icon in cut list (like burst), and re-encode affected frames during audio cutting via libav.

**Tech Stack:** libavcodec/libavformat (AC3 decode/encode), Qt5 (UI)

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `common/ttcut.h/.cpp` | Modify | Add `normalizeAcmod` setting |
| `gui/ttcutsettings.cpp` | Modify | Load/save setting |
| `gui/ttcutsettingsencoder.cpp` | Modify | Add checkbox UI |
| `gui/ttcuttreeview.cpp` | Modify | Add acmod change icon (like burst) |
| `extern/ttffmpegwrapper.h/.cpp` | Modify | Add acmod detection helper + re-encode in cutAudioStream |
| `data/ttavdata.cpp` | Modify | Pass acmod info to cutAudioStream |

---

## Task 1: Add normalizeAcmod setting

**Files:**
- Modify: `common/ttcut.h`
- Modify: `common/ttcut.cpp`
- Modify: `gui/ttcutsettings.cpp`
- Modify: `gui/ttcutsettingsencoder.cpp`

- [ ] **Step 1: Add static bool to TTCut**

In `common/ttcut.h`, add after `burstThresholdDb`:
```cpp
static bool normalizeAcmod;
```

In `common/ttcut.cpp`, add initialization:
```cpp
bool TTCut::normalizeAcmod = true;
```

- [ ] **Step 2: Add load/save in ttcutsettings.cpp**

In `readSettings()`, in the `[Audio]` group (or create if needed):
```cpp
TTCut::normalizeAcmod = value("NormalizeAcmod/", TTCut::normalizeAcmod).toBool();
```

In `writeSettings()`:
```cpp
setValue("NormalizeAcmod/", TTCut::normalizeAcmod);
```

- [ ] **Step 3: Add checkbox in ttcutsettingsencoder.cpp**

In `setTabData()`:
```cpp
cbNormalizeAcmod->setChecked(TTCut::normalizeAcmod);
```

In `getTabData()`:
```cpp
TTCut::normalizeAcmod = cbNormalizeAcmod->isChecked();
```

Add the checkbox widget in the .ui file or programmatically, label:
`tr("Normalize AC3 channel format at cuts")`

- [ ] **Step 4: Build and verify**

```bash
rm -f obj/ttcut.o obj/ttcutsettings.o obj/ttcutsettingsencoder.o
bear -- make -j$(nproc)
```
Verify: Settings dialog shows checkbox, value persists.

- [ ] **Step 5: Commit**

---

## Task 2: Add acmod change detection helper

**Files:**
- Modify: `extern/ttffmpegwrapper.h`
- Modify: `extern/ttffmpegwrapper.cpp`

- [ ] **Step 1: Add acmod analysis struct and static helper**

In `ttffmpegwrapper.h`:
```cpp
struct TTAcmodInfo {
    int mainAcmod;       // Majority acmod of segment (-1 if not AC3)
    int cutInAcmod;      // acmod at CutIn position
    int cutOutAcmod;     // acmod at CutOut position
    double cutInChangeTime;   // Time where acmod changes to mainAcmod (0 if no change)
    double cutOutChangeTime;  // Time where acmod changes from mainAcmod (0 if no change)
};

static TTAcmodInfo analyzeAcmod(const QString& audioFile,
                                 double cutInTime, double cutOutTime);
```

- [ ] **Step 2: Implement analyzeAcmod()**

In `ttffmpegwrapper.cpp`:
- Open audio file, find AC3 stream
- Read all AC3 frames between cutInTime and cutOutTime
- Parse acmod from byte 6 bits 7-5 of each frame (same as TTAC3AudioHeader)
- Count acmod occurrences → determine mainAcmod (majority)
- Find first frame with mainAcmod after cutIn → cutInChangeTime
- Find last frame with mainAcmod before cutOut → cutOutChangeTime
- Return TTAcmodInfo struct

- [ ] **Step 3: Build and verify**

```bash
rm -f obj/ttffmpegwrapper.o
bear -- make -j$(nproc)
```

- [ ] **Step 4: Commit**

---

## Task 3: Add acmod icon in cut list

**Files:**
- Modify: `gui/ttcuttreeview.cpp`

- [ ] **Step 1: Add updateAcmodIcon() method**

Follow `updateBurstIcon()` pattern (line 576):
```cpp
void TTCutTreeView::updateAcmodIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (item.avDataItem()->audioCount() == 0) return;

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    double frameRate = vStream->frameRate();
    QString audioFile = item.avDataItem()->audioStreamAt(0)->filePath();

    double cutInTime = item.cutInIndex() / frameRate;
    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;

    TTAcmodInfo info = TTFFmpegWrapper::analyzeAcmod(audioFile, cutInTime, cutOutTime);
    if (info.mainAcmod < 0) return;  // Not AC3

    bool hasChange = (info.cutInAcmod != info.mainAcmod) ||
                     (info.cutOutAcmod != info.mainAcmod);
    if (!hasChange) return;

    // Set icon in column 6 (or reuse column 5 with combined text)
    QString text, tip;
    if (info.cutInAcmod != info.mainAcmod && info.cutOutAcmod != info.mainAcmod)
        text = tr("acmod: start+end");
    else if (info.cutInAcmod != info.mainAcmod)
        text = tr("acmod: start");
    else
        text = tr("acmod: end");

    tip = tr("AC3 channel format change: %1 → %2")
        .arg(AC3Mode[info.cutInAcmod]).arg(AC3Mode[info.mainAcmod]);

    treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxInformation));
    treeItem->setText(5, text);
    treeItem->setToolTip(5, tip);
}
```

- [ ] **Step 2: Call updateAcmodIcon() alongside updateBurstIcon()**

In the methods that call `updateBurstIcon()`, add `updateAcmodIcon()` call afterward.
Note: If burst and acmod both detected, decide priority (burst warning > acmod info).

- [ ] **Step 3: Build and verify**

Load a file with AC3 acmod changes, create cuts at boundaries, verify icon appears.

- [ ] **Step 4: Commit**

---

## Task 4: Implement acmod re-encoding in cutAudioStream

**Files:**
- Modify: `extern/ttffmpegwrapper.h`
- Modify: `extern/ttffmpegwrapper.cpp`

- [ ] **Step 1: Add normalizeAcmod parameter to cutAudioStream**

Extend signature:
```cpp
bool cutAudioStream(const QString& inputFile, const QString& outputFile,
                    const QList<QPair<double, double>>& cutList,
                    bool normalizeAcmod = false);
```

- [ ] **Step 2: Implement post-segment acmod normalization**

After stream-copying each segment in `cutAudioStream()`:
1. If `normalizeAcmod` is false, skip
2. Analyze the output segment for acmod changes (reuse analyzeAcmod logic)
3. If boundary frames have wrong acmod:
   - Close output file
   - Extract affected frames to temp file
   - Re-encode temp file with target channel layout:
     - `2/0` (stereo) → `3/2` (5.1): use `pan` filter or `-ac 6`
     - `3/2` (5.1) → `2/0` (stereo): use `-ac 2` downmix
   - Replace affected frames in output via concat stream-copy:
     - Write: good_start + re-encoded + good_end → final output

Alternative simpler approach: Re-encode the entire segment boundaries inline
during the stream-copy loop. When writing a frame with wrong acmod:
- Decode the AC3 frame
- Re-encode with target acmod/channels
- Write re-encoded frame instead of original

- [ ] **Step 3: Build and verify**

```bash
rm -f obj/ttffmpegwrapper.o
bear -- make -j$(nproc)
```

- [ ] **Step 4: Commit**

---

## Task 5: Wire acmod normalization into cut workflow

**Files:**
- Modify: `data/ttavdata.cpp`

- [ ] **Step 1: Pass normalizeAcmod to cutAudioStream calls**

In `doH264Cut()` and `doMpeg2Cut()`, change:
```cpp
ffmpeg.cutAudioStream(srcAudioFile, cutAudioFile, keepList)
```
to:
```cpp
ffmpeg.cutAudioStream(srcAudioFile, cutAudioFile, keepList, TTCut::normalizeAcmod)
```

- [ ] **Step 2: Build and full test**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Test with Navy CIS recording that has acmod changes:
- Preview should not stutter at acmod boundaries
- Final cut MKV should play without audio output reconfiguration

- [ ] **Step 3: Commit**
