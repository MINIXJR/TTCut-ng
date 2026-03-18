# Black Frame Navigation Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace batch black frame detection with interactive prev/next navigation buttons that search from the current frame position.

**Architecture:** Two new buttons + spinbox in the navigation widget emit a signal caught by TTCutMainWindow. The main window iterates frames using the existing decode infrastructure (TTMPEG2Window2 for MPEG-2, TTFFmpegWrapper for H.264/H.265) and navigates to the first black frame found. isBlackFrame() logic is inlined in the search method.

**Tech Stack:** Qt5, libmpeg2, libav (existing decoders)

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `ui/pixmaps/blackframe_18.xpm` | Create | Monitor icon for buttons |
| `ui/ttcutframenavigationwidget.qrc` | Modify | Add icon reference |
| `ui/ttcutframenavigationwidget.ui` | Modify | Add row 4: buttons + spinbox |
| `gui/ttcutframenavigation.h` | Modify | Add signals, slots, spinbox pointer |
| `gui/ttcutframenavigation.cpp` | Modify | Connect buttons, emit searchBlackFrame signal |
| `gui/ttcutmainwindow.h` | Modify | Add onSearchBlackFrame slot |
| `gui/ttcutmainwindow.cpp` | Modify | Connect signal, implement search logic |
| `gui/ttstreampointwidget.cpp` | Modify | Remove black frame settings from UI |
| `data/ttstreampoint_videoworker.h` | Modify | Remove detectBlackFrames/isBlackFrame declarations |
| `data/ttstreampoint_videoworker.cpp` | Modify | Remove detectBlackFrames/isBlackFrame, remove from operation() |
| `common/ttcut.h` | Modify | Remove spDetectBlackFrames/spBlackThreshold/spBlackMinDuration |
| `common/ttcut.cpp` | Modify | Remove initialization of removed variables |

---

### Task 1: Create monitor icon

**Files:**
- Create: `ui/pixmaps/blackframe_18.xpm`
- Modify: `ui/ttcutframenavigationwidget.qrc`

- [ ] **Step 1: Create blackframe_18.xpm**

Create an 18x18 XPM icon showing a monitor with black screen and stand. Light grey frame, black screen, small base.

```xpm
/* XPM */
static char* blackframe_18_xpm[] = {
"18 18 5 1",
"  c None",
". c #999999",
"# c #1A1A1A",
"+ c #777777",
"- c #555555",
"                  ",
"  ..............  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  .############.  ",
"  ..............  ",
"      ......      ",
"      +----+      ",
"     +------+     ",
"    +--------+    ",
"                  "};
```

- [ ] **Step 2: Add to .qrc**

Add `<file>pixmaps/blackframe_18.xpm</file>` to `ui/ttcutframenavigationwidget.qrc` inside the existing `<qresource>` block.

- [ ] **Step 3: Commit**

```bash
git add ui/pixmaps/blackframe_18.xpm ui/ttcutframenavigationwidget.qrc
git commit -m "Add blackframe monitor icon for navigation buttons"
```

---

### Task 2: Add black frame row to navigation UI

**Files:**
- Modify: `ui/ttcutframenavigationwidget.ui`

- [ ] **Step 1: Insert new row 4**

After the F-frame row (row 3), insert a new `QHBoxLayout` at row 4 containing:
- `pbPrevBlackFrame` (QPushButton, 45x24–45x32, icon blackframe_18.xpm, text `<`, tooltip "Vorheriges Schwarzbild suchen")
- `pbNextBlackFrame` (QPushButton, same sizing, text `>`, tooltip "Nächstes Schwarzbild suchen")
- `sbBlackThreshold` (QDoubleSpinBox, min 0.800, max 1.000, step 0.005, decimals 3, value 0.980, tooltip "Schwellwert für Schwarzbild-Erkennung")

Follow the exact pattern of the existing I/P/B/F rows for button sizing and layout.

- [ ] **Step 2: Shift subsequent rows**

