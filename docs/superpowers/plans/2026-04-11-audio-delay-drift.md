# Audio Delay per Track + Audio-Drift Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Editable per-track audio delay in the audio list (applied during cutting and muxing), and read-only audio drift column in the cut list (boundary drift from preview).

**Architecture:** Two independent features sharing `TTESInfo` extensions. Item 7 adds a QSpinBox to the audio tree view and threads the delay value through audio cutting and MKV muxing. Item 8 renames the cut list column from "Audiooffset" to "Audio-Drift", shows "—" until preview, then displays the accumulated `localAudioOffset` from the first audio track.

**Tech Stack:** Qt5 (QSpinBox, QTreeWidget, signals/slots), libav (MKV muxer track delay), XML DOM (project file persistence)

**Spec:** `docs/superpowers/specs/2026-04-11-audio-delay-drift-design.md`

---

### Task 1: Extend TTAudioTrackInfo with trimmedMs and firstPts

**Files:**
- Modify: `avstream/ttesinfo.h:35-39`
- Modify: `avstream/ttesinfo.cpp:166-176`

- [ ] **Step 1: Extend TTAudioTrackInfo struct**

In `avstream/ttesinfo.h`, add fields to the struct:

```cpp
struct TTAudioTrackInfo {
    QString file;
    QString codec;
    QString language;
    double  firstPts;     // audio_N_first_pts from .info
    int     trimmedMs;    // audio_N_trimmed_ms from .info
};
```

- [ ] **Step 2: Parse new fields in TTESInfo::parseSection**

In `avstream/ttesinfo.cpp`, in the `section == "audio"` block (line ~170-176), after setting `track.language`:

```cpp
track.firstPts  = values.value(QString("audio_%1_first_pts").arg(i), "0").toDouble();
track.trimmedMs = values.value(QString("audio_%1_trimmed_ms").arg(i), "0").toInt();
```

- [ ] **Step 3: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles without errors.

- [ ] **Step 4: Verify parsing with test file**

Run TTCut-ng, open a video that has a .info file (e.g. from `/media/Daten/Video_Tmp/ProjectX_Temp/`). Check debug output for `Audio tracks:` — the values should now include the parsed trimmedMs.

Add temporary debug line in parseSection after the loop:

```cpp
qDebug() << "    Audio" << i << "trimmedMs:" << track.trimmedMs << "firstPts:" << track.firstPts;
```

- [ ] **Step 5: Commit**

```bash
git add avstream/ttesinfo.h avstream/ttesinfo.cpp
git commit -m "Parse per-track audio_N_trimmed_ms and first_pts from .info"
```

---

### Task 2: Change audioDelay from QString to int in TTAudioItem

**Files:**
- Modify: `data/ttaudiolist.h:76-81`
- Modify: `data/ttaudiolist.cpp:52-96,102-114,217-219`

- [ ] **Step 1: Change field type in header**

In `data/ttaudiolist.h`, change:

```cpp
// Old:
QString        audioDelay;

// New:
int            mAudioDelayMs;
```

Change the getter:

```cpp
// Old:
QString        getDelay() const;

// New:
int            getDelayMs() const;
void           setDelayMs(int ms);
```

- [ ] **Step 2: Update implementation**

In `data/ttaudiolist.cpp`:

Constructor (line ~52-59): change `audioDelay` initialization:
```cpp
// Old:
// (no audioDelay init — set in setItemData)

// New: add to constructor body
mAudioDelayMs = 0;
```

Copy constructor (line ~66-74): change:
```cpp
// Old:
audioDelay   = item.audioDelay;

// New:
mAudioDelayMs = item.mAudioDelayMs;
```

`setItemData()` (line ~79-97): remove the FIXME and old audioDelay line:
```cpp
// Remove these two lines:
// FIXME: use real delay value for audio delay
audioDelay  = "0";
```

`operator=` (line ~102-115): change:
```cpp
// Old:
audioDelay   = item.audioDelay;

// New:
mAudioDelayMs = item.mAudioDelayMs;
```

