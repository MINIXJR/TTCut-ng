# TTCut-ng TODO / Feature Requests

Offene Punkte und bekannte Einschränkungen. Erledigtes steht mit seinen
Belegen in [docs/completed-work.md](docs/completed-work.md).

## High Priority

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

- **Der Projektlader umgeht die Verträglichkeitsprüfung zweier Videos**
  (Code-Audit Lauf 6, 2026-09-14)
  - `TTCutProjectData::parseVideoSection` ruft `TTAVData::doOpenVideoStream`
    je `<Video>`-Abschnitt, und `parseCutSection` hängt die Schnitte über
    `TTAVItem::appendCutEntry` direkt an — beides ohne `TTAVItem::canCutWith`,
    das beim Anlegen über die GUI jedes weitere Video gegen alle bereits
    geladenen prüft (gleicher Codec, gleiche Bildrate, gleiche Tonspurzahl,
    bei MPEG-2 auch Seitenverhältnis und Bildgrösse).
  - Folge: ein von Hand geschriebenes `.ttcut` mit zwei Videos
    unterschiedlicher Codecs lädt. Der Encoder-Codec wird dann aus dem
    aktuellen Element gesetzt, die Weiche in `TTAVData::onDoCut` entscheidet
    aber nach Eintrag 0 der Auftragsliste — die beiden können auseinanderlaufen.
  - Beschrieben in `docs/code-map/cut-edit-and-start.md` (Fallstricke,
    Befund 5).

- **Weitere geteilte Temp-Namen** (2026-08-12, offen, niedrige Priorität)
  - Dieselbe Bauform steht noch an zwei Stellen: `data/ttpreviewclip.cpp` und
    `data/ttcutpreviewtask.cpp` — beide schreiben `preview_audio_temp.<ext>`
    und `preview_video_temp.<ext>` (der Neubau lag bis 2026-09-21 in
    `gui/ttcutpreview.cpp`). Zwei gleichzeitig offene Fenster benutzen
    dieselben Namen. (Die dritte Stelle, das
    Wiedergabe-MKV in `gui/ttcurrentframe.cpp`, trägt seit 2026-09-11 einen
    Namen je Mux — Pflicht für den asynchronen Abbruchpfad, siehe
    `docs/completed-work.md`.)
  - Kein gemessener Fehlerfall — deshalb nicht mitgefixt. Wer es angeht:
    dasselbe Muster wie in `encodePart()` (`QTemporaryDir` je Vorgang).