Increment all row numbers after the F-frame row by 1:
- Old row 4 (spacer) → row 5
- Old row 5 (Cut-In) → row 6
- Old row 6 (Cut-Out) → row 7
- Old row 7 (spacer) → row 8
- Old row 8 (Add range) → row 9
- Old row 9 (Zeitsprung/Marker) → row 10
- Old row 10 (bottom spacer) → row 11

- [ ] **Step 3: Build to verify UI compiles**

```bash
rm -f obj/ttcutframenavigation.o && make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add ui/ttcutframenavigationwidget.ui
git commit -m "Add black frame navigation row to frame navigation widget"
```

---

### Task 3: Add signals and slots to TTCutFrameNavigation

**Files:**
- Modify: `gui/ttcutframenavigation.h`
- Modify: `gui/ttcutframenavigation.cpp`

- [ ] **Step 1: Add to header**

In `gui/ttcutframenavigation.h`:

Add public slots:
```cpp
void onPrevBlackFrame();
void onNextBlackFrame();
```

Add signal:
```cpp
void searchBlackFrame(int currentPos, int direction, float threshold);
```

- [ ] **Step 2: Add to implementation**

In `gui/ttcutframenavigation.cpp` constructor, add connections:
```cpp
connect(pbPrevBlackFrame, SIGNAL(clicked()), this, SLOT(onPrevBlackFrame()));
connect(pbNextBlackFrame, SIGNAL(clicked()), this, SLOT(onNextBlackFrame()));
```

Add slot implementations:
```cpp
void TTCutFrameNavigation::onPrevBlackFrame()
{
  if (!isControlEnabled) return;
  emit searchBlackFrame(currentPosition, -1, sbBlackThreshold->value());
}

void TTCutFrameNavigation::onNextBlackFrame()
{
  if (!isControlEnabled) return;
  emit searchBlackFrame(currentPosition, +1, sbBlackThreshold->value());
}
```

- [ ] **Step 3: Build**

```bash
rm -f obj/ttcutframenavigation.o && bear -- make -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add gui/ttcutframenavigation.h gui/ttcutframenavigation.cpp
git commit -m "Add black frame search signals and slots to navigation widget"
```

---

### Task 4: Implement search logic in TTCutMainWindow

**Files:**
- Modify: `gui/ttcutmainwindow.h`
- Modify: `gui/ttcutmainwindow.cpp`

- [ ] **Step 1: Add slot declaration**

In `gui/ttcutmainwindow.h`, add private slot:
```cpp
void onSearchBlackFrame(int startPos, int direction, float threshold);
```

- [ ] **Step 2: Connect signal**

In `gui/ttcutmainwindow.cpp` constructor (near existing navigation connections ~line 265):
```cpp
connect(navigation, SIGNAL(searchBlackFrame(int,int,float)), this, SLOT(onSearchBlackFrame(int,int,float)));
```

- [ ] **Step 3: Implement search**

The search method decodes frames starting from `startPos` in `direction` (+1 forward, -1 backward). For each frame, it navigates using the existing `TTMPEG2Window2` infrastructure which handles both MPEG-2 and H.264/H.265.

For MPEG-2: after `moveToVideoFrame()`, access `frameInfo->Y` (Y plane) with `frameInfo->width` and `frameInfo->height`.

For H.264/H.265: after `decodeFrame()`, use `QImage::convertToFormat(QImage::Format_Grayscale8)` and `QImage::bits()` for luma data.

The search accesses the MPEG2Window2 through the `currentFrame` widget (defined in the `.ui` file as a promoted TTMPEG2Window2 widget named `mpegWindow`). Since `currentFrame` is a TTCurrentFrame which is a UI widget, and the main window doesn't have direct access to `mpegWindow`, the search should operate at a lower level using `mpCurrentAVDataItem->videoStream()`.

Implementation approach:
1. Get `TTVideoStream* vs` from `mpCurrentAVDataItem->videoStream()`
2. For MPEG-2: use the video stream's internal decoder via `vs->moveToIndexPos(pos)` + access decoded frame data
3. For H.264/H.265: use `TTFFmpegWrapper` to decode individual frames

