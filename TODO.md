# TTCut-ng TODO / Feature Requests

## High Priority

- **MPEG-2: Cut-Out auf B-Frame verliert bis zu M−1 Frames** (2026-07-10, BELEGT)
  - **Reproduziert** mit `tools/diag/test_mpeg2_cutout` auf zwei echten DVB-Streams:
    - `TEST.m2v` (M=3): `cut(0,5)` fordert 6 Frames, schreibt 4 → 2 fehlen
    - Futurama 02x01 (Comedy Central, VDR, M=4): `cut(0,7)` fordert 8, schreibt 5 → 3 fehlen
    - Cut-Out auf I/P ist immer korrekt; Cut-Out auf B verliert alles zwischen dem
      letzten I/P und der Cut-Out-Position.
  - **Ursache** (`getCutEndObject`, `avstream/ttmpeg2videostream.cpp`):
    ```
    if (bFrameCount > 0 && cutOutPos <= ipFramePos + bFrameCount)
        cutParams->setCutOutIndex(cutOutPos);
    ```
    `ipFramePos` ist ein **Display**-Index, `bFrameCount` zählt B-Frames in
    **Bitstream**-Reihenfolge nach dem I/P — bei IBBP sind das die Frames, die
    *davor* angezeigt werden. Dadurch wird `cutOutIndex` auf `cutOutPos` gesetzt,
    `cut()`s Bedingung `cutOutPos > getCutOutIndex()` wird falsch und der
    Tail-`encodePart()` unterbleibt.
  - **Reichweite:** `TTSettings::mEncoderMode` ist per Default `true`; nur dieses
    Flag entscheidet, ob die GUI (`ttcutframenavigation.cpp`) B-Frames als Cut-Out
    anbietet. `isCutOutPoint()` wird im Cut-Pfad **nicht** erzwungen.
  - **Nicht einfach löschen:** Die Zeile kam mit `bb83d60` (2026-03-21) — ein
    Commit über i18n und Progress-Scaling, in dem diese Änderung unerwähnt mitlief.
    Ihr Kommentar beansprucht, doppelte B-Frames zu verhindern. **Welcher** GOP-Fall
    das war, ist ungeklärt. Erst diesen Fall reproduzieren, dann Design, dann Fix.
  - **Testmaterial:** `TEST.m2v` reicht für Fix + Regression. Kontrollfälle:
    Nur-I/P-Stream (`ffmpeg -bf 0`, dort ist `bFrameCount == 0`), Open-GOP.
  - Map: [docs/code-map/mpeg2-cut.md](docs/code-map/mpeg2-cut.md)

- **MPEG-2 field-picture: Cut-Positionen zählen Felder statt Frames** (2026-07-10, BELEGT)
  - `createIndexList()` legt pro `picture_start_code` einen `TTVideoIndex` an, also
    **zwei Einträge pro Field-Picture-Frame** (gleicher `temporal_reference` ⇒ gleicher
    `display_order`). `mExtraIndices` markiert den Zweiteintrag, **entfernt ihn aber nicht**.
  - Gelesen wird `extraIndices()` nur von der Audio-Schnittkorrektur (`data/ttavdata.cpp`)
    und der Standbildanzeige (`gui/ttcurrentframe.cpp`) — **nicht** vom Video-Cut.
  - Messung Futurama 02x01: 85 720 Index-Einträge / 85 495 echte Frames (222 Feldpaare,
    erstes bei Picture 11 673, mitten im Stream geclustert, nicht am Anfang).
  - Folge: Eine Cut-Position driftet gegen die echte Frame-Nummer um die Anzahl der
    vorangehenden Feldpaare.
  - **Unabhängig vom Cut-Out-Defekt oben** — die ursprüngliche Vermutung, Feldpaare
    würden `bFrameCount` aufblähen und den Fehler vergrößern, ist durch Messung
    **widerlegt** (Cut-Outs im Feldbereich verlieren genau 1 Frame wie sonst auch).
  - Zu klären: Soll die Cut-Achse Frames zählen (Zweitfelder aus `index_list` entfernen)
    oder bleibt sie feldbasiert und alle Konsumenten korrigieren?

