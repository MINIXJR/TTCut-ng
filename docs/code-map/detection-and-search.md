---
base_commit: 7068942c9294e96ea6bcfa8c18bfb69a523449ff
last_verified: 2026-08-20
sources:
  - data/ttanalysislog.cpp
  - data/ttanalysislog.h
  - data/ttsearchtask.cpp
  - data/ttsearchtask.h
  - data/ttsearchtask_aspectscan.cpp
  - data/ttsearchtask_aspectscan.h
  - data/ttsearchtask_blackframe.cpp
  - data/ttsearchtask_logo.cpp
  - data/ttsearchtask_scenechange.cpp
  - data/ttaspectdetect.cpp
  - data/ttaspectdetect.h
  - data/ttstreampoint.h
  - data/ttstreampoint_videoworker.cpp
  - data/ttstreampoint_audioworker.cpp
  - data/ttstreampointmodel.cpp
  - data/ttframesearchtask.cpp
  - data/ttframesearchtask.h
  - data/ttaudioanomalyscantask.h
  - data/ttaudioanomalyscantask.cpp
  - data/ttaudiorepairitem.h
  - gui/ttstreampointwidget.cpp
  - gui/ttcutmainwindow.cpp
  - gui/ttaudiorepairdialog.h
  - gui/ttaudiorepairdialog.cpp
  - common/ttthreadtaskpool.cpp
  - common/ttthreadtask.cpp
  - common/ttsettings.h
  - common/ttsettings.cpp
  - data/ttcutpreviewtask.cpp
  - data/ttcutvideotask.cpp
---

# Erkennung und Suche: ein Dekodier-Unterbau, zwei Ergebnisformen

Alle automatischen Bildanalysen der Anwendung sitzen auf **einer** Basisklasse,
`TTSearchTask`. Sie liefert das Dekodieren (N parallele Dekoder, geteilter
Bildindex, Stapelbildung entlang der Indexliste); die Unterklassen liefern nur
das Urteil über ein Einzelbild.

Die Familie zerfällt nach **Ergebnisform**, nicht nach Codec:

| Form | Signal | Abbruch bei | Klassen |
|---|---|---|---|
| **Gerichtete Suche** — „nächster Treffer ab hier" | `found(pos, wasAborted)` | erstem Treffer | `TTBlackFrameSearchTask`, `TTSceneChangeSearchTask`, `TTLogoSearchTask` |
| **Voll-Scan** — „alle Wechsel im Strom" | `pointsDetected(QList<TTStreamPoint>)` | Dateiende | `TTAspectScanTask` |

Daneben stehen drei Analysen, die **kein** Bild dekodieren und deshalb nicht von
`TTSearchTask` erben, aber in denselben Aufgaben-Pool und dieselbe Marker-Liste
münden: `TTStreamPointVideoWorker` (MPEG-2-Sequenz-Header),
`TTStreamPointAudioWorker` (Stille, AC3-Formatwechsel) und seit
`2026-08-19/20` `TTAudioAnomalyScanTask` (`data/ttaudioanomalyscantask.{h,cpp}`)
— sequenzieller Scan einer AC3-Spur auf CRC-gültige, aber strukturell
defekte Center+LFE-Bursts in Material, dessen LFE sonst digital still ist
(`docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md`,
Komponente 1). Erbt wie die anderen beiden direkt `TTThreadTask`, verbindet
sich identisch (`finished`/`aborted` → `onAnalysisWorkerFinished`, zählt in
`mStreamPointWorkersRunning`) und liefert `pointsDetected(QList<TTStreamPoint>)`
an denselben Slot wie der Voll-Scan (`TTCutMainWindow::onVideoPointsDetected`)
— kein eigener Slot.

Und eine Bildanalyse steht **ausserhalb** dieser Familie: die Gleichbild-Suche
`TTFrameSearchTask` (`data/ttframesearchtask.cpp`). Sie erbt direkt von
`TTThreadTask`, läuft im `mpThreadTaskPool` statt im Stream-Point-Pool, wird vom
CutOut-Fenster über `TTAVData::onDoFrameSearch()` ausgelöst und vergleicht
YUV-Ebenen ganzer Bilder, statt ein Prädikat auf I-Bilder anzuwenden — anderer
Algorithmus, eigene Schleife, eigener Dekoder. Zwei Unterschiede im Melde- und
Index-Verhalten, die beim Lesen der Familie oben leicht falsch übertragen
werden:

- **Meldung:** `TTFrameSearchTask` meldet an den Fortschrittsdialog (`Start` und
  ein `Step` **pro verglichenem Bild**; gemessen 2251 Steps auf 224 930 Bildern,
  `tools/diag/test_framesearch_progress`). Die drei gerichteten Suchen oben tun
  das **nicht** — sie senden ihr eigenes `progress(int)` alle ~20 Bilder in die
  Statuszeile und haben ihren Abbrechen-Knopf im Navigationsbereich; ein
  Fortschrittsdialog erscheint bei ihnen gar nicht, weil `TTProgressBar` erst
  bei `Start` oder `Step` sichtbar wird.
- **Bildindex:** beide Ströme der Gleichbild-Suche übernehmen einen vorhandenen
  Index über `TTH26xVideoStream::provideFrameIndexTo()` und scannen nur, wenn
  keiner da ist. Der Suchstrom tat das bis 2026-08-15 nicht und scannte immer —
  5553 ms von 11 464 ms Suchlaufzeit auf echtem Material, siehe
  `docs/completed-work.md`.

