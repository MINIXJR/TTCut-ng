# PTS-Analyse Migration & Landezonen-Clustering Implementation Plan

> **Status: COMPLETED (2026-03-28)** — All tasks implemented and verified.

**Goal:** Replace ttcut-esrepair with a focused `ttcut-pts-analyze` tool, clean up dead repair code, and add configurable Landezonen clustering for extra-frame regions in TTCut-ng.

**Architecture:** New standalone C tool (`ttcut-pts-analyze`) extracted from ttcut-esrepair containing only PTS grid analysis. ttcut-demux calls it instead of ttcut-esrepair. TTCut-ng groups extra frames into clusters using configurable gap/offset settings and offers import as Landezonen.

**Tech Stack:** C17 (ttcut-pts-analyze), Bash (ttcut-demux), Qt5/C++ (TTCut-ng settings + clustering)

---

### Task 1: Create `ttcut-pts-analyze` Tool

Extract PTS analysis functions from `tools/ttcut-esrepair/ttcut-esrepair.c` into a clean, single-purpose C tool.

**Files:**
- Create: `tools/ttcut-pts-analyze/ttcut-pts-analyze.c`
- Create: `tools/ttcut-pts-analyze/Makefile`

- [x] **Step 1: Create Makefile**

```makefile
# tools/ttcut-pts-analyze/Makefile
CC      = gcc
CFLAGS  = -pipe -std=c17 -Wall -Wextra -O2
TARGET  = ttcut-pts-analyze

$(TARGET): ttcut-pts-analyze.c
	$(CC) $(CFLAGS) -o $@ $<

install:
	install -m 755 $(TARGET) /usr/bin/

clean:
	rm -f $(TARGET)

.PHONY: install clean
```

- [x] **Step 2: Create ttcut-pts-analyze.c**

Extract these functions verbatim from `tools/ttcut-esrepair/ttcut-esrepair.c`:
- Lines 44-51: TS constants (`TS_PACKET_SIZE`, `TS_SYNC_BYTE`, `PAT_PID`, stream type defines)
- Lines 67-74: `AccessUnit` struct
- Lines 99-128: `open_mmap()`, `close_mmap()`
- Lines 135-167: `ts_payload()`
- Lines 171-323: `find_video_pid()`
- Lines 331-339: `parse_pes_timestamp()`
- Lines 344-437: `find_vdr_segments()`
- Lines 442-598: `collect_access_units()`
- Lines 601-604: `PtsSortEntry` struct (used by `detect_extra_frames`)
- Lines 624-839: `detect_extra_frames()`

Write a new `main()` function:
- CLI: `ttcut-pts-analyze [--verbose] <file.ts>`
- Call `find_video_pid()` → `collect_access_units()` → `detect_extra_frames()`
- Output on stdout: `extra_frames=<comma-separated-indices>` (single line, only if extra frames found)
- Exit codes: 0 = clean, 1 = extra frames found, 2 = fatal error
- No `--log`, no `--ts-source`, no ES file argument, no codec detection, no decode test

The new `main()` should be approximately:

```c
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>

/* [paste TS constants, AccessUnit struct, all extracted functions here] */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [--verbose] <file.ts>\n\n"
        "Analyze TS file for extra frames via PTS grid analysis.\n\n"
        "Options:\n"
        "  --verbose, -v   Show progress on stderr\n"
        "  --help, -h      Show this help\n\n"
        "Output (stdout):\n"
        "  extra_frames=<comma-separated frame indices>\n"
        "  (empty if no extra frames detected)\n\n"
        "Exit codes:\n"
        "  0  Clean stream\n"
        "  1  Extra frames detected\n"
        "  2  Fatal error\n",
        prog);
}

int main(int argc, char *argv[])
{
    bool verbose = false;

    static struct option long_options[] = {
        {"verbose", no_argument, NULL, 'v'},
        {"help",    no_argument, NULL, 'h'},
        {NULL,      0,           NULL,  0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'v': verbose = true; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 2;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified.\n\n");
        print_usage(argv[0]);
        return 2;
    }

    const char *ts_file = argv[optind];

    /* Open and mmap TS file */
    const uint8_t *ts_data = NULL;
    int64_t ts_size = 0;
    if (open_mmap(ts_file, &ts_data, &ts_size) < 0)
        return 2;

    if (verbose)
        fprintf(stderr, "TS file: %s (%lld bytes)\n", ts_file, (long long)ts_size);

    /* Find video PID via PAT/PMT */
    int video_pid = find_video_pid(ts_data, ts_size);
    if (video_pid < 0) {
        fprintf(stderr, "Error: cannot detect video PID from TS.\n");
        close_mmap(ts_data, ts_size);
        return 2;
    }

    if (verbose)
        fprintf(stderr, "Video PID: 0x%04x (%d)\n", video_pid, video_pid);

    close_mmap(ts_data, ts_size);

    /* Collect access units (handles VDR multi-segment internally) */
    AccessUnit *aus = NULL;
    int au_count = 0;
    int64_t total_es_bytes = 0;

    if (collect_access_units(ts_file, video_pid,
                             &aus, &au_count, &total_es_bytes, verbose) < 0) {
        fprintf(stderr, "Error: PTS analysis failed\n");
        return 2;
    }

    if (verbose) {
        int with_dts = 0, with_pts = 0;
        for (int i = 0; i < au_count; i++) {
            if (aus[i].dts >= 0) with_dts++;
            if (aus[i].pts >= 0) with_pts++;
        }
        fprintf(stderr, "Access units: %d (DTS: %d, PTS: %d)\n",
                au_count, with_dts, with_pts);
    }

    /* Detect extra frames */
    int extra_count = detect_extra_frames(aus, au_count, verbose);

    if (extra_count <= 0) {
        if (verbose)
            fprintf(stderr, "No extra frames detected\n");
        free(aus);
        return 0;
    }

    /* Output comma-separated extra frame indices */
    printf("extra_frames=");
    bool first = true;
    for (int i = 0; i < au_count; i++) {
        if (aus[i].extra) {
            if (!first) printf(",");
            printf("%d", i);
            first = false;
        }
    }
    printf("\n");

    if (verbose)
        fprintf(stderr, "Extra frames detected: %d\n", extra_count);

    free(aus);
    return 1;
}
```

**Important:** The extracted functions from ttcut-esrepair.c must be copied verbatim — they are tested and working. Do NOT rewrite or refactor them. Only remove the `#include` lines for libav headers (`libavcodec`, `libavformat`, `libavutil`) and the `pthread.h` include since ttcut-pts-analyze doesn't use them.

Remove from extracted code:
- `MmapIOContext` struct (line 61-65, only used by decode-test functions)
- `Segment` struct (line 53-59, only used by decode-test)
- `codec_names[]` array (line 76-78, not needed)
- `CodecType` enum (line 36-41, not needed)
- Any forward declarations for functions not extracted

- [x] **Step 3: Build and test**

```bash
cd tools/ttcut-pts-analyze && make
```

Expected: compiles without warnings, produces `ttcut-pts-analyze` binary.

Test with known TS file (if available):
```bash
./ttcut-pts-analyze --verbose /path/to/test.ts
```

Expected: either `extra_frames=...` on stdout (exit 1) or nothing (exit 0).

- [x] **Step 4: Commit**

```bash
git add tools/ttcut-pts-analyze/
git commit -m "Add ttcut-pts-analyze: focused PTS grid analysis tool

Extracts PTS-based extra frame detection from ttcut-esrepair into
a single-purpose tool. No ES modification, no decode test, no
libav dependency."
```

---

### Task 2: Update ttcut-demux to Use ttcut-pts-analyze

Replace ttcut-esrepair invocation with ttcut-pts-analyze and remove all repair-region code.

**Files:**
- Modify: `tools/ttcut-demux/ttcut-demux`

- [x] **Step 1: Replace ttcut-esrepair invocation**

In `tools/ttcut-demux/ttcut-demux`, find the esrepair invocation block (around lines 618-691).

Replace the ttcut-esrepair call and all repair output parsing with:

