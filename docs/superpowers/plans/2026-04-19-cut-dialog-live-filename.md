# Cut Dialog Live Filename — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Cut dialog's output filename field react live to the `_cut` suffix checkbox and to container/codec changes in the other tabs.

**Architecture:** A new private slot `TTCutAVCutDlg::updateOutputFilename()` rebuilds the field text (basename ± `_cut` suffix + current container extension) whenever the checkbox toggles or the Encoding/Muxing tabs emit their relevant signals. A new signal `TTCutSettingsMuxer::containerChanged(int)` fires on muxer combo changes. The codec-driven whitelist logic in `getCommonData()` is replaced by a trivial container-driven helper (`mpg` for mplex, `mkv` for libav) — consistent with the final on-disk filename produced by `ttavdata.cpp`.

**Tech Stack:** Qt 5 C++ (`QCheckBox::toggled`, `QLineEdit`, `QFileInfo`, signal/slot).

**Spec:** `docs/superpowers/specs/2026-04-19-cut-dialog-live-filename-design.md`

**Branch:** `fix/cut-dialog-live-filename` (to be created from master).

---

## File Structure

Four files touched, no new files:

- **`gui/ttcutsettingsmuxer.h`** — declare new `containerChanged(int)` signal alongside the existing `public slots:` (needs a `signals:` block added since the class currently has no signals).
- **`gui/ttcutsettingsmuxer.cpp`** — emit `containerChanged(value)` at the end of `onMuxerProgChanged`.
- **`gui/ttcutavcutdlg.h`** — add a private slot `updateOutputFilename()` and a private static helper `expectedContainerExtension(int)`.
- **`gui/ttcutavcutdlg.cpp`** — implement both, wire three new connects in the constructor, call `updateOutputFilename()` at the end of `setCommonData()`, simplify `getCommonData()`.

No unit-test files; UI-layer state with no existing coverage. Verification is manual (Task 6).

---

## Task 1: Create feature branch

- [ ] **Step 1: Verify clean master**

Run:
```bash
git status
```
Expected: `On branch master`, working tree clean (no staged/modified files). If anything is present — stop and investigate.

- [ ] **Step 2: Branch from master**

Run:
```bash
git checkout -b fix/cut-dialog-live-filename
git status
```
Expected: `Switched to a new branch 'fix/cut-dialog-live-filename'`, working tree clean.

---

## Task 2: Add `containerChanged(int)` signal to `TTCutSettingsMuxer`

**Files:**
- Modify: `gui/ttcutsettingsmuxer.h` (add `signals:` block)
- Modify: `gui/ttcutsettingsmuxer.cpp` (emit from `onMuxerProgChanged`)

- [ ] **Step 1: Inspect current header layout**

Run:
```bash
grep -n "public slots\|private\|protected slots\|signals:" gui/ttcutsettingsmuxer.h
```
Expected (post-muxer-cleanup state):
```
49:  public slots:
52:  private:
60:  protected slots:
```
No `signals:` block exists yet — we will add one.

- [ ] **Step 2: Add `signals:` block to the header**

Edit `gui/ttcutsettingsmuxer.h`. Insert the signals block **between** the `public slots:` block (ends at line 50, currently `void onEncoderCodecChanged(int codecIndex);`) and the `private:` block at line 52.

Before (lines 49-52):
```cpp
  public slots:
    void onEncoderCodecChanged(int codecIndex);

  private:
```

After:
```cpp
  public slots:
    void onEncoderCodecChanged(int codecIndex);

  signals:
    void containerChanged(int containerValue);

  private:
```

- [ ] **Step 3: Emit `containerChanged` from `onMuxerProgChanged`**

Locate `onMuxerProgChanged` in `gui/ttcutsettingsmuxer.cpp` (it now lives near line 221-234 after the muxer cleanup edits):

Current body:
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

Replace with:
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
  emit containerChanged(value);
}
```

- [ ] **Step 4: Verify**

Run:
```bash
grep -n "signals:\|containerChanged" gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp
```
Expected: one `signals:` line and one signal declaration in the header; one `emit containerChanged` in the cpp.

---

## Task 3: Declare slot and helper in `TTCutAVCutDlg` header

**Files:**
- Modify: `gui/ttcutavcutdlg.h` (add private slot and private static helper)

- [ ] **Step 1: Inspect current header layout**

Run:
```bash
grep -n "public:\|protected slots:\|private:" gui/ttcutavcutdlg.h
```
Expected:
```
59:  public:
67:  protected slots:
72:  private:
76:  private:
```
(There are two `private:` blocks in the existing header — the first for methods, the second for the member `log`. We add to the methods one at line 72.)

- [ ] **Step 2: Add declarations**

Edit `gui/ttcutavcutdlg.h`. Current `private:` block (lines 72-74):
```cpp
  private:
    void   getFreeDiskSpace();
    DfInfo getDiskSpaceInfo(QString path);