**Der Bildindex wird wiederverwendet, wo es einen gibt.** Jeder Aufruf zieht ihn
aus dem Vorschau-Wrapper
(`currentFrame->videoWindow()->ffmpegWrapper()->frameIndex()`) und reicht ihn als
`preBuiltFrameIndex` durch; nur wenn der leer ist, baut jeder Sub-Dekoder sich
selbst einen. Das spart den erneuten Aufbau — und ist für H.264/H.265
**Voraussetzung**, nicht Optimierung (siehe Fallstricke).

## Datenfluss

**Legende:** durchgezogene Kante = *Daten fließen*; gestrichelte Kante = *löst
aus* (Kontrollfluss, keine Nutzdaten).

```mermaid
flowchart TB
    subgraph SRC["Quellen (GUI-Thread, vor dem Start eingesammelt)"]
        PREV["Vorschau-Wrapper<br/>frameIndex()"]
        IDX["TTVideoIndexList<br/>Anzeige-Ordnung"]
    end

    LOADEXIT["Ladepipeline-Exits<br/>onAVDataReloaded() /<br/>onAVItemChanged()"]
    MW["TTCutMainWindow<br/>onAnalyzeStreamPoints /<br/>onSearchBlackFrame …"]
    WIDGET["TTStreamPointWidget<br/>Trefferliste, Start, Abbruch"]
    POOLQ["TTThreadTaskPool<br/>mTaskQueue"]

    subgraph BASE["TTSearchTask — gemeinsamer Unterbau (Arbeitsthread)"]
        SETUP["setupWorkers()<br/>N × TTFFmpegWrapper<br/>bzw. 1 × TTMpeg2Decoder"]
        BATCH["collectNextBatch()<br/>Stapel entlang der Indexliste"]
        PMAP["parallelMap()<br/>QThreadPool + QSemaphore"]
    end

    subgraph TASKS["Unterklassen: Urteil je Bild"]
        DIRT["TTBlackFrameSearchTask<br/>TTSceneChangeSearchTask<br/>TTLogoSearchTask"]
        ASPECT["TTAspectScanTask<br/>collectSampleBatch<br/>classifyBatch<br/>refineTransition"]
    end

    PURE["ttaspectdetect<br/>classifyAspectSample<br/>TTAspectHysteresis"]

    subgraph NODEC["Ohne Bilddekodierung"]
        VWORK["TTStreamPointVideoWorker<br/>MPEG-2-Sequenz-Header"]
        AWORK["TTStreamPointAudioWorker<br/>Stille + acmod"]
        ANOMALY["TTAudioAnomalyScanTask<br/>AC3 C+LFE-Burst"]
    end

    subgraph OUT["Ergebnis (GUI-Thread)"]
        NAV["Navigation<br/>onVideoSliderChanged"]
        MODEL["TTStreamPointModel<br/>Markerliste"]
        BAR["TTProgressBar"]
    end

    WIDGET -. "analyzeRequested / abortRequested" .-> MW
    LOADEXIT -. "0-ms-Timer → maybeStartAutoAnomalyScan()" .-> MW
    PREV -- "preBuiltFrameIndex" --> MW
    IDX -- "Positionen" --> MW
    MW -. "start()" .-> POOLQ
    POOLQ -. "run() → operation()" .-> BASE
    POOLQ -. "run() → operation()" .-> NODEC

    SETUP --> PMAP
    BATCH --> PMAP
    PMAP -- "QImage / Histogramm / Schwarzurteil" --> DIRT
    PMAP -- "QImage" --> ASPECT
    ASPECT -- "Graustufenbild" --> PURE
    PURE -- "TTAspectSample<br/>TTAspectTransition" --> ASPECT

    DIRT -- "found(pos, wasAborted)" --> NAV
    ASPECT -- "pointsDetected" --> MODEL
    NODEC -- "pointsDetected" --> MODEL
    POOLQ -- "statusReport" --> BAR
    BAR -. "cancel → onAbortStreamPoints" .-> POOLQ
```

Richtung gemessen (2026-08-20, nach Einbau der drei neuen Knoten/Kanten):
`TD` viewBox-Verhältnis 0,80, `LR` 5,66 — `TD`/`TB` bleibt klar näher an 1,
beibehalten.

## Kanten-Semantik

