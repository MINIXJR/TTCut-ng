---
base_commit: b06fd6cc54628a2db897d8c33fdb176fc0dba868
last_verified: 2026-09-14
sources:
  - extern/ttaudiocutter.cpp
  - extern/ttaudiocutter.h
  - avstream/ttac3acmod.h
  - avstream/ttac3acmod.cpp
  - avstream/ttaspectwindow.h
  - avstream/ttaspectwindow.cpp
  - gui/ttcutpreview.h
  - data/ttavdata.cpp
  - data/ttavdata.h
  - gui/ttcuttreeview.cpp
  - gui/ttcuttreeview.h
  - gui/ttcutpreview.cpp
  - data/ttpreviewclip.cpp
  - gui/ttcutsettingsaudio.cpp
  - gui/ttcutmainwindow.cpp
  - gui/ttcutmainwindow_headless.cpp
  - common/ttsettings.cpp
---

# Burst-Erkennung (+ Seitenverhältnis, acmod): Analyse → Schnittliste, Vorschau, Warndialog

Audio-Burst = Werbe-Knall unmittelbar an einer Schnittgrenze (DVB: Werbung
startet ~1 Frame vor/nach dem Content-Übergang). Ein Detektor (Schwelle als
Parameter, kein separater Nachfilter mehr), zwei Anzeigen (Schnittliste +
Preview-Dialog).

**Spalte 5 der Schnittliste hat drei Produzenten**: `burstHint` (RMS-Burst,
libav-Dekodierung), `aspectHint` (MPEG-2-Seitenverhältnis am Schnittrand, siehe
unten) und `acmodHint` (AC3-Formatwechsel, nur In-Memory-Header —
teilt sich seit `ada97fd2` die Mehrheits-acmod-Logik mit der Cut-Pipeline über
`ttAnalyzeAcmodWindow`, `avstream/ttac3acmod.cpp`). Beide sind seit derselben
Änderung reine Funktionen, die ein `HintCell{text, tip}` zurückgeben, ohne das
Baum-Widget zu berühren; `updateHintColumn()` — weiterhin der **einzige
Eingang** — ruft alle drei, komponiert Text/Tooltip/Icon aus den Rückgaben
und schreibt Spalte 5 **einmal**. Die frühere Append-an-die-Zelle-Gefahr
(Aufrufreihenfolge zweier Setzer als Vertrag) ist damit entfallen, siehe
Redundanz-Abschnitt. Die Helfer sind `private` und werden von nirgends sonst
gerufen. Der acmod- und der Seitenverhältnis-Pfad sind deshalb hier
mitkartiert, obwohl sie kein Burst sind.

**Seitenverhältnis (nur MPEG-2)** folgt demselben Muster wie der Burst: **eine**
Analyse, `ttAnalyzeAspectWindow` (`avstream/ttaspectwindow.cpp`), drei
Konsumenten (Spalte 5, eigene Vorschau-Zeile mit Sprungknopf, gemeinsamer
Warndialog vor dem Schnitt). Anlass: Der MKV-Muxer übernimmt das
Seitenverhältnis der ganzen Videospur aus dem ersten Sequence-Header des
Schnitts; ein einzelnes 4:3-Bild am Anfang eines 16:9-Programms markiert die
ganze Datei als 4:3 (gemessen 2026-09-19 an `01x03`, Cut-In 23975 statt 23976).

**Die Erkennung ist ein Hinweis, kein Urteil.** Ihre Auflösungsgrenzen sind gemessen
und stehen unter Pitfalls; eine fehlende Warnung heißt *nicht*, dass der Schnitt
sauber ist. Anwenderfassung in `TODO.md` → „Known Limitations".

## Datenfluss

**Legende:** durchgezogene Kante = *Daten fließen* (Produzent → Konsument);
gestrichelte Kante = *löst aus* (Kontrollfluss, keine Nutzdaten).

`RES` (`CutBurstInfo`) ist bewusst ein Datenknoten statt einer Komponente: sonst
müsste das Detektor-Ergebnis als Rückwärtskante `DET → WRAP` gegen den Aufrufpfeil
laufen und würde alle anderen Kanten kreuzen. Alle Datenkanten zeigen so in eine
Richtung. Die Knoten tragen nur Symbol und Rolle — das Verhalten steht in der
Edge-Semantik-Tabelle, eine Zeile pro Kante.

