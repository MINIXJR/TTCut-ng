---
base_commit: 6ef4fde021c76323be391aa80a20f68dccfe3373
last_verified: 2026-10-05
sources:
  - data/ttaudioanomalyscantask.h
  - data/ttaudioanomalyscantask.cpp
  - extern/ttaudiorepairitem.h
  - extern/ttaudiorepair.h
  - extern/ttaudiorepair.cpp
  - extern/ttac3reencoder.h
  - extern/ttac3reencoder.cpp
  - gui/ttaudiorepairdialog.h
  - gui/ttaudiorepairdialog.cpp
  - gui/ttstreampointwidget.cpp
  - gui/ttcutmainwindow.cpp
  - data/ttavlist.h
  - data/ttavlist.cpp
  - data/ttavdata.h
  - data/ttavdata.cpp
  - data/ttcutprojectdata.cpp
  - data/ttstreampoint.h
  - extern/ttaudiocutter.h
  - extern/ttaudiocutter.cpp
  - docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md
---

# Code Map: Audio anomaly repair

**Scope:** the way of one audio disturbance from detection to the cut
audio — the AC3 anomaly scan and its markers, the marker's context menu,
the repair dialog with its audition, the repair list on the AV item and
its track renumbering, the `<Repair>` entries of the project file with
their load validation, and how a repair becomes replacement frames in
every audio cut (final cut, audio-only cut, preview clips).

**Neighbours, not part of this map:** when the scan starts and how the
marker list behaves in general ([stream-points.md](stream-points.md)), the
keep list, delay and acmod arithmetic of the audio cut
([audio-cut-timing.md](audio-cut-timing.md)), the preview windows
([cut-preview.md](cut-preview.md)), mpv ([playback.md](playback.md)), the
project file in general ([project-lifecycle.md](project-lifecycle.md)) and
the track list ([track-management.md](track-management.md)). The decisions
behind the design are in `docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md`;
deliberately postponed work is in `TODO.md` („Audio-Anomalie-Reparatur —
bewusste Folgearbeiten“).

## Data flow

Legend: solid = data, dashed = trigger (who starts what).

```mermaid
flowchart TD
    MW["TTCutMainWindow<br/>startAudioAnomalyScan"]
    SCAN["TTAudioAnomalyScanTask<br/>collectFrameStats / evaluate / evaluateStops"]
    PTS["TTStreamPointModel<br/>AudioAnomaly markers"]
    WID["TTStreamPointWidget<br/>context menu"]
    DLG["TTAudioRepairDialog"]
    AUD["writePreviewWindow<br/>audition .ac3 → mpv"]
    ITEM["TTAVItem<br/>mAudioRepairs"]
    PROJ["TTCutProjectData<br/>&lt;Repair&gt; / stream points"]
    PEND["TTAVData<br/>mPendingAudioRepairs"]
    ANNO["TTCutMainWindow<br/>onStreamPointsLoaded"]
    DEL["TTCutMainWindow<br/>onStreamPointDelete / DeleteAll"]
    CUT["TTAVData::cutAudioTracks"]
    TABLE["TTAudioRepair::buildRepairTable"]
    REENC["TTAc3Reencoder<br/>aligned re-encode"]
    FT["FrameTable<br/>AC3 frame → bytes"]
    CUTTER["TTAudioCutter::cut<br/>writeBytesPacket"]

    MW -.->|start task| SCAN
    SCAN -->|markers with AC3 range| PTS
    PTS -->|marker| WID
    ITEM -->|overlapping repair| WID
    WID -->|marker, first AC3 track| DLG
    WID -->|remove repair| ITEM
    WID -.->|delete marker| DEL
    ITEM -->|repair index| DEL
    DEL -->|remove repair, after asking| ITEM
    DEL -->|remove marker| PTS
    DLG -->|range, mask: audition + accept probe| TABLE
    FT -->|replacement frames| AUD
    DLG -->|TTAudioRepairItem| ITEM
    ITEM -->|repairs per track| PROJ
    PROJ -->|validated repairs| PEND
    PEND -->|repairs of a loaded track| ITEM
    PROJ -->|markers| ANNO
    ITEM -->|disabled repairs| ANNO
    ANNO -->|annotated markers| PTS
    ITEM -->|enabled repairs of a track| CUT
    CUT -->|item, target acmod| TABLE
    TABLE -->|packets, mask/fade edit| REENC
    REENC -->|replacement frames, one frame late| FT
    FT -->|merged table| CUTTER
```