Replace `getDelay()` (line ~217-219):
```cpp
int TTAudioItem::getDelayMs() const
{
  return mAudioDelayMs;
}

void TTAudioItem::setDelayMs(int ms)
{
  mAudioDelayMs = ms;
}
```

- [ ] **Step 3: Update TTAudioTreeView to use new getter**

In `gui/ttaudiotreeview.cpp`, line 162:

```cpp
// Old:
treeItem->setText(6, item.getDelay());

// New:
treeItem->setText(6, QString("%1 ms").arg(item.getDelayMs()));
```

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles. Audio list shows "0 ms" in Delay column.

- [ ] **Step 5: Commit**

```bash
git add data/ttaudiolist.h data/ttaudiolist.cpp gui/ttaudiotreeview.cpp
git commit -m "Change audioDelay from QString to int, add setDelayMs()"
```

---

### Task 3: Add QSpinBox for editable Delay in TTAudioTreeView

**Files:**
- Modify: `gui/ttaudiotreeview.h:57-61`
- Modify: `gui/ttaudiotreeview.cpp:152-166,246-253`

- [ ] **Step 1: Add delayChanged signal**

In `gui/ttaudiotreeview.h`, add signal:

```cpp
signals:
    void openFile();
    void removeItem(int index);
    void swapItems(int oldIndex, int newIndex);
    void languageChanged(int index, const QString& language);
    void delayChanged(int index, int delayMs);
```

- [ ] **Step 2: Replace text with QSpinBox in onAppendItem**

In `gui/ttaudiotreeview.cpp`, replace line 162 in `onAppendItem`:

```cpp
// Old:
treeItem->setText(6, QString("%1 ms").arg(item.getDelayMs()));

// New:
QSpinBox* delaySpin = new QSpinBox();
delaySpin->setRange(-9999, 9999);
delaySpin->setSuffix(" ms");
delaySpin->setValue(item.getDelayMs());
audioListView->setItemWidget(treeItem, 6, delaySpin);

connect(delaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, delaySpin](int value) {
    for (int row = 0; row < audioListView->topLevelItemCount(); row++) {
        QTreeWidgetItem* item = audioListView->topLevelItem(row);
        if (audioListView->itemWidget(item, 6) == delaySpin) {
            emit delayChanged(row, value);
            break;
        }
    }
});
```

Add `#include <QSpinBox>` at the top of the file.

- [ ] **Step 3: Update onReloadList and onSwapItems**

`onSwapItems` already calls `onReloadList` which rebuilds the full list including QSpinBox widgets — same pattern as the QComboBox for language. No changes needed here.

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles. Audio list shows a spin box with "0 ms" in the Delay column.

- [ ] **Step 5: Commit**

```bash
git add gui/ttaudiotreeview.h gui/ttaudiotreeview.cpp
git commit -m "Add editable QSpinBox for per-track audio delay"
```

---

### Task 4: Wire delayChanged signal through TTAVItem to TTAudioItem

**Files:**
- Modify: `data/ttavlist.h` — add slot `onAudioDelayChanged(int index, int delayMs)`
- Modify: `data/ttavlist.cpp` — implement the slot
- Modify: `gui/ttaudiotreeview.cpp:93-122` — connect/disconnect `delayChanged`

- [ ] **Step 1: Add slot to TTAVItem**

In `data/ttavlist.h`, in the `public slots:` section of TTAVItem, add:

```cpp
void onAudioDelayChanged(int index, int delayMs);
```

- [ ] **Step 2: Implement the slot**

In `data/ttavlist.cpp`, add:

```cpp
void TTAVItem::onAudioDelayChanged(int index, int delayMs)
{
    if (index < 0 || index >= audioCount()) return;
    TTAudioItem item = audioListItemAt(index);
    item.setDelayMs(delayMs);
    mpAudioList->update(audioListItemAt(index), item);
}
```

- [ ] **Step 3: Connect signal in TTAudioTreeView**

In `gui/ttaudiotreeview.cpp`, in `onAVDataChanged`, add connect/disconnect for `delayChanged` alongside the existing `languageChanged` connections.

