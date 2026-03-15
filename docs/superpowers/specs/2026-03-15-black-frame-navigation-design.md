# Black Frame Navigation

## Summary

Replace batch-based black frame detection (Landezonen analysis) with interactive navigation buttons in the frame navigation widget. The user presses a button to find the next/previous black frame from the current position — fast, accurate, and directly useful while editing cut points.

## Motivation

The batch analysis decodes the entire video file upfront, which takes too long for large files (6.4 GB H.264 = minutes). Frame index accuracy is also problematic with the batch approach. Interactive search from the current position avoids both issues: it only decodes forward/backward until a match is found, and uses TTCut-ng's own frame index for perfect position accuracy.

## UI Changes

### Navigation Widget (ttcutframenavigationwidget.ui)

New row inserted at **row 4** (between F-frame buttons and the vertical spacer). Layout follows the existing I/P/B/F pattern:

```
[◀ monitor] [monitor ▶]  [0.980 ▲▼]
```

- Two QPushButtons (`pbPrevBlackFrame`, `pbNextBlackFrame`) with a custom 18x18 XPM icon showing a monitor with black screen and stand
- One QDoubleSpinBox (`sbBlackThreshold`) — range 0.800–1.000, step 0.005, 3 decimals, default 0.980
- Horizontal spacer removed (spinbox fills the space instead)

All subsequent rows shift down by 1 (spacer→5, Cut-In→6, Cut-Out→7, etc.).

### Landezonen Settings Tab (ttstreampointwidget.cpp)

Remove the "Schwarzbilder" checkbox, threshold spinbox, and min-duration spinbox from the Einstellungen tab. The corresponding TTCut:: settings variables (`spDetectBlackFrames`, `spBlackThreshold`, `spBlackMinDuration`) are no longer needed for batch analysis. The threshold is now on the navigation widget spinbox.

Remove black frame detection from `TTStreamPointVideoWorker::operation()`.

### Icon (blackframe_18.xpm)

New 18x18 XPM file in `ui/pixmaps/`. Design: monitor silhouette with black screen (dark fill) and a small stand/base at the bottom. Light grey frame (#999999), black screen (#1A1A1A), stand (#777777), transparent background.

## Search Logic

### Algorithm

When the user presses ▶ (next black frame):

1. Get current frame position from TTCurrentFrame
2. Get the video stream's frame count and decoded frame access
3. Starting from `currentPos + 1`, iterate forward frame by frame
4. For each frame: decode it, run `isBlackFrame()` on the Y plane (center 80%, Y <= 17, ratio >= threshold)
5. On first match: emit signal with the frame index → main window navigates there
6. If end of stream reached without match: show status message "Kein Schwarzbild gefunden"
7. Set busy cursor during search, restore on completion

For ◀ (previous): same but iterate from `currentPos - 1` backward.

### Frame Access

Use `TTVideoStream::getFrameYData(int frameIndex)` or equivalent to access decoded Y plane data for arbitrary frame positions. TTCut-ng already has decoders for all supported codecs (libmpeg2 for MPEG-2, libav for H.264/H.265). The search reuses the existing decode infrastructure rather than opening a separate decoder instance.

If direct Y-plane access is not available through the existing API, decode via `TTMpeg2Decoder` (MPEG-2) or `TTFFmpegWrapper` (H.264/H.265) with seek-to-position.

### isBlackFrame() Reuse

The existing `isBlackFrame()` logic from `ttstreampoint_videoworker.cpp` is extracted into a shared utility (or kept as a static method) so both the navigation search and any remaining batch code can use it. Parameters:
- `pixelThreshold = 18` (Y <= 17 is broadcast black)
- `ratioThreshold` from the spinbox value
- Center 80% crop (10% from each edge)

### Performance

- **Interactive search**: Decodes only the frames between current position and the next black frame. For typical DVB content, black frames at ad boundaries are seconds apart, so only dozens to hundreds of frames need decoding.
- **Worst case** (no black frame exists): Full scan to end of file. Busy cursor + ability to cancel (Escape key or re-press button) prevents UI freeze.
- The search runs in the main thread's event loop using `processEvents()` periodically, or in a lightweight worker thread with signal back to main thread.

## Signal Flow

```
pbNextBlackFrame clicked
  → TTCutFrameNavigation::onNextBlackFrame()
    → emit searchBlackFrame(currentPosition, +1, threshold)
      → TTCutMainWindow::onSearchBlackFrame(pos, direction, threshold)
        → decode frames from pos in direction
        → if found: call onVideoSliderChanged(foundPos) for proper sync
        → if not found: statusBar message
```

## Files Changed

| File | Change |
|------|--------|
| ui/ttcutframenavigationwidget.ui | Add row 4: black frame buttons + spinbox |
| ui/pixmaps/blackframe_18.xpm | New: monitor icon |
| ui/ttcutframenavigationwidget.qrc | Add blackframe_18.xpm reference |
| gui/ttcutframenavigation.h | Add slots, signals, spinbox member |
| gui/ttcutframenavigation.cpp | Connect buttons, emit signals |
| gui/ttcutmainwindow.h | Add onSearchBlackFrame slot |
| gui/ttcutmainwindow.cpp | Implement search logic using video stream decoder |
| gui/ttstreampointwidget.cpp | Remove black frame checkbox + spinboxes from settings tab |
| data/ttstreampoint_videoworker.cpp | Remove detectBlackFrames() from operation(), keep isBlackFrame() as static/shared |
| common/ttcut.h/cpp | Remove spDetectBlackFrames, spBlackThreshold, spBlackMinDuration |

## Not In Scope

- Scene change navigation (future feature, separate row)
- Backward-compatible batch black frame detection
- GPU-accelerated decoding
