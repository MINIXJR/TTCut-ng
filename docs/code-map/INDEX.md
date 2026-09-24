# Code Map Index — TTCut-ng

Maintained architecture/data-flow maps. See the `code-map` skill for how these are
created, checked for staleness, and updated. **Before answering an architecture or
data-flow question, check here first.**

## Detail maps

| Map | Subsystem | base_commit | Status |
|---|---|---|---|
| [smart-cut.md](smart-cut.md) | Smart Cut engine (`TTESSmartCut`) for **H.264 + H.265**: segment planning (`analyzeCutPoints`), the `processSegment` branches (standard, H.264 SPS-unification, HEVC RASL-preserving seam with rollback), and the bitstream surgery at the re-encode→stream-copy seam (EOS/EOB, `frame_num`, POC, MMCO, SPS; HEVC bit-level machinery in `extern/tthevcseam.{h,cpp}`, H.264 in `extern/tth264bitstream.{h,cpp}`, both on the one bit layer `avstream/ttbitstream`). One class, runtime branches per codec/stream-type → **one map with a variant matrix** (Codec × PAFF/Non-IDR/Open-GOP/mid-GOP-cut-out), not one map per codec. Findings: SPS-Unification is **not** PAFF-only (also fires on non-bridgeable POC seams). The unreachable "PAFF fallback" branch + `realStartAU` were removed (`3191d98`, `1c0bd2b`); the two encoder→copy `frame_num` bridges are unified into `bridgeFrameNum` with correct IDR semantics and the four EOS-emit sites into `writeEos` (`df20bb3`, `24fea34` — verified bit-identical on non-IDR material + pixel-identical on a purpose-built IDR-seam project). | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7: Display-Order-Kante über `TTMkvVideoOptions`; Symbol-Grep) |
| [mpeg2-cut.md](mpeg2-cut.md) | MPEG-2 cutting engine (`TTMpeg2VideoStream::cut` + `TTTranscodeProvider`) — a **separate** engine from Smart Cut, descended from the original TTCut. Segment boundary objects, byte-level GOP copy with in-buffer header rewriting, re-encode escape hatch (recursive!). Pitfall: ffmpeg-n = TTCut-display − dropped leading Bs. Still measured/open: field-picture material double-counts index positions (fields vs frames). Frame-type magic numbers named via `enum Mpeg2PicCoding`. Extra-frame consumer (`data/ttavdata.cpp`) prefers the parser's `extraIndices()` over `.info es_extra_frames`, decided in `onOpenVideoFinished` once the parser list is built — detail in `audio-cut-timing.md`. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7: mplex-Ziel, Skript), Symbol-Grep) |
| [frame-order.md](frame-order.md) | Frame-order pipeline: still-image display vs cut-set vs smart-cut execution; decode-order vs display-order semantics. **Historical root cause of the Cut-In preview bug: display↔cut index-interpretation asymmetry** — the cut path mixed a display index with a decode index and landed ~4 display-frames late (RESOLVED v0.72.0; cut positions are display positions end to end, converted via `TTDisplayOrderMap`). | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7: Display-PTS je Paket aus ns gerundet, Übergabe über `TTMkvVideoOptions`; Symbol-Grep) |
| [audio-cut-timing.md](audio-cut-timing.md) | Audio-Cut-Zeitkette: wie ein Schnitt in Video-Frame-Indizes (Anzeige-Ordnung) zu einem tonrasteralignierten Audio-Schnitt wird — Extra-Frame-Korrektur (`countExtraFramesBefore`), Delay, Raster-Snapping mit Feed-Forward-Drift (`planAudioCut`), Einzeldurchlauf-Schnitt mit fortlaufendem PTS + AC3-acmod-Umkodierung (`TTAudioCutter::cut`). **Konsolidiert (`7849f66`):** 5 Producer + Drift-only-Stelle über `cutAudioTracks` + `buildVideoKeepList`, bit-identisch belegt (Benders MP2 deu+eng, ServusTV AC3); zwei abweichende Vorschau-Pfade bewusst offen (Option A), zwei Drift-Signale offen. **Nachgezogen (2026-07-12):** `doH264Cut` verliert seinen offen-codierten Keep-List-Sonderfall und ruft jetzt ebenfalls `buildVideoKeepList` auf — kein Produzent baut die Extra-Frame-Korrektur mehr selbst (`1d5b956`, erneut bit-identisch: ServusTV H.264, Designermode H.265); für MPEG-2 hat der Bitstream-Parser jetzt Vorrang vor `.info` bei der Extra-Frame-Quelle, Ladepunkt auf `onOpenVideoFinished` verschoben (`loadMpeg2FieldExtras` entfernt, ersetzt durch `loadExtraFrameIndices` — `b69dfcf`, `fc2a573`); `cutAudioTracks` range-checkt `trackIndices` jetzt. Synchron-Kontrakt mit `burst-detection.md` (gleiche Grenzformel). **Repair-Pipeline ergänzt (2026-08-19):** neue Kante `ACMOD → REPAIR → CUT` — `cutAudioTracks` baut die Ersatzframe-Tabelle (`TTAudioRepair::buildRepairTable`) pro Spur nach `computeTargetAcmods`; `TTAudioCutter::cut` fragt sie im Paket-Loop vor der acmod-Prüfung ab. Tabellenbau-Fehler oder ein Item, das eine Segmentgrenze überspannt, brechen die Spur vor dem Schreiben ab (kein stilles Weiterschneiden ohne Reparatur); ein Item außerhalb jedes Keep-Fensters wird still übersprungen. E2E am 02x06-Korpusfall gemessen: LFE im Reparaturbereich −93…−117 dBFS (vorher Peak −8,9 dBFS), Center-Burst-Peak −2,8 dB → −22,3 dB; alle Frames außerhalb exakt (Paket-MD5) mit einem Lauf ohne `<Repair>` identisch. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [burst-detection.md](burst-detection.md) | Audio-Burst-Erkennung: **ein** Detektor auf dem Quell-AC3 (`TTAudioCutter::detectBurst`; Peak der Randchunks gegen Median, plus absolutes −40-dB-Gate), Schwelle `burstMinDeltaDb` als Parameter — Zwei Wrapper, drei Konsumenten (CutList-Spalte 5 mit Settings-Refresh, Preview-Warnung, konsolidierter Final-Warndialog `confirmCutWarnings` mit GUI/headless-Zweig). Seit 2026-09-19 mitkartiert: MPEG-2-Seitenverhältnis am Schnittrand (`ttAnalyzeAspectWindow`, dritter Produzent in Spalte 5, Vorschau-Sprungknopf über das gemeinsame `moveCutEdge`). **Erkennung ist ein Hinweis, kein Urteil** — gemessene Auflösungsgrenzen unter Pitfalls (32-ms-Zeitauflösung, nur ~64 ms von 200 ms geprüft, ungeprüfter Chunk hebt den Median, Gate um <4 dB passiert). Mitkartiert: der acmod-Pfad, der sich Spalte 5 mit dem Burst-Icon teilt; `updateHintColumn()` ist der einzige Eingang und hält den Reihenfolge-Vertrag. Tote `AcmodInfo`-ChangeTime-Felder entfernt (`f4d4e66`). | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [ttcut-demux.md](ttcut-demux.md) | TS→ES-Demux-Pipeline (Bash) mit Mess- und Meldekette: Timestamp-Repair, parallele Extraktion (+MPEG-2 Leading-B-Skip, Null-Truncation), `ttcut-pts-analyze` (3 Methoden; Grid-Methode kann TS-Korruption nicht von Field-Pictures unterscheiden), Gap-Erkennung + Silence-Insert, A/V-Dauerabgleich + End-Padding, `.info`-Erzeugung und wer welche Felder konsumiert (`TTESInfo`→`TTAVData`: bei MPEG-2 haben die Parser-Feldpaare Vorrang vor `es_extra_frames`; H.264/H.265 nutzen weiter .info; speist Audio-Korrektur UND GUI-Marker; Dauer-Felder sind reine Menschen-Info). **Melde-Defekte FIXED (2026-07-12):** `VIDEO_DURATION` jetzt Video-PTS-Spanne (start_time statt Container) → Frame-Zahl exakt, Über-Padding weg, ehrliche Drift (`f85b237`+`d7a046b`); Warntext neutral; GUI-„Defekt:" per Parser-Abgleich zu „Feldpaare:" (Klassifikation in `onOpenVideoFinished`, wo `extraIndices()` bereit ist — `fc2a573`). Offen: Feld-vs-Frame-Index im Cut (Defekt 2, mpeg2-cut.md). **Konsolidiert (2026-07-12):** normalized-MKV-Modus entfernt (v0.52-Relikt, `ce06817`, −203 Zeilen, ES ist Default, `-e` No-op) + alle 5 Redundanz-Kandidaten aufgelöst (`probe_first_video_pts`, `probe_audio_props`, `warn_ffmpeg_log`); ES-Ausgaben byte-identisch verifiziert. **Defekt-Erkennung+-Reparatur Rev 3 (2026-07-18):** frame-skalige DTS-Gap-Erkennung über alle VDR-Segmente, echte Audio-Reparatur (Silence-Insert/Truncate per Segment-Copy, Überlappungs-Koaleszierung), neue `.info`-Felder (`es_missing_ranges`, `corrupt_frame_ranges`, `audio_N_silence_ms`/`removed_ms`) → geclusterte GUI-Landezonen; Gates 07x11/07x12 gemessen. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [progress-reporting.md](progress-reporting.md) | Fortschrittsmeldekette: alle Wege einer Statusmeldung vom Produzenten (Stream-Parser, Cut-Tasks, Smart Cut, Muxer) bis `TTProgressBar` — beide `TTThreadTaskPool`s (AVData + Stream-Point), der Direktpfad `task == 0` (Wert IST Prozent, oder bei `Stage` die `ProgressStage`-Id; bei Pool-Pfaden wird `value` verworfen und `overallPercentage()` genutzt), die MPEG-2-Operationsklammer `mCutOperationActive` (Pool-`aborted` feuert VOR `exit`, Flag wird in `onThreadPoolExit` konsumiert), und seit dem Restzeit-Umbau (2026-08-09) `TTProgressEstimator`/`ITTCalibrationStore`: `operationPlanReady` liefert den Stufenplan (Video/Audio/Mux, Arbeitsmenge in Sekunden bzw. Frames), `Stage`-Meldungen steuern `beginStage()`, gemessene Stufenzeiten kalibrieren pro Maschine/Material persistent (`QSettings` `progressCalibration/`) — kein Codec-Festwert. **Befunde:** `runEncodePass()` meldet jetzt alle 10 gesendeten Frames (vorheriger Stillstand behoben); Smart-Cut-Prozent ist arbeitsgewichtet (gemessene ms/Frame Kopie vs. Re-Encode, Fallback zuerst auf die im letzten Lauf gemessene Rate `videok/<codec>`, sonst 1:1) statt reiner Frame-Zählung; mplex-Zweig setzt `mLastCutError` weiterhin nie (TODO.md, außerhalb dieses Umbaus). Grundlage/Nachweis der Spec `2026-08-09-progress-eta-weighted`. | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7: Audio-Fortschritt geteilt, MKA meldet Fortschritt, Abbruch-Poll in `writeInterleaved`; Symbol-Grep) |
| [detection-and-search.md](detection-and-search.md) | Automatische Erkennung + Suche: **eine** Basisklasse `TTSearchTask` (N parallele Dekoder, geteilter Bildindex via `preBuiltFrameIndex`, `collectNextBatch`/`parallelMap`) trägt zwei Ergebnisformen — gerichtete Suche (`found(pos, wasAborted)`, hält beim ersten Treffer: Schwarzbild, Szenenwechsel, Logo) und Voll-Scan (`pointsDetected`, sammelt alle Wechsel: `TTAspectScanTask`). Dazu die drei Analysen ohne Bilddekodierung (MPEG-2-Sequenz-Header, Audio-Stille/acmod, AC3-Center+LFE-Burst-Scanner `TTAudioAnomalyScanTask` — automatischer Start nach dem Laden, abschaltbar) im selben Pool, und — ausserhalb der `TTSearchTask`-Familie — die Gleichbild-Suche `TTFrameSearchTask` (eigener Dekoder, eigenes Melde-/Index-Verhalten). Enthält den Klassifizierer `classifyAspectSample` (drei Werte inkl. `NoStatement` für Schwarzbilder) + `TTAspectHysteresis`, die Codec-Varianten-Matrix (MPEG-2 = 1 Dekoder, keine Header-Liste bei H.26x), die Kanten-Semantik von Abbruch/Aufräumen, und wie die drei Analysen sich seit `TTAnalysisLog` im Fortschrittsdialog-Detailbereich erklären. **Zwei Ur-Defekte belegt und behoben (2026-07-29):** Video-Worker hing an der Header-Liste, die `TTH26xVideoStream` nie anlegt; Worker-Wrapper ohne Bildindex gab leere Bilder zurück — Pillarbox-Erkennung hat für H.264/H.265 nie funktioniert. Mitkartiert: Abbruch-Use-after-free (`e247dbda`), der Stichprobenabstand-≤-Hysteresefenster-Vertrag (`aed01838`), ein Spät-Abbruch nach Taskende (`0af72ab1`) und fehlende Skip-Meldungen für nie gebaute Worker (`3b24be6a`). | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7: gemeinsame H.264-Slice-Suche im Frame-Indexer), Symbol-Grep) |
| [settings-state.md](settings-state.md) | Einstellungen als Zustandsmaschine: die drei Wertklassen in `TTSettings` (App-Defaults in `TTCut-ng.conf`, Working-Set für die Pipeline, Projektidentität/Ausgabename) und jeder Übergang dazwischen — Start (`load()` zweimal), Einstellungsdialog, Stream wird aktuell (`setEncoderCodec`, kehrt bei gleichem Codec früh um), Projekt laden (`currentAVItemChanged` VOR `deserializeSettings`, Projektwerte gewinnen nur weil synchron), Projekt speichern (13 Elemente, nie die App-Defaults), Cut-Dialog OK (Working-Set + Container-Sticky + ES-Name), Pipeline (schreibt `cutVideoName` mit dem Endnamen zurück), `closeProject` (`load()` als Verwerfen, dann Identität löschen), Headless. `load()` fällt bei fehlendem Schlüssel auf den Feldwert zurück. Befunde aus dem Lesen (alle drei mit `7bfd4a3b` erledigt): Muxer-Seite schrieb `setMkvCreateChapters` live und `openSettingsDialog` speicherte auch nach Abbrechen (Gate `test_settings_cancel`); vier Stellen synchronisierten Codec → Preset/CRF/Profil/Container (jetzt `syncWorkingSetToCodec`); `Muxer\OutputContainer` entfernt. | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7: `workingMpeg2Target` erreicht mplex, `workingMuxDeleteES` gilt auch für MKA; vier Quellen ergänzt; Symbol-Grep) |
| [stream-open-project-load.md](stream-open-project-load.md) | Stream öffnen und Projekt laden: die vier Eingänge, Nebendatei-Erkennung und `.info` (nur im Plain-Weg), die drei Öffnen-Tasks auf dem Pool (queued `finished`, der Pool erfährt das Ende zuerst), die Fertig-Handler in `TTAVData`, das Hauptfenster (`onAVItemChanged`, `onAVDataReloaded`, `onOpenProjectFileFinished`) und die nachgelagerten Auslöser (Audio-Sortierfenster, Anomalie-Tor mit drei Eingängen, Logo-Autoload, VDR-Marken, Defekt-Dialog). Verträge: `exit()` = Queue leer, wartet nicht auf queued Zustellungen; der modale Defekt-Dialog lässt `onThreadPoolExit` VOR dem Anhängen laufen (zweiter `initialAudioLoadDone`-Setter); `aborted()` vor `exit()` nur wenn der LETZTE Task abgebrochen war (Pool-Semantik bleibt; seit `cc85a6c8` hängt TTAVData bei Spuren nicht mehr daran: fehlgeschlagene Audio-/UT-Spur wird gemerkt, nach dem Laden gemeldet, Projekt gilt als geladen — Gate `test_open_track_failure`); `onOpenAVStreamsAborted` macht das LETZTE Item aktuell. Redundanz (Stand Batch G `026aa9fb`): aktuelles Item über `setCurrentAVItem`, Abort-Slots verbunden, Wrapper weg, Headless wartet auf die Ladekette (`waitForProjectLoad`); offen: `.info` zweimal gelesen. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [playback.md](playback.md) | Wiedergabe im „Aktueller Frame"-Widget: Codec-Weiche (MPEG-2 spielt die ES direkt mit `--audio-file`, H.26x über das Wiedergabe-MKV aus `TTPlaybackMuxTask` mit Fingerprint-Cache und `discard()`), die vier Schichten `TTMpvWrapper` → `ITTMpvBackend` → `TTMpvLibBackend` → `TTMpvRenderWidget`, drei Zeitdomänen (Stream-Index, mpv-Sekunden mit Display-PTS, zuletzt gerenderte Zeit) und ihre Umrechnungen, der Frame-Stack (StackAll nur während der Sitzung, KWin-Falle) und die Stop-Position (`lastRenderedTimePos`, Rest ~5 Frames). Ladevertrag gegen den Keyframe-Blitzer: `--pause=yes`, Entpausen bei `PLAYBACK_RESTART`, Stack-Wechsel beim zweiten gerenderten Frame, `paintGL` nur bei `MPV_RENDER_UPDATE_FRAME`. Ende aus zwei Quellen gegenseitig gesperrt; `END_FILE reason=STOP` ist ein Replace. Befunde: Live-Position bei MPEG-2-Feldbildern ohne die Korrektur, die Stop hat — behoben `3504eb3e` (`displayToStreamIndex`, Gate `test_extra_index_rank` Abschnitt 4, Sichttest offen); die Wiedergabeposition erreicht `checkCutPosition` — gewollt, Kommentar korrigiert `3504eb3e`; offen: Fingerprint ignoriert Delay und `.info`-Offset. Zweitnutzer `TTCutPreview` (keep-open) und `TTAudioRepairDialog` als Vertragsvergleich. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7: `TTPlaybackMuxParams::video`), Symbol-Grep) |
| [quick-jump.md](quick-jump.md) | Zeitsprung-Dialog: Kachelraster zum Positionssprung — `onQuickJump` (Dialog als **Stack-Objekt**, Destruktor läuft im GUI-Thread), `TTQuickJumpModel` (sammelt ausschliesslich I-Frame-Positionen, Intervall dünnt sie nur aus), der Worker mit eigenem Dekoder je Codec, und der Abbruchweg Dialog → Pool → `waitForDone` → laufender Decode. Kernbefunde: der Bildindex reist als `TTFrameIndexBundle` (Liste **plus** H.264-Strommetadaten) — ohne die Metadaten läuft `decodeFrame()` auf Feldpaar-Zielen bis zum Dateiende (gemessen 72 675 ms gegen 13 ms); zwei Indexdomänen (Modell sammelt Stream-Indexpositionen, Dekoder liest sie als Anzeigepositionen); graue Kachel = wartend, dunkelrote = fehlgeschlagen; teuer ist das **Schliessen** des Dialogs, nicht das Öffnen. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [stream-points.md](stream-points.md) | Landezonen (Stream-Points) im Hauptfenster, die Verbraucherseite: wie Marker in `TTStreamPointModel` kommen (Analyse-Dispatch mit bis zu vier Tasks und Zähler `mStreamPointWorkersRunning`, Ergebnis-Slots, Handmarker, Projekt-Restore mit DISABLED-Annotation, VDR-/Defekt-Import), was das Widget damit tut (Sprung mit `onGotoFrame(idx, 0)` statt Slider, Cut-In/Out, Löschen, Audio-Reparatur), Speichern (alle Zeilen + `TTLogoProjectData`) und das Logo-Profil (`TTLogoDetector`: manuelle ROI aus 10 I-Frames mit 70-%-Kantenregel, markad-PGM bündig in die Ecke ohne Prüfung, Projekt-Rundreise baut die manuelle ROI neu). Verträge: Marker-Index = Anzeige-Domäne; Modell sortiert, dedupliziert nicht; `clearAutoDetected` verschont Hand-/VDR-/Error-Marker; Auto-Anomalie-Scan mit drei Einstiegen und Latch. Die vier Lese-Befunde der Karte sind in Audit-Lauf 4 behoben (`f2198216` Worker-`deleteLater` in `startAnalysisTask` + ein markad-Lader, `3ce254f4` `tr("Marker (manual)")`, `0400b9a0` Zeitspalte mit Feldbild-Korrektur); Gates `test_analysis_task_lifetime`, `test_streampoint_model_time`. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [project-lifecycle.md](project-lifecycle.md) | Projekt-Lebenszyklus: zwei Identitäten (`mProjectDisplayName` für den Titel, `TTSettings::projectFileName` als Speicherziel — nur Speichern setzt es), Dirty-Flag aus genau acht `TTAVData`-Signalen (Schnitte, Videoliste, Legacy-Marker; Stream-Points, Reparaturen, Delays, Sprachen, Logo, `<Settings>` markieren nicht), New/Open/Save/Save as/Recent/Exit/closeEvent, das `.ttcut`-Format (`TTCutProjectData`: absolute Pfade, sichtbare Spurposition als `<Order>`, `<Marker>` als Legacy-Träger ohne Anzeige, `writeXml` ohne atomares Schreiben, Versionsprüfung nur `qDebug`), Kommandozeile und Headless-Modi. Lese-Befunde: Save nach Open fragt wie Save as; Save-as-Abbruch verliert das Speicherziel; Menü-Exit ruft `quit()` trotz Cancel; Projekt ohne öffnbares Video beendet die Ladekette nie. | `179d28d5` | fresh (2026-09-24, nur Quellen geändert (Audit-Lauf 7), Symbol-Grep) |
| [cut-edit-and-start.md](cut-edit-and-start.md) | Einen Schnitt bearbeiten und starten: von der Geste bis zum Task-Auftrag — die **zwei** Schnittlisten (je AV-Item eine, dazu die globale als Spiegel: Inhalt nach oben, Reihenfolge nach unten), der Bearbeiten-Zweig der Navigation, der `TTCutTreeView` als Zeilen-über-Position-Modell und Erzeuger der Auftragsliste (`cutListFromSelection`), der Startdialog und die drei Empfänger (`TTCutVideoTask`, `TTH26xCutTask`, `TTAudioOnlyCutTask`). Kernvertrag: **Eintrag 0 ist die Quelle** — Videostrom, Bildrate, Tonspuren und der Codec-Entscheid kommen in allen drei Zweigen aus `cutList->at(0)`, während die Liste selbst nur eine Folge von Bereichen ist. 20 Kantenzeilen, 8 Fallstricke, 6 Redundanz-Einträge; die Befunde von Audit-Lauf 6 gemessen und behoben; offen bleibt die Codec-Lücke des Projekt-Laders. | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7: `deleteTrackFiles` in den Audio-only-Parametern; Symbol-Grep) |
| [output-mux.md](output-mux.md) | Ausgabe: vom fertigen Elementarstrom zur Datei — `TTMkvMergeProvider` und seine sechs Aufrufer (Konfigurationsmatrix: Default-Duration, PAFF, Codec, Display-Reihenfolge, A/V-Versatz, Sprachen, Untertitel, Kapitel), das Innere von `mux()` (synthetische Video-Zeitstempel in Millisekunden-Zeitbasis, PAFF-Feldpaare, Nicht-VCL-Filter), die zwei Versatz-Stufen (Spur-Delay im Audio-Schnitt, `.info`-Versatz nur auf Tonpakete beim Mux), `muxAudioOnly` (MKA) und der mplex-Zweig als zweite MPEG-2-Senke; neun Lese-Hypothesen für Audit-Lauf 7 | `179d28d5` | fresh (2026-09-24, Audit-Lauf 7 eingearbeitet: H1–H9 erledigt und nach completed-work, Konfigurationsmatrix neu; Symbol-Grep, Richtung TD 2,39 vs LR 2,63) |

