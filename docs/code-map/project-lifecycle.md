---
base_commit: 6f6574936b90725072fa008d50194fa44a3aa2a9
last_verified: 2026-09-24
sources:
  - gui/ttcutmainwindow.h
  - gui/ttcutmainwindow.cpp
  - gui/ttcutmainwindow_headless.cpp
  - gui/ttcutmain.cpp
  - data/ttavdata.h
  - data/ttavdata.cpp
  - data/ttavlist.h
  - data/ttcutprojectdata.h
  - data/ttcutprojectdata.cpp
  - data/ttmarkerlist.h
  - common/ttsettings.h
  - common/ttsettings.cpp
---

# Code Map: Project lifecycle

**Scope:** What a "project" is while TTCut-ng runs and how it is born, named,
marked dirty, saved, closed and remembered: the two identities
(`mProjectDisplayName` for the title, `TTSettings::projectFileName` as the
save target), the dirty flag and its sources, New / Open / Save / Save as /
Recent / Exit / window close, the `.ttcut` file (what `TTCutProjectData`
writes and reads back, what does not round-trip), the command line and the
two headless modes. The **load chain** itself (tasks, pool exit, pending
maps, `onReadProjectFileFinished` order) is `stream-open-project-load.md`;
what the `<Settings>` section does to `TTSettings` is `settings-state.md`;
the stream points and the logo profile that ride along are
`stream-points.md`. This map starts at the menu and ends at the file.

## Data flow

Solid edges carry data (producer → consumer); dashed edges are triggers.