```bash
# --- PTS analysis for extra frame detection ---
ES_EXTRA_FRAMES_CSV=""
if command -v ttcut-pts-analyze >/dev/null 2>&1; then
    if [ -n "$VERBOSE" ]; then
        echo "Running PTS analysis on $TS_SOURCE ..."
    fi
    set +e
    PTS_OUTPUT=$(ttcut-pts-analyze ${VERBOSE:+--verbose} "$TS_SOURCE" 2>&1)
    PTS_EXIT=$?
    set -e

    if [ "$PTS_EXIT" -eq 1 ]; then
        ES_EXTRA_FRAMES_CSV=$(echo "$PTS_OUTPUT" | grep -oP '^extra_frames=\K.*' || true)
        if [ -n "$VERBOSE" ] && [ -n "$ES_EXTRA_FRAMES_CSV" ]; then
            local count=$(echo "$ES_EXTRA_FRAMES_CSV" | tr ',' '\n' | wc -l)
            echo "PTS analysis: $count extra frames detected"
        fi
    elif [ "$PTS_EXIT" -eq 2 ]; then
        echo "Warning: PTS analysis failed (exit 2)" >&2
    fi
else
    echo "Warning: ttcut-pts-analyze not found, skipping PTS analysis" >&2
fi
```

**Important:** This block must be inside a function (check if the current code is inside a function to avoid the `local` keyword issue). If not inside a function, use a plain variable without `local`:

```bash
count=$(echo "$ES_EXTRA_FRAMES_CSV" | tr ',' '\n' | wc -l)
```

Remove ALL of these variables and their parsing code:
- `ES_REMOVED_SEGMENTS`, `ES_REMOVED_FRAMES`, `ES_FRAMES_BEFORE`, `ES_FRAMES_AFTER`
- `ES_REPAIR_POINTS`, `ES_REPAIR_METHOD`, `ES_EXTRA_FRAMES` (the count)
- `ES_EXTRA_FRAME_INDICES` array and the loop that fills it
- `ES_REPAIR_REGIONS` array and its parsing loop
- `REPAIR_OUTPUT`, `REPAIR_EXIT`, `REPAIR_LOG`

- [x] **Step 2: Update .info file writing**

Find the .info file writing section (around lines 1119-1161). Replace the repair data section with:

```bash
# Extra frame indices from PTS analysis
if [ -n "$ES_EXTRA_FRAMES_CSV" ]; then
    echo "" >> "$OUTPUT_INFO"
    echo "[warnings]" >> "$OUTPUT_INFO"
    echo "es_extra_frames=$ES_EXTRA_FRAMES_CSV" >> "$OUTPUT_INFO"
fi
```

Remove ALL of these .info keys:
- `es_repaired`, `es_removed_segments`, `es_removed_frames`
- `es_frames_before`, `es_frames_after`, `es_extra_frame_count`
- `es_repair_regions`, `es_repair_region_N` loop

- [x] **Step 3: Remove CONTAINER_VIDEO_DURATION PTS-mode special case**

Search for the PTS-mode duration preservation code (around where `ES_REPAIR_METHOD` was used):

```bash
# This condition is no longer needed — ttcut-pts-analyze never modifies the ES
# Remove: if [ "$ES_REPAIR_METHOD" != "pts" ]; then CONTAINER_VIDEO_DURATION=""; fi
```

Remove any conditional that referenced `ES_REPAIR_METHOD`.

- [x] **Step 4: Test**

```bash
# Dry run with a TS file to verify the flow
tools/ttcut-demux/ttcut-demux -e -v /path/to/test.ts
```

Verify:
1. `ttcut-pts-analyze` is called (visible in verbose output)
2. No reference to `ttcut-esrepair` in output
3. `.info` file contains `es_extra_frames=` if extras found
4. `.info` file does NOT contain `es_repaired`, `es_repair_region_*`, etc.

- [x] **Step 5: Commit**