## Edge semantics

| From → To | What crosses (data / order / invariant) |
|---|---|
| `MW` -.-> `SCAN` | `startAudioAnomalyScan`: file of `TTAVItem::firstAc3TrackIndex()` only (one AC3 track, TODO M8), the video frame rate, `TTAVData::extraFrameIndices()` and `audioGapFrameRanges()`. Automatic start and its latch (`maybeStartAutoAnomalyScan`, `anomalyScanStarted`) are in stream-points.md. |
| `SCAN` → `PTS` | `pointsDetected` emits once, at the end of `operation()`; a cancel emits an empty list (partial results are discarded). One `TTStreamPoint` per finding of either search, sorted by position: `frameIndex` = `videoFrameForTime(frameFrom · 32 ms)`, duration in seconds, description with the LFE peak (LFE search) or the drop in dB (stop search; “overlaps gap repair” when it touches an audio-gap range), the finding's own AC3 range via `setAudioFrameRange` (both bounds inclusive) the planes of the finding via `setAudioChannelMask` — C+LFE for an LFE finding, every plane of its frames for a stop — and its kind via `setAudioAnomalyKind` (`LfeBurst` / `AbruptStop`). A stop marker covers three AC3 frames: the one with the boundary and one on each side. The scan refuses any sample rate other than 48 kHz (empty stats, “not run”). |
| `PTS` → `WID` | The marker row; the menu offers repair actions only for `AudioAnomaly` markers and only when the item has an AC3 track. A **new** repair (“Repair…”) only when `TTStreamPoint::offersNewAudioRepair()` — not for an `AbruptStop` marker, where muting with a short fade ends as hard as the stop itself (TODO.md); a marker without a kind (older project files) counts as an LFE finding. |
| `ITEM` → `WID` | `TTAudioRepairDialog::repairIndexForMarker(item, point, extras)` — the one marker ↔ repair link: on the item's first AC3 track, `findAudioRepairOverlapping(track, from, to)` over the marker's range (`approxAc3RangeForMarker`), i.e. the index of the first repair whose closed AC3 range touches it, or -1 (also for other marker types and items without AC3). Decides “Repair…” vs “Edit repair…”/“Remove repair”; an existing repair stays editable and removable behind every kind of marker. |
| `WID` → `DLG` | Marker, `repairTrackIndex` = the item's **first** AC3 track, `mExtraFrameIndices`. Before the dialog opens the widget emits `jumpToFrame(marker frame)`, so the main window shows the spot (a right click does not navigate; the dialog has no jump button). The dialog is modal (`exec()`). A menu closed without a choice does nothing (`handleContextAction` returns on a null action). Its channel boxes start from the marker's `audioChannelMask()`; a marker without one (0: older project files) gets C+LFE; an existing repair's own mask wins over both. On accept the widget appends the “(repair planned)” suffix to the marker text. |
| `WID` → `ITEM` | `removeAudioRepairAt(repairIndex)` and removal of the “planned” suffix (all language variants). |
| `WID` -.-> `DEL` / `ITEM` → `DEL` | “Delete” and “Delete all” of the marker list reach the main-window slots, which look up `repairIndexForMarker` per marker (the same link as the context menu). |
| `DEL` → `ITEM` / `DEL` → `PTS` | A marker with a repair goes only together with it: one question (“Delete all”: one question for all, `%n` repairs), No keeps both; Yes removes the repairs (highest index first, so the others stay valid) and then the marker(s). A marker without a repair is removed without a question. |
| `DLG` → `TABLE` | Audition and the probe build of `accept`: the current spin-box range (`currentFrameFrom` = round(start ms / frame ms), `currentFrameTo` = round(end ms / frame ms) − 1, end exclusive in the spin box) and channel mask, `targetAcmod = -1` (keep the source layout). |
| `FT` → `AUD` | `writePreviewWindow` walks the packets by ordinal and writes a ±3 s window around the range to `ttcut_repair_preview_<instance>_{before,after}.ac3` in the temp directory, replacement bytes where the table has the frame; mpv plays it. The files are removed in the destructor. |
| `DLG` → `ITEM` | `accept` (the button reads “Plan repair”): rejects end < start and an empty channel mask, then builds the table once in the source layout (`targetAcmod = -1`, like the audition) and rejects any build error with the message, the dialog staying open (channel-layout or frame-size change inside the range, a channel the track does not have, range past the file end). Only then it removes the edited repair (if any) and appends `TTAudioRepairItem(track, from, to, mask)`. The cut's target layout is unknown here; mixed target layouts are caught only at cut time (`ITEM` → `CUT`). |
| `ITEM` → `PROJ` | Save: `<Repair>` (FrameFrom, FrameTo, Channels, Method) under the `<Audio>` section whose position equals `trackIndex()`; the enabled flag is not written. Markers go to the stream-point section with `AudioFrameFrom`/`AudioFrameTo` and, when known, `AudioChannels` (read back only in 1…0x3F, anything else stays “unknown”) and `AnomalyKind` (`LfeBurst` / `AbruptStop`; any other text reads as unknown). |
| `PROJ` → `PEND` | `parseAudioSection` builds items with `trackIndex = <Order>`, then validates: negative/reversed range, unknown AC3 frame size (`ac3FrameByteSize`) or `(frameTo + 1) · frameBytes > file size` → `setEnabled(false)` with a warning; never dropped. Parked by `(TTAVItem*, order)` via `setPendingAudioRepairs`. |
| `PEND` → `ITEM` | `onOpenAudioFinished` appends the parked repairs when the track with that order arrives; their `trackIndex` is the position the track reaches after `sortByProjectOrder`. |
| `PROJ` → `ANNO` / `ITEM` → `ANNO` | `onStreamPointsLoaded`: every `AudioAnomaly` marker whose approximate AC3 range overlaps a **disabled** repair gets the “(repair DISABLED …)” suffix instead of “planned”. |
| `ANNO` → `PTS` | The restored markers, annotated, go into the model in one `addPoints` call (the project path; a scan's markers take `onPointsDetected`). |
| `ITEM` → `CUT` | `cutAudioTracks`, `.ac3` tracks only: every repair of this track index; disabled ones are skipped with a warning. Item seconds = frame · `frame_time` of the first header. Touching no keep window → skipped. A repair may reach past the window(s) it touches — the cutter writes only frames inside the windows, so the part outside is never looked up; this holds for the short preview windows as for the final cut. Only when the touched windows want **different** target acmods (normalising) the whole track fails with “reaches into cut segments with different channel layouts”. |
| `CUT` → `TABLE` | The item and the common target acmod of the windows it touches (`targetAcmods[s]` when normalising, else -1). |
| `TABLE` → `REENC` | `buildRepairTable` pushes every packet from `frameFrom − 2` (decoder warm-up) to one frame **behind** `frameTo`, in file order; the frames of the range with the edit that silences the masked channels (5 ms raised-cosine fades on the first and the last frame, in the source layout), and a request: the common target acmod and the bit rate of the replaced frames (`frame bytes · 8 · rate / 1536`; one file may switch bit rate, e.g. stereo 192 / 5.1 384 kbit/s). The frame behind the range only supplies the 256 samples that complete the last replacement and is exempt from the checks; a range ending with the file is completed with silence. Hard errors raised here (empty table + message): frame-size change among warm-up and range frames, channel-mode change inside the range, mask bit beyond the channel count, range past the file end. |
| `REENC` → `FT` | One replacement per frame of the range, tagged with the frame number (= packet ordinal in the file), finished one push after its frame. Each is decoded with `drc_scale` 0 (the stream's dynamic range compression is not applied; the encoder cannot write compression words), carries the header fields of its source frame (`dialnorm`, mix levels incl. the extended ones, `dsurmod`, `bsmod`, production info, copyright, original — a field only when it means something in the source frame's own layout and its code is not reserved), and holds exactly the audio of the frame it replaces: the unit feeds the encoder 1280 zero samples first, drops the first packet and so compensates the encoder's 256-sample delay. A header change inside the range starts a new encoder without a seam. `buildRepairTable` rejects a replacement whose size differs from the source frame's. |
| `FT` → `CUTTER` | Tables of all items merged. In the packet loop the frame number is `qRound64(pktTime / frameDurSec)`; a hit writes the replacement bytes with the packet's PTS offset (`writeBytesPacket`) and is not re-encoded by the acmod normalisation (it was built in the target layout already). |

## Assumptions, contracts & pitfalls

- **What a replacement frame shares with the frame it replaces, and what
  not** (gate `ac3_reencode`; on a DVB recording `gate_ac3_reencode_real.sh`):
  header fields, position of the audio (lag 0) and the level when decoded
  without compression (within 0.1 dB) are the source's; the fades land on
  the first and the last sample of the range. Not shared: the compression
  word. A player that applies dynamic range compression plays the replaced
  range without it — 1 to 2 dB off on the measured recording (`TODO.md`).
- **The decoder's noise filling is random.** libav fills mantissas without
  bits from a running generator; two decodes of the same frame differ when
  the frames before it differ. Comparisons of decoded audio need the decoder
  option `cons_noisegen` (as `test_ac3_reencode` sets it).

- **One AC3 frame numbering, two ways to count it.** The scan, the
  replacement table and the audition count packets (ordinal in the file);
  the cutter derives the number from the packet time (`pktTime / 32 ms`).
  They agree while the elementary stream's timestamps start at 0 and run
  without gaps — measured so on the test material and a real recording
  (audit run 10); not checked in the code.
- **Both bounds inclusive** in `TTAudioRepairItem`, `Finding` and
  `TTStreamPoint::audioFrameFrom/To`; `TTStreamPoint::duration()` and the
  dialog's end spin box are end-exclusive (hence the −1 in
  `currentFrameTo` and `approxAc3RangeForMarker`).