In the disconnect block (line ~103):
```cpp
disconnect(this, SIGNAL(delayChanged(int, int)), mpAVItem, SLOT(onAudioDelayChanged(int, int)));
```

In the connect block (line ~118):
```cpp
connect(this, SIGNAL(delayChanged(int, int)), mpAVItem, SLOT(onAudioDelayChanged(int, int)));
```

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles. Changing the spin box value updates the data model.

- [ ] **Step 5: Commit**

```bash
git add data/ttavlist.h data/ttavlist.cpp gui/ttaudiotreeview.cpp
git commit -m "Wire audio delay changes from UI through to data model"
```

---

### Task 5: Save/load audio delay in project file

**Files:**
- Modify: `data/ttcutprojectdata.cpp:83-91,177-195,247-267`

- [ ] **Step 1: Write delay in serializeAVDataItem**

In `data/ttcutprojectdata.cpp`, change `writeAudioSection` call (line ~90) to pass delay:

```cpp
writeAudioSection(video, aStream->filePath(), aItem.order(), aItem.getLanguage(), aItem.getDelayMs());
```

- [ ] **Step 2: Update writeAudioSection signature and body**

Change the method signature (line ~247):

```cpp
QDomElement TTCutProjectData::writeAudioSection(QDomElement& parent, const QString& filePath, int order, const QString& language, int delayMs)
```

Add delay element after the language block (after line ~264):

```cpp
if (delayMs != 0) {
    QDomElement delay = xmlDocument->createElement("Delay");
    audio.appendChild(delay);
    delay.appendChild(xmlDocument->createTextNode(QString::number(delayMs)));
}
```

Update the declaration in `data/ttcutprojectdata.h` accordingly.

- [ ] **Step 3: Read delay in parseAudioSection**

In `data/ttcutprojectdata.cpp`, `parseAudioSection` (line ~177-195):

After the language parsing block, add:

```cpp
int delayMs = 0;
int nextIdx = lang.isEmpty() ? 2 : 3;
if (audioNodesList.size() > nextIdx && audioNodesList.at(nextIdx).nodeName() == "Delay") {
    delayMs = audioNodesList.at(nextIdx).toElement().text().toInt();
}
```

After `setPendingAudioLanguage` call, add a similar pending delay mechanism:

```cpp
if (delayMs != 0) {
    avData->setPendingAudioDelay(avItem, order, delayMs);
}
```

- [ ] **Step 4: Add setPendingAudioDelay to TTAVData**

In `data/ttavdata.h`, add:
```cpp
void setPendingAudioDelay(TTAVItem* avItem, int order, int delayMs);
```

Add member:
```cpp
QMap<QPair<TTAVItem*, int>, int> mPendingAudioDelays;
```

In `data/ttavdata.cpp`, implement (alongside `setPendingAudioLanguage`):
```cpp
void TTAVData::setPendingAudioDelay(TTAVItem* avItem, int order, int delayMs)
{
    mPendingAudioDelays.insert(qMakePair(avItem, order), delayMs);
}
```

Apply pending delays in the same place where pending languages are applied (grep for `mPendingAudioLanguages` to find the application site and mirror the pattern for delays).

- [ ] **Step 5: Build, verify round-trip**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Test: Open project, set audio delay to 100ms, save project, close, re-open. Delay should show 100ms.

- [ ] **Step 6: Commit**

```bash
git add data/ttcutprojectdata.h data/ttcutprojectdata.cpp data/ttavdata.h data/ttavdata.cpp
git commit -m "Persist per-track audio delay in project file"
```

---

### Task 6: Apply audio delay in audio cutting (TTCutAudioTask)

**Files:**
- Modify: `data/ttcutaudiotask.h` — add `mDelayMs` member
- Modify: `data/ttcutaudiotask.cpp:53-63,81-125`
- Modify: caller that creates TTCutAudioTask (grep for `TTCutAudioTask` to find it)

- [ ] **Step 1: Add delay parameter to init()**

