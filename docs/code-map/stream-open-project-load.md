---
base_commit: b06fd6cc54628a2db897d8c33fdb176fc0dba868
last_verified: 2026-09-14
sources:
  - data/ttavdata.h
  - data/ttavdata.cpp
  - data/ttavlist.h
  - data/ttavlist.cpp
  - data/ttopenvideotask.cpp
  - data/ttopenaudiotask.cpp
  - data/ttopensubtitletask.cpp
  - data/ttcutprojectdata.cpp
  - common/ttthreadtaskpool.cpp
  - common/ttthreadtask.cpp
  - gui/ttcutmainwindow.h
  - gui/ttcutmainwindow.cpp
  - gui/ttcutmainwindow_headless.cpp
  - gui/ttcutmain.cpp
  - gui/ttvideotreeview.cpp
---

# Code Map: Stream open and project load

**Scope:** from the four entry points (a video file from the GUI, a `.ttcut`
project, the command line, the headless `--auto-cut`/`--screenshots` modes)
through sibling-file discovery and the `.info` metadata, the three open tasks
on `TTAVData`'s thread pool, the completion handlers, up to the main window's
current-item state and the triggers that hang off it (audio sort window,
automatic anomaly scan gate, logo autoload, VDR marks, defect dialog), plus
`closeProject`. Ends where the streams are open and navigable. Not covered:
cutting, the searches (`detection-and-search.md`), the settings values a
load overwrites (`settings-state.md`), the pool's progress bookkeeping
(`progress-reporting.md`).

## Two ways in, one task path

| | Plain open (`TTAVData::openAVStreams`) | Project load (`TTCutProjectData::deserializeAVDataItem`) |
|---|---|---|
| Video | `doOpenVideoStream(path)`, order default | `doOpenVideoStream(path, order)` per `<Video>` |
| Audio | discovered: `<base>*.{mpa,mp2,ac3,aac}` next to the video (`getAudioNames`), order −1 | every `<Audio>` with its `<Order>`; `<Language>`, `<Delay>`, `<Repair>` become pending entries keyed `(item, order)` |
| Subtitles | discovered `<base>*.srt` (`getSubtitleNames`) | every `<Subtitle>` with order, pending language/delay |
| `.info` | read here: languages → pending, VDR marks → `mpPendingVdrMarkers`, item marked for the defect dialog, legacy decode-error warning (modal) | not read here; `onOpenVideoFinished` reads it again for the extra-frame list only |
| Cuts / markers | from VDR marks in `onOpenVideoFinished` | `parseCutSection`/`parseMarkerSection` append synchronously while the tasks still run, then `sortCutItemsByOrder`/`sortMarkerByOrder` `parseCutSection` refuses a range `TTAVItem::checkCut` rejects (negative or inverted) and skips that entry with a warning rather than failing the load. |
| Pool abort hook | `aborted → onOpenAVStreamsAborted` (armed per open, dropped in `onThreadPoolExit`) | `exit → onReadProjectFileFinished`, `aborted → onReadProjectFileAborted` (armed in `readProjectFile`) |
| Main window flag | — | `mProjectLoadInProgress` from `openProjectFile` to `onOpenProjectFileFinished`/`Aborted` |

Both paths meet in `doOpenVideoStream`: `createAVItem` (wires the item's
cut and marker lists to the global ones), `mpThreadTaskPool->init(audio+1)`,
`start(TTOpenVideoTask)`. Audio and subtitle tasks are started right behind
it; the pool runs them on `QThreadPool::globalInstance()`.

## Data flow

Solid edges carry data (producer → consumer); dashed edges are triggers or
signal deliveries that carry no payload the receiver keeps.