- **48 kHz only.** The scan refuses other rates; the dialog and the cut read
  the real frame duration, the fallback estimate in
  `approxAc3RangeForMarker` uses a fixed 32 ms.
- **CBR inside a repair range.** `buildRepairTable` fails on a frame-size
  change inside the range and encodes at that range's bit rate; the load
  validation checks the range against the file size with the **first**
  frame's size, i.e. assumes CBR over the whole file (not seen violated on
  real material, where all 140 654 packets had 1792 bytes; a file with a
  bit-rate switch before the range is unmeasured).
- **Track index = visible list position.** Kept right by
  `remapAudioRepairTracks` on remove and swap; on load by parking under
  `<Order>`. Hand-edited `<Order>` gaps are unvalidated (TODO).
- **Markers are display only** (spec: “Wahrheit ist die Repair-Liste”). The
  marker ↔ repair link is range overlap on the first AC3 track, computed
  anew each time (`repairIndexForMarker`); there is no other UI for the
  repair list — which is why deleting such a marker takes its repair along.
- **Disabled, never dropped.** A repair failing the load validation stays in
  the list, is skipped in the cut with a warning and marked on the marker.
- **Audition ≠ main-window playback.** Main-window playback plays the
  original by design (spec Komponente 4); the audition is a separate file.