In `data/ttcutaudiotask.h`, add member `int mDelayMs;` and update `init()`:

```cpp
void init(QString tgtFilePath, TTCutList* cutList, int srcAudioIndex,
          TTMuxListDataItem* muxListItem, const QString& language, int delayMs = 0);
```

- [ ] **Step 2: Store delay in init()**

In `data/ttcutaudiotask.cpp`, `init()` (line ~53-62):

```cpp
mDelayMs = delayMs;
```

Constructor: `mDelayMs = 0;`

- [ ] **Step 3: Apply delay as time offset in operation()**

In `data/ttcutaudiotask.cpp`, `operation()` (line ~81-125), modify the getStartIndex/getEndIndex calls to include the delay as a time offset.

The delay shifts the audio cut window. A positive delay means audio should start later (shift audio cut start forward), negative means earlier:

```cpp
// Before the loop, convert delay to a frame offset factor
float delayFrames = (mDelayMs / 1000.0f) * frameRate;
```

Modify the getStartIndex/getEndIndex calls:

```cpp
int startIndex = mpCutStream->getStartIndex(cutItem.cutInIndex(),
    frameRate, localAudioOffset);
int endIndex = mpCutStream->getEndIndex(cutItem.cutOutIndex(),
    frameRate, localAudioOffset);

// Apply delay: shift audio indices
if (mDelayMs != 0) {
    float delayInAudioFrames = (mDelayMs / 1000.0f) / (mpCutStream->headerAt(0)->headerLength() /
        (float)mpCutStream->headerAt(0)->sampleRate());
    startIndex = qMax(0, startIndex + (int)round(delayInAudioFrames));
    endIndex = qMin(mpCutStream->headerList()->count() - 1,
                    endIndex + (int)round(delayInAudioFrames));
}
```

**Note:** The exact offset calculation depends on how `getStartIndex` works internally. The core idea is shifting the audio window by `delayMs` worth of audio frames. Review `TTAudioStream::getStartIndex()` to confirm the correct approach.

- [ ] **Step 4: Pass delay from caller**

Find where `TTCutAudioTask::init()` is called (grep for `TTCutAudioTask`) and pass the audio delay:

```cpp
// Get delay from audio item
int delayMs = avItem->audioListItemAt(i).getDelayMs();
task->init(tgtFilePath, cutList, i, muxListItem, language, delayMs);
```

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add data/ttcutaudiotask.h data/ttcutaudiotask.cpp
# Also add the caller file that was modified
git commit -m "Apply per-track audio delay offset during audio cutting"
```

---

### Task 7: Apply audio delay in MKV muxing

**Files:**
- Modify: `extern/ttmkvmergeprovider.h` — add per-track delay support
- Modify: `extern/ttmkvmergeprovider.cpp` — use per-track delay
- Modify: caller of TTMkvMergeProvider (grep for `TTMkvMergeProvider` in cut/preview code)

- [ ] **Step 1: Add per-track delay list to TTMkvMergeProvider**

In `extern/ttmkvmergeprovider.h`, add:

```cpp
void setAudioDelays(const QList<int>& delaysMs);
```

Add member:
```cpp
QList<int> mAudioDelaysMs;
```

- [ ] **Step 2: Implement setAudioDelays and apply in mux**

In `extern/ttmkvmergeprovider.cpp`:

```cpp
void TTMkvMergeProvider::setAudioDelays(const QList<int>& delaysMs)
{
    mAudioDelaysMs = delaysMs;
}
```

In the mux method, where `ain.syncMs` is set (line ~594), use per-track delay if available:

```cpp
// Old:
ain.syncMs = mAudioSyncOffsetMs;