## Project module overview

Not yet generated. Run `code-map index` to build the coarse module/class
responsibility overview + top-level Mermaid diagram. Until then, the per-subsystem
detail maps above are the authoritative source; the existing
`memory/architecture_*.md` notes (TTSettings, TTSearchTask, cutVideoName split)
also cover specific areas.

## Module dependencies

Measured on the `#include "../<module>/…"` edges (2026-09-03, after the
TTFFmpegWrapper split and the module-edge cleanup): `common` has no outgoing
edge and is the foundation; `data` and `gui` sit on top and are cited by
nothing below them. The one remaining cycle is
**avstream → extern → mpeg2decoder → avstream** through a single edge on the
first leg: `ttmpeg2videostream.cpp` includes `tttranscode.h`
(`TTMpeg2VideoStream::cut()` re-encodes partial GOPs through
`TTTranscodeProvider`; the header stopped re-exporting it on 2026-09-04), `TTTranscodeProvider` decodes through `TTMpeg2Decoder`,
and the decoder reads the header and index lists from `avstream`. Removed on
2026-09-04 (stream-ownership B1): `TTH26xVideoStream` no longer holds a
`TTFFmpegWrapper` — it probes the file with `ttProbeVideo()` and owns the
`TTFrameIndexBundle`, whose display-order map `TTFrameIndexer` now builds; the
indexer, the bundle types and `ttavutil` live in `avstream/`. Dissolving the
last edge (B2) is a separate project (spec
`docs/superpowers/specs/2026-09-03-stream-ownership-design.md`, "Deferred").
Removed on 2026-09-03: `common → avstream`
(`ttcut.h` re-exported `ttcommon.h`) and `extern → data` (the two DTO headers
`ttaudiorepairitem.h`, `ttmuxlistdata.h` now live next to their consumers in
`extern/`).

