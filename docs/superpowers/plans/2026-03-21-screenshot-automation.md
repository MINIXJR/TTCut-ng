# Screenshot Automation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `--screenshots` CLI mode to TTCut-ng that captures all widgets and dialogs via QWidget::grab() for reproducible Wiki/README screenshots.

**Architecture:** QCommandLineParser detects `--screenshots` flag in main(), loads test project, triggers runScreenshotMode() in TTCutMainWindow which sequentially opens each dialog, captures widgets, scales to max 1200px, and exits. Bash wrapper generates test video from Tux SVG.

**Tech Stack:** Qt5 (QCommandLineParser, QWidget::grab(), QPixmap), ffmpeg (video generation), bash

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `gui/ttcutmain.cpp` | Modify | QCommandLineParser for --screenshots + --project |
| `gui/ttcutmainwindow.h` | Modify | Add runScreenshotMode() declaration |
| `gui/ttcutmainwindow.cpp` | Modify | Implement runScreenshotMode() |
| `tools/ttcut-screenshots.sh` | Create | Wrapper: generate video + run screenshot mode |
| `tools/ttcut-test.prj` | Create | Test project with 3 cuts |
| `ui/pixmaps/Tux.svg` | Create | Tux source image (download from Wikimedia) |

---

## Task 1: Add Tux SVG and test project

**Files:**
- Create: `ui/pixmaps/Tux.svg`
- Create: `tools/ttcut-test.prj`

- [ ] **Step 1: Download Tux SVG**

```bash
wget -O ui/pixmaps/Tux.svg "https://upload.wikimedia.org/wikipedia/commons/3/35/Tux.svg"
```

- [ ] **Step 2: Create test project file**

Create `tools/ttcut-test.prj` referencing the generated video path. The video will be at `/usr/local/src/CLAUDE_TMP/tux_video.264` with audio at `/usr/local/src/CLAUDE_TMP/tux_video_deu.ac3`.

Project must contain 3 cuts at positions with scene changes:
- Cut 1: frames 0-7500 (0:00-5:00, black background)
- Cut 2: frames 10075-14999 (6:43-10:00, includes black→blue transition)
- Cut 3: frames 22575-29999 (15:03-20:00, includes black→red transition)

- [ ] **Step 3: Commit**

---

## Task 2: Add QCommandLineParser

**Files:**
- Modify: `gui/ttcutmain.cpp`

- [ ] **Step 1: Add command line parsing**

Before creating TTCutMainWindow, add:

```cpp
QCommandLineParser parser;
parser.setApplicationDescription("TTCut-ng - Frame-accurate video cutter");
parser.addHelpOption();

QCommandLineOption screenshotOpt("screenshots",
    "Capture all widget screenshots to <dir> and exit.", "dir");
QCommandLineOption projectOpt("project",
    "Load project file <file>.", "file");
parser.addOption(screenshotOpt);
parser.addOption(projectOpt);
parser.process(a);
```

Pass parsed values to TTCutMainWindow or store in TTCut statics.

- [ ] **Step 2: If --screenshots is set, trigger screenshot mode after window is shown**

```cpp
if (parser.isSet(screenshotOpt)) {
    TTCut::screenshotDir = parser.value(screenshotOpt);
    TTCut::screenshotProject = parser.value(projectOpt);
    // Delay execution until event loop is running
    QTimer::singleShot(500, mainWnd, &TTCutMainWindow::runScreenshotMode);
}
```

- [ ] **Step 3: Add static strings to TTCut**

In `common/ttcut.h`:
```cpp
static QString screenshotDir;
static QString screenshotProject;
```