```mermaid
flowchart TD
    MENU["File menu / toolbar<br/>New, Open, Save, Save as, Recent, Exit"]
    CLI["gui/ttcutmain.cpp<br/>positional file, --project,<br/>--auto-cut, --screenshots"]
    HEADLESS["runAutoCutMode /<br/>runScreenshotMode"]
    OPEN["TTCutMainWindow::openProjectFile"]
    LOAD["load chain: readXml, deserialize*, tasks<br/>(stream-open-project-load.md)"]
    FIN["onOpenProjectFileFinished /<br/>onOpenProjectFileAborted"]
    IDENT["Project identity<br/>mProjectDisplayName,<br/>TTSettings::projectFileName"]
    DIRTY["Dirty flag<br/>mProjectModified"]
    SIGS["TTAVData<br/>cutItem*, avItem*, marker* signals"]
    TITLE["updateWindowTitle"]
    SAVE["onFileSave / onFileSaveAs"]
    WRITE["TTAVData::writeProjectFile"]
    PRJ["TTCutProjectData<br/>(data/ttcutprojectdata.cpp)"]
    FILE[(".ttcut file<br/>Version, Video…, StreamPoint…,<br/>LogoProfile, Settings")]
    RECENT["Recent list<br/>TTSettings::recentFileList<br/>insertRecentFile / updateRecentFileActions"]
    CLOSE["closeProject"]
    EXIT["closeEvent / onFileExit"]
    SETTINGS["TTSettings<br/>(settings-state.md)"]

    MENU -. "New" .-> CLOSE
    MENU -- "chosen .ttcut / .prj path" --> OPEN
    RECENT -- "action data = path" --> OPEN
    CLI -- "path (.ttcut/.prj → project, else video)" --> OPEN
    CLI -. "--auto-cut / --screenshots" .-> HEADLESS
    HEADLESS -- "project path" --> OPEN
    OPEN -. "closeProject if items exist" .-> CLOSE
    OPEN -- "QFileInfo" --> LOAD
    LOAD -. "readProjectFileFinished(path) / Aborted" .-> FIN
    FIN -- "display name, clean flag" --> IDENT
    FIN -- "path" --> RECENT
    SIGS -. "onProjectModified" .-> DIRTY
    DIRTY -- "* suffix" --> TITLE
    IDENT -- "base name" --> TITLE
    MENU -. "Save / Save as" .-> SAVE
    SAVE -- "save target (dialog when empty)" --> IDENT
    SAVE -- "QFileInfo, stream points, logo data" --> WRITE
    WRITE -- "AV items, points, logo" --> PRJ
    SETTINGS -- "cut output + working set + encoder" --> PRJ
    PRJ -- "writeXml (truncate)" --> FILE
    SAVE -. "setProjectModified(false)" .-> DIRTY
    MENU -. "Exit → close()" .-> EXIT
    EXIT -. "save dialog when dirty, then" .-> CLOSE
    CLOSE -- "clear identity, clean flag" --> IDENT
    CLOSE -. "TTSettings::load(), clear projectFileName / cutVideoName" .-> SETTINGS
```

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| Menu / Recent / CLI → `openProjectFile` | A file path. The menu dialog filters `*.ttcut` and "Legacy Project (*.prj)"; `.prj` is **the same XML format** under the old extension (renamed in v0.63.0, no converter exists). The command line treats a positional file by extension (`.prj`/`.ttcut` → project, anything else → `onReadVideoStream`), takes `--project` only when no positional file exists, and defers the open by a 100 ms timer so the window is up. Recent entries carry the path in `QAction::data`. |
| `openProjectFile` → `closeProject` → load chain | `closeProject` runs first **only when `avCount() > 0`**; `lastDirPath` is set to the project's directory; `mProjectLoadInProgress = true`; the finished/aborted connections to `TTAVData` are made per open and dropped again in the handlers. Then `TTAVData::readProjectFile` (`stream-open-project-load.md`). |
| Load chain → `onOpenProjectFileFinished` | `readProjectFileFinished(path)` after `TTAVData` has restored items, stream points, logo data and the `<Settings>` section. The handler releases the flag, queues the auto anomaly scan, and — only when a current item exists — inserts the path into the recent list, sets `mProjectDisplayName` to the base name and **clears the dirty flag** (the `avItemAppended` signals of the load had set it). It does **not** set `TTSettings::projectFileName` (see pitfalls). `onOpenProjectFileAborted` releases the flag and the connections and calls `closeProject()` unconditionally (a second, harmless run when `onAVItemChanged(nullptr)` already closed): no recent entry, dirty flag cleared, cut list empty. |
| `TTAVData` signals → `onProjectModified` | Exactly eight signals mark the project dirty: `cutItemAppended/Removed/Updated`, `cutOrderUpdated`, `avItemAppended/Removed`, `markerAppended/Removed`. Cuts and the video list, that is. **Not** dirty-tracked: stream points (manual marker, analysis, delete), audio repairs, audio/subtitle language and delay, track reorder, the logo profile, and every `<Settings>` value (output name, mux working set, encoder) — all of which the file stores. `onProjectModified` only flips false → true and refreshes the title. |
| Dirty flag / identity → `updateWindowTitle` | `"<display name> - <version>"` or just the version string, plus `" *"` when dirty. The display name is the base name of the last opened **or saved** project; empty for a plain video. A plain video open therefore shows `"TTCut-ng x.y *"` — dirty through `avItemAppended`, no name. |
| `onFileSave` → identity → `writeProjectFile` | Refuses without items. When `projectFileName` is empty it asks through `askProjectFileName` (default `<video base>.ttcut` in `lastDirPath`) and returns on cancel without changing anything. Suffix `.ttcut` appended when the chosen name has none. Returns whether a file was written — the close handler needs that. Then `TTLogoProjectData` is built from `mLogoDetector` (markad path **or** ROI) and `writeProjectFile(fInfo, mpStreamPointModel->points(), logoData)` runs inside `try`: a `TTException` is logged (`errorMsg`) and the method returns — no dialog, flag stays dirty, display name unchanged. On success: display name = chosen base name, flag clean. `insertRecentFile` is **not** called on save. |
| `onFileSaveAs` → `onFileSave` | Asks through the same `askProjectFileName` (default `<video base>.ttcut`, not the current project's name), and only a real answer becomes the save target and the new `lastDirPath`; then `onFileSave` writes without asking again. Cancel changes nothing — until `ea7f763a` the empty dialog result was assigned and a project that had a save target lost it. |
| `writeProjectFile` → `TTCutProjectData` → file | A fresh `TTCutProjectData` per save: `serializeAVDataItem` per `TTAVList` entry, `serializeStreamPoints` only when the list is non-empty, `serializeLogoData` only when `valid`, then `writeXml` = `serializeSettings` + open with `Truncate` + `write(toByteArray())`. No temp file, no rename, no backup: a failure after `open` leaves a truncated file. Open failure throws `TTIOException` with `errorString`. |
| `serializeAVDataItem` → `<Video>` | Per item: `<Order>` (always 0), `<Name>` = the video stream's **absolute** `filePath()`; per audio track a `<Audio>` with `<Order>` = the **visible list position** (`sortByProjectOrder` restores exactly that), `<Name>` absolute, `<Language>` and `<Delay>` only when non-empty / non-zero, nested `<Repair>` elements for repairs whose `trackIndex()` equals that position; `<Cut>` with order, in, out (display-order positions, see `parseCutSection` note); `<Marker>` with order, pos and a `<MarkerType>` that is always `1`; `<Subtitle>` like audio, `<Order>` = the visible list position too, not `sItem.order()`, which stays −1 for discovered or added subtitles. Written paths are absolute; the reader also accepts relative ones. |
| `<Settings>` ↔ `TTSettings` | Written from the **working set** and the transient encoder values, never from the App-Defaults; read back into the same (`settings-state.md`). `CutDirPath` goes through `resolveProjectPath` (absolute or project-relative, no `..`), `CutVideoName` must be a bare file name. |
| `readXml` → version | Parses with `QDomDocument::setContent`; unreadable or unparsable file → `TTDataFormatException` → `TTAVData::onReadProjectFileAborted`. The version check (`<Version>` rounded to int must be 1) only prints a `qDebug` and **returns normally** — `readProjectFile` then deserialises the document anyway. A `TTCut-Projectfile` root is not checked either; any XML whose first element is not `Version` passes with the same debug line. |
| `deserializeAVDataItem` → `parseVideoSection` | Every top-level `<Video>` (children from index 1 on, `Version` skipped); each needs at least `<Order>` and `<Name>`; a path `resolveProjectPath` rejects skips the whole section with a warning. Both refusals live in `parseSectionHeader`, shared with the `<Audio>` and `<Subtitle>` parsers since `3c888bf1`. `deserializeAVDataItem` returns how many sections started an open task, and `readProjectFile` ends the load itself at zero (see pitfalls). A video that passes gets `doOpenVideoStream(name, order)` — which creates the `TTAVItem` and enqueues the open task; the item joins `TTAVList` only in `onOpenVideoFinished` (`avItemAppended` → dirty), so right after `deserializeAVDataItem` the list is still empty even for a good project. Cuts and markers are appended synchronously, audio/subtitle tracks become tasks; `sortCutItemsByOrder`/`sortMarkerByOrder` run per video section on the global lists. Cuts go in through `TTAVItem::appendCutEntry`, which validates the range: a project file is the one route that never passed a navigator, and a rejected range is skipped with a warning. |
| `<Marker>` ↔ `TTMarkerList` | Legacy list, separate from the stream points: its only writers today are the project parser and the VDR import (`openAVStreams` appends cut-in and cut-out of every VDR pair "for the Marker tab"). **No GUI widget reads `TTMarkerList`** (grep over `gui/` and `ui/`), the "Set marker" button feeds the stream-point model since the Landezonen feature. Markers therefore round-trip invisibly and are the only thing besides cuts that `markerAppended` marks dirty for. |
| `onOpenProjectFileFinished` → recent list | `insertRecentFile(path)`: read-modify-write on `TTSettings::recentFileList` (dedupe, prepend, cap 5). `setRecentFileList` emits `recentFilesChanged`, which every main window's constructor connects to `updateRecentFileActions` — that is the whole refresh since `ea7f763a` (before, `insertRecentFile` walked `QApplication::topLevelWidgets()` and the signal had no receiver). Persisted in QSettings group `RecentFiles`; the menu is built from it in the constructor. |
| `closeEvent` → `closeProject` | Order: save window geometry to QSettings, `TTSettings::save()`, then — if dirty — Save / Discard / Cancel. Cancel and a Save that wrote nothing (`onFileSave` returns false: cancelled file dialog, failed write) both `event->ignore()` and return; otherwise `closeProject`, `accept`. `mpAVData` is deleted in the destructor, not here. |
| `onFileExit` → `close()` | Only `close()`; the application ends through `quitOnLastWindowClosed` and `main()`'s `lastWindowClosed` connection, the same route the window's own close button takes. Until `5e47997b` it also called `qApp->quit()` unconditionally: on Qt 6.10 that runs `closeEvent` on the open windows a second time (measured), so Cancel produced the question twice, and "Save" with a cancelled file dialog ended the application with the changes unsaved. |
| `onFileNew` → `closeProject` | Refuses without items; asks only when dirty ("Close current project and start a new one?"); then `closeProject` — "new" is nothing but "close". |
| `closeProject` → everything | Releases the load flag; aborts a running directed search and the stream-point pool and waits for the global `QThreadPool` (tasks hold pointers into the lists `mpAVData->clear()` frees); disconnects the two AV signals while the widgets are nulled; clears the stream-point model and the extras; `mpAVData->clear()` (AV, cut and marker lists); `TTSettings::load()` **then** `setProjectFileName("")` (order matters: a stale name must not come back from QSettings — it is not persisted since `8008128`, the order is the second guard), `setCutVideoName("")`; display name cleared, flag clean; signals reconnected. |
| Headless → `openProjectFile` | `runAutoCutMode`: `setNonInteractive(true)`, open, `waitForProjectLoad(60 s)` (pumps until the flag falls), needs a non-empty cut list, sets the encoder codec from the stream type, then container, `cutDirPath`, `muxOutputPath` and `cutVideoName` from the CLI path (details in `settings-state.md`, edge `HEAD` → `ID`/`WORK`), connects `cutFinished` to a handler that picks the exit code from `lastCutError()`, `onDoCut`. Every end of the run goes through `endAutoCut(code)`: clear the modified flag, store the code (`headlessExitCode()`, returned by `main()`), `quit()` — 0 only for a completed cut, 1 for a project that did not load, no cuts, a wrong output extension or a failed cut (gate `autocut_exit`). `runScreenshotMode`: open, `waitForProjectLoad(30 s)`, a warning on timeout but it carries on. Neither calls `save()` itself (the `quit()` of `--auto-cut` reaches `closeEvent`, which does — and which runs `closeProject` before teardown; `QApplication::exit()` would skip both, which is why the exit code travels separately); neither touches the recent list beyond what `onOpenProjectFileFinished` does. |

## Assumptions, contracts & pitfalls

- **Two identities by design** — `mProjectDisplayName` is for the title and set by open and save; `TTSettings::projectFileName` is the save target and set **only by saving** (header comment). Consequence: after Open, the first Save behaves like Save as, pre-filled with `<video base>.ttcut` in `lastDirPath`, not with the opened file. Saving under the opened name is the user's job. Read-derived.
- **Exit and the close event** — `onFileExit` is `close()` and nothing else, so the unsaved-changes dialog decides. Gate: `tools/diag/test_exit_cancel` (Cancel keeps the window and asks once, Discard closes and ends the application, Save with a cancelled file dialog keeps the window). Measured on the way there: `qApp->quit()` on Qt 6.10.2 runs `closeEvent` on the open windows instead of leaving the loop, which is why the old code asked twice rather than quitting.
- **Dirty flag covers cuts and the video list only** — stream points, repairs, delays, languages, logo and the `<Settings>` values are saved but never mark the project dirty; New / Exit do not warn about them. A plain video open is "dirty" through `avItemAppended`.
- **Version check is advisory** — wrong `<Version>` or a foreign root element is logged and then parsed as if fine.
- **A project that starts no video ends as aborted** — when every `<Video>` is missing or rejected by `resolveProjectPath`, no task is queued and the pool emits neither `exit` nor `aborted`; since `01c6116f` `readProjectFile` sees the zero count and calls `onReadProjectFileAborted` itself, so the flag falls and the headless wait ends (gate `tools/diag/test_project_load_rejected`). Before that the load stayed open for the rest of the session and the `TTCutProjectData` object was left behind — `readProjectFile` deletes a previous one now, which needed `mpProjectData` to be initialised at all (it was not). A video that exists but fails to open still goes through the task's abort route and is reported.
- **A project whose video file is missing** — the `<Video>` section starts an open task (the path is well-formed), the task fails, and the load ends through `onReadProjectFileAborted`. The item `doOpenVideoStream` created never joined `mpAVList` (that happens in `onOpenVideoFinished`), so `onAVItemChanged(nullptr)` saw no change and nothing closed: the project's cuts stayed in the global cut list and the window stayed dirty (measured 2026-09-24 offscreen, closing asked "Save changes?"). Now `TTAVData` keeps such items in `mPendingVideoItems` and frees them in `endAbortedProjectLoad`, shows "Project Not Loaded" with the task's reason (unless non-interactive), and `onOpenProjectFileAborted` closes the project. Gate `project_missing_video`.
- **A project whose videos cannot be cut together ends as aborted** — `TTAVData::onReadProjectFileFinished` first runs `findIncompatibleVideos` (every cut through `canCutWith` against all loaded videos, the rule the GUI applies per appended cut). A refusal is logged with both file names and the reason, shown in a warning unless non-interactive, and the load takes the abort tail (`endAbortedProjectLoad` → `closeProject`), so `--auto-cut` finds nothing loaded. Gate `project_incompatible` (Tux tier), measured before the fix: a MPEG-2 + H.264 project loaded and `--auto-cut` cut the H.264 entry through the MPEG-2 path.
- **No atomic write** — `writeXml` truncates in place; a write error after opening leaves a broken file; the only sign of a failed save is the log line.
- **Recent list = opened projects** — a project saved for the first time appears in the recent menu only after it has been opened once.
- **`<Marker>` is a legacy carrier** — VDR import and old projects fill `TTMarkerList`; nothing displays it; `markerAppended` still marks dirty.
- **Order of `closeProject`** — `TTSettings::load()` before `setProjectFileName("")` is the second guard against the v0.74 "New project overwrote the old .ttcut" defect (`8008128`, memory `project_new_project_overwrite_fix`); the first is that the key is not persisted.
- **Headless modes rely on the flag** — `waitForProjectLoad` is the only wait; it returns false on the never-finishing case above and both modes then quit or carry on without a project.

## Redundancy / consolidation candidates

- **default project name**
  - sites: `gui/ttcutmainwindow.cpp:TTCutMainWindow::askProjectFileName` (callers `onFileSave`, `onFileSaveAs`)
  - shared purpose: default name, start directory and filter of the save dialog
  - status: done `ea7f763a` (audit run 5) — the helper returns the path and leaves the save target alone, which also ended the cancel defect above
- **recent-list refresh**
  - sites: `common/ttsettings.cpp:TTSettings::setRecentFileList` → `recentFilesChanged` → `TTCutMainWindow::updateRecentFileActions`
  - shared purpose: refresh the menu after the list changed
  - status: done `ea7f763a` (audit run 5) — the signal the header comment always intended is connected in the constructor; the widget walk is gone
- **section header guard**
  - sites: `data/ttcutprojectdata.cpp:parseVideoSection`, `:parseAudioSection`, `:parseSubtitleSection`
  - shared purpose: node count, `<Order>`, and `<Name>` through `resolveProjectPath` — eleven identical lines, differing only in return type and the name in the log
  - status: done `3c888bf1` (audit run 5 follow-up) — `parseSectionHeader` takes the section name as a parameter so the messages read unchanged; the read-side counterpart of `writeTrackSection`. Gate `test_project_load_rejected` now drives the track guards too
- **project close-and-reset**
  - sites: `gui/ttcutmainwindow.cpp:TTCutMainWindow::closeProject`, `gui/ttcutmainwindow.cpp:TTCutMainWindow::onAVItemChanged` (`avItem == 0` → `closeProject`), `gui/ttcutmainwindow.cpp:TTCutMainWindow::closeEvent`, `gui/ttcutmainwindow.cpp:TTCutMainWindow::onFileNew`
  - shared purpose: one implementation, four callers
  - status: kept → already consolidated; listed so the callers are known
- **"has a project" guard**
  - sites: `onFileNew`, `onFileSave`, `onFileSaveAs`, `onOpenAudioFile`, `onOpenSubtitleFile` (`mpAVData->avCount() == 0` → return)
  - shared purpose: menu actions that need an item
  - status: deliberate → five one-liners; the menu actions are not disabled instead, which is the actual gap (a `navigationEnabled`-style enable/disable would replace the guards)
