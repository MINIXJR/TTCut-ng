# Pre-Flight Boundary Check Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Detect audio bursts and SPS changes at cut boundaries before cutting, and let the user shift cut points or accept.

**Architecture:** A shared `checkAudioBoundaries()` method analyzes RMS levels around each CutIn/CutOut via ffmpeg CLI. For H.264/H.265, `compareSPSAtBoundary()` compares SPS NAL raw bytes. Results are shown in a QMessageBox before the cut starts. Integration at two points: `onDoCut()` (final cut, all codecs) and `doCutPreview()` (preview, audio check only).

**Tech Stack:** Qt5 (QProcess, QMessageBox), ffmpeg CLI (astats filter), TTNaluParser (SPS access)

---

### Task 1: Add BoundaryIssue struct and method declarations

**Files:**
- Modify: `data/ttavdata.h:31-42` (add include + struct before class)
- Modify: `data/ttavdata.h:209` (add private method declarations)
- Modify: `extern/ttffmpegwrapper.h:236-238` (add static method)
- Modify: `extern/ttessmartcut.h:103-104` (add public method)

**Step 1: Add BoundaryIssue struct to ttavdata.h**

After the existing includes (line 43) and before the forward declarations (line 45), add:

```cpp
#include <QMessageBox>

// Pre-flight boundary check result
struct BoundaryIssue {
    int segmentIndex;        // Which segment (0-based)
    bool isCutOut;           // true=CutOut, false=CutIn
    int frameIndex;          // Current frame index
    double boundaryTime;     // Time in seconds

    // Audio burst
    bool hasAudioBurst;
    double burstRmsDb;       // RMS of burst chunk (dB)
    double contextRmsDb;     // Median RMS of surrounding chunks (dB)

    // SPS change (H.264/H.265 only)
    bool hasSPSChange;
};
```

**Step 2: Add private method declarations to TTAVData**

In `data/ttavdata.h`, after `doH264Cut()` declaration (line 209), add:

```cpp
    QList<BoundaryIssue> checkAudioBoundaries(const QString& audioFile,
                                               const QList<QPair<double, double>>& keepList,
                                               double frameRate);
    int showBoundaryDialog(QList<BoundaryIssue>& issues, TTCutList* cutList,
                           double frameRate);
```

**Step 3: Add detectAudioBurst to TTFFmpegWrapper**

In `extern/ttffmpegwrapper.h`, after `cutAudioStream()` declaration (line 238), add:

```cpp
    // Detect audio burst near a boundary (returns true if burst found)
    // Sets burstRmsDb and contextRmsDb if burst detected
    static bool detectAudioBurst(const QString& audioFile, double boundaryTime,
                                  bool isCutOut, double& burstRmsDb, double& contextRmsDb);
```

**Step 4: Add compareSPSAtBoundary to TTESSmartCut**

In `extern/ttessmartcut.h`, after `analyzeCutPoints()` declaration (line 104), add:

```cpp
    // Check if SPS changes between frame and frame+1 (or frame-1 for cutIn)
    bool hasSPSChangeAtBoundary(int frameIndex, bool isCutOut) const;
```

**Step 5: Build to verify header changes compile**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

