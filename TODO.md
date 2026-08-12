# TTCut-ng TODO / Feature Requests

Offene Punkte und bekannte Einschränkungen. Erledigtes steht mit seinen
Belegen in [docs/completed-work.md](docs/completed-work.md).

## High Priority

- **SIGSEGV nach Smart Cut in `doH264Cut` — Use-after-free in Qt-Model-Internals
  (2026-08-07, vertagt auf User-Entscheidung; blockiert den Merge von
  `feature/progress-details`)**
  - Absturz bei der GUI-Abnahme: Öffnen (18:34) → Vorschau (18:36:38) →
    Schnitt-Dialog → GUI-Schnitt einer 85-min-H.264-Aufnahme; Crash ~18:38,
    direkt nach „Smart Cut complete" (Log endet dort), in der
    Audio-/Folgephase von `doH264Cut`.
  - **Forensik (Core `core.500359`, 2 GB, Build-ID passt zu `bb553cd6`):**
    Absturz-Thread: `QAbstractItemModelPrivate::invalidatePersistentIndexes`
    ← `QAbstractProxyModelPrivate::_q_sourceModelDestroyed` ← (Frames
    fehlen/kollabiert) ← `TTAVData::doH264Cut` (attribuiert ~ttavdata.cpp:1637,
    -O2-Inlining, ±). Der Proxy-Private-Block ist **freigegebener,
    wiederverwendeter Speicher**: ungültiger vtable-Zeiger, `persistent.indexes`
    mit m_size 2 951 281, „Source-Model"-Zeiger zeigt in denselben Block.
    Backtrace-Dump: `/usr/local/src/CLAUDE_TMP/TTCut-ng/core500359_bt.txt`.
  - TTCut-Code enthält **kein einziges** Proxy-Model und keinen QCompleter
    (Grep 2026-08-07) — das Proxy ist Qt-intern. Direkt vor `onDoCut` wird
    `TTCutAVCutDlg` per `delete` zerstört (`ttcutmainwindow.cpp:1151`), davor
    der Vorschau-Dialog (`delete cutPreview`, `:1100`).
  - **Zuordnung offen:** Der Branch fasst keine Models an; dieselbe
    Use-after-free-Klasse trat am 2026-08-01 vorbestehend auf (qtwayland,
    → Memory `reference_core_dump_forensics`). Diese Bediensequenz
    (Vorschau → Cut-Dialog → langer GUI-Schnitt) läuft unter Qt6 erst seit
    dem 04.08. — Ursache kann im Branch, in der Qt6-Migration oder in Qt
    selbst (6.10, Debian sid) liegen.
  - **ASAN-Lauf 2026-08-09 (Branch-Stand `8b156cb4`, RelWithDebInfo +
    `-fsanitize=address`): SAUBER.** Identifizierte Absturz-Datei
    (`03x01_-_Drunter_und_drüber.264`, per Core-Strings belegt — die
    „85 min" oben waren grob, real 75 min), komplette Sequenz Öffnen →
    Vorschau → Cut-Dialog → GUI-Schnitt → Warten nach „Smart Cut
    complete": kein ASAN-Report, kein Core, Mux komplett. Zusätzlich
    lief die gesamte GUI-Abnahme am 2026-08-09 (>10 Öffnen-/
    Schnitt-Zyklen, unintstrumentiert) absturzfrei. Der Befund stützt
    „timing-abhängig, Qt-intern" (gleiche UAF-Klasse trat am 2026-08-01
    vorbestehend auf, qtwayland). `core.500359` und der ASAN-Build
    wurden am 2026-08-09 auf User-Entscheid gelöscht (veralten mit dem
    nächsten Build); der Backtrace-Dump
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/core500359_bt.txt` bleibt.
    Bei Wiederauftreten: Core sichern, Forensik-Referenz nutzen,
    ASAN-Build neu erzeugen (`cmake -B build-asan -G Ninja
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address
    -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`).

- **H.264 gemischt MBAFF+PAFF (08x04-Korpus) — verbleibende Befunde**
  (Wurzel — TS↔ES-AU-Nummerierungs-Drift der es_extra_frames — GELÖST 2026-07-19,
  siehe Spec `docs/superpowers/specs/2026-07-19-es-extras-field-awareness-design.md`):
  - ~~**Befund B — Decode-Hänger** beim Navigieren auf ein PAFF-Feldpaar-AU~~
    → **GEFIXT 2026-07-19** (`46d3dcb`): Index-Adopter erben jetzt den
    PAFF-Zustand des Owners (`adoptStreamMetadata`); Diag `test_adopt_paff`.
    Restpunkte: die Crash-Variante (SIGABRT in `avcodec_send_packet`) ist als
    Folge des beseitigten EOF-Drains plausibel, aber nicht formal bewiesen
    (GUI-Soak ohne Crash bestanden). **Der Core-Dump `core.456277` ist am
    2026-07-31 gelöscht worden** — ein Backtrace wäre ohnehin unbrauchbar
    gewesen, weil das Binary seit dem 19.07. vielfach neu gebaut ist und keine
    passenden Symbole mehr existieren. Nachprüfbar also nur noch über einen
    neuen Repro-Lauf auf dem 08x04-Korpus, nicht mehr aus vorhandenem Material.
    PAFF-**Playback**-Fehler beim Play (mpv `reference picture missing during
    reorder`) besteht fort — separates Follow-up (libmpv Phase 2).
  - ~~**Befund E — Smart-Cut-Re-Encode liefert uniform graue Frames**~~
    → **GEFIXT 2026-07-19** (`8dfda6d`): der SPS-Unification-Rewriter
    schrieb/las die CABAC-Alignment-Bits unbedingt; endete der umgeschriebene
    Slice-Header exakt byte-aligniert (08x04: 42+6 = 48 Bits am ersten IDR),
    schob ein falsches 0xFF-Byte die Payload weg → Slice still verworfen,
    Frame grau concealed, bf=0-P-Frames trugen das Grau bis zum Copy-IDR.
    Alignment jetzt spec-bedingt (H.264 7.3.4) auf Lese- und Schreibseite;
    Details in `docs/code-map/smart-cut.md`, Diag `tools/diag/test_feed_decode`.
  - ~~**Befund D — H.264/H.265-Standbild-Aspect fehlt**~~ → **GEFIXT 2026-07-19**:
    `showVideoFrame()` korrigiert im FFmpeg-Zweig jetzt jeden SAR≠1:1 in
    Upscale-Richtung (Breite×SAR, z.B. 720×576 SAR 16:11 → 1047×576), Quelle
    `TTFFmpegWrapper::sampleAspectRatio()` (Codec-Kontext, codecpar-Fallback).
    MPEG-2-Pfad unverändert. Spec
    `docs/superpowers/specs/2026-07-19-h26x-still-aspect-design.md`,
    Diag `tools/diag/test_sar`.

- **Logo für TTCut-ng**
  - Projekt braucht ein wiedererkennbares Logo/Icon für GitHub, Debian-Paket, Desktop-Launcher
  - Anforderungen: SVG (skalierbar), funktioniert als 16x16 bis 512x512, passt zu Video-Editing

## Medium Priority

- **Vorbestehende Defekte, gefunden beim Abbruch-Vorhaben (2026-08-10)** —
  keiner davon wurde von `feature/cut-abort` verursacht, alle sind dort beim
  Lesen bzw. Messen aufgefallen und bisher nur in den SDD-Berichten
  festgehalten. Reihenfolge grob nach Nutzerwirkung.
  - **Eine echte `TTException` aus einer Schnitt-Aufgabe wird als „Cut
    cancelled" gemeldet** und löst kein `cutFinished()` aus: ein echter
    Fehler sieht aus wie ein Nutzer-Abbruch, und ein `--auto-cut`-Lauf auf
    diesem Pfad wartet ewig.
    **Wurzel gefunden 2026-08-12:** `TTThreadTask::run()`
    (`common/ttthreadtask.cpp:171-189`) fängt beide Ausnahmearten getrennt —
    `catch(TTAbortException&)` und `catch(TTException&)` — und sendet aus
    **beiden** dasselbe Signal `aborted(this)`. `TTAbortException` erbt von
    `TTException` (`common/ttexception.h:93`); die Unterscheidung liegt also
    im Typsystem und im Kontrollfluss vor und wird genau dort weggeworfen,
    wo sie gebraucht würde. Behebung: ein zweites Signal
    (`failed(task, grund)`), im unteren `catch` gesendet — klein zu ändern,
    aufwendig zu prüfen, weil `TTThreadTask` die Basisklasse **jeder**
    Aufgabe ist (Öffnen, Suche, Analyse, Schnitt, Vorschau, Mux). Die
    Aufrufer-Seite ist seit `c7436a07` sauber: alle Schnittpfade melden ihr
    Ergebnis über `TTAVData::finishCutOperation()`.
  - **`TTThreadTask::abort()` sendet `aborted()` selbst dann**, wenn der
    Abbruch eintrifft, während `finished()` schon in der Warteschlange
    liegt — eine tatsächlich fertig gewordene Aufgabe kann sich damit als
    abgebrochen melden. Generisch, betrifft jede Aufgabe im Pool.
  - **Der MPEG-2-Neucodierer ist von Lauf zu Lauf nicht reproduzierbar**
    (`thread_count = 0`; zwei verschiedene Video-ES in sechs Läufen
    **desselben** unveränderten Binaries). Für die Ausgabequalität harmlos,
    aber es schließt den Byte-Vergleich als Prüfkriterium auf diesem Pfad
    aus — festgehalten, damit niemand erneut eines baut.
    `tools/diag/qc-autocut.sh` überspringt die Videospur deshalb bei
    mpeg2video und prüft dort nur Paketzahl, Dauer und Audiospur.
  - **`runEncodePass`s Fehlerpfad bei `av_packet_alloc`** liefert `true`
    zurück, nachdem die Bildliste bereits teilweise verarbeitet wurde —
    der Rest leckt und das Segment wird still abgeschnitten. Nur bei einer
    fehlgeschlagenen Speicheranforderung erreichbar.
  - **Arbeiter-Fäden lesen den `TTSettings`-Singleton direkt** — latent, kein
    Datenrennen im heutigen Programm, aber die Begründung dafür steht
    nirgends im Code. Warum es heute sicher ist: alle benutzten Zugriffe
    sind triviale Inline-Feldlesungen (`normalizeAcmod()`,
    `cutDirPath()`, `workingMkvCreateChapters()`,
    `workingMkvChapterInterval()`, `workingMuxDeleteES()`,
    `logCutPipeline()`); **keiner** fasst `QSettings` an — das wird nur in
    `TTSettings::load()`/`save()` geöffnet, und der einzige Speicher, der
    pro Aufruf öffnet, ist `TTCalibrationStore`, den ausschließlich der
    GUI-Faden benutzt. `QThreadPool::start()` liefert die
    Happens-before-Kante, die das fertig gebaute Objekt im Arbeiter sichtbar
    macht (der Singleton selbst ist ein ungeschützter Lazy-Zeiger, wird aber
    beim Programmstart im GUI-Faden gebaut). Und jeder Schreiber dieser
    Felder sitzt hinter einem Dialog, der zu diesem Zeitpunkt geschlossen
    ist, bzw. hinter dem Hauptfenster, das von `Init` bis `Exit`/`Canceled`
    genau für die Lebensdauer des Arbeiters deaktiviert ist.
    Latent bleibt es, weil dieses Sicherheitsargument eine **nirgends
    erzwungene äußere Zusicherung** ist und `cutDirPath()` ein `QString`
    zurückgibt — eine nebenläufige Zuweisung dazu ist undefiniertes
    Verhalten, kein zerrissener `int`. Ein nicht-modales Seitenpanel, ein
    Hintergrund-Timer oder ein Settings-Schreibvorgang aus einem
    Status-Slot macht daraus ein echtes Rennen mit einer Absturzsignatur,
    die kein headless-Harness reproduziert.
    **Nicht** durch Kopieren der fünf Felder nach `TTH26xCutParams`
    schließen: `TTH26xCutTask` ist kein Ausreißer — `data/ttcutpreviewtask.cpp`
    liest denselben Singleton aus seinem Arbeiter an **30** Stellen,
    `data/ttopenvideotask.cpp` an 2. Eine Task allein umzubauen erkauft nur
    den Anschein einer Disziplin, die es im Code nicht gibt. Angemessen ist
    ein projektweiter Schritt in eigener Änderung: entweder eine
    dokumentierte, geprüfte Regel („Arbeiter-Code liest keinen Singleton",
    erzwingbar über eine Faden-Zugehörigkeitsprüfung in
    `TTSettings::instance()`) — oder es bewusst so lassen und hier stehen
    haben.

- **Teilfehlschläge beim Spurenschnitt werden nur auf einem von drei Pfaden
  erkannt** (Schlussprüfung `feature/cut-outcome-reporting`, 2026-08-12)
  - `TTAVData::cutAudioTracks()` überspringt eine fehlgeschlagene Spur intern
    still (`continue` bei Index außerhalb des Bereichs, fehlendem Stream oder
    leerem Schnittplan - `ttavdata.cpp:2731`, `:2734`, `:2741`) und meldet das
    nach außen nur über die Größe der zurückgegebenen Dateiliste. Nur der
    Audio-only-Pfad (`data/ttaudioonlycuttask.cpp`, seit `c7436a07`)
    vergleicht `trackFiles.size()` gegen die angeforderte Spurenzahl und
    setzt bei Abweichung `mError`/`mExitMessage` ("Only %1 of %2 audio
    track(s) could be cut"). Die beiden anderen Aufrufer prüfen das nicht:
    `TTAVData::onDoCut()` beim MPEG-2-Pfad (`ttavdata.cpp:1566`) und
    `TTH26xCutTask::runCut()` (`data/tth26xcuttask.cpp:329`) melden Erfolg,
    auch wenn eine oder mehrere Spuren nie geschnitten wurden.
  - Folge: Beide Pfade schreiben trotzdem einen Kalibrierfaktor für die
    Restzeitschätzung, obwohl die zugrunde liegende Arbeitsmenge Spuren
    enthielt, die tatsächlich nie bearbeitet wurden - der Faktor wird auf
    einer falschen Basis berechnet.
  - Vorlage für die Behebung ist der Audio-only-Pfad selbst: dieselbe
    Größenvergleich-Prüfung (Rückgabeliste vs. angeforderte Spurenzahl, s.o.)
    müsste an beiden übrigen Aufrufstellen ergänzt und über
    `finishCutOperation()` gemeldet werden. Nicht in `feature/cut-outcome-
    reporting` behoben - hätte den Zweig gesprengt.

- **Gemeinsame Temp-Namen in `encodePart()`** (2026-08-12, erledigt)
  - `TTMpeg2VideoStream::encodePart()` legte `encode.avi`/`encode.m2v` direkt
    unter `TTSettings::tempDirPath()` an und räumte am Ende jede `encode.*`
    dort weg — auch die einer gleichzeitig laufenden Instanz. Vier
    gleichzeitige `test_mpeg2cut_abort`-Läufe scheiterten 3–5 von 12 Malen mit
    Speicherabzug; einzeln liefen rund 60 Durchgänge fehlerfrei.
  - Der Absturz selbst kam aus `TTVideoHeaderList::firstSequenceHeader()`: die
    Schleife griff mit `at(index)` zu, bevor sie `index < count()` prüfte.
    Nachdem sie `NULL` lieferte statt abzubrechen, wurde daraus ein
    Nullzeiger-Zugriff in `encodePart()` (8 von 12 Läufen, rc=139) — beides
    ist behoben, die Funktion prüft `getSequenceHeader()` jetzt.
  - **Richtigstellung zum früheren Eintrag:** Der Absturz passiert *nicht*
    nach dem PASS. Er trifft den Kontrolllauf („restart after cancel") mitten
    im Video-Schnitt, Rückgabewert 134. Die Annahme „meldet PASS, stirbt beim
    Beenden" war eine Deutung ohne Beleg — ein Speicherabzug nennt die Phase
    nicht.
  - Gate: `tools/diag/gate_encode_tempdir.sh` (vorher `failed=8 dumps=5`,
    nachher `failed=0 dumps=0`), dazu `tools/diag/test_seqheader_missing`
    (vorher rc=134, nachher NULL).

- **`test_previewcut_abort` scheitert mit MPEG-2-Material** (2026-08-12,
  offen, beim Abschluss der Temp-Kollisions-Arbeit gemessen)
  - Mit `tux_mpeg2_576i_pal_test.m2v`/`.mp2` scheitern zwei der vier Fälle:
    `none` mit „cutPreviewFinished never fired" (Klammern `init=1 exit=0
    cancel=1`, Text „Preview cancelled", im Protokoll ein `CutPreviewTask ...
    catched TTException`) und `fail` mit „a real failure was reported as
    Canceled". `video` und `audio` laufen durch.
  - **Vorbestehend, keine Folge des Temp-Fixes**: auf `67e21d83` in einem
    eigens gebauten Vergleichs-Worktree dieselben zwei Fehlschläge mit
    denselben Meldungen.
  - Mit H.264-Material (`tux_h264_1080p_progressive_test.264`/`.ac3`) laufen
    alle vier Fälle durch — der Harnisch ist bisher offenbar nur damit
    gefahren worden.
  - Zwei mögliche Lesarten, ungeprüft: Der Harnisch trifft für MPEG-2 die
    falschen Erwartungen (dann gehört das in seinen Kopfkommentar), oder der
    Vorschau-Abbruch meldet auf dem MPEG-2-Pfad einen echten Fehler als
    Abbruch (dann ist es derselbe Fehlerbild-Typ wie beim Schnitt-Ausgang,
    siehe `project_cut_outcome_reporting`). Erster Schritt: nachsehen, welche
    `TTException` der `CutPreviewTask` dort fängt.

- **Weitere geteilte Temp-Namen** (2026-08-12, offen, niedrige Priorität)
  - Dieselbe Bauform steht noch an drei Stellen: `gui/ttcutpreview.cpp` und
    `data/ttcutpreviewtask.cpp` (Vorschau-Dateien) sowie
    `gui/ttcurrentframe.cpp:970` (`ttcut-ng_playback_temp.mkv`). Zwei
    gleichzeitig offene Fenster benutzen dieselben Namen.
  - Kein gemessener Fehlerfall — deshalb nicht mitgefixt. Wer es angeht:
    dasselbe Muster wie in `encodePart()` (`QTemporaryDir` je Vorgang).

- **Landezonen-Analysen, die gar nicht erst starten, erklären sich nicht**
  (Nachfolgepunkt der Detailausgabe, Schlussprüfung 2026-08-11)
  - Seit `88b911f9` sagt jede der drei Analysen im Detailbereich, warum sie
    nichts finden konnte — aber nur, wenn ihr Worker überhaupt gebaut wird.
    `TTCutMainWindow::onAnalyzeStreamPoints()` legt einen Worker nur bei
    erfüllter Voraussetzung an: keine Header-Liste (also **jedes**
    H.264/H.265-Material für die Sequenzkopf-Analyse), keine Tonspur, leere
    Indexliste. In diesen Fällen schweigt der Bereich zu der Analyse
    vollständig — genau die Ununterscheidbarkeit „nichts gefunden" gegen
    „lief gar nicht", die sonst beseitigt ist.
  - Sichtbare Folge: Der eigens dafür geschriebene Text „Seitenverhältnis:
    kein MPEG-2-Strom – übersprungen" (`ttstreampoint_videoworker.cpp`) ist
    aus der Oberfläche **unerreichbar**; er wird nur von
    `tools/diag/test_streampoint_order` ausgelöst. Das Änderungsprotokoll
    kündigt ihn als Fall an, den der Nutzer sieht.
  - Warum es kein Einzeiler ist: Die Meldung müsste **vor** dem ersten
    `Start` gesendet werden, und `TTCutMainWindow::onStatusReport` verwirft
    alles, solange `progressBar == 0` — der Balken wird erst im `Start`-Zweig
    angelegt. Ein Fix greift damit in die Fortschrittskette selbst ein, nicht
    in die Worker. Denkbar: die Zeilen sammeln und nach dem ersten `Start`
    des ersten tatsächlich gebauten Workers nachreichen; läuft **kein**
    Worker, erscheint heute ohnehin nur der Hinweis „Keine Erkennungsmethode
    aktiviert".
  - Beschrieben in `docs/code-map/detection-and-search.md`, Abschnitt „Was
    der Detailbereich erfährt".

- **Fortschrittszeilen fluten den Detailbereich** (User-Befund 2026-08-12 bei
  der Abnahme der Detailausgabe; bewusst zurückgestellt)
  - Der Bildformat-Scan meldet alle 20 Proben einen Zählerstand, bei 5452
    geplanten Stichproben also **272 Zeilen** je Lauf. Die wenigen
    aussagekräftigen Zeilen gehen darin unter. Der Mehrwert ist gering:
    „4360 von 5452" beantwortet, was Balken und Prozentanzeige daneben
    bereits zeigen; im Protokoll bleibt allein der Zeitstempel.
  - Ursache: `TTProgressBar::onSetProgress` hängt jede `Step`-Meldung als
    Detailzeile an und unterdrückt Wiederholungen nur bei **identischem
    Text** (`mLastStepMsg`). Ein mitlaufender Zählerstand ändert den Text bei
    jeder Meldung, also greift die Unterdrückung nie. Sie wurde für
    Meldungen wie „Segment 3 von 5" gebaut, die minutenlang gleich bleiben.
  - Vorgeschlagener Weg (nicht umgesetzt): Die drei Analyse-Tasks melden als
    Text konstant „Bildformat wird geprüft" und führen den Zählerstand nur
    noch im Zahlwert mit. Dann greift die vorhandene Unterdrückung — eine
    Zeile statt 272 —, Balken und Prozentanzeige bleiben unberührt, und der
    Eingriff beschränkt sich auf die drei Tasks. Preis: Die Aktionszeile über
    dem Balken zeigt keinen Zählerstand mehr, nur noch der Balken selbst.
  - Verworfen wurden: Step-Meldungen generell nicht mehr protokollieren
    (beträfe jede Operation, auch Schnitt und Muxen, wo die Verlaufszeilen
    gelesen werden) und seltener melden (macht den Balken ruckelig und
    verändert die Meldungsfolge, deren Unverändertheit aufwendig belegt ist).

- **ttcut-demux: bash + ffmpeg-CLI → libav-Library-Migration**
  - `tools/ttcut-demux/ttcut-demux` ist aktuell ein bash-Script (~1800 Zeilen) das ffmpeg-CLI-Subprozesse spawnt für: TS-Demux, Audio-Trim, Audio-Padding, Audio-Gap-Repair, PTS-Analyse, etc.
  - Der Rest der TTCut-ng-Pipeline ist bereits auf libav umgezogen (v0.60.0): cutAudioStream(), TTMkvMergeProvider, TTFFmpegWrapper, etc. — kein ffmpeg-CLI mehr (nur noch mplex für MPEG-2-Multiplex).
  - ttcut-demux blieb auf bash+CLI hängen.
  - **Probleme**: stream-copy concat über libav-CLI ist fragil bei mp2/ac3 Splice-Punkten (Frame-Misalignment, Header-missing-Errors). Re-encode als Workaround funktioniert (siehe Audio-Gap-Fix 2026-05-10), aber libav-direkt wäre PTS-genauer und ohne Subprocess-Overhead.
  - **Migration-Pfade**:
    1. ttcut-demux nach C/C++ portieren (vollständiger Rewrite, nutzt libav direkt)
    2. Audio-Gap-Detection + Repair in TTCut-ng integrieren (load-time statt demux-time)
    3. Hybrid: bash-Skelett bleibt, kleine C-Helfer für PTS-Analyse + Audio-Splice via libav
  - **Scope**: mehrtägig, separater Refactor.

- **Bit-Stream API in extern/ vereinheitlichen**
  - `extern/ttessmartcut.cpp` hat eigene file-lokale Bit-Primitives (`spsReadBits`,
    `spsWriteBits`, `spsReadUE`, `spsWriteUE`, `spsReadSE`, `spsWriteSE`,
    `skipScalingList`) für SPS-Patching mit Read+Write-Pfad. Andere Caller
    (`ttffmpegwrapper.cpp`, `ttmkvmergeprovider.cpp`) nutzen die nur lesenden
    `TTNaluParser::readBits` / `readExpGolombUE` / `readExpGolombSE`.
  - Folge: SPS-Bit-Skipping-Block (chroma, bit_depth, scaling lists) ist 4×
    dupliziert (siehe code-review-2026-05-01/02-extern.md MEDIUM-2). Die
    Predicate-Hälfte ist konsolidiert (`TTNaluParser::isH264HighProfile`),
    aber die Bit-Skipping-Logik selbst kann erst zusammengelegt werden, wenn
    beide APIs unifiziert sind — entweder TTNaluParser um Write-Primitives
    erweitern, oder die ttessmartcut-locals als file-scope-statics in einen
    Shared-Header ziehen.
  - Risiko: SPS-Patching ist heißer Pfad bei PAFF/MBAFF Smart Cut → erst
    abdeckende Tests bauen, dann unifizieren.

- **CLI Interface for batch Smart Cut (headless mode)**
  - Teilweise abgedeckt: `ttcut-ng --project <file> --auto-cut <out.mkv>` lädt ein `.ttcut`-Projekt
    und führt Smart Cut + Audio + MKV-Mux headless aus (für QC-Regression). Es bleibt aber die
    Qt-GUI-Anwendung — echte X11/Wayland-Freiheit fehlt.
  - Burst-Warndialog-Blocker BEHOBEN (v0.72.0, `27f8f29`): der modale Burst-Warndialog am finalen
    Schnitt wird im headless `--auto-cut`-Modus geloggt statt zu blockieren (`setNonInteractive`).
  - Selbstbeendung BEHOBEN (v0.78.0, `9da00f13`/`4071cc3d`): `--auto-cut` endet jetzt von
    selbst — bei Erfolg wie bei Fehlschlag, für MPEG-2 wie für H.264. Ein Wächter-Wrapper,
    der auf eine stabile Ausgabedatei wartet und den Prozess killt, ist nicht mehr nötig.
  - Offen: echtes Qt-freies Standalone-Tool, das `.ttcut` liest und ohne GUI-Event-Loop schneidet —
    läuft dann auch auf reinen Servern. Use case: VDR → demux → TTCut-ng CLI → archive

- **LipSync-Prüfdialog: A/V-Versatz objektiv messen und übernehmen** (Idee 2026-07-05)
  - Ziel: den Audio/Video-Versatz einer Aufnahme objektiv bestimmen und als
    Per-Track-Audio-Delay (bestehendes `mAudioDelayMs`, v0.66.0) übernehmen —
    statt ihn per Gehör am Regler zu schätzen. Belegt 2026-07-05: an
    Sprecherszenen ist der Höreindruck selbst bei 400 ms Versatz unzuverlässig.
  - UI: bevorzugt den **bestehenden Zeitsprung-Dialog erweitern** statt einen
    neuen Dialog zu bauen — er ist bereits der Thumbnail-Szenen-Browser, den man
    zum Szenenfinden braucht. Idee: ein „LipSync hier messen"-Aktion/Knopf am
    ausgewählten Thumbnail. Workflow mit Anleitung: geeignete Szene finden →
    messen → gemessenen Versatz in den Audio-Delay der Spur übernehmen.
    (Detail-Entscheidung — eigener Dialog vs. Zeitsprung-Erweiterung — beim
    echten Design klären.)
  - Messmethode (2026-07-05 erarbeitet, siehe Memory `reference_lipsync_measurement`):
    Lippenabstand pro Frame (innere Ober-/Unterlippe) gegen den Tonverlauf.
    **WICHTIGE Lektion:** Voll-Signal-Kreuzkorrelation über Dauer-Sprache
    konvergiert NICHT — nur der ereignisbasierte Abgleich EINES sauberen
    Verschluss-Ereignisses (Mund komplett zu ↔ Ton-Delle, bilabiales b/p/m)
    liefert einen belastbaren Wert. Der Dialog muss den Nutzer gezielt zu so
    einer Stelle führen.
  - Anleitung zur Szenenwahl (in den Dialog): Sprecher-Nahaufnahme mit klarem
    Sprechbeginn nach Pause; UNGEEIGNET sind Geräte-Bedien-Szenen (das gefilmte
    Gerät reagiert selbst verzögert → falscher Anker) und ruhige Halbprofil-
    Szenen ohne klares audiovisuelles Ereignis.
  - Offene Abhängigkeitsfrage: der Prototyp nutzt mediapipe (Python/pip, NICHT
    in Debian) + rhubarb. Für ein auslieferbares Feature bräuchte es entweder
    eine C++/libav-native Lippendetektion, ein gebündeltes Modell, oder das
    Feature bleibt optional (nur aktiv, wenn die Tools vorhanden sind).
    Prototyp-Werkzeuge unter `/usr/local/src/CLAUDE_TMP/TTCut-ng/`
    (`lip_landmark.py`, `venv-mp/`, `lip_final.png`).
  - Synergie: die Landezonen-Infrastruktur (libavfilter, silencedetect) könnte
    Kandidaten-Szenen vorschlagen (Sprechbeginn nach Stille = silencedetect-Kante).

- **Doppelte Mehrheits-acmod-Logik** (Folge-Fund aus den Dead-Code-Audits, kein
  toter Code)
  - `analyzeAcmod()` (Datei-Scan per Syncword, dient der Cut-Normalisierung) und
    `TTCutTreeView::updateAcmodIcon()` (In-Memory-`TTAudioHeaderList`, dient der
    Anzeige) implementieren dieselbe Mehrheitsauswahl doppelt, mit
    verschiedenen Stichprobenbereichen → sie können verschiedene `mainAcmod`
    liefern.
  - `updateAcmodIcon()` liest `text(5)`/`toolTip(5)`/`icon(5)` aus dem
    Tree-Widget zurück, um seinen Text anzuhängen: das Widget dient als
    Zwischenspeicher zwischen zwei Produzenten. `updateHintColumn()` kapselt die
    Reihenfolge seit `666ed08`, beseitigt die Append-Semantik aber nicht.
    Sauberer: beide liefern `{icon, text, tooltip}` zurück, ein Setter
    komponiert und schreibt einmal.
  - Wiederkehrender Audit-Lauf selbst: Skill `dead-code-audit` invoken.

- **MP3/AAC re-encoding für Audio-Only-Output**
  - `audioOnlyBitrateKbps` Setting im Code vorhanden, UI ausgeblendet (v0.70.0)
  - Code-Stelle `TTAVData::doAudioOnlyCut` (data/ttavdata.cpp) warnt "not implemented yet"
  - Bei Implementation: `sbAudioOnlyBitrate`-UI wieder einblenden

- **Batch-Mux-Workflow per CLI für alle Codecs**
  - `TTMplexProvider::writeMuxScript()` ist heute nur via mplex/MPEG-2 erreichbar
  - Erweitern auf MKV (libav matroska muxer) — z.B. via `--auto-cut`-CLI-Flag
  - Bezug: erörtert bei Obsolete-Removal-Brainstorm 2026-05-15

- **Custom MKV Chapter Editor**
  - Dialog mit Liste editierbarer Kapitel: Zeitstempel (hh:mm:ss.zzz), Name, Sprache
  - Vor-Populierung aus Cut-Ins (jeder Cut-In wird Default-Kapitel)
  - Persistenz in `.ttcut`-Projektdatei
  - Die Intervall-basierte Auto-Generierung (`cbMkvCreateChapters` + `leChapterInterval`) im Muxer-Tab bleibt als einfacher Default bestehen
- Internationalisation (i18n) - translate UI to other languages
  - **English source + de_DE: DONE** — die App ist vollständig auf englische
    Source-Strings konvertiert, deutsche Übersetzung in `trans/ttcut-ng_de_DE.ts`
    (661 Einträge, vollständig). Settings+Cut-Dialog (`ed2a531`/`d716c83`), Rest der App
    (`51e798b`..`7b3eec5`).
  - **Offen:** weitere Zielsprachen — je `ttcut-ng_<locale>.ts` anlegen und
    mit `lupdate` (Verzeichnis-Scan `common data avstream gui extern
    mpeg2decoder mpeg2window ui -ts trans/ttcut-ng_<locale>.ts`, siehe
    `.claude/skills/release/SKILL.md`) / `lrelease` pflegen.
- Undo/Redo for cut list operations
- Direct VDR .rec folder support (open recording without manual demux)

### Audio Format Support

**Status:** Open
**Priority:** Medium
**Created:** 2026-01-31

TTCut currently only supports AC3 (Dolby Digital) and MPEG-2 Audio (MP2) formats. Modern DVB broadcasts and streaming sources often use other audio codecs.

#### Requested Audio Formats

| Format | Sync Word | Use Case |
|--------|-----------|----------|
| **AAC** (ADTS) | `0xFFF` | DVB-T2, streaming, modern broadcasts |
| **EAC3** (Dolby Digital Plus) | `0x0B77` + extended header | HD broadcasts, streaming |
| **DTS** | `0x7FFE8001` | Blu-ray, some broadcasts |

#### Current Implementation

Audio detection is in `avstream/ttavtypes.cpp` (lines 180-260), which only checks for:
- AC3: Sync word `0x0B77`
- MPEG Audio: Sync word `0xFFE0`

**E-AC3 (Dolby Digital Plus) status:** `ttcut-demux` correctly demuxes E-AC3 streams with `.eac3` extension. The AC3 header parser (`TTAC3AudioStream`) detects E-AC3 (bsid > 10) and skips it with a warning. A native E-AC3 header parser is needed for frame-accurate cutting within TTCut-ng.

#### Required Changes

For each new format:
1. Add sync word detection in `TTAudioType::getAudioStreamType()`
2. Create new stream class (e.g., `TTEAC3AudioStream`, `TTAacAudioStream`)
3. Create header class (e.g., `TTEAC3AudioHeader`, `TTAacAudioHeader`)
4. Add to `TTAVTypes` enum
5. Update file dialogs in `ttcutmainwindow.cpp`

**E-AC3 specifics:** Same sync word as AC3 (`0x0B77`) but `bsid >= 11`. Frame size is encoded as 11-bit `frmsiz` field (not via lookup table). The existing `AC3FrameLength` table does not apply.

#### Workaround

Convert unsupported audio to AC3:
```bash
ffmpeg -i input.eac3 -c:a ac3 -b:a 384k output.ac3
ffmpeg -i input.aac -c:a ac3 -b:a 384k output.ac3
```

### DVB Subtitle Support

- Support DVB-SUB (bitmap subtitles) and Teletext subtitles
- Extract and convert to SRT or keep as PGS for MKV output

## Low Priority

- **Cut-ES-Dateinamen zwischen den Codec-Pfaden vereinheitlichen**
  (User-Wunsch 2026-08-05, bei der Untertitel-Abnahme aufgefallen). Die
  beiden Finalschnitt-Pfade benennen ihre Elementary-Stream-Ausgaben nach
  verschiedenen Schemata UND verschiedenen Basen:
  - MPEG-2 (`onDoCut` via `createCutFileName`): Basis = **Zielname** →
    `<ziel>.m2v`, `<ziel>_001.mp2`, `<ziel>_001.srt`
  - H.264/H.265 (`doH264Cut`, lokale Lambdas): Basis = **Quellname** →
    `<quelle>_cut.264`, `<quelle>_audio1.ac3`, `<quelle>_sub1.srt`
  Sichtbar wird das vor allem mit „ES-Dateien nach Mux löschen" AUS, wenn
  die Dateien im Schnittverzeichnis liegen bleiben. Vereinheitlichung =
  Verhaltensänderung an Ausgabenamen → eigenes kleines Vorhaben mit
  Entscheidung, welches Schema gewinnt (Zielname wirkt konsistenter zur
  MKV-Benennung); `createCutFileName` existiert bereits als gemeinsamer
  Baustein.

- **`gate_pool_crossthread.sh` baut noch gegen Qt5** (gefunden bei der
  Qt6-Migrations-Abschlussprüfung 2026-08-04, `tools/diag/gate_pool_crossthread.sh:28,41,47`
  — `pkg-config Qt5Core`/`Qt5Widgets` und moc via `host_bins`). Solange das
  so bleibt, prüft das Gate `TTThreadTaskPool::startNested()` gegen die
  Qt5-`QList`-Implementierung statt der Qt6-eigenen — ein bestandenes Gate
  belegt dann nicht mehr, dass der tatsächlich gebaute Qt6-Binary
  race-frei ist. Die Portierung braucht eine moc-Pfad-Entscheidung: unter
  Qt6 liegt moc nicht mehr in `host_bins`, sondern unter
  `/usr/lib/qt6/libexec/`. Zur selben Portierung gehören die veralteten
  manuellen g++/Qt5-Build-Anleitungen in den Headerkommentaren von
  `tools/diag/test_pool_crossthread.cpp:151-167` und
  `tools/diag/test_task_cleanup_order.cpp:109-126` (referenzieren
  `gate_pool_crossthread.sh:28-33` und Qt5Core/Qt5Widgets direkt).

- **TTMpv-Wrapper: Folge-Verbesserungen** (aus Code-Reviews des Player-Refactors)
  - ~~`TTMpvWrapper::stop()` „best-effort", gestoppter Frame ~1 Frame ungenau~~ →
    **ÜBERHOLT/erledigt (2026-07-25):** die vorgeschlagene synchrone Lesung ist längst
    implementiert. `TTMpvLibBackend::shutdown()` macht ein synchrones `pause` + synchrone
    `time-pos`-Lesung und propagiert sie nach `mPlaybackPosition`; `onPlaybackFinished()`
    nimmt als Stop-Frame `lastRenderedTimePos()` (die time-pos des zuletzt gemalten
    Frames). Verbleibende Ungenauigkeit ist allein der ~5-Frame-Pipeline-Versatz unten,
    kein Sync-Problem.
  - **Stop-Rest-Versatz ~5 Frames (Known Issue, tiefere Analyse offen)** — siehe
    Abschnitt „Known Limitations". Bei `vo=libmpv` hängt das in die FBO gerenderte Bild
    der mpv-Clock um eine feste Pipeline-Tiefe (~16 Frames) hinterher. Der eingebaute
    Fix (`TTMpvRenderWidget::lastRenderedTimePos()`, von `onPlaybackFinished` als
    Stop-Position genutzt statt `time-pos`) reduziert den sichtbaren Sprung beim STOP
    von ~16 auf ~5 Frames. Die letzten ~5 Frames sind mpvs interne Frame-Queue-Tiefe
    und nur über einen tiefen Render-Thread-Umbau eliminierbar. **Verworfene Versuche
    (gemessen):** `report_swap` an `frameSwapped` → 0 zusätzlicher Effekt;
    `MPV_RENDER_PARAM_ADVANCED_CONTROL` → blockiert den Stop-Pfad (`mpv_terminate_destroy`
    hängt, Play/Stop-Button toggelt nicht mehr, render.h §93-94) → nicht gangbar ohne
    separaten Render-Thread. Tiefere Lösung Prio low: ggf. mit künftiger libmpv-Version
    (echte „angezeigter-Frame"-Property) oder Render-Thread-Architektur erneut bewerten.
  - ~~`createTempMkvForPlayback` (`gui/ttcurrentframe.cpp`): keine Absicherung gegen
    `frameRate==0` (Division → UB); kein Destruktor-Cleanup (Temp-MKV bleibt liegen,
    wenn das Fenster während H.264/H.265-Wiedergabe geschlossen wird).~~
    → **ERLEDIGT (2026-07-25, `25c966eb`)**: `frameRate <= 0` bricht mit Warn-Log
    ab und liefert einen leeren Pfad (der Aufrufer behandelt das als „Wiedergabe
    nicht möglich"); `~TTCurrentFrame()` ruft `cleanupTempPlaybackFile()`.
    (Temp-Dateiname ist seit v0.71.0 eindeutig: `ttcut-ng_playback_temp.mkv`.)
  - **Erster PLAY pro Quelle ~5 s** (H.264/H.265): die ganze ES wird vor der
    Wiedergabe in eine temp-MKV gemuxt. Seit v0.71.0 wird die MKV über
    STOP→PLAY gecacht (Re-PLAY sofort), aber der erste Mux bleibt. Hebel:
    nur den abgespielten Bereich muxen, oder mpv die ES mit erzwungener
    Framerate direkt füttern. Prio low.

- **Screenshot-Modus: Vorschau-Dialog fehlt** (2026-07-26, beim v0.76.0-Release
  aufgefallen)
  - Der `--screenshots`-Modus deckt inzwischen alle Dialoge ab außer dem
    **Vorschau-Dialog** — es gibt kein `ttcutng-preview.png` im Wiki, obwohl
    sich der Dialog in v0.76.0 sichtbar geändert hat (eigene Zeile für die
    Burst-Warnung, mitwachsender Cut-Wähler, neue Tooltips, Start als
    Enter-Vorgabe).
  - Aufwändiger als die anderen: der Dialog braucht einen erzeugten
    Vorschau-Clip (Smart Cut auf dem Tux-Video) **und** einen laufenden
    mpv-Render-Kontext, sonst ist der Bildbereich leer. Beides im
    Screenshot-Modus aufzusetzen ist mehr als die ~10 Zeilen, die Goto- und
    Abschlussdialog gekostet haben (`8c47403e`).
  - Ebenfalls noch ohne Bild, aber unkritisch: der Vorschau-Fehlerdialog
    (`f658db7f`), der nur bei einem nicht schneidbaren Stream erscheint.

- **Auto-Cut from Markers** (ohne .info-Datei, z.B. bei ProjectX-Demux)
  - VDR-Marks werden bei ttcut-demux bereits automatisch als Cut-Einträge übernommen
  - Für manuelle Marker-Listen: Button der Marker-Paare in Cut-Einträge konvertiert
- **Rename TTMPEG2Window2 → TTVideoFrameWidget**
  - Class name and files (`mpeg2window/ttmpeg2window2.*`) are misleading — the widget handles MPEG-2, H.264, and H.265
  - Rename class, files, and directory (e.g., `videoframe/ttvideoframewidget.*`)
  - Update all includes, .pro file, .ui references, and moc references
- Implement plugin interface for external tools (encoders, muxers, players)
- GPU-accelerated encoding (NVENC, VAAPI, QSV) for faster Smart Cut

## Entwicklungs-Workflow

- **Verification-Test-Policy: Tux-Videos bevorzugen**
  - Bei Cut-Verification + Pipeline-Validation IMMER zuerst die Tux-Test-Videos verwenden
    (`tools/test-videos/cache/tux_*`). Kompakt (8-85 MB), reproduzierbar, im Repo.
  - Original-User-Videos nur bei neuen Problemen, die kein Tux-Test-Video reproduziert.
    Bei jedem solchen Fall ein neues Tux-Test-Video erzeugen (via `make_test_video.sh` o.ä.).
  - **Teilweise gelöst:** Tux-`.ttcut`-Files haben nach wie vor keine
    Cut-Entries. `tools/diag/qc-autocut.sh` erzeugt sich seit 2026-08-10 sein
    MPEG-2-Referenzprojekt selbst (drei Schnitte, dieselben wie in
    `tools/diag/test_mpeg2cut_abort.cpp`, mit den passenden Sollwerten). Für
    H.264/H.265 fehlt das Gegenstück noch — dort muss ein Projekt mit Schnitten
    weiterhin von Hand bzw. per Skript gebaut werden.

## Known Limitations

- **Cancelling a cut: what it reaches, and the one place it does not.**
  Since `feature/cut-abort` (2026-08-10) Cancel — and the progress dialog's
  X / Esc, which take the same route — stops the H.264/H.265 final cut
  (elementary-stream parse, Smart Cut video, audio, MKV mux), every remaining
  phase of the MPEG-2 final cut (audio, MKV mux and the mplex step for MPG
  output; the video phase was already abortable), the audio-only cut (audio
  and MKA mux) and the H.264/H.265 cut preview. A cancel deletes every file
  that run created, closes the operation with `Canceled` instead of `Exit`,
  emits no `cutFinished()`, leaves the progress bar frozen at its last value
  and writes no error, warning or fatal log line. A genuine failure is treated
  the opposite way: its partial files stay on disk for diagnosis — and for
  the *preview* that includes the bracket: a real failure still closes with
  `Exit` and raises the damaged-recording dialog, only a cancel reports
  `Canceled`. The preview also keeps the clips it had already finished
  (they are valid previews); only the clip being written when the cancel
  landed is removed.
  One gap remains, deliberate:
  - **A cancel during MPEG-2 *preview* generation does nothing.** The MPEG-2
    preview's video phase runs through `TTThreadTaskPool::startNested()`,
    which is deliberately never enqueued and therefore never receives the
    pool's `onUserAbort()` broadcast (`data/ttcutpreviewtask.cpp:269`).
    Preview cancel works for H.264/H.265 and is silently a no-op for
    MPEG-2. Closing it means covering the whole MPEG-2 preview branch —
    the video task, the audio track and the mux that follows it — not just
    adding a predicate to the audio call, which would mux a truncated track
    into a clip reported as "created".
  Four further sharp edges worth knowing when testing:
  - A cancel arriving during **subtitle** cutting is acted on only at the
    end of that phase, not immediately (`cutSubtitleTracks` has no poll
    point of its own).
  - The **mplex** step stops an external process, so its cancel is bounded
    by that process's exit: `TTMplexProvider::stopProcess()` sends SIGTERM
    and waits up to 2 s, then SIGKILL and up to 1 s more. In practice mplex
    exits on the SIGTERM immediately (measured: the SIGKILL branch never ran
    in any harness run). The bound is 2 s + 1 s only as long as the SIGKILL
    is reaped inside its second — if it is not, `~QProcess` waits again with
    Qt's own 30 s budget and prints a `qWarning`, so the honest worst case is
    ~33 s. That needs a process surviving SIGKILL, i.e. wedged in
    uninterruptible I/O; do not quote "3 s" without this caveat.
  - A cancel clicked in the **first moments** of an H.264/H.265 cut — before
    `TTESSmartCut::initialize()` starts — is not seen by the elementary-stream
    parse: `initialize()` is the only place that clears `mAbortRequested`
    (`extern/ttessmartcut.cpp:249`), so a request that arrived before it is
    wiped, and the parse (which *is* pollable via
    `TTNaluParser::setAbortCallback`) runs to the end. The request is not
    lost — `TTH26xCutTask` keeps its own flag and acts on it right after
    `initialize()` returns (`data/tth26xcuttask.cpp:235`) — but on a long
    recording that is a Cancel that appears dead for several seconds.
  - There is deliberately **no poll behind a successful mux**: a cancel
    landing in that last microsecond lets the run finish with a regular
    `Exit` rather than deleting a complete result.

- **Cancelling an MPEG-2 cut in the last cut-list entry can emit TWO closing
  brackets** (`Canceled`, then a stray `Exit` "exiting thread pool"), and
  reloads both tree views twice. Pre-existing — reproduced unchanged on
  `1a621fa0` — and *not* specific to MPG output; found while building the
  mplex-abort probe (2026-08-11), measured in 4 of 10 runs of
  `tools/diag/test_mpeg2cut_abort … mplexlate` in its earlier, queued-injection
  form (`exit=1 cancel=1 avReload=2`).
  Mechanism: the MPEG-2 video phase runs **two** tasks — `TTCutVideoTask` on
  the pool plus a nested `TTCutTask` per cut (`startNested`). A cancel arriving
  after the last `isAborted()` poll makes both of them throw `TTAbortException`
  and emit `aborted(this)`. `TTThreadTaskPool::onThreadTaskAborted()` fires
  `emit aborted(); emit exit();` whenever `mTaskQueue` is empty afterwards, and
  the queue is already empty for the *second* one — so the pair is emitted
  twice. The first `exit()` is swallowed by `onThreadPoolExit()`'s
  `mCutOperationActive` branch (which also consumes the flag); the second finds
  the flag false, takes the `else` branch and emits the stray `Exit` plus a
  second `avDataReloaded()`.
  Fix would be in the pool (emit the pair only on a real non-empty → empty
  transition), which is shared by every operation on this branch — deliberately
  not attempted as a late change to the cut-abort work. The `mplexlate` probe
  therefore arms deterministically *after* the pool has drained, where this
  cannot occur.
  A related consequence in the same window: the aborted nested `TTCutTask`
  belongs to the shared `TTMpeg2VideoStream`, and its `mIsAborted` is never
  cleared, so the *next* cut on the same stream throws immediately on entry
  (`TTThreadTask::run()`'s "entering running state while already aborted").
  Observed once in a restart-after-cancel run; same root, same fix location.

- **KWin stale-area bug: upstream report + minimal test case still open**
  (the in-app symptom itself is RESOLVED since 2026-08-06 — the render
  widget is stacked StackAll only during playback, `f87ea06c`; full
  investigation record incl. threshold matrix, Wayland protocol proof and
  experiment history in `docs/completed-work.md`, GUI und Wiedergabe, and
  in the kwin-fractional-scale-bug memory).
  - Proven in-app ingredient under Qt6: a visible-but-obscured
    QOpenGLWidget (StackAll) in a large window at fractional scaling. It
    is NECESSARY (TTCUT_DIAG_NO_PLAYER bisection 2026-08-06: bug gone) but
    NOT SUFFICIENT (`kwin-repaint-testcase.cpp` with a QOpenGLWidget does
    not reproduce) — a KDE report still lacks the second ingredient.
  - NOTE the path dependence: the SAME bisection on Qt5 (2026-08-02) saw
    the bug survive TTCUT_DIAG_NO_PLAYER and a nearly empty window — Qt5
    ran KWin's forced-server-side-scale path (no fractional-scale
    protocol), Qt6 binds `wp_fractional_scale_manager_v1`. The trigger
    sets differ per path; Qt5 data must not be mixed into a Qt6 report.
  - Tools/logs: `tools/diag/window-geometry.sh`,
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/kwin-*.{sh,log}`, `wayland-diff/`,
    `kwin-bugreport/` (BEFUND.md — keep, do not clean up).
  - On recurrence: check for a KWin update first
    (`apt policy kwin-wayland`), then read the record.