```mermaid
flowchart TD
    GUI["GUI open<br/>onOpenVideoFile / onFileOpen / recent"]
    CLI["Command line + headless<br/>ttcutmain.cpp, runAutoCutMode"]
    OPEN["TTAVData::openAVStreams<br/>discovery + .info"]
    PRJ["TTCutProjectData::deserializeAVDataItem<br/>parseVideo/Audio/Subtitle/Cut/Marker"]
    PEND["pending maps<br/>languages, delays, repairs, VDR marks,<br/>defect-dialog mark"]
    DISP["TTAVData::doOpenVideoStream<br/>createAVItem, pool init"]
    POOL["TTThreadTaskPool<br/>init / exit / aborted"]
    TV["TTOpenVideoTask<br/>header + index list, display sort"]
    TA["TTOpenAudioTask / TTOpenSubtitleTask<br/>header list"]
    OVF["TTAVData::onOpenVideoFinished"]
    OAF["TTAVData::onOpenAudioFinished /<br/>onOpenSubtitleFinished"]
    ITEM["TTAVItem<br/>video, audio list, subtitle list,<br/>initialAudioLoadDone, anomalyScanStarted"]
    LIST["TTAVList (mpAVList)<br/>+ global cut/marker lists"]
    DLG["defect dialog<br/>showExtraFrameClusterDialog (modal)"]
    PEXIT["TTAVData::onThreadPoolExit"]
    PRF["TTAVData::onReadProjectFileFinished"]
    MW["TTCutMainWindow::onAVItemChanged<br/>mpCurrentAVDataItem, widgets, navigation"]
    MWR["TTCutMainWindow::onAVDataReloaded<br/>audio/subtitle tree reload"]
    MWP["TTCutMainWindow::onOpenProjectFileFinished<br/>flag release, recent file, title"]
    GATE["maybeStartAutoAnomalyScan<br/>gate, zero-timer"]
    SP["stream points / logo / VDR<br/>onStreamPointsLoaded, onLogoDataLoaded,<br/>onPointsDetected"]
    CLOSE["TTCutMainWindow::closeProject"]

    GUI -->|"video path"| OPEN
    GUI -->|".ttcut path"| PRJ
    CLI -->|"path or project"| OPEN
    CLI -->|"project"| PRJ
    OPEN -->|"languages, VDR pairs, dialog mark"| PEND
    PRJ -->|"language, delay, repairs per (item, order)"| PEND
    OPEN -->|"video path"| DISP
    PRJ -->|"video path + order"| DISP
    PRJ -->|"cuts, markers (synchronous)"| ITEM
    DISP -->|"item + task"| POOL
    OPEN -->|"audio/subtitle tasks"| POOL
    PRJ -->|"audio/subtitle tasks with order"| POOL
    POOL -.->|"global QThreadPool"| TV
    POOL -.->|"global QThreadPool"| TA
    TV -->|"finished(item, TTVideoStream, order, demuxedAudio), queued"| OVF
    TA -->|"finished(item, stream, order), queued"| OAF
    PEND -->|"VDR pairs, dialog mark"| OVF
    PEND -->|"language, delay, repairs"| OAF
    OVF -->|"setVideoStream, extra-frame list, VDR cuts"| ITEM
    OVF -.->|"before append, nested event loop"| DLG
    OVF -->|"append"| LIST
    OAF -->|"appendAudioEntry / appendSubtitleEntry, sort in initial window"| ITEM
    POOL -.->|"exit() when the queue is empty"| PEXIT
    PEXIT -->|"initialAudioLoadDone for every listed item"| ITEM
    OVF -.->|"currentAVItemChanged(item)"| MW
    OVF -.->|"avDataReloaded (direct call)"| MWR
    PEXIT -.->|"avDataReloaded, threadPoolExit"| MWR
    POOL -.->|"exit() (project path)"| PRF
    PRF -.->|"1. avDataReloaded  2. currentAVItemChanged(item 0)"| MW
    PRF -->|"3. stream points  4. logo data"| SP
    PRF -.->|"6. readProjectFileFinished"| MWP
    MW -.->|"zero-timer"| GATE
    MWR -.->|"zero-timer"| GATE
    MWP -.->|"zero-timer, after the flag"| GATE
    ITEM -->|"videoStream, initialAudioLoadDone, anomalyScanStarted, firstAc3TrackIndex"| GATE
    MW -.->|"avItem == 0"| CLOSE
    CLOSE -->|"clear lists, null current item"| LIST
```

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| `GUI`/`CLI` → `OPEN`/`PRJ` | `onReadVideoStream` clears `cutVideoName` only when no item exists yet, then `openAVStreams`; `openProjectFile` first runs `closeProject` if anything is open, sets `mProjectLoadInProgress`, arms the two project hooks, then `readProjectFile` (which does `readXml` + `deserializeAVDataItem` synchronously; an exception there goes straight to `onReadProjectFileAborted`). The CLI picks the path by extension (`.prj`/`.ttcut` → project) on a 100 ms timer after the event loop starts; headless modes on a 500 ms timer. |
| `OPEN` → `PEND` | `.info` audio languages are matched to the discovered audio files by file name and stored per `(item, discovery index)`; VDR marks are paired `(in, out)` from consecutive markers, kept only when `in > 0 && out > in`; the item is inserted into `mpPendingExtraFrameDialog` on every fresh open that has an `.info` (project load never marks, so it never shows the dialog). `mExtraFrameIndices` is cleared here so `onOpenVideoFinished` rebuilds it. |
| `PRJ` → `PEND`/`ITEM` (since `01c6116f`: a project where no section starts a task ends as aborted, see `project-lifecycle.md`) | Paths go through `resolveProjectPath` (absolute or relative to the project, no `..`, no control bytes; rejected = section skipped with a warning). Repairs are validated against the real AC3 frame size and file length; invalid ones are DISABLED, not dropped. Cuts and markers are appended before the video stream exists: `appendCutEntry` takes the stored display positions unconverted (see the comment in `parseCutSection`). |
| `DISP` → `POOL` | `init(audioCount + 1)` sizes the pool's progress estimate from the DISCOVERED audio count even on the project path, where the real count comes from the XML. `start()` asserts the pool's own thread, emits `init()` only when nothing is running, and enqueues the task. Ownership: the item is created by `TTAVData`; `TTOpenVideoTask::onUserAbort` deletes it if it is not in the list yet. |
| `TV`/`TA` → `OVF`/`OAF` | `finished` is connected `Qt::QueuedConnection`, so the handler runs on the GUI thread one event-loop turn (or more) after the worker finished — the pool, connected direct in `wireTask`, learns about the same completion first. A task that throws `TTException` ends in `aborted(this)` with a failure message. Since `cc85a6c8` `TTAVData::watchOpenAbort` listens to every open task's `aborted` in a DIRECT connection (the task is certainly alive there), reads `failureMessage()` and queues only the text back to the GUI thread: `onOpenAudioAborted`/`onOpenSubtitleAborted` record a failed track, `onOpenVideoAborted` sets the video-failed flag; an empty reason is the user's cancel and sets `mOpenCancelled` instead. Video: header list, index list, `sortDisplayOrder()` all happen in the worker; container extensions are rejected with the demux hint. |
| `OVF` → `ITEM`/`LIST`/`DLG` | Order inside the handler: `setVideoStream` → `.info` re-read → `loadExtraFrameIndices` (MPEG-2 parser field pairs win over `.info`, only if the list is empty) → defect dialog if marked (MODAL: a nested event loop runs here, the pool can drain and `onThreadPoolExit` can run inside it) → `mpAVList->append` → `setInitialAudioLoadDone()` if the pool is already drained (the second setter, added for exactly that dialog) → VDR pairs become cut entries + markers + `VDRImportMarker` stream points (halved when any mark exceeds the frame count: field-rate marks) → `avDataReloaded`/`cutDataReloaded`/`markerDataReloaded` called directly → `mpCurrentAVItem = item`, `currentAVItemChanged` → demuxed audio (legacy, always empty today). |
| `OAF` → `ITEM` | Append, then the pending language/delay are applied at `audioCount() − 1` (the position the entry just got), repairs by their stored track index. Sorting happens per append and only while `!initialAudioLoadDone()`: project tracks (order ≥ 0) restore `<Order>`, discovered tracks (order −1) sort AC3 > language preference > discovery order — per append on purpose, so `audioStreamAt(0)` is right before the pool drains. |
| `POOL` → `PEXIT` | `exit()` fires from `onThreadTaskFinished` or `onThreadTaskAborted` when the queue is empty; `aborted()` precedes `exit()` only when the LAST task to leave was the aborted one. The pool keeps these semantics (ruled 2026-09-12, contract finding 3), but since `cc85a6c8` `TTAVData` no longer depends on them for tracks: the failures recorded per task (row above) are logged by `onThreadPoolExit`, emitted as `trackOpenFailed(QStringList)` and, in the GUI, shown in one warning dialog via a 0-ms timer after the load chain; `onReadProjectFileAborted` runs `onReadProjectFileFinished` instead when only tracks failed (video in, no cancel). Gate `tools/diag/test_open_track_failure`. `onThreadPoolExit` drops the open-abort hook, reports and clears the failed tracks, sets `initialAudioLoadDone` on every listed item (not on an item still waiting in `onOpenVideoFinished`'s dialog), emits the `Exit` status unless a cut bracket is open, swallows the exit of an MKV mux run, then `avDataReloaded` + `threadPoolExit`. |
| `PRF` → `MW`/`SP`/`MWP` | Fixed order: `avDataReloaded` → `setCurrentAVItem(avItemAt(0))` (the first video; since `cc85a6c8` also stored — before, `mpCurrentAVItem` kept whatever `onOpenVideoFinished` set last) → `streamPointsLoaded` → `logoDataLoaded` → `deserializeSettings` → `readProjectFileFinished`. Every step is a synchronous signal to the main window. `onReadProjectFileAborted` instead calls `setCurrentAVItem(nullptr)`, which the main window answers with `closeProject`, then `readProjectFileAborted` — unless only tracks failed (see `POOL` → `PEXIT`), then it runs the finished chain. |
| `OVF`/`PRF` → `MW` | `onAVItemChanged` returns at once for the current item or when `avItem == 0` (→ `closeProject`). Otherwise: re-wire the subtitle-append hook, set the current item on the stream-point widget with the extra-frame list, frame rate into the stream-point model, `setEncoderCodec(streamType)` (see `settings-state.md`), both frame widgets, audio/subtitle lists, subtitle overlay if a subtitle already landed, `onNewFramePos(currentIndex)`, navigator, `navigationEnabled(true)`, zero-timer to the gate, logo profile cleared and `<video>.logo.pgm` autoload on a zero-timer. |
| `MW`/`MWR`/`MWP` → `GATE` | Three entry points, all deferred by a zero-timer; `maybeStartAutoAnomalyScan` is idempotent: it needs `audioAnomalyScanEnabled`, no project load in progress, a current item with video, `initialAudioLoadDone`, not `anomalyScanStarted`, no stream-point workers running, an AC3 track, and no `AudioAnomaly` marker already in the model. Whichever entry runs last with all conditions true starts the scan. |
| `LIST` → dirty flag | `TTAVList::itemAppended` → `avItemAppended` → `onProjectModified` during BOTH paths; `onOpenProjectFileFinished` resets it to clean at the end, a plain open leaves the window marked modified (unsaved new project). |
| `MW` → `CLOSE` | Aborts the stream-point pool and waits for the global `QThreadPool`, disconnects the two AV signals, nulls every widget, clears the stream-point model and `TTAVData` (`mpAVList`, cut list, marker list; the `TTAVItem`s and their streams die with the list), reloads `TTSettings`, clears project identity and cut name, reconnects. |
| `CLI` (headless) → wait | `runAutoCutMode` and `runScreenshotMode` call `waitForProjectLoad(timeout)` (since `026aa9fb`): pump until `mProjectLoadInProgress` falls, which happens in `onOpenProjectFileFinished`/`Aborted` — i.e. after `readProjectFileFinished`, the pool's `exit` with every open task (audio and subtitles included) gone. Until then they polled `avCount() > 0` (video appended) and slept a fixed 2 s for the audio tasks. |

## Assumptions, contracts & pitfalls

- **`TTAVData::openAVStreams`** — assumes the video's siblings by base name ARE its audio (a second recording in the same directory with the same base name prefix is picked up too: the filter is `<base>*.<ext>`); guarantees the pending maps are filled before any task can finish (tasks are queued, handlers run later on the GUI thread); pitfall: two modal dialogs can run from here (legacy decode-error warning) and from `onOpenVideoFinished` (defect dialog) — headless callers set `setNonInteractive` for the cut, not for these.
- **`TTAVData::onOpenVideoFinished`** — contract: the item is in the list and current when it returns; pitfall: `onThreadPoolExit` may already have run inside the defect dialog's nested loop, which is why `initialAudioLoadDone` is set here too and why the anomaly gate has three entries (measured 2026-08-20 on a repaired DVB recording, see the comment block above `maybeStartAutoAnomalyScan`).
- **`TTThreadTaskPool`** — `exit` = queue empty, nothing more; it does not wait for queued `finished` deliveries. `aborted` before `exit` depends on which task leaves last, so "run aborted" is not "some task failed". `isDrained()` is the same predicate as the `exit` condition.
- **`TTAVData::onOpenAVStreamsAborted`** — makes the LAST listed item current (not the one that failed to open) and emits `currentAVItemChanged`; with one video open the main window's early return hides it, with several the user is switched to the last video.
- **`TTAVData::onReadProjectFileAborted`** — a project with an unreadable audio/subtitle track: since `cc85a6c8` loaded on both pool routes, the track reported (`trackOpenFailed`, log, dialog; gate `test_open_track_failure`, 5/5 runs on the exit route — the aborted route follows the same code but was not exercised, the type failures are instant). A project whose VIDEO fails still depends on the route: a good audio track that leaves last ends in `readProjectFileFinished` with zero items (observed while writing the gate, not changed — outside the ruling).
- **`TTCutMainWindow::onAVItemChanged`** — assumes it runs on the GUI thread synchronously with the emit (it is: same thread, auto connection); pitfall: `onOpenAVStreamsAborted`'s emit reaches it during a cut abort if the open hook was never dropped — fixed by the disconnect in `onThreadPoolExit`, documented there.
- **`TTCutProjectData::parseVideoSection`** — `sortCutItemsByOrder` runs per video section on the GLOBAL cut list, i.e. n times for n videos; harmless, but the order key is per item.
- **Headless wait** — until `026aa9fb` the 2 s audio sleep was a guess, not a contract (a slow NAS or a large AC3 header scan could exceed it and the cut ran with fewer tracks than the project lists); `waitForProjectLoad` waits for the load chain itself. Screenshot gate: all 21 images unchanged by the switch.

## Redundancy / consolidation candidates

- ~~**current-item switch** (five sites)~~ — consolidated `cc85a6c8`: `TTAVData::setCurrentAVItem` assigns and emits; `onRemoveAVItem` uses it too.
- **initialAudioLoadDone setter**
  - sites: `data/ttavdata.cpp:TTAVData::onOpenVideoFinished` (if drained), `data/ttavdata.cpp:TTAVData::onThreadPoolExit` (all items)
  - shared purpose: close the audio sort window and open the anomaly gate
  - status: kept separate → the two cover the two orders the dialog can produce; a single site would need the pool to wait for queued deliveries
- **`.info` read**
  - sites: `data/ttavdata.cpp:TTAVData::openAVStreams`, `data/ttavdata.cpp:TTAVData::onOpenVideoFinished`
  - shared purpose: `TTESInfo::findInfoFile` + load for the same video
  - status: consolidate → load once in `openAVStreams`/`doOpenVideoStream` and keep it on the pending map with the item (the project path would then get the extra-frame list from the same place). Still open after batches B and D (2026-09-13): B's `TTESInfo::timingForVideo` covers only the rate/offset readers, these two sites need the whole object.
- ~~**audio/subtitle open entry** (wrapper pair)~~ — consolidated `cc85a6c8`: the wrappers are gone, the main window calls `doOpenAudioStream`/`doOpenSubtitleStream`.
- ~~**headless "wait for the project"** (two sites, fixed 2-s sleep)~~ — consolidated `026aa9fb`: `TTCutMainWindow::waitForProjectLoad(timeoutMs)` waits on `mProjectLoadInProgress` (the `readProjectFileFinished`/`Aborted` chain).
- ~~**dead abort slots**~~ — resolved `cc85a6c8` (ruled: connect and report): `watchOpenAbort` feeds them, the failed track is reported by `onThreadPoolExit`; gate `tools/diag/test_open_track_failure`.
