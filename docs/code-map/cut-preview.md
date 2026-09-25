---
base_commit: 31915bd364c3001dfcce1997edcc18dcef0f2628
last_verified: 2026-09-25
sources:
  - gui/ttcuttreeview.h
  - gui/ttcuttreeview.cpp
  - gui/ttcutmainwindow.h
  - gui/ttcutmainwindow.cpp
  - data/ttavdata.h
  - data/ttavdata.cpp
  - data/ttcutpreviewtask.h
  - data/ttcutpreviewtask.cpp
  - data/ttpreviewclip.h
  - data/ttpreviewclip.cpp
  - gui/ttcutpreview.h
  - gui/ttcutpreview.cpp
  - common/ttsettings.h
  - ui/ttcutsettingssearch.ui
---

# Code Map: Cut preview

**Scope:** from the preview action in the cut list to the closed preview
dialog — how the job list becomes a list of short preview windows, how
`TTCutPreviewTask` turns each clip into `preview_NNN.mkv` in the temp
directory (MPEG-2 through a nested cut task, H.264/H.265 through a shared
Smart Cut engine), how the drift column gets its values, how the dialog
loads clips, shows the burst and aspect hints of the selected transition,
moves a cut edge and rebuilds one clip, and how the files are removed again.

**Neighbours, not part of this map:** where the job list comes from and what
`skipFirst`/`skipLast` mean to the selection ([cut-edit-and-start.md](cut-edit-and-start.md)),
the Smart Cut engine itself ([smart-cut.md](smart-cut.md)), the MPEG-2 cut
engine ([mpeg2-cut.md](mpeg2-cut.md)), the audio keep list, delay and drift
arithmetic ([audio-cut-timing.md](audio-cut-timing.md)), the muxer
configuration ([output-mux.md](output-mux.md)), mpv
([playback.md](playback.md)), progress brackets and cancel mechanics
([progress-reporting.md](progress-reporting.md)), the burst detector
([burst-detection.md](burst-detection.md)) and the aspect analysis
([detection-and-search.md](detection-and-search.md)).

## Data flow

Legend: solid arrows carry data (producer → consumer); dashed arrows are
triggers only.

```mermaid
flowchart TD
    TV["TTCutTreeView<br/>onEntryPreview / cutListFromSelection"]
    MW["TTCutMainWindow<br/>onCutPreview / onCutPreviewFinished"]
    AVD["TTAVData<br/>doCutPreview / onCutPreviewFinished / onCutPreviewAborted"]
    TASK["TTCutPreviewTask::operation<br/>(pool thread)"]
    PL["preview TTCutList<br/>createPreviewCutList"]
    BCL["ttBuildClipCutList"]
    SRC["ttResolvePreviewSource"]
    ENG["TTESSmartCut<br/>shared / clip-local"]
    M2V["TTCutVideoTask<br/>(nested)"]
    AUD["TTAVData::cutAudioTracks<br/>/ cutSubtitleTracks"]
    TMP["intermediate files<br/>preview_*_temp.*, preview_NNN.m2v/.ac3"]
    MUX["TTMkvMergeProvider<br/>ttConfigurePreviewMux"]
    CLIP["clip files<br/>preview_NNN.mkv / .srt"]
    CTV["TTCutTreeView::onAudioDriftUpdated<br/>(column 4)"]
    DLG["TTCutPreview<br/>initPreview / onCutSelectionChanged"]
    PLAYER["TTMpvWrapper"]
    HINT["checkBurstForCurrentCut<br/>checkAspectForCurrentCut"]
    MOVE["moveCutEdge<br/>onBurstShift / onAspectJump"]
    MODEL["TTAVItem::updateCutEntry<br/>(real cut list)"]
    REB["regeneratePreviewClip<br/>ttRebuild*PreviewClip"]
    CLEAN["ttRemovePreviewFiles"]

    TV -->|job list, skipFirst, skipLast| MW
    MW -->|job list| AVD
    AVD -->|job list, on the pool| TASK
    TASK -.->|first: delete old files| CLEAN
    TASK -->|job list| PL
    PL -->|entries of clip i| BCL
    BCL -->|clip cut list| SRC
    SRC -->|source, fps, audio file, A/V offset| ENG
    SRC -->|source, clip cut list| M2V
    SRC -->|item, keep list| AUD
    ENG -->|video ES| TMP
    M2V -->|video ES| TMP
    AUD -->|audio track 0| TMP
    AUD -->|subtitle track 0| CLIP
    TMP -->|inputs| MUX
    MUX -->|clip| CLIP
    AVD -->|drift per project cut| CTV
    AVD -.->|finished / aborted| MW
    PL -->|preview list, via MW| DLG
    DLG -->|file of combo entry| PLAYER
    CLIP -->|file| PLAYER
    DLG -->|clip index| HINT
    HINT -->|segment, direction, target| MOVE
    MOVE -->|new edge| MODEL
    MOVE -->|window around new edge| PL
    MOVE -.->|rebuild current clip| REB
    REB -->|clip index| BCL
    DLG -.->|close| CLEAN
```