- **Audio burst detection is approximate — treat it as a hint, not a verdict.** It
  reliably flags the case it was built for (a loud advertising burst reaching the cut
  boundary over quiet programme material: 3 of 3 on the ServusTV reference). Outside that
  case its resolution is limited by design, and the limits below are measured, not
  assumed. Deciding whether a cut is clean still requires listening to the preview.
  - **Time resolution is one audio frame (32 ms for AC3).** The detector computes RMS per
    decoded audio frame. A transient of a few milliseconds — a click, a switching artefact
    — is averaged away and can stay invisible even when its sample peak reaches 0 dBFS.
    Verified 2026-07-09: at both AC3 acmod changes in `TEST_deu.ac3` neither RMS *nor*
    sample peak shows an upward excursion.
  - **Only the outermost two chunks are tested (~64 ms).** The analysis window spans
    200 ms, but everything further inside contributes to the context median only. See the
    multi-frame entry below.
  - **An untested transient makes detection *worse*.** A loud chunk inside the window but
    outside the tested range raises the context median, which raises the bar the edge
    chunks must clear. The detector is thus least sensitive exactly when something loud is
    nearby.
  - **The criterion cannot separate an isolated outlier from a level step.** `peak − median`
    fires the same way for a short click and for an advertising onset that jumps and then
    stays loud (measured: a 55 dB step at 624.128 s in `TEST_deu.ac3`).
  - **The absolute audibility gate silently drops quiet bursts.** `kBurstAbsoluteFloorDb`
    (−40 dB) rejects anything below it regardless of how far it sticks out. Real
    advertising bursts on the reference recording peak at −37.5 / −27.3 / −36.5 dB, i.e.
    two of three clear the gate by under 4 dB. A quieter broadcaster is missed without
    notice. The gate cannot simply be lowered: at −50 dB it would admit 709 further
    positions on that same recording.
  - **RMS is broadband, unweighted.** Inaudible content (infrasound, >16 kHz) counts
    toward the level. Practically irrelevant for DVB programme audio; K-weighting
    (ITU BS.1770) noted as a follow-up in
    `docs/superpowers/specs/2026-07-04-burst-context-filter-design.md`.

