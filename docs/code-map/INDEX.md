# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [smart-cut.md](smart-cut.md) | Smart Cut engine (`TTESSmartCut`) for **H.264 + H.265**: segment planning (`analyzeCutPoints`), the three `processSegment` branches, and the bitstream surgery at the re-encode→stream-copy seam (EOS, `frame_num`, POC, MMCO, SPS). One class, runtime branches per codec/stream-type → **one map with a variant matrix** (Codec × PAFF/Non-IDR/Open-GOP/mid-GOP-cut-out), not one map per codec. Findings: SPS-Unification is **not** PAFF-only (also fires on non-bridgeable POC seams); the "PAFF fallback" branch is **unreachable**; `realStartAU` is a dead field. | `98b2a66` | fresh (2026-07-10: created; every named symbol grepped, Mermaid rendered TD ratio 0.24 vs LR 9.3) |
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `3e5f75e` | fresh (2026-07-10: `base_commit` in this index corrected — it read `bda6529` while the map itself stamps `3e5f75e`; drift `3e5f75e..98b2a66` touches no source of this map) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: **ein** Detektor auf dem Quell-AC3 (`detectAudioBurst`; Peak der Randchunks gegen Median, plus absolutes −40-dB-Gate), Schwelle `burstMinDeltaDb` als Parameter — der frühere Nachfilter `applyBurstDeltaFilter` ist seit `a7d1c0e` entfernt. Zwei Wrapper, drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmBurstWarnings` mit GUI/headless-Zweig). **Erkennung ist ein Hinweis, kein Urteil** — gemessene Auflösungsgrenzen unter Pitfalls (32-ms-Zeitauflösung, nur ~64 ms von 200 ms geprüft, ungeprüfter Chunk hebt den Median, Gate um <4 dB passiert). Mitkartiert: der acmod-Pfad, der sich Spalte 5 mit dem Burst-Icon teilt; seit `666ed08` ist `updateHintColumn()` der einzige Eingang und hält den Reihenfolge-Vertrag. | `666ed08` | fresh (2026-07-10: Spalte-5-Eingang nach `666ed08` gegen den Code gelesen + GUI-verifiziert; Symbole re-verifiziert; Messgrenzen an `TEST_deu.ac3`/ServusTV empirisch belegt) |

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

From `smart-cut.md`:
- Three independent `frame_num` bridge computations (SPS-unification branch,
  standard branch, inter-segment block in `smartCutFrames`), each with its own
  wrap correction and its own choice of `log2_max_frame_num` width.
- Four open-coded EOS-NAL emit sites, each re-deciding
  `codecType() == H265 ? kEosNalH265 : kEosNalH264`.
- Dead code: the unreachable `processSegment` "PAFF fallback" branch and the
  write-only `ReencodeContext::realStartAU` field.

**Cross-cutting:** `CLAUDE.md`'s "PAFF Smart Cut implementation notes" attribute
SPS-Unification, MMCO neutralization and `realStartAU` filtering to PAFF. All
three attributions are too narrow or wrong — see `smart-cut.md` pitfalls.
