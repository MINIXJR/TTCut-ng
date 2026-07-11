# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [smart-cut.md](smart-cut.md) | Smart Cut engine (`TTESSmartCut`) for **H.264 + H.265**: segment planning (`analyzeCutPoints`), the three `processSegment` branches, and the bitstream surgery at the re-encode→stream-copy seam (EOS, `frame_num`, POC, MMCO, SPS). One class, runtime branches per codec/stream-type → **one map with a variant matrix** (Codec × PAFF/Non-IDR/Open-GOP/mid-GOP-cut-out), not one map per codec. Findings: SPS-Unification is **not** PAFF-only (also fires on non-bridgeable POC seams). The unreachable "PAFF fallback" branch + its dead helpers and the write-only `realStartAU` field were **removed** (`3191d98`, `1c0bd2b`). | `3191d98` | fresh (2026-07-11: dead PAFF-fallback branch + realStartAU removed, sections re-stamped; named symbols re-checked) |
| [mpeg2-cut.md](mpeg2-cut.md) | MPEG-2 cutting engine (`TTMpeg2VideoStream::cut` + `TTTranscodeProvider`) — a **separate** engine from Smart Cut, descended from the original TTCut. Segment boundary objects, byte-level GOP copy with in-buffer header rewriting, re-encode escape hatch (recursive!). **Contains a CONFIRMED, measured defect:** cut-out on a B-frame silently drops up to M−1 frames (`getCutEndObject` adds a display index to a bitstream-order B-frame count). Also measured: with field-picture material the index list holds two entries per real frame, so cut positions count fields. Frame-type magic numbers named via `enum Mpeg2PicCoding` (`9e3b0d0`). | `3191d98` | fresh (2026-07-11: frame-type constants introduced, section re-stamped; defect still open) |
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `3e5f75e` | fresh (2026-07-10: `base_commit` in this index corrected — it read `bda6529` while the map itself stamps `3e5f75e`; drift `3e5f75e..98b2a66` touches no source of this map) |
| [audio-cut-timing.md](audio-cut-timing.md) | Audio-Cut-Zeitkette: wie ein Schnitt in Video-Frame-Indizes (Anzeige-Ordnung) zu einem tonrasteralignierten Audio-Schnitt wird — Extra-Frame-Korrektur (`countExtraFramesBefore`), Delay, Raster-Snapping mit Feed-Forward-Drift (`planAudioCut`), Einzeldurchlauf-Schnitt mit fortlaufendem PTS + AC3-acmod-Umkodierung (`cutAudioStream`). **Redundanz:** 6 Produzenten bauen dieselbe Sequenz; `(index−extra)/fps` an ≥6 Stellen offen kodiert; zwei Drift-Signale auf einen Slot. Synchron-Kontrakt mit `burst-detection.md` (gleiche Grenzformel). | `f90d0ab` | fresh (2026-07-11: erstellt; alle Symbole gegen den Code gelesen, `cutAudioStream` komplett; Mermaid TD ratio 0.76 vs LR 5.16; am Benders-Beispiel empirisch belegt) |
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

From `audio-cut-timing.md`:
- Six producers (`onDoCut`, `doH264Cut`, `doAudioOnlyCut`, `TTCutPreviewTask` ×2,
  `TTCutPreview`) repeat the same `videoKeepList` → `planAudioCut` → `targetAcmods`
  → `cutAudioStream` sequence with subtle per-path differences — candidate for a
  shared `buildAudioCutPlan` + cut helper.
- `(index − extra)/fps` open-coded in ≥6 places — the audio-time sibling of the
  display-time conversions already noted for `frame-order.md`.
- Two drift signals (`audioDriftCalculated`, `cutAudioDriftCalculated`) into one
  slot (`onAudioDriftUpdated`).

From `smart-cut.md`:
- Three independent `frame_num` bridge computations (SPS-unification branch,
  standard branch, inter-segment block in `smartCutFrames`), each with its own
  wrap correction and its own choice of `log2_max_frame_num` width.
- Four open-coded EOS-NAL emit sites, each re-deciding
  `codecType() == H265 ? kEosNalH265 : kEosNalH264`.
- ~~Dead code: the unreachable `processSegment` "PAFF fallback" branch and the
  write-only `ReencodeContext::realStartAU` field.~~ Removed (`3191d98`, `1c0bd2b`).

**Cross-cutting:** `CLAUDE.md`'s "PAFF Smart Cut implementation notes" attribute
SPS-Unification, MMCO neutralization and `realStartAU` filtering to PAFF. All
three attributions are too narrow or wrong — see `smart-cut.md` pitfalls.
