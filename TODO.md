# TTCut-ng TODO / Feature Requests

Offene Punkte und bekannte Einschränkungen. Erledigtes steht mit seinen
Belegen in [docs/completed-work.md](docs/completed-work.md).

## High Priority

- **SIGSEGV nach Smart Cut in `doH264Cut` — Use-after-free in Qt-Model-Internals
  (2026-08-07, vertagt auf User-Entscheidung; einmaliger Absturz, seither in
  keiner GUI-Abnahme wieder aufgetreten — der frühere Zusatz „blockiert den
  Merge von `feature/progress-details`" ist überholt, der Branch ist seit
  2026-08-09 gemergt und released)**
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
    Backtrace-Dump: war `CLAUDE_TMP/TTCut-ng/core500359_bt.txt` — **gelöscht
    2026-08-16** (CLAUDE_TMP-Purge, Memory `reference_claude_tmp_purge_2026_08_16`);
    die tragenden Frames stehen wörtlich in diesem Eintrag.
  - ~~TTCut-Code enthält kein einziges Proxy-Model und keinen QCompleter
    (Grep 2026-08-07) — das Proxy ist Qt-intern.~~ **Richtiggestellt
    2026-08-13, gemessen:** Der Grep konnte es nicht finden, weil TTCut keins
    *schreibt* — die **Dateidialoge bringen eins mit**. Gemessen mit
    `tools/diag/test_filedialog_proxy` in dieser Sitzung (Wayland, ASAN):
    ein Dateidialog lädt `KF6KIOFileWidgets`/`KIOWidgets`/`KIOCore` unter
    `KDEPlasmaPlatformTheme` und legt ein **`KDirSortFilterProxyModel`** an —
    abgeleitet von `QSortFilterProxyModel`, erbt also genau das
    `_q_sourceModelDestroyed` aus Frame #1. Damit ist das Objekt benannt,
    auf dem der Absturz operierte; die Schlussfolgerung „nicht zuordenbar"
    beruhte auf einer Ausschlussbegründung, die so nicht trug.
  - Dateidialoge kommen in der Absturz-Sequenz **dreimal** vor:
    `getOpenFileName` beim Öffnen, `getExistingDirectory` im Cut-Dialog
    (`gui/ttcutavcutdlg.cpp:249`, mit `qApp->processEvents()` direkt
    dahinter) und ein `QFileDialog`-Objekt beim Frame-Speichern
    (`gui/ttcurrentframe.cpp:545`). Direkt vor `onDoCut` wird
    `TTCutAVCutDlg` per `delete` zerstört (`ttcutmainwindow.cpp:1484`, dort
    `cutAVDlg`), davor der Vorschau-Dialog (`delete cutPreview`, `:1435`)
    — und `doH264Cut` betritt über die Statusmeldungen von `cutAudioTracks`/`cutSubtitleTracks`
    wieder die Ereignisschleife (`qApp->processEvents()`). Dort werden die
    aufgeschobenen Löschungen aus diesen Abbauten ausgeführt; dorthin gehören
    die im Backtrace fehlenden Frames. **Mechanismus kohärent, nicht
    gezeigt.**
  - **Stand 2026-08-15: der vollständige Pfad ist nachgebaut und bleibt
    sauber.** Zwei neue Sonden schließen die Lücken, die 2026-08-13 noch
    offen waren, beide unter ASAN auf dem Originalmaterial
    (`03x01`, 224 949 Bilder) mit den Originalschnittgrenzen 49719..190218:
    | Sonde | was zusätzlich im Prozess ist | Ergebnis |
    |---|---|---|
    | `test_preview_then_cut` | Vorschau-Dialog, `doCutPreview` mit echten Clips, `initPreview`, mpv lädt sie, modaler Abbau per `delete` | sauber, Schnitt 2810,016 s |
    | `test_mainwindow_then_cut` | **echtes `TTCutMainWindow`** mit allen Views und Modellen, Projekt geladen, Cut-Dialog, Bedienung über die Knöpfe `pbPreview`/`pbCutAudioVideo` | sauber, Schnitt 2810,016 s |
    Damit ist der Vorschau-Dialog als alleinige Ursache ausgeschlossen, und
    auch das vollständige Zusammenspiel aus Hauptfenster, beiden Dialogen und
    Schnitt zeigt nichts.
  - **Was jetzt noch fehlt**: echte Mausereignisse. `QAbstractButton::click()`
    löst `clicked` aus, ohne durch `QWidget::mouseReleaseEvent` zu gehen — und
    genau dort begann der Original-Backtrace. Dazu die Zeitabhängigkeit: der
    Absturz trat einmal in Monaten auf, ein Einzellauf trifft so etwas nicht.
    Nächster sinnvoller Schritt wäre ein Dauerlauf des Harnisch (er ist jetzt
    vollständig) oder echte Ereignisse per `QTest`/xdotool.
  - **Zwei Fallen, die diese Runde gekostet hat — beide erzeugten einen
    Absturz, der dem gesuchten ähnlich sah:**
    1. Ein Wächter-Zeitgeber mit **rohem Zeiger** auf einen bereits gelöschten
       Dialog feuerte im `processEvents()` von `doH264Cut` — Backtrace mit
       `processEvents` und `doH264Cut` direkt darunter, also genau die Form,
       auf die diese Jagd wartet. Abhilfe: `QPointer` und den Zeitgeber nach
       `exec()` stoppen.
    2. **`accept()` auf einer Plasma-nativen `QMessageBox` stürzt zuverlässig
       ab** (`~QDialog` → `QDialogPrivate::setNativeDialogVisible` →
       `QWidget::hide`), ein Knopfklick nicht. Einzelvariablen-Vergleich
       gemessen: `PROBE_DISMISS=accept` → SEGV, `=click` → sauber. Kein
       Produktfehler — kein Anwendungscode schließt Meldungen per `accept()`.
  - **Harnisch-Hinweis**: erst drücken, wenn das Hauptfenster wieder
    freigegeben ist. Ein Klick 2 s nach `openProjectFile()` (Öffnen dauert bei
    dieser Datei 9,7 s, unter ASAN 21 s) ließ den Oberflächen-Faden über zehn
    Minuten bei 100 % rechnen, ohne dass je ein Vorschau-Dialog erschien — mit
    **und** ohne ASAN. Ursache nicht geklärt; für Anwender nicht erreichbar,
    weil das Fenster währenddessen deaktiviert ist.

  - **Vier ASAN-Läufe am 2026-08-13, alle sauber** — die Spur ist damit
    eingegrenzt, nicht bestätigt:
    | Lauf | enthält | Ergebnis |
    |---|---|---|
    | `test_filedialog_proxy`, 20 Zyklen | Dateidialog (KIO-Proxy) | sauber |
    | `--auto-cut` auf 03x01 (Originalmaterial) | ganzer H.264-Schnitt + Mux | sauber, Ausgabe exakt 2810,016 s |
    | `test_dialog_then_cut` | Dialog **und** Schnitt in einem Prozess | sauber, Ausgabe identisch |
    | `test_dialog_then_cut` (Wiederholung) | dasselbe | sauber, Ausgabe identisch |
    **Was in keinem dieser Läufe steckt** und in der Absturz-Sequenz vorkam:
    der Vorschau-Dialog (`TTCutPreview`, bringt libmpv + `QOpenGLWidget` mit,
    wird per `delete` zerstört), der Cut-Dialog, das vollständige Hauptfenster
    mit seinen Views — und echte Mausklicks. Der Vorschau-Dialog ist der
    auffälligste verbliebene Kandidat (`QOpenGLWidget` war in diesem Projekt
    schon einmal Ursache, `f87ea06c`). Nächster Schritt bleibt die echte
    GUI-Sequenz unter ASAN, die sich nicht ohne Bedienung fahren lässt.
  - **Zeitangabe richtiggestellt:** Der Schnitt dieses Materials dauert **rund
    eine Minute**, nicht eine Stunde (gemessen, Smart Cut ist fast reiner
    Stream-Copy). „Crash ~60 s nach den Dialog-Schließungen" heißt also: der
    Absturz kam am **Ende** des Schnitts, in der Mux-/Abschlussphase.
  - **Was der Sondenlauf NICHT zeigte:** 20 Zyklen Dialog auf/zu/`delete` +
    Wiedereintritt in die Ereignisschleife, unter ASAN, ohne Report. Die
    schärfere Form (modal per `exec()`, Abbau mit laufenden KIO-Jobs — das,
    was TTCut tatsächlich tut) **hängt**: die modale Schleife kehrt nie
    zurück, weder `reject()` noch ein 1,5-s-Wächter holen sie zurück, KIO
    meldet vorher einen toten Socket. Drei Versuche, dann abgebrochen. Wer
    hier weitermacht: dieser Fall bleibt der erreichenswerte — aber nicht
    über diesen Harnisch.
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
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/core500359_bt.txt` [Material verloren 2026-08-16] bleibt.
    Bei Wiederauftreten: Core sichern, Forensik-Referenz nutzen,
    ASAN-Build neu erzeugen (Hinweis: auch der Backtrace-Dump ist seit dem
    CLAUDE_TMP-Purge 2026-08-16 weg — die Frames oben sind der Restbestand)
    (`cmake -B build-asan -G Ninja
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address
    -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`).

- **H.264 gemischt MBAFF+PAFF (08x04-Korpus) — PAFF-Wiedergabe**
  Die Befunde B, D und E sind gefixt (2026-07-19, `46d3dcb` / `8dfda6d`),
  ebenso die Wurzel (TS↔ES-AU-Nummerierungs-Drift der `es_extra_frames`) —
  Belege in `docs/completed-work.md`, Smart Cut.

  **Offen ist allein die PAFF-Wiedergabe:** beim Play meldet mpv
  `reference picture missing during reorder`. Follow-up zu libmpv Phase 2.
  Nebenpunkt: die Crash-Variante von Befund B (SIGABRT in
  `avcodec_send_packet`) ist als Folge des beseitigten EOF-Drains plausibel,
  aber nicht formal bewiesen und nur über einen neuen Repro-Lauf auf dem
  08x04-Korpus nachprüfbar — der alte Core-Dump ist gelöscht.


- **Logo für TTCut-ng**
  - Projekt braucht ein wiedererkennbares Logo/Icon für GitHub, Debian-Paket, Desktop-Launcher
  - Anforderungen: SVG (skalierbar), funktioniert als 16x16 bis 512x512, passt zu Video-Editing

## Medium Priority

- **Zwei überlagerte Widgets in „Aktueller Frame" — sind sie nötig?**
  (2026-08-26, vertagt auf User-Entscheid: „aber nicht heute")
  - Während der Wiedergabe läuft das `QStackedLayout` in `TTCurrentFrame` auf
    **StackAll**, d. h. Render-Widget und Standbild-Widget (`mpegWindow`) sind
    gleichzeitig sichtbar (`gui/ttcurrentframe.cpp:78-92`). Der Stapel
    existiert allein als Umgehung des KWin-Repaint-Fehlers.
  - Die Vorschau kommt ohne aus: dort *ist* mpv die Anzeige, ein einzelnes
    Render-Widget in `videoFrame`, kein Standbild-Fallback
    (`gui/ttcutpreview.cpp:57-69`).
  - **Anlass**, gemessen 2026-08-26 an einer 720p50-H.264-Aufnahme: Die
    Wiedergabe in „Aktueller Frame" ruckelte, die Vorschau derselben Quelle
    nicht — beide unter derselben Last (paralleles HandBrake-Transcode mit
    1971 % CPU, load 25). Mit dem Ende des Transcodes verschwand das Ruckeln.
  - **Ausgeschlossen** (jeweils gemessen): Framerate und Zeitstempel der
    Wiedergabe-MKV (20 ms Abstand, `r_frame_rate=50/1`), Dekodierleistung
    (0 verworfene Frames bei Echtzeit-Wiedergabe, 24,7× Reserve bei reiner
    Dekodierung), mpv-Konfiguration (beide Player teilen sich
    `gui/ttmpvlibbackend.cpp` samt `hwdec=no`).
  - **Offen und ausdrücklich ungemessen:** ob die Zusatzstufe des Stapels
    (Render-to-Texture plus Komposition pro Frame statt direkter Darstellung)
    tatsächlich die Reserve kostet, die der Vorschau das Ruckeln erspart.
    Nächster Schritt wäre, die Last künstlich nachzustellen und beide Fenster
    gegeneinander laufen zu lassen.
  - **Zu klären ist dann:** Braucht es die Überlagerung überhaupt noch, oder
    gibt es eine bessere — auch gern kompliziertere — Lösung für den
    KWin-Repaint-Fehler, die ohne zweites gleichzeitig sichtbares Widget
    auskommt. Siehe auch `Known Limitations` und das KWin-Thema in
    `reference_kwin_fractional_scale_bug` (Memory).

- **DVB-Bitmap-Untertitel entlang der Schnittliste in TTCut-ng schneiden**
  (Folgevorhaben aus dem Untertitel-Export 2026-08-16; vereinbart, nicht
  begonnen)
  - **ZURÜCKGESTELLT — User-Entscheid 2026-08-16: erstmal beobachten.**
    Solange der OCR-SRT-Export gut funktioniert, besteht kein Bedarf; die
    geschnittene SRT deckt den Anwendungsfall ab. Erst wieder aufgreifen,
    wenn die OCR-Qualität in der Praxis nicht reicht (z. B. Sender, bei
    denen die Glyph-Reparatur versagt) oder die verlustfreie Bitmap-Spur
    in der Ausgabe-MKV konkret vermisst wird.
  - ttcut-demux exportiert DVB-UT seit 2026-08-16 als `.mks` (`--subs`);
    TTCut-ng selbst schneidet aber nur SRT (`TTSrtSubtitleStream`). Für
    Bitmap-UT fehlt: Stream-Klasse, Schnitt entlang der Cut-Liste
    (PTS-Fenster-Copy wie beim Audio-Stream-Copy-Schnitt) und Mux in die
    Ausgabe-MKV.
  - Referenz-Mechanik: `mkvmerge --split parts:START-END` schneidet die
    UT-Spur verlustfrei (siehe `/home/fpwild/Skripte/Ts2MKV.sh`, dort
    produktiv im Einsatz); in TTCut-ng wäre der libav-Weg analog zum
    Audio-Schnitt (Pakete im Zeitfenster kopieren, PTS versetzen).
  - Materiallage (gemessen 2026-08-16): 15 von 21 lokalen Aufnahmen tragen
    echte DVB-UT-Daten; öffentlich-rechtliche Sender praktisch immer.

- **`setEncoderCodec()`-Early-Return lässt `workingOutputContainer` und
  `Mpeg2Muxer` auseinanderlaufen** (2026-08-16, gemessen beim
  ttcut-audiofix-Realfall-Gate; dokumentiert im Kommentar von
  `tools/diag/gate_audiofix.sh` ~Z. 437)
  - `runAutoCutMode()` ruft `TTSettings::setEncoderCodec(0)` für MPEG-2, um
    `workingOutputContainer` aus `mMpeg2Muxer` nachzuziehen —
    `setEncoderCodec()` hat aber einen Early-Return
    (`if (mEncoderCodec == v) return;`), und der kompilierte Default ist
    schon 0. Auf frischer Konfiguration feuert der Resync also nie:
    eine Ini mit nur `Encoder\Mpeg2Muxer=0` ergab gemessen
    `workingOutputContainer=1` (MKV) im Lauf-Log und in den beim Beenden
    zurückgeschriebenen Settings. Wer mplex will, muss derzeit **beide**
    Schlüssel (`Encoder\Mpeg2Muxer=0` UND `Muxer\OutputContainer=0`) setzen.
  - Betrifft die App unabhängig vom audiofix-Feature; GUI-Pfad (Settings-
    Dialog setzt beide?) noch nicht vermessen — vor einem Fix prüfen, wo
    der Resync überall hängt.

- **Vorbestehende Defekte, gefunden beim Abbruch-Vorhaben (2026-08-10)** —
  keiner davon wurde von `feature/cut-abort` verursacht, alle sind dort beim
  Lesen bzw. Messen aufgefallen und bisher nur in den SDD-Berichten
  festgehalten. Reihenfolge grob nach Nutzerwirkung.
  - **Der MPEG-2-Neucodierer ist von Lauf zu Lauf nicht reproduzierbar**
    (`thread_count = 0`; zwei verschiedene Video-ES in sechs Läufen
    **desselben** unveränderten Binaries). Für die Ausgabequalität harmlos,
    aber es schließt den Byte-Vergleich als Prüfkriterium auf diesem Pfad
    aus — festgehalten, damit niemand erneut eines baut.
    `tools/diag/qc-autocut.sh` überspringt die Videospur deshalb bei
    mpeg2video und prüft dort nur Paketzahl, Dauer und Audiospur.
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

- **H.265-UHD: Schieber reagiert träge; Absturz (SIGABRT) unerklärt**
  (Rest des Befunds von 2026-08-15; der Minuten-Hänger selbst ist GELÖST —
  siehe `docs/completed-work.md`, „EAGAIN-Paketverlust")
  - **Was der Hänger war**: `skipCurrentFrame()` verwarf bei
    `avcodec_send_packet == EAGAIN` das Paket samt vergebenem Decode-Tag.
    Bei B-Hierarchie blieb die Dekoder-Warteschlange voll, jedes weitere
    Paket bis Dateiende wurde einzeln gelesen und verworfen, das Ziel-Tag
    erschien nie, und die Wiederhol-plus-Rekursionskaskade in `decodeFrame()`
    machte aus **einem** Schieber-Ereignis Minuten bis Stunden GUI-Rechenzeit.
    Gefixt (korrekte Send/Receive-Pumpe mit schwebendem Paket); UHD-Sprung
    jetzt 2,6 s statt nie.
  - **Gemessen 2026-08-28** (Regressionslauf zum Frame-Index-Bündel, UHD
    `Designermode`, 178 224 AUs): `decodeFrame()` kostet auf diesem Material
    im Median **1262 ms** (Ziehen) bzw. **1390 ms** (Sprung); eine Ziehbewegung
    mit 50 Ereignissen sind damit rund **63 s** GUI-Zeit. Der Restbefund
    „Schieber reagiert träge" ist also **echte Dekodierkosten**, keine Schleife
    mehr — die Bündel-/Schleifenarbeit von 2026-08-28 ändert daran nichts und
    war dafür auch nicht gedacht. Wer das angeht, muss die Zahl der synchronen
    `onGotoFrame()`-Aufrufe pro Ziehbewegung senken (Entprellen/Abbrechen),
    nicht die Dekodierung schneller machen.
  - **Offen B — der SIGABRT** aus der GUI-Abnahme 2026-08-15 ist weiter
    unerklärt (Zusicherung, ungefangene Ausnahme oder fehlgeschlagene
    Allokation; kein Speichermangel: 91 GB RAM, 82 GB frei). Plausibel
    geworden, aber unbewiesen: die Kaskade oben las die 502-MB-Datei
    wiederholt komplett — was dabei an Puffern anfällt, war nie im Blick.
    Nach dem Fix neu provozieren, bevor jemand tiefer gräbt. Das Helferskript
    `abnahme/start-gdb.sh` ist **gelöscht** (CLAUDE_TMP-Purge 2026-08-16) —
    neu zu schreiben ist es in Minuten: TTCut-ng unter gdb starten, beim
    Absturz alle Fäden dumpen und das Logfile wegsichern (es wird beim
    Absturz nicht geschrieben und beim Neustart überschrieben, siehe
    Messfallen unten).
  - **Zwei Messfallen aus der ersten Runde** (Beweise gingen verloren):
    abgeschnittener Core durch Shell-Zeitlimit beim Schreiben; die
    `Q_ASSERT`-Meldung geht ins Logfile, das beim Absturz nicht geschrieben
    und beim Neustart überschrieben wird.

- **`decodeFrameYUV()` hat noch die alte unbegrenzte Skip-Schleife** (Fund aus
  dem Abschluss-Review zum Frame-Index-Bündel, 2026-08-28)
  - Der nicht-sequenzielle Zweig in `extern/ttffmpegwrapper.cpp` setzt
    `guardMax = mFrameIndex.size()` (bzw. 100000 ohne Index) und prüft
    innerhalb der Schleife kein `isCancelled()` — Zeile für Zeile die
    Geschwisterschleife, die am 2026-08-28 in `decodeFrame()` auf die
    Suchdistanz begrenzt und abbrechbar gemacht wurde (siehe
    `docs/completed-work.md`). Genutzt von `data/ttframesearchtask.cpp`.
  - Die Metadaten-Ursache ist für diese Aufrufer geschlossen — sie kommen
    über `provideFrameIndexTo()` an denselben Bündel-Index heran wie
    `decodeFrame()`, aber die EOF-Drain-Form der Schleife selbst besteht
    fort, versteckt in einer Suche, die viele Frames durchläuft und ihren
    Abbruch nur zwischen den Frames prüft, nicht während eines einzelnen
    `decodeFrameYUV()`-Aufrufs.
  - Entweder dieselbe Begrenzung (Suchdistanz + Abbruchprüfung) hier
    nachziehen, oder begründen, warum `decodeFrameYUV()` sie nicht braucht.

- **`TTH26xVideoStream::ffmpegFrameIndex()` ist toter Code** (Fund aus dem
  Abschluss-Review zum Frame-Index-Bündel, 2026-08-28)
  - Kein Aufrufer mehr außerhalb der eigenen Deklaration/Definition
    (`avstream/tth26xvideostream.h`/`.cpp`) — beide Subklassen greifen direkt
    auf `mFFmpeg->frameIndex()` zu. Die Methode steht weiterhin unter dem
    Header-Kommentar, der sie als kanonischen Zugriffsweg vorstellt, direkt
    über dem Bündel-Mechanismus (`ffmpegFrameIndexBundle()`/
    `provideFrameIndexTo()`), der sie ersetzt hat.
  - Entfernen oder dem nächsten Dead-Code-Audit überlassen.

- **Vorschau-Rückfall-Engine ist nicht abbrechbar** (Kartenbefund 2026-08-15,
  niedrige Priorität — nur erreichbar, wenn die geteilte Smart-Cut-Engine der
  Vorschau nicht initialisiert werden konnte, also auf stark beschädigten
  Aufnahmen)
  - `TTCutPreviewTask` legt dann je Clip eine lokale `TTESSmartCut` an, die
    nie bei `mpActiveSmartCut` registriert wird; ihr `initialize()` (voller
    ES-Parse) ist nicht abbrechbar, und das wiederholt sich für jeden Clip.
    Der Code-Kommentar an `localSmartCut` (`data/ttcutpreviewtask.cpp`)
    beschreibt Lücke und Lösungsform (Publish/Clear unter `mSmartCutMutex`
    wie bei der geteilten Engine). Auch vermerkt in
    `docs/code-map/smart-cut.md`.

- **Weitere geteilte Temp-Namen** (2026-08-12, offen, niedrige Priorität)
  - Dieselbe Bauform steht noch an drei Stellen: `gui/ttcutpreview.cpp` und
    `data/ttcutpreviewtask.cpp` (Vorschau-Dateien) sowie
    `gui/ttcurrentframe.cpp` (`createTempMkvForPlayback`,
    `ttcut-ng_playback_temp.mkv`). Zwei
    gleichzeitig offene Fenster benutzen dieselben Namen.
  - Kein gemessener Fehlerfall — deshalb nicht mitgefixt. Wer es angeht:
    dasselbe Muster wie in `encodePart()` (`QTemporaryDir` je Vorgang).

- **Wiedergabe-Mux blockiert den GUI-Thread** (2026-08-17, mittlere/niedrige
  Priorität)
  - `TTCurrentFrame::createTempMkvForPlayback()` (`gui/ttcurrentframe.cpp`)
    läuft synchron auf dem GUI-Thread: kein Fortschritt, kein Abbruch, das
    Fenster meldet „reagiert nicht", bis der Mux fertig ist. Seit dem
    Konsole-cgroup-Fund (siehe `docs/completed-work.md`, Untertitel-/
    Wiedergabe-Einträge 2026-08-17) sind das ~6 s pro Quelle (vorher
    minutenlang) — deshalb herabgestuft, aber strukturell offen.
  - Lösungsform: wie die Schnitt-Tasks in den Task-Pool verlagern
    (Fortschritt + Abbruch inklusive); hängt mit dem geteilten Temp-Namen
    `ttcut-ng_playback_temp.mkv` zusammen (Eintrag „Weitere geteilte
    Temp-Namen" oben).
  - Messwerkzeug: `tools/diag/bench_playback_mux` (Mux-Durchsatz standalone).

- **ttcut-demux: bash + ffmpeg-CLI → libav-Library-Migration**
  - `tools/ttcut-demux/ttcut-demux` ist aktuell ein bash-Script (~3050 Zeilen, Stand v0.82.1) das ffmpeg-CLI-Subprozesse spawnt für: TS-Demux, Audio-Trim, Audio-Padding, Audio-Gap-Repair, PTS-Analyse, etc.
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
    Die Prototyp-Werkzeuge (`lip_landmark.py`, `venv-mp/`, `lip_final.png`)
    sind **gelöscht** (CLAUDE_TMP-Purge 2026-08-16) — die Messmethode ist im
    Memory `reference_lipsync_measurement` dokumentiert, venv + Skript sind
    daraus neu aufsetzbar.
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
  - Code-Stelle `data/ttaudioonlycuttask.cpp` warnt "not implemented yet"
    (bis zum Task-Pool-Umbau sass die Meldung in `TTAVData::doAudioOnlyCut`;
    Pfad beim v0.82.2-Abgleich korrigiert)
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

### Audio-Anomalie-Reparatur — bewusste Folgearbeiten

Aus `docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md`
(Nicht-Ziele) und den Task-Reviews des Vorhabens. Nichts davon blockiert
v1 (Scanner + Reparatur-Dialog + Schnittpfad, siehe CHANGELOG „Unreleased").

- **Lückenhafte `<Order>`-Werte in handbearbeiteten Projektdateien sind bei
  der Repair-Spur-Zuordnung unvalidiert** (Task-3-Review-Befund, hierher
  verschoben statt in Task 8 mitgelöst): `TTAudioRepairItem::trackIndex()`
  übernimmt die gespeicherte `<Order>` ungeprüft als spätere Listenposition;
  ein Projekt mit nicht-fortlaufenden oder doppelten `<Order>`-Werten (von
  Hand editiert, nicht über die App gespeichert) kann eine Reparatur der
  falschen Spur zuordnen, ohne Warnung.
- **Nur die ERSTE AC3-Spur wird gescannt und repariert** (Final-Review-Befund
  M8). Die Spec spricht von „AC3-Spuren" (Mehrzahl), umgesetzt ist genau eine:
  `TTAVItem::firstAc3TrackIndex()` liefert den Scan-Ort, und das Kontextmenü
  des Markers ordnet jede Reparatur derselben Spur zu. Bei einer Aufnahme mit
  zwei AC3-Spuren (z. B. deutsch + Originalton) bleibt die zweite unbeachtet —
  eine Störung dort wird weder gefunden noch repariert, ohne Hinweis. Für den
  Ausbau: Scan-Task pro AC3-Spur starten (der Task kennt seinen `trackIndex`
  bereits), Markertext um die Spur ergänzen (steht schon drin), und im
  Reparatur-Dialog die Spur wählbar machen statt sie aus
  `firstAc3TrackIndex()` abzuleiten.
- **Ersatzframe-Bau meldet OOM und Schreibfehler nicht getrennt** (M1/M2 aus
  dem Final-Review, bewusst offen gelassen). `TTAudioRepair::buildRepairTable()`
  baut die komplette Tabelle im Speicher (`QMap<qint64, QByteArray>`); bei sehr
  langen Bereichen ist der Verbrauch unbegrenzt, und eine fehlgeschlagene
  Allokation innerhalb von libav wird als gewöhnlicher Fehler gemeldet, nicht
  als „zu wenig Speicher". Der Schnitt bricht in beiden Fällen sauber ab (die
  Fehlerkette stimmt), aber die Meldung führt den Nutzer nicht zur Ursache.
  Sinnvoll erst zusammen mit einer Obergrenze für die Bereichslänge.
- **Stereo-/MP2-Scan**: die LFE-Insel-Heuristik ist AC3-5.1-spezifisch und
  bewusst nicht auf Stereo/MP2 übertragen (Spec-Entscheidung: Fehlalarmrisiko
  ohne eigene Kalibrierung).
- **Weitere Ersatzverfahren**: v1 kennt nur „Stille mit Randfades"
  (`Method`-Feld ist für Erweiterung vorgesehen). Interpolation
  (autoregressive Vorhersage aus Randsamples, nur für kurze Störungen) und
  Raumton-Ersatz (Nachbarschafts-Atmo, Center aus gedämpfter L+R-Summe,
  Rauschsynthese nach Spektralprofil) brauchen eigene Parameter + Hörtests.
- **Begriffskollision „Tonstörung" vs. „Tonanomalie"** (beim Wiki-Audit zu
  v0.82.0 aufgefallen). Drei Namen für zwei Dinge: die Einstellung heißt im
  Dialog **„Tonstörung (AC3 5.1)"** (`ui/ttcutsettingsstreampoints.ui`), der
  vom Scan erzeugte Marker heißt **„Tonanomalie: C+LFE-Störimpuls …"**, und
  **„Tonstörungen: X–Y (Spur N)"** ist der Text der davon unabhängigen
  Marker, die `ttcut-audiofix` beim Demux für echten Strukturschaden setzt
  (Junk-Regionen mitten im Strom und CRC-defekte Frames; die angeschnittenen
  Rahmen an den Rändern jeder Aufnahme erzeugen seit v0.82.6 keinen Marker
  mehr). Der Nutzer sieht damit „Tonstörung" an zwei Stellen mit verschiedener
  Bedeutung. Vorschlag: Einstellungs-Label auf „Tonanomalie (AC3 5.1)"
  ziehen (nur `.ui` + Übersetzung, keine Code-Logik). Nicht mehr in v0.82.0
  gemacht, weil die Version bereits veröffentlicht war; das Wiki erklärt die
  Lage bis dahin.
- **Sprachunabhängiges Suffix-Abschneiden beruht auf einer Handliste**
  (Restbefund R6, per Code-Prüfung abgesichert, nicht gemessen).
  `TTStreamPoint::repairPlannedSuffixVariants()` /
  `repairDisabledSuffixVariants()` (`data/ttstreampoint.{h,cpp}`) führen jede
  bekannte Sprachvariante der Marker-Zusätze literal auf (aktuell EN + de_DE),
  damit ein in Sprache A angehängter Zusatz beim Neu-Antasten in Sprache B
  erkannt und nicht verdoppelt wird. Jede neue `.ts`-Übersetzung braucht dort
  einen manuellen Eintrag — wird er vergessen, entsteht genau das doppelte
  Suffix, das der Fix verhindern soll. Kein Testfall wechselt zur Laufzeit die
  Oberflächensprache und prüft das Abschneiden. Sauberer wäre, den Zustand am
  Datenmodell zu führen statt im Anzeigetext.
- **Wächter gegen Scan-Re-Entrancy im Screenshot-Modus ist nicht scharf
  erprobt** (Restbefund R2, ebenfalls nur code-geprüft).
  `TTCutMainWindow::onAnalyzeStreamPoints()` bricht bei
  `mStreamPointWorkersRunning > 0` ab, weil `runScreenshotMode()` die Methode
  direkt aufruft und dabei auf einen noch laufenden Auto-Scan aus
  `maybeStartAutoAnomalyScan()` treffen kann (sonst würde der Worker-Zähler
  genullt und „Analyse fertig" zu früh gemeldet). Kein Screenshot-Lauf hat das
  Rennen tatsächlich provoziert; über die normale GUI ist der Pfad nicht
  erreichbar.
- **Scanner als Standalone-Ableger für VDR_Demux.sh-Batch** (Nutzeranregung):
  der Scan läuft heute nur als Hintergrund-Task in TTCut-ng nach dem Laden;
  ein CLI-Ableger könnte den gesamten Korpus batch-scannen, ohne jede Datei
  einzeln in der GUI zu öffnen.

## Low Priority

- **Der Audio-Schnittpfad `TTAVStream::copySegment()` ist toter Code**
  (2026-08-31, beim Randmeldungs-Vorhaben aufgefallen)
  - `TTMPEGAudioStream::cut()` und `TTAC3AudioStream::cut()` überschreiben die
    rein virtuelle `TTAVStream::cut()` und rufen als einzige `copySegment()`
    auf — aufgerufen wird aber keine der beiden. Im ganzen Projekt gibt es vier
    `cut(...)`-Aufrufstellen, und keine trifft Audio: Video in
    `ttmpeg2videostream.cpp` und `ttcutvideotask.cpp`, Untertitel in
    `ttavdata.cpp`, dazu der Harness `test_mpeg2_cutout.cpp`. Der Audio-Schnitt
    läuft seit dem libav-Umbau über `TTFFmpegWrapper::cutAudioStream()`.
  - `copySegment()` selbst bleibt am Leben: `TTMpeg2VideoStream::checkIFrameSequence()`
    ruft es für den Sequenzheader-Block auf. Nur der Audio-Weg dorthin ist tot.
  - Folgenlos, aber irreführend: `copySegment()` wertet den Rückgabewert von
    `readByte()` nicht aus und schreibt immer die volle angeforderte Länge —
    am Dateiende also ungelesene Pufferreste. Über den lebenden Video-Aufruf
    ist das unerreichbar (beide Adressen sind Header-Offsets aus der Liste und
    liegen per Konstruktion in der Datei), über den toten Audio-Weg wäre es
    ein echter Defekt gewesen.
  - Für den nächsten Dead-Code-Audit: die beiden `cut()`-Überschreibungen
    entfernen und prüfen, ob `TTAVStream::cut()` danach noch rein virtuell
    sein muss.

- **Belege gehören nicht nach `CLAUDE_TMP`** (2026-08-23, offen, niedrige
  Priorität). Beim Sitzungsabschluss gemessen: von 42 Pfaden, auf die
  `TODO.md`, `docs/` und das Gedächtnis in `/usr/local/src/CLAUDE_TMP/TTCut-ng/`
  verweisen, existierten **41 nicht mehr** — vernichtet beim Purge am
  2026-08-16. Das Verzeichnis ist von der Sicherung ausgeschlossen
  (`excludes-root.txt`), überlebt also weder ein `rm -rf` noch einen
  Plattenverlust.

  71 der 76 einzelnen Verweise waren bereits als verloren gekennzeichnet; die
  restlichen fünf sind es seit heute. Gerettet wurde dabei das einzige noch
  lebende Stück: `av_sync_check/measure_sync.py` liegt jetzt als
  `tools/diag/measure_av_sync.py` im Repo.

  **Offen ist die Regel dahinter**: Messwerkzeuge, Baselines und Repro-Material,
  auf die TODO oder Memory verweisen, müssen beim Entstehen an einen gesicherten
  Ort — ins Repo oder unter `/home/` —, nicht ins Temp-Verzeichnis. Solange das
  nicht festgelegt ist, wiederholt sich der Verlust beim nächsten Aufräumen.


- **Restversatz an Störzonen: welche Gaplänge ist die richtige?**
  (2026-08-24, offen, niedrige Priorität — Größenordnung einer halben
  Framedauer bis ~150 ms je nach Codec.)

  Nach dem Störzonen-Umbau bleibt auf der Referenzaufnahme
  `SDTV/…RTLup-Remington-Steele-03x15` ein konstanter Versatz von ~150 ms an
  der Segmentnaht: Bild vor der Naht 1079 Frames = 43 160 ms, Ton dort
  43 300–43 320 ms. Ursache ist, mit welcher Größe die Gaplänge gemessen wird.
  `detect_video_gaps` löst auf **DTS** aus, gibt als Grenzen aber **PTS** aus,
  und `build_disturbance_zones` bildet den Verlust aus der Differenz der
  Grenzen — bei B-Frame-Reorder sind das zwei verschiedene Zahlen.

  **Ein Umstellen auf die DTS-Länge wurde versucht und wieder verworfen**
  (2026-08-24). Auf MPEG-2 ist DTS klar richtig: 1079 Pakete + 665 Frames
  Lücke = 1744 gegen 1745 aus der PTS-Spanne, ein Frame Abweichung; mit der
  PTS-Länge sind es vier. Der Fix senkte den Restversatz dort messbar von
  ~150 ms auf 19–40 ms. Auf H.264 kehrt sich das aber um — gemessen an
  `HDTV/…The-Rookie-07x12`, 133 Lücken über fünf Segmente:

  | Segment | PTS-Spanne | Pakete+DTS | Pakete+PTS |
  |---|---|---|---|
  | 00001 | 2780,960 s | +0,280 | **+0,160** |
  | 00002 | 16,200 s | −0,040 | **±0,000** |
  | 00004 | 0,440 s | +0,080 | **−0,020** |

  Dort ist PTS durchweg näher, und die DTS-Länge würde die Video-Lücken um
  120 ms überschätzen — also zu viel Ton kürzen. Der Fix hätte den Fehler von
  MPEG-2 auf H.26x verschoben statt ihn zu beseitigen.

  Eine codec-abhängige Fallunterscheidung wäre messbar besser, wurde aber
  bewusst nicht gebaut: die Ursache des Unterschieds ist unverstanden, und
  eine Regel ohne verstandenen Grund bricht beim nächsten Codec wieder.
  Auffällig ist, dass auf H.264 **beide** Größen systematisch überschätzen
  (~0,2 Frames je Lücke, Segment 00001: real fehlen 559 Frames, PTS sagt 567,
  DTS 573). Das deutet darauf hin, dass die richtige Größe eine dritte ist —
  die tatsächlich fehlende Framezahl. Die sauber zu bestimmen wäre der
  eigentliche Einstiegspunkt für einen neuen Anlauf.

- **PTS-Umlauf macht die Lückenerkennung an dieser Stelle blind**
  (2026-08-23, offen, niedrige Priorität — Sonderfall per User-Einschätzung).
  Der 33-Bit-Zeitstempel läuft alle 2³³/90000 = 95443,718 s (26,5 h) auf 0
  zurück. Beide Lückenerkennungen rechnen mit Differenzen roher Zeitstempel:
  `detect_video_gaps` prüft `curr_dts - prev_dts > threshold`, die
  Multifile-Varianten `nächster_Anfang - vorheriges_Ende > threshold`. Am
  Umlauf ist diese Differenz stark **negativ**, die Prüfung greift nicht — es
  wird nie eine Lücke erfunden, aber eine echte an dieser Stelle übersehen.

  Belegt an `SDTV/MPEG2_SD576i25_16-9_multifile-2part-ptswrap_MP2-deu+eng_Comedy-Central`
  im Testkorpus (Details in dessen `BESCHREIBUNG.md`): letzte PTS von Segment 0
  bei 95386,744 s, Umlauf bei 95443,718 s, erste PTS von Segment 1 bei
  201,426 s. Bild und Ton verlieren die 258 s gleichermassen, der Sync leidet
  also nicht — nur die Meldung fehlt.

  **Nachgemessen 2026-08-24, nach dem Störzonen-Umbau.** Herausgerechnet sind es
  258,480 s Video- und 258,744 s Audio-Lücke, Bilanz +264 ms. Der frühere Grund,
  es nicht anzufassen (die Grenze würde doppelt korrigiert, weil
  `detect_segment_boundaries` dort ohnehin eine Zeile schrieb), ist mit dieser
  Funktion entfallen. An ihre Stelle tritt ein anderer, grösserer:
  `build_disturbance_zones()` sortiert **alle** Fenster global nach Startzeit.
  Eine wrap-korrigierte Nahtlücke läge bei 95386…95645, jede Lücke innerhalb des
  Folgesegments bei 201…3140 — die Naht sortierte ans Ende, und der Offset würde
  in falscher Reihenfolge akkumuliert. Genau die Fehlerklasse, die der Umbau
  beseitigt hat. Eine Behandlung muss deshalb **alle** Segment-PTS auf eine
  durchgehende Achse normalisieren, nicht nur die Splice-Differenz. Deutlich
  mehr Aufwand als „ein Vorzeichen richtigstellen".


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
  Zwei der vier Punkte sind erledigt (synchrone Stop-Lesung; `frameRate==0`-
  Absicherung + Destruktor-Cleanup) — Belege in `docs/completed-work.md`,
  GUI und Wiedergabe. Offen bleiben:
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
  - Update all includes, CMakeLists.txt, .ui references, and moc references
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
  The former deliberate gap — a cancel during MPEG-2 *preview* generation
  was a silent no-op, because `startNested()` keeps its task out of the
  queue `onUserAbortRequest()` broadcasts over — is CLOSED since
  `0bd07c93` (2026-08-15): `TTCutPreviewTask::onUserAbort()` forwards the
  cancel to the nested video task itself, and it takes effect within the
  clip (measured: a cancel armed 36 ms into a clip used to let it finish
  2.4 s later; now it stops inside it and removes the half-written files).
  Four sharp edges worth knowing when testing:
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
  - Tools/logs: `tools/diag/window-geometry.sh` (im Repo, erhalten). The
    CLAUDE_TMP material (`kwin-*.{sh,log}`, `wayland-diff/`, `kwin-bugreport/`
    incl. BEFUND.md — despite its "keep" marker) was **destroyed in the
    2026-08-16 CLAUDE_TMP purge** (no backup; see memory
    `reference_claude_tmp_purge_2026_08_16`). The surviving record is the
    investigation summary in `docs/completed-work.md` (GUI und Wiedergabe)
    and the kwin-fractional-scale-bug memory — raw logs and helper scripts
    would have to be re-created for an upstream report.
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