Expected: Linker errors for unimplemented methods (that's OK, confirms headers are correct).
If there are compile errors in headers, fix them before proceeding.

**Step 6: Commit**

```bash
git add data/ttavdata.h extern/ttffmpegwrapper.h extern/ttessmartcut.h
git commit -m "Add boundary check structs and method declarations"
```

---

### Task 2: Implement detectAudioBurst() in TTFFmpegWrapper

**Files:**
- Modify: `extern/ttffmpegwrapper.cpp` (add method at end, before closing)

**Step 1: Implement detectAudioBurst()**

Add at the end of `extern/ttffmpegwrapper.cpp` (before the last closing brace or at EOF):

```cpp
bool TTFFmpegWrapper::detectAudioBurst(const QString& audioFile, double boundaryTime,
                                        bool isCutOut, double& burstRmsDb, double& contextRmsDb)
{
    // Analyze 200ms window around boundary
    // For CutOut: check last 200ms before boundary
    // For CutIn: check first 200ms after boundary
    double windowStart, windowEnd;
    if (isCutOut) {
        windowStart = qMax(0.0, boundaryTime - 0.200);
        windowEnd   = boundaryTime + 0.032;  // +1 audio frame to catch leak
    } else {
        windowStart = qMax(0.0, boundaryTime - 0.032);
        windowEnd   = boundaryTime + 0.200;
    }

    QStringList args;
    args << "-v" << "error"
         << "-i" << audioFile
         << "-ss" << QString::number(windowStart, 'f', 6)
         << "-to" << QString::number(windowEnd, 'f', 6)
         << "-af" << "astats=metadata=1:reset=1536"
         << "-f" << "null" << "-";

    QProcess proc;
    proc.start("/usr/bin/ffmpeg", args);
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(10000)) {
        qDebug() << "detectAudioBurst: ffmpeg failed for" << audioFile;
        return false;
    }

    QString output = QString::fromUtf8(proc.readAllStandardError());

    // Parse RMS levels from astats output
    // Format: [Parsed_astats_0 ...] RMS level dB: -XX.XX
    QRegularExpression rmsRegex("RMS level dB:\\s*(-?[\\d.]+|inf|-inf)");
    QList<double> rmsValues;
    auto it = rmsRegex.globalMatch(output);
    while (it.hasNext()) {
        auto match = it.next();
        QString val = match.captured(1);
        if (val == "-inf" || val == "inf") continue;
        rmsValues.append(val.toDouble());
    }

    if (rmsValues.size() < 3) return false;

    // Calculate median RMS (excluding the boundary chunk)
    QList<double> sorted = rmsValues;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];

    // Check for burst: any chunk >20dB above median
    double maxRms = *std::max_element(rmsValues.begin(), rmsValues.end());

    // For CutOut: check last 2 chunks; for CutIn: check first 2 chunks
    int checkStart = isCutOut ? qMax(0, rmsValues.size() - 2) : 0;
    int checkEnd   = isCutOut ? rmsValues.size() : qMin(2, rmsValues.size());

    for (int i = checkStart; i < checkEnd; i++) {
        if (rmsValues[i] - median > 20.0 && rmsValues[i] > -40.0) {
            burstRmsDb = rmsValues[i];
            contextRmsDb = median;
            qDebug() << "detectAudioBurst: BURST at" << boundaryTime
                     << (isCutOut ? "CutOut" : "CutIn")
                     << "burst=" << burstRmsDb << "dB, context=" << contextRmsDb << "dB";
            return true;
        }
    }

    return false;
}
```

**Step 2: Build to verify**

```bash
rm obj/ttffmpegwrapper.o && make -j$(nproc)
```

Expected: Build succeeds (linker errors for other unimplemented methods are OK).

**Step 3: Commit**

```bash
git add extern/ttffmpegwrapper.cpp
git commit -m "Implement audio burst detection via ffmpeg astats"
```

---

### Task 3: Implement hasSPSChangeAtBoundary() in TTESSmartCut

**Files:**
- Modify: `extern/ttessmartcut.cpp` (add method)

**Step 1: Implement hasSPSChangeAtBoundary()**

Add after the existing `analyzeCutPoints()` method in `extern/ttessmartcut.cpp`:

```cpp
bool TTESSmartCut::hasSPSChangeAtBoundary(int frameIndex, bool isCutOut) const
{
    if (!mIsInitialized) return false;
    if (mParser.spsCount() <= 1) return false;  // Only 1 SPS in entire stream, no changes

    // Find the two frames to compare:
    // CutOut: compare cutOut frame vs cutOut+1 (first frame outside segment)
    // CutIn: compare cutIn frame vs cutIn-1 (last frame before segment)
    int frameA = frameIndex;
    int frameB = isCutOut ? frameIndex + 1 : frameIndex - 1;

    if (frameB < 0 || frameB >= mParser.accessUnitCount()) return false;

    // Find which SPS NAL is closest before each frame
    const auto& nalUnits = mParser.nalUnits();
    const auto& auA = mParser.accessUnitAt(frameA);
    const auto& auB = mParser.accessUnitAt(frameB);

    // Search backward from each AU's first NAL for the most recent SPS
    auto findActiveSPS = [&](const TTAccessUnit& au) -> int {
        int firstNal = au.nalIndices.isEmpty() ? 0 : au.nalIndices.first();
        for (int i = firstNal; i >= 0; i--) {
            if (nalUnits[i].isSPS) return i;
        }
        return -1;
    };

    int spsA = findActiveSPS(auA);
    int spsB = findActiveSPS(auB);

    if (spsA < 0 || spsB < 0) return false;
    if (spsA == spsB) return false;  // Same SPS NAL, no change

    // Different SPS NALs — compare raw data
    QByteArray dataA = const_cast<TTNaluParser&>(mParser).readNalData(spsA);
    QByteArray dataB = const_cast<TTNaluParser&>(mParser).readNalData(spsB);

    return dataA != dataB;
}
```

**Step 2: Build to verify**

```bash
rm obj/ttessmartcut.o && make -j$(nproc)
```

Expected: Build succeeds.

**Step 3: Commit**

```bash
git add extern/ttessmartcut.cpp
git commit -m "Implement SPS boundary comparison for aspect ratio detection"
```

---

### Task 4: Implement checkAudioBoundaries() and showBoundaryDialog()

**Files:**
- Modify: `data/ttavdata.cpp` (add two methods after `onDoCut()`, before `doH264Cut()`)

**Step 1: Implement checkAudioBoundaries()**

Add after `onDoCut()` (after line 942) in `data/ttavdata.cpp`:

```cpp
QList<BoundaryIssue> TTAVData::checkAudioBoundaries(const QString& audioFile,
                                                     const QList<QPair<double, double>>& keepList,
                                                     double frameRate)
{
    QList<BoundaryIssue> issues;
    if (audioFile.isEmpty()) return issues;

    for (int i = 0; i < keepList.size(); i++) {
        // Check CutIn (start of segment) — skip first segment's CutIn (start of recording)
        if (i > 0) {
            double cutInTime = keepList[i].first;
            int cutInFrame = qRound(cutInTime * frameRate);

            BoundaryIssue issue;
            issue.segmentIndex = i;
            issue.isCutOut = false;
            issue.frameIndex = cutInFrame;
            issue.boundaryTime = cutInTime;
            issue.hasAudioBurst = false;
            issue.hasSPSChange = false;

            if (TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false,
                                                   issue.burstRmsDb, issue.contextRmsDb)) {
                issue.hasAudioBurst = true;
                issues.append(issue);
            }
        }

        // Check CutOut (end of segment) — skip last segment's CutOut (end of recording)
        if (i < keepList.size() - 1) {
            double cutOutTime = keepList[i].second;
            int cutOutFrame = qRound(cutOutTime * frameRate) - 1;  // -1 because +1 was added

            BoundaryIssue issue;
            issue.segmentIndex = i;
            issue.isCutOut = true;
            issue.frameIndex = cutOutFrame;
            issue.boundaryTime = cutOutTime;
            issue.hasAudioBurst = false;
            issue.hasSPSChange = false;

            if (TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true,
                                                   issue.burstRmsDb, issue.contextRmsDb)) {
                issue.hasAudioBurst = true;
                issues.append(issue);
            }
        }
    }

    return issues;
}
```

**Step 2: Implement showBoundaryDialog()**

Add directly after `checkAudioBoundaries()`:

```cpp
int TTAVData::showBoundaryDialog(QList<BoundaryIssue>& issues, TTCutList* cutList,
                                  double frameRate)
{
    if (issues.isEmpty()) return QMessageBox::AcceptRole;

    QString msg = tr("Possible issues detected at cut boundaries:\n\n");

    for (const auto& issue : issues) {
        QString pos = issue.isCutOut ? tr("End") : tr("Start");
        msg += tr("Segment %1 %2 (Frame %3, %4s):\n")
            .arg(issue.segmentIndex + 1)
            .arg(pos)
            .arg(issue.frameIndex)
            .arg(issue.boundaryTime, 0, 'f', 1);

        if (issue.hasAudioBurst) {
            msg += tr("  Audio burst: %1 dB (context: %2 dB)\n")
                .arg(issue.burstRmsDb, 0, 'f', 1)
                .arg(issue.contextRmsDb, 0, 'f', 1);
        }
        if (issue.hasSPSChange) {
            msg += tr("  SPS change detected (possible aspect ratio change)\n");
        }
        msg += "\n";
    }

    QMessageBox box;
    box.setWindowTitle(tr("Boundary Check"));
    box.setText(msg);
    box.setIcon(QMessageBox::Warning);
    QPushButton* shiftBtn  = box.addButton(tr("Shift 1 Frame"),  QMessageBox::ActionRole);
    QPushButton* acceptBtn = box.addButton(tr("Accept"),         QMessageBox::AcceptRole);
    QPushButton* cancelBtn = box.addButton(tr("Cancel"),         QMessageBox::RejectRole);
    box.setDefaultButton(acceptBtn);
    box.exec();

    if (box.clickedButton() == cancelBtn) {
        return QMessageBox::RejectRole;
    }

    if (box.clickedButton() == shiftBtn) {
        // Adjust cut points: CutOut -= 1, CutIn += 1
        for (const auto& issue : issues) {
            TTCutItem item = cutList->at(issue.segmentIndex);
            if (issue.isCutOut) {
                int newCutOut = item.cutOutIndex() - 1;
                cutList->update(issue.segmentIndex, item.avDataItem(),
                                item.cutInIndex(), newCutOut);
                log->infoMsg(__FILE__, __LINE__,
                    QString("Boundary check: shifted CutOut segment %1: %2 -> %3")
                    .arg(issue.segmentIndex + 1).arg(item.cutOutIndex()).arg(newCutOut));
            } else {
                int newCutIn = item.cutInIndex() + 1;
                cutList->update(issue.segmentIndex, item.avDataItem(),
                                newCutIn, item.cutOutIndex());
                log->infoMsg(__FILE__, __LINE__,
                    QString("Boundary check: shifted CutIn segment %1: %2 -> %3")
                    .arg(issue.segmentIndex + 1).arg(item.cutInIndex()).arg(newCutIn));
            }
        }
    }

    return QMessageBox::AcceptRole;
}
```

**Step 3: Check if TTCutList::update() exists**

Before building, verify that `TTCutList` has an `update()` method. Search for it:

```bash
grep -n "update\|replace\|setItem\|modify" data/ttcutlist.h
```

If no `update()` method exists, we need to adjust the approach — possibly remove and re-insert, or modify the item directly. Check and adapt as needed.

**Step 4: Build to verify**

```bash
rm obj/ttavdata.o && make -j$(nproc)
```

Expected: Build succeeds.

**Step 5: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Implement boundary check and dialog for audio burst detection"
```

---

### Task 5: Integrate into onDoCut() (Final Cut, all codecs)

**Files:**
- Modify: `data/ttavdata.cpp:858-871` (add check before codec dispatch)

**Step 1: Add boundary check to onDoCut()**

In `data/ttavdata.cpp`, in method `onDoCut()`, after line 865 (`bool isH264H265 = ...`)
and before line 867 (`if (isH264H265)`), insert:

```cpp
  // Pre-flight boundary check: detect audio bursts at cut boundaries
  if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
    double frameRate = firstStream->frameRate();
    QString audioFile = cutList->at(0).avDataItem()->audioStreamAt(0)->filePath();

    // Build keepList for boundary analysis
    QList<QPair<double, double>> keepList;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      double cutInTime = item.cutInIndex() / frameRate;
      double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
      keepList.append(qMakePair(cutInTime, cutOutTime));
    }

    QList<BoundaryIssue> issues = checkAudioBoundaries(audioFile, keepList, frameRate);

    if (!issues.isEmpty()) {
      int result = showBoundaryDialog(issues, cutList, frameRate);
      if (result == QMessageBox::RejectRole) {
        emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
        return;
      }
    }
  }
