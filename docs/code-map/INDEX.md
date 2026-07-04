# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** (display shows display-frame n; cut's `selectFramesNonPAFF` applies a display-order mapping → lands ~2 frames later at a B-frame Cut-In). | `ccd06b8` | fresh (corrected 2026-06-07, empirically verified) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: ein kontextrelativer Detektor (Quell-AC3) → absoluter Threshold-Nachfilter → drei Konsumenten (CutList-Spalte 5, Preview-Warnung, Final-Warndialog). **Pitfalls: Default −30 verwirft reale DVB-Bursts; Skala kontraintuitiv; kein Listen-Refresh bei Threshold-Änderung.** | `88445b3` | fresh (2026-07-04, empirisch belegt) |

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
