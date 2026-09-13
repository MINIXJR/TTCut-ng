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
| 7 Audit-Lauf 5 nach Karte | fertig 2026-09-13, gemergt `40ee2563` | 26 nie beurteilte Kandidaten auf den 12 Quelldateien von `project-lifecycle.md` (2 consolidate, 18 deliberate, 6 documented), drei Sonnet-Klassifizierer, zwei Karten-Redundanzen und vier Vertragsbefunde bestätigt. Vier Batches A–D (`fc4dd533` … `5e47997b`): ungenutzter Getter; `startDetectorTask` + `askProjectFileName` (behebt den Verlust des Speicherziels bei Abbruch) + `recentFilesChanged` ans Menü; Ladekette ohne gestarteten Task endet als Abbruch (dabei fand der neue Harness einen uninitialisierten Zeiger); Beenden überlässt dem Schliess-Ereignis die Entscheidung. `run-gates.sh` 80 → 84 PASS (vier neue Gates), Cut-Identität 5/5, Refactor-Suiten ref5, Screenshots unverändert. Gemessene Korrektur am Befund C3: `quit()` führt unter Qt 6.10 selbst ein `closeEvent` aus — die alte Fassung fragte bei Cancel zweimal, statt zu beenden; der Datenverlust lag im Speichern-Zweig. Rescans: 16 → 0 neu im Scope. Sechs Karten aktualisiert und gestempelt. Nächster Schritt: neue Karte (Regel „Karte, Audit, Karte") |