| Kante | Bedeutung | Fallstrick |
|---|---|---|
| `PREV → MW → Task` (`preBuiltFrameIndex`) | Kopie der `QList<TTFrameInfo>` des Vorschau-Wrappers. Leer ⇒ jeder Sub-Dekoder ruft `buildFrameIndex()` selbst. | Bei H.26x ist die Liste die **einzige** Quelle für Anzeige↔Dekodier-Zuordnung. Ein Worker ohne Index gibt aus `decodeFrame()` leere `QImage` zurück — genau der zweite der beiden Ur-Defekte der Pillarbox-Erkennung. |
| `IDX → collectNextBatch/collectSampleBatch` | Positionen stammen aus `moveToNextIndexPos`/`moveToPrevIndexPos`, also **Anzeige**-Positionen für alle Codecs. | Nicht in Dekodier-/AU-Indizes umrechnen. Die Kette (Positionsauswahl → `decodeFrame` → `found`/`pointsDetected` → `onVideoSliderChanged`) ist durchgehend anzeigeordnungs-konsistent. `TTStreamPointVideoWorker` zählt zwar `picture_start_code`-Header in Bitstrom-Reihenfolge, **schlägt die gemeldete Position aber in der anzeigesortierten Indexliste nach** (`displayPositionAfter`) — siehe die Zeile darunter. |
| `VWORK → displayPositionAfter → IDX` | Der Bitstrom-Zähler wird über den Kopf-Index des ersten Bildes nach dem Sequenzkopf in einen Rang der anzeigesortierten `TTVideoIndexList` übersetzt. | War bis `2026-07-30` nicht vorhanden; der rohe Zähler landete als Markerposition in `onVideoSliderChanged`. Gemessen (`tools/diag/test_streampoint_order`): TELE5 576p25 alle 626 Sequenzköpfe um +2 daneben, Comedy Central 268 von 350 (überwiegend +3, bis +6); RTLZWEI fährt geschlossene GOPs und war zufällig richtig. **Nachrechnen (`base_number + temporal_reference`) genügt nicht** — Feldbild-Paare lassen Rang und `display_order`-Wert auseinanderlaufen (65 von 7507 bzw. 14 von 136319 Einträgen). Der lineare Suchlauf ist vertretbar, weil er nur bei einem echten Wechsel läuft. |
| `setupWorkers → parallelMap` | N Wrapper mit `setAnalysisMode(true)` **und** `setSearchMode(true)`; `parallelMap` verteilt Index *i* fest auf Wrapper *i*. | `setSearchMode(true)` = direkter Keyframe-Sprung ohne DPB-Vorlauf. Gemessen 34 ms statt 111 ms je I-Frame (2026-07-29 auf `03x01_-_Drunter_und_drüber.264`, 720p50) — aber Open-GOP-B-Bilder unmittelbar nach dem Sprung sind dabei nicht garantiert korrekt. Für Stichproben-Analysen belanglos, für Standbildanzeige **nicht** (dort ist der Vorlauf Pflicht, siehe `frame-order.md`). |
| `parallelMap` Worker-Zahl | `TTSettings::searchWorkerCount()`, 0 = automatisch (`idealThreadCount()/2`, gedeckelt 4), geklemmt auf [1, 16]. | MPEG-2 wird hart auf `mWorkerCount = 1` gesetzt — libmpeg2-Dekoder sind nicht mehrfach instanziierbar. Für MPEG-2 fällt `parallelMap` deshalb in den Inline-Zweig. |
| `ASPECT → PURE` | `classifyAspectSample()` bekommt ein `Format_Grayscale8`-Bild und liefert drei Werte: `Pillarbox`, `NoPillarbox`, `NoStatement`. Seit `37d20e13` zusätzlich über einen **optionalen** Ausgabeparameter den Grund (`TTAspectReason`: `None`, `NoBars`, `BarsTooWide`, `CentreTooDark`, `Unusable`). | `NoStatement` ist **kein** Fehlerwert, sondern die Aussage „dieses Bild darf den Zustand nicht bewegen": Schwarzbild (mittlere Luminanz ≤ 20), zu breiter Balken (> 1,5 × Nennwert — dunkler Bildinhalt am Rand) oder Dekodierfehler. Die Hysterese ignoriert solche Proben, statt den Kandidatenlauf zurückzusetzen. Der Grund existiert für die Abschlussbilanz im Detailbereich, die die `NoStatement`-Proben aufschlüsselt; der Vorgabewert `nullptr` hält die zweiargumentigen Aufrufe (u. a. `refineTransition`, ~15 Prüffälle im Harness) gültig. **Zählen darf nur der Hauptlauf**: `refineTransition()` ruft `classifyBatch()` ein zweites Mal über dieselbe Gegend und übergibt bewusst keinen Gründe-Vektor, sonst wären die Proben doppelt gezählt. |
| `PURE → ASPECT` (`TTAspectTransition`) | Die Hysterese meldet einen Wechsel erst, wenn der neue Zustand `10 s × fps` Frames durchgehalten hat; `firstFrame` ist die **erste** Probe des Laufs, nicht die bestätigende. | Der Stichprobenabstand darf das Hysteresefenster nicht überschreiten, sonst ist die Hysterese wirkungslos. Seit `aed01838` klemmt der Konstruktor `mSampleStride` auf das Fenster; beide Seiten lesen `kHysteresisWindowSeconds`. |
| `PURE → ASPECT` (`TTAspectCandidate`) | Zusätzlich zum bestätigten Wechsel gibt die Hysterese **verworfene** Kandidatenläufe heraus — abzuholen mit `takeDiscardedCandidate()` nach jedem `feed()`, Einzelplatz mit Löschen beim Lesen. `feed()` behält seine Signatur. | Aufgezeichnet wird nur ein Lauf, der den **bestätigten** Zustand herausgefordert hätte (`mCandidate != mConfirmed`); Schwanken innerhalb des bestätigten Zustands erzeugt nichts. `heldFrames` ist `letzte − erste Probe des Laufs`, nicht `brechende − erste` — dafür führt die Klasse `mCandidateLast` mit, das auf **allen drei** Pfaden nachgezogen wird (Grundzustand, Kandidatenwechsel, Fortsetzung). Ein Lauf, der einen Wechsel auslöst, wird nicht zusätzlich als verworfen gemeldet. Der Scan zeigt Läufe mit `heldFrames == 0` **nicht** an (Einzelausreißer des Klassifizierers, gemessen 11 von 15 auf einer 90-min-Aufnahme mit 1 s Abstand), zählt sie aber getrennt in der Bilanz. |
| `ASPECT.refineTransition` | Zweiter, engerer Durchlauf über **jeden** I-Frame zwischen der letzten Probe des alten Zustands und `firstFrame`; liefert den ersten Frame mit dem gesuchten Zustand. | Ohne den Nachlauf wäre der Marker nur auf den Stichprobenabstand genau. Der Nachlauf läuft nur über I-Frames — auf Bildgenauigkeit *zwischen* zwei I-Frames kommt er nicht. |
| `DIR → found(pos, wasAborted)` | Ein Signal für beide Ausgänge: `pos ≥ 0` Treffer, `pos = -1` kein Treffer. Das zweite Argument trennt „nichts gefunden" von „abgebrochen". | Bei `-1` springt die GUI auf `mLastSearchStartPos` zurück, damit der Abbruch die Anzeige nicht verschiebt. |
| `SCAN → pointsDetected` | Wird **auch bei Abbruch** ausgesendet, mit den bis dahin gefundenen Punkten. | Bewusst: Teilergebnisse sind brauchbar. Die Statuszeile des Widgets kennzeichnet den Lauf über `setAnalysisRunning(false, aborted)` als unvollständig. |
| `ANOMALY → pointsDetected` | Weicht vom `SCAN`-Verhalten oben **bewusst ab**: bei Abbruch sendet `TTAudioAnomalyScanTask::operation()` eine **leere** Liste, keine Teilergebnisse. | Ein Abbruch darf nicht wie ein vollständiger Lauf mit kurzer Trefferliste aussehen (Spec Komponente 1, „Sichtbarkeit"). `pointsDetected` feuert bei ihr nur genau einmal, ganz am Ende von `operation()`. |
| `LOADEXIT → MW` (`maybeStartAutoAnomalyScan()`) | Zwei unabhängige Einsprünge — `TTAVData::onAVDataReloaded()` (jeder Pool-Exit) und `TTCutMainWindow::onAVItemChanged()` (setzt `mpCurrentAVDataItem`) — beide über `QTimer::singleShot(0, …)`, damit der Scan erst nach der restlichen Ladekette einreiht statt mitten in ihr. | Notwendig, weil `TTAVData::onOpenVideoFinished` den modalen „Defective Frames"-Dialog vor `currentAVItemChanged()` zeigen kann; der Pool-Exit wartet nicht auf diesen Dialog, `onAVDataReloaded()` kann also mit `mpCurrentAVDataItem == nullptr` feuern (gemessen 2026-08-20 auf realem 1,6-GB-Material). Welcher der beiden Einsprünge tatsächlich startet, hängt vom Material ab; die Wächter in `maybeStartAutoAnomalyScan()` (`anomalyScanStarted()`, laufende Analyse, vorhandene `AudioAnomaly`-Marker, `audioAnomalyScanEnabled()`, `mProjectLoadInProgress`) machen jeden weiteren Aufruf zum No-op. Ruft intern `startAudioAnomalyScan()` — denselben Helfer wie der Button-Pfad über `WIDGET`/`MW`; kein zweiter Verdrahtungscode. |
| `NODEC → MODEL` (`ANOMALY`-Anteil) | `TTAudioAnomalyScanTask` liefert `TTStreamPoint`s vom Typ `AudioAnomaly` mit **zwei** Koordinaten: `frameIndex`/`duration` (Video-Anzeigeindex, `duration` endexklusiv in Sekunden — dieselbe Konvention wie alle anderen Markertypen) **und** `audioFrameFrom()`/`audioFrameTo()` (exakter AC3-Quell-Frame-Bereich, **beide Grenzen inklusiv** — dieselbe Konvention wie `TTAudioRepairItem`). | Die Video-Koordinate ist eine verlustbehaftete Projektion (Rundung auf 40-ms-Videoraster gegen das 32-ms-Audioraster); der Reparaturdialog verwendet deshalb `audioFrameFrom/To`, nicht `frameIndex/duration`, sobald sie vorhanden sind (`hasAudioFrameRange()`). Ein aus einer älteren Projektdatei geladener `AudioAnomaly`-Marker hat `-1/-1` (unbekannt) und muss auf die Schätzung zurückfallen. |
| `POOLQ → BAR` (`statusReport`) | Die Aufgaben dieser Familie melden `Start`/`Step`/`Finished` — `Init` sendet nur `TTAVData` auf den Schnitt-Pfaden. Der `Start`-Zweig in `onStatusReport` öffnet den Dialog. | Zwei Aufgaben ⇒ zwei `Start`. Das Kreuz des Dialogs bricht deshalb ab (`closeEvent → onBtnCancelClicked`, `7d6dad0d`): reines Verstecken hätte die zweite `Start`-Meldung wieder aufgezogen. |
| `BAR.cancel → onAbortStreamPoints` | Nicht direkt an den Pool, sondern über die Fenstermethode, damit `mStreamPointAnalysisAborted` gesetzt wird. | Ohne dieses Flag meldet das Widget einen abgebrochenen Lauf als normal beendet. |
| `finished` **und** `aborted` → `deleteLater` | `TTThreadTask::run()` wirft `TTAbortException`, wenn die Aufgabe schon vor dem Start abgebrochen wurde — dann kommt **nur** `aborted`, nie `finished`. | Nur `finished` zu verbinden leckt jede vor dem Start abgebrochene Aufgabe. Alle vier Aufgaben verbinden beide Signale. Bei den drei gerichteten Suchen war das Leck die kleinere Hälfte: ohne `found` bleibt auch `mpRunningSearch` gesetzt und blockiert jede weitere Suche. |
| Marker → `onStreamPointJump(frameIndex)` | Ruft `TTCurrentFrame::onGotoFrame(frameIndex, 0)` und danach `checkCutPosition`. | **Nicht** `onVideoSliderChanged`: dessen `fastSlider()`-Weiterreichung als zweites Argument an `onGotoFrame` ist ein **Bildtyp**, kein Geschwindigkeitsschalter — `1` heißt „ab hier den nächsten I-Frame suchen". Mit eingeschaltetem FastSlider landete ein Marker bei 7045 deshalb auf 7050, und die drei Fehlermarker eines Defekts (7045, 7048, 7048) fielen auf dasselbe Bild (`ad536d7c`). Seit `ba393cb6` ruft `onVideoSliderChanged` `onGotoFrame` nicht mehr direkt, sondern merkt nur die Position und entprellt über einen 50-ms-Timer (`onSliderDecodeTimer`); erst der greift `fastSlider()` wie beschrieben ab — bei gehaltenem Schieber sogar über einen dritten Pfad, `TTCurrentFrame::onGotoFramePreview(pos)`, ohne Bildtyp-Argument. Marker-Sprünge sind davon nicht betroffen; Details der Entprellung in `frame-order.md`. |
| `MODEL → onContextMenu` (`AudioAnomaly`-Zweig) | Kein Teil des Erkennungs-Datenflusses oben, sondern ein GUI-Thread-Konsument der fertigen Markerliste: `TTStreamPointWidget::onContextMenu` bietet bei einem `AudioAnomaly`-Marker (nur wenn eine AC3-Spur existiert) „Repair…"/„Edit repair…"/„Remove repair" an, öffnet `TTAudioRepairDialog` und schreibt/löscht dabei `TTAudioRepairItem`-Einträge am `TTAVItem`; liest/schreibt den Markertext über `TTStreamPointModel::pointAt()`/`setDescriptionAt()`. | Bewusst außerhalb dieses Diagramms belassen (Scope = Erkennung/Suche, nicht Reparatur) — die Reparatur-Pipeline selbst (`ACMOD → REPAIR → CUT`) ist in `audio-cut-timing.md` kartiert. Repair-vs-Scan-Track-Konsistenz: beide Seiten ermitteln die Spur über `TTAVItem::firstAc3TrackIndex()`, kein zweiter Auswahlmechanismus. |

## Varianten-Matrix

| | MPEG-2 (`mpeg2_demuxed_video`) | H.264 / H.265 |
|---|---|---|
| Dekoder | 1 × `TTMpeg2Decoder` (libmpeg2, RGB32) | N × `TTFFmpegWrapper` |
| Parallelität | keine (`mWorkerCount = 1`, Inline-Zweig) | N (Voreinstellung ≤ 4) |
| Bildindex | `TTVideoIndexList` + `TTVideoHeaderList` aus dem Parser | `preBuiltFrameIndex` bzw. `buildFrameIndex()` |
| Header-Liste | vorhanden (`ttmpeg2videostream.cpp:73`) | **null** — `TTH26xVideoStream` legt keine an |
| Seitenverhältnis über Header | ja (`TTStreamPointVideoWorker`, liest `aspect_ratio_information`) | nein — Elementarströme tragen keine Sequenz-Header |
| Pillarbox über Bildanalyse | ja | ja |
| Schwarzbild-Prüfung | Eigenimplementierung in `TTSearchTask::isFrameBlackAt` | `TTFFmpegWrapper::isFrameBlack` |

Die Zeile „Header-Liste" ist der **erste** der beiden Ur-Defekte: bis
`1f1797d9` hing der gesamte Video-Worker an `videoHeaders->size() > 0`, womit
die Pillarbox-Erkennung für H.264/H.265 nie startete. Die beiden Erkennungen
sind seither getrennt verdrahtet — Header-Seitenverhältnis an
`spDetectAspectChange()` **und** eine nicht-leere Header-Liste, Pillarbox an
`spDetectPillarbox()` **und** eine nicht-leere Indexliste.

## Der Klassifizierer im Detail

`classifyAspectSample()` (`data/ttaspectdetect.cpp`), reine Funktion, ohne Qt-GUI
testbar (`tools/diag/test_aspectdetect`). Die Schritte 4 und 5 sind zugleich die
Gründe, die der optionale dritte Parameter herausgibt (`BarsTooWide`,
`CentreTooDark`); Schritt 3 liefert `NoBars`, ein Nullbild oder ein Bild unter
20 × 20 `Unusable`:

1. Messband = mittlere 40 % der Bildhöhe (`0.30 h … 0.70 h`).
2. Spalte gilt als schwarz, wenn ≥ 90 % der jeden zweiten abgetasteten Zeilen
   unter dem Luminanz-Schwellwert liegen (Voreinstellung 20).
3. Von beiden Rändern nach innen zählen, bis die erste nicht-schwarze Spalte
   kommt. Beide Balken müssen ≥ 10 % der Bildbreite messen.
4. **Obergrenze der Balkenbreite**: ist ein Balken breiter als 1,5 × dem
   Nennwert (`w·3/16`, bei 1280 also 240 statt 160), ist es `NoStatement`.
   Sonst liest eine dunkle Nachtszene als Pillarbox — gemessen im
   Flemming-Korpus 167/384, 391/364, 544/185, 318/211, 137/276.
5. **Gegenprobe in der Mitte**: liegt die mittlere Luminanz zwischen den Balken
   bei ≤ 20, ist es `NoStatement` — sonst läse ein Schwarzbild als Pillarbox,
   weil seine „Balken" in der Mitte zusammenstoßen. Das spiegelt
   `TTFFmpegWrapper::isFrameBlack`, das seinerseits die äußeren 10 % ignoriert.

Der Schwellwert ist ein **Bild**-Wert (swscale-Ausgabe, Schwarz = 0), nicht der
Videobereichs-Y-Wert des Stroms (Schwarz ≈ 16).

## Was der Detailbereich erfährt

Seit `3c756b9b`..`833e4a46` erklären sich alle drei Analysen im Detailbereich
des Fortschrittsdialogs, statt nur einen Zählerstand zu zeigen. Der Weg dorthin
ist in `progress-reporting.md` beschrieben (Kante „Landing-zone workers →
`TTAnalysisLog` → `AddProcessLine`"); hier nur, was für diese Karte zählt:

- Jeder Worker hält einen `TTAnalysisLog` (`data/ttanalysislog.{h,cpp}`, kein
  `QObject`), dessen Senke `onStatusReport(AddProcessLine, …)` ruft — der
  einzige Statuszustand **ohne** Fortschrittswirkung. Kein `Step` wurde dafür
  hinzugefügt oder verändert; belegt durch einen zeichengenauen Vergleich der
  `Start`/`Step`/`Finished`-Folge gegen eine vor der Änderung gezogene
  Aufzeichnung (`tools/diag/test_aspectscan` schreibt sie mit).
- Ereigniszeilen sind gedeckelt (`resetCap()` zwischen den zwei Abschnitten des
  Audio-Workers), Kopf- und Bilanzzeilen nicht. Was der Deckel schluckt, hängt
  als Zahl an der Bilanz. Der Bildformat-Scan hält dafür **zwei** Protokollierer
  auf derselben Senke (`7f472967`): `mLog` für Kopf, bestätigte Übergänge und
  Bilanz (20 Zeilen), `mNoiseLog` für verworfene Kandidaten (10). Mit einem
  gemeinsamen Budget konnte eine unruhige Aufnahme jede Zeile für Beinahe-Treffer
  ausgeben, bevor ein einziger bestätigter Übergang geschrieben war — die
  Übergänge sind aber das Ergebnis, die Verwürfe nur die Begründung.
- **`Step`-Text muss sich ändern, um im Detailbereich zu landen.**
  `TTProgressBar` hängt einen `Step` nur an den Detailbereich an, wenn sein
  Text vom vorigen `Step`-Text abweicht (`mLastStepMsg`). Der Bildformat-Scan
  trug seinen Stichprobenzähler im Text (`"Aspect format: %1 of %2 samples"`)
  und unterlief die Abschaltung damit bei **jedem** Aufruf — 272 Zeilen für
  5452 Proben, die wenigen wichtigen Zeilen darin ertrunken. Seit `aab25003`
  ist der Text konstant (`"Checking aspect format..."`), der Zähler wandert
  allein in den Zahlwert, den Balken und Prozentanzeige ohnehin schon zeigen.
  Betrifft nur `TTAspectScanTask`; Video- und Audio-Worker melden konstanten Text.
- **Der Bildformat-Scan hat keine eigene Bilanzzeile mehr**: seine Bilanz *ist*
  die `Finished`-Meldung (`417c3aad`). Vorher standen beide da und sagten
  dasselbe, und auf dem Abbruchpfad erschien die Bilanz im Bereich **nach** dem
  Schlussstrich des Laufs, obwohl `operation()` sie zuerst absetzt. Die
  Umordnung ist nicht aufgeklärt — die beiden anderen Worker senden dasselbe
  Meldungspaar in richtiger Reihenfolge. Eine Meldung kann sich nicht selbst
  überholen, damit ist der Punkt gegenstandslos; taucht der Effekt anderswo
  auf, ist das hier der Anhaltspunkt.
- Läufe, die von vornherein nichts finden **können**, sagen das: kein
  MPEG-2-Strom für die Sequenzkopf-Analyse, keine AC3-Kopfdaten für die
  Kanalwechsel-Erkennung (MP2 wird in `detectAudioChanges()` übersprungen —
  Lücke sichtbar gemacht, nicht geschlossen), und alle acht Ausstiege der
  Stille-Erkennung über `silenceUnavailable()`, das zugleich die sonst
  irreführende Zeile „0 Bereiche gefunden" unterdrückt.
- **Blind bleibt, wer gar nicht erst gebaut wird — inzwischen behoben.**
  `onAnalyzeStreamPoints()` legt einen Worker nur an, wenn seine
  Voraussetzung erfüllt ist; ohne Tonspur, ohne Header-Liste (also bei
  H.264/H.265 für die Sequenzkopf-Analyse) oder mit leerer Indexliste sammelt
  `mSkippedAnalysisNotes` (`QStringList`, `TTCutMainWindow`) eine Zeile pro
  übersprungener, aber eingeschalteter Analyse. Läuft mindestens ein Worker,
  gehen die Notizen ins Log **und** über `mPendingSkipNotesForDialog` in den
  Detailbereich — aber erst **nach** dem `Start`-Zweig in `onStatusReport`,
  nicht darin: `TTProgressBar::onSetProgress()` ruft bei `Start` unter
  Umständen `resetForNewOperation()`, das den Detailbereich leert, und ein
  Eintrag davor wäre sofort wieder gelöscht worden. Läuft **kein** Worker,
  meldet ein eigener Dialog die Gründe statt der pauschalen „keine
  Erkennungsmethode aktiviert" (die bei aktivierten, aber ungeeigneten
  Methoden in die falsche Ecke — die Einstellungen — schickte).

## Fallstricke

- **Dekodier- gegen Anzeige-Ordnung.** `TTFFmpegWrapper::frameIndex()` ist in
  **Dekodier**-Reihenfolge; `decodeFrame()` erwartet eine **Anzeige**-Position.
  Wer über `frameIndex()` iteriert und die Einträge an `decodeFrame()` gibt,
  misst falsch. Genau das tat die erste Fassung von `tools/diag/test_pillarbox`
  und lieferte 49726 statt 49719 (behoben in `61c36f5c`).
- **Symmetrie taugt *nicht* als Kriterium.** Naheliegend wäre, echte Balken an
  ihrer Mittigkeit zu erkennen (echt 0–3 px Unterschied, Nachtszene 16–638).
  Gemessen überlappen die beiden Verteilungen aber: im *echten* 4:3-Abschnitt
  von 03x01 bleibt der linke Balken bei 161 und der rechte wächst über 10 s auf
  212 (Unterschied 51), im Flemming-Korpus sogar auf 472. Dunkler Bildinhalt
  direkt am Balken lässt ihn breiter erscheinen. Ein Symmetrie-Gate zerriss
  beide echten Abschnitte in drei. Deshalb die Obergrenze auf die *absolute*
  Breite, und sie liefert `NoStatement` (übersprungen) statt `NoPillarbox`
  (bricht den Lauf ab) — genau damit dieses Ausbluten einen echten Abschnitt
  nicht zerschneidet.
- **Zu kleines Messfenster.** Vor jeder Aussage „der Wechsel ist bei Frame X"
  prüfen, ob der neue Zustand *anhält*. Auf `pb43.m2v` liegt bei 715 ein
  1,4-s-Pillarbox-Einschub, dann 3,0 s 16:9, der bleibende Wechsel erst bei 825
  — die Hysterese unterdrückt den Einschub absichtlich, 830 ist richtig, 722
  wäre falsch.
- **ffmpeg-`n` ≠ TTCut-Anzeigeindex bei MPEG-2.** Für `pb43.m2v` Versatz +2
  (135000 Indexeinträge gegen 134998 ffmpeg-Ausgabeframes = 2 verworfene
  führende B-Bilder). Gleiche Falle wie in `mpeg2-cut.md`.
- **Abbruch räumt synchron auf.** `TTThreadTaskPool::onUserAbortRequest()`
  bricht auch noch nicht gestartete Aufgaben ab, und `TTThreadTask::run()`
  prüft `mIsAborted` **vor** `operation()`. Eine abgebrochene Aufgabe meldet
  deshalb nie `Start`. Darauf ruht die Kreuz-gleich-Abbrechen-Lösung.
- **`onUserAbortRequest()` darf `finished` nicht trennen.** Genau dieser Slot
  nimmt die Aufgabe über `removeAll()` aus `mTaskQueue`; trennt man ihn und
  löscht die Aufgabe per `deleteLater`, behält der Pool einen toten Zeiger
  (Absturz, behoben in `e247dbda`, nachstellbar mit
  `tools/diag/test_pool_abort 16 400`). Die Schleife iteriert seither eine
  `QPointer`-Momentaufnahme, weil `TTThreadTask::abort()` selbst
  `processEvents()` ruft.
- **Ein Abbruch nach dem Ende wird jetzt ignoriert statt doppelt verarbeitet.**
  `TTThreadTask::abort()` unterscheidet mehr als zwei Flags
  (`!mIsRunning && !mIsAborted`) und konnte „noch nicht gestartet" (Aufgabe
  liegt noch in der Warteschlange, `aborted()` ist hier richtig) nicht von
  „schon fertig" unterscheiden: `run()` hat `mIsRunning = false` gesetzt,
  `cleanUp()` gerufen und `finished()` ausgelöst — das Signal wartet aber noch
  in der Ereigniswarteschlange des GUI-Threads. Ein Abbruch, der in dieses
  Fenster fiel, löste ein zusätzliches `aborted()` und ein zweites `cleanUp()`
  auf dem anderen Thread aus; der Pool protokollierte „Last thread task
  aborted" für einen längst abgeschlossenen Lauf. Ein neues, atomares Flag
  `mIsFinished` (gesetzt **vor** `emit finished()`, gelesen von `abort()`)
  lässt diesen Fall jetzt still — geloggt, nicht stillschweigend — abbrechen.
  Das Flag wird bei **jedem** Eintritt in `run()` zurückgesetzt, nicht nur im
  Konstruktor: Aufgaben werden wiederverwendet (`TTCutVideoTask` treibt eine
  einzige `TTCutTask`-Instanz durch jeden Schnittlisteneintrag), ein
  stehengebliebenes Flag hätte jeden Eintrag nach dem ersten unabbrechbar
  gemacht — derselbe Defekt wie `1f372cca`, diesmal am Abschluss- statt am
  Abbruchpfad. Harness `tools/diag/test_abort_after_finish`, drei Fälle
  (Abbruch nach Ende, Abbruch vor Start, Abbruch während eines
  Wiederholungslaufs).