- **Multi-frame audio burst at cut boundaries**: DVB advertising audio can bleed 2-3+
  audio frames before the video transition. Two *distinct* gaps, easily conflated:
  1. **Detection is edge-only.** `TTFFmpegWrapper::detectAudioBurst()` analyses a 200 ms
     window around the boundary but tests only the outermost two chunks
     (`checkStart = rmsValues.size() - 2` for CutOut, the first two for CutIn). A
     multi-frame burst that *reaches* the boundary **is** detected — it overlaps those two
     frames. What is **not** detected is an isolated transient sitting further inside the
     kept material, e.g. 100–200 ms from the cut: it *is* inside the analysis window, but
     only the outermost two chunks are ever tested. Worse, such a transient raises the
     context median and thereby makes the edge chunks *less* likely to trip the threshold.
     Only beyond ~200 ms does it leave the window entirely.
     (Corrected 2026-07-09 — the earlier wording claimed the window "never covers them"
     and located them in a "silence region between segments". Both were wrong.)

     **Open design question, not yet a defect with a repro.** The median-over-~7-chunks
     criterion answers "is this chunk louder than its surroundings overall". To find a
     *short* outlier (1–3 audio frames) the better question is whether the chunk is loud
     while the 1–3 chunks **before and after** it are quiet — a local neighbourhood
     contrast rather than a window median. That distinction also separates a genuine
     isolated transient from an advertising onset, where the level jumps and then *stays*
     high (measured: a 55 dB step at 624.128 s in `TEST_deu.ac3`). The median criterion
     cannot tell the two apart; a neighbourhood criterion can. A false-positive rate for
     any widened test range is unmeasured — normal programme audio (door slams, musical
     accents) would also qualify.

     **Blocked on material.** The suspected trigger — a level spike caused by an AC3
     format switch (5.1↔2.0) shortly before/after the cut — could not be reproduced.
     `TEST_deu.ac3` has exactly two acmod changes (83.808 s 2/0→3/2, 624.128 s 3/2→2/0);
     at *neither* does the level spike upwards, in RMS **or** in sample peak (the latter
     checked specifically because a few-millisecond transient would be averaged away by
     the 32 ms RMS). At 83.808 s the level even dips by 11 dB. `ServusTV_HD_deu.ac3` has
     zero acmod changes across 6501 s. Do not design a widened window until a recording
     that actually exhibits the artefact exists.
  2. **Correction is single-step.** The preview offers only `Shift -1 Frame` /
     `Shift +1 Frame` (`TTCutPreview::onBurstShift()`), so a 2-3 frame burst needs
     repeated clicks. Note the shift moves the cut by one *video* frame (40 ms @ 25 fps)
     while an AC3 audio frame is 32 ms — the two grids do not align.

  Orthogonal and already solved: the context-relative threshold `burstMinDeltaDb`
  (v0.72.0) fixed false negatives on quiet programme material; the 2026-07-09 rewrite
  passed that threshold into the detector, removed the redundant post-filter, and made
  the detector report the **peak** of the tested chunks instead of the first one above
  the threshold. None of this addresses the two gaps above — both remain open.