- **Preview clips apply repairs** of audio track 0 (`cutAudioTracks(…, {0}, …)`
  in `TTCutPreviewTask` and `ttRebuild*PreviewClip`), under the same rule as
  the final cut; a preview window cutting through a repair keeps its audio.
- **Audition and cut differ only in the target layout.** Both build with the
  same table code; the audition (and the accept probe) with `targetAcmod = -1`,
  the cut with the segments' majority acmod when normalising (unmeasured for
  a repair in a segment whose target differs from the source).
- **A cut reports its repairs.** `cutAudioTracks` keeps one line per track
  with repairs in `TTAVData::audioRepairNotes()` (applied, frames the cutter
  wrote from the table — `TTAudioCutter::repairedFrames()` —, outside the
  cut, disabled), logs it, and `TTCutMainWindow::onCutFinished` appends the
  lines to the completion box. Cleared with each `cutAudioTracks` call, so
  after a preview they describe the preview clip.
- **A failed repair fails its track, and a failed track fails the cut**
  (`mRequireAllInputs`); for H.26x preview clips a failed audio cut only
  logs and the clip is muxed without audio (cut-preview.md).
- **Scan statistics:** one `FrameStat` per decoded frame, plus one failed
  entry for a packet the decoder rejects on send, to keep the numbering in
  step with the packet ordinal; a packet accepted on send that yields no
  frame adds nothing. Besides the 5.1 fields of the LFE search a `FrameStat`
  carries the power of the six 256-sample blocks of the frame (all planes
  but the LFE added) and the number of planes; `channels == 0` marks a frame
  the stop search must not use (decode failure, sample format, frame size).