```

**Step 2: Build to verify**

```bash
rm obj/ttavdata.o && make -j$(nproc)
```

Expected: Clean build.

**Step 3: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Integrate boundary check into final cut path (all codecs)"
```

---

### Task 6: Integrate into doCutPreview() (Preview, all codecs)

**Files:**
- Modify: `data/ttavdata.cpp:794-806` (add check before thread start)

**Step 1: Add boundary check to doCutPreview()**

In `data/ttavdata.cpp`, in method `doCutPreview()`, after line 797
(`cutPreviewTask = new TTCutPreviewTask(...)`) and before line 799
(`connect(cutPreviewTask, ...)`), insert:

```cpp
  // Pre-flight boundary check: detect audio bursts
  if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
    TTVideoStream* vStream = cutList->at(0).avDataItem()->videoStream();
    double frameRate = vStream->frameRate();
    QString audioFile = cutList->at(0).avDataItem()->audioStreamAt(0)->filePath();

    QList<QPair<double, double>> keepList;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      double cutInTime = item.cutInIndex() / frameRate;
      double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
      keepList.append(qMakePair(cutInTime, cutOutTime));
    }

    QList<BoundaryIssue> issues = checkAudioBoundaries(audioFile, keepList, frameRate);

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

**Step 2: Build to verify**

```bash
rm obj/ttavdata.o && make -j$(nproc)
```

Expected: Clean build.

**Step 3: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Integrate boundary check into preview path (all codecs)"
```

