# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `bda6529` | fresh (2026-07-09: symbols re-verified, line refs removed; drift since `ab76afe` is burst-detection only and does not touch this map) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: **ein** Detektor auf dem Quell-AC3 (`detectAudioBurst`; Peak der Randchunks gegen Median, plus absolutes −40-dB-Gate), Schwelle `burstMinDeltaDb` als Parameter — der frühere Nachfilter `applyBurstDeltaFilter` ist seit `a7d1c0e` entfernt. Zwei Wrapper, drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmBurstWarnings` mit GUI/headless-Zweig). Historische Pitfalls des absoluten Filters dokumentiert. | `966dd61` | fresh (2026-07-09: Claims gegen den Code gelesen — Kriterium, Floor, Default, doppelter Frühausstieg, Extra-Frame-Korrektur in **beiden** Wrappern) |

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
