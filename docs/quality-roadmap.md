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
| 3 Audit-Lauf 3 nach Karte | offen | — |