---

### Task 7: Add SPS check to doH264Cut()

**Files:**
- Modify: `data/ttavdata.cpp:1031-1039` (add SPS check after smartCut init)

**Step 1: Add SPS check after smartCut.initialize()**

In `doH264Cut()`, after `smartCut.initialize()` succeeds (after line 1031, before line 1033),
insert:

```cpp
    // SPS boundary check (H.264/H.265 only)
    for (int i = 0; i < cutFrames.size(); i++) {
      // Check CutOut (skip last segment)
      if (i < cutFrames.size() - 1) {
        if (smartCut.hasSPSChangeAtBoundary(cutFrames[i].second, true)) {
          log->warningMsg(__FILE__, __LINE__,
              QString("SPS change at CutOut segment %1 (frame %2) - possible aspect ratio change")
              .arg(i + 1).arg(cutFrames[i].second));
        }
      }
      // Check CutIn (skip first segment)
      if (i > 0) {
        if (smartCut.hasSPSChangeAtBoundary(cutFrames[i].first, false)) {
          log->warningMsg(__FILE__, __LINE__,
              QString("SPS change at CutIn segment %1 (frame %2) - possible aspect ratio change")
              .arg(i + 1).arg(cutFrames[i].first));
        }
      }
    }
```

Note: SPS changes are logged as warnings. They were already reported in the boundary
dialog if an audio burst was also detected. This is a supplementary check that catches
SPS-only changes (no audio burst). A full dialog for SPS-only is not implemented here
to keep complexity low — it can be added later if needed.

