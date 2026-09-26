# Qualitäts-Fahrplan

Wie Codequalität und Fehlerzahl über die nächsten Iterationen verbessert
werden. Zeigt nur den offenen Rest und den Stand; Erledigtes wandert mit
seinen Belegen nach [completed-work.md](completed-work.md).

## Befund (2026-09-11)

Die Fehler vom 2026-09-10/11 waren fast alle Vertragsfehler, keine
Codierfehler: welche Kopie eines Werts gilt (Container in `TTSettings::load()`
gegen den Setter), wer einen Abbruch weiterreicht (fehlende Cancel-Tokens in
zwei Such-Tasks), ein Fix, der nur eine von zwei gleichen Schleifen erreicht
hat. Der Code-Audit findet Duplikate und toten Code, aber keine Verträge; die
Code-Karten finden Verträge, aber nur dort, wo eine existiert — und die
bestehende Karte zur Suche trug die Cancel-Token-Kante nicht. Gefunden wurden
die Fehler durch Messung: fünf von sechs bekamen erst ein Gate, dann den Fix.

## Die drei Bausteine, in dieser Reihenfolge

### 1. Gate-Läufer

Rund 15 schnelle Harnesses existieren, nichts führt sie zusammen aus;
`test_leadingclass` war wochenlang rot, ohne dass es jemand sah.

- `tools/diag/run-gates.sh`: baut und startet alle Harnesses, die ohne großes
  Material auskommen (Tux-Fixtures, synthetische Dateien, keine Eingabe), und
  druckt eine PASS/FAIL-Tabelle mit Laufzeit je Gate. Bestand war nicht 15,
  sondern 89 Harnesses plus 13 Gate-Skripte; 73 Gates sind drin, der Rest
  steht mit Grund im Skriptkopf (Display nötig, Vergleichs-Baseline,
  Korpusmaterial, nur Dump ohne Urteil).
- Aufruf vor jedem Merge auf `master` und als Schritt im Release-Skill.
- **Fertig**, wenn ein Lauf grün ist und der Release-Skill ihn ruft.

### 2. Karten für die unkartierten Hauptfunktionen

Die neun Karten unter [code-map/](code-map/) decken den Schnitt selbst ab;
die Lücken liegen im Drumherum, wo sich die Fehler häufen. Erstellt mit dem
`code-map`-Skill, Kanten im Code belegt, Stempel wie bei den bestehenden.

- **Einstellungen als Zustandsmaschine**: persistente Werte, Working-Set,
  Überschreibung durch die Projektdatei, wer wann synchronisiert
  (`common/ttsettings.*`, `data/ttcutprojectdata.*`, Dialoge).
- **Stream öffnen und Projekt laden**: `TTAVData`,
  `onReadProjectFileFinished`, `onAVItemChanged`, die Reihenfolge-Verträge,
  die heute nur in Kommentaren stehen; dazu `TTCutMainWindow` (2689 Zeilen,
  der offene Audit-Rest) — erst kartieren, dann zerlegen.
- **Wiedergabe**: `TTCurrentFrame`, `TTMpvWrapper`, libmpv-Backend,
  Wiedergabe-MKV (seit 2026-09-11 asynchron), Stop-Position.
- Bestehende Karte `detection-and-search.md` um die Cancel-Token-Kante
  ergänzen (`TTSearchTask`, `TTFrameSearchTask` → `TTFFmpegWrapper`).
- **Fertig** je Karte nach dem Kriterium des Skills.

### 3. Audit-Lauf 3 nach Karte statt Round-Robin

Der Rückstand liegt bei 1627 nie beurteilten Kandidaten; blind 40 je Modul zu
nehmen liefert abnehmenden Ertrag. Mit den Karten aus Schritt 2 werden die
Kandidaten eines Teilsystems mit dessen Verträgen in der Hand beurteilt.

- `code-audit`-Skill, Kandidaten auf die drei kartierten Teilsysteme
  eingeschränkt; Urteile im Store wie bisher.
- **Fertig**, wenn die Kandidaten der drei Teilsysteme beurteilt sind.
- Danach im Wechsel: Karte, Audit des kartierten Bereichs, Karte.

## Regeln