```bash
git add tools/ttcut-demux/ttcut-demux
git commit -m "Replace ttcut-esrepair with ttcut-pts-analyze in ttcut-demux

Remove all repair-region code and simplify extra frame handling.
ttcut-pts-analyze outputs comma-separated indices directly."
```

---

### Task 3: Delete ttcut-esrepair

**Files:**
- Delete: `tools/ttcut-esrepair/` (entire directory)

- [x] **Step 1: Verify no remaining references**

```bash
grep -rn "ttcut-esrepair" tools/ --include="*.sh" --include="ttcut-demux"
grep -rn "ttcut-esrepair" . --include="*.cpp" --include="*.h" --include="*.md" --include="*.pro"
```

Expected: no hits in code files (may appear in spec/plan docs which is fine).

- [x] **Step 2: Delete directory**

```bash
rm -rf tools/ttcut-esrepair/
```

- [x] **Step 3: Commit**

```bash
git add -A tools/ttcut-esrepair/
git commit -m "Delete ttcut-esrepair (replaced by ttcut-pts-analyze)

The decode-test ES repair mode caused worse A/V desync than
leaving streams untouched. PTS grid analysis is now handled
by ttcut-pts-analyze."
```

---

### Task 4: Clean Up TTESInfo (Remove Repair Region Code)

Remove repair-related members and parsing from TTESInfo. Keep `mEsExtraFrames` and `countExtraFramesBefore()`. Keep `TTDecodeErrorRegion` (still used by legacy decode error warnings).

**Files:**
- Modify: `avstream/ttesinfo.h` (lines 132-139 accessors, lines 184-191 members)
- Modify: `avstream/ttesinfo.cpp` (lines 219-264 warnings parsing)

- [x] **Step 1: Remove repair members and accessors from ttesinfo.h**

Remove these members (lines 184-190, keep line 191 `mEsExtraFrames`):
```cpp
// REMOVE these:
bool mEsRepaired;
int mEsRemovedSegments;
int mEsRemovedFrames;
int mEsFramesBefore;
int mEsFramesAfter;
QList<TTDecodeErrorRegion> mEsRepairRegions;
```

Remove these accessors (lines 132-137, keep lines 138-139):
```cpp
// REMOVE these:
bool esRepaired() const { return mEsRepaired; }
int esRemovedSegments() const { return mEsRemovedSegments; }
int esRemovedFrames() const { return mEsRemovedFrames; }
int esFramesBefore() const { return mEsFramesBefore; }
int esFramesAfter() const { return mEsFramesAfter; }
QList<TTDecodeErrorRegion> esRepairRegions() const { return mEsRepairRegions; }
```

Keep:
```cpp
QList<int> esExtraFrames() const { return mEsExtraFrames; }
int countExtraFramesBefore(int frameIndex) const;
```

And keep `TTDecodeErrorRegion` struct (used by `mDecodeErrorRegions` for legacy warnings).

- [x] **Step 2: Simplify warnings section parsing in ttesinfo.cpp**

Replace the `[warnings]` parsing block (around lines 219-264) with simplified version that only parses extra frames:

```cpp
else if (section == "warnings") {
    // Parse extra frame indices (comma-separated list)
    QString extraFrameStr = values.value("es_extra_frames", "");
    if (!extraFrameStr.isEmpty()) {
        QStringList indices = extraFrameStr.split(',');
        for (const QString& idx : indices) {
            bool ok;
            int frameIdx = idx.trimmed().toInt(&ok);
            if (ok) mEsExtraFrames.append(frameIdx);
        }
        if (!mEsExtraFrames.isEmpty())
            qDebug() << "Loaded" << mEsExtraFrames.size() << "extra frame indices from .info";
    }
}
```

Remove parsing for: `es_repaired`, `es_removed_segments`, `es_removed_frames`, `es_frames_before`, `es_frames_after`, `es_repair_regions`, `es_repair_region_N`.

Also remove any constructor initialization of the removed members.

- [x] **Step 3: Build test**

```bash
cd /usr/local/src/TTCut-ng && make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles without errors. Any code referencing removed members will fail here (to be fixed in Task 5).

- [x] **Step 4: Commit**

```bash
git add avstream/ttesinfo.h avstream/ttesinfo.cpp
git commit -m "Remove repair region code from TTESInfo