- ~~**TTCut-ng Cut-Pipeline A/V Drift bei MPEG-2 mit field-picture-encoding**~~ → **RESOLVED** (2026-05-13, branch `feature/mpeg2-field-picture-fix`)
  - Root Cause: Field-Picture-Detection im MPEG-2-Parser (`picture_coding_extension` nicht gelesen, jeder picture_start_code als Frame gezählt, doppelte Zählung bei field-picture-encoded Frames). Fix in `avstream/ttmpeg2videostream.cpp` + Pipeline-Wiring in `data/ttavdata.cpp`. Spec: `docs/superpowers/specs/2026-05-12-mpeg2-field-picture-fix-design.md`.
  - Validation: Audio_a_sync.m2v Cut [60s..2400s] zeigt im Verlauf perfekt 0ms drift an mehreren Sample-Points (0/600/1200/1800/2300s). Pre-fix war 11.85s Drift. Der vorher gemeldete 104ms "Rest-Drift" war End-PTS-Asymmetrie-Artefakt (Frame-Duration 24ms+40ms quantisiert End-Diff bis ~64ms) + 2 Frames cutOut-Snap, ohne dass Verlauf-Inhalt asynchron ist.
  - Lessons Learned: A/V-Drift-Diagnose IMMER mit Verlauf-Sample-Points, nicht End-PTS allein. Memory: [feedback_av_drift_diagnosis.md](memory/feedback_av_drift_diagnosis.md)
  - Memory: [project_av_drift_cut_pipeline.md](memory/project_av_drift_cut_pipeline.md)

- ~~**Security Audit Findings beheben**~~ → **25/25 FIXED** (2026-03-28, commits aea1809 + 66eacb2)
  - Siehe [docs/security-audit-2026-03-02.md](docs/security-audit-2026-03-02.md) für alle Findings

- ~~**Smart Cut Quality Test Suite**~~ → **DONE** (`tools/ttcut-quality-check.py` + `verify-smartcut` skill)





- **Logo für TTCut-ng**
  - Projekt braucht ein wiedererkennbares Logo/Icon für GitHub, Debian-Paket, Desktop-Launcher
  - Anforderungen: SVG (skalierbar), funktioniert als 16x16 bis 512x512, passt zu Video-Editing

- ~~**HEVC CRA-only Stream: Smart Cut Verifikation**~~ → **DONE** (v0.72.0)
  - Testfall: `Ausdrucksstarke_Designermode.265` (HEVC 4K 3840x2160, 50fps, CRA-only, has_b_frames=5)
  - Der reale Verifikationslauf im Zuge der Display-Order-Map-/RASL-Arbeit deckte
    zusätzlich einen echten Bug auf: die Display-Order-Map rankte die RASL-Leading-
    Pictures des ersten CRA, die jeder konforme Decoder (ffmpeg/mpv) verwirft →
    HEVC-Framezahl inflationiert, jede Framenummer um eine Konstante (+7 auf dem
    Referenz-DVB-Stream) verschoben. Fix: `TTLeadingPicClassifier` erkennt
    verworfene RASL dynamisch (NAL-Typ + `NoRaslOutputFlag`), sie werden aus der
    Display-Dimension gedroppt (`f85d659`, `455f9f3`, `6dc0ccf`).
  - Verifiziert end-to-end: `decodeFrame(N)`/Suche/Cut landen auf ffmpeg-Display-
    Frame N (Pearson r ≈ 1.0); voller HEVC-Cut startet exakt beim gewählten
    Display-Frame (keine Werbe-/Logo-Frames am Anfang); keine "backward
    timestamps"/"co located POCs". CRA korrekt nicht als IDR → Re-Encode
    (`ttnaluparser.cpp` / `ttessmartcut.cpp`).

- ~~**Smart Cut Performance: mmap statt QFile für Stream-Copy**~~ → **IMPLEMENTIERT** (2026-03-28, commits d80b918 + 2f3bb69)
  - `accessUnitPtr()` für Zero-Copy mmap Frame-Zugriff, Bulk-Write für ungepatche Segmente
  - Funktionale Verifikation de-facto erledigt: nachfolgende Smart-Cut-Refactors (reencodeFrames-Split
    9f31ede, buildFrameIndex-Split 38bb6ea) wurden bit-identisch via `ffprobe show_packets` verifiziert —
    der mmap-Pfad ist dabei mit abgedeckt. Offen bleibt nur eine optionale dedizierte Performance-Messung.

- ~~**Equal-Frame Search: H.264/H.265-Support fehlt**~~ → **DONE** (commit 24562c0)
  - `TTFrameSearchTask::decoderKindFor()` dispatcht codec-aware: `TTFFmpegWrapper` (YUV-API)
    für H.264/H.265, `TTMpeg2Decoder` für MPEG-2 — für Reference- und Search-Stream.
  - Algorithmus bleibt YUV-byte-delta (SSIM/cross-correlation wäre separate Verbesserung).

## Medium Priority