Since the main window doesn't have direct decoder access, the most practical approach is to open a **temporary decoder** for the search (similar to how the video worker did it). This avoids interfering with the display decoder's state.

```cpp
void TTCutMainWindow::onSearchBlackFrame(int startPos, int direction, float threshold)
{
  if (!mpCurrentAVDataItem) return;

  TTVideoStream* vs = mpCurrentAVDataItem->videoStream();
  if (!vs) return;

  int frameCount = vs->frameCount();
  if (frameCount <= 0) return;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  // Open temporary decoder for search
  AVFormatContext* fmtCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  int videoIdx = -1;
  QString filePath = vs->filePath();

  // Use avformat to open the file
  if (avformat_open_input(&fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0) {
    QApplication::restoreOverrideCursor();
    return;
  }
  avformat_find_stream_info(fmtCtx, nullptr);
  videoIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoIdx < 0) {
    avformat_close_input(&fmtCtx);
    QApplication::restoreOverrideCursor();
    return;
  }

  const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[videoIdx]->codecpar->codec_id);
  codecCtx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecCtx, fmtCtx->streams[videoIdx]->codecpar);
  codecCtx->thread_count = 0;
  avcodec_open2(codecCtx, codec, nullptr);

  // Decode frames sequentially, check each one
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int frameCounter = 0;
  int foundPos = -1;
  const int pixelThreshold = 18;  // Y <= 17

  // For forward search: decode from start, skip until startPos
  // For backward search: decode from start, remember last black before startPos
  int searchFrom = (direction > 0) ? startPos + 1 : 0;
  int searchTo = (direction > 0) ? frameCount : startPos;
  int lastBlackBefore = -1;

  while (av_read_frame(fmtCtx, pkt) >= 0) {
    if (pkt->stream_index == videoIdx) {
      avcodec_send_packet(codecCtx, pkt);
      while (avcodec_receive_frame(codecCtx, frame) >= 0) {
        if (direction > 0 && frameCounter > startPos) {
          // Forward: check if black
          if (isFrameBlack(frame, pixelThreshold, threshold)) {
            foundPos = frameCounter;
            goto done;
          }
        } else if (direction < 0 && frameCounter < startPos) {
          // Backward: remember last black pos before startPos
          if (isFrameBlack(frame, pixelThreshold, threshold)) {
            lastBlackBefore = frameCounter;
          }
        }

        // Process events periodically for responsiveness
        if (frameCounter % 500 == 0)
          QApplication::processEvents();

        frameCounter++;
        av_frame_unref(frame);
      }
    }
    av_packet_unref(pkt);
  }

  // Flush decoder
  avcodec_send_packet(codecCtx, nullptr);
  while (avcodec_receive_frame(codecCtx, frame) >= 0) {
    if (direction > 0 && frameCounter > startPos) {
      if (isFrameBlack(frame, pixelThreshold, threshold)) {
        foundPos = frameCounter;
        av_frame_unref(frame);
        goto done;
      }
    } else if (direction < 0 && frameCounter < startPos) {
      if (isFrameBlack(frame, pixelThreshold, threshold))
        lastBlackBefore = frameCounter;
    }
    frameCounter++;
    av_frame_unref(frame);
  }

done:
  if (direction < 0)
    foundPos = lastBlackBefore;

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&codecCtx);
  avformat_close_input(&fmtCtx);

  QApplication::restoreOverrideCursor();

  if (foundPos >= 0) {
    onVideoSliderChanged(foundPos);
  } else {
    statusBar()->showMessage(tr("Kein Schwarzbild gefunden"), 3000);
  }
}
```

Add the `isFrameBlack` helper as a private method:
```cpp
bool TTCutMainWindow::isFrameBlack(AVFrame* frame, int pixelThreshold, float ratioThreshold)
{
  if (!frame->data[0] || frame->width <= 0 || frame->height <= 0)
    return false;

  int w = frame->width;
  int h = frame->height;
  int x0 = w / 10, y0 = h / 10;
  int x1 = w - x0, y1 = h - y0;

  int totalPixels = 0;
  int blackPixels = 0;
  const uint8_t* yPlane = frame->data[0];
  int linesize = frame->linesize[0];

  for (int row = y0; row < y1; row++) {
    const uint8_t* line = yPlane + row * linesize;
    for (int col = x0; col < x1; col++) {
      totalPixels++;
      if (line[col] < pixelThreshold)
        blackPixels++;
    }
  }

  if (totalPixels == 0) return false;
  return (float)blackPixels / totalPixels >= ratioThreshold;
}
```