In `common/ttcut.cpp`:
```cpp
QString TTCut::screenshotDir;
QString TTCut::screenshotProject;
```

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
./ttcut-ng --help  # should show --screenshots and --project options
```

- [ ] **Step 5: Commit**

---

## Task 3: Implement runScreenshotMode()

**Files:**
- Modify: `gui/ttcutmainwindow.h`
- Modify: `gui/ttcutmainwindow.cpp`

- [ ] **Step 1: Add method declaration**

In `ttcutmainwindow.h`, public slots:
```cpp
void runScreenshotMode();
```

- [ ] **Step 2: Implement screenshot capture helper**

Private helper that grabs a widget and saves to the output dir:

```cpp
void TTCutMainWindow::saveWidgetScreenshot(QWidget* widget, const QString& filename, int maxWidth)
{
    QPixmap pixmap = widget->grab();
    if (maxWidth > 0 && pixmap.width() > maxWidth) {
        pixmap = pixmap.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    }
    QString path = QDir(TTCut::screenshotDir).filePath(filename);
    pixmap.save(path, "PNG");
    qDebug() << "Screenshot saved:" << path << pixmap.width() << "x" << pixmap.height();
}
```

- [ ] **Step 3: Implement runScreenshotMode()**

```cpp
void TTCutMainWindow::runScreenshotMode()
{
    QString projectFile = TTCut::screenshotProject;
    if (projectFile.isEmpty()) {
        qDebug() << "Screenshot mode: no --project specified";
        QApplication::quit();
        return;
    }

    QDir outDir(TTCut::screenshotDir);
    if (!outDir.exists()) outDir.mkpath(".");

    // Load project
    openProjectFile(projectFile);

    // Wait for project to load (process events until avCount > 0)
    QElapsedTimer timer;
    timer.start();
    while (mpAVData->avCount() == 0 && timer.elapsed() < 30000) {
        QApplication::processEvents();
        QThread::msleep(100);
    }

    // Wait a bit more for audio streams
    QThread::msleep(2000);
    QApplication::processEvents();

    int maxW = 1200;

    // 1. Main window
    saveWidgetScreenshot(this, "ttcutng-main.png", maxW);

    // 2. Frames area
    saveWidgetScreenshot(currentFrame, "ttcutng-frames.png", maxW);

    // 3. Navigation panel
    saveWidgetScreenshot(navigation, "ttcutng-nav-panel.png", 0);

    // 4. Cut list
    saveWidgetScreenshot(cutList, "ttcutng-cutlist-detail.png", maxW);

    // 5. Stream navigator / controls
    saveWidgetScreenshot(streamNavigator, "ttcutng-controls.png", maxW);

    // 6. Tabs (video file info area)
    saveWidgetScreenshot(videoFileInfo, "ttcutng-tabs.png", 0);

    // 7. Action buttons area — grab right side of cut list widget
    // (action buttons are part of the cutList widget layout)

    // 8. Landezonen: run analysis
    onAnalyzeStreamPoints();
    timer.restart();
    while (mStreamPointWorkersRunning > 0 && timer.elapsed() < 60000) {
        QApplication::processEvents();
        QThread::msleep(100);
    }
    QApplication::processEvents();
    saveWidgetScreenshot(mpStreamPointWidget, "ttcutng-landezonen.png", 0);

    // 9. Landezonen settings tab
    // Switch to settings tab programmatically
    mpStreamPointWidget->showSettingsTab();
    QApplication::processEvents();
    saveWidgetScreenshot(mpStreamPointWidget, "ttcutng-landezonen-settings.png", 0);
    mpStreamPointWidget->showLandezonenTab();

    // 10. Zeitsprung dialog
    onQuickJump();
    QApplication::processEvents();
    QThread::msleep(3000);  // Wait for thumbnails
    QApplication::processEvents();
    // Find the dialog
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<TTQuickJumpDialog*>(w)) {
            saveWidgetScreenshot(w, "ttcutng-zeitsprung.png", maxW);
            w->close();
            break;
        }
    }

    // 11. Preview dialog
    TTCutList* previewList = cutList->cutListFromSelection(true);
    if (previewList && previewList->count() > 0) {
        onCutPreview(previewList);
        QThread::msleep(5000);
        QApplication::processEvents();
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (qobject_cast<TTCutPreview*>(w)) {
                saveWidgetScreenshot(w, "ttcutng-preview.png", maxW);
                w->close();
                break;
            }
        }
    }

    // 12. Settings dialog
    onActionSettings();
    QApplication::processEvents();
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<TTCutSettingsDlg*>(w)) {
            saveWidgetScreenshot(w, "ttcutng-settings.png", 0);
            w->close();
            break;
        }
    }

    // 13. About dialog
    onHelpAbout();
    QApplication::processEvents();
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog*>(w) && w != this) {
            saveWidgetScreenshot(w, "ttcutng-about.png", 0);
            w->close();
            break;
        }
    }

    // 14. Keyboard shortcuts
    onHelpKeyboardShortcuts();
    QApplication::processEvents();
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog*>(w) && w != this) {
            saveWidgetScreenshot(w, "ttcutng-shortcuts.png", 0);
            w->close();
            break;
        }
    }

    // 15. Copy main window as docs/MainWindow.png
    QFile::copy(outDir.filePath("ttcutng-main.png"),
                QFileInfo(QDir(QApplication::applicationDirPath()), "../docs/MainWindow.png").absoluteFilePath());

    qDebug() << "Screenshot mode complete:" << outDir.absolutePath();
    QApplication::quit();
}
```

Note: The exact widget names (currentFrame, navigation, cutList, etc.) must match the member variable names in TTCutMainWindow. Some widgets may need `showSettingsTab()`/`showLandezonenTab()` methods added to TTStreamPointWidget.

- [ ] **Step 4: Add showSettingsTab/showLandezonenTab to TTStreamPointWidget**

Simple methods to switch the tab widget programmatically.

- [ ] **Step 5: Build and test**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
QT_QPA_PLATFORM=xcb ./ttcut-ng --screenshots /usr/local/src/CLAUDE_TMP/screenshots --project tools/ttcut-test.prj
ls -la /usr/local/src/CLAUDE_TMP/screenshots/
```