- **Two searches, both pure functions over the statistics.** `evaluate()`
  (LFE search): a burst in centre and LFE, dropped when the reported range
  is longer than `kMaxLfeFindingFrames` (15 frames, 0.48 s). `evaluateStops()`
  (stop search): at a block boundary the level falls from the three blocks
  before (above −35 dBFS) to the two after; a **hole** — one of the six
  blocks from the boundary is back within 10 dB of the level before — counts
  from 45 dB, a **lasting stop** from 30 dB; boundaries within 19 blocks are
  one event; a stop followed only by silence (< −80 dBFS) up to the end of
  the track is `ttcut-demux`'s padding and is not reported. The thresholds
  are constants in the task's header, not settings; what they rest on:
  `docs/completed-work.md`, “Tonanomalie-Scan: LFE-Grenze und Abbruch-Suche”.
- **Material gate, LFE search only:** LFE must be (near) silent in
  ≥ `anomalyLfeNullPercent` of the 5.1 frames, otherwise “unsuitable, no
  statement” (logged, no LFE markers). The stop search runs on every decoded
  frame, whatever its layout, and does not depend on the gate.
- **The channel preset is a mask of decoder planes** (bit n = plane n, the
  convention of `TTAudioRepairItem::channelMask()`), carried
  `Finding::channelMask` → `TTStreamPoint::audioChannelMask()` →
  `<AudioChannels>` → dialog. 0 means unknown. For a stop marker the dialog
  is reachable only through an existing repair (whose own mask wins), so
  its preset is unused until a repair method for stops exists.
- **The kind of an anomaly marker** (`AudioAnomalyKind`, one enum for the
  scan's `Finding` and the marker) travels `Finding::kind` →
  `TTStreamPoint::audioAnomalyKind()` → `<AnomalyKind>` and decides one
  thing: whether a new repair is offered.

## Redundancy / consolidation candidates

- **Extra frames below an index**
  - sites: `data/ttavdata.cpp:TTAVData::countExtraFramesBefore`, `data/ttaudioanomalyscantask.cpp:countExtrasBefore`, `gui/ttaudiorepairdialog.cpp:countExtrasBefore`
  - shared purpose: binary search in the sorted extra-frame list
  - status: consolidate → one free function next to the extra-frame list; the dialog's comment keeps its copy because it has no `TTAVData`, which a free function does not need
- **AC3 frame duration / size of a file**
  - sites: `data/ttaudioanomalyscantask.cpp:kFrameDurSec`, `gui/ttaudiorepairdialog.cpp:probeFrameDurationMs` (+ `kApproxFrameDurMs`), `data/ttavdata.cpp:TTAVData::cutAudioTracks` (`frame_time` of the first header), `extern/ttaudiocutter.cpp:TTAudioCutter::cut` (`frameDurSec`), `data/ttcutprojectdata.cpp:ac3FrameByteSize`
  - shared purpose: the 32 ms grid in time or bytes
  - status: kept separate → four of them read the real file, the scan's constant is guarded by its 48 kHz refusal; worth one helper only if the load validation stops assuming CBR over the whole file
- **Opening the first audio stream**
  - sites: `extern/ttaudiorepair.cpp:TTAudioRepair::openFirstAudioStream` (table + audition), `data/ttaudioanomalyscantask.cpp:TTAudioAnomalyScanTask::collectFrameStats` (own ladder, `av_find_best_stream`)
  - shared purpose: libav open + stream lookup for one audio file
  - status: documented → TODO P9 (audio decoder opening); the ladder of `avstream/ttavutil.cpp:ttOpenInput` and the dialog's `probeFrameDurationMs` belong to it as well (audit run 10)
- **Packet walk by frame ordinal**
  - sites: `extern/ttaudiorepair.cpp:TTAudioRepair::buildRepairTable`, `gui/ttaudiorepairdialog.cpp:TTAudioRepairDialog::writePreviewWindow`
  - shared purpose: count audio packets to reach an AC3 frame number
  - status: kept separate → a few lines each, with different work per packet
