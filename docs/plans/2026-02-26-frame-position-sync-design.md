# Frame Position Sync Fix

## Problem

When editing cut points via Prev/Next B-Frame buttons (in "Aktueller Frame") or Prev/Next CutOut buttons (in "Cut-Out Frame"), the slider and position label jump to the wrong position.

**Root Cause:** TTCurrentFrame and TTCutOutFrame share the same `videoStream` object. The `updateCutEntry()` call triggers a signal cascade that moves the shared stream to a different position before `updateCurrentPosition()` reads from it.

### Signal Chain (CurrentFrame Prev/Next B-Frame)

```
onPrevBFrame()
  ├─ videoStream->moveToIndexPos(newFramePos)     ← CORRECT position
  ├─ updateCutEntry(cutItem, newFramePos, cutOut)
  │   └─ Signal: itemUpdated → TTCutTreeView::onUpdateItem
  │       └─ Signal: itemUpdated → cutOutFrame->onCutOutChanged
  │           └─ onGotoCutOut(cutOut)
  │               └─ videoStream->moveToIndexPos(cutOut)  ← OVERWRITES stream position!
  ├─ mpegWindow->showFrameAt(newFramePos)          ← frame display is correct
  └─ updateCurrentPosition()
      └─ videoStream->currentIndex()               ← reads CUTOUT position, not newFramePos!
          └─ emit newFramePosition(cutOutPos)       ← slider jumps to CutOut
```

### Signal Chain (CutOutFrame Prev/Next CutOut)

Same pattern: `updateCutEntry` triggers `onCutOutChanged` on itself, which moves the stream back to the old CutOut position before `updateCurrentPosition()` reads from it.

## Fix: Explicit Position Parameter

Add an optional `pos` parameter to `updateCurrentPosition()` in both widgets. When provided, use position-based stream queries instead of `currentIndex()`.

### TTCurrentFrame::updateCurrentPosition(int pos)

**Before:**
```cpp
void TTCurrentFrame::updateCurrentPosition()
{
  int frame_type = videoStream->currentFrameType();
  szTemp1 = videoStream->currentFrameTime().toString("hh:mm:ss.zzz");
  szTemp2 = QString(" (%1)").arg(videoStream->currentIndex());
  // ...
  emit newFramePosition(videoStream->currentIndex());
}
```

**After:**
```cpp
void TTCurrentFrame::updateCurrentPosition(int pos)
{
  int actualPos = (pos >= 0) ? pos : videoStream->currentIndex();
  int frame_type = videoStream->frameType(actualPos);
  szTemp1 = videoStream->frameTime(actualPos).toString("hh:mm:ss.zzz");
  szTemp2 = QString(" (%1)").arg(actualPos);
  // ...
  emit newFramePosition(actualPos);
}
```

### TTCutOutFrame::updateCurrentPosition(int pos)

Same pattern as TTCurrentFrame.

### Affected Callers (pass explicit position)

| File | Method | Call |
|------|--------|------|
| ttcurrentframe.cpp:261 | `onPrevBFrame()` | `updateCurrentPosition(newFramePos)` |
| ttcurrentframe.cpp:283 | `onNextBFrame()` | `updateCurrentPosition(newFramePos)` |
| ttcutoutframe.cpp:172 | `onPrevCutOutPos()` | `updateCurrentPosition(newFramePos)` |
| ttcutoutframe.cpp:196 | `onNextCutOutPos()` | `updateCurrentPosition(newFramePos)` |

### Unaffected Callers (keep default pos=-1)

All other callers do not trigger signal cascades via `updateCutEntry()`, so the shared videoStream position remains stable. They continue using the default parameter.

## Files Changed

- `gui/ttcurrentframe.h` — method signature: `void updateCurrentPosition(int pos = -1)`
- `gui/ttcurrentframe.cpp` — method implementation + 2 call sites
- `gui/ttcutoutframe.h` — method signature: `void updateCurrentPosition(int pos = -1)`
- `gui/ttcutoutframe.cpp` — method implementation + 2 call sites