- **`cleanUp()` ist absichtlich leer.** Der Dekoder wird nur im Destruktor
  freigegeben (GUI-Thread über `deleteLater`). Ein Aufräumen im Arbeitsthread
  läuft ohne Happens-before-Kante gegen den Destruktor und kann doppelt
  freigeben.
- **`run()` räumt vor dem Endsignal auf.**
  Wer `finished`/`aborted` an `deleteLater` hängt (jeder Nutzer dieser Familie),
  gab dem GUI-Thread damit die Erlaubnis, die Aufgabe zu zerstören, während der
  Arbeitsthread noch den virtuellen `cleanUp()`-Aufruf über den vptr absetzte.
  Mit `tools/diag/test_task_cleanup_order` unter AddressSanitizer nachgestellt:
  `heap-use-after-free` in `cleanUp()`, freigegeben im `DeferredDelete` des
  GUI-Threads. Keine `cleanUp()`-Implementierung im Baum braucht das Signal
  vorher; die beiden, die etwas freigeben, halten es privat ohne Getter.
- **Leerer Indexlisten-Fall meldet nichts.** `TTAspectScanTask::operation()`
  kehrt bei `mIndexList->count() == 0` zurück, ohne `onStatusReport` zu rufen;
  die GUI schirmt den Fall zwar ab, der Harness-Pfad aber nicht. Kleinigkeit,
  offen.

