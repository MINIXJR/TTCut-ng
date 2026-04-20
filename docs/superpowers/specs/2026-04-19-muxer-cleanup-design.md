# Muxer UI Cleanup + Codec Compatibility Guard — Design

**Date:** 2026-04-19
**Branch:** master (to be branched for implementation)
**Status:** Design approved

## Problem

The Cut dialog's Muxing tab presents three misleading or broken options:

- **„Mplex (MPEG-2)"** — correct tool name but codec-in-parens is redundant with a separate Codec selector.
- **„mkvmerge (MKV)"** — names an external tool (`mkvmerge` CLI) that was removed in v0.60.0. The actual MKV muxing is now done via libavformat's matroska muxer (`avformat_alloc_output_context2(..., "matroska", ...)`) inside `TTMkvMergeProvider`. The class retained its legacy name; the UI label never did.
- **„FFmpeg (MP4/TS)"** — for MPEG-2 sources produces `.mp4`; for H.264/H.265 sources is silently bypassed (the Smart-Cut path in `ttavdata.cpp:1293-1298` hard-forces `.mkv`). The label promises MP4 but delivers MKV in the most common use case.

Additionally, the combo does not disable combinations that cannot work (e.g. mplex muxer with H.264/H.265 source produces nonsense or crashes), relying entirely on the auto-detect logic in `doCut()` to pre-select sensibly. A user who opens the Muxing tab and manually picks mplex for an H.264 project gets no feedback that the combination is invalid.

## Design Decision

Reframe the combo around **what the user actually gets on disk** (container), with the tool in parentheses as an implementation hint. Remove the MP4 option entirely because its utility does not justify the complexity it adds (dual code paths, misleading label, dead branches in H.264/H.265 flow). Post-hoc MP4 conversion of an MKV result is a trivial `ffmpeg -i foo.mkv -c copy foo.mp4` — feature duplication inside TTCut-ng is not worth it.

**Display order decouples from internal value.** MKV is shown first (it is the default and the modern choice), MPG second. Internal `TTCut::outputContainer` values remain unchanged (0 = mplex, 1 = MKV) so no schema migration is required for the switch in `ttavdata.cpp`, the extension logic in `ttcutavcutdlg.cpp`, or per-codec preferences (`mpeg2Muxer`, `h264Muxer`, `h265Muxer`). Only the combo UI maps between display position and stored value via `QComboBox::itemData()`.

### New combo contents

| Display position | Label | Stored value (`outputContainer`) | Tool | Container | Valid for |
|---|---|---|---|---|---|
| 0 | `MKV (libav)` | **1** | internal libavformat matroska muxer | `.mkv` | MPEG-2, H.264, H.265 |
| 1 | `MPG (mplex)` | **0** | external `mplex` | `.mpg` | MPEG-2 only |

No third option.

### Compatibility guard

When the current encoder codec (`TTCut::encoderCodec`, which is auto-set from source stream type in `doCut()` at `ttcutmainwindow.cpp:1032-1042`) is H.264 (1) or H.265 (2):

- The `MPG (mplex)` row (display position 1) is **disabled** in the combo (user cannot select it).
- If the stored preference for that codec was `0` (mplex) from a prior misconfiguration, it is clamped to `1` (MKV) when the codec change hits `onEncoderCodecChanged()`.

When the encoder codec is MPEG-2 (0): both items enabled, existing preference logic unchanged.

## Changes

### `gui/ttcutsettingsmuxer.cpp` — combo population

Line 68-71 currently:
```cpp
cbMuxerProg->clear();
cbMuxerProg->insertItem(0, "Mplex (MPEG-2)");
cbMuxerProg->insertItem(1, "mkvmerge (MKV)");
cbMuxerProg->insertItem(2, "FFmpeg (MP4/TS)");
```

Replace with:
```cpp
cbMuxerProg->clear();
// Display order: MKV first (default/modern), MPG second.
// userData holds the internal TTCut::outputContainer value (stable across UI rework).
cbMuxerProg->insertItem(0, "MKV (libav)", 1);
cbMuxerProg->insertItem(1, "MPG (mplex)", 0);
```

### `gui/ttcutsettingsmuxer.cpp` — helper for index ↔ value mapping

Two tiny helpers (file-local or private methods) to avoid scattering the lookup:

```cpp
int TTCutSettingsMuxer::muxerValueAt(int displayIndex) const {
  return cbMuxerProg->itemData(displayIndex).toInt();
}

int TTCutSettingsMuxer::indexForMuxerValue(int outputContainerValue) const {
  for (int i = 0; i < cbMuxerProg->count(); ++i) {
    if (cbMuxerProg->itemData(i).toInt() == outputContainerValue) return i;
  }
  return 0;  // fall back to first (MKV)
}
```