```

Replace with:
```cpp
  private:
    void   getFreeDiskSpace();
    DfInfo getDiskSpaceInfo(QString path);
    static QString expectedContainerExtension(int container);

  private slots:
    void updateOutputFilename();
```

- [ ] **Step 3: Verify**

Run:
```bash
grep -n "updateOutputFilename\|expectedContainerExtension" gui/ttcutavcutdlg.h
```
Expected: two matches — the slot declaration and the helper declaration.

---

## Task 4: Implement slot, helper, and constructor wiring in `TTCutAVCutDlg`

**Files:**
- Modify: `gui/ttcutavcutdlg.cpp` (constructor wiring, slot + helper implementations, `setCommonData`, `getCommonData`)

This is the largest task — all edits in one file, done in one go so the file compiles at the end.

- [ ] **Step 1: Extend constructor wiring**

Locate the constructor. After the muxer cleanup, the constructor ends with these lines:
```cpp
  // Initial sync based on current codec.
  muxingPage->onEncoderCodecChanged(TTCut::encoderCodec);
}
```

Append before the closing `}`:
```cpp
  // Live filename updates: suffix toggle, codec change, container change.
  connect(cbAddSuffix,  SIGNAL(toggled(bool)),           SLOT(updateOutputFilename()));
  connect(encodingPage, SIGNAL(codecChanged(int)),       SLOT(updateOutputFilename()));
  connect(muxingPage,   SIGNAL(containerChanged(int)),   SLOT(updateOutputFilename()));
```

The function now ends:
```cpp
  // Initial sync based on current codec.
  muxingPage->onEncoderCodecChanged(TTCut::encoderCodec);

  // Live filename updates: suffix toggle, codec change, container change.
  connect(cbAddSuffix,  SIGNAL(toggled(bool)),           SLOT(updateOutputFilename()));
  connect(encodingPage, SIGNAL(codecChanged(int)),       SLOT(updateOutputFilename()));
  connect(muxingPage,   SIGNAL(containerChanged(int)),   SLOT(updateOutputFilename()));
}
```

- [ ] **Step 2: Call the slot at end of `setCommonData()`**

Locate the end of `setCommonData()` (around line 170-171):
```cpp
  if (TTCut::muxPause)
    // ...
  getFreeDiskSpace();
 }
```

Actually, the current tail is:
```cpp
  // cut options
  // write max bittrate tp first sequence
  cbMaxBitrate->setChecked(TTCut::cutWriteMaxBitrate);

  // write sequence end code
  cbEndCode->setChecked(TTCut::cutWriteSeqEnd);

  getFreeDiskSpace();
 }
```

Change to:
```cpp
  // cut options
  // write max bittrate tp first sequence
  cbMaxBitrate->setChecked(TTCut::cutWriteMaxBitrate);

  // write sequence end code
  cbEndCode->setChecked(TTCut::cutWriteSeqEnd);

  // Populate the field with suffix + extension from current state.
  updateOutputFilename();

  getFreeDiskSpace();
 }
```

- [ ] **Step 3: Simplify `getCommonData()`**

Current body (lines 176-219):
```cpp
void TTCutAVCutDlg::getCommonData()
{
  // cut output filename and output path
  TTCut::cutVideoName  = leOutputFile->text();
  TTCut::cutDirPath    = leOutputPath->text();
  TTCut::cutAddSuffix  = cbAddSuffix->isChecked();

  if ( !QDir(TTCut::cutDirPath).exists() )
    TTCut::cutDirPath    = QDir::currentPath();

  // Check for video file extension based on codec and muxer selection
  QFileInfo cutFile(TTCut::cutVideoName);
  QString ext = cutFile.suffix().toLower();

  // Determine appropriate extension based on output container and codec
  // TTCut::outputContainer: 0=mplex, 1=mkvmerge, 2=ffmpeg
  // TTCut::encoderCodec: 0=MPEG-2, 1=H.264, 2=H.265
  QString expectedExt;

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

  // Add extension if missing or different
  if (ext.isEmpty() || (ext != expectedExt && ext != "m2v" && ext != "h264" && ext != "h265" && ext != "ts")) {
    TTCut::cutVideoName += "." + expectedExt;
  }

  // cut options
  // write max bittrate tp first sequence
  TTCut::cutWriteMaxBitrate = cbMaxBitrate->isChecked();

  // write sequence end code
  TTCut::cutWriteSeqEnd = cbEndCode->isChecked();
}
```

Replace entirely with:
```cpp
void TTCutAVCutDlg::getCommonData()
{
  TTCut::cutVideoName  = leOutputFile->text();
  TTCut::cutDirPath    = leOutputPath->text();
  TTCut::cutAddSuffix  = cbAddSuffix->isChecked();

  if (!QDir(TTCut::cutDirPath).exists())
    TTCut::cutDirPath = QDir::currentPath();

  // Defensive: if the user stripped the extension, re-append the expected one.
  QFileInfo cutFile(TTCut::cutVideoName);
  if (cutFile.suffix().isEmpty()) {
    TTCut::cutVideoName += "." + expectedContainerExtension(TTCut::outputContainer);
  }

  TTCut::cutWriteMaxBitrate = cbMaxBitrate->isChecked();
  TTCut::cutWriteSeqEnd     = cbEndCode->isChecked();
}
```

- [ ] **Step 4: Implement the static helper and the slot**

Append at the end of `gui/ttcutavcutdlg.cpp` (after the last method, preserving any trailing newline):

```cpp
/* /////////////////////////////////////////////////////////////////////////////
 * Extension that matches the final on-disk container.
 * 0 = MPG (mplex) → mpg
 * 1 = MKV (libav) → mkv
 */
