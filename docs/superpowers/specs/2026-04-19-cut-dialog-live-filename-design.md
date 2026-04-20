# Cut Dialog Live Filename — Design

**Date:** 2026-04-19
**Branch:** master (to be branched for implementation)
**Status:** Design approved

## Problem

Two UX defects in the Cut dialog's output filename field (`leOutputFile`):

1. **Suffix checkbox does not react live.** `cbAddSuffix` toggles `TTCut::cutAddSuffix` (applied next time the name is derived in `doCut()`), but the currently displayed text in `leOutputFile` is never updated. The user ticks / un-ticks and nothing visible happens. The `_cut` suffix is decided once at dialog open and is fixed thereafter.

2. **Extension not visible in the field.** The user sees only the base name (e.g. `08x01_-_Bingen_bis_zum_Tod`). The extension is appended only when the dialog closes, in `getCommonData()` at `gui/ttcutavcutdlg.cpp:186-211`, and even then the value is misleading: for MKV output the code appends the **intermediate ES extension** (`.h264`/`.h265`/`.m2v`), while the actual final on-disk file is always `.mkv` (see `ttavdata.cpp:1293-1298` forcing MKV on the Smart-Cut path, and `ttavdata.cpp:1604-1605` for the MPEG-2 MKV path). The user sees something that does not match the actual output.

The 2026-04-19 muxer cleanup removed the MP4/TS option, so the current final-extension matrix is trivial:

| Container | MPEG-2 | H.264 | H.265 |
|---|---|---|---|
| 0 `MPG (mplex)` | `.mpg` | n/a (disabled) | n/a (disabled) |
| 1 `MKV (libav)` | `.mkv` | `.mkv` | `.mkv` |

Extension depends only on the container; codec no longer influences it.

## Design Decision

Make the filename field a live reflection of the user's intent. Three triggers keep it in sync:

1. **`cbAddSuffix::toggled(bool)`** — adds or strips `_cut` from the current base name (string-manipulation approach; does not re-derive from the video file name, so any custom base name the user typed manually survives).
2. **`encodingPage::codecChanged(int)`** — recompute extension. In practice this only matters for completeness, because with MP4 gone the extension is container-driven and the codec change does not affect it. Kept for defensive symmetry: future container additions may reintroduce codec dependence.
3. **`muxingPage::containerChanged(int)`** (new signal) — recompute extension. This is the active trigger today: MPG↔MKV toggle flips the extension.

A single private slot `updateOutputFilename()` executes all three paths, reading current checkbox state and current container selection from `TTCut::outputContainer`.

### Extension helper

A static helper inside `TTCutAVCutDlg` (no need for a class member):

```cpp
static QString expectedContainerExtension(int container) {
  // 0 = MPG (mplex), 1 = MKV (libav)
  return (container == 1) ? QStringLiteral("mkv") : QStringLiteral("mpg");
}
```

The previous codec-based whitelist logic in `getCommonData()` is obsolete and is removed. `getCommonData()` simply takes `leOutputFile->text()` as-is, with a defensive fallback: if the user erased the extension, append the expected one.

### Suffix toggle semantics

```cpp
QFileInfo fi(leOutputFile->text());
QString base = fi.completeBaseName();
bool hasSuffix  = base.endsWith("_cut");
bool wantSuffix = cbAddSuffix->isChecked();
if      ( wantSuffix && !hasSuffix) base += "_cut";
else if (!wantSuffix &&  hasSuffix) base.chop(4);
```

Edge case: a user who manually typed `foo_cut` and then un-ticks the checkbox gets `foo`. Acceptable — `_cut` is defined as the suffix by this checkbox and stripping it is the only consistent behaviour.

### Signal wiring

`TTCutSettingsMuxer` already has a `codecChanged(int)` signal. It needs a sibling `containerChanged(int)` emitted from `onMuxerProgChanged()`:

```cpp
// TTCutSettingsMuxer.h (new signal in the `signals:` block)
signals:
    void codecChanged(int codecIndex);        // existing, emitted from encoder tab
    void containerChanged(int containerValue); // new

// TTCutSettingsMuxer.cpp
void TTCutSettingsMuxer::onMuxerProgChanged(int index) {
  int value = muxerValueAt(index);
  TTCut::outputContainer = value;
  // ... existing per-codec preference save ...
  updateMuxerVisibility();
  emit containerChanged(value);
}
```