### `gui/ttcutsettingsmuxer.cpp` — `setTabData()` current-index lookup

Current line 114 reads:
```cpp
cbMuxerProg->setCurrentIndex(TTCut::outputContainer);
```
Replace with:
```cpp
cbMuxerProg->setCurrentIndex(indexForMuxerValue(TTCut::outputContainer));
```

The same pattern applies anywhere else the code sets the combo's current index from `TTCut::outputContainer` (search the file to catch all usages).

### `gui/ttcutsettingsmuxer.cpp` — `onMuxerProgChanged(int index)` reads via userData

Current line 224-242:
```cpp
void TTCutSettingsMuxer::onMuxerProgChanged(int index)
{
  TTCut::outputContainer = index;
  switch (TTCut::encoderCodec) {
    case 0: TTCut::mpeg2Muxer = index; break;
    case 1: TTCut::h264Muxer  = index; break;
    case 2: TTCut::h265Muxer  = index; break;
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
  switch (TTCut::encoderCodec) {
    case 0: TTCut::mpeg2Muxer = value; break;
    case 1: TTCut::h264Muxer  = value; break;
    case 2: TTCut::h265Muxer  = value; break;
  }
  updateMuxerVisibility();
}
```

### `gui/ttcutsettingsmuxer.cpp` — `onEncoderCodecChanged(int)` extended

Current logic (line 255-280) selects a preferred muxer per codec via `cbMuxerProg->setCurrentIndex(preferredMuxer)` where the argument was a display index AND a container value because those coincided. They no longer coincide.

Replace the function with:
```cpp
void TTCutSettingsMuxer::onEncoderCodecChanged(int codecIndex)
{
  // Disable the MPG row for H.264/H.265; enable it for MPEG-2.
  bool mpgSupported = (codecIndex == 0);
  QStandardItemModel* model = qobject_cast<QStandardItemModel*>(cbMuxerProg->model());
  if (model) {
    int mpgRow = indexForMuxerValue(0);  // 0 = mplex
    QStandardItem* mpgItem = model->item(mpgRow);
    if (mpgItem) {
      Qt::ItemFlags f = mpgItem->flags();
      mpgItem->setFlags(mpgSupported ? (f | Qt::ItemIsEnabled)
                                     : (f & ~Qt::ItemIsEnabled));
    }
  }

  // Fetch stored preference for this codec.
  int preferred;
  switch (codecIndex) {
    case 0: preferred = TTCut::mpeg2Muxer; break;
    case 1: preferred = TTCut::h264Muxer;  break;
    case 2: preferred = TTCut::h265Muxer;  break;
    default: preferred = 1;  // MKV
  }
  if (!mpgSupported && preferred == 0) {
    preferred = 1;  // MPG invalid for this codec → fall back to MKV
  }

  cbMuxerProg->setCurrentIndex(indexForMuxerValue(preferred));
  TTCut::outputContainer = preferred;
  updateMuxerVisibility();
}
```

Include `#include <QStandardItemModel>` in the `.cpp` file if not already present.

### `gui/ttcutsettingsmuxer.h` — declare helpers

Add the two private helpers (both `const` since they only read state):
```cpp
int muxerValueAt(int displayIndex) const;
int indexForMuxerValue(int outputContainerValue) const;
```

### `data/ttavdata.cpp` — remove MP4 case

Delete the entire `case 2: // MP4 - use FFmpeg` block (lines 1661-1737). The switch after the edit:

```cpp
switch (TTCut::outputContainer) {
  case 1: // MKV - libav matroska muxer
    // ... existing block unchanged ...
    break;

  case 3: // Elementary - no muxing (defensive; not reachable from UI)
    qDebug() << "Elementary output selected, skipping muxing";
    break;

  case 0: // MPG - use mplex (default)
  default:
    // ... existing block unchanged ...
    break;
}
```

Update the comment block above the switch (lines 1567-1571) from:
```
// 0 = TS (Transport Stream, mplex)
// 1 = MKV (mkvmerge)
// 2 = MP4 (FFmpeg)
// 3 = Elementary (no muxing)
```
to:
```
// 0 = MPG (mplex)
// 1 = MKV (libav matroska muxer)
// 3 = Elementary (no muxing; not reachable from UI, kept as defensive default)
```

### `gui/ttcutavcutdlg.cpp` — remove dead MP4 branch in extension logic