Direction measured with `mmdc` (2026-09-25): `TD` viewBox ratio 0.57, `LR` 10.41.

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| `TV` → `MW` | `previewCut(list, skipFirst, skipLast)`. `onEntryPreview` puts the selected cuts **plus their neighbours** into a fresh job list (`newJobCutList`, owned by the view, replaced by the next job); `skipFirst`/`skipLast` are true when the first/last list entry is only a neighbour. Selecting one cut in the middle therefore gives three entries and `skipFirst = skipLast = true`. |
| `MW` → `AVD` | `onCutPreview` stores the job list as `mpPreviewOriginalCutList` and the two flags, re-arms (disconnect, connect) `cutPreviewFinished` → `onCutPreviewFinished` and `cutAudioDriftCalculated` → `TTCutTreeView::onAudioDriftUpdated`, then `doCutPreview(list)`. |
| `AVD` → `TASK` | `doCutPreview` deletes the previous task, creates `TTCutPreviewTask(avData, jobList)`, connects `finished` and the pool's `aborted` → `onCutPreviewAborted`, `init(count * 2)`, `start`. The task owns its nested `TTCutVideoTask` and the preview list; it lives until the next preview starts or until `onCutPreviewAborted` deletes it — the dialog's pointer to the preview list relies on that. |
| `TASK` -.-> `CLEAN` | First thing in `operation()`: `ttRemovePreviewFiles` deletes **every** `preview*` file in `TTSettings::tempDirPath()`. |
| `TASK` → `PL` | `createPreviewCutList`: two entries per job cut, from the shared window rules in `data/ttpreviewclip`: `ttPreviewFrames` = `cutPreviewSeconds · fps / 2` (half the setting per side; the first entry's stream decides it for all), `ttPreviewCutInWindow` = `[cutIn, min(cutIn + frames, cutOut)]` with the end moved forward past B-frames (`frameType == 3`) but not past `cutOut`, `ttPreviewCutOutWindow` = `[max(cutOut − frames, cutIn), cutOut]` with the start moved back to `findIDRBefore` only while that stays `≥ cutIn`. Both windows stay inside their cut: a cut shorter than the window is shown whole (both entries then equal the cut). |
| `PL` → `BCL` | `ttBuildClipCutList(list, i)`: `ttPreviewClipCount` = `count / 2 + 1` clips; `ttPreviewCutOutEntry(i)` = `2i − 1` is clip i's first entry. Clip 0 = entry 0 (first cut-in), clip i (0 &lt; i &lt; last) = entries `2i−1` and `2i` (cut-out of cut i, cut-in of cut i+1), last clip = entry `2(n−1)+1` (last cut-out). Entries keep their `avDataItem`, so a clip can span two videos. |
| `BCL` → `SRC` | `ttResolvePreviewSource` reads everything off clip entry 0: item, video stream, source file, suffix, frame rate (stream first, `.info` only when the stream has none), `.info` A/V offset, audio track 0 (`hasAudio`, `audioFile`). |
| `SRC` → `ENG` | H.26x: one shared `TTESSmartCut` for the whole run, `initialize(source, fps)` (full ES parse), preview preset and the stream's display-order map (`ttApplyPreviewEncoderSettings`). Registered in `mpActiveSmartCut` under `mSmartCutMutex` before the parse, so a cancel reaches it. If the shared init fails, `createH264PreviewClip` builds a clip-local engine per clip — never registered, so not cancellable. |
| `SRC` → `M2V` | MPEG-2: `cutVideoTask->init(preview_NNN.m2v, clipList)` and `threadTaskPool()->startNested` (synchronous, outside the pool queue; `onUserAbort` forwards the cancel to it). |
| `SRC` → `AUD` | `cutAudioTracks(item, {0}, keepList, normalizeAcmod, …, shouldAbort)` — track 0 only, keep list from `buildVideoKeepList(clipList, fps)`; `cutSubtitleTracks(item, {0}, …)` — subtitle 0 only. The audio file keeps the source suffix. |
| `ENG` / `M2V` / `AUD` → `TMP` | H.26x: `preview_video_temp.<suffix>` and `preview_audio_temp.<ext>` (fixed names, one at a time); MPEG-2: `preview_NNN.m2v` and `preview_NNN.<audio ext>`. The task keeps the H.26x temp video after the mux ("for debugging"), the rebuild removes it; audio is removed by both. |
| `AUD` → `CLIP` | `preview_NNN.srt` next to the clip; the dialog picks it up by name. |
| `TMP` → `MUX` | `ttConfigurePreviewMux`: video options for the stream and fps, `.info` A/V offset, the engine's output display order (H.26x), `setRequireAllInputs(false)` — a clip without its audio still gets made. MPEG-2 without audio skips the mux and renames the `.m2v` to `preview_NNN.mkv`: the target is removed first (`QFile::rename` does not overwrite) and a failed rename is an error (task: `TTIOException`, rebuild: `false`). |
| `MUX` → `CLIP` | `preview_NNN.mkv`, `NNN` = clip index + 1. A failed mux throws `TTIOException` with `mErrorMessage` set. |
| `AVD` → `CTV` | `TTAVData::onCutPreviewFinished`, on the GUI thread: `planAudioCut(track 0, buildVideoKeepList(project cut list), delay of track 0).drifts` over **all cuts of the project**, whatever the preview showed (user decision 2026-09-25), emitted as `cutAudioDriftCalculated`; `onAudioDriftUpdated` writes value i into row i. Connected only between `onCutPreview` and `onCutPreviewFinished`. |
| `AVD` -.-> `MW` | Success: `onCutPreviewFinished` drops the abort connection and emits `cutPreviewFinished(previewList)`. Abort: `onCutPreviewAborted` deletes the task; empty `errorMessage()` = user cancel → `Canceled` bracket (arms `mCutOperationActive`), otherwise "Preview not possible" warning. `onCutPreviewFinished` never runs then; the next `onCutPreview` re-arms its connections. |
| `PL` → `DLG` | `onCutPreviewFinished` creates `TTCutPreview`, `initPreview(previewList, jobList, avData, skipFirst, skipLast)`, `exec()` (modal), deletes it, disconnects both signals. `initPreview` fills the combo from `clipLabel` (`Start`, `Cut i-(i+1)`, `End`, the first/last skipped per flag; the times come from the job-list copy: cut-in of the first joined cut to cut-out of the second, not the preview windows) with signals blocked, and sets the entry tooltips (`updatePrePostRollToolTips` → `prePostRollToolTip`: pre-/post-roll = frames of the clip's cut-out and cut-in entries / fps, plus the setting); `mClipOffset = skipFirst ? 1 : 0`. The first load waits for `showEvent` + one event-loop turn (render context). |
| `DLG` → `PLAYER` / `CLIP` → `PLAYER` | `onCutSelectionChanged(iCut)`: clip `clipIndexOf(iCut)` = `iCut + mClipOffset` (first sets the combo's own tooltip to that entry's pre-/post-roll), file `createPreviewFileName(clip + 1, "mkv")`; `preview_NNN.srt` as `--sub-file` when non-empty; output channels pinned from audio 0; load paused on dialog open, playing on later selections. |
| `DLG` → `HINT` | Both checks get the **clip index** (`clipIndexOf`): 0 = cut-in of cut 1; otherwise entry `ttPreviewCutOutEntry(clip)` (cut-out) first, then the one after it (cut-in); the cut numbers in the messages are clip-based and match the combo labels. Burst reads the preview entries, aspect reads the job-list cut (`originalCutItem(segmentIdx) = job[segmentIdx / 2]`). |
| `HINT` → `MOVE` | `mBurstSegmentIdx` / `mAspectSegmentIdx`, cut-in or cut-out, and for the aspect jump `mAspectTarget`. Burst shift: ±1 frame with a one-frame-cut guard; aspect jump: to the first/last picture of the cut's majority aspect. |
| `MOVE` → `MODEL` | `updateRealCutItem`: finds the real cut by `(cutIn, cutOut, avItem)` equality with the job-list copy and calls `TTAVItem::updateCutEntry` — the project's cut list changes. The shared video stream's position is saved and restored around the whole move + rebuild. |
| `MOVE` → `PL` | `applyEdgeMoveToLists`: the job-list copy gets the new edge; both preview entries of the moved cut (2k, 2k+1) are rebuilt from the moved cut with the same rule as `createPreviewCutList` (`ttPreviewCutInWindow` / `ttPreviewCutOutWindow`), so a jump beyond the old window cannot turn an entry around and the other edge's window stays inside the cut; then every combo text is set again from `clipLabel` and the entry tooltips from `updatePrePostRollToolTips`. |
| `MOVE` -.-> `REB` | `regeneratePreviewClip(clipIndexOf(combo index))`: GUI thread, modal `QProgressDialog` without cancel, `processEvents` per stage. Output file index clip + 1. A failed rebuild shows a warning and keeps the old clip. |
| `REB` → `BCL` | `ttBuildClipCutList(previewList, clip)`, then `ttRebuildMpeg2PreviewClip` (a stack `TTCutVideoTask` run through `threadTaskPool()->start(task, /*runSyncron=*/true)`; its re-raised `TTException` is caught and turned into `false`; subtitle 0, audio 0, mux or rename) or `ttRebuildSmartCutPreviewClip` (a new `TTESSmartCut`, full ES parse, same temp names, subtitle 0, audio 0, temp files removed after the mux). Both write `preview_<clip+1>.srt` again (`rebuildClipSubtitle`). Afterwards reload paused and re-run both checks. |
| `DLG` -.-> `CLEAN` | `closeEvent` → `cleanUp`: stop the player, `ttRemovePreviewFiles`. |


## Assumptions, contracts & pitfalls

- **Three indices, one mapping each.** Combo index → clip index is
  `TTCutPreview::clipIndexOf` (adds `mClipOffset`, 1 when `skipFirst` left the
  first clip out); clip → entries is `ttPreviewCutOutEntry`; clip → file is
  `createPreviewFileName(clip + 1, …)`. Anything clip-based in the dialog goes
  through them.
- **The preview list is not the job list.** Two entries per cut, each only
  a window of half the preview length around one edge, never reaching
  outside the cut. Analyses that need
  the whole cut (aspect) go back to the job list; burst reads the window.
- **Temp directory is shared state.** `ttRemovePreviewFiles` deletes every
  `preview*` file at task start and on dialog close; the H.26x temp names
  are fixed. A second TTCut-ng instance or harness on the same temp
  directory collides (TODO „Weitere geteilte Temp-Namen“).
- **Cancel reaches:** the shared engine (registered), the nested MPEG-2
  video task (forwarded), the audio cut (predicate), the clip loop (top of
  each iteration). **Not:** a clip-local fallback engine, the mux, the
  dialog's rebuild.
- **Error reporting differs by codec.** MPEG-2: a failed audio cut, mux or
  rename throws with a message. H.26x: a failed mux throws; a failed audio
  cut only logs and the clip is muxed without audio; a failed local-engine
  init or an invalid source returns without a clip and without a message
  (not reproduced so far - kept with TODO P3).
- **Pitfall — `preview_NNN.mkv` can be raw MPEG-2.** Without audio the
  `.m2v` is renamed, not muxed; mpv sniffs the content.
- **The drift column is filled only by a preview.** The audio-only cut
  emits `cutAudioDriftCalculated` too, but the column is connected only
  while a preview runs, so that emission reaches nothing.

## Redundancy / consolidation candidates

- **Clip production twice** (TODO.md P3)
  - sites: `data/ttcutpreviewtask.cpp:TTCutPreviewTask::operation` (MPEG-2 branch) + `:createH264PreviewClip`, `data/ttpreviewclip.cpp:ttRebuildMpeg2PreviewClip` + `:ttRebuildSmartCutPreviewClip`
  - shared purpose: one clip cut list → video, audio 0, mux → `preview_NNN.mkv`
  - status: documented → TODO.md P3; the copies still differ in error handling, temp cleanup and cancel
- **Clip index arithmetic**
  - sites: `data/ttpreviewclip.h:ttPreviewClipCount`, `:ttPreviewCutOutEntry`, `gui/ttcutpreview.h:TTCutPreview::clipIndexOf`
  - shared purpose: clip count, the entry indices of clip i, combo → clip
  - status: done → one helper each, used by the task, `ttBuildClipCutList` and the dialog (audit run 9)
- **Preview file names**
  - sites: the fixed temp names `preview_video_temp.*` / `preview_audio_temp.*` in `createH264PreviewClip` and `ttRebuildSmartCutPreviewClip` (clip files go through `createPreviewFileName` everywhere)
  - shared purpose: where a clip's intermediates live
  - status: documented → TODO.md P3 and „Weitere geteilte Temp-Namen“