- **Smart-Cut Code-Map Findings prüfen** (2026-07-10, aus `docs/code-map/smart-cut.md`)
  - Punkte (1)–(3) **ERLEDIGT 2026-07-11** (Branch `refactor/redundancy-safe-batch`):
    - (1) PAFF-fallback-Zweig: unerreichbar bestätigt (Bedingungsanalyse) und samt
      exklusiver toter Helfer (`convertAUToIDR`, `convertSliceNalToIDR`) entfernt —
      372 Zeilen, reine Löschung (`3191d98`).
    - (2) `realStartAU` entfernt, Debug-Ausgabe erhalten (`1c0bd2b`).
    - (3) Korrigierter Befund: **zwei** Encoder→Copy-Kopien (der Inter-Segment-Block
      ist eine andere Rechnung und bleibt); vereinheitlicht in `bridgeFrameNum` mit
      korrekter IDR-Semantik + `writeEos` für die 4 EOS-Stellen (`df20bb3`,
      `24fea34`). Verifiziert: bit-identisch auf ServusTV/Moon_Crash/Petrocelli/
      Designermode, Pixel-identisch auf gezieltem IDR-Naht-Projekt (`servus_idr`,
      Sonde `tools/diag/probe_copystart`). Nebenbefund behoben: alter Standard-Zweig
      patchte IDR-`frame_num` (Verstoß gegen 7.4.3); alter Unification-Guard
      übersprang Wrap-auf-0-Keyframes.
  - **(4) Annahme `kExpectedEncoderLog2PocLsb = 4`** — OFFEN:
    - `pocDomainBridgeable()` entscheidet über den Zweig, *bevor* die echte Encoder-SPS
      geparst ist. Ändert libx264 diesen Wert, fällt die Klassifikation still in die
      falsche Richtung (Seam als bridgeable eingestuft → erste kopierte GOP verworfen).
      Backstop ist nur die `warningMsg` in `applyPocDomainFix()`.
    - Prüfen, ob sich die Entscheidung nach dem ersten Encoder-Paket verifizieren lässt.

- **Vorschau: am letzten Frame stehenbleiben statt zum ersten Frame zurückspringen**
  - Aktuell springt die Vorschau am Ende der Wiedergabe zurück zum ersten Frame:
    `TTCutPreview::onPlayerFinished()` (`gui/ttcutpreview.cpp:270`) lädt die (bereits
    fertige) Preview-MKV via `mPlayer->load(current_video_file, 0.0, …, autoPlay=false)`
    neu — pausiert an Position 0. Grund ist nur, mpv nach EOF ohne Neustart in einen
    bespielbaren Startzustand zu bringen (kein MKV-Neubau — die Datei wird vorab von
    `TTCutPreviewTask` erstellt).
  - Gewünscht: am letzten Frame pausiert stehenbleiben.
  - Machbar und isolierbar: die Preview hat eine **eigene** mpv-Instanz
    (`ttcutpreview.cpp:54`), getrennt von der „Aktueller Frame"-Wiedergabe
    (`ttcurrentframe.cpp:510`) — eine Umstellung betrifft die Hauptwiedergabe nicht.
  - **Nötige Änderungen** (~3):
    1. `keep-open=yes` nur für die Preview-mpv-Instanz (Default ist `no`,
       `ttmpvlibbackend.cpp:73`) — mpv pausiert dann am letzten Frame statt auszulaufen.
    2. EOF-Erkennung umstellen: mit `keep-open=yes` entfällt `MPV_EVENT_END_FILE`/
       `playerFinished()` — stattdessen auf mpvs `eof-reached`-Property lauschen, um
       den Play/Stop-Button-Zustand zu setzen.
    3. `onPlayPreview()`: bei `eof-reached` vor dem Abspielen erst auf Position 0 seeken.
  - **Offene UX-Frage**: soll „Play" am Ende automatisch von vorne starten, oder erst
    nach einem expliziten zweiten Klick? → vor Umsetzung per brainstorming klären.