## Project-wide redundancy patterns

Collected from detail maps as they are created. From `frame-order.md`:
- Three ad-hoc decode-order↔display-time conversions (`onPlayVideo`,
  `onPlaybackFinished`, `onPlaybackPositionChanged`) — candidate for a shared
  `decodeIndexToDisplaySeconds` / `displaySecondsToDecodeIndex` helper.

From `settings-state.md`:
- ~~Four sites derive the Working-Set from the per-codec App-Defaults (`TTSettings::load`, `TTSettings::setEncoderCodec`, `TTCutAVCutDlg::onResetDefaults`, `TTCutSettingsEncoder::loadCodecSettings`) and three map a stream type to the codec index (`onAudioVideoCut`, `onAVItemChanged`, `runAutoCutMode`).~~
  **Consolidated (`7bfd4a3b`)** onto `TTSettings::syncWorkingSetToCodec`/`encoderDefaultsFor`/`resetWorkingMuxSet` and `TTAVTypes::encoderCodecFor`; `OutputContainer` removed.

From `stream-open-project-load.md`:
- ~~Five sites assign or emit the current AV item (`onOpenVideoFinished`, `onOpenAVStreamsAborted`, `onChangeCurrentAVItem`, `onReadProjectFileFinished`, `onReadProjectFileAborted`).~~
  **Consolidated (`cc85a6c8`)** onto `TTAVData::setCurrentAVItem`; the dead abort slots are connected and report the failed track (gate `test_open_track_failure`); the headless 2-s sleep is `waitForProjectLoad` (`026aa9fb`). Still open: the `.info` file is loaded twice per open.