Keep extra frame indices and countExtraFramesBefore() for audio
time correction. TTDecodeErrorRegion struct stays for legacy
decode error warnings."
```

---

### Task 5: Clean Up ttavdata.cpp (Remove Repair MessageBox, Add Extra-Frame Dialog)

Replace the repair-regions MessageBox with a new extra-frame clustering dialog.

**Files:**
- Modify: `data/ttavdata.cpp` (lines 374-419 repair MessageBox → extra-frame dialog)

- [x] **Step 1: Remove repair MessageBox**

Remove the entire `if (esInfo.esRepaired())` block (lines 374-419 in ttavdata.cpp). This includes:
- The `repairMsg` string construction
- The `QMessageBox` with "Import as Stream Points" button
- The `esRepairRegions()` usage and `TTDecodeErrorRegion` iteration

- [x] **Step 2: Add extra-frame clustering dialog**

After the extra frame loading code (around line 372, after `mExtraFrameIndices` is populated), add:

```cpp
// Show dialog if extra frames were detected (not on project reload)
if (!mExtraFrameIndices.isEmpty() && avItem) {
    TTVideoStream* vs = avItem->videoStream();
    double frameRate = vs ? vs->frameRate() : 25.0;
    int gapFrames = TTCut::extraFrameClusterGapSec * frameRate;
    int offsetFrames = TTCut::extraFrameClusterOffsetSec * frameRate;

    // Cluster extra frames by gap
    QList<TTStreamPoint> clusters;
    int clusterStart = mExtraFrameIndices.first();
    int clusterEnd = clusterStart;
    int clusterCount = 1;

    auto emitCluster = [&]() {
        int pos = qMax(0, clusterStart - offsetFrames);
        double durSec = (clusterEnd - clusterStart + 1) / frameRate;
        QString desc = QString("Defekte Frames: %1\u2013%2 (%3 Frames, %4s)")
            .arg(clusterStart).arg(clusterEnd)
            .arg(clusterCount).arg(durSec, 0, 'f', 1);
        clusters.append(TTStreamPoint(pos, StreamPointType::Error, desc));
    };

    for (int i = 1; i < mExtraFrameIndices.size(); ++i) {
        if (mExtraFrameIndices[i] - clusterEnd <= gapFrames) {
            clusterEnd = mExtraFrameIndices[i];
            clusterCount++;
        } else {
            emitCluster();
            clusterStart = mExtraFrameIndices[i];
            clusterEnd = clusterStart;
            clusterCount = 1;
        }
    }
    emitCluster();

    // Show dialog
    QString msg = tr("%1 defective frames in %2 groups detected.")
        .arg(mExtraFrameIndices.size())
        .arg(clusters.size());

    QMessageBox msgBox(QMessageBox::Warning,
                       tr("Defective Frames Detected"),
                       msg, QMessageBox::NoButton, TTCut::mainWindow);
    QPushButton* importBtn = msgBox.addButton(
        tr("Import as Stream Points"), QMessageBox::AcceptRole);
    msgBox.addButton(QMessageBox::Ok);
    msgBox.exec();

    if (msgBox.clickedButton() == importBtn) {
        emit vdrMarkersLoaded(clusters);
    }
}
```

**Important:** This dialog must only appear on first file open, NOT on project reload. Check if there's already a guard for this. The existing code at line 542-551 (onOpenVideoFinished) loads extra frames with a `mExtraFrameIndices.isEmpty()` guard, so project-reload won't re-trigger the dialog since `mExtraFrameIndices` is already populated.

However, the dialog code above is in the initial load path (around line 372), not in `onOpenVideoFinished()`. Verify which path runs during project load and ensure the dialog is only in the non-project-load path.

- [x] **Step 3: Build test**

```bash
cd /usr/local/src/TTCut-ng && rm obj/ttavdata.o && bear -- make -j$(nproc)
```

Expected: compiles without errors.

- [x] **Step 4: Functional test**

Load a video with extra frames (one that has an .info file with `es_extra_frames=`). Verify:
1. Dialog appears: "156 defective frames in N groups detected."
2. Clicking "Import as Stream Points" creates Landezonen
3. Each Landezone shows: "Defekte Frames: X–Y (N Frames, Zs)"
4. Landezone position is offset_seconds before first extra frame in cluster
5. Reloading from .prj does NOT show the dialog again

- [x] **Step 5: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Replace repair MessageBox with extra-frame clustering dialog

Groups extra frames into clusters using configurable gap threshold.
Each cluster becomes a Landezone with frame range description."
```