Add necessary includes at top of `ttcutmainwindow.cpp`:
```cpp
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
```

- [ ] **Step 4: Add isFrameBlack declaration to header**

In `gui/ttcutmainwindow.h`, add in private section:
```cpp
struct AVFrame;
bool isFrameBlack(AVFrame* frame, int pixelThreshold, float ratioThreshold);
```

- [ ] **Step 5: Build**

```bash
rm -f obj/ttcutmainwindow.o && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add gui/ttcutmainwindow.h gui/ttcutmainwindow.cpp
git commit -m "Implement interactive black frame search from current position"
```

---

### Task 5: Remove batch black frame detection

**Files:**
- Modify: `gui/ttstreampointwidget.cpp`
- Modify: `data/ttstreampoint_videoworker.h`
- Modify: `data/ttstreampoint_videoworker.cpp`
- Modify: `common/ttcut.h`
- Modify: `common/ttcut.cpp`

- [ ] **Step 1: Remove black frame UI from settings tab**

In `gui/ttstreampointwidget.cpp`, remove:
- `mCbBlackFrames` checkbox creation and `gl->addWidget` (the "Schwarzbilder" checkbox)
- `mSbBlackThreshold` spinbox creation and label
- `mSbBlackMinDuration` spinbox creation and label
- Related `loadSettings()`/`saveSettings()` lines for these widgets
- Member declarations in the header if they exist

- [ ] **Step 2: Remove from video worker**

In `data/ttstreampoint_videoworker.cpp`:
- Remove `detectBlackFrames()` method entirely
- Remove `isBlackFrame()` method entirely
- Remove `mDetectBlack` usage from `operation()` (the `if (mDetectBlack...)` block)
- Remove the `bool detectBlack, float blackThreshold, float blackMinDuration` parameters from constructor

In `data/ttstreampoint_videoworker.h`:
- Remove `detectBlackFrames()` declaration
- Remove `isBlackFrame()` declaration
- Remove `mDetectBlack`, `mBlackThreshold`, `mBlackMinDuration` members
- Update constructor signature

- [ ] **Step 3: Update call sites**

In `gui/ttcutmainwindow.cpp`, update where `TTStreamPointVideoWorker` is constructed to remove the black frame parameters.

- [ ] **Step 4: Remove TTCut settings variables**

In `common/ttcut.h`, remove:
```cpp
static bool   spDetectBlackFrames;
static float  spBlackThreshold;
static float  spBlackMinDuration;
```

In `common/ttcut.cpp`, remove their initialization.

- [ ] **Step 5: Build**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add gui/ttstreampointwidget.cpp gui/ttstreampointwidget.h \
        data/ttstreampoint_videoworker.h data/ttstreampoint_videoworker.cpp \
        gui/ttcutmainwindow.cpp common/ttcut.h common/ttcut.cpp
git commit -m "Remove batch black frame detection from Landezonen analysis"
```

---

### Task 6: Final build and test

- [ ] **Step 1: Clean build**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 2: Test with MPEG-2 file**

1. Open a MPEG-2 video file
2. Navigate to a position near an ad break
3. Press the ▶ black frame button
4. Verify it jumps to the next black frame
5. Press ◀ to go back to the previous one
6. Adjust threshold and repeat

- [ ] **Step 3: Test with H.264 file**

Same test with an H.264 ES file.

- [ ] **Step 4: Test Landezonen**

1. Verify Stille and Audioformatwechsel still work in Landezonen analysis
2. Verify Schwarzbilder checkbox is gone from Einstellungen tab

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Black frame navigation: final build verification"
```