From `playback.md`:
- ~~The MPEG-2 field-picture extras-before-index search is written twice in `TTCurrentFrame` (`onPlayVideo`, `onPlaybackFinished`) and missing in `onPlaybackPositionChanged`; the three end-of-playback paths in `TTMpvWrapper` each set the same flags and emit `playerFinished`.~~
  **Consolidated (`3504eb3e`)** onto `ttCountBelow`/`TTMpeg2VideoStream::extrasBefore`/`streamIndexForDisplayIndex` (live position corrected too) and `TTMpvWrapper::finishPlayback()`.

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
- ~~The three directed searches (`_blackframe`, `_logo`, `_scenechange`) share an
  identical `operation()` body.~~ Consolidated (code audit 2026-09-03) onto
  `TTSearchTask::runDirectedSearch`; equivalence gate `tools/diag/test_directed_search`.
- ~~The same 10%-border mask + `step = 2` sampling rule exists three times:
  `TTSearchTask::isFrameBlackAt`, `TTSearchTask::buildHistogramAt` (both MPEG-2
  fallbacks of what `TTFFmpegWrapper` does for H.26x) and `centreMeanLuma` in
  `ttaspectdetect.cpp`.~~ **Consolidated (`5a7601a0`)** onto `TTCentreBand`
  (avstream/ttlumasample.h) for the two `TTSearchTask` fallbacks and the wrapper;
  `centreMeanLuma` was a misread — it samples a caller-chosen rectangle and shares
  only the step, so it stays as it is.