- **Cut point stutter (rare)**: For streams without any IDR frames (only Non-IDR I-slices), Smart Cut re-encodes 1 GOP at each segment boundary to produce an IDR. This is typically invisible but may cause minor quality differences at cut points (~0.5% of frames affected). When B-frame reorder delay shifts CutIn past the stream-copy keyframe (Case B), a small leak of ≤ reorder_delay pre-CutIn frames may occur to avoid POC domain mismatch.

- **Stop still-frame offset ~5 frames (mpv playback)**: When stopping playback in the "Current Frame" widget, the displayed still jumps ~5 frames (~200 ms) relative to the image visible when STOP was clicked. Cause: with `vo=libmpv` (in-process rendering for native Wayland support) mpv does not display frames itself but hands them to our `paintGL`. The mpv clock (`time-pos`) runs ahead of the frame actually rendered into the FBO by a fixed pipeline depth. A built-in fix (`lastRenderedTimePos` instead of `time-pos` as stop position) reduces the jump from ~16 to ~5 frames. Playback itself is smooth; only the frozen still is affected, the cut position is unaffected. The old `vo=x11` backend did not have this because mpv displayed frames itself (clock = visible frame). Deeper fix see TODO (Low Priority, "TTMpv-Wrapper: Folge-Verbesserungen"): requires a separate render thread or a future libmpv extension; `report_swap` and `ADVANCED_CONTROL` were tested and rejected (no effect / blocks the stop path).