// New:
int trackDelay = mAudioSyncOffsetMs;
if (audioInputIdx < mAudioDelaysMs.size()) {
    trackDelay += mAudioDelaysMs.at(audioInputIdx);
}
ain.syncMs = trackDelay;
```

(Where `audioInputIdx` is the 0-based index of the current audio track being added.)

- [ ] **Step 3: Pass delays from callers**

In the code that creates `TTMkvMergeProvider` for final cut and preview, collect delays:

```cpp
QList<int> audioDelays;
for (int i = 0; i < avItem->audioCount(); i++) {
    audioDelays.append(avItem->audioListItemAt(i).getDelayMs());
}
mkvProvider.setAudioDelays(audioDelays);
```

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 5: Commit**

```bash
git add extern/ttmkvmergeprovider.h extern/ttmkvmergeprovider.cpp
# Also add caller files
git commit -m "Apply per-track audio delay in MKV muxer"
```

---

### Task 8: Apply audio delay in preview audio cutting

**Files:**
- Modify: `data/ttcutpreviewtask.cpp:386-415`

- [ ] **Step 1: Apply delay offset to keepList times**

In `data/ttcutpreviewtask.cpp`, in the preview audio cut section (line ~394-400), apply the first audio track's delay to the keepList times:

```cpp
// After building keepList, apply audio delay if set
int audioDelayMs = 0;
if (avItem->audioCount() > 0) {
    audioDelayMs = avItem->audioListItemAt(0).getDelayMs();
}

QList<QPair<double, double>> keepList;
for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    double cutInTime = item.cutInIndex() / frameRate;
    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;

    if (audioDelayMs != 0) {
        double delaySeconds = audioDelayMs / 1000.0;
        cutInTime += delaySeconds;
        cutOutTime += delaySeconds;
        if (cutInTime < 0) cutInTime = 0;
    }

    keepList.append(qMakePair(cutInTime, cutOutTime));
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add data/ttcutpreviewtask.cpp
git commit -m "Apply audio delay offset in preview audio cutting"
```

---

### Task 9: Rename cut list column from "Audiooffset" to "Audio-Drift"

**Files:**
- Modify: `ui/cutlistwidget.ui:269-272`
- Modify: `trans/ttcut-ng_de_DE.ts` — update translation
- Modify: `trans/ttcut-ng_en_US.ts` — update translation

- [ ] **Step 1: Change column header in .ui file**

In `ui/cutlistwidget.ui`, line 271:

```xml
<!-- Old: -->
<string>Audiooffset</string>

<!-- New: -->
<string>Audio-Drift</string>
```

- [ ] **Step 2: Update translation files**

Run `lupdate` to update translation sources:

```bash
lupdate ttcut-ng.pro
```

Then update the German translation in `trans/ttcut-ng_de_DE.ts`:
- Source: `Audio-Drift`
- Translation: `Audio-Drift` (same — it's a technical term used in both languages)

- [ ] **Step 3: Show "—" as default instead of .info av_offset_ms**

In `gui/ttcuttreeview.cpp`, replace the `onAppendItem` offset logic (lines 178-191):

```cpp
// Old: Read av_offset_ms from .info file
// ...complex block reading TTESInfo...
// treeItem->setText(4, offsetStr);

