# Screenshot Automation Design

**Date:** 2026-03-21
**Type:** Feature / Tooling

## Problem

Wiki and README screenshots must be updated manually after UI changes. The process is error-prone (inconsistent cropping, wrong state, missing Tux test video) and easy to forget during releases.

## Design

### CLI Mode: `--screenshots`

TTCut-ng gets a new CLI mode that loads a test project, captures all relevant widgets and dialogs via `QWidget::grab()`, and exits.

```bash
./ttcut-ng --screenshots <output-dir> --project <prj-file>
```

### Screenshots (15 total)

| # | Filename | Source | Action |
|---|----------|--------|--------|
| 1 | ttcutng-main.png | TTCutMainWindow | Project loaded |
| 2 | ttcutng-frames.png | currentFrame + cutOutFrame area | - |
| 3 | ttcutng-nav-panel.png | navigation widget | - |
| 4 | ttcutng-action-buttons.png | Action buttons (right of cut list) | - |
| 5 | ttcutng-controls.png | streamNavigator/slider | - |
| 6 | ttcutng-cutlist-detail.png | cutList widget | Cuts must exist |
| 7 | ttcutng-tabs.png | Tabs (Videodatei/Audio/Untertitel) | - |
| 8 | ttcutng-landezonen.png | mpStreamPointWidget (Landezonen tab) | Run analysis |
| 9 | ttcutng-landezonen-settings.png | mpStreamPointWidget (Settings tab) | Switch tab |
| 10 | ttcutng-zeitsprung.png | TTQuickJumpDialog | Open dialog |
| 11 | ttcutng-preview.png | TTCutPreview | Open preview |
| 12 | ttcutng-settings.png | TTCutSettingsDlg | Open settings |
| 13 | ttcutng-about.png | TTCutAboutDlg | Open about |
| 14 | ttcutng-shortcuts.png | Keyboard shortcuts dialog | Open help |
| 15 | MainWindow.png | = ttcutng-main.png | Copy for README |

### Internal Flow

```
1. Parse --screenshots and --project arguments (QCommandLineParser)
2. Load project, wait until all streams loaded
3. Capture main window + widget screenshots (1-7)
4. Start Landezonen analysis, wait until finished
5. Capture Landezonen tab (8), switch to Settings tab, capture (9)
6. Open Zeitsprung dialog, capture (10), close
7. Open Preview dialog, capture (11), close
8. Open Settings dialog, capture (12), close
9. Open About dialog, capture (13), close
10. Open Shortcuts dialog, capture (14), close
11. Scale all PNGs to max 1200px width
12. Exit with code 0
```

### Bash Wrapper: `tools/ttcut-screenshots.sh`

```bash
#!/usr/bin/bash
# 1. Generate Tux test video if not present
# 2. Run TTCut-ng in screenshot mode
# 3. Copy MainWindow.png to docs/
```

Generates video from `ui/pixmaps/Tux.svg` using ffmpeg (30min H.264 ES with black frame segments, scene changes, AC3 stereo/5.1 changes).

### Test Project: `tools/ttcut-test.prj`

XML project file referencing the generated test video. Contains 3 cuts at positions with acmod changes and scene transitions. Stored in Git (< 1KB).

### Files in Git

| File | Size | Purpose |
|------|------|---------|
| `ui/pixmaps/Tux.svg` | 27KB | Source image for test video |
| `tools/ttcut-test.prj` | < 1KB | Test project with 3 cuts |
| `tools/ttcut-screenshots.sh` | ~2KB | Wrapper script |
| `gui/ttcutmain.cpp` | Modify | QCommandLineParser for --screenshots |
| `gui/ttcutmainwindow.h/.cpp` | Modify | runScreenshotMode() method |

### New Wiki Pages

| Page | Screenshots | Content |
|------|-----------|---------|
| Settings.md | ttcutng-settings.png | All settings tabs explained |
| Preview.md | ttcutng-preview.png | Preview function, burst shift |

### Advantages

- Pixel-accurate widget screenshots via QWidget::grab()
- No external tools needed (no xdotool, no ImageMagick import)
- Reproducible, deterministic output
- Works on both Wayland and X11
- Always uses Tux test video (no copyright issues)
- Integrated in release skill (step 4.5)
