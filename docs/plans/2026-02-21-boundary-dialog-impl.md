# Boundary Burst Dialog Redesign — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the blocking pre-flight boundary dialog with integrated burst feedback in the cut list and preview dialog.

**Architecture:** Burst analysis runs via libav (`detectAudioBurst()`) at ~5ms per boundary. Results are shown as icons in the cut list tree view (immediate feedback when setting cuts) and as a warning with shift button in the preview dialog (where the user can hear the burst). The final cut shows a simple warning if unresolved bursts remain.

**Tech Stack:** Qt5 (QTreeWidget, QLabel, QPushButton), libav (already linked), existing TTFFmpegWrapper::detectAudioBurst()

**Design doc:** `docs/plans/2026-02-21-boundary-dialog-redesign.md`

---

### Task 1: Add `burstThresholdDb` setting

**Files:**
- Modify: `common/ttcut.h` (after line 165, h265Muxer)
- Modify: `common/ttcut.cpp` (after h265Muxer init, ~line 165)
- Modify: `gui/ttcutsettingscommon.h` (add spinbox pointer)
- Modify: `gui/ttcutsettingscommon.cpp` (setTabData/getTabData)
- Modify: `ui/ttcutsettingscommon.ui` (add spinbox widget)

**Step 1: Add static member to TTCut**

In `common/ttcut.h`, after `static int h265Muxer;` (~line 165), add:

```cpp
   // --- audio boundary detection ---
   static int burstThresholdDb;      // dB RMS threshold for burst detection (-30 default, 0=disabled)
```

In `common/ttcut.cpp`, after `int TTCut::h265Muxer = 1;` (~line 165), add:

```cpp
int TTCut::burstThresholdDb = -30;
```

**Step 2: Add spinbox to settings UI**

In `ui/ttcutsettingscommon.ui`, add a new row at the bottom of the form layout:
- QLabel: "Audio-Burst Schwellwert (dB)"
- QSpinBox named `sbBurstThreshold`, range -60 to 0, value -30, suffix " dB"

If modifying .ui is complex, add programmatically in `TTCutSettingsCommon` constructor:
```cpp
// In constructor, after setupUi(this):
sbBurstThreshold = new QSpinBox(this);
sbBurstThreshold->setRange(-60, 0);
sbBurstThreshold->setSuffix(" dB");
QLabel* lblBurst = new QLabel(tr("Audio-Burst Threshold (dB, 0=off)"), this);
// Add to existing layout at the end
QFormLayout* fl = qobject_cast<QFormLayout*>(layout());
if (fl) fl->addRow(lblBurst, sbBurstThreshold);
```

Declare in `gui/ttcutsettingscommon.h` private section:
```cpp
QSpinBox* sbBurstThreshold;
```

Add `#include <QSpinBox>` if not present.

**Step 3: Wire up setTabData/getTabData**

In `gui/ttcutsettingscommon.cpp`:

`setTabData()` — add at end:
```cpp
sbBurstThreshold->setValue(TTCut::burstThresholdDb);
```

`getTabData()` — add at end:
```cpp
TTCut::burstThresholdDb = sbBurstThreshold->value();
```

**Step 4: Build and verify**

```bash
rm -f obj/ttcut.o obj/ttcutsettingscommon.o && make -j$(nproc)
```

Run ttcut-ng, open Settings, verify spinbox appears with default -30.

**Step 5: Commit**

```bash
git add common/ttcut.h common/ttcut.cpp gui/ttcutsettingscommon.h gui/ttcutsettingscommon.cpp
git commit -m "Add burstThresholdDb setting for audio burst detection"
```

---

### Task 2: Add burst icon to cut list

**Files:**
- Modify: `gui/ttcuttreeview.h` (add helper method declaration)
- Modify: `gui/ttcuttreeview.cpp` (onAppendItem, onUpdateItem, column setup)

**Context:** TTCutTreeView has 5 columns (0-4). We add column 5 for burst status.
The `onAppendItem()` slot receives a `TTCutItem` with `avDataItem()` giving access
to audio streams. Column widths are set in the constructor (~lines 56-63).

**Step 1: Add column and helper method declaration**

In `gui/ttcuttreeview.h`, add in the private section:
```cpp
void updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item);
```

