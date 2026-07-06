# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** (display shows display-frame n; cut's `selectFramesNonPAFF` applies a display-order mapping → lands ~2 frames later at a B-frame Cut-In). | `ab76afe` | fresh (updated 2026-07-06: P/I-navigation −1-sentinel fix, v0.72.2) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: kontextrelativer Detektor (Quell-AC3) → kontextrelativer Delta-Filter (`burstMinDeltaDb`, seit `48cf828`) → drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmBurstWarnings` mit GUI/headless-Zweig). Historische Pitfalls des absoluten Filters dokumentiert. | `ab76afe` | fresh (updated 2026-07-06: stamp bump; ttffmpegwrapper drift is buildDisplayOrderMap/v0.72.1, not burst-related) |

## Project module overview

Not yet generated. Run `code-map index` to build the coarse module/class
responsibility overview + top-level Mermaid diagram. Until then, the per-subsystem
detail maps above are the authoritative source; the existing
`memory/architecture_*.md` notes (TTSettings, TTSearchTask, cutVideoName split)
also cover specific areas.

## Project-wide redundancy patterns

Collected from detail maps as they are created. From `frame-order.md`:
- Three ad-hoc decode-order↔display-time conversions (`onPlayVideo`,
  `onPlaybackFinished`, `onPlaybackPositionChanged`) — candidate for a shared
  `decodeIndexToDisplaySeconds` / `displaySecondsToDecodeIndex` helper.
