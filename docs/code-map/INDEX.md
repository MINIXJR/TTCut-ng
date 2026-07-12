# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [smart-cut.md](smart-cut.md) | Smart Cut engine (`TTESSmartCut`) for **H.264 + H.265**: segment planning (`analyzeCutPoints`), the three `processSegment` branches, and the bitstream surgery at the re-encode→stream-copy seam (EOS, `frame_num`, POC, MMCO, SPS). One class, runtime branches per codec/stream-type → **one map with a variant matrix** (Codec × PAFF/Non-IDR/Open-GOP/mid-GOP-cut-out), not one map per codec. Findings: SPS-Unification is **not** PAFF-only (also fires on non-bridgeable POC seams). The unreachable "PAFF fallback" branch + `realStartAU` were removed (`3191d98`, `1c0bd2b`); the two encoder→copy `frame_num` bridges are unified into `bridgeFrameNum` with correct IDR semantics and the four EOS-emit sites into `writeEos` (`df20bb3`, `24fea34` — verified bit-identical on non-IDR material + pixel-identical on a purpose-built IDR-seam project). | `df20bb3` | fresh (2026-07-11: S3/S4 consolidated + verified; symbols re-checked) |
| [mpeg2-cut.md](mpeg2-cut.md) | MPEG-2 cutting engine (`TTMpeg2VideoStream::cut` + `TTTranscodeProvider`) — a **separate** engine from Smart Cut, descended from the original TTCut. Segment boundary objects, byte-level GOP copy with in-buffer header rewriting, re-encode escape hatch (recursive!). **B-frame cut-out defect FIXED (`3b087ae`):** the block that dropped up to M−1 tail frames (display index + bitstream-order B-count mixup) was removed after its "duplicate frames" claim was disproved; regression on TEST.m2v + Futurama M=4 + GUI cut. New pitfall documented: ffmpeg-n = TTCut-display − dropped leading Bs. Still measured/open: field-picture material double-counts index positions (fields vs frames). Frame-type magic numbers named via `enum Mpeg2PicCoding` (`9e3b0d0`). | `3b087ae` | fresh (2026-07-12: defect fixed + map re-stamped) |
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `3e5f75e` | fresh (2026-07-10: `base_commit` in this index corrected — it read `bda6529` while the map itself stamps `3e5f75e`; drift `3e5f75e..98b2a66` touches no source of this map) |
| [audio-cut-timing.md](audio-cut-timing.md) | Audio-Cut-Zeitkette: wie ein Schnitt in Video-Frame-Indizes (Anzeige-Ordnung) zu einem tonrasteralignierten Audio-Schnitt wird — Extra-Frame-Korrektur (`countExtraFramesBefore`), Delay, Raster-Snapping mit Feed-Forward-Drift (`planAudioCut`), Einzeldurchlauf-Schnitt mit fortlaufendem PTS + AC3-acmod-Umkodierung (`cutAudioStream`). **Konsolidiert (`7849f66`):** 5 Producer + Drift-only-Stelle über `cutAudioTracks` + `buildVideoKeepList`, bit-identisch belegt (Benders MP2 deu+eng, ServusTV AC3); zwei abweichende Vorschau-Pfade bewusst offen (Option A), zwei Drift-Signale offen. Synchron-Kontrakt mit `burst-detection.md` (gleiche Grenzformel). | `7849f66` | fresh (2026-07-11: A1/A2 konsolidiert + bit-identisch verifiziert; Mermaid TD 0.76 vs LR 5.16) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: **ein** Detektor auf dem Quell-AC3 (`detectAudioBurst`; Peak der Randchunks gegen Median, plus absolutes −40-dB-Gate), Schwelle `burstMinDeltaDb` als Parameter — der frühere Nachfilter `applyBurstDeltaFilter` ist seit `a7d1c0e` entfernt. Zwei Wrapper, drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmBurstWarnings` mit GUI/headless-Zweig). **Erkennung ist ein Hinweis, kein Urteil** — gemessene Auflösungsgrenzen unter Pitfalls (32-ms-Zeitauflösung, nur ~64 ms von 200 ms geprüft, ungeprüfter Chunk hebt den Median, Gate um <4 dB passiert). Mitkartiert: der acmod-Pfad, der sich Spalte 5 mit dem Burst-Icon teilt; seit `666ed08` ist `updateHintColumn()` der einzige Eingang und hält den Reihenfolge-Vertrag. Tote `AcmodInfo`-ChangeTime-Felder entfernt (`f4d4e66`). | `f4d4e66` | fresh (2026-07-12: totes-Feld-Finding aufgelöst, Symbole re-geprüft) |
| [ttcut-demux.md](ttcut-demux.md) | TS→ES-Demux-Pipeline (Bash) mit Mess- und Meldekette: Timestamp-Repair, parallele Extraktion (+MPEG-2 Leading-B-Skip, Null-Truncation), `ttcut-pts-analyze` (3 Methoden; Grid-Methode kann TS-Korruption nicht von Field-Pictures unterscheiden), Gap-Erkennung + Silence-Insert, A/V-Dauerabgleich + End-Padding, `.info`-Erzeugung und wer welche Felder konsumiert (`TTESInfo`→`TTAVData`: `es_extra_frames` hat Vorrang vor Parser-Extras, speist Audio-Korrektur UND GUI-„Defekt:"-Marker; Dauer-Felder sind reine Menschen-Info). **Melde-Defekte FIXED (2026-07-12):** `VIDEO_DURATION` jetzt Video-PTS-Spanne (start_time statt Container) → Frame-Zahl exakt, Über-Padding weg, ehrliche Drift (`f85b237`+`d7a046b`); Warntext neutral; GUI-„Defekt:" per Parser-Abgleich zu „Feldpaare:" (Klassifikation in `onOpenVideoFinished`, wo `extraIndices()` bereit ist — `fc2a573`). Offen: Feld-vs-Frame-Index im Cut (Defekt 2, mpeg2-cut.md). | `d7a046b` | fresh (2026-07-12: Melde-Fix umgesetzt + verifiziert; Mermaid LR 1.78 vs TD 2.12) |

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
- ~~Six producers repeat the `videoKeepList` → `planAudioCut` → `targetAcmods` →
  `cutAudioStream` sequence; `(index − extra)/fps` open-coded in ≥6 places.~~
  **Consolidated (`7849f66`)** onto `TTAVData::cutAudioTracks` + `buildVideoKeepList`;
  bit-identical verified (Benders MP2, ServusTV AC3). Two divergent preview paths
  left by choice (Option A).
- Two drift signals (`audioDriftCalculated`, `cutAudioDriftCalculated`) into one
  slot (`onAudioDriftUpdated`) — still open.

From `smart-cut.md`:
- ~~Frame_num bridges + EOS-emit sites duplicated.~~ Resolved (`df20bb3`, `24fea34`):
  the two encoder→copy bridges now share `bridgeFrameNum` (corrected finding: the
  inter-segment block is a *different* computation and stays); the four EOS sites
  share `writeEos`.
- ~~Dead code: the unreachable `processSegment` "PAFF fallback" branch and the
  write-only `ReencodeContext::realStartAU` field.~~ Removed (`3191d98`, `1c0bd2b`).

**Cross-cutting:** `CLAUDE.md`'s "PAFF Smart Cut implementation notes" attribute
SPS-Unification, MMCO neutralization and `realStartAU` filtering to PAFF. All
three attributions are too narrow or wrong — see `smart-cut.md` pitfalls.