QString TTCutAVCutDlg::expectedContainerExtension(int container)
{
  return (container == 1) ? QStringLiteral("mkv") : QStringLiteral("mpg");
}

/* /////////////////////////////////////////////////////////////////////////////
 * Rebuild leOutputFile from the current widget state:
 *   basename (± "_cut" suffix) + "." + container extension.
 * A manual base name typed by the user is preserved; only the "_cut" suffix
 * and the extension are adjusted.
 */
void TTCutAVCutDlg::updateOutputFilename()
{
  QFileInfo fi(leOutputFile->text());
  QString base = fi.completeBaseName();

  bool hasSuffix  = base.endsWith(QStringLiteral("_cut"));
  bool wantSuffix = cbAddSuffix->isChecked();
  if      ( wantSuffix && !hasSuffix) base += QStringLiteral("_cut");
  else if (!wantSuffix &&  hasSuffix) base.chop(4);

  QString ext = expectedContainerExtension(TTCut::outputContainer);
  leOutputFile->setText(base + "." + ext);
}
```

- [ ] **Step 5: Sanity check**

Run:
```bash
grep -n "updateOutputFilename\|expectedContainerExtension" gui/ttcutavcutdlg.cpp
```
Expected: at least 4 matches — one in the constructor `connect` block, one at the end of `setCommonData`, one in `getCommonData`, and three implementations at the file's tail (two connects to the slot and the slot/helper definitions). Adjust the expected count based on what you see but ensure both names appear in the constructor and both are defined at the tail.

```bash
grep -n "private slots:\|expectedContainerExtension\|updateOutputFilename" gui/ttcutavcutdlg.h
```
Expected: declarations from Task 3 are still present.

---

## Task 5: Full rebuild

**Files:** (build artefacts only)

- [ ] **Step 1: Clean and rebuild**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: exit status 0, produces `./ttcut-ng`. Warnings acceptable; errors are not.

Common pitfalls to watch for:
- If `Q_OBJECT`-related errors appear for `TTCutAVCutDlg`, the `make clean` + fresh moc regeneration via `qmake` should cover them.
- If a "no such signal" or "no such slot" error appears, double-check Task 2 (signals block) and Task 3 (private slots block) are correctly placed with exact Qt macros.

- [ ] **Step 2: Verify binary**

Run:
```bash
ls -la ttcut-ng
```
Expected: recent modification time, `ELF 64-bit LSB executable`.

---

## Task 6: Manual GUI verification

Eight scenarios from the spec. Launch with `QT_QPA_PLATFORM=xcb ./ttcut-ng`. Use the test files from prior sessions (MPEG-2 `.m2v`, H.264 `.264`).

- [ ] **Step 1: Scenario 1 — Initial display MPEG-2**

1. Start TTCut-ng.
2. File → Open Video → select an MPEG-2 `.m2v`.
3. Open Cut dialog.

Expected: `leOutputFile` shows `<videobase>_cut.mpg` OR `<videobase>_cut.mkv` depending on the user's stored MPEG-2 muxer preference. Either way, the extension is **visible** in the field.

Cancel the dialog.

- [ ] **Step 2: Scenario 2 — Initial display H.264**

1. File → New, Open Video → select an H.264 `.264`.
2. Open Cut dialog.

Expected: `leOutputFile` shows `<videobase>_cut.mkv` (MPG is disabled for H.264 by the prior muxer cleanup fix).

Cancel.

- [ ] **Step 3: Scenario 3 — Suffix toggle adds `_cut`**

1. Inside the open Cut dialog, un-tick `cbAddSuffix` if it is ticked (Common tab).

Expected: `_cut` is stripped from the field; extension unchanged.

2. Tick it again.

Expected: `_cut` reappears; extension unchanged.

- [ ] **Step 4: Scenario 4 — Container change updates extension live** (MPEG-2 source)

1. With an MPEG-2 source loaded, open Cut dialog.
2. Switch to the Muxing tab, toggle the Muxer Program combo between `MKV (libav)` and `MPG (mplex)`.
3. Switch back to the Common tab.

Expected: `leOutputFile` now shows the new extension (`.mkv` ↔ `.mpg`). Base name unchanged.

- [ ] **Step 5: Scenario 5 — Manual base name persists across suffix toggle**

1. In the Common tab, clear `leOutputFile` and type `my_archive`.
2. Tick `cbAddSuffix`. Expected: `my_archive_cut.<ext>`.
3. Un-tick `cbAddSuffix`. Expected: `my_archive.<ext>`.
4. Change the Muxing tab combo. Expected: `my_archive[.<new-ext>]` (base still `my_archive` or with `_cut` per checkbox state).

- [ ] **Step 6: Scenario 6 — Manual extension is overwritten on container change**

1. In Common tab, type `movie.avi` in `leOutputFile`.
2. Change the Muxing tab container.
3. Return to Common tab.

Expected: field now shows `movie.mkv` (or `.mpg`) — custom extension replaced. This is the accepted trade-off documented in the spec.

- [ ] **Step 7: Scenario 7 — End-to-end cut at each valid combination**

1. MPEG-2 + MPG → run a real cut → verify `.mpg` file on disk.
2. MPEG-2 + MKV → run a real cut → verify `.mkv` file on disk.
3. H.264 + MKV → run a real cut → verify `.mkv` file on disk.

After each, `ls -la <cutDirPath>/<name>.<ext>` should show the expected file. The filename shown in the Cut dialog at OK-time must match the filename on disk.

- [ ] **Step 8: Scenario 8 — Defensive extension fallback in `getCommonData`**

1. In Common tab, clear `leOutputFile` and type `foo` (no extension).
2. Click OK.
3. After the cut completes (or examine the dialog state if you can cancel right before execution), verify the final filename on disk is `foo.mkv` (or `.mpg`) — the missing extension was re-appended by the defensive fallback.

- [ ] **Step 9: Pass/fail**

If all scenarios pass, proceed to Task 7.
If any fails, stop; do not commit; investigate.

---

## Task 7: Commit

**Files:** (four modified files)

- [ ] **Step 1: Review the diff**

Run:
```bash
git diff --stat gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp gui/ttcutavcutdlg.h gui/ttcutavcutdlg.cpp
git diff gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp gui/ttcutavcutdlg.h gui/ttcutavcutdlg.cpp
```
Expected: changes only in the four files; no unrelated hunks. The ttcutavcutdlg.cpp diff should show a **shrink** (getCommonData simplification) more than offsets the new slot implementation, so the net size change is moderate.

- [ ] **Step 2: Stage and commit**

Run:
```bash
git add gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp gui/ttcutavcutdlg.h gui/ttcutavcutdlg.cpp
git commit -m "$(cat <<'EOF'
Cut dialog: live filename updates from suffix and container