Add include at top of `gui/ttcuttreeview.cpp`:
```cpp
#include "../extern/ttffmpegwrapper.h"
#include "../common/ttcut.h"
```

**Step 2: Add column 5 header and width**

In constructor of `gui/ttcuttreeview.cpp` (~lines 56-63), where column widths
are set, add after the existing column width settings:

```cpp
videoCutList->headerItem()->setText(5, "");  // Burst status (no header text, just icon)
videoCutList->setColumnWidth(5, 30);         // Small column for icon
```

**Step 3: Implement updateBurstIcon helper**

In `gui/ttcuttreeview.cpp`, add:

```cpp
void TTCutTreeView::updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (!item.avDataItem() || item.avDataItem()->audioCount() == 0) {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
        return;
    }

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    if (!vStream) return;
    double frameRate = vStream->frameRate();
    QString audioFile = item.avDataItem()->audioStreamAt(0)->filePath();

    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
    double cutInTime  = item.cutInIndex() / frameRate;

    double burstDb = 0, contextDb = 0;
    bool hasCutOutBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, burstDb, contextDb);
    bool hasCutInBurst  = TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false, burstDb, contextDb);

    // Apply threshold filter
    int threshold = TTCut::burstThresholdDb;
    if (threshold != 0) {
        if (hasCutOutBurst && burstDb < threshold) hasCutOutBurst = false;
        if (hasCutInBurst && burstDb < threshold) hasCutInBurst = false;
    }

    if (hasCutOutBurst || hasCutInBurst) {
        treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxWarning));
        QString tip;
        if (hasCutOutBurst) tip += QString("Audio-Burst am Ende: %1 dB (Context: %2 dB)").arg(burstDb, 0, 'f', 1).arg(contextDb, 0, 'f', 1);
        if (hasCutInBurst) {
            if (!tip.isEmpty()) tip += "\n";
            tip += QString("Audio-Burst am Anfang: %1 dB (Context: %2 dB)").arg(burstDb, 0, 'f', 1).arg(contextDb, 0, 'f', 1);
        }
        treeItem->setToolTip(5, tip);
    } else {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
    }
}
```

**Important:** This helper runs detectAudioBurst twice (CutIn + CutOut), ~10ms total.
The burstDb/contextDb variables are overwritten by the second call. This is acceptable
because the tooltip only needs to show whether a burst exists, not details of both.
To fix properly: use separate variables for each call.

Actually, fix this properly — use separate variables:

```cpp
void TTCutTreeView::updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (!item.avDataItem() || item.avDataItem()->audioCount() == 0) {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
        return;
    }

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    if (!vStream) return;
    double frameRate = vStream->frameRate();
    QString audioFile = item.avDataItem()->audioStreamAt(0)->filePath();

    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
    double cutInTime  = item.cutInIndex() / frameRate;
    int threshold = TTCut::burstThresholdDb;

    double outBurstDb = 0, outContextDb = 0;
    bool hasCutOutBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, outBurstDb, outContextDb);
    if (hasCutOutBurst && threshold != 0 && outBurstDb < threshold) hasCutOutBurst = false;

    double inBurstDb = 0, inContextDb = 0;
    bool hasCutInBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false, inBurstDb, inContextDb);
    if (hasCutInBurst && threshold != 0 && inBurstDb < threshold) hasCutInBurst = false;

    if (hasCutOutBurst || hasCutInBurst) {
        treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxWarning));
        QString tip;
        if (hasCutOutBurst)
            tip += QString("Audio-Burst am Ende: %1 dB (Context: %2 dB)").arg(outBurstDb, 0, 'f', 1).arg(outContextDb, 0, 'f', 1);
        if (hasCutInBurst) {
            if (!tip.isEmpty()) tip += "\n";
            tip += QString("Audio-Burst am Anfang: %1 dB (Context: %2 dB)").arg(inBurstDb, 0, 'f', 1).arg(inContextDb, 0, 'f', 1);
        }
        treeItem->setToolTip(5, tip);
    } else {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
    }
}
```

**Step 4: Call from onAppendItem**

In `onAppendItem()`, after `treeItem->setText(4, offsetStr);` (~line 186), add:

```cpp
    updateBurstIcon(treeItem, item);
```

**Step 5: Call from onUpdateItem**

In `onUpdateItem()`, after `treeItem->setText(3, uitem.cutLengthString());` (~line 220), add:

```cpp
    updateBurstIcon(treeItem, uitem);
```

**Step 6: Build and verify**

```bash
rm -f obj/ttcuttreeview.o && make -j$(nproc)
```

Run ttcut-ng, load Clouseau project, set cuts. Verify warning icon appears on
Schnitt 3 (CutOut at frame 119661). Hover over icon to see tooltip.

**Step 7: Commit**

```bash
git add gui/ttcuttreeview.h gui/ttcuttreeview.cpp
git commit -m "Add audio burst warning icon to cut list"
```

---

### Task 3: Add burst warning and shift button to preview dialog

**Files:**
- Modify: `gui/ttcutpreview.h` (add members)
- Modify: `gui/ttcutpreview.cpp` (add widgets, burst check, shift logic)

**Context:** The preview dialog (`TTCutPreview`) has a combobox `cbCutPreview` for
cut selection and prev/next buttons. Layout is a QGridLayout with row 0 = video,
row 1 = controls. We add a burst warning between rows 0 and 1.

The combobox items are: "Start", "Cut 1-2", "Cut 2-3", ..., "End".
Preview files: `preview_001.mkv`, `preview_002.mkv`, etc.

`initPreview()` receives a `TTCutList*` which we need to store for shift access.

**Step 1: Add members to header**

In `gui/ttcutpreview.h`, add to private section:

```cpp
    // Burst warning
    QLabel*      lblBurstWarning;
    QPushButton* pbBurstShift;
    TTCutList*   mpCutList;        // stored for shift access
    int          mBurstSegmentIdx; // which segment has burst (-1 = none)
    bool         mBurstIsCutOut;   // true if burst is at CutOut

    void checkBurstForCurrentCut(int iCut);
```

Add to private slots:

```cpp
    void onBurstShift();
```

Add includes at top:
```cpp
#include <QLabel>
class TTCutList;
```

**Step 2: Create widgets in constructor**

In `gui/ttcutpreview.cpp` constructor, after `setupUi(this)` and before signal
connections, add:

```cpp
    // Burst warning widgets
    lblBurstWarning = new QLabel(this);
    lblBurstWarning->setStyleSheet("QLabel { color: #FF8C00; font-weight: bold; }");
    lblBurstWarning->hide();

    pbBurstShift = new QPushButton(tr("Shift -1 Frame"), this);
    pbBurstShift->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    pbBurstShift->hide();
    connect(pbBurstShift, SIGNAL(clicked()), this, SLOT(onBurstShift()));

    // Insert burst warning into layout (between video and controls)
    QHBoxLayout* burstLayout = new QHBoxLayout();
    burstLayout->addWidget(lblBurstWarning);
    burstLayout->addWidget(pbBurstShift);
    burstLayout->addStretch();

    // Insert at row 1 in the grid layout (pushing controls to row 2)
    QGridLayout* grid = qobject_cast<QGridLayout*>(layout());
    if (grid) {
        grid->addLayout(burstLayout, 1, 0);
    }

    mpCutList = nullptr;
    mBurstSegmentIdx = -1;
    mBurstIsCutOut = false;
```

**Step 3: Store cutList in initPreview**

In `initPreview()`, at the very beginning (after the existing code starts),
store the cut list:

```cpp
    mpCutList = cutList;
```

**Step 4: Implement checkBurstForCurrentCut**

The combobox items map to cut transitions. Item 0 = "Start" (before first cut),
item N = "Cut N-(N+1)" (transition between cuts), last item = "End".
Only middle items (1..numPreview-2) represent cut transitions where bursts matter.

For a transition "Cut i-(i+1)": the CutOut of segment i and CutIn of segment i+1
are the boundary points. In the cut list, segment i has CutOut at `cutList->at(i*2+1-1)`,
but the indexing is: pairs of (cutIn, cutOut). Wait — looking at initPreview:

```
numPreview = cutList->count()/2+1
item 0: "Start" — cutList->at(0).cutIn
item 1: "Cut 1-2" — cutList->at(1).cutOut / cutList->at(2).cutIn  (iPos = (1-1)*2+1 = 1)
item 2: "Cut 2-3" — cutList->at(3).cutOut / cutList->at(4).cutIn  (iPos = (2-1)*2+1 = 3)
...
```

Wait, that's not right. Let me re-read initPreview carefully. The cut list has
entries where each TTCutItem has cutIn and cutOut. For 4 segments there are 4
TTCutItems (count=4). numPreview = 4/2+1 = 3. Items: "Start", "Cut 1-2", "End".

Hmm, that doesn't match 4 segments producing 3 preview items. Let me re-read...

Actually, TTCutList has cut ENTRIES, not segments. Each entry is one cut
(cutIn, cutOut pair). For Clouseau with 4 segments, there are 4 entries in the
cut list: entry 0 (29-27505), entry 1 (36386-70765), entry 2 (78780-119661),
entry 3 (124902-162466).

So cutList->count() = 4, numPreview = 4/2+1 = 3. But that gives:
- Item 0: "Start" — entry 0 cutIn
- Item 1: "Cut 1-2" — entry 1 cutOut / entry 2 cutIn (iPos = 1)
- Item 2: "End" — entry 3 cutOut

Wait, that's only 3 preview items for 4 cut entries? That seems wrong. Let me
re-read initPreview more carefully...

Actually, looking at initPreview: `cutList->count()` returns the number of cut
entries. For the Clouseau project, there are 4 cut entries. `numPreview = 4/2+1 = 3`.

The preview shows transitions BETWEEN segments. With 4 segments there are 3
transitions: Start, Cut1-2 (=transition between segment1 and segment2), End.

Wait no, 4 segments means 3 inner boundaries plus start and end = 5 preview items?
Let me just look at what numPreview=3 gives:

- i=0 (first): "Start: HH:MM:SS" from cutList->at(0).cutInTime()
- i=1 (middle): "Cut 1-2: HH:MM:SS - HH:MM:SS" from cutList->at(1), cutList->at(2)
- i=2 (last): "End: HH:MM:SS" from cutList->at(1).cutOutTime() — wait, iPos=(2-1)*2+1=3

Hmm, this seems like the preview only shows 3 items for 4 cuts. The user said the
preview has transitions 1-2, 2-3, 3-4. So maybe cutList->count() is different
from what I think.

**For implementation: the burst check for combobox item `iCut` needs to figure out
which cut entries are involved. Use the same iPos calculation as initPreview.**

```cpp
void TTCutPreview::checkBurstForCurrentCut(int iCut)
{
    lblBurstWarning->hide();
    pbBurstShift->hide();
    mBurstSegmentIdx = -1;

    if (!mpCutList || mpCutList->count() < 2) return;

    int numPreview = mpCutList->count() / 2 + 1;

    // Only middle items have transitions to check
    if (iCut <= 0 || iCut >= numPreview - 1) return;

    // iPos points to the CutOut entry of the left segment
    int iPos = (iCut - 1) * 2 + 1;
    if (iPos >= mpCutList->count()) return;

    TTCutItem cutOutItem = mpCutList->at(iPos);
    if (!cutOutItem.avDataItem() || cutOutItem.avDataItem()->audioCount() == 0) return;

    double frameRate = cutOutItem.avDataItem()->videoStream()->frameRate();
    QString audioFile = cutOutItem.avDataItem()->audioStreamAt(0)->filePath();
    double cutOutTime = (cutOutItem.cutOutIndex() + 1) / frameRate;
    int threshold = TTCut::burstThresholdDb;

    double burstDb = 0, contextDb = 0;
    bool hasBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, burstDb, contextDb);

    if (hasBurst && threshold != 0 && burstDb < threshold) hasBurst = false;

    if (hasBurst) {
        lblBurstWarning->setText(QString("⚠ Audio-Burst am Ende von Schnitt %1 (%2 dB)")
            .arg(iCut).arg(burstDb, 0, 'f', 1));
        lblBurstWarning->show();
        pbBurstShift->show();
        mBurstSegmentIdx = iPos;
        mBurstIsCutOut = true;
    }

    // Also check CutIn of the right segment
    if (iPos + 1 < mpCutList->count()) {
        TTCutItem cutInItem = mpCutList->at(iPos + 1);
        double cutInTime = cutInItem.cutInIndex() / frameRate;
        double inBurstDb = 0, inContextDb = 0;
        bool hasInBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false, inBurstDb, inContextDb);
        if (hasInBurst && threshold != 0 && inBurstDb < threshold) hasInBurst = false;

        if (hasInBurst && !hasBurst) {
            lblBurstWarning->setText(QString("⚠ Audio-Burst am Anfang von Schnitt %1 (%2 dB)")
                .arg(iCut + 1).arg(inBurstDb, 0, 'f', 1));
            lblBurstWarning->show();
            pbBurstShift->show();
            pbBurstShift->setText(tr("Shift +1 Frame"));
            mBurstSegmentIdx = iPos + 1;
            mBurstIsCutOut = false;
        }
    }
}
```