Lines 196-202 contain the now-dead `TTCut::outputContainer == 2` branch:
```cpp
} else if (TTCut::outputContainer == 2) {
  // MP4/TS output via ffmpeg - extension is based on codec
  if (TTCut::encoderCodec == 1 || TTCut::encoderCodec == 2) {
    expectedExt = "ts";
  } else {
    expectedExt = "m2v";
  }
}
```

Delete this block. The `else if` becomes a plain `else` handling container `0` (mplex → `.m2v`). The remaining logic (container `1` → `.h264`/`.h265`/`.m2v`) stays unchanged; it will be replaced entirely in the follow-up Suffix+Extension-Live ticket.

### `gui/ttcutsettings.cpp` — migration for legacy QSettings values

After reading the container/muxer preference values, clamp any legacy `2` to `1` (MKV):

```cpp
// Legacy migration: outputContainer / per-codec muxer preferences may
// contain the removed MP4 option (value 2). Map to MKV (1).
if (TTCut::outputContainer == 2) TTCut::outputContainer = 1;
if (TTCut::mpeg2Muxer     == 2) TTCut::mpeg2Muxer     = 1;
if (TTCut::h264Muxer      == 2) TTCut::h264Muxer      = 1;
if (TTCut::h265Muxer      == 2) TTCut::h265Muxer      = 1;
```

Inserted right after the block that reads these values in `readSettings()`.

## Invariants After Fix

- Muxer combo has exactly two items; display order is `MKV (libav)` then `MPG (mplex)`.
- Internal `TTCut::outputContainer` values are unchanged: `0 = mplex`, `1 = MKV`. The combo maps display index → value via `itemData()`.
- Source codec H.264/H.265 → `MPG (mplex)` row is greyed out and cannot be selected.
- MP4 output is impossible through TTCut-ng. Users who want MP4 convert the MKV result with `ffmpeg -i out.mkv -c copy out.mp4`.
- Existing QSettings with stale `== 2` values silently migrate to `1` on next app start.
- No changes to the actual mux engines (`TTMplexProvider`, `TTMkvMergeProvider`) — only UI layer and one dead-case removal in `ttavdata.cpp`.

## Out of Scope

- **Suffix-toggle live update** in the Cut dialog — separate ticket.
- **Live extension display** in the filename field — separate ticket.
- **Codec selector guard** (disabling incompatible encoder codecs for the source stream type) — separate ticket.
- **Wiki/README documentation** of post-hoc MP4 conversion — separate follow-up (editing `/usr/local/src/TTCut-ng.wiki/` and `README.md`).
- **Renaming `TTMkvMergeProvider` class** — legacy name is misleading but out of scope for this UI fix.

## Testing

Manual verification:

1. **MPEG-2 source:**
   - Open an MPEG-2 `.m2v` file.
   - Open the Cut dialog, go to the Muxing tab.
   - Expected: Combo shows `MKV (libav)` at top and `MPG (mplex)` below. Both selectable. Default selection is `MKV (libav)` (or the user's stored MPEG-2 preference if set).
2. **H.264 source:**
   - Open an H.264 `.264` file (e.g. `Moon_Crash_(2022).264`).
   - Open the Cut dialog, go to the Muxing tab.
   - Expected: Combo shows both items in the same order, but `MPG (mplex)` is greyed out. Selection is `MKV (libav)`.
3. **H.265 source:** same as H.264.
4. **Legacy QSettings migration:**
   - Edit `~/.config/<org>/ttcut-ng.conf` to set `outputContainer=2` (or `h264Muxer=2`).
   - Start TTCut-ng, open Cut dialog → Muxing tab.
   - Expected: Combo selection is `MKV (libav)` (migrated from 2).
5. **Full cut end-to-end:**
   - With MPEG-2 source + `MPG (mplex)`, run a cut → verify `.mpg` output on disk.
   - With MPEG-2 source + `MKV (libav)`, run a cut → verify `.mkv` output.
   - With H.264 source + `MKV (libav)`, run a cut → verify `.mkv` output.
6. **Previously-reachable MP4 path stays dead:**
   - No on-disk `.mp4` file should be produced by any UI action.

No automated tests — the muxer options are UI state with no existing unit coverage.

## Rollback

Five files touched (`gui/ttcutsettingsmuxer.cpp`, `gui/ttcutsettingsmuxer.h`, `data/ttavdata.cpp`, `gui/ttcutavcutdlg.cpp`, `gui/ttcutsettings.cpp`), no schema changes. Reverting is `git checkout -- <paths>`.