The output filename field in the Cut dialog now reflects the user's
intent the moment it changes:

- Toggling the "Add _cut suffix" checkbox immediately adds or strips
  "_cut" from the displayed base name.
- Changing the container in the Muxing tab immediately updates the
  displayed extension (.mpg for mplex, .mkv for libav).
- Changing the codec in the Encoding tab triggers the same slot for
  symmetry (no visible effect today since extension is container-driven).

Previously the field showed only the base name; the extension was
appended on dialog close using a codec-dependent whitelist that
appended intermediate ES extensions (.h264/.h265/.m2v) even though
the actual on-disk file was always .mkv or .mpg.

TTCutSettingsMuxer grows a containerChanged(int) signal; TTCutAVCutDlg
grows a private updateOutputFilename() slot and a static
expectedContainerExtension(int) helper. getCommonData() shrinks to
passing leOutputFile text through verbatim, with a defensive extension
fallback if the user stripped it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify repo state**

Run:
```bash
git log --oneline -3 && git status
```
Expected: new commit at HEAD on `fix/cut-dialog-live-filename`, working tree clean.

---

## Rollback

Four files, no schema changes. Revert with:

```bash
git diff gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp gui/ttcutavcutdlg.h gui/ttcutavcutdlg.cpp
git checkout -- gui/ttcutsettingsmuxer.h gui/ttcutsettingsmuxer.cpp gui/ttcutavcutdlg.h gui/ttcutavcutdlg.cpp
```

If Task 6 verification fails after a clean build, return to systematic-debugging before touching more code.