## Redundanz / offene Punkte

- Die drei gerichteten Suchen (`operation()` in `_blackframe`, `_logo`,
  `_scenechange`) haben denselben Rumpf: `setupWorkers` → erste Position →
  Schleife aus `collectNextBatch` + `parallelMap` + erster Treffer → `found` →
  `teardownWorkers`. Unterschied ist ausschließlich das Urteil pro Bild (und
  bei Logo/Szenenwechsel ein Anfangszustand aus Worker 0). Kandidat für eine
  Schablonenmethode `bool matchesAt(int pos, int workerIndex)` in der Basis.
- `TTSearchTask::isFrameBlackAt` und `buildHistogramAt` enthalten je eine
  MPEG-2-Zweitimplementierung dessen, was `TTFFmpegWrapper` für H.26x tut —
  inklusive der 10-%-Randmaske und der `step = 2`-Abtastung. Drei Kopien
  derselben Abtastregel im Baum (dazu `centreMeanLuma` in `ttaspectdetect.cpp`).
- ~~Die drei gerichteten Suchen verbinden nur `finished → deleteLater`, nicht
  `aborted`.~~ Behoben in `f8fe7dd6` — dabei zeigte sich, dass das Leck nur die
  halbe Wirkung war: eine vor dem Start abgebrochene Suche meldet weder
  `finished` noch `found`, also blieb auch `mpRunningSearch` gesetzt und
  blockierte jede weitere Suche.