**Step 5: Call from onCutSelectionChanged**

In `onCutSelectionChanged()`, at the end (after the existing code), add:

```cpp
    checkBurstForCurrentCut(iCut);
```

**Step 6: Implement onBurstShift**

```cpp
void TTCutPreview::onBurstShift()
{
    if (mBurstSegmentIdx < 0 || !mpCutList) return;
    if (mBurstSegmentIdx >= mpCutList->count()) return;

    const TTCutItem& currentItem = mpCutList->at(mBurstSegmentIdx);
    TTCutItem updatedItem(currentItem);

    if (mBurstIsCutOut) {
        updatedItem.update(currentItem.cutInIndex(), currentItem.cutOutIndex() - 1);
    } else {
        updatedItem.update(currentItem.cutInIndex() + 1, currentItem.cutOutIndex());
    }

    mpCutList->update(currentItem, updatedItem);

    // Inform user that preview needs regeneration
    QMessageBox::information(this, tr("Cut Shifted"),
        tr("Schnittpunkt wurde verschoben.\n"
           "Bitte Vorschau schließen und neu starten\n"
           "um das Ergebnis zu prüfen."));

    // Close preview
    close();
}
```

Add `#include <QMessageBox>` if not present.

**Step 7: Add includes to .cpp**

```cpp
#include "../extern/ttffmpegwrapper.h"
#include "../common/ttcut.h"
#include "../data/ttcutlist.h"
#include <QMessageBox>
#include <QHBoxLayout>
```

**Step 8: Build and verify**

```bash
rm -f obj/ttcutpreview.o && make -j$(nproc)
```

Load Clouseau project, click Preview. Navigate to "Cut 3-4". Verify burst warning
appears. Click "Shift -1 Frame" — verify dialog closes with info message.

**Step 9: Commit**

```bash
git add gui/ttcutpreview.h gui/ttcutpreview.cpp
git commit -m "Add burst warning and shift button to preview dialog"
```

---

### Task 4: Replace old pre-flight dialog with simple warning

**Files:**
- Modify: `data/ttavdata.cpp` (onDoCut, doCutPreview)

**Context:** Currently `onDoCut()` and `doCutPreview()` both call
`checkAudioBoundaries()` + `showBoundaryDialog()` before processing.
Replace with:
- `doCutPreview()`: remove pre-flight check entirely (burst info in preview instead)
- `onDoCut()`: simple warning listing cuts with bursts, "Trotzdem schneiden" / "Abbrechen"

**Step 1: Remove pre-flight from doCutPreview**

In `doCutPreview()` (~lines 800-824), remove the entire block:
```cpp
  // Pre-flight boundary check: detect audio bursts
  if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
    ...
    if (!issues.isEmpty()) {
      int result = showBoundaryDialog(issues, cutList, frameRate);
      if (result == QMessageBox::RejectRole) {
        delete cutPreviewTask;
        cutPreviewTask = 0;
        return;
      }
    }
  }
```

**Step 2: Replace pre-flight in onDoCut**

In `onDoCut()` (~lines 894-917), replace the existing pre-flight block with:

```cpp
  // Check for unresolved audio bursts
  if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
    double frameRate = firstStream->frameRate();
    QString audioFile = cutList->at(0).avDataItem()->audioStreamAt(0)->filePath();
    int threshold = TTCut::burstThresholdDb;

    QStringList burstWarnings;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
      double burstDb = 0, contextDb = 0;

      if (TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, burstDb, contextDb)) {
        if (threshold == 0 || burstDb >= threshold) {
          burstWarnings << tr("Schnitt %1: Audio-Burst am Ende (%2 dB)")
                           .arg(i + 1).arg(burstDb, 0, 'f', 1);
        }
      }
    }

    if (!burstWarnings.isEmpty()) {
      QString msg = tr("Folgende Schnitte haben erkannte Audio-Bursts:\n\n")
                  + burstWarnings.join("\n")
                  + tr("\n\nVorschau nutzen um zu prüfen ob Shift nötig ist.");

      int ret = QMessageBox::warning(TTCut::mainWindow, tr("Audio-Burst Warning"),
                    msg, tr("Trotzdem schneiden"), tr("Abbrechen"));
      if (ret == 1) {
        emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
        return;
      }
    }
  }
```

**Step 3: Build and verify**

```bash
rm -f obj/ttavdata.o && make -j$(nproc)
```

Test: Click "AV Schnitt" with Clouseau project. Verify simple warning appears
mentioning Schnitt 3 burst. Click "Trotzdem schneiden" → cut proceeds.
Click "Abbrechen" → cut cancelled.

Test Preview: Click Vorschau → should start immediately without pre-flight dialog.

**Step 4: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Replace pre-flight dialog with simple warning in final cut"
```

---

### Task 5: Remove dead code and clean up

**Files:**
- Modify: `data/ttavdata.h` (remove BoundaryIssue if unused, remove method declarations)
- Modify: `data/ttavdata.cpp` (remove showBoundaryDialog, checkAudioBoundaries)
- Modify: `extern/ttffmpegwrapper.cpp` (remove verbose debug timestamps)

**Step 1: Remove showBoundaryDialog and checkAudioBoundaries**

In `data/ttavdata.cpp`, delete the entire `showBoundaryDialog()` function
(~lines 1057-1126) and the `checkAudioBoundaries()` function (~lines 1000-1054).

In `data/ttavdata.h`, remove the private method declarations:
```cpp
    QList<BoundaryIssue> checkAudioBoundaries(const QString& audioFile,
                                               const QList<QPair<double, double>>& keepList,
                                               double frameRate);
    int showBoundaryDialog(QList<BoundaryIssue>& issues, TTCutList* cutList,
                           double frameRate);
```

Remove `BoundaryIssue` struct if no longer used anywhere. Check with grep first:
```bash
grep -r "BoundaryIssue" --include="*.cpp" --include="*.h"
```

If only declared in ttavdata.h and used in the deleted functions → remove it.

Also remove unused includes from `data/ttavdata.cpp` if they were only needed by
the deleted code (e.g., `#include <QPushButton>` if showBoundaryDialog was the
only user).

**Step 2: Clean up detectAudioBurst debug output**

In `extern/ttffmpegwrapper.cpp`, in `detectAudioBurst()`:
- Remove the verbose per-frame debug line (the one printing all timestamps+dB values)
- Keep the BURST/OK summary lines but at reduced verbosity
- Remove `rmsTimestamps` list (was only for debug)

Replace the debug block:
```cpp
    // Debug: show actual time range and per-frame RMS
    if (!rmsValues.isEmpty()) {
        QString dbgFrames;
        for (int i = 0; i < rmsValues.size(); i++) {
            dbgFrames += QString(...);
        }
        qDebug() << "detectAudioBurst:" << ...;
    }
```

With just:
```cpp
    // (debug output removed — uncomment for troubleshooting)
```

Also remove the `QList<double> rmsTimestamps;` declaration and all
`rmsTimestamps.append(frameTime);` calls.

**Step 3: Build and verify**

```bash
rm -f obj/ttavdata.o obj/ttffmpegwrapper.o && make -j$(nproc)
```

Verify grep shows no remaining references to deleted code:
```bash
grep -r "showBoundaryDialog\|checkAudioBoundaries" --include="*.cpp" --include="*.h"
```

**Step 4: Commit**

```bash
git add data/ttavdata.h data/ttavdata.cpp extern/ttffmpegwrapper.cpp
git commit -m "Remove old boundary dialog code and clean up debug output"
```