// New: Show placeholder until preview calculates drift
treeItem->setText(4, QString::fromUtf8("\u2014"));  // em-dash "—"
treeItem->setToolTip(4, tr("Audio drift is calculated during preview (first audio track)"));
```

- [ ] **Step 4: Also update onUpdateItem to preserve drift column**

In `gui/ttcuttreeview.cpp`, `onUpdateItem` (line ~214-246): the method currently does not update column 4. This is correct — the drift value should persist until recalculated by preview.

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: Cut list column 4 now says "Audio-Drift" and shows "—" for all entries.

- [ ] **Step 6: Commit**

```bash
git add ui/cutlistwidget.ui gui/ttcuttreeview.cpp trans/ttcut-ng_de_DE.ts trans/ttcut-ng_en_US.ts
git commit -m "Rename cut list column to Audio-Drift, show placeholder until preview"
```

---

### Task 10: Display localAudioOffset after preview

**Files:**
- Modify: `data/ttcutpreviewtask.cpp` — emit drift values after audio cut
- Modify: `data/ttcutpreviewtask.h` — add signal for drift values
- Modify: `gui/ttcuttreeview.h` — add slot for receiving drift values
- Modify: `gui/ttcuttreeview.cpp` — update column 4 with drift values
- Connect signal/slot in the caller that sets up preview (grep for `previewCut` signal)

- [ ] **Step 1: Capture localAudioOffset per cut in preview**

In `data/ttcutpreviewtask.cpp`, in the MPEG-2 audio cut path or the libav audio cut path, capture `localAudioOffset` after each cut segment. For the libav path (which uses `cutAudioStream` with keepList), `localAudioOffset` is not directly available — it's internal to `TTAudioStream::getStartIndex/getEndIndex`.

Alternative approach: Calculate audio drift externally using the same formula as `getStartIndex`:

```cpp
// After preview audio cut, calculate drift per cut for first audio track
QList<float> audioDrifts;
if (hasAudio && avItem->audioCount() > 0) {
    TTAudioStream* firstAudio = avItem->audioStreamAt(0);
    float localOffset = 0.0f;
    for (int i = 0; i < cutList->count(); i++) {
        TTCutItem item = cutList->at(i);
        float fr = frameRate;
        firstAudio->getStartIndex(item.cutInIndex(), fr, localOffset);
        firstAudio->getEndIndex(item.cutOutIndex(), fr, localOffset);
        audioDrifts.append(localOffset);
    }
}
```

- [ ] **Step 2: Add signal for drift values**

In `data/ttcutpreviewtask.h`, add signal:

```cpp
signals:
    void audioDriftCalculated(const QList<float>& driftsMs);
```

Emit after calculating drifts in step 1.

- [ ] **Step 3: Add slot in TTCutTreeView**

In `gui/ttcuttreeview.h`, add slot:

```cpp
public slots:
    void onAudioDriftUpdated(const QList<float>& driftsMs);
```

In `gui/ttcuttreeview.cpp`:

```cpp
void TTCutTreeView::onAudioDriftUpdated(const QList<float>& driftsMs)
{
    for (int i = 0; i < driftsMs.size() && i < videoCutList->topLevelItemCount(); i++) {
        QTreeWidgetItem* treeItem = videoCutList->topLevelItem(i);
        float driftMs = driftsMs.at(i);
        treeItem->setText(4, QString("%1 ms").arg(driftMs, 0, 'f', 1));
    }
}
```

- [ ] **Step 4: Connect signal to slot**

Find where `previewCut` is handled and the preview task is created. Connect `audioDriftCalculated` to `onAudioDriftUpdated`.

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Test: Open video with audio, add cut points, run preview. After preview, column 4 should show drift values like "12.3 ms" instead of "—".

- [ ] **Step 6: Commit**

```bash
git add data/ttcutpreviewtask.h data/ttcutpreviewtask.cpp gui/ttcuttreeview.h gui/ttcuttreeview.cpp
# Also add any connector files
git commit -m "Display accumulated audio drift per cut after preview"
```

---

### Task 11: Update translations

**Files:**
- Modify: `trans/ttcut-ng_de_DE.ts`
- Modify: `trans/ttcut-ng_en_US.ts`

- [ ] **Step 1: Run lupdate**

```bash
lupdate ttcut-ng.pro
```

- [ ] **Step 2: Update translations**

New/changed strings to translate:
- `"Audio drift is calculated during preview (first audio track)"` → DE: `"Audio-Drift wird bei der Vorschau berechnet (erste Audiospur)"`

- [ ] **Step 3: Run lrelease**

```bash
lrelease ttcut-ng.pro
```

- [ ] **Step 4: Commit**

```bash
git add trans/ttcut-ng_de_DE.ts trans/ttcut-ng_en_US.ts trans/*.qm
git commit -m "Update translations for audio delay and drift features"
```

---

### Task 12: Remove temporary debug output from Task 1

**Files:**
- Modify: `avstream/ttesinfo.cpp`

- [ ] **Step 1: Remove debug line**

Remove the temporary `qDebug` line added in Task 1 Step 4.

- [ ] **Step 2: Build**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add avstream/ttesinfo.cpp
git commit -m "Remove temporary debug output from TTESInfo"
```