---

### Task 6: Add Clustering Settings to TTCut-ng

Add "Gruppierung defekter Frames" settings to the Common tab.

**Files:**
- Modify: `common/ttcut.h` (after line 189)
- Modify: `common/ttcut.cpp` (after line 189)
- Modify: `gui/ttcutsettingscommon.h` (add SpinBox members)
- Modify: `gui/ttcutsettingscommon.cpp` (add GroupBox + SpinBoxes + load/save)
- Modify: `gui/ttcutsettings.cpp` (add QSettings read/write, lines ~78 and ~260)

- [x] **Step 1: Add settings variables to ttcut.h**

After line 189 (`static bool spDetectAspectChange;`), add:

```cpp
   // --- Gruppierung defekter Frames ---
   static int extraFrameClusterGapSec;      // Cluster gap threshold (default 5s)
   static int extraFrameClusterOffsetSec;   // Start offset before cluster (default 2s)
```

- [x] **Step 2: Add defaults to ttcut.cpp**

After line 189 (`bool  TTCut::spDetectAspectChange = true;`), add:

```cpp
// --- Gruppierung defekter Frames ---
int TTCut::extraFrameClusterGapSec    = 5;
int TTCut::extraFrameClusterOffsetSec = 2;
```

- [x] **Step 3: Add QSettings read/write to ttcutsettings.cpp**

In the read section (after line 78, inside `/Common` group):

```cpp
  TTCut::extraFrameClusterGapSec    = value( "ExtraFrameClusterGap/",    TTCut::extraFrameClusterGapSec ).toInt();
  TTCut::extraFrameClusterOffsetSec = value( "ExtraFrameClusterOffset/", TTCut::extraFrameClusterOffsetSec ).toInt();
```

In the write section (after line 260, inside `/Common` group):

```cpp
  setValue( "ExtraFrameClusterGap/",    TTCut::extraFrameClusterGapSec );
  setValue( "ExtraFrameClusterOffset/", TTCut::extraFrameClusterOffsetSec );
```

- [x] **Step 4: Add SpinBox members to ttcutsettingscommon.h**

After line 57 (`QSpinBox*   sbQuickJumpInterval;`), add:

```cpp
    QSpinBox*   sbClusterGap;
    QSpinBox*   sbClusterOffset;
```

- [x] **Step 5: Add GroupBox + SpinBoxes to ttcutsettingscommon.cpp constructor**

After the AC3 acmod normalization checkbox block (line 74), before the preview hint (line 76), add:

```cpp
  // Gruppierung defekter Frames
  QGroupBox* gbCluster = new QGroupBox(tr("Gruppierung defekter Frames"), this);
  QGridLayout* clusterLayout = new QGridLayout(gbCluster);

  sbClusterGap = new QSpinBox(gbCluster);
  sbClusterGap->setRange(1, 30);
  sbClusterGap->setSuffix(tr(" Sekunden"));
  QLabel* lblClusterGap = new QLabel(tr("Gruppengroesse"), gbCluster);
  clusterLayout->addWidget(lblClusterGap, 0, 0);
  clusterLayout->addWidget(sbClusterGap, 0, 1);

  sbClusterOffset = new QSpinBox(gbCluster);
  sbClusterOffset->setRange(0, 10);
  sbClusterOffset->setSuffix(tr(" Sekunden"));
  QLabel* lblClusterOffset = new QLabel(tr("Start-Offset"), gbCluster);
  clusterLayout->addWidget(lblClusterOffset, 1, 0);
  clusterLayout->addWidget(sbClusterOffset, 1, 1);

  if (gl) {
    int rowCluster = gl->rowCount();
    gl->addWidget(gbCluster, rowCluster, 0, 1, 3);
  }
```