- **ttcut-demux: bash + ffmpeg-CLI → libav-Library-Migration**
  - `tools/ttcut-demux/ttcut-demux` ist aktuell ein bash-Script (~3050 Zeilen, Stand v0.82.1) das ffmpeg-CLI-Subprozesse spawnt für: TS-Demux, Audio-Trim, Audio-Padding, Audio-Gap-Repair, PTS-Analyse, etc.
  - Der Rest der TTCut-ng-Pipeline ist bereits auf libav umgezogen (v0.60.0): TTAudioCutter::cut(), TTMkvMergeProvider, TTFFmpegWrapper, etc. — kein ffmpeg-CLI mehr (nur noch mplex für MPEG-2-Multiplex).
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
    (`avstream/ttframeindexer.cpp`, `ttmkvmergeprovider.cpp`) nutzen die nur lesenden
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
  **Herabgestuft 2026-09-11 (User-Entscheid):** betrifft nach Code nur den
  Fall zweier AC3-Spuren — Scan (`TTAudioAnomalyScanTask`, AC3-5.1-Heuristik
  auf 32-ms-Rahmenraster) und Spurwahl (`firstAc3TrackIndex()`, prüft
  `streamType() == ac3_audio`) sind AC3-spezifisch, MP2/Stereo ist per
  Spec-Entscheid ausgenommen (eigener Eintrag „Stereo-/MP2-Scan"). Zwei
  AC3-Spuren kommen bei DVB-Aufnahmen nach Erfahrung des Anwenders nicht
  vor.

- **SIGSEGV nach Smart Cut in `doH264Cut` — einmaliger Absturz, herabgestuft
  2026-09-07.** Ein Use-after-free am 2026-08-07 (Backtrace endete in
  `QAbstractProxyModelPrivate::_q_sourceModelDestroyed` unter `doH264Cut`);
  seither keine Wiederkehr. Die Forensik samt sechs sauberen ASAN-Nachbauten
  und der am 2026-09-07 gemessen widerlegten KIO-Dialog-Hypothese steht in
  `docs/completed-work.md` (GUI und Wiedergabe). Der Code-Stand des Absturzes
  existiert seit `9e1e502c` nicht mehr (H.26x-Schnitt läuft im Pool statt
  synchron im GUI-Thread), Core und Backtrace-Dump sind gelöscht — es gibt
  nichts mehr nachzustellen.
  **Bei Wiederauftreten:** Core sichern (`coredumpctl dump`), `thread apply
  all bt` mit Qt-Debugsymbolen (`DEBUGINFOD_URLS` setzen oder
  `libqt6core6t64-dbgsym`), prüfen ob Frame #1 wirklich der Proxy-Slot ist
  und welches Objekt `this` war. ASAN-Build:
  `cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`; Harnisch
  `tools/diag/test_mainwindow_then_cut` (`PROBE_TRACE_DEFERRED=1` zeigt, was
  in den Ereignisschleifen aufgeschoben gelöscht wird).

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

- **Qualitäts-Fahrplan**: Reihenfolge der Qualitätsarbeit (Gate-Läufer →
  Karten der unkartierten Hauptfunktionen → Audit nach Karte) mit Stand und
  Regeln in [docs/quality-roadmap.md](docs/quality-roadmap.md).

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
  1. **Detection is edge-only.** `TTAudioCutter::detectBurst()` analyses a 200 ms
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
     repeated clicks, and every click costs a full clip rebuild (Smart Cut, audio,
     mux). Note the shift moves the cut by one *video* frame (40 ms @ 25 fps)
     while an AC3 audio frame is 32 ms — the two grids do not align.

     **Design worked out 2026-09-22, deferred by the user** ("bringt mich aktuell
     nicht weiter"). Not blocked on anything — pick it up as written:
     - `TTAudioCutter::detectBurst` already computes the RMS of EVERY audio frame
       in the 200 ms window (`rmsValues`) and only *tests* the outermost two. The
       length of the loud run is therefore available at zero extra I/O: keep
       walking inward from the boundary while `rms - median >= minDeltaDb` and the
       chunk clears the absolute floor. New out-parameter with a default value, so
       the existing callers stay untouched.
     - `TTAVData::CutBurstInfo` carries the run length on, converted to video
       frames; the button labels itself with the distance instead of a fixed
       "1 Frame" (needs a `%1` translation entry) and moves it in ONE
       `moveCutEdge` — one clip rebuild instead of N. `wouldInvert` then guards
       the whole distance.
     - **The bound falls out of the measurement, no invented number:** a loud run
       that reaches the inner end of the window has no measurable end — exactly the
       level-step case of Pitfall 5. The button then stays at one frame and the row
       says the burst reaches past the window. In the one-frame case the button
       behaves exactly as today, so the change can only add.
     - Gate: synthetic AC3 with a burst of exactly N audio frames (N = 1, 2, 3)
       plus one that fills the window; assert the reported distance. Unlike the
       *detection* widening above, this needs no scarce real material.

     **What the 2026-09-22 spike removed from its rationale:** the click at an AC3
     format change is NOT in the audio (see the separate entry below), so the
     button cannot help there. What remains is the ordinary advertising burst, and
     the gain is limited to the 2-3 frame case.

  Orthogonal and already solved: the context-relative threshold `burstMinDeltaDb`
  (v0.72.0) fixed false negatives on quiet programme material; the 2026-07-09 rewrite
  passed that threshold into the detector, removed the redundant post-filter, and made
  the detector report the **peak** of the tested chunks instead of the first one above
  the threshold. None of this addresses the two gaps above — both remain open.

- **Das Knacksen am AC3-Formatwechsel steht nicht im Ton — mpv baut den
  Ausgabeweg neu auf** (gemessen 2026-09-22, offen)

  Ausgangsfrage des Users: ist der Knacks beim Formatwechsel (5.1 ↔ 2.0)
  überhaupt im Strom, oder entsteht er erst bei der Wiedergabe? Gemessen an
  einem eigens gebauten Fixture (440 Hz durchgehend, nur die Kanalzahl wechselt
  bei 8,0 s und 12,0 s — die Tonhöhe bleibt gleich, damit der Sprung nicht vom
  Inhalt kommt):

  - **mpv reißt den Ausgabeweg ab und baut ihn neu auf**, an jeder Wechselstelle:
    drei `Trying audio driver` statt einer, zwei `drain timeout` dazwischen, und
    der PCM-Schreiber fing die Datei neu an (8,0 s statt 20 s Ausgabe). Mit
    `--audio-channels=stereo`: **eine** Öffnung, **null** timeouts.
  - **TTCut ist genau so eingestellt.** `gui/ttmpvlibbackend.cpp` setzt acht
    mpv-Optionen, `audio-channels` ist keine davon. Und
    `TTCurrentFrame::buildPlaybackMuxParams` hängt `audioStream->filePath()` an —
    die **Quell**-Tonspur, ungeschnitten und unvereinheitlicht; MPEG-2 spielt die
    Quell-ES ohnehin direkt. Beim Sichten ist der Wechsel also voll da.
  - **Im Ton selbst kein Befund.** Weder die Messung von 2026-07-09 an
    `TEST_deu.ac3` noch diese zeigt einen lauten Transienten. Der kleine Sprung,
    den eine Herunter-Mischung erzeugt, ist eine Eigenschaft der Mischung: über
    eine Layout-Grenze hinweg gibt es ohne eine solche Entscheidung gar keine
    gemeinsamen Abtastwerte.

  **Umgesetzt 2026-09-22** als Einstellung „Kanalaufteilung bei der Wiedergabe"
  im Audio-Tab: `Original` (Vorgabe, ändert nichts) oder `5.1` (hält die Ausgabe
  bei AC3-Spuren fest). `TTMpvWrapper::channelsOptionFor` ist die einzige Stelle
  der Zuordnung und hält nur AC3 fest — ein Codec, der ohnehin nur Stereo kann,
  bringt mpv nie zum Umschalten. Beide Abspieler wenden es an (Hauptfenster und
  Vorschau). Gate `mpv_channels` prüft die Zuordnung und zählt in einer echten
  libmpv-Instanz die AO-Initialisierungen: drei ohne die Option, eine mit.
  Der Schnitt ist nicht berührt.

  **Der Hörtest ist gelaufen und blieb negativ (2026-09-22).** Der Abriss ist
  gemessen, seine Hörbarkeit nicht — und der Knacks, den der User kennt, ist
  damit NICHT erklärt. Die Einstellung bleibt trotzdem drin, um sie an echtem
  Material umschalten zu können, sobald eines auftaucht.

  Geprüft wurde, in dieser Reihenfolge:
  - `TEST_deu.ac3` (Wechsel 83,808 s / 624,128 s): nichts hörbar. **Messfalle:**
    an beiden Stellen ist es leise — RMS-Senke auf −53 dB bzw. Stille bei −95 dB.
    Untaugliches Material, sagt in beide Richtungen nichts.
  - Vollständiger Durchlauf der übrigen Aufnahmen: `05x02`, `05x03` ohne Wechsel;
    `05x04` drei Wechsel (alle fallen auf −114 bzw. −120 dB ab); `The Silent Hour`
    zwei, davon einer bei 6161,504 s in durchgehend lautem Material (−25 dB, keine
    Senke) — **dort ebenfalls nichts hörbar.** Das ist das belastbare Negativ.
  - Synthetischer Härtefall: Dauerton 440 Hz, Kanalwechsel bei 38,0 und 42,0 s,
    **pegelgleich gemacht** — die erste Fassung legte denselben Ton auf alle
    sechs Kanäle und war dadurch im Downmix 3 dB lauter, was als Pegelsprung zu
    hören war und den Test verfälscht hätte. In der bereinigten Fassung: kein
    Unterschied zwischen `Original` und `5.1`, und **es klingt nicht wie der
    Knacks, den der User kennt**.

    Die Datei lag in `CLAUDE_TMP` und ist gelöscht — dieses Verzeichnis ist von
    der Sicherung ausgeschlossen, ein Verweis darauf wäre in ein paar Wochen
    tot gewesen. Sie ist in Sekunden wiederherstellbar; der 5.1-Abschnitt trägt
    den Ton NUR auf FL/FR, sonst entsteht der genannte Pegelsprung:

    ```bash
    ffmpeg -y -f lavfi -i "sine=frequency=440:duration=38:sample_rate=48000" \
        -ac 2 -c:a ac3 -b:a 448k s1.ac3
    ffmpeg -y -f lavfi -i "sine=frequency=440:duration=4:sample_rate=48000" \
           -f lavfi -i "anullsrc=r=48000:cl=mono:d=4" \
        -filter_complex "[0:a][0:a][1:a][1:a][1:a][1:a]join=inputs=6:channel_layout=5.1[a]" \
        -map "[a]" -c:a ac3 -b:a 448k s2.ac3
    ffmpeg -y -f lavfi -i "sine=frequency=440:duration=78:sample_rate=48000" \
        -ac 2 -c:a ac3 -b:a 448k s3.ac3
    cat s1.ac3 s2.ac3 s3.ac3 > klicktest_deu.ac3
    ```

    Dazu ein beliebiges 120-s-Video als Bildspur (z.B.
    `tools/test-videos/cache/tux_h264_1080p_progressive_test.264`) und eine
    `.ttcut` mit `<Video>`/`<Audio>` darauf.

  **Vorrangige Hypothese für das echte Knacksen ist damit die Wiedergabekette
  selbst, nicht der Formatwechsel.** Präzedenzfall im Korpus-Inventar
  ([[reference_test_corpus_naming]], Astra-UHD1-Eintrag): ein gemeldetes Knistern
  reproduzierte sich unter VDR *und* mpv, die Tonspur war sauber gemessen, Ergebnis
  war die Ausgabekette (Teufel Soundbar One) — mit dem Vermerk, das nicht erneut
  als TTCut-Problem zu untersuchen. Dieselbe Hardware. Wer hier weitermacht,
  braucht zuerst eine Aufnahme, bei der der Knacks reproduzierbar auftritt.

  Die fertige MKV ist nicht betroffen: der Schnitt vereinheitlicht den
  acmod (gemessen 2026-09-22, 626 Rahmen ohne Wechsel).

- **Cut point stutter (rare)**: For streams without any IDR frames (only Non-IDR I-slices), Smart Cut re-encodes 1 GOP at each segment boundary to produce an IDR. This is typically invisible but may cause minor quality differences at cut points (~0.5% of frames affected). When B-frame reorder delay shifts CutIn past the stream-copy keyframe (Case B), a small leak of ≤ reorder_delay pre-CutIn frames may occur to avoid POC domain mismatch.

- **Stop still-frame offset ~5 frames (mpv playback)**: When stopping playback in the "Current Frame" widget, the displayed still jumps ~5 frames (~200 ms) relative to the image visible when STOP was clicked. Cause: with `vo=libmpv` (in-process rendering for native Wayland support) mpv does not display frames itself but hands them to our `paintGL`. The mpv clock (`time-pos`) runs ahead of the frame actually rendered into the FBO by a fixed pipeline depth. A built-in fix (`lastRenderedTimePos` instead of `time-pos` as stop position) reduces the jump from ~16 to ~5 frames. Playback itself is smooth; only the frozen still is affected, the cut position is unaffected. The old `vo=x11` backend did not have this because mpv displayed frames itself (clock = visible frame). Deeper fix see TODO (Low Priority, "TTMpv-Wrapper: Folge-Verbesserungen"): requires a separate render thread or a future libmpv extension; `report_swap` and `ADVANCED_CONTROL` were tested and rejected (no effect / blocks the stop path).