- ~~`TTCutPreviewTask`/`TTCutVideoTask` rufen `pool->start()` aus
  Arbeitsthreads und verändern `mTaskQueue` threadübergreifend ohne
  Absicherung.~~ Behoben in `0f7b532d`: beide rufen jetzt
  `TTThreadTaskPool::startNested()`. Die eingebettete Aufgabe läuft weiterhin
  **synchron im aufrufenden Arbeitsthread** (`runSynchron()`) — sie wird nicht
  an den Pool-Thread gereicht. Weggefallen ist alles, was `mTaskQueue`
  berührt: die Aufgabe wird nicht eingereiht, und die `destroyed`-Verbindung
  entfällt (sie existiert nur, um eine tote Aufgabe aus der Warteschlange zu
  nehmen). Die Fortschritts-Slots laufen im Pool-Thread, weil die
  Aufgabenobjekte dort leben — Qt wählt dann eine Queued Connection.
  `start()` sichert seine Thread-Zugehörigkeit jetzt per `Q_ASSERT` ab.
  Beleg: ThreadSanitizer meldete auf dem alten Stand Datenrennen auf
  `QListData` der Warteschlange und einen SEGV in `runningTaskCount()`
  (Zeiger aus einem gerade neu belegten Puffer); Harness
  `tools/diag/test_pool_crossthread`, Gate
  `tools/diag/gate_pool_crossthread.sh`.

