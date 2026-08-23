# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [smart-cut.md](smart-cut.md) | Smart Cut engine (`TTESSmartCut`) for **H.264 + H.265**: segment planning (`analyzeCutPoints`), the `processSegment` branches (standard, H.264 SPS-unification, HEVC RASL-preserving seam with rollback), and the bitstream surgery at the re-encode→stream-copy seam (EOS/EOB, `frame_num`, POC, MMCO, SPS; HEVC bit-level machinery in `extern/tthevcseam.{h,cpp}`). One class, runtime branches per codec/stream-type → **one map with a variant matrix** (Codec × PAFF/Non-IDR/Open-GOP/mid-GOP-cut-out), not one map per codec. Findings: SPS-Unification is **not** PAFF-only (also fires on non-bridgeable POC seams). The unreachable "PAFF fallback" branch + `realStartAU` were removed (`3191d98`, `1c0bd2b`); the two encoder→copy `frame_num` bridges are unified into `bridgeFrameNum` with correct IDR semantics and the four EOS-emit sites into `writeEos` (`df20bb3`, `24fea34` — verified bit-identical on non-IDR material + pixel-identical on a purpose-built IDR-seam project). | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Diff-Fläche seit dem letzten Stand betrifft ausschließlich Teile der geteilten Dateien (`data/ttavdata.{cpp,h}`), die diese Karte nicht beschreibt (Audio-Anomalie-Erkennung + -Reparatur, siehe `audio-cut-timing.md`); Inhalt unverändert) |
| [mpeg2-cut.md](mpeg2-cut.md) | MPEG-2 cutting engine (`TTMpeg2VideoStream::cut` + `TTTranscodeProvider`) — a **separate** engine from Smart Cut, descended from the original TTCut. Segment boundary objects, byte-level GOP copy with in-buffer header rewriting, re-encode escape hatch (recursive!). Pitfall: ffmpeg-n = TTCut-display − dropped leading Bs. Still measured/open: field-picture material double-counts index positions (fields vs frames). Frame-type magic numbers named via `enum Mpeg2PicCoding`. Extra-frame consumer (`data/ttavdata.cpp`) prefers the parser's `extraIndices()` over `.info es_extra_frames`, decided in `onOpenVideoFinished` once the parser list is built — detail in `audio-cut-timing.md`. | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Diff-Fläche seit dem letzten Stand betrifft ausschließlich Teile der geteilten Dateien (`data/ttavdata.{cpp,h}`, `common/ttsettings.h`), die diese Karte nicht beschreibt (Audio-Anomalie-Erkennung + -Reparatur, siehe `audio-cut-timing.md`); Inhalt unverändert) |
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Diff-Fläche seit dem letzten Stand betrifft ausschließlich Teile der geteilten Dateien (`data/ttavdata.cpp`, `extern/ttffmpegwrapper.{cpp,h}`, `gui/ttcutmainwindow.cpp`), die diese Karte nicht beschreibt (Audio-Anomalie-Erkennung + -Reparatur, siehe `audio-cut-timing.md`); Inhalt unverändert) |
| [audio-cut-timing.md](audio-cut-timing.md) | Audio-Cut-Zeitkette: wie ein Schnitt in Video-Frame-Indizes (Anzeige-Ordnung) zu einem tonrasteralignierten Audio-Schnitt wird — Extra-Frame-Korrektur (`countExtraFramesBefore`), Delay, Raster-Snapping mit Feed-Forward-Drift (`planAudioCut`), Einzeldurchlauf-Schnitt mit fortlaufendem PTS + AC3-acmod-Umkodierung (`cutAudioStream`). **Konsolidiert (`7849f66`):** 5 Producer + Drift-only-Stelle über `cutAudioTracks` + `buildVideoKeepList`, bit-identisch belegt (Benders MP2 deu+eng, ServusTV AC3); zwei abweichende Vorschau-Pfade bewusst offen (Option A), zwei Drift-Signale offen. **Nachgezogen (2026-07-12):** `doH264Cut` verliert seinen offen-codierten Keep-List-Sonderfall und ruft jetzt ebenfalls `buildVideoKeepList` auf — kein Produzent baut die Extra-Frame-Korrektur mehr selbst (`1d5b956`, erneut bit-identisch: ServusTV H.264, Designermode H.265); für MPEG-2 hat der Bitstream-Parser jetzt Vorrang vor `.info` bei der Extra-Frame-Quelle, Ladepunkt auf `onOpenVideoFinished` verschoben (`loadMpeg2FieldExtras` entfernt, ersetzt durch `loadExtraFrameIndices` — `b69dfcf`, `fc2a573`); `cutAudioTracks` range-checkt `trackIndices` jetzt. Synchron-Kontrakt mit `burst-detection.md` (gleiche Grenzformel). **Repair-Pipeline ergänzt (2026-08-19):** neue Kante `ACMOD → REPAIR → CUT` — `cutAudioTracks` baut die Ersatzframe-Tabelle (`TTAudioRepair::buildRepairTable`) pro Spur nach `computeTargetAcmods`; `cutAudioStream` fragt sie im Paket-Loop vor der acmod-Prüfung ab. Tabellenbau-Fehler oder ein Item, das eine Segmentgrenze überspannt, brechen die Spur vor dem Schreiben ab (kein stilles Weiterschneiden ohne Reparatur); ein Item außerhalb jedes Keep-Fensters wird still übersprungen. E2E am 02x06-Korpusfall gemessen: LFE im Reparaturbereich −93…−117 dBFS (vorher Peak −8,9 dBFS), Center-Burst-Peak −2,8 dB → −22,3 dB; alle Frames außerhalb exakt (Paket-MD5) mit einem Lauf ohne `<Repair>` identisch. | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Inhalt bereits aktuell (Abschnitt „Final-Review-Welle eingearbeitet" oben deckt die Repair-Pipeline ab); nur der Stempel war veraltet) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: **ein** Detektor auf dem Quell-AC3 (`detectAudioBurst`; Peak der Randchunks gegen Median, plus absolutes −40-dB-Gate), Schwelle `burstMinDeltaDb` als Parameter — Zwei Wrapper, drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmBurstWarnings` mit GUI/headless-Zweig). **Erkennung ist ein Hinweis, kein Urteil** — gemessene Auflösungsgrenzen unter Pitfalls (32-ms-Zeitauflösung, nur ~64 ms von 200 ms geprüft, ungeprüfter Chunk hebt den Median, Gate um <4 dB passiert). Mitkartiert: der acmod-Pfad, der sich Spalte 5 mit dem Burst-Icon teilt; `updateHintColumn()` ist der einzige Eingang und hält den Reihenfolge-Vertrag. Tote `AcmodInfo`-ChangeTime-Felder entfernt (`f4d4e66`). | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Diff-Fläche seit dem letzten Stand betrifft ausschließlich Teile der geteilten Dateien (`common/ttsettings.cpp`, `data/ttavdata.{cpp,h}`, `extern/ttffmpegwrapper.{cpp,h}`, `gui/ttcutmainwindow.cpp`), die diese Karte nicht beschreibt (Audio-Anomalie-Erkennung + -Reparatur, siehe `audio-cut-timing.md`); Inhalt unverändert) |
| [ttcut-demux.md](ttcut-demux.md) | TS→ES-Demux-Pipeline (Bash) mit Mess- und Meldekette: Timestamp-Repair, parallele Extraktion (+MPEG-2 Leading-B-Skip, Null-Truncation), `ttcut-pts-analyze` (3 Methoden; Grid-Methode kann TS-Korruption nicht von Field-Pictures unterscheiden), Gap-Erkennung + Silence-Insert, A/V-Dauerabgleich + End-Padding, `.info`-Erzeugung und wer welche Felder konsumiert (`TTESInfo`→`TTAVData`: bei MPEG-2 haben die Parser-Feldpaare Vorrang vor `es_extra_frames`; H.264/H.265 nutzen weiter .info; speist Audio-Korrektur UND GUI-Marker; Dauer-Felder sind reine Menschen-Info). **Melde-Defekte FIXED (2026-07-12):** `VIDEO_DURATION` jetzt Video-PTS-Spanne (start_time statt Container) → Frame-Zahl exakt, Über-Padding weg, ehrliche Drift (`f85b237`+`d7a046b`); Warntext neutral; GUI-„Defekt:" per Parser-Abgleich zu „Feldpaare:" (Klassifikation in `onOpenVideoFinished`, wo `extraIndices()` bereit ist — `fc2a573`). Offen: Feld-vs-Frame-Index im Cut (Defekt 2, mpeg2-cut.md). **Konsolidiert (2026-07-12):** normalized-MKV-Modus entfernt (v0.52-Relikt, `ce06817`, −203 Zeilen, ES ist Default, `-e` No-op) + alle 5 Redundanz-Kandidaten aufgelöst (`probe_first_video_pts`, `probe_audio_props`, `warn_ffmpeg_log`); ES-Ausgaben byte-identisch verifiziert. **Defekt-Erkennung+-Reparatur Rev 3 (2026-07-18):** frame-skalige DTS-Gap-Erkennung über alle VDR-Segmente, echte Audio-Reparatur (Silence-Insert/Truncate per Segment-Copy, Überlappungs-Koaleszierung), neue `.info`-Felder (`es_missing_ranges`, `corrupt_frame_ranges`, `audio_N_silence_ms`/`removed_ms`) → geclusterte GUI-Landezonen; Gates 07x11/07x12 gemessen. | `10190992` | fresh (2026-08-20: `.info audio_gap_frames → TTAVData`-Zeile korrigiert — zweiter Konsument `TTAVData::audioGapFrameRanges()` clustert dieselben Indizes für `TTAudioAnomalyScanTask`s Gap-Overlap-Kennzeichnung (Audio-Anomalie-Erkennung + -Reparatur, `data/ttaudioanomalyscantask.cpp` neu in `sources:`); Stempel auf v0.82.0 (`10190992`) gezogen) |
| [progress-reporting.md](progress-reporting.md) | Fortschrittsmeldekette: alle Wege einer Statusmeldung vom Produzenten (Stream-Parser, Cut-Tasks, Smart Cut, Muxer) bis `TTProgressBar` — beide `TTThreadTaskPool`s (AVData + Stream-Point), der Direktpfad `task == 0` (Wert IST Prozent, oder bei `Stage` die `ProgressStage`-Id; bei Pool-Pfaden wird `value` verworfen und `overallPercentage()` genutzt), die MPEG-2-Operationsklammer `mCutOperationActive` (Pool-`aborted` feuert VOR `exit`, Flag wird in `onThreadPoolExit` konsumiert), und seit dem Restzeit-Umbau (2026-08-09) `TTProgressEstimator`/`ITTCalibrationStore`: `operationPlanReady` liefert den Stufenplan (Video/Audio/Mux, Arbeitsmenge in Sekunden bzw. Frames), `Stage`-Meldungen steuern `beginStage()`, gemessene Stufenzeiten kalibrieren pro Maschine/Material persistent (`QSettings` `progressCalibration/`) — kein Codec-Festwert. **Befunde:** `runEncodePass()` meldet jetzt alle 10 gesendeten Frames (vorheriger Stillstand behoben); Smart-Cut-Prozent ist arbeitsgewichtet (gemessene ms/Frame Kopie vs. Re-Encode, Fallback zuerst auf die im letzten Lauf gemessene Rate `videok/<codec>`, sonst 1:1) statt reiner Frame-Zählung; mplex-Zweig setzt `mLastCutError` weiterhin nie (TODO.md, außerhalb dieses Umbaus). Grundlage/Nachweis der Spec `2026-08-09-progress-eta-weighted`. | `10190992` | fresh (2026-08-20: nur Stempel auf v0.82.0 (`10190992`) gezogen — Diff-Fläche seit dem letzten Stand betrifft ausschließlich Teile der geteilten Dateien (`data/ttaudioonlycuttask.cpp`, `data/ttavdata.{cpp,h}`, `data/tth26xcuttask.cpp`, `extern/ttffmpegwrapper.cpp`, `gui/ttcutmainwindow.{cpp,h}`), die diese Karte nicht beschreibt (Audio-Anomalie-Erkennung + -Reparatur, siehe `audio-cut-timing.md`); Inhalt unverändert) |
| [detection-and-search.md](detection-and-search.md) | Automatische Erkennung + Suche: **eine** Basisklasse `TTSearchTask` (N parallele Dekoder, geteilter Bildindex via `preBuiltFrameIndex`, `collectNextBatch`/`parallelMap`) trägt zwei Ergebnisformen — gerichtete Suche (`found(pos, wasAborted)`, hält beim ersten Treffer: Schwarzbild, Szenenwechsel, Logo) und Voll-Scan (`pointsDetected`, sammelt alle Wechsel: `TTAspectScanTask`). Dazu die drei Analysen ohne Bilddekodierung (MPEG-2-Sequenz-Header, Audio-Stille/acmod, AC3-Center+LFE-Burst-Scanner `TTAudioAnomalyScanTask` — automatischer Start nach dem Laden, abschaltbar) im selben Pool, und — ausserhalb der `TTSearchTask`-Familie — die Gleichbild-Suche `TTFrameSearchTask` (eigener Dekoder, eigenes Melde-/Index-Verhalten). Enthält den Klassifizierer `classifyAspectSample` (drei Werte inkl. `NoStatement` für Schwarzbilder) + `TTAspectHysteresis`, die Codec-Varianten-Matrix (MPEG-2 = 1 Dekoder, keine Header-Liste bei H.26x), die Kanten-Semantik von Abbruch/Aufräumen, und wie die drei Analysen sich seit `TTAnalysisLog` im Fortschrittsdialog-Detailbereich erklären. **Zwei Ur-Defekte belegt und behoben (2026-07-29):** Video-Worker hing an der Header-Liste, die `TTH26xVideoStream` nie anlegt; Worker-Wrapper ohne `buildFrameIndex()` gab leere Bilder zurück — Pillarbox-Erkennung hat für H.264/H.265 nie funktioniert. Mitkartiert: Abbruch-Use-after-free (`e247dbda`), der Stichprobenabstand-≤-Hysteresefenster-Vertrag (`aed01838`), ein Spät-Abbruch nach Taskende (`0af72ab1`) und fehlende Skip-Meldungen für nie gebaute Worker (`3b24be6a`). | `10190992` | fresh (2026-08-20: dritte „ohne Bilddekodierung"-Familie `TTAudioAnomalyScanTask` (AC3 Center+LFE-Burst-Scan) samt drittem Auslöser-Pfad (`maybeStartAutoAnomalyScan()`, zwei Ladepipeline-Einsprünge über `onAVDataReloaded()`/`onAVItemChanged()`, s. dortiger Implementierungskommentar) und dem Repair-Kontextmenü-Konsumenten in `TTStreamPointWidget::onContextMenu` eingearbeitet; Diagramm-Richtung erneut gemessen (TD-Verhältnis 0,80 vs. LR 5,66, TD/TB behalten); Stempel auf v0.82.0 (`10190992`) gezogen) |

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

From `detection-and-search.md`:
- The three directed searches (`_blackframe`, `_logo`, `_scenechange`) share an
  identical `operation()` body — only the per-frame verdict differs. Candidate
  for a `matchesAt(pos, workerIndex)` template method on `TTSearchTask`.
- The same 10%-border mask + `step = 2` sampling rule exists three times:
  `TTSearchTask::isFrameBlackAt`, `TTSearchTask::buildHistogramAt` (both MPEG-2
  fallbacks of what `TTFFmpegWrapper` does for H.26x) and `centreMeanLuma` in
  `ttaspectdetect.cpp`.
- ~~The three directed searches connect only `finished → deleteLater`, not
  `aborted`.~~ Fixed (`f8fe7dd6`), together with the root hazard underneath it:
  `TTThreadTask::run()` emitted its terminal signal before the virtual
  `cleanUp()`, which every owner turns into a use-after-free by wiring that
  signal to `deleteLater` (reproduced under ASAN).

From `progress-reporting.md`:
- The audio progress lambda pair for `cutAudioTracks` exists three times
  (`doMpeg2Cut`, `doH264Cut`, `doAudioOnlyCut`) — candidate for one helper.
  Still open.
- Progress-bar creation + cancel wiring duplicated in the `Init` and `Start`
  branches of `TTCutMainWindow::onStatusReport` — candidate `ensureProgressBar()`.
  Still open.
- ~~Three "how long" sources with different semantics (direct timer, pool
  `overallTime()`, task `elapsedTime()`).~~ **Resolved** (spec
  `2026-08-09-progress-eta-weighted`, commits `1fd8de26`..`fb7cbb76`): all
  three removed outright; `TTProgressEstimator`'s injected clock plus the
  dialog's debug-only wall clock are the sole remaining time sources, with
  non-overlapping purposes.

**Cross-cutting:** `CLAUDE.md`'s "PAFF Smart Cut implementation notes" attribute
SPS-Unification, MMCO neutralization and `realStartAU` filtering to PAFF. All
three attributions are too narrow or wrong — see `smart-cut.md` pitfalls.