- **Schnittdialog: Button-Leiste überarbeiten + alle Dialoge auf einheitliches Design prüfen**
  - Im Schnittdialog („Schnitt-Optionen", `ui/avcutdialog.ui`, unteres H-Layout Z. 30–77)
    ist die Button-Reihenfolge `[Auf Standard zurücksetzen] [Starten] [Abbrechen]`, und
    **kein** Button hat `default=true` → Qt macht den ersten (`btnResetDefaults`) zum
    Default-Button (wird bei Enter ausgelöst, ist hervorgehoben). Unglücklich: die
    primäre Aktion (Starten) sollte der Default sein, nicht „Auf Standard zurücksetzen".
  - **Gewünschtes Layout (KDE-Konvention, mit User abgestimmt):** Reset links abgesetzt,
    rechts `[Abbrechen] [✓ Starten]`; Starten ganz rechts und als Default.
    - Layout-Reihenfolge: `laFreeSpace`, Spacer, `btnResetDefaults`, Spacer (neu),
      `cancelButton`, `okButton`.
    - `okButton`: `default=true` / `autoDefault=true`; `btnResetDefaults` + `cancelButton`:
      `autoDefault=false` (damit Enter zuverlässig Starten auslöst).
    - Keine Signal/Slot- oder Übersetzungsänderung nötig; vorher prüfen, ob der
      `gui/ttcutavcutdlg.cpp`-Konstruktor bereits einen Default-Button setzt.
  - **Ausweiten auf alle Dialoge — einheitliches Design:** übrige Dialoge (Einstellungen,
    Vorschau, About, QuickJump, …) auf konsistente Button-Leisten prüfen — primäre Aktion
    = Default-Button, einheitliche Reihenfolge/Aufteilung (Reset/sekundär links,
    Abbrechen + OK/primär rechts). Ziel: durchgängig gleiches Button-Layout im ganzen
    Programm.

- **ttcut-demux: bash + ffmpeg-CLI → libav-Library-Migration**
  - `tools/ttcut-demux/ttcut-demux` ist aktuell ein bash-Script (~1100 Zeilen) das ffmpeg-CLI-Subprozesse spawnt für: TS-Demux, Audio-Trim, Audio-Padding, Audio-Gap-Repair, PTS-Analyse, etc.
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

- ~~**Decode error detection for H.264/H.265 streams during demux**~~ → **DONE** (v0.63.0)
  - Implemented as `ttcut-pts-analyze` (formerly `ttcut-esrepair`): mmap-based start-code scanner,
    per-segment decode testing with custom AVIOContext, multi-threaded, integrated into ttcut-demux and TTCut-ng
  - H.265 false positives fixed: `AV_EF_CAREFUL` only for H.264/H.265 (not MPEG-2)

- ~~**Projektdatei-Endung: .prj → .ttcut**~~ → **DONE** (v0.63.0)
  - Neue Dateien: `.ttcut`, bestehende `.prj` behalten Endung
  - File-Dialog Filter: `"TTCut Project (*.ttcut);;Legacy Project (*.prj)"`

- **CLI Interface for batch Smart Cut (headless mode)**
  - Teilweise abgedeckt: `ttcut-ng --project <file> --auto-cut <out.mkv>` lädt ein `.ttcut`-Projekt
    und führt Smart Cut + Audio + MKV-Mux headless aus (für QC-Regression). Es bleibt aber die
    Qt-GUI-Anwendung — echte X11/Wayland-Freiheit fehlt.
  - Burst-Warndialog-Blocker BEHOBEN (v0.72.0, `27f8f29`): der modale Burst-Warndialog am finalen
    Schnitt wird im headless `--auto-cut`-Modus geloggt statt zu blockieren (`setNonInteractive`).
  - Offen: echtes Qt-freies Standalone-Tool, das `.ttcut` liest und ohne GUI-Event-Loop schneidet —
    läuft dann auch auf reinen Servern. Use case: VDR → demux → TTCut-ng CLI → archive

- ~~**Parallele Dekodierung mit mehreren FFmpegWrapper-Instanzen**~~ → **DONE** (Search-Performance-Refactor, gemerged d20a070)
  - `TTSearchTask` ist Coordinator mit lokalem `QThreadPool` + `parallelMap`; N Sub-Decoder
    (`TTSettings::searchWorkerCount`, Default 4) für Black-/Scene-/Logo-Suche
  - Scaling-Investigation: Sweet Spot 4-8 Worker, siehe `project_hevc_search_perf_investigation.md`

- ~~**Projektdatei: Fehlende Einstellungen speichern**~~ → **DONE** (v0.66.0)
  - Ausgabepfad, Dateiname, Suffix-Option, Mux-Settings, Encoder-Settings werden
    jetzt in `<Settings>`-Sektion der `.ttcut` Datei gespeichert
  - Beim Laden: Override der TTCut-Globals, beim Schließen: Restore aus QSettings
  - Codec-spezifisches Encoder-Mapping basierend auf Video-Typ
  - Rückwärtskompatibel: alte .ttcut Dateien ohne Settings-Sektion laden normal

- ~~**Dirty-Tracking: "Neues Projekt" Warnung nur bei echten Änderungen**~~ → Completed (v0.62.1)

- ~~**Manual audio delay/offset per track**~~ → **DONE** (v0.66.0)

- ~~**Schnittliste "Audio-Versatz" Spalte überarbeiten**~~ → **DONE** (v0.66.0)

- ~~**Audio-Drift Minimierung durch optimierte Rundungsstrategie**~~ → **DONE**
  - `TTAVData::planAudioCut()` in `data/ttavdata.cpp` snappt pro Segment auf
    Audio-Frame-Grenzen mit Feed-Forward-Kompensation des akkumulierten Drift
  - Drift bleibt steady-state ±½ Audio-Frame statt monoton zu wachsen
  - Alle drei Sites (MPEG-2 final, H.264 final, Preview) und Drift-Anzeige
    nutzen denselben Plan
  - Tote Funktionen `getStartIndex`/`getEndIndex` und `TTCutAudioTask` entfernt

- **Echte Fortschrittsanzeige für `cutAudioStream` / Audio-Only-Cut**
  - Aktuell springt der Balken pro Audiospur in einem Schritt, da `TTFFmpegWrapper::cutAudioStream` keine Pro-Packet-Progress-Callbacks liefert
  - Lösung: Optionalen `std::function<void(int percent)>` Callback in `cutAudioStream` einbauen, an `av_read_frame`-Loop koppeln (bekanntes Total über `endTime − startTime` pro Segment)
  - Audio-Only-Pfad in `TTAVData::doAudioOnlyCut` daraus echte Step-Updates emittieren
  - Auch dem MP3/AAC-Re-Encode-Pfad (Stage 2) gleich mitgeben

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

- **Dead-Code-Audit (Medium Priority)**
  - Systematische Suche nach toten Klassen/Funktionen/Includes (Beispiel:
    `TTCutAudioTask` blieb seit der v0.60.0-libav-Migration jahrelang stehen)
  - Vorgehen: clangd-Suche nach Klassen ohne lebende Caller, dann
    cross-check via grep, dann entfernen
  - Außerdem: ungenutzte includes in .cpp/.h entfernen (clangd `unused-includes`)
  - Sollte als wiederkehrender Wartungs-Pass laufen, nicht als Einmalaktion
  - Konkrete Funde 2026-07-09/10 (belegt, noch offen):
    - `AcmodInfo::cutInChangeTime` / `cutOutChangeTime` (`extern/ttffmpegwrapper.h`) —
      deklariert, in `analyzeAcmod()` auf `0.0` initialisiert, nie berechnet, nie gelesen.
      Gedacht war die Distanz des Formatwechsels zur Schnittgrenze.
    - `analyzeAcmod()` (Datei-Scan per Syncword, dient der Cut-Normalisierung) und
      `TTCutTreeView::updateAcmodIcon()` (In-Memory-`TTAudioHeaderList`, dient der Anzeige)
      implementieren die Mehrheits-acmod-Logik doppelt, mit verschiedenen
      Stichprobenbereichen → können verschiedene `mainAcmod` liefern.
    - `updateAcmodIcon()` liest `text(5)`/`toolTip(5)`/`icon(5)` aus dem Tree-Widget zurück,
      um seinen Text anzuhängen: Das Widget dient als Zwischenspeicher zwischen zwei
      Produzenten. `updateHintColumn()` kapselt die Reihenfolge seit `666ed08`, beseitigt
      die Append-Semantik aber nicht. Sauberer: beide liefern `{icon, text, tooltip}`
      zurück, ein Setter komponiert und schreibt einmal.

- Display the resulting stream lengths after cut
- Make the current frame position clickable (enter current frame position)
- ~~Prepare long term processes for user cancellation (abort button)~~ → **DONE**
  - `TTProgressBar` hat Cancel-Button → `TTAVData::onUserAbortRequest()` → `TTThreadTaskPool`;
    Cut-, Search- und QuickJump-Tasks werfen `TTAbortException` bei `onUserAbort()`

- ~~**FastForward-Player-Feature**~~ → **DONE** (TTMpv-Wrapper-Refactor)
  - Geschwindigkeits-Stufen −4×…1×…4× via mpv `speed`/`play-dir`, ◀◀/▶▶-Buttons +
    Tempo-Label im "Aktueller Frame"-Widget. Verwaistes `playSkipFrames`-Setting entfernt.

- **MP3/AAC re-encoding für Audio-Only-Output**
  - `audioOnlyBitrateKbps` Setting im Code vorhanden, UI ausgeblendet (v0.70.0)
  - Code-Stelle `data/ttavdata.cpp:1934` warnt "not implemented yet"
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
    (660 Einträge). Settings+Cut-Dialog (`ed2a531`/`d716c83`), Rest der App
    (`51e798b`..`7b3eec5`).
  - **Offen:** weitere Zielsprachen — je `ttcut-ng_<locale>.ts` anlegen, in
    `TRANSLATIONS` (`ttcut-ng.pro`) eintragen, mit `lupdate`/`lrelease` pflegen.
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

- **Systemanforderungen dokumentieren**
  - Mindestanforderungen für README/Wiki: Architektur (x86_64), OS, Qt, ffmpeg/libav, libmpeg2
  - Optionale Abhängigkeiten: mplex, mpv, ttcut-pts-analyze
  - Empfehlungen für Speicher/Plattenplatz bei großen DVB-Aufnahmen

## Low Priority

- **Code-Review-Follow-ups Redundanz-Branch** (2026-07-11, /code-review high über
  `refactor/redundancy-safe-batch`; die Korrektheits-/Log-Funde wurden direkt gefixt):
  - `cutAudioTracks`-Interface: `outPath`-Lambda transportiert an 2 Stellen
    Seiteneffekte (Datei-Löschen, statusReport) — sauberer wäre ein eigener
    Before-Hook oder Vorab-Löschen im Helfer (`data/ttavdata.h`).
  - Alle-Spuren-Überladung für `cutAudioTracks` (der 2-Zeilen-Boilerplate
    `QList<int> tracks; for(...)` steht an 3 Stellen).
  - `tools/diag/Makefile`: `test_mpeg2_cutout` linkt per Glob gegen alle obj/*.o
    (inkl. Qt5Widgets/mpv) statt kuratierter Objektliste — kaschiert die Kopplung
    von `TTMpeg2VideoStream` an GUI/mpv; bei Gelegenheit entkoppeln.

- ~~**Wayland: Ursache für `QT_QPA_PLATFORM=xcb`-Zwang ermitteln**~~ → **DONE** (v0.71.0, libmpv-Render-Backend)
  - Root Cause war das mpv-`--wid`-Embedding des alten Process-Backends. Mit dem
    libmpv-in-process-Render-Backend (vo=libmpv, `TTMpvRenderWidget` als
    `QOpenGLWidget`) entfällt das Fremdfenster-Embedding; TTCut-ng läuft nativ
    unter Wayland ohne `QT_QPA_PLATFORM=xcb`.

- ~~**Live-Timecode bei mpv-Wiedergabe**~~ → **DONE** (TTMpv-Wrapper-Refactor)
  - `TTMpvWrapper::positionChanged` (aus `observeProperty("time-pos")`) → der Timecode
    im "Aktueller Frame"-Widget läuft während der mpv-Wiedergabe live mit.

- **TTMpv-Wrapper: Folge-Verbesserungen** (aus Code-Reviews des Player-Refactors)
  - `TTMpvWrapper::stop()` ist „best-effort": der gestoppte Frame kann ~1 Frame ungenau
    sein (kein synchrones Warten auf das eingefrorene `time-pos`). Frame-genau wäre ein
    synchrones `getProperty` im `ITTMpvBackend`-Interface (bewusst weggelassen) oder ein
    kurzes Warten auf das `time-pos`-Event nach `pause` in `stop()`.
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
  - `createTempMkvForPlayback` (`gui/ttcurrentframe.cpp`): keine Absicherung gegen
    `frameRate==0` (Division → UB); kein Destruktor-Cleanup (Temp-MKV bleibt liegen,
    wenn das Fenster während H.264/H.265-Wiedergabe geschlossen wird).
    (Temp-Dateiname ist seit v0.71.0 eindeutig: `ttcut-ng_playback_temp.mkv`.)
  - **Erster PLAY pro Quelle ~5 s** (H.264/H.265): die ganze ES wird vor der
    Wiedergabe in eine temp-MKV gemuxt. Seit v0.71.0 wird die MKV über
    STOP→PLAY gecacht (Re-PLAY sofort), aber der erste Mux bleibt. Hebel:
    nur den abgespielten Bereich muxen, oder mpv die ES mit erzwungener
    Framerate direkt füttern. Prio low.

- **Auto-Cut from Markers** (ohne .info-Datei, z.B. bei ProjectX-Demux)
  - VDR-Marks werden bei ttcut-demux bereits automatisch als Cut-Einträge übernommen
  - Für manuelle Marker-Listen: Button der Marker-Paare in Cut-Einträge konvertiert
- **Rename TTMPEG2Window2 → TTVideoFrameWidget**
  - Class name and files (`mpeg2window/ttmpeg2window2.*`) are misleading — the widget handles MPEG-2, H.264, and H.265
  - Rename class, files, and directory (e.g., `videoframe/ttvideoframewidget.*`)
  - Update all includes, .pro file, .ui references, and moc references
- Implement plugin interface for external tools (encoders, muxers, players)
- GPU-accelerated encoding (NVENC, VAAPI, QSV) for faster Smart Cut

- ~~**ttcut-ng-Kommandozeilenoptionen im Wiki dokumentieren**~~ → **DONE** (Quickstart.md, 2026-06-03)
  - Nutzerrelevante Optionen (`<datei>`, `--project`, `--help`) als Abschnitt
    in `Quickstart.md` dokumentiert; Entwickler-/QC-Flags (`--screenshots`,
    `--auto-cut`) bewusst als intern gekennzeichnet. Erster Fund des neuen
    `wiki-audit`-Skills.

## Entwicklungs-Workflow

- **Verification-Test-Policy: Tux-Videos bevorzugen**
  - Bei Cut-Verification + Pipeline-Validation IMMER zuerst die Tux-Test-Videos verwenden
    (`tools/test-videos/cache/tux_*`). Kompakt (8-85 MB), reproduzierbar, im Repo.
  - Original-User-Videos nur bei neuen Problemen, die kein Tux-Test-Video reproduziert.
    Bei jedem solchen Fall ein neues Tux-Test-Video erzeugen (via `make_test_video.sh` o.ä.).
  - **Offen:** Tux-`.ttcut`-Files haben aktuell keine Cut-Entries — `--auto-cut`-Verification
    erfordert dass Cuts via Skript hinzugefügt werden. Helper-Script `make_tux_with_cuts.sh` wäre
    nützlich.

- ~~**Subagent-Driven Development: Build-Permissions für Subagents**~~ → **Konfiguriert 2026-05-19**
  - `.claude/settings.local.json` (lokal, gitignored) erweitert um `Bash(make:*)`,
    `Bash(make clean:*)`, `Bash(qmake:*)`, `Bash(bear -- make:*)`, `Bash(lrelease:*)`.
  - `Bash(bear:*)` und `Bash(lupdate:*)` waren bereits drin.
  - Bei nächstem Subagent-Driven-Run verifizieren ob ausreichend.

## Completed

- [x] H.264 open-GOP cold-start leading-picture alignment: non-IDR-I streams (`I B B B P`) no longer hang on load; map/still/search/cut match ffmpeg decoder (v0.72.1)
- [x] Frame-accurate H.264/H.265 cut-in and cut-out (TTDisplayOrderMap display↔decode, tail-GOP re-encode) (v0.72.0)
- [x] HEVC RASL leading-picture alignment: frame count/numbers match ffmpeg/mpv decoder (v0.72.0)
- [x] Display-PTS for smart-cut output MKV (H.264/H.265 + MPEG-2 temporal_reference) and playback temp MKV (v0.72.0)
- [x] Context-relative audio-burst detection with configurable threshold (burstMinDeltaDb) + cut-list refresh (v0.72.0)
- [x] H.264/H.265 Smart Cut support (TTESSmartCut)
- [x] SRT subtitle support
- [x] Replace mplayer with mpv for preview
- [x] Replace transcode with ffmpeg for MPEG-2 encoding
- [x] Connect encoder UI settings to actual encoders
- [x] MKV output via libav matroska muxer (originally mkvmerge, migrated to libav in v0.60.0)
- [x] MKV chapter marks support
- [x] A/V sync offset support for demuxed streams
- [x] New GUI layout with TreeView widgets and multi-input-stream support
- [x] Batch muxing via mux script generation
- [x] Preview: Next/Previous cut navigation buttons
- [x] Current Frame: Play button with audio (via mpv)
- [x] User warning when clicking "New Project"
- [x] Keyboard shortcuts (j/k for frame, g/G for home/end, [ ] for cut-in/out)
- [x] Warning if audio and video length differ
- [x] ttcut-demux: Audio trim at start for A/V offset correction
- [x] ttcut-demux: Audio padding at end (like ProjectX) - reduces drift from 372ms to 8ms
- [x] ttcut-demux: Duration mismatch detection and reporting in .info file
- [x] Preview widget: Corrected button order (Zurück/Start/Vor)
- [x] Fix thread-pool completion race condition (processEvents from worker threads → deadlock)
- [x] Fix AC3 parser infinite loop on E-AC3 streams (bsid > 10 detection + zero frame length guard)
- [x] ttcut-demux: E-AC3 streams get `.eac3` extension (was incorrectly mapped to `.ac3`)
- [x] Replace mkvmerge CLI with libav matroska muxer (v0.60.0)
- [x] Replace ffmpeg CLI audio cutting with libav stream-copy (v0.60.0)
- [x] Remove macOS support code (v0.60.0)
- [x] Remove 1,882 lines dead code from ttffmpegwrapper.cpp (v0.60.0)
- [x] Audio boundary burst detection with shift-button in preview (v0.59.0)
- [x] Audio quality fixes: click false positive, off-by-one duration, bitrate autodetect (v0.58.0)
- [x] Fix H.264 Smart Cut inter-segment stutter via forced-idr (v0.61.0)
- [x] Fix preview stutter by preferring IDR keyframes for preview clip start (v0.61.0)
- [x] Restore CutIn/CutOut editing and burst detection in navigation buttons (v0.61.0)
- [x] Fix frame position sync between slider and navigation buttons (v0.61.1)
- [x] Fix shared videoStream position corruption in navigation and cut points (v0.61.2)
- [x] Separate navigation from auto-save in CurrentFrame widget (v0.61.3)
- [x] Fix Smart Cut segment boundary stutter for B-frame reorder crossing — Case A/B (v0.61.4)
- [x] Fix CutOut frame display for last cut entry — H.264 EOF drain (v0.61.4)
- [x] VDR multi-file support in ttcut-demux — auto-detect, concat protocol, `-n` parameter
- [x] VDR demux example script (`tools/vdr-demux-example.sh`)
- [x] Replace transcode CLI with libavcodec API for MPEG-2 encoding (TTTranscodeProvider)
- [x] H.264/H.265 A/V Sync in ttcut-demux: audio trim, padding, duration mismatch, bitrate autodetect, VDR multi-file
- [x] Zeitsprung (Quick Jump) thumbnail browser dialog with interval filter (v0.61.7)
- [x] Stream Point Detection: Landezonen widget with black frame, silence, audio format change, scene change detection via libavfilter; cut pair auto-derivation; .prj persistence (v0.62.0)
- [x] Dirty-tracking for unsaved project changes (v0.62.1)
- [x] Decode error detection for H.264/H.265 streams — ttcut-pts-analyze with mmap, multi-threaded decode testing (v0.63.0)
- [x] Security audit: all 25 findings fixed (v0.63.0)
- [x] German translations (de_DE): all 165 strings, Q_OBJECT standardization (v0.62.0)
- [x] Screenshot automation: `--screenshots` CLI mode with test media generation
- [x] MPEG-2 extra-frame correction for A/V sync and quality-check (v0.63.0)
- [x] Remove redundant F-buttons from navigation widget, add frame-type labels (I, P/I, B/P/I)
- [x] Remove redundant "Set Cut-Out" from cut list context menu, reorder entries
- [x] Logo detection: markad PGM import and manual ROI selection with Sobel edge profiling
- [x] Logo profile persistence in project file (.ttcut)
- [x] Project file extension change: .prj → .ttcut (with backward compatibility)
- [x] Pillarbox detection: 4:3 in 16:9 with 10s hysteresis (all codecs, I-frame analysis)
- [x] Progress dialog for Landezonen analysis
- [x] Per-track audio delay (±9999ms QSpinBox, applied in keepList for all codecs, persisted in .ttcut) (v0.66.0)
- [x] Cut list "Audio-Drift" column showing accumulated boundary drift per cut after preview (v0.66.0)
- [x] TTESInfo: parse per-track audio_N_trimmed_ms and first_pts from .info (v0.66.0)
- [x] Fix audio list UI not refreshed after locale-based sorting (v0.66.0)
- [x] Per-project settings persistence in .ttcut (output path, muxing, encoder with codec-specific mapping) (v0.66.0)
- [x] Audio language preference list (replaces hardcoded system-locale sort, accepts 2/3-letter codes with alias normalization) (v0.66.0)
- [x] Replace deprecated qSort() with std::sort() in TTSubtitleHeaderList
- [x] Suffix-Checkbox im Cut-Dialog reagiert live auf Toggle (updateOutputFilename slot)
- [x] Remove inactive UI elements: Chapters tabs (spumux-legacy), Configure Muxer button, hidden videoFileInfo widget
- [x] Settings-Dialog Sidebar (7 Kategorien: Navigation, Suche & Preview, Audio & Sprache, Encoder, Multiplexen, Pfade, Logging) — Allgemein-Tab und Files-Tab aufgeteilt (v0.70.0)
- [x] Cut-Dialog 2-Tab Reorg (Schnitt + Encoder), Container-Wahl in gbOutput (v0.70.0)
- [x] Persistent/transient Trennung für Encoder + Mux/Audio: Cut-Dialog überschreibt App-Defaults nicht mehr; working* Variants für 7 Mux/Audio-Settings; .ttcut serialisiert working set (v0.70.0)
- [x] 'Reset to defaults' Buttons in 6 Settings-Tabs + Cut-Dialog (v0.70.0)

## Known Limitations

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