**Step 2: Build and verify full build**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

Expected: Clean build, binary `./ttcut-ng` exists.

**Step 3: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Add SPS boundary check for H.264/H.265 aspect ratio detection"
```

---

### Task 8: Manual test with Clouseau project

**Files:** None (manual testing)

**Step 1: Run ttcut-ng and open Clouseau project**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng
```

Open the Clouseau project file:
`/media/Daten/Video_Tmp/ProjectX_Temp/Inspektor_Clouseau_-_Der_irre_Flic_mit_dem_heißen_Blick_(1978).prj`

**Step 2: Test Preview**

Click Preview. Expected: boundary check dialog appears warning about audio burst at
segment 3 end (boundary 3→4). Click "Accept" to continue, verify preview plays.
Then test again with "Shift 1 Frame" to verify cutOut adjustment.

**Step 3: Test Final Cut**

Click Cut. Expected: same boundary check dialog. Test all three buttons:
- "Shift 1 Frame" → verify cut completes with adjusted boundary
- "Accept" → verify cut completes normally
- "Cancel" → verify cut is cancelled, returns to editor

**Step 4: Test with MPEG-2 file (if available)**

Open a MPEG-2 project and test cut/preview to verify the boundary check works
for MPEG-2 as well.

**Step 5: Run quality check on cut output**

```bash
python3 tools/ttcut-quality-check.py \
  "/media/Daten/Video_Tmp/Filme_cut/Inspektor_Clouseau_-_Der_irre_Flic_mit_dem_heißen_Blick_(1978).mkv" \
  "/media/Daten/Video_Tmp/ProjectX_Temp/Inspektor_Clouseau_-_Der_irre_Flic_mit_dem_heißen_Blick_(1978).264"
```

Expected: All tests PASS (especially Audio Waveform at boundary 3→4 should be clean
if "Shift 1 Frame" was used).

**Step 6: Commit (if any fixes were needed)**

```bash
git add -A && git commit -m "Fix boundary check issues found during testing"
```