```mermaid
flowchart TD
    SRC["Quell-AC3<br/>(nicht der Output)"]
    SET["burstMinDeltaDb"]
    HDR["TTAudioHeaderList<br/>acmod"]

    WRAP["detectCutInBurst /<br/>detectCutOutBurst"]
    DET["TTAudioCutter::detectBurst"]
    RES["CutBurstInfo"]

    BURST["burstHint"]
    ACMOD["acmodHint<br/>(ttAnalyzeAcmodWindow)"]
    COL5["Spalte 5"]
    PREV["checkBurstForCurrentCut"]
    FINAL["cutWarnings /<br/>confirmCutWarnings"]
    SEQ["Sequence-Header<br/>(Header-Liste)"]
    ASPWIN["ttAnalyzeAspectWindow"]
    AINFO["TTAspectWindowInfo"]
    ASPECT["aspectHint"]
    APREV["checkAspectForCurrentCut"]
    MOVE["moveCutEdge"]

    HINT["updateHintColumn"]
    APPEND["onAppendItem /<br/>onUpdateItem"]
    REFRESH["refreshHintIcons"]
    SEL["Clip-Auswahl<br/>(Preview)"]
    CUTRUN["Cut-Start"]
    NONINT["setNonInteractive"]
    PROBE["ttcut-burst-probe"]

    SET -->|"minDeltaDb"| WRAP
    WRAP -->|"boundaryTime"| DET
    SRC -->|"Samples"| DET
    DET --> RES
    RES --> BURST
    RES --> PREV
    RES --> FINAL
    HDR --> ACMOD
    SEQ -->|"getSequenceHeader(pos)"| ASPWIN
    ASPWIN --> AINFO
    AINFO --> ASPECT
    AINFO --> APREV
    AINFO --> FINAL
    BURST -->|"HintCell"| HINT
    ASPECT -->|"HintCell"| HINT
    ACMOD -->|"HintCell"| HINT
    HINT -->|"schreibt einmal"| COL5

    APPEND -.-> HINT
    REFRESH -.-> HINT
    HINT -. "1." .-> BURST
    HINT -. "2." .-> ASPECT
    HINT -. "3." .-> ACMOD
    SEL -.-> PREV
    SEL -.-> APREV
    PREV -. "Shift-Knopf" .-> MOVE
    APREV -. "Sprungknopf" .-> MOVE
    MOVE -. "Neuerzeugung" .-> PREV
    MOVE -. "Neuerzeugung" .-> APREV
    CUTRUN -.-> FINAL
    NONINT -. "Modus" .-> FINAL
    PROBE -. "umgeht WRAP" .-> DET
```

## Edge-Semantik

Eine Zeile pro Diagramm-Kante, in Diagramm-Reihenfolge. Die Knoten-IDs sind die
aus dem Mermaid-Block. Durchgezogen = Daten, gestrichelt = löst aus.