`codecChanged` belongs on `TTCutSettingsEncoder` where it already lives — no change there. Note: `codecChanged` is currently declared in the encoder tab; the Cut dialog connects it. The Muxer tab will own `containerChanged` symmetrically.

In `TTCutAVCutDlg::TTCutAVCutDlg`, after the existing `setCommonData()`/`setTabData()`/`onEncoderCodecChanged()` block:

```cpp
connect(cbAddSuffix,  SIGNAL(toggled(bool)),    this, SLOT(updateOutputFilename()));
connect(encodingPage, SIGNAL(codecChanged(int)), this, SLOT(updateOutputFilename()));
connect(muxingPage,   SIGNAL(containerChanged(int)), this, SLOT(updateOutputFilename()));
```

The connects come **after** the `setTabData()` calls so the initial population (which emits `currentIndexChanged`) does not fire into our slot before setup is finished.

### `setCommonData()` change

At the end of `setCommonData()`, call `updateOutputFilename()` once so the field shows base name + suffix + extension immediately when the dialog opens:

```cpp
// existing end of setCommonData():
cbAddSuffix->setChecked(TTCut::cutAddSuffix);
// ... other widget sets ...

updateOutputFilename();   // new: initial display with suffix + extension
getFreeDiskSpace();
```

### `getCommonData()` simplification

Replace the entire codec/container whitelist block (lines 186-212) with:

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

The dialog now always displays the correct extension, so the complex whitelist reconciliation is obsolete.

## Files Touched

- `gui/ttcutavcutdlg.h` — add private slot declaration and static helper declaration.
- `gui/ttcutavcutdlg.cpp` — implement slot, implement helper, extend constructor wiring, add `updateOutputFilename()` call at end of `setCommonData()`, simplify `getCommonData()`.
- `gui/ttcutsettingsmuxer.h` — add `containerChanged(int)` signal.
- `gui/ttcutsettingsmuxer.cpp` — emit `containerChanged` from `onMuxerProgChanged`.

## Invariants After Fix

- `leOutputFile` always shows `<basename>[.<mkv|mpg>]` matching the final on-disk extension.
- Ticking/un-ticking `cbAddSuffix` immediately adds or strips `_cut` from the displayed base name.
- Changing the container in the Muxing tab immediately updates the displayed extension.
- Changing the codec in the Encoding tab triggers the slot (currently no visible change since extension is container-driven; kept for symmetry).
- Manual edits to `leOutputFile` between triggers are respected for the base name; only the extension is overwritten when a trigger fires.
- `getCommonData()` takes the field text verbatim, adding an extension only if the user stripped it.

## Out of Scope

- **Codec selector guard** (prevent selecting an encoder incompatible with the source stream). Already out of scope of the muxer cleanup ticket; still out of scope here.
- **Renaming `TTMkvMergeProvider`** — unrelated.
- **H.265 MKV muxing bug** — tracked separately in `TODO.md`.

## Testing

Manual verification:

1. **Initial display, MPEG-2 source.**
   Open an `.m2v`. Open Cut dialog. Expected: `<video>_cut.mpg` or `<video>_cut.mkv` depending on stored Muxer preference. Extension visible in the field immediately.

2. **Initial display, H.264 source.**
   Open a `.264`. Open Cut dialog. Expected: `<video>_cut.mkv` (MPG disabled by muxer cleanup fix).

3. **Suffix toggle — add.**
   Open the dialog. Tick `cbAddSuffix` when un-ticked. Expected: `_cut` appears in the base name; extension unchanged.

4. **Suffix toggle — remove.**
   Tick twice so it ends un-ticked. Expected: `_cut` is removed from the base name.

5. **Container change MPG → MKV** (MPEG-2 source only).
   In Muxing tab, change container. Expected: displayed extension in the Common tab's `leOutputFile` flips from `.mpg` to `.mkv` (and back).

6. **Manual base name edit persists across toggle.**
   Type a custom base name like `my_archive`. Tick suffix on/off. Expected: `my_archive_cut` and `my_archive` — custom base survives.

7. **Manual extension edit is overwritten by container change.**
   Type `movie.avi`. Change container. Expected: `movie.mkv` or `movie.mpg`. (Accepted trade-off per Edge Cases section.)

8. **End-to-end cut still produces correct file.**
   Run a real cut at each of MPEG-2+MPG, MPEG-2+MKV, H.264+MKV. Expected: output file name on disk matches what the dialog showed.

No automated tests — UI-layer state without existing coverage.

## Rollback

Four files touched, no schema or persistence changes. Revert with `git checkout -- <paths>`.