## Prüfwerkzeuge

| Werkzeug | Aufruf | Prüft |
|---|---|---|
| `tools/diag/test_aspectdetect` | ohne Argumente | Klassifizierer + Hysterese, synthetische Bilder |
| `tools/diag/test_aspectscan` | `<datei> <fps> <stichprobe_s> [erwarteter_frame]` | Voll-Scan über H.264/H.265 |
| `tools/diag/test_aspectscan_mpeg2` | `<datei> <stichprobe_s> [erwartete_anzahl]` | dito für MPEG-2 — **andere Argumentreihenfolge**, kein fps |
| `tools/diag/test_pillarbox` | `<datei> [erster] [letzter] [schritt] [schwelle]` | Einzelbild-Klassifikation über den echten Dekodierpfad — **kein** fps-Argument |
| `tools/diag/test_analysislog` | ohne Argumente | Deckelung, `resetCap()` und Positionsformat des Protokollierers |
| `tools/diag/test_silence_unavailable` | `[datei_ohne_tonspur]` | zwei der acht Ausstiege der Stille-Erkennung: Begründung kommt, „0 Bereiche gefunden" bleibt weg |
| `tools/diag/test_pool_abort` | `[anzahl] [millisekunden]` | Abbruch-Absturz aus `e247dbda` |
| `tools/diag/test_task_cleanup_order` | `[wiederholungen]` | Aufräum-Reihenfolge aus `f8fe7dd6` — **braucht einen ASAN-Bau**, Aufrufzeile steht in der Datei |
| `tools/diag/test_abort_after_finish` | ohne Argumente | Spät-Abbruch nach Ende aus `0af72ab1`, drei Fälle inkl. Wiederverwendung |
| `tools/diag/test_framesearch_progress` | `<es-datei> [refIndex] [searchIndex]` | Meldeverhalten und Indexübernahme der Gleichbild-Suche (`TTFrameSearchTask`) |