- [x] **Step 6: Add load/save in setTabData() and getTabData()**

In `setTabData()`, after line 143 (`sbQuickJumpInterval->setValue(...)`):

```cpp
  // Gruppierung defekter Frames
  sbClusterGap->setValue(TTCut::extraFrameClusterGapSec);
  sbClusterOffset->setValue(TTCut::extraFrameClusterOffsetSec);
```

In `getTabData()`, after line 178 (`TTCut::quickJumpIntervalSec = ...`):

```cpp
  // Gruppierung defekter Frames
  TTCut::extraFrameClusterGapSec    = sbClusterGap->value();
  TTCut::extraFrameClusterOffsetSec = sbClusterOffset->value();
```

- [x] **Step 7: Build and verify**

```bash
cd /usr/local/src/TTCut-ng && make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Run TTCut-ng, open Settings → Common. Verify:
1. "Gruppierung defekter Frames" GroupBox appears
2. "Gruppengroesse" SpinBox shows 5, range 1-30, suffix " Sekunden"
3. "Start-Offset" SpinBox shows 2, range 0-10, suffix " Sekunden"
4. Values persist after closing and reopening Settings

- [x] **Step 8: Commit**

```bash
git add common/ttcut.h common/ttcut.cpp gui/ttcutsettingscommon.h gui/ttcutsettingscommon.cpp gui/ttcutsettings.cpp
git commit -m "Add defective frame clustering settings to Common tab

Gruppengroesse (default 5s) and Start-Offset (default 2s) control
how extra frames are grouped into Landezonen clusters."
```

---

### Task 7: Full Integration Test and Final Cleanup

**Files:**
- Possibly modify: `data/ttavdata.cpp` (if integration issues found)

- [x] **Step 1: Build everything**

```bash
cd /usr/local/src/TTCut-ng && make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
cd tools/ttcut-pts-analyze && make
```

- [x] **Step 2: Install ttcut-pts-analyze**

User must copy to `/usr/bin/`:
```bash
# User runs: sudo cp tools/ttcut-pts-analyze/ttcut-pts-analyze /usr/bin/
# User runs: sudo cp tools/ttcut-demux/ttcut-demux /usr/bin/
```

- [x] **Step 3: End-to-end test with TS file**

1. Run demux: `ttcut-demux -e -v /path/to/recording.ts`
2. Verify `.info` file contains `es_extra_frames=` (no `es_repaired`, no `es_repair_region_*`)
3. Open ES in TTCut-ng
4. Verify dialog: "N defective frames in M groups detected"
5. Click "Import as Stream Points"
6. Verify Landezonen appear with correct descriptions ("Defekte Frames: X–Y (N Frames, Zs)")
7. Navigate to a Landezone — position should be 2s before first defective frame
8. Save project, reload — dialog should NOT reappear, Landezonen still present
9. Adjust cluster settings (Settings → Common), reopen file — clusters should reflect new settings

- [x] **Step 4: Verify audio time correction still works**

1. Make a cut in a region with extra frames
2. Preview — verify A/V sync
3. Final cut — verify A/V sync
4. Both should use `countExtraFramesBefore()` for corrected audio timing

- [x] **Step 5: Verify no references to ttcut-esrepair remain**

```bash
grep -rn "ttcut-esrepair" /usr/local/src/TTCut-ng/ --include="*.cpp" --include="*.h" --include="*.sh" --include="*.pro"
grep -rn "esRepaired\|esRemovedSegments\|esRemovedFrames\|esFramesBefore\|esFramesAfter\|esRepairRegions" /usr/local/src/TTCut-ng/ --include="*.cpp" --include="*.h"
```

Expected: no hits in source code.

- [x] **Step 6: Final commit (if any fixes needed)**

```bash
git add -A
git commit -m "Final integration fixes for PTS-analyze migration"
```