- Karte vor Audit desselben Bereichs.
- Jeder Fix bekommt ein Gate; das Gate kommt in den Gate-Läufer.
- Kein breiter Audit-Lauf ohne Karte des Bereichs.
- Jeder Schritt startet mit seinem eigenen Brainstorming (Design-Abnahme
  vor Code).
- Offen darf nicht offen bleiben (seit Lauf 8, 2026-09-25): Jeder Audit-Lauf
  entscheidet zuerst über alle offenen Punkte (`consolidate` ohne Umbau) in
  seinem Umfang, bevor neue Kandidaten drankommen. Jeder Punkt kommt in genau
  eine Schublade: in diesem Lauf umbauen, eigenes Projekt (TODO-Eintrag,
  im Urteils-Speicher `documented` mit Verweis) oder `deliberate` mit
  Begründung.
- Rückstands-Schritt: Nach jeder zweiten Runde aus Karte und Audit folgt ein
  Schritt, der nur offene Punkte abarbeitet, sortiert nach Reichweite und
  Risiko, ohne neuen Scan.
- Jeder Laufbericht nennt die Zahl der offenen Punkte und ihren Trend
  gegenüber dem Vorlauf. Ziel: Die Zahl wächst nicht.

## Stand

| Schritt | Status | Beleg |
|---|---|---|
| 1 Gate-Läufer | fertig 2026-09-11 | `tools/diag/run-gates.sh`, Lauf `20260911-173329`: 73 PASS, 0 FAIL, 0 SKIP, 5,5 min Gate-Zeit; Release-Skill Step 5 ruft ihn. Erster Fund: `mLogFile.log` statt `logfile.log` seit `ab3fae4d` (alte Binary bestand, frische nicht) |
| 2a Karte Einstellungen | fertig 2026-09-11 | `docs/code-map/settings-state.md`: drei Wertklassen, 21 Kanten, 7 Pitfalls, 5 Redundanz-Kandidaten (Codec-Sync an vier Stellen, Stream-Typ → Codec an drei) |
| 2b Karte Stream öffnen / Projekt laden | fertig 2026-09-11 | `docs/code-map/stream-open-project-load.md`: zwei Wege, ein Task-Pfad; 14 Kantenzeilen, 8 Pitfalls, 6 Redundanz-Kandidaten; `TTCutMainWindow` kartiert, nicht zerlegt |
| 2c Karte Wiedergabe | fertig 2026-09-11 | `docs/code-map/playback.md`: drei Zeitdomänen, vier Schichten, 16 Kantenzeilen, 8 Pitfalls, 5 Redundanz-Kandidaten; Zweitnutzer als Vertragsvergleich |
| 2d `detection-and-search.md` Cancel-Token-Kante | fertig 2026-09-11 | Update: Knoten `WRAP`/`FSEARCH`, zwei Kantenzeilen, Pollpunkt-Fallstrick, drei Cancel-Harnesses; Stempel auf HEAD nach Symbol-Grep |
| 3 Audit-Lauf 3 nach Karte | fertig 2026-09-13, gemergt `76bbae80` | 545 Urteile auf den 68 Quelldateien der vier Karten (277 consolidate, 244 deliberate, 24 documented), sechs Vertragsbefunde belegt und entschieden; acht Batches A–H (`1441c142` … `8788b25f`), je ein Commit mit Gate: `run-gates.sh` 73 → 78 PASS (fünf neue Gates), `gate_cut_identity.sh` fünf Fixtures identisch, Refactor-Suiten ref == cand, 21 Screenshots byteidentisch bis auf den gewollten Muxer-Text; Store `docs/code-audit/build-verdicts-2026-09-12.py` (203 consolidate erledigt, 74 offen); Details `docs/completed-work.md`. Sichttest Wiedergabe: MPEG-2-Versatz beim Play/Stop per A/B gegen v0.83.0 als vorbestehend bestätigt |
| 4 Karte Landezonen im Hauptfenster | fertig 2026-09-13 | `docs/code-map/stream-points.md`: Verbraucherseite der Stream-Points (Modell, Widget, Analyse-Dispatch, Projekt-Rundreise, Logo-Profil); 19 Kantenzeilen, 10 Pitfalls, 5 Redundanz-Kandidaten (davon 4 consolidate) |
| 5 Audit-Lauf 4 nach Karte | fertig 2026-09-13, gemergt `c887becd` | 55 Urteile auf den 26 Quelldateien von `stream-points.md` (15 consolidate — alle erledigt —, 31 deliberate, 9 documented), 12 Layer-3-Rulings, die vier Lese-Befunde der Karte im Code belegt: Worker-Leck (LSan blind, Harness `test_analysis_task_lifetime`), deutsche Marker-Beschriftung, Zeitspalte ohne Feldbild-Korrektur (Entscheid User → Batch E), markad-Lader ohne Prüfung. Fünf Batches A–E (`3ce254f4` … `0400b9a0`), je ein Commit mit Gate: `run-gates.sh` 78 → 80 PASS, `gate_cut_identity.sh` fünf Fixtures identisch, Refactor-Suiten ref4 == Batch, 21 Screenshots byteidentisch bis auf die Plattenplatz-Ziffern. Rescan nach dem Umbau: 12 neu im Scope (Verdrahtungs-Idiome neu gefingerprintet, das `applyPending`-Paar, ein cppcheck-Fund — behoben), Store `docs/code-audit/build-verdicts-2026-09-13.py`; zweiter Rescan 1 neu (setTabData-Idiom dreier Einstellungsseiten, deliberate), dritter Rescan 0 neu im Scope — konvergiert. Fünf Karten aktualisiert und auf HEAD gestempelt. Nächster Schritt: Karte Projekt-Lebenszyklus |
| 6 Karte Projekt-Lebenszyklus | fertig 2026-09-13 | `docs/code-map/project-lifecycle.md`: Identitäten, Dirty-Flag, Menüaktionen, `.ttcut`-Format, Kommandozeile, Headless; 18 Kantenzeilen, 11 Pitfalls, 4 Redundanz-Einträge (2 consolidate). Vier Lese-Befunde für Audit-Lauf 5 (Save nach Open, Save-as-Abbruch, Menü-Exit trotz Cancel, Ladekette ohne Video). Nächster Schritt: Audit-Lauf 5 auf den Quellen dieser Karte |
| 7 Audit-Lauf 5 nach Karte | fertig 2026-09-13, gemergt `40ee2563` | 26 nie beurteilte Kandidaten auf den 12 Quelldateien von `project-lifecycle.md` (2 consolidate, 18 deliberate, 6 documented), drei Sonnet-Klassifizierer, zwei Karten-Redundanzen und vier Vertragsbefunde bestätigt. Vier Batches A–D (`fc4dd533` … `5e47997b`): ungenutzter Getter; `startDetectorTask` + `askProjectFileName` (behebt den Verlust des Speicherziels bei Abbruch) + `recentFilesChanged` ans Menü; Ladekette ohne gestarteten Task endet als Abbruch (dabei fand der neue Harness einen uninitialisierten Zeiger); Beenden überlässt dem Schliess-Ereignis die Entscheidung. `run-gates.sh` 80 → 84 PASS (vier neue Gates), Cut-Identität 5/5, Refactor-Suiten ref5, Screenshots unverändert. Gemessene Korrektur am Befund C3: `quit()` führt unter Qt 6.10 selbst ein `closeEvent` aus — die alte Fassung fragte bei Cancel zweimal, statt zu beenden; der Datenverlust lag im Speichern-Zweig. Rescans: 16 → 0 neu im Scope. Sechs Karten aktualisiert und gestempelt. Die zwei offenen consolidate des Rescans (die drei Sektionskopf-Wächter in `TTCutProjectData`) sind am 2026-09-14 mit `3c888bf1` geschlossen — `parseSectionHeader` für alle drei Sektionen, Gate `test_project_load_rejected` um die `<Audio>`/`<Subtitle>`-Wächter erweitert, `run-gates.sh` weiter 84 PASS. Nächster Schritt: neue Karte (Regel „Karte, Audit, Karte") |
| 8 Karte Schnitt bearbeiten/starten | fertig 2026-09-14 | `docs/code-map/cut-edit-and-start.md`: von der Geste bis zum Task-Auftrag — zwei Schnittlisten (Inhalt nach oben, Reihenfolge nach unten), Bearbeiten-Zweig der Navigation, `cutListFromSelection` als Auftragsbauer, Startdialog, drei Empfänger. Diagrammrichtung gemessen (TD 1,37 vs LR 2,88), 49 Symbole gegrept. 19 Kantenzeilen, 9 Fallstricke, 5 Redundanz-Einträge (2 consolidate). Fünf Lese-Befunde für Audit-Lauf 6: `checkCut` prüft nichts (einzige Prüfung auskommentiert), `TTCutList::remove` ohne `indexOf`-Wache, die Auftragsliste aus `cutListFromSelection` hat keinen Eigentümer, `editCutData` leckt beim zweiten Bearbeiten, Codec aus dem aktuellen Item gegen Weiche aus Eintrag 0. Nächster Schritt: Audit-Lauf 6 auf den Quellen dieser Karte |
| 9 Audit-Lauf 6 nach Karte | fertig 2026-09-14 | 27 nie beurteilte Kandidaten auf den 21 Quelldateien von `cut-edit-and-start.md` (9 consolidate, 18 deliberate), zwei Sonnet-Klassifizierer, dazu die fünf Lese-Befunde der Karte gemessen: 1, 2, 4 bestätigt, 5 größtenteils widerlegt (`canCutWith` prüft den Codec), 3 nur für den GUI-Weg. Sechs Batches A–F, beim Squash in `91e088ff` zusammengefasst: `checkCut` prüft wirklich (Batch A), `canCutWith` vergleicht endlich die zwei Videos statt `video2` mit sich selbst (Batch B), Eigentum für Bearbeiten-Kopie und Auftragsliste (Batch C), drei tote Symbole weg (Batch D), gemeinsamer Zeilen-Lookup plus `override`/`explicit` (Batch E), Wache in `TTCutList::remove` (Batch F). `run-gates.sh` 84 → 86 PASS, 0 FAIL; zwei neue Gates `cut_range_check`, `cut_job_ownership`, beide mit Negativprobe. Screenshot-Lauf nachgeholt, sobald die NAS wieder da war: 19 von 21 Bildern byteidentisch, die zwei Schnitt-Dialog-Bilder nur in der Plattenplatz-Ziffer, die auch zwischen zwei Läufen desselben Baums wechselt. Rescan: 23 Meldungen, alle Fingerprint-Artefakte verschobener Zeilen. Nachgereicht auf Nachfrage des Users: `updateCutEntry` prüft ebenfalls (Batch A2, `isValidCut` für beide Schreibwege) und die drei Gesten halten an der jeweils anderen Grenze an (Batch A3). Codec-Lücke des Projekt-Laders (umging `canCutWith`) geschlossen 2026-09-23 mit `22479ed8`, Vorschau-Klone als Batch G zurückgestellt (erledigt 2026-09-21), Sichttest aller drei Anschläge vom User bestätigt (2026-09-14); dabei fiel auf, dass zwölf Texte seit v0.83.0 unterübersetzt waren — nachgeholt |
| 10 Karte Ausgabe (Mux) | fertig 2026-09-24 | `docs/code-map/output-mux.md`: `TTMkvMergeProvider` mit seinen sechs Aufrufern als Konfigurationsmatrix, das Innere von `mux()`, die zwei Versatz-Stufen, `muxAudioOnly` und der mplex-Zweig. Diagrammrichtung gemessen (TD 2,39 vs LR 2,66), Symbole gegrept; Zeitbasis des Matroska-Muxers und EOS-Paketierung der libav-Parser im ffmpeg-8.1.2-Quelltext nachgelesen. 17 Kantenzeilen für 25 Diagrammkanten, 5 Redundanz-Einträge (4 consolidate), neun Lese-Hypothesen H1–H9 für Audit-Lauf 7, darunter Millisekunden-Rundung bei 29,97/23,976 fps (H1) und die nie an mplex übergebene MPG-Zielwahl (H2). Nächster Schritt: Audit-Lauf 7 auf den Quellen dieser Karte |
| 11 Audit-Lauf 7 nach Karte | fertig 2026-09-24 | 130 nie beurteilte Kandidaten auf den 21 Quelldateien von `output-mux.md` (89 consolidate, 35 deliberate, 6 documented), zwei Sonnet-Klassifizierer; die neun Lese-Hypothesen H1–H9 gemessen, sieben bestätigt, zwei aus dem Code eindeutig. Sechs verhaltensneutrale Batches (B1–B6) und acht Fixes (A1–A8), jeder Fix mit Gate: `run-gates.sh` 101 → 109 PASS, 0 FAIL. Schnitt-Identität nach jedem Batch gleich, Harness-Suite ref7 == a8. 70 consolidate umgesetzt, 19 offen (8 davon Batch B7, per User zurückgestellt). Details `docs/completed-work.md`. Nächster Schritt: neue Karte (Regel „Karte, Audit, Karte“) |
| 12 Karte Spurverwaltung | fertig 2026-09-25 | `docs/code-map/track-management.md`: Ton- und Untertitelspuren von der Aufnahme in die Liste bis zu den Lesern — drei Eingänge, geparkte Werte je `(Item, Order)`, Sortierung nur im ersten Ladefenster, zwei TreeViews, Reparatur-Umnummerierung, Position 0 als Leitspur. 25 Kantenzeilen, 7 Pitfalls, 5 Redundanz-Einträge (3 consolidate, 1 bewusst getrennt, 1 an den Dead-Code-Audit). Diagrammrichtung gemessen (TD 2,17 vs LR 3,14), Symbole gegrept. Sieben Lese-Hypothesen H1–H7 für Audit-Lauf 8 |
| 13 Audit-Lauf 8 nach Karte | fertig 2026-09-25 | 62 nie beurteilte Kandidaten auf den 28 Quelldateien von `track-management.md` (5 consolidate umgebaut, 21 documented → TODO P6, 36 deliberate), zwei Sonnet-Klassifizierer; die sieben Lese-Hypothesen H1–H7 zur Laufzeit gemessen, sechs bestätigt, eine strukturell, alle behoben (drei neue Gates `track_persist`, `track_language`, `track_gui`; run-gates 113 → 116 PASS). Erster Lauf unter der Regel „offen darf nicht offen bleiben“: die 42 offenen Punkte im Umfang einsortiert (3 umgebaut, 24 → Projekte P1–P5, 15 deliberate). Offen im Umfang 42 → 0, projektweit 87 → 45; Nachscan konvergiert (0 neu). Alle 17 Karten auf `5943fe7a` gestempelt |
| 14 Karte Schnitt-Vorschau | fertig 2026-09-25 | `docs/code-map/cut-preview.md`: vom Vorschau-Befehl bis zum geschlossenen Dialog — Vorschau-Liste, Clip-Bau je Codec, Drift-Spalte, Dialog mit Hinweisen, Kantenverschiebung und Einzelclip-Neubau, Temp-Dateien. 25 Kantenzeilen, 7 Pitfalls, 3 Redundanz-Einträge (P3, Clip-Index-Rechnung, Dateinamen). Diagrammrichtung gemessen (TD 0,57 vs LR 10,41), Symbole gegrept. Dabei `audio-cut-timing.md` berichtigt (Drift-Spalte hängt an einem Signal, nur im Vorschau-Fenster). Sieben Lese-Hypothesen H1–H7 für Audit-Lauf 9 |
| 15 Audit-Lauf 9 nach Karte | fertig 2026-09-25 | 9 nie beurteilte Kandidaten auf den 14 Quelldateien von `cut-preview.md` (1 consolidate, 8 deliberate), in der Hauptsitzung beurteilt (zu klein für Klassifizierer); die 4 offenen Punkte im Umfang zuerst einsortiert (3 umgebaut, 1 → P3). Lese-Hypothesen gemessen: H1, H2, H3, H4 bestätigt, dazu beim Messen H8 (ungefangene Ausnahme beim Neubau → Absturz) und H9 (Neubau ohne Ton ersetzt den Clip nicht); alle behoben, H5 nicht messbar (→ P3), H6/H7 Code-Fakten. User-Entscheid: Drift-Spalte über alle Schnitte. Gates `preview_clip_index`, `preview_drift_rows` (run-gates 116 → 118). Offen im Umfang 4 → 0, projektweit 45 → 41; Nachscan konvergiert. 17 Karten auf `e47e1d80` gestempelt. Nächster Schritt laut Regel: Rückstands-Schritt |
| 16 Rückstands-Schritt 1 | fertig 2026-09-25 | Die 41 projektweit offenen Punkte nach Lauf 9 einsortiert (User-Entscheid): 10 umgebaut in sechs Batches R1–R6 (Muxer-Eingänge und Ausgabe-Öffnen, `TTAnalysisLog::summary`, Encoder-Qualitätsfelder + `.ui`-Beförderung als `QWidget`, MPEG-2-Suchbild `mpeg2FrameAt`, `openFile`-Fehlerpfad, Seitenverhältnis-Zweige, Kommentarstil), 10 → Projekte P2, P8 (Dekodierpfade `TTFFmpegWrapper`), P9 (Audio-Dekoder öffnen), 21 `deliberate`. Beim Lesen umentschieden: R2-Phasenklone (#5, #6, #10, #11) unterscheiden sich je Phase → `deliberate`; R4 (#21) war seit `97551783` erledigt, der Rest gehört zu P9; #19 nach R1 nur noch der Abschluss-Tail. Verhaltensgleich belegt: Mux-Gates, Stream-Point-Gates, `test_directed_search` vorher/nachher identisch (zwei MPEG-2-Fixtures). `run-gates.sh` 118 PASS. Offen projektweit 41 → 0; Nachscan (`--all`) konvergiert, 3 neue Formklone `deliberate`. Store `docs/code-audit/build-verdicts-2026-09-25-backlog1.py`. 9 Karten auf `64481da8` gestempelt (Symbol-Grep). Nächster Schritt: neue Karte (Regel „Karte, Audit, Karte“) |
| 17 Karte Tonanomalie-Reparatur | fertig 2026-09-25 | `docs/code-map/audio-repair.md`: vom AC3-Anomalie-Scan über Marker, Kontextmenü und Reparatur-Dialog (Probehören) zur Reparaturliste am AV-Item, zu `<Repair>` in der Projektdatei (Lade-Prüfung) und in jeden Tonschnitt (Endschnitt, Nur-Ton, Vorschau-Clips). 18 Kantenzeilen (19 Kanten), 12 Verträge/Fallen, 4 Redundanz-Einträge (1 consolidate `countExtrasBefore` ×3, 1 → P9, 2 bewusst getrennt). Diagrammrichtung gemessen (TD 0,86 vs LR 7,16), Symbole gegrept. Sieben Lese-Hypothesen H1–H7 für Audit-Lauf 10 |
| 18 Abgleich alter Umbau-Urteile | fertig 2026-09-25 | 168 `consolidate` ohne „done“, deren Fingerabdruck im Code nicht mehr vorkam (127 aus Lauf 1, dessen Urteils-Skript Umgebautes nie markierte). Drei Sonnet-Subagenten prüften jede Begründung am heutigen Code, die Hauptsitzung jedes „lebt weiter“ und acht Stichproben „erledigt“ (zwei Subagenten-Urteile korrigiert). Ergebnis: 158 waren erledigt, 3 umgebaut (Gate-Projektdateien über `ttcut_project_xml` in `tools/diag/ttcut-project.sh`, acht Stellen, XML-Gleichheit bzw. fünf Gates PASS; `TTFrameSearchTask::openMpeg2DecoderFor`, Suche vorher/nachher gleich), 3 → Projekte (P1, P2, P8 erweitert zu „TTFFmpegWrapper zerlegen“), 4 deliberate. Nachscan (`--all`) konvergiert, 7 neue Formklone deliberate. Store `docs/code-audit/build-verdicts-2026-09-25-reconcile.py`; kein `consolidate` ohne „done“ mehr. Danach die 8 alten `unsure`-Zeilen (Lauf 1, Fingerabdruck ebenfalls verschwunden) in der Hauptsitzung: 3 erledigt, 2 documented, 3 Scanner-Fehlzuordnungen deliberate (`build-verdicts-2026-09-25-reconcile-unsure.py`); kein `unsure` mehr im Speicher. Falle im code-audit-Skill vermerkt. Nächster Schritt: Audit-Lauf 10 |
| 19 Audit-Lauf 10 nach Karte | fertig 2026-09-26 | 51 nie beurteilte Kandidaten auf den 16 Quelldateien von `audio-repair.md` (im Umfang nichts offen), in der Hauptsitzung beurteilt: 17 umgebaut in C1–C3 (`TTAudioCutter`-Paket-Helfer `static`, gemeinsamer Schreibschritt, `buildRepairTable` mit einem `qScopeGuard`, 313 → 258 Zeilen; beide byteidentisch belegt), 4 → P9, 28 deliberate, 2 Dateien in die 4er-Einrückungs-Ausnahme von `docs/conventions.md`. Lese-Hypothesen gemessen: H1–H4 bestätigt, dazu beim Messen H8 (Reparatur kodierte mit der Bitrate des ersten Rahmens); alle behoben, jeder Fix mit einem Gate, das auf dem alten Stand rot ist. User-Entscheide: Reparatur darf über den Fensterrand ragen (nur verschiedene Ziel-acmods scheitern); Marker löschen fragt und nimmt die Reparatur mit. H5 nicht gemessen, H6 nicht aufgetreten, H7 widerlegt. Gates neu `repair_window_edge`, `marker_delete_repair` (run-gates 119 → 121). Nachscan: 15 der 17 Umbau-Meldungen weg, `buildRepairTable`-Größe/Komplexität (95 → 83) und 3 neue Formklone deliberate; konvergiert, offen im Umfang und projektweit 0. Store `docs/code-audit/build-verdicts-2026-09-26-run10.py`. Nächster Schritt laut Regel: neue Karte (User wählte am 2026-09-26 K1 Ton-ES einlesen, gemessen gegen fünf weitere unkartierte Cluster) |
| 20 Karte Ton-ES einlesen | fertig 2026-09-26 | `docs/code-map/audio-es-input.md`: Typerkennung (`ttavtypes`), MPEG-Audio- und AC3-Kopfleser, `TTAudioHeaderList` und die sieben Leser der Liste (Tonliste, Verträglichkeitsprüfung, Längenwarnung, `planAudioCut`, Reparatur-Sekunden, acmod-Fenster, Tonwechsel-Marker) plus die eigene Rahmengrößen-Suche der Projekt-Ladeprüfung. 15 Kantenzeilen (17 Kanten), 7 Verträge/Fallen, 4 Redundanz-Einträge; Kern: Kopf 0 spricht für die Datei, Rahmennummer = Kopf-Index = Zeit / Raster. Richtung gemessen TD 1,99 vs LR 3,35, 104 Symbole gegrept. Lese-Hypothesen H1–H6 für Audit-Lauf 11 (44,1-kHz-Raster, `.eac3`, MPEG-Abbruch bei kaputtem Kopf, Tonwechsel-Marker ohne Extra-Bilder, MPEG-2-Layer-I/II-Länge, Verträglichkeit nur am ersten Rahmen). Nächster Schritt: Audit-Lauf 11 |
| 21 Audit-Lauf 11 nach Karte | fertig 2026-09-26 | 118 nie beurteilte Kandidaten auf den 27 Quelldateien von `audio-es-input.md` (im Umfang nichts offen), fast alles cppcheck-Stil im alten TTCut-Code, in der Hauptsitzung beurteilt: 97 umgebaut in C1–C4 (tote Felder, mechanisch, zwei Klone in die Basisklassen, statischer Kopf-Parser), 19 deliberate, 2 → neues P10; jeder Batch mit gleicher Ausgabe aller Kopfwerte auf neun Dateien. Lese-Hypothesen gemessen: H1 bestätigt mit anderem Mechanismus (44,1 kHz: ~20 ms pro Segment, nicht proportional zur Länge), H2 breiter (auch `.aac`/`.m4a`/`.dts`, automatische Suche), H3, H4 (auch 48,8 s bei 44,1-kHz-AC3, auch Stille-Marker), H5 mit anderer Folge (Ablehnung statt falscher Liste) — alle behoben nach User-Entscheid; H6 belassen. Dabei `abs_frame_time` `float` → `double`. Gate neu `audio_es_input` (15 Prüfungen, alt rot). Neuscan: 7 Formklone (2 → P10, 5 deliberate), konvergiert, offen im Umfang und projektweit 0. Store `docs/code-audit/build-verdicts-2026-09-26-run11.py`. Nach der zweiten Runde seit Schritt 16 wäre laut Regel der Rückstands-Schritt dran; mit 0 offenen Punkten hat er nichts abzuarbeiten, daher nächster Schritt: neue Karte |