- [ ] **Step 6: Commit**

---

## Task 4: Create bash wrapper script

**Files:**
- Create: `tools/ttcut-screenshots.sh`

- [ ] **Step 1: Write wrapper script**

```bash
#!/usr/bin/bash
# Generate Wiki/README screenshots for TTCut-ng
# Uses Tux test video (generated from SVG if not present)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TMPDIR="${TMPDIR:-/usr/local/src/CLAUDE_TMP}"
WIKI_DIR="/usr/local/src/TTCut-ng.wiki/images"
SVG="$PROJECT_DIR/ui/pixmaps/Tux.svg"
VIDEO="$TMPDIR/tux_video.264"
AUDIO="$TMPDIR/tux_video_deu.ac3"
PRJ="$SCRIPT_DIR/ttcut-test.prj"

# Generate test video if not present
if [ ! -f "$VIDEO" ] || [ ! -f "$AUDIO" ]; then
    echo "Generating Tux test video..."
    # Convert SVG to PNG
    convert "$SVG" -resize 720x576 -background black -gravity center -extent 720x576 "$TMPDIR/tux.png"

    # Create video with scene changes (black/blue/red/green backgrounds)
    # ... (ffmpeg commands from create_test_video.sh)

    # Create AC3 with stereo/5.1 changes
    # ... (ffmpeg commands from create_test_audio.sh)
    echo "Test video generated."
fi

# Determine output directory
OUTPUT_DIR="${1:-$WIKI_DIR}"
mkdir -p "$OUTPUT_DIR"

echo "Capturing screenshots to: $OUTPUT_DIR"
cd "$PROJECT_DIR"
QT_QPA_PLATFORM=xcb ./ttcut-ng --screenshots "$OUTPUT_DIR" --project "$PRJ"

# Copy main window to docs/
cp "$OUTPUT_DIR/ttcutng-main.png" "$PROJECT_DIR/docs/MainWindow.png"

echo "Screenshots complete."
ls -la "$OUTPUT_DIR"/*.png
```

- [ ] **Step 2: Make executable**

```bash
chmod +x tools/ttcut-screenshots.sh
```

- [ ] **Step 3: Test end-to-end**

```bash
bash tools/ttcut-screenshots.sh /usr/local/src/CLAUDE_TMP/test-screenshots
```

- [ ] **Step 4: Commit**

---

## Task 5: Update release skill and Wiki

- [ ] **Step 1: Update SKILL.md step 4.5**

Reference `tools/ttcut-screenshots.sh` for automated screenshot capture.

- [ ] **Step 2: Create Wiki pages Settings.md and Preview.md**

Add new pages with screenshots and descriptions.

- [ ] **Step 3: Update _Sidebar.md**

Add links to new pages.

- [ ] **Step 4: Commit**