- ~~The three directed searches connect only `finished → deleteLater`, not
  `aborted`.~~ Fixed (`f8fe7dd6`), together with the root hazard underneath it:
  `TTThreadTask::run()` emitted its terminal signal before the virtual
  `cleanUp()`, which every owner turns into a use-after-free by wiring that
  signal to `deleteLater` (reproduced under ASAN).

From `progress-reporting.md`:
- The audio progress lambda pair for `cutAudioTracks` exists three times
  (`doMpeg2Cut`, `doH264Cut`, `doAudioOnlyCut`) — candidate for one helper.
  Still open.
- ~~Progress-bar creation + cancel wiring duplicated in the `Init` and `Start`
  branches of `TTCutMainWindow::onStatusReport`.~~ **Consolidated (`026aa9fb`)** onto
  `TTCutMainWindow::ensureProgressBar()`.
- ~~Three "how long" sources with different semantics (direct timer, pool
  `overallTime()`, task `elapsedTime()`).~~ **Resolved** (spec
  `2026-08-09-progress-eta-weighted`, commits `1fd8de26`..`fb7cbb76`): all
  three removed outright; `TTProgressEstimator`'s injected clock plus the
  dialog's debug-only wall clock are the sole remaining time sources, with
  non-overlapping purposes.

**Cross-cutting:** `CLAUDE.md`'s "PAFF Smart Cut implementation notes" attribute
SPS-Unification, MMCO neutralization and `realStartAU` filtering to PAFF. All
three attributions are too narrow or wrong — see `smart-cut.md` pitfalls.

From `stream-points.md`:
- The four consolidation candidates of the map (markad loader, analysis wrapper,
  detector result slots, analysis-task start) were merged in audit run 4
  (`f2198216`: `loadMarkadLogoProfile`, `createAnalysisWrapper`, `onPointsDetected`,
  `startAnalysisTask`). Kept separate on purpose: the three auto-scan entry points
  (three measured orderings, one gate inside `maybeStartAutoAnomalyScan`).

From `project-lifecycle.md`:
- Both redundancy candidates were merged in audit run 5 (`ea7f763a`:
  `askProjectFileName()` for Save and Save as, `recentFilesChanged` connected to the
  menu). Its four read findings are settled: the cancelled Save as no longer drops
  the save target, a project that starts no video ends as aborted, Exit leaves the
  decision to the close handler, and Save after Open still asks by design.

From `output-mux.md`:
- The five muxer options that four callers set by hand, and the frame
  duration string built at four sites, are one `TTMkvVideoOptions` value
  since audit run 7; the ES deletion after a mux goes through
  `ttRemoveElementaryStreams`/`ttRemoveFiles` everywhere. Open: the output
  open/finish of `mux()`/`muxAudioOnly()` and the preview audio-cut
  skeleton.