| Kante | Daten / Ordnung / Invariante |
|---|---|
| `SET → WRAP` | `TTSettings::burstMinDeltaDb()`, Default 20 dB. **`<= 0` ⇒ Frühausstieg im Wrapper, ohne die Audiodatei zu öffnen** (verifiziert: 0 `openat`). Werte 1–19 wirken; die Schwelle ist Parameter des Detektors, nicht hartcodiert. |
| `WRAP → DET` | `boundaryTime` in Sekunden der **Quell**-Zeitachse, aus dem Video-Frame-Index: CutIn `(cutInIndex − extraIn)/frameRate`, CutOut `(cutOutIndex + 1 − extraOut)/frameRate`. Das `+1` legt die Grenze hinter den letzten behaltenen Frame; `extraIn/Out` = `countExtraFramesBefore` (MPEG-2-Field-Extras, siehe `mpeg2-cut.md`). Dazu `minDeltaDb` durchgereicht. |
| `SRC → DET` | Dekodierte Samples des **Quell**-AC3 (Track 0 = Position 0 der Audio-Liste), nicht des geschnittenen Outputs — daher unabhängig von Smart-Cut-, Mux- und PTS-Pfaden. Wer Track 0 ist, bestimmt allein die initiale Ladesortierung (AC3 > Sprachpräferenz; Projekt-Load: gespeicherte `<Order>`) — danach ist die Reihenfolge dauerhaft user-kontrolliert (Up/Down-Knöpfe); ein Re-Sort bei Pool-Exit findet nicht statt. Der per-Append-Sort in `onOpenAudioFinished` garantiert weiterhin, dass Track 0 schon **vor** dem Pool-Exit die präferierte Spur ist (Burst-Detection beim initialen VDR-Cut-Add). |
| `DET → RES` | `bool present` + `burstRmsDb`/`contextRmsDb` (**nur bei Treffer gesetzt**). Kriterium: **Peak** der zwei Randchunks, `peak − median >= minDeltaDb` **UND** `peak > kBurstAbsoluteFloorDb` (−40 dB, absolutes Hörbarkeits-Gate). Peak statt First-Hit, weil die Anstiegsflanke 38–51 dB pro 32-ms-Frame steigt und der erste überschwellige Chunk sonst rasterabhängig irgendwo darauf landet. **Merke:** Peak vs. First-Hit ändert nur den *angezeigten* `burstRmsDb` (beide Bedingungen monoton in rms ⇒ `present` invariant); der Erkennungs-Fix war die Schwellen-Vereinheitlichung. |
| `RES → BURST`, `RES → PREV`, `RES → FINAL` | Dasselbe `CutBurstInfo` an alle drei Konsumenten, kein Nachfilter mehr (`a7d1c0e`). Deshalb zeigen Schnittliste, Preview-Dialog und Final-Warndialog **zwangsläufig dieselbe `present`-Entscheidung** — „Icon fehlt" und „Warnung fehlt" haben immer dieselbe Ursache. |
| `HDR → ACMOD` | `acmodHint` liest `item.avDataItem()->audioStreamAt(0)` und ruft `ttAnalyzeAcmodWindow(stream, cutInSec, cutOutSec)` (`avstream/ttac3acmod.cpp`) — **dieselbe Funktion, die `TTAVData::computeTargetAcmods` für die Cut-Pipeline aufruft** (seit `ada97fd2`, siehe `audio-cut-timing.md`, Kante `ACMOD → CUT`). Arbeitet auf der **In-Memory** `TTAudioHeaderList` (`TTAC3AudioHeader`): kein File-I/O, kein libav — anders als der Burst-Pfad. Fenster `[floor(cutIn/frameDur), floor(cutOut/frameDur) − 1]` in AC3-Frames (frameDur aus dem ersten Header der Liste, Default 32 ms bei 48 kHz); Mehrheit über die ersten und letzten `kAcmodSampleFrames` (100) Frames des Fensters, jeder Frame einmal gezählt — ein kürzeres Fenster wird komplett gesampelt. Nur AC3 (`streamType() == ac3_audio`), sonst leerer `TTAcmodInfo` (`mainAcmod == -1`). |
| `SEQ → ASPWIN` | Pro Bild des Fensters `[cutIn, cutOut]` (Anzeige-Positionen, auf den Stream geklemmt) ein Aufruf `TTVideoStream::getSequenceHeader(pos)`: Anzeige-Position → Header-Index des Bildes → rückwärts bis zum Sequence-Header, also **Dekodier-Reihenfolge**. Ein führender B-Frame einer offenen GOP gehört damit zum **neuen** Header, obwohl er vor seinem I-Frame angezeigt wird (gemessen 2026-09-19: Wechsel auf Anzeigebild 22, I-Frame bei 24, deckungsgleich mit `temporal_reference`). Liest nur die Listen, bewegt den Stream-Index nicht. Kosten: 19–23 ms für alle 126 012 Bilder einer 84-min-Aufnahme — deshalb keine Lauflängen-Abkürzung. Nur `mpeg2_demuxed_video` mit Header- und Index-Liste, sonst leeres Ergebnis. |
| `ASPWIN → AINFO` | `TTAspectWindowInfo{mainAspect, cutInAspect, cutOutAspect, cutInTarget, cutOutTarget}` (MPEG-2-Codes, 2 = 4:3, 3 = 16:9). `mainAspect` = häufigster Wert; **exakter Gleichstand ⇒ `-1`, keine Ziele** (sonst gewänne am Cut-Out der Trailer). Ziel am Cut-In = erstes Bild `>= cutIn` mit `mainAspect`, am Cut-Out = letztes Bild `<= cutOut`, jeweils nur wenn der Rand selbst abweicht; bildgenau, auch wenn es ein B-Frame ist. Keine Abstandsgrenze. |
| `AINFO → ASPECT`, `AINFO → APREV`, `AINFO → FINAL` | Alle drei rufen dieselbe Funktion — Schnittliste, Vorschau und Warndialog können nicht auseinanderlaufen. `APREV` liest dabei den **Originalschnitt** (`mpOriginalCutList->at(segmentIdx / 2)`), nicht das Vorschau-Stück: die Vorschauliste hält nur die kurzen Stücke um jede Kante (zwei Einträge je Schnitt), deren Mehrheit bedeutungslos wäre. |
| `BURST → HINT`, `ASPECT → HINT`, `ACMOD → HINT` | Alle liefern ein `HintCell{text, tip}` zurück (leere Felder, wenn nichts zu melden ist) — kein Seiteneffekt auf das Widget. `aspectHint`: `Aspect start` / `Aspect end` / `Aspect start+end`, Tooltip je Rand mit beiden Formaten, Ziel-Frame und Abstand (`(+1)` / `(-2)`). |
| `HINT → COL5` | `updateHintColumn` komponiert **einmal**: Texte in der Reihenfolge Burst, Seitenverhältnis, acmod, mit `" + "` verbunden; Tooltips ebenso mit `"\n"`. Ein Burst+acmod-Fall liest sich dadurch genau wie vorher mit zwei Produzenten (Gate `hint_column` unverändert grün). Icon: Burst **oder** Seitenverhältnis ⇒ Warn-Icon (beide beschädigen die Ausgabe), sonst acmod ⇒ Info-Icon (der Schnitt normalisiert es), sonst keins. Schreibt Icon/Text/Tooltip **unbedingt** bei jedem Aufruf — deckt das Leeren der Zelle (auch im Kein-Audio-Ausstieg) ohne eigenen Sonderfall ab. |
| `APPEND -.-> HINT`, `REFRESH -.-> HINT` | Die drei Aufrufstellen (`onAppendItem`, `onUpdateItem`, `refreshHintIcons`) gehen **ausschließlich** über den Helper. `onAppend/onUpdate` bei Anlage/Änderung eines Cuts, inklusive Projekt-Laden (das appended). `refreshHintIcons` wird aus `TTCutMainWindow::openSettingsDialog` gerufen (von `onActionSettings` und dem Audio-Kategorie-Kurzweg aufgerufen) — **seit `9e5511f0` NUR bei „OK"** (`if (settingsDlg->exec() == QDialog::Accepted) { …; cutList->refreshHintIcons(); }`); bei „Abbrechen" laufen weder `TTSettings::save()` noch der Refresh mehr (vorher liefen beide unbedingt, siehe Gate `tools/diag/test_settings_cancel`). Tree-Reihenfolge == CutList-Reihenfolge, Zähl-Guard `qMin`. |
| `APPEND -.-> HINT`, `REFRESH -.-> HINT` | Die zwei Auslöser derselben Aktualisierung: `onAppendItem`/`onUpdateItem` bauen bzw. ändern eine Zeile und rufen `updateHintColumn` für genau diese, `refreshHintIcons` läuft über alle Zeilen — seit `9e5511f0` nur nach **Annahme** des Einstellungsdialogs, nicht mehr auch nach Abbrechen (Gate `test_settings_cancel`). |
| `HINT -.-> BURST` (1.), `HINT -.-> ASPECT` (2.), `HINT -.-> ACMOD` (3.) | Aufrufreihenfolge in `updateHintColumn()` — bestimmt nur die Kompositionsreihenfolge und die Icon-Priorität, nicht, ob ein Wert überschrieben wird: alle Rückgaben werden erst am Ende zusammengesetzt (siehe `HINT → COL5`). Alle Callees sind `private`. |
| `SEL -.-> PREV`, `SEL -.-> APREV` | Pro **ausgewähltem** Clip (`onCutSelectionChanged`, beide Prüfungen, danach `updateHintRowSpace`): `iCut == 0` ⇒ nur CutIn von Schnitt 1; sonst CutOut von Schnitt `iCut` (Priorität, `return`), danach CutIn von Schnitt `iCut+1`. Ein Fund je Übergang und Art. Kein globaler Überblick im Dialog. Burst in Grid-Zeile 2, Seitenverhältnis in Grid-Zeile 3, beide volle Breite. Der Burst-Knopf behält seinen Platz auch versteckt (seit `8ecf4cb0`, damit das Videobild beim Clip-Wechsel nicht springt) — **außer** solange die Seitenverhältnis-Zeile sichtbar ist: `updateHintRowSpace()` gibt die leere Burst-Zeile dann frei, die Meldung rückt an ihren Platz (Sichttest 2026-09-19 fand sie sonst unter einer Leerzeile). Zwei Zeilen nur bei Burst **und** Formatwechsel am selben Übergang. |
| `PREV -.-> MOVE`, `APREV -.-> MOVE` | `onBurstShift` (±1 Frame, mit `wouldInvert`-Prüfung) und `onAspectJump` (auf `mAspectTarget`; liegt per Konstruktion im Schnitt) rufen beide `moveCutEdge(segmentIdx, isCutOut, oldIdx, newIdx)`: sichert den Index des **geteilten** videoStream, `updateRealCutItem` (echtes Modell via `updateCutEntry`, Suche über die Position), `applyEdgeMoveToLists` (Kopie der Originalliste + Vorschau-Eintrag), `regeneratePreviewClip`, Index zurück. |
| `MOVE -.-> PREV`, `MOVE -.-> APREV` | `regeneratePreviewClip` ruft am Ende **beide** Prüfungen und `updateHintRowSpace` — ein verschobener Rand kann auch den Burst-Befund ändern. Die grüne Bestätigung gehört dem Aufrufer: „✓ Burst resolved" steht in `onBurstShift` (stand bis 2026-09-19 in `regeneratePreviewClip` und hätte nach einem Formatsprung einen nie vorhandenen Burst für behoben erklärt), „✓ Cut-in/Cut-out moved" in `onAspectJump`. |
| `CUTRUN -.-> FINAL` | `confirmCutWarnings()` hängt an **beiden** Cut-Pfaden in `TTAVData` (audio-only und Normalpfad); vor `27f8f29` existierte der Burst-Dialog dort doppelt. Sammelt über die reine Funktion `cutWarnings()` Burst- und Seitenverhältnis-Zeilen für die gesamte `TTCutList` (ein Dialog „Cut Warnings"). Der Burst-Teil prüft **je Schnitt**, ob dessen Item Audio hat; der Seitenverhältnis-Teil braucht kein Audio (bis 2026-09-19 stieg die Funktion für die ganze Liste aus, wenn der **erste** Schnitt kein Audio hatte). |
| `NONINT -.-> FINAL` | `--auto-cut` (`TTCutMainWindow::runAutoCutMode`, seit `ab3fae4d` in `gui/ttcutmainwindow_headless.cpp` statt `gui/ttcutmainwindow.cpp`) setzt `mNonInteractive = true` (`27f8f29`). Dann wird jede verbleibende Warnung via `TTMessageLogger::warningMsg` geloggt, plus eine Sammelzeile „N cut warning(s) - proceeding (auto-cut)" (Gate `aspect_autocut` sucht beide in `$XDG_CACHE_HOME/ttcut-ng/logfile.log`), und der Schnitt läuft weiter (Semantik = „Cut anyway"). GUI-Pfad (`false`) zeigt den modalen Dialog, „Cancel" bricht ab. Verhindert Hängen im Headless-Betrieb. |
| `PROBE -.-> DET` | `tools/ttcut-burst-probe` ruft `TTAudioCutter::detectBurst` **direkt** auf und umgeht damit beide Wrapper samt ihrem `minDelta <= 0`-Frühausstieg. **Genau deshalb** steht derselbe Guard ein zweites Mal am Anfang von `TTAudioCutter::detectBurst` („Callers short-circuit on <= 0 before opening the file; guard anyway"). |

## Annahmen & Verträge

- Detektor: Quell-Audio Track 0; boundaryTime in Sekunden der Quell-Zeitachse
  (Audio-Start = Video-Frame 0, ttcut-demux-Trim). Track 0 ist nach dem
  initialen Laden user-kontrolliert (siehe `SRC → DET`) —
  eine manuelle Umsortierung ändert also auch die Burst-Analyse-Spur.
- `burstMinDeltaDb == 0` schaltet die **Erkennung** ab (Frühausstieg vor dem Dateizugriff; im Settings-Tooltip dokumentiert).
- Der `minDeltaDb <= 0`-Ausstieg steht **zweimal**: in beiden Wrappern (spart den
  Dateizugriff) und als Guard gleich am Anfang von `TTAudioCutter::detectBurst`
  selbst (Kommentar dort: „Callers short-circuit on <= 0 before opening the file;
  guard anyway"). Der Guard greift für Direktaufrufer, die an den Wrappern
  vorbeigehen — `tools/ttcut-burst-probe` ruft `TTAudioCutter::detectBurst`
  unmittelbar auf.
- Der Detektor braucht **mindestens 3 RMS-Chunks**, sonst `false` + Warnung. Der
  „Median" ist `sorted[size/2]`, bei gerader Chunk-Zahl also das obere der beiden
  mittleren Elemente — für die Kontextschätzung unerheblich, beim Nachrechnen von
  `contextRmsDb` gegen eigene Messungen aber zu beachten.
- Spalte 5 wird nur über `updateHintColumn()` geschrieben, und zwar **unbedingt**
  bei jedem Aufruf (Icon/Text/Tooltip) — unabhängig davon, ob `burstHint`/
  `aspectHint`/`acmodHint` etwas melden. Ein eigener „leeren"-Sonderfall für den
  Kein-Audio-Ausstieg entfällt damit; das unbedingte Schreiben deckt ihn ab.
- `refreshHintIcons()` läuft seit `9e5511f0` nur noch, wenn der Einstellungs-
  dialog mit „OK" verlassen wurde (`TTCutMainWindow::openSettingsDialog`);
  ein „Abbrechen" ändert weder `TTSettings` noch Spalte 5.
- Preview-Dialog und Schnittliste zeigen IMMER dieselbe `present`-Entscheidung
  (gemeinsame Wrapper) — Diskrepanzen zwischen beiden UIs sind ausgeschlossen;
  „Icon fehlt" und „Warnung fehlt" haben zwangsläufig dieselbe Ursache. Dasselbe
  gilt für das Seitenverhältnis (eine Funktion, `ttAnalyzeAspectWindow`).

## Pitfalls

1. **Frequenz unbewertet**: Detektor misst breitbandiges RMS — unhörbare
   Anteile (Infraschall, >16 kHz) zählen mit. Für DVB-Programmton praktisch
   irrelevant; Follow-up K-Weighting (ITU BS.1770) im Spec
   `2026-07-04-burst-context-filter-design.md` dokumentiert.
2. **Zeitauflösung = ein Audio-Frame (AC3: 32 ms).** RMS wird pro dekodiertem
   Audio-Frame gebildet. Ein Transient von wenigen Millisekunden wird
   weggemittelt und bleibt unsichtbar, selbst wenn sein Sample-Peak 0 dBFS
   erreicht. Empirisch 2026-07-09: an beiden acmod-Wechseln in `TEST_deu.ac3`
   (83,808 s / 624,128 s) zeigt **weder RMS noch Sample-Peak** einen Ausschlag
   nach oben. **2026-09-22 aufgeklärt:** das Knacksen am Formatwechsel steht
   gar nicht im Ton — mpv reißt den Ausgabeweg ab und baut ihn neu auf, sobald
   die Kanalzahl mitten im Strom wechselt (drei AO-Öffnungen statt einer, zwei
   `drain timeout`). Gegen diesen Fall hilft kein Detektor und kein
   Verschiebe-Knopf; siehe `TODO.md`, Eintrag „Das Knacksen am
   AC3-Formatwechsel steht nicht im Ton".
3. **Nur die äußersten zwei Chunks (~64 ms) werden geprüft**, das Fenster
   spannt aber 200 ms. Alles weiter innen geht **ausschließlich in den
   Kontext-Median** ein.
4. **Ein ungeprüfter lauter Chunk verschlechtert die Erkennung aktiv.** Liegt er
   im Fenster, aber außerhalb des Prüfbereichs, hebt er den Median und damit die
   Latte, die die Randchunks reißen müssen. Der Detektor ist also genau dann am
   unempfindlichsten, wenn nebenan etwas Lautes liegt.
5. **`peak − median` trennt Ausreißer nicht von Pegelstufe.** Ein kurzer Klick
   und ein Werbe-Einsatz, der laut *bleibt*, feuern gleich (gemessen: 55-dB-Stufe
   bei 624,128 s in `TEST_deu.ac3`). Ein Nachbarschaftskontrast (laut, während
   die 1–3 Chunks **davor und danach** leise sind) könnte beides trennen — offene
   Design-Idee, siehe `TODO.md`.
6. **Das Absolut-Gate verwirft leise Bursts lautlos.** `kBurstAbsoluteFloorDb`
   (−40 dB) weist ab, egal wie weit der Chunk herausragt. Reale Werbe-Bursts der
   Referenzaufnahme liegen bei −37,5 / −27,3 / −36,5 dB — **zwei von dreien
   passieren das Gate um unter 4 dB**. Ein leiserer Sender wird stumm verfehlt.
   Absenken ist keine Option: bei −50 dB kämen 709 weitere Stellen derselben
   Aufnahme durch.
7. i18n: Burst-UI-Strings sind englische Sources mit deutscher Übersetzung.
8. **Ein einen Frame langer Schnitt hat keinen Spielraum für den Shift-Knopf.**
   `TTCutPreview::onBurstShift()` prüft seit `91e088ff` (Audit-Lauf 6), ob die
   Verschiebung das Cut-Ende über das Cut-In (oder umgekehrt) hinaus schieben
   würde (`wouldInvert`), und bricht mit einer Meldung „The cut is only one
   frame long …" ab, statt den Knopf wirkungslos erscheinen zu lassen.
9. **Die MKV-Spur übernimmt das Seitenverhältnis des ersten Bildes.**
   `TTMkvMergeProvider::setupVideoInput()` kopiert die von libav ermittelte SAR
   des Schnitt-ES in die Spur. Deshalb ist der Seitenverhältnis-Hinweis am
   Cut-In von Schnitt 1 der mit dem größten Schaden; ein Mehrheits-SAR im Muxer
   für absichtlich gemischte Ausgabe ist bewusst nicht Teil dieser Änderung.
10. **Die Vorschauliste enthält Stücke, keine Schnitte.** Wer in `TTCutPreview`
   etwas über einen ganzen Schnitt wissen will (Mehrheit, Länge), muss
   `mpOriginalCutList->at(segmentIdx / 2)` lesen (`originalCutItem`).
11. **clangd stürzt an Harnessen mit `ttavdata.h` ab** — `test_aspect_hint.cpp`
   eingeschlossen. `.clangd` sperrt nur den Hintergrund-Index; das Parsen nach
   dem Schreiben einer solchen Datei hinterlässt trotzdem einen Core-Dump im
   Wurzelverzeichnis (2026-09-19: 3 GB).

## Redundanz / Konsolidierungskandidaten

- Die Relativschwelle (`burstMinDeltaDb`) ist Parameter des Detektors; einen
  nachgelagerten Filter gibt es nicht — er könnte nur abweisen und wäre
  unterhalb der Detektorschwelle wirkungslos.
- `detectCutInBurst` und `detectCutOutBurst` sind bis auf
  boundaryTime-Berechnung und `isCutOut`-Flag identisch (Rest-Duplikat:
  Rahmencode der beiden Wrapper inkl. `minDelta <= 0`-Frühausstieg).
- Drei Konsumenten reimplementieren die „welcher Text/welches UI"-Logik
  (TreeView-Icon, Preview-Label, Final-Warndialog) über denselben zwei
  Wrappern — bei Filter-Änderungen alle drei Pfade gegentesten.
- `configureBurstShiftButton(isCutOut)` besitzt Beschriftung, Icon und Tooltip
  des Shift-Knopfs im Preview-Dialog als einzige Stelle; der Sprungknopf wird in
  `checkAspectForCurrentCut` beschriftet (Ziel-Frame ändert sich je Fund).
- **Rand-Verschiebung in der Vorschau**
  - sites: `onBurstShift`, `onAspectJump`
  - shared purpose: einen Schnittrand in Modell und Vorschaulisten verschieben,
    Clip neu erzeugen, geteilten videoStream-Index wiederherstellen
  - status: done 2026-09-19 — gemeinsam in `moveCutEdge`; `updateRealCutItem`
    und `applyEdgeMoveToLists` (vormals `applyBurstShiftToLists`) nehmen Rand und
    Position als Parameter statt `mBurstIsCutOut`/`mBurstSegmentIdx` zu lesen.
- **Clip-Neubau gegen die Vorschau-Task**
  - sites: `TTCutPreview::regeneratePreviewClip` (Dialog, EIN Clip) gegen
    `TTCutPreviewTask::operation`/`createH264PreviewClip` (Task, alle Clips)
  - shared purpose: Temp-Aufräumen, Quellauflösung aus Eintrag 0,
    Segment-Indexrechnung, Encoder-Aufbau, MKV-Mux-Konfiguration
  - status: done 2026-09-21 — die fünf Teile liegen als freie Funktionen in
    `data/ttpreviewclip.cpp`, die Ablaufsteuerung des Einzelclips als
    `ttRebuildMpeg2PreviewClip`/`ttRebuildSmartCutPreviewClip` daneben. Das war
    nötig, damit der Neubau ohne mpv und GL-Kontext läuft und damit prüfbar
    wird: Gate `preview_clip_h264`/`preview_clip_mpeg2`
    (`tools/diag/test_preview_clip.cpp`) hält einen neu gebauten Clip gegen
    den, den die Task für denselben Schnitt erzeugt hat. Der Tonschnitt bleibt
    getrennt (Option A, `audio-cut-timing.md`); das Gate misst den Abstand,
    statt ihn wegzudefinieren.
- **Seitenverhältnis-Text dreifach**
  - sites: (ehemals) `ttAspectText()` (`avstream/ttaspectwindow.cpp`),
    Ternär-Ketten in `TTStreamPointVideoWorker::detectAspectChanges()`,
    `TTSequenceHeader::aspectRatioText()`
  - shared purpose: MPEG-2-`aspect_ratio_information` → „4:3"/„16:9"
  - status: done 2026-09-19 — eine Tabelle, `static TTSequenceHeader::aspectText(int)`
    (`avstream/ttmpeg2videoheader.cpp`, unterste Ebene: Harnesse, die nur den
    Header-Parser linken, brauchen nichts weiter). `aspectRatioText()`, der Worker
    und die drei Seitenverhältnis-Konsumenten rufen sie.
- **Append-Semantik über das Widget**
  - sites: `TTCutTreeView::updateBurstIcon` / `updateAcmodIcon` (vor `ada97fd2`)
  - shared purpose: zwei Produzenten schreiben nacheinander in dieselbe Zelle
  - status: done `ada97fd2` — `burstHint`/`acmodHint` sind jetzt reine Funktionen,
    die `{text, tip}` zurückgeben, ohne das Widget zu berühren; `updateHintColumn()`
    komponiert Text/Tooltip/Icon aus beiden Rückgaben und schreibt Spalte 5 einmal.
    Die Aufrufreihenfolge (Burst zuerst) bestimmt nur noch Text-/Icon-Priorität im
    Kompositionscode, nicht mehr, ob ein Wert überschrieben wird — kein Vertrag
    zwischen zwei Setzern mehr, siehe Kante `HINT → COL5`.
- **acmod-Mehrheitslogik doppelt implementiert**
  - sites: (ehemals) `TTAudioCutter::analyzeAcmod` (Sync-Word-Dateiscan, feste
    32-ms-Frame, Cut-Normalisierung `targetAcmods`), `TTCutTreeView::updateAcmodIcon`
    (In-Memory-`TTAudioHeaderList`, abweichende Sampling-Regel, Anzeige)
  - shared purpose: „Mehrheits-acmod aus ~100 Randframes" pro Segment bestimmen
  - status: done `ada97fd2` — beide durch `ttAnalyzeAcmodWindow` (`avstream/
    ttac3acmod.cpp`) ersetzt, `TTAudioCutter::analyzeAcmod` + `AcmodInfo` entfernt
    (114 Zeilen). Der alte Dateiscan hatte bei Fenstern < 100 Frames einen echten
    Fehler: sein Cut-Out-Samplebereich begann vor dem Cut-In, sodass Cut-In-acmod
    und Mehrheit teils von außerhalb des Fensters stammten und ein kurzes Segment
    auf die Nachbarschaft statt auf sich selbst normalisiert werden konnte — mit der
    gemeinsamen Funktion behoben (7/7/7 statt fehlerhaft, Kurzfenster-Testfall).
    Gates: `test_acmod_majority` (520 Fenster ≥100 Frames gegen den alten Dateiscan
    als Referenz, keine Abweichung), `gate_cut_identity.sh` (fünf Fixtures identisch),
    `test_hint_column` (neu: Hinweis-Spalte „AC3 start/end/start+end", Tooltip nennt
    beide Modi).
- Der Final-Warndialog liegt einfach in `confirmCutWarnings()` (Sammeln in der
  reinen Funktion `cutWarnings()`), mit GUI/headless-Verzweigung über
  `mNonInteractive`. Verbleibendes Duplikat sind allein die zwei Detektor-Wrapper.
