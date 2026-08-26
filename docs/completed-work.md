# Erledigte Arbeit

Abgeschlossene Untersuchungen, Fixes und Funktionen — mit den Belegen,
auf denen der Abschluss beruht: Messwerte, Commits, widerlegte Hypothesen,
Namen der Prüf-Harnesses.

Hier steht **nicht**:

- offene Punkte und bekannte Einschränkungen → `TODO.md`
- die Anwendersicht auf eine Version → `CHANGELOG.md`
- wie ein Teilsystem heute funktioniert → `docs/code-map/`

Einträge werden hier nicht mehr gepflegt. Widerspricht ein späterer Befund
einem Eintrag, gehört der Befund in die betroffene Karte unter
`docs/code-map/` und der offene Rest zurück in `TODO.md`.

## Untersuchungen und Fixes

### Smart Cut (H.264 / H.265)

- **H.264 gemischt MBAFF+PAFF (08x04-Korpus) — Befunde B, D, E** → **GELÖST
  (2026-07-19)**. Wurzel war der TS↔ES-AU-Nummerierungs-Drift der
  `es_extra_frames`, Spec
  `docs/superpowers/specs/2026-07-19-es-extras-field-awareness-design.md`.
  Verschoben aus `TODO.md` beim v0.82.2-Abgleich; offen blieb dort nur der
  PAFF-**Playback**-Fehler unter mpv.
  - **Befund B — Decode-Hänger** beim Navigieren auf ein PAFF-Feldpaar-AU
    (`46d3dcb`): Index-Adopter erben jetzt den PAFF-Zustand des Owners
    (`adoptStreamMetadata`); Diag `test_adopt_paff`. Die Crash-Variante
    (SIGABRT in `avcodec_send_packet`) ist als Folge des beseitigten
    EOF-Drains plausibel, aber nicht formal bewiesen (GUI-Soak ohne Crash
    bestanden). Der Core-Dump `core.456277` wurde am 2026-07-31 gelöscht; ein
    Backtrace wäre ohnehin unbrauchbar, weil das Binary seit dem 19.07.
    vielfach neu gebaut ist. Nachprüfbar nur über einen neuen Repro-Lauf auf
    dem 08x04-Korpus.
  - **Befund E — Smart-Cut-Re-Encode liefert uniform graue Frames**
    (`8dfda6d`): der SPS-Unification-Rewriter schrieb/las die
    CABAC-Alignment-Bits unbedingt; endete der umgeschriebene Slice-Header
    exakt byte-aligniert (08x04: 42+6 = 48 Bits am ersten IDR), schob ein
    falsches 0xFF-Byte die Payload weg → Slice still verworfen, Frame grau
    concealed, bf=0-P-Frames trugen das Grau bis zum Copy-IDR. Alignment
    jetzt spec-bedingt (H.264 7.3.4) auf Lese- und Schreibseite; Details in
    `docs/code-map/smart-cut.md`, Diag `tools/diag/test_feed_decode`.
  - **Befund D — H.264/H.265-Standbild-Aspect fehlt**: `showVideoFrame()`
    korrigiert im FFmpeg-Zweig jeden SAR≠1:1 in Upscale-Richtung
    (Breite×SAR, z.B. 720×576 SAR 16:11 → 1047×576), Quelle
    `TTFFmpegWrapper::sampleAspectRatio()` (Codec-Kontext,
    codecpar-Fallback). MPEG-2-Pfad unverändert. Spec
    `docs/superpowers/specs/2026-07-19-h26x-still-aspect-design.md`, Diag
    `tools/diag/test_sar`.

- **`av_packet_alloc`-Fehlschlag schnitt das Segment still ab** → **GELÖST
  (2026-08-15)**. Zwei Stellen (`decodeFramesIntoList`, `runEncodePass`)
  antworteten mit `break` und die Funktion meldete Erfolg; jetzt
  `setError` + `return false` wie jeder andere Fehler derselben Schleifen
  (`flushEncoder` tat es schon richtig). Nur bei gescheiterter
  Speicheranforderung erreichbar — deshalb ohne Repro, als Angleichung.
  Regression: `test_h26xcut_abort` (none), `test_smartcut_abort` (Achtung:
  braucht sein festes Ausgabeverzeichnis `CLAUDE_TMP/TTCut-ng/cut-abort/` [Material verloren 2026-08-16];
  nach einem Temp-Aufräumen schlägt er auch auf unverändertem Stand fehl).

- **H.265 Smart Cut: RASL-Verlust an der Non-IDR/CRA-Naht (Defekt A,
  H.265-Teil)** → **DONE (2026-07-21, branch `feature/hevc-seam-rasl`)**
  - War: Der EOS an der Naht verwarf die RASL-Bilder des Copy-Start-CRA still
    (`NoRaslOutputFlag=1`, exakt das RASL-Fenster, 0 Fehler). Mux-Zeitachse
    inhaltstreu → Symptom = ~200-ms-Standbild pro Naht, kein Sync-Drift.
    **User-Entscheid 2026-07-20: nicht akzeptabel.**
  - Fix: RASL-erhaltende Naht (`planHevcSeamFix` + neues Modul
    `extern/tthevcseam.{h,cpp}`) — Quell-Parametersätze ab Segmentbeginn,
    kein EOB an der Naht, Encoder-PPS auf freier `pps_id`, Slice-Rewrite
    (IDR→CRA-Demotion, POC-Ankerung, RPS-Retain-Erweiterung). Encoder-SPS
    wird gemessen statt angenommen; jede Preflight-Absage fällt auf die
    bisherige Naht zurück und meldet das im Schnitt-Fortschritt.
  - Belege: synthetisch 237/237 Frames (vorher 233), Designermode 4K und
    Astra HLG je 301/301 (vorher 294), 0 Dekoderfehler, Kopie ab Copy-CRA
    bit-identisch, RASL-SSIM 0,997–0,9997; SES-IDR-Material und alle
    H.264-Gates unverändert (byte-identisch zu master).

- **H.264 Smart Cut: EOS+Non-IDR-Naht beschädigt Leading-Pics des Copy-Start-Keyframes**
  → **DONE (2026-07-20, branch `feature/defect-a-seam-unification`)**
  (Defekt A, BESTÄTIGT 2026-07-16; H.264-Teil GEFIXT 2026-07-20)
  - **Fix:** Non-IDR-Copy-Starts mit Leading-Pics nehmen jetzt die
    Unification-Branch (`seamNeedsUnification`-Trigger, Probe
    `kfHasLeadingPics`); IDR- und Leading-Pic-freie Nähte bleiben auf dem
    byte-identischen Standard-Pfad. Zwei dabei freigelegte, vorbestehende
    Unification-Defekte mitgefixt: RPLM-Kurzzeit-Diffs werden aus der
    modularen Encoder-PicNum-Domäne (MaxPicNum 16, Voll-Zyklus-Padding!)
    in die lineare Quell-Nummerierung übersetzt; MMCO-Neutralisierung nur
    noch bei PAFF. Gates: Synthetik 160/260/349, MBAFF 500, Petro 900
    (96 harte Fehler → 0) + 1500 alle PASS; PAFF/IDR byte-identisch;
    MKV-PTS monoton. Qualitäts-Gate `tools/diag/gate_h264_seam.sh`.
    Rest-Artefakt: 1 gutartige `mmco: unref short failure` pro Naht auf
    Material mit periodischen MMCOs (Ketten-Neuaufsatz, pixel-neutral).
  - Nach dem EOS am Re-Encode→Stream-Copy-Übergang ist der DPB leer; die
    Leading-B-Frames des Non-IDR-Copy-Start-Keyframes referenzieren Vor-Naht-Bilder.
    Die frame_num-Brücke lässt diese Referenzen **still auf falsche Bilder** auflösen
    (bf=1: 1 Frame verloren + korruptes Duplikat, 0 Decoder-Fehler; real ONE-HD
    720p50: 3 korrupte Frames = Reorder-Tiefe, `mmco: unref short failure`).
  - Reichweite: ARD/ONE progressive HD ist durchgehend non-IDR → jeder
    frame-genaue Cut-In über die Standard-Branch betroffen (~60–120 ms Glitch).
    Auch die (gefixte) Unification-Branch zeigt an der Naht dasselbe
    Rest-Fenster (34 Frames Drift, SSIM ≥0,84, keine Artefakte).
  - Alte Schutz-Annahme widerlegt: Die Boundary-Crossing-Extension feuert bei
    Mitten-GOP-Cuts NIE (nur wenn Leading-Pics vor dem Cut-In anzeigen) —
    `has_b_frames≥2` schützt nicht.
  - **H.265-Teilfrage GEMESSEN (2026-07-17): ja, Frame-VERLUST statt Korruption.**
    Synthetik x265 open-gop bf=4 (CRA-Keyframes), Cut 160..400: 237 statt 241
    Frames, 0 Decoder-Fehler — die 4 RASL-Pictures des Copy-Start-CRA
    (Display 196–199) werden nach dem EOS still verworfen (`NoRaslOutputFlag=1`).
    ~~Folge-Verdacht (noch zu messen): fortlaufende Mux-Timestamps ⇒ ~160 ms
    A/V-Verschiebung ab der Naht bei 25fps.~~ → widerlegt, s. Mess-Session.
  - **Mess-Session 2026-07-20 (Design-Entscheid steht noch aus):**
    - **Extension-Hypothese als Fix-Richtung WIDERLEGT.** Jeder Mitten-GOP-Cut
      sitzt schon heute an einem "nächsten Keyframe"; die verschobene Naht ist
      strukturgleich. Auf IDR-freiem bf=3-Material (Keyframes alle 50) zeigen
      zwei Standard-Branch-Nähte (KF 199 mit 1 Leading-B, KF 297 mit 3)
      dieselbe Schadensklasse: **der KF wird N Display-Slots zu früh
      ausgegeben, die N Leading-Bs kommen danach und sind still korrupt**
      (Hash matcht kein Quellbild; 0–1 Decoder-Fehler; Rest ab KF
      bit-identisch). Rekursion unmöglich: auf IDR-freiem Material hat jeder
      Ziel-KF wieder Leading-Pics.
    - **Schwere = Branch-Lotterie** (POC des Copy-Start-KF entscheidet
      pocBridgeable): dieselbe Naht-Klasse über die Unification-Branch
      (KF 248, 2 Leading-Bs) liefert korrekte Reihenfolge + **richtigen Inhalt
      in Re-Encode-Qualität** (SSIM 0,973/0,981; Nachbar-Baseline 0,92;
      Kreuz-SSIM 0,906), 0 Fehler, ab KF bit-identisch. Die
      MMCO-Neutralisierung + POC-Domain-Kontinuität der Unification-Naht
      erreichen das "Standins statt Korruption"-Ziel MIT EOS.
      ⇒ Evidenz-gestützte Fix-Richtung H.264: der Standard-Branch-Naht die
      Unification-Naht-Behandlung geben (statt Extension oder EOS-Verzicht).
    - **H.265: Verlust wandert exakt mit** (Cut 210..450, Naht am nächsten
      CRA 246: wieder genau 4 RASL-Frames weg, 0 Fehler, Rest bit-identisch).
    - **A/V-Shift-Verdacht WIDERLEGT** (echte Mux-Pipeline via neuem Diag-Tool
      `tools/diag/test_mkvmux`, app-getreu inkl. `outputDisplayOrder`):
      RASL-AUs bekommen eigene PTS-Slots, die MKV-Zeitachse bleibt
      inhaltstreu (PTS-Sprung 1,400→1,600 s an der Naht, Gesamtdauer voll
      9,64 s). Symptom = **200-ms-Standbild** an der Naht (4 verlorene Slots
      + Frameabstand bei 25fps), **kein kumulativer Sync-Drift**.
    - H.264 im MKV: Container-PTS tragen die korrekten Slots (KF 1,600 s,
      korruptes B 1,560 s; nicht-monoton in Decode-Reihenfolge) — die
      ES-Inversion wird bei PTS-treuer Präsentation maskiert; sichtbares
      Symptom = N korrupte Frames + PTS-Wobble (deckt sich mit realem
      60–120-ms-Glitch).
  - Repro: `tools/diag/test_smartcut_seam`, `tools/diag/test_mkvmux`; Karte
    [docs/code-map/smart-cut.md](docs/code-map/smart-cut.md); Artefakte
    `CLAUDE_TMP/TTCut-ng/eos_nonidr/` [Material verloren 2026-08-16].

- **H.264 Smart Cut: SPS-Unification zerstört progressive Quellen** →
  **FIXED** (2026-07-16, Defekt B) — Slice-Rewriter ließ bei poc_type-2-Encoder
  (progressiv) das von der Quell-SPS verlangte `pic_order_cnt_lsb` weg → alle
  Header ab dort bit-verschoben (real: 495 Decoder-Fehler, 13/1001 Frames weg).
  Feld wird jetzt eingefügt (verankerte POC-Nummerierung). MBAFF-Unification und
  Standard-Branch byte-identisch. Details CHANGELOG (Unreleased) + Karte.

- **Smart Cut Quality Test Suite** → **DONE** (`tools/ttcut-quality-check.py` + `verify-smartcut` skill)

- **HEVC CRA-only Stream: Smart Cut Verifikation** → **DONE** (v0.72.0)
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

- **Smart Cut Performance: mmap statt QFile für Stream-Copy** → **IMPLEMENTIERT** (2026-03-28, commits d80b918 + 2f3bb69)
  - `accessUnitPtr()` für Zero-Copy mmap Frame-Zugriff, Bulk-Write für ungepatche Segmente
  - Funktionale Verifikation de-facto erledigt: nachfolgende Smart-Cut-Refactors (reencodeFrames-Split
    9f31ede, buildFrameIndex-Split 38bb6ea) wurden bit-identisch via `ffprobe show_packets` verifiziert —
    der mmap-Pfad ist dabei mit abgedeckt. Offen bleibt nur eine optionale dedizierte Performance-Messung.

- **Smart-Cut Code-Map Findings prüfen** → **ALLE 4 PUNKTE ERLEDIGT** (2026-07-11/12, Details unten)
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
  - ~~**(4) Annahme `kExpectedEncoderLog2PocLsb = 4`**~~ → **ERLEDIGT 2026-07-12**
    (`4ed3a4e`..`b0e1335`): `probeEncoderPocParams()` misst die echte Encoder-SPS
    vorab (Wegwerf-libx264 mit GLOBAL_HEADER, SPS aus extradata); Konstante nur
    noch Fallback; Post-hoc-Check vergleicht Sonde↔Real (log2 UND poc_type).
    - Norm-Befund: die gefürchtete Richtung war unmöglich — H.264 7.4.2.1.1
      erlaubt minimal log2=4, die Annahme war also nie zu klein, nur ggf.
      konservativ.
    - **Neubefund:** libx264 nutzt bei progressivem bf=0 **poc_type=2** (0 nur
      interlaced). Stand seit der v0.72-Ära in der Debug-Zeile („Encoder SPS:
      … poc_type=2"), wurde aber nie gegen das poc_type-0-Denkmodell gehalten;
      der warnende Check war auf poc_type==0 gegated. Naht dort über
      EOS+frame_num (bewiesen), Routing bewusst unverändert; Meldung ist info,
      nicht warning. Byte-identisch verifiziert auf MBAFF/IDR-Naht/progressiv/
      HEVC.

### MPEG-2-Schnitt

- **Gemeinsame Temp-Namen in `encodePart()` + Nullzeiger in
  `firstSequenceHeader()`** → **GELÖST (2026-08-12, master `f5f51d1f`; der
  Eintrag stand danach noch als „erledigt" im TODO und wandert erst beim
  Abgleich am 2026-08-15 hierher)**
  - `TTMpeg2VideoStream::encodePart()` legte `encode.avi`/`encode.m2v` direkt
    unter `TTSettings::tempDirPath()` an und räumte am Ende jede `encode.*`
    dort weg — auch die einer gleichzeitig laufenden Instanz. Vier parallele
    `test_mpeg2cut_abort`-Läufe scheiterten 3–5 von 12 Malen mit
    Speicherabzug; einzeln liefen rund 60 Durchgänge fehlerfrei
    (→ `reference_shared_tempdir_parallel_runs` im Memory: vor jeder Deutung
    sporadischer Harness-Fehlschläge lesen). Fix: `QTemporaryDir` je Vorgang.
  - Der Absturz selbst kam aus `TTVideoHeaderList::firstSequenceHeader()`:
    `at(index)`-Zugriff vor der Bereichsprüfung; nach dem NULL-Fix wurde
    daraus ein Nullzeiger-Zugriff in `encodePart()` (8 von 12 Läufen,
    rc=139) — beides behoben, `getSequenceHeader()` wird geprüft.
  - **Richtigstellung**: Der Absturz passiert *nicht* nach dem PASS, sondern
    trifft den Kontrolllauf („restart after cancel") mitten im Video-Schnitt
    (rc=134). „Meldet PASS, stirbt beim Beenden" war eine Deutung ohne Beleg.
  - Gates: `tools/diag/gate_encode_tempdir.sh` (vorher `failed=8 dumps=5`,
    nachher `failed=0 dumps=0`), `tools/diag/test_seqheader_missing`
    (vorher rc=134, nachher NULL). Die Schwester-Baustellen (Vorschau- und
    Wiedergabe-Temp-Namen) stehen weiter offen in `TODO.md`.

- **MPEG-2-Vorschau meldete Fehlschläge als Erfolg, MPEG-2-Schnitt bemerkte
  Schreibfehler gar nicht** → **FIXED** (2026-08-13)
  - Ausgangspunkt war ein Harnisch-Fehlschlag, nicht ein Anwenderbericht:
    `test_previewcut_abort` scheiterte mit MPEG-2-Material in zwei von vier
    Fällen. Der TODO-Eintrag dazu nannte zwei mögliche Lesarten und hielt sie
    für Alternativen — gemessen waren **beide zugleich** wahr, und der
    Harnisch-Mangel verdeckte die Produktionsdefekte.
  - **Harnisch (die Maskierung):** `cutList.append(avItem, 1000, 4000)` war
    hart kodiert und passte nur zum 6000-Frame-H.264-Korpus. Auf dem
    3000-Frame-MPEG-2-Korpus existiert Frame 4000 nicht;
    `createPreviewCutList()` → `findIDRBefore(3500)` warf
    `TTIndexOutOfRangeException` (gdb `catch throw`). Weil diese Ausnahme
    zufällig genau die Signatur eines Abbruchs erzeugte (`cancel=1 exit=0
    finished=0`), **bestanden die Fälle `video` und `audio` aus dem falschen
    Grund** — zwei weitere Leertests der Art, die das Abbruch-Vorhaben schon
    sechsmal gefunden hatte. Grenzen jetzt aus `frameCount()` abgeleitet und
    per Argument überschreibbar, wie `test_mpeg2cut_abort` es längst tat; bei
    6000 Frames ergibt die Ableitung rechnerisch die alten Werte, die
    H.264-Messbasis bleibt also vergleichbar. Dieselbe harte Kodierung steckte
    in `test_cut_outcome` (`3000, 3999`) und wurde mitbehoben.
  - **`TTThreadTask::run()` verschluckte jeden Fehler** (`common/ttthreadtask.cpp`):
    der `catch(const TTException&)` band die Ausnahme nicht einmal, protokollierte
    also nichts — eine Ursache wie „Index 3500 exceeds list bounds: 3000" erschien
    nirgends —, und anders als der `TTAbortException`-Zweig darüber warf er bei
    `mIsSynchron` nicht weiter. Der `catch`, mit dem `TTCutPreviewTask` seinen
    genesteten Schnitt seit jeher umgibt, konnte deshalb **nie** feuern. Gemessene
    Folge: kein Vorschauclip wurde geschrieben, drei Fehler standen im Protokoll,
    und die Vorschau meldete „fertig".
  - **`TTFileBuffer::directWrite()` prüfte nichts** und versprach im Kommentar
    „-1 if an error occured" — bei Rückgabetyp `quint64`, wo `-1` als
    18446744073709551615 ankommt; eine Prüfung auf `< 0` hätte nie greifen
    können, und kein Aufrufer prüfte überhaupt. Ein vollständiger MPEG-2-Schnitt
    in ein nicht beschreibbares Verzeichnis erzeugte **102** Qt-Warnungen
    („device not open"), und der Task meldete `finished`. Direkt danach stürzte
    `TTAVData::onCutFinished()` über `mpCutList->at(0)` auf leerer Liste ab
    (`ASSERT`, SIGABRT, rc=134; im Paket ohne `Q_ASSERT` wäre derselbe Zugriff
    undefiniertes Verhalten). Mit der Prüfung in `directWrite` verschwindet
    **beides**: der Schnitt endet beim ersten fehlgeschlagenen Schreibvorgang
    mit „Could not write 42319 bytes to …: Permission denied", der Absturz
    tritt nicht mehr auf. `TTAVStream::copySegment()` hält seinen Puffer jetzt
    per `unique_ptr` — der neue Ausnahmeweg hätte ihn geleckt, der bestehende
    Abbruchweg tat es bereits.
  - **`onCutAborted()` meldete jeden Fehler als „Cut cancelled".** Der Code
    kannte das Problem (der Kommentar über dem Aufräumblock beschreibt es
    wörtlich und nutzt `mSyncPhaseAbort`, um die Dateien eines Fehllaufs *nicht*
    zu löschen), nur die schließende Klammer machte den Unterschied nicht mit.
    `TTThreadTask` merkt sich die Ursache jetzt in `mFailureMessage`, der Pool
    reicht sie über `lastFailureMessage()` weiter, und `onCutAborted()` wählt
    danach zwischen `CutOutcome::Cancelled` und `CutOutcome::Failed`.
    **Abweichung vom TODO-Vorschlag:** dort stand „ein zweites Signal
    (`failed(task, grund)`)" — verworfen, weil ein neues Signal alle 15
    Verbraucher von `TTThreadTask::aborted` berührt hätte (elf in
    `ttcutmainwindow.cpp`, vier im Pool), während die Fehlermeldung am Task
    genau dem Muster folgt, das `TTCutPreviewTask::errorMessage()` schon
    verwendet. Gleiches Ergebnis, kleinere Reichweite.
  - **MPEG-2-Vorschau-Abbruch** (bis dahin als eigener offener Punkt geführt,
    mit ausdrücklicher Warnung vor einer stückweisen Lösung): kein neues
    Abbruchverfahren nötig, sondern eine Weiterleitungslücke.
    `TTThreadTaskPool::onUserAbortRequest()` sendet nur an `mTaskQueue`, und
    `startNested()` trägt seinen Task dort nicht ein — beim Endschnitt liegt
    `TTCutVideoTask` per `start()` in der Queue und bekommt den Abbruch, in der
    Vorschau läuft derselbe Task genestet und bekam ihn nie. Die Prüfungen
    selbst gab es längst. `TTCutPreviewTask::onUserAbort()` reicht jetzt weiter
    (ohne Mutex, anders als bei `mpActiveSmartCut`: `cutVideoTask` lebt vom
    Konstruktor bis zum Destruktor), die Audiophase bekommt dasselbe
    Abbruch-Prädikat wie der H.264-Zweig, der `ok`-Rückgabewert von
    `cutAudioTracks` wird nicht mehr verworfen, und beide Ausgänge räumen die
    Dateien des Clips ab. Messung vorher/nachher: Abbruch bei 36 ms, Clip 1 lief
    trotzdem 2,4 s zu Ende und ließ drei Dateien liegen → Abbruch bei 268 ms,
    Lauf endet nach 1107 ms **innerhalb** Clip 1, Temp-Verzeichnis leer.
  - **Auch die Harnesse prüften material-spezifisch statt sachlich:** die
    Dialogprüfung verlangte den H.26x-Wortlaut „too damaged" (für ein fehlendes
    Verzeichnis eine falsche Diagnose — die MPEG-2-Meldung nennt stattdessen die
    Ursache), und `test_cut_outcome` prüfte auf „Cannot create output file"
    statt auf den fehlgeschlagenen **Pfad**, den seine eigene Begründung nennt.
    Beide prüfen jetzt die Sache, nicht die Formulierung einer Engine.
  - Prüfstand danach, beide Korpora: `test_previewcut_abort` 4/4 (MPEG-2 und
    H.264; `audio` mit ausgewiesenem INCONCLUSIVE, weil die Audiophase schneller
    ist als der 2-ms-Takt des Harnisch), `test_cut_outcome` 4/4 (beide),
    `test_mpeg2cut_abort` 4/4, `test_h26xcut_abort` 4/4,
    `test_audioonlycut_abort` 2/2. Spec:
    `docs/superpowers/specs/2026-08-13-preview-abort-mpeg2-design.md`.

- **MPEG-2: Cut-Out auf B-Frame verliert bis zu M−1 Frames** → **FIXED** (2026-07-12, `3b087ae`)
  - Der Block in `getCutEndObject()` (Display-Index + Bitstream-B-Zählung vermischt,
    unterdrückte den Tail-`encodePart()`) wurde ersatzlos entfernt. Die
    „Duplikat"-Begründung seines Kommentars war widerlegt: strukturell (Trailing-Bs
    zeigen VOR dem I/P an, Nachkodier-Bereich disjunkt zur Kopie) und per
    Laufzeit-A/B (8 Cut-Out-Lagen TEST.m2v inkl. Open-GOP-Grenze: keine Doppler,
    I/P-Fälle bit-identisch mit/ohne Block).
  - Regression: TEST.m2v-Matrix exakt (I/P bit-identisch zur Baseline, SSIM-Diagonale
    ≥0,97); Futurama M=4 alle B-Lagen exakt (Worst Case verlor 3 = M−1); GUI-Cut
    via `--auto-cut` byte-identisch zur Engine. Spec/Protokolle:
    `docs/superpowers/specs/2026-07-12-mpeg2-cutout-bframe-fix-design.md` (lokal),
    `CLAUDE_TMP/TTCut-ng/dupcase/REGRESSION-*.md` [Material verloren 2026-08-16].
  - Map: [docs/code-map/mpeg2-cut.md](docs/code-map/mpeg2-cut.md)

- **MPEG-2-Re-Encoder: Einzelbild-Encode kann beschädigten letzten Slice liefern**
  → **FIXED** (2026-07-13, `add2ac8`) — und die geerbte Verdachtsrichtung war
  doppelt falsch: der Encoder war unschuldig, und die frühere „Entlastung" des
  Cut-/Kopier-Pfads (dupcase/DIAGNOSE-neu10.md) ein Fehlschluss, weil auch der
  Standalone-Repro durch `cut()` lief.
  - Echte Root Cause: `TTFileBuffer::readByte()` warf am EOF nie, sondern
    lieferte stale Ringpuffer-Bytes hinter `writePos`. Der Header-Parser las
    nach dem stillen EOF-Return des Startcode-Scanners so ein Phantom-Typ-Byte
    (zufällig 0x00 = picture_start_code) → Phantom-Picture-Header 3 Bytes vor
    Dateiende → `getByteCount()` beendete die Kopie am Phantom → die letzten
    3 Entropie-Bytes des letzten Slice fehlten (`ac-tex damaged` am letzten
    Makroblock). „Inhaltsabhängig" war nur, ob das stale Byte 0x00 war.
  - Fix: `readByte()` wirft `StreamEOF` (der Vertrag, den alle Aufrufer schon
    implementierten); `nextStartCodeTS()` stoppt bei < 4 gültigen Restbytes.
  - Verifiziert: TEST.m2v-Matrix (Einzelbilder 4/10/22, Paare 9-10/10-11,
    Bereiche 0..3–0..10) exakte Framezahlen + 0 Decoder-Warnungen; Repro 10..10
    schreibt jetzt das volle Encoder-Paket. Isolationstool
    `tools/diag/test_startcode_scan` (Scanner allein: 41 echte Startcodes,
    sauberes EOF, kein Phantom).

- **MPEG-2: Cut der letzten Frames der Datei → Segfault**
  → **FIXED** (2026-07-13, `2dd104c`)
  - Kein Memory-Bug: uncaught `TTInvalidOperationException`. Cut-In auf ein
    Nicht-I-Frame ohne folgendes I-Frame (Aufnahme mitten in der GOP
    abgeschnitten) → `moveToNextIndexPos` liefert -1 → `encodeEnd = -2` →
    `encodePart(cutIn, -2)` schlägt fehl und wirft → `std::terminate`.
  - Fix in `getCutStartObject()`: `iFramePos < 0` wie „nächstes I jenseits
    cutOut" behandeln — ganzen Bereich re-encodieren, letzten Header
    zurückgeben (Transfer wird übersprungen).
  - Verifiziert: TEST.m2v 73470..73474 (vorher Crash) = 5 Frames, 0 Warnungen;
    Regressionsmatrix unverändert; weitere EOF-Randfälle exakt.

- **MPEG-2 field-picture: Cut-Positionen zählen Felder statt Frames** → **HARMLOS, GESCHLOSSEN** (2026-07-16)
  - `createIndexList()` legt pro `picture_start_code` einen `TTVideoIndex` an, also
    **zwei Einträge pro Field-Picture-Frame** (gleicher `temporal_reference` ⇒ gleicher
    `display_order`). `mExtraIndices` markiert den Zweiteintrag, **entfernt ihn aber nicht**.
    Gelesen wird `extraIndices()` nur von der Audio-Schnittkorrektur (`data/ttavdata.cpp`)
    und der Standbildanzeige (`gui/ttcurrentframe.cpp`) — **nicht** vom Video-Cut.
  - **Repro-Ergebnis (echtes Material, Futurama 02x01, beide Demux-Wege):** kein
    sichtbarer Cut-Defekt. Prüfmatrix mit `tools/diag/dump_mpeg2_fields` +
    `tools/diag/test_mpeg2_cutout`:
    - **Erreichbarkeit (korrigiert 2026-07-18):** Der reale Workflow ist ttcut-demux
      für ALLE Codecs (`-c copy`) — dessen ES **enthält die Feldpaare** (222,
      85 721/85 499). Die frühere „unerreichbar via ProjectX"-Begründung war
      hinfällig (ProjectX ist nicht Teil des Workflows; sein Feld-Stripping wurde
      nur in meinem Testlauf beobachtet). Das Harmlos-Urteil stützt sich allein
      auf die Messungen am ffmpeg-Copy-ES — dem realen Fall (nächster Punkt).
    - **Kein Frame-Verlust:** Cut über die Field-Region (39 Pictures → 37 Frames, alle
      da); Cut-Out **auf** einem Extra-Feld bzw. „halbem Paar" ebenfalls sauber.
    - **0 Decoder-Fehler** über alle Cuts; dekodierter Inhalt **pixel-identisch** zur
      Quelle (md5 über 9 Frames inkl. beider Field-Frames).
    - **Audio:** `countExtraFramesBefore()` zieht die Extra-Felder ab
      (`time=(frame−extras_before)/fps`) — im realen Workflow aktiv.
  - Einziger realer Effekt: kosmetisch — Frame-Zähler
    um bis zu 222 erhöht, zwei Navigations-Stops pro echtem Frame in der Field-Region.
    Kein Korrektheitsfehler → **kein Fix**.
  - **Ordnungs-Vermischung: gemessen folgenlos** (2026-07-31, Harness
    `tools/diag/test_extra_index_rank`, Protokoll
    `CLAUDE_TMP/TTCut-ng/fieldrank/MESSUNG.md` [Material verloren 2026-08-16]). `mExtraIndices` speichert
    Bitstrom-Ordnung (`current_pic_num`, `ttmpeg2videostream.cpp:163`), wird nach
    `sortDisplayOrder()` (`ttopenvideotask.cpp:144`) aber gegen Anzeigepositionen
    gelesen. Die früher hier vermutete „±1–2 Frame Ungenauigkeit der
    Audio-Korrektur" ist auf echtem Material **nicht reproduzierbar**:
    - Der **Einzelversatz ist real**: 807 von 904 gespeicherten Positionen liegen
      nach der Sortierung woanders (Bereich −3…+6, Mittel ~2,5). Vier Aufnahmen,
      Comedy Central SD 576i (Futurama 150 Paare, Benders 324, AV-async 315,
      multifile 115).
    - Die **Zählung stimmt trotzdem exakt**: 0 Abweichungen über alle 323 381
      Anzeigepositionen. Grund gemessen, nicht angenommen — beide Felder eines
      Paares landen immer auf zwei benachbarten Anzeigeplätzen (0 von 904
      Ausnahmen), und die Menge der so belegten Doppelplätze ist elementweise
      identisch mit der Menge der gespeicherten Bitstrom-Positionen. Die
      Sortierung tauscht nur, welches Paar welchen Platz belegt; `countBefore()`
      sieht davon nichts.
    - Betroffen wären nur Verbraucher, die eine **einzelne** Position aus der
      Liste nehmen statt zu zählen — heute keiner: `countExtraFramesBefore()` und
      die beiden Stellen in `ttcurrentframe.cpp` zählen, und `clusterConfirmed`
      (`ttavdata.cpp:580`, ±4-Fenster, das +6 überschreiten würde) vergleicht
      gegen `.info`-Roh-AU-Nummern, also beide Seiten in Bitstrom-Ordnung.
    - Messung, kein Beweis: die Platzerhaltung ist eine beobachtete Eigenschaft
      der MPEG-2-Umordnung auf diesem Material. Ein neuer Verbraucher, der
      Einzelwerte benutzt, braucht vorher wieder eine Messung.
    - Nebenbefund: die frühere Abmoderation „irrelevant, da im Standard-Workflow
      leer" war falsch — die Listen sind nicht leer (115–324 Paare je Aufnahme).

- **TTCut-ng Cut-Pipeline A/V Drift bei MPEG-2 mit field-picture-encoding** → **RESOLVED** (2026-05-13, branch `feature/mpeg2-field-picture-fix`)
  - Root Cause: Field-Picture-Detection im MPEG-2-Parser (`picture_coding_extension` nicht gelesen, jeder picture_start_code als Frame gezählt, doppelte Zählung bei field-picture-encoded Frames). Fix in `avstream/ttmpeg2videostream.cpp` + Pipeline-Wiring in `data/ttavdata.cpp`. Spec: `docs/superpowers/specs/2026-05-12-mpeg2-field-picture-fix-design.md`.
  - Validation: Audio_a_sync.m2v Cut [60s..2400s] zeigt im Verlauf perfekt 0ms drift an mehreren Sample-Points (0/600/1200/1800/2300s). Pre-fix war 11.85s Drift. Der vorher gemeldete 104ms "Rest-Drift" war End-PTS-Asymmetrie-Artefakt (Frame-Duration 24ms+40ms quantisiert End-Diff bis ~64ms) + 2 Frames cutOut-Snap, ohne dass Verlauf-Inhalt asynchron ist.
  - Lessons Learned: A/V-Drift-Diagnose IMMER mit Verlauf-Sample-Points, nicht End-PTS allein. Memory: [feedback_av_drift_diagnosis.md](memory/feedback_av_drift_diagnosis.md)
  - Memory: [project_av_drift_cut_pipeline.md](memory/project_av_drift_cut_pipeline.md)

### ttcut-demux

- **Lückenreparatur auf einen Durchlauf umgebaut** → **DONE (v0.82.1,
  2026-08-23)**. Auslöser: eine Aufnahme mit 6730 Lücken lief ~18 Minuten ohne
  Rückmeldung. Details in den Commits `a7aecee0` (Engine) und `1656df85`
  (Beispielskript); Datenfluss in `docs/code-map/ttcut-demux.md`.

  **Gate — so wird es nachgebaut.** Die Testmedien werden erzeugt, nicht
  aufbewahrt; die vier Befehle sind der eigentliche Beleg:

  ```bash
  # Kontrolldateien mit berechenbarer Sollgröße
  ffmpeg -f lavfi -i "sine=f=440:r=48000:d=1800,volume=0.5" -ac 2 -c:a mp2 -b:a 256k ctrl.mp2
  ffmpeg -f lavfi -i "sine=f=440:r=48000:d=1500,volume=0.5" -ac 2 -c:a ac3 -b:a 192k ctrl.ac3
  # echtes AC3 mit Bitratenwechsel (67 % 192k / 33 % 384k, deckt den
  # format=duration-Fehler auf): aus dem Korpus, Tux_Video/tux_video_deu.ac3

  # Lückenliste: 400 Zeilen mit allen drei Zeilentypen
  awk 'BEGIN{for(i=0;i<400;i++){s=20+i*4.0; e=s+0.2; sms=200;
        if(i%7==0)sms=0; if(i%11==0)sms=-150;
        printf "%.6f %.6f %.0f %.0f\n",s,e,200,sms}}' > gaps.txt

  # Splice-Segmente mit gebauter 7,800-s-Lücke (belegt den Komma-Fix)
  ffmpeg -i <quelle.ts> -t 10 -c copy -output_ts_offset 1000 -muxdelay 0 -muxpreload 0 segA.ts
  ffmpeg -i <quelle.ts> -t 10 -c copy -output_ts_offset 1017 -muxdelay 0 -muxpreload 0 segB.ts
  ```

  Geprüft wird die **echte** Funktion, aus alter und neuer Fassung
  herausgeschnitten (`awk '/^repair_audio_with_silence_inserts\(\) \{/,/^\}/'`
  plus `probe_audio_props`, `audio_es_duration`, `quantize_up_to_frame`,
  `repair_progress`), beide auf derselben Kopie laufen lassen, Kriterium
  **Byte-Identität**. Messwerte: 50,7 s → 11,7 s (400 MP2-Lücken), 32,6 s → 7,0 s
  (300 AC3-Lücken); Splice: alt 0 Zeilen und 2 „ffprobe failed", neu genau
  `1010.045000 1017.845000 7800`.

  Falle beim Nachbauen: eine Probe, die als `t1 && echo ok` aufgerufen wird,
  läuft im Bedingungskontext — dort schaltet bash `set -e` ab, und der Test
  winkt die kaputte Fassung durch.

- **Untertitel-Zeitachse: ES-Rebase war komplett wirkungslos — alle Cues
  um die Länge des untertitelfreien Vorlaufs zu früh** → **GEFIXT
  (2026-08-17, `5b2b0256`, released v0.81.1)**
  - **Symptom** (User-Fund an 03x06): Untertitel „massiv versetzt", TS OK.
    Gemessen: erster Untertitel im Original-TS bei 27117,848 s absolut,
    `first_video_pts` 27079,308 s → korrekte ES-Position **38,540 s**; die
    SRT begann aber bei 1,400 s, die `.mks` bei 0 → konstant **−37,14 s**
    über die volle Länge (letzte Cue alt 01:19:20,485 / neu 01:19:57,625).
  - **Mechanismus**: Der Subs-Only-TS enthält nur den Untertitelstrom.
    ffmpegs Default-Rebase nimmt dessen erstes Paket als Startzeit,
    `avoid_negative_ts` pinnt es fest, der mpegts-`muxdelay` addiert 1,4 s
    — das beabsichtigte `-output_ts_offset -ORIG_VIDEO_PTS` war damit
    vollständig neutralisiert: **jede** Aufnahme meldete „delay 1400 ms"
    (der rauchende Colt in allen sechs Logs vom 2026-08-17). Die
    ccextractor-`-delay`-Ableitung aus dem ersten Paket zementierte den
    Fehler in die SRT. Maskiert auf Material, dessen Untertitel nahe am
    Videostart beginnen (GUI-Abnahme 2026-08-16: Fehler ≈ 0; die übrigen
    Folgen der Staffel: ~4 s bzw. ~0 — deckt sich exakt mit dem Modell
    „Vorlauf − 1,4 s").
  - **Fix**: `-copyts -muxdelay 0 -avoid_negative_ts disabled` an der
    Subs-Only-Extraktion (Offset wird ehrliche Subtraktion) und `-copyts`
    am `.mks`-Schritt (sonst re-nullt die Matroska-Stufe). Verifiziert per
    Voll-Demux 03x06: „delay 38540 ms", SRT-Cue 1 = 00:00:38,540 mit
    korrektem Text, `.mks`-Pakete identisch, Versatz am Dateiende exakt 0.
  - Details der Flag-Semantik in `docs/code-map/ttcut-demux.md`
    (Untertitel-Export-Zeile).

- **Untertitel-Export als Option — und drei Schichten vorbestehender
  Defekte, die ihn immer leer laufen ließen** (2026-08-16, branch
  `feature/subs-option`)
  - **Option**: `--subs`/`--no-subs` (Default AUS, Long-Option-Shim vor
    getopts); DVB-Bitmap → `.mks`, eingebettetes SubRip → `.srt`;
    `[subtitles]`-Sektion der `.info` bleibt bei „aus" mit `count=0`
    gültig. GUI-Wahl je Lauf im User-Wrapper `VDR_Demux.sh`
    (Skripte-Repo `7dcb314`).
  - **Drei gemessene Altdefekte** (die bisherige Immer-an-Extraktion hat
    für DVB-UT nachweislich nie etwas geliefert):
    1. Der „empty, skipped"-Vorab-Check nutzte `ffprobe -read_intervals`,
       das auf realen DVB-UT-Streams NULL Pakete aufzählt, wo
       `ffmpeg -c copy` 678 KB/10 min extrahiert (Tatort 1994x05) —
       jeder Stream galt als leer. Fix: Byte-Zählung einer
       120-s-ffmpeg-Stream-Copy-Probe (Mitte, dann Dateianfang).
    2. Extraktion las aus der Timestamp-reparierten TS — die mappt nur
       `0:v:0`+`0:a?`, enthält also gar keine UT-Streams. Fix:
       `SUBS_INPUT_ARGS` sichert die Original-Quelle vor der Reparatur.
    3. ffmpegs `.sup`-Muxer akzeptiert nur PGS, kein `dvb_subtitle` —
       selbst erreichte Extraktion schrieb eine leere Datei. Fix:
       `.mks` mit `-f matroska`.
  - **Positiv-Beweis**: Rookie-07x11-Korpus-Demux mit `--subs` →
    `00001_deu.mks` 1,99 MB, `.info` `count=1`; Default-Lauf: keine
    UT-Dateien, „Subtitle export disabled"-Zeile, A/V-ES byte-identisch
    zum `--subs`-Lauf (Slice-Messung); `gate_audiofix.sh` 22/22.
  - **Materiallage gemessen**: 15/21 lokale Aufnahmen mit echten
    DVB-UT-Daten (237–440 KB je 2-min-Fenster); NAS-Stichprobe (26 von
    3296): 1× Das Erste HD mit Daten, Privatsender überwiegend ohne.
  - **Teletext bewusst NICHT umgesetzt** (Negativbefund): Voll-Scan aller
    3296 NAS-Aufnahmen → 43 PMT-Einträge `dvb_teletext`, davon nur 4 mit
    Datenpaketen (VDR schrieb die PMT mit, den PID meist nicht), und
    **keiner** davon enthält UT-Tafeln — ccextractor „No captions" und
    ffmpeg/libzvbi 0 Bytes selbst mit `-txt_page all` auf allen 4
    (ProSieben/Nitro 2013, SIXX 2015, arte HD 2015). Wer Teletext erneut
    erwägt: erst neues Material mit belegten UT-Tafeln beschaffen.
  - **Messfalle für die Nachwelt**: `ffprobe -read_intervals`/-Paket-
    Aufzählung ist auf DVB-Untertitel-Streams blind (0 Pakete bei
    nachweislich vollen Streams, alle Varianten inkl. `-probesize 8M`,
    `-show_packets`) — Datenpräsenz IMMER per
    `ffmpeg -c copy -f mpegts - | wc -c` messen.

- **ttcut-demux reicht beschädigte Tonrahmen durch, statt sie zu ersetzen**
  → **GEFIXT (2026-08-16, branch `feature/ttcut-audiofix`, Commits
  `a27954e7`..`70749afe`)**
  - War (Befund 2026-08-15, ProjectX-Vergleich gemessen): ein 90-min-Schnitt
    über MPG/mplex hatte 85 min Bild und nur 4,7 min Ton. Ursache lag in der
    Quelle: bei Byte-Offset 6 796 224 endete die gültige MP2-Rahmenkette (nach
    11799 Rahmen), es folgten 468 Bytes ohne Rahmenkopf, danach setzte die
    Kette wieder ein — mplex erklärte den Strom für defekt und ließ die
    Tonspur ab dort weg. Zusätzlich gefunden (Task 7): eine zweite, bis dahin
    unentdeckte Bruchstelle am Aufnahmeende (390 Bytes Junk).
  - **Fix**: neues eigenständiges C-Werkzeug `tools/ttcut-audiofix/ttcut-audiofix.c`
    — läuft die ES frame-genau ab (MP2/AC3/E-AC3), entfernt Junk-Bytes
    zwischen gültigen Rahmen, meldet CRC-defekte Rahmen (ohne sie
    anzufassen), Fix-Modus mit Selbstprüfung, Exit 0/1/2 (sauber/behoben/
    Fehler). Integriert in `tools/ttcut-demux/ttcut-demux`: pro Spur nach
    der AC3-Header-Reparatur und der Interlace-FPS-Korrektur, vor der
    Rev-3-Lückenreparatur. Fail-safe auf jedem Zweig: Werkzeug-Fehlschlag
    lässt die Originaldatei unangetastet; fehlt `ttcut-audiofix` im PATH,
    warnt das Skript und fährt fort.
  - **Meldung**: neue `.info`-Felder je Spur `audio_N_corrupt_ranges`
    (geclusterte Video-Frame-Bereiche aus Junk- und CRC-ms-Positionen, per
    korrigierter FPS umgerechnet, `≤2s`-Clustering wie `corrupt_frame_ranges`),
    `audio_N_junk_bytes`/`audio_N_dropped_frames` (Diagnose, bewusst nicht
    geparst). `TTESInfo` parst `audio_N_corrupt_ranges` in
    `TTAudioTrackInfo::corruptRanges` (gehärtet wie der globale
    `corrupt_frame_ranges`-Block). `TTAVData`-Cluster-Pass 3b macht daraus
    Zeitleisten-Marken „Tonstörungen: X–Y (Spur N)"; der Früh-Ausstieg-Wächter
    der Cluster-Dialog-Funktion wurde erweitert, damit ein reiner Ton-Defekt
    (kein Video-Befund) den Dialog trotzdem öffnet.
  - **Belege (Task 10)**: derselbe Realfall (2023-10-19 RTLZWEI-Aufnahme)
    lässt mplex jetzt bis zum Ende scannen („Scanned to end AU 119999", kein
    Abbruch mehr); finales .mpg Video=4799,440s / Audio=4799,976s /
    Diff=0,536s, vollständiger Audio-Decode fehlerfrei. Rohe Junk-Regionen im
    unangetasteten Quellauszug: `0@0ms:178`, `11834@284016ms:468`,
    `227212@5453088ms:390` (Summe 1036); demuxte `audio_0_junk_bytes=858` =
    468 (die im ursprünglichen TODO-Befund dokumentierte Region) + 390 (neu
    entdeckte Region am Aufnahmeende) — die 178-Byte-Startregion fällt in den
    818-ms-Vorspann-Trim (plausibler Mechanismus, so gekennzeichnet, nicht
    einzeln nachgewiesen). `.info` der Realaufnahme:
    `audio_0_corrupt_ranges=7079-7079,136306-136306`; der alte, unveränderte
    Video-Marker `corrupt_frame_ranges=7095-7095` bleibt bestehen (der
    Bildschaden dort war real). Defekt-Slice (40 MB, isoliert getestet):
    `audio_0_corrupt_ranges=1024-1024,2452-2452`, 990 Bytes Junk
    (Slice-Rand-Artefakte eingeschlossen), `dropped_frames=1`.
  - **Korpus-Nebenbefund (Task 7)**: die Comedy-Central-SDTV-Aufnahme
    (`MPEG2_SD576i25_16-9_AV-async_MP2-deu+eng`) trägt echte Defekte — Junk
    auf beiden Tonspuren plus abgeschnittenes Ende —, vom Sanitizer beim
    Korpuslauf gefunden, nicht künstlich erzeugt.
  - **Gate**: `tools/diag/gate_audiofix.sh`, 20 Checks im Default-Lauf
    (~11 s) + 2 weitere mit `--full` (voller Realaufnahme-Demux plus
    Auto-Cut/mplex End-zu-End). ASAN/UBSan über das gesamte Gate sauber,
    0 Reports (Task 6).
  - Offen: Anwender-GUI-Abnahme und Aufnahme in ein v-next-Release.

- **Audio-Padding bricht bei relativem `output_dir` ab** → **DONE
  (2026-07-31, `40087a4c`, v0.77.0)**
  - War: Der concat-Demuxer löst `file`-Einträge relativ zum Verzeichnis der
    Liste auf. `TEMP_CONCAT` liegt in `$OUTDIR` und listete `$OUTDIR/…`-Pfade
    → gesucht wurde `dir/dir/file`. Die Padding-Subshell starb, `set -e` riss
    das Skript am `wait` um: exit 254, keine `.info`, und auf dem Bildschirm
    keine Fehlermeldung (das Padding-Log liegt in `$PAD_LOG_DIR`).
  - Fix: `realpath` auf beide Einträge — dieselbe Absicherung, die die
    VDR-Multi-Datei-Liste am Kopf des Skripts längst hatte, samt Begründung
    im Kommentar. Die Listen von `repair_audio_with_silence_inserts` sind
    **nicht** betroffen (Basename-Einträge, Segmente liegen neben der Liste) —
    beim Fix mitgeprüft.
  - Beleg: 8-s-Tux-TS mit 7 s Ton (damit der Padding-Zweig läuft), relativ
    gegen absolut. Vorher exit 254 / nur `.m2v` + `.ac3`, nachher exit 0.
    Beide Läufe byte-identisch in `.m2v` und `.ac3`; die `.info` unterscheidet
    sich nur in Zeitstempel und Quellpfad. Absolute Aufrufe — der
    VDR_Demux.sh-Workflow — waren nie betroffen.

- **ttcut-demux: Löcher aus TS-Korruption / VDR-Signalverlust werden
  weder erkannt noch repariert** → **DONE** (2026-07-18, branch
  `feature/demux-defect-repair`)
  - Frame-skaliger PTS-Lücken-Scan (Video: DTS-basiert, 2,5× Frame-Dauer —
    PTS ist unter B-Frame-Reorder nicht monoton; Audio: 0,08s = 2,5× größte
    Paketdauer) über ALLE VDR-Segmente + Segmentnähte (vorher nur das erste
    Segment gescannt). Reparatur zeitachsen-treu: Audio-eigene Löcher →
    codec-native, layout-treue Stille (korrektes AC3-acmod statt
    Kanalzahl-Raten); Video-Löcher → passendes Audio-Stück entfernt.
    Assembly per Segment-Stream-Copy (kein Re-Encode der erhaltenen Audio-
    Anteile); überlappende/aneinandergrenzende Reparaturfenster werden vor
    der Assembly koalesziert (sonst ffmpeg-Abbruch „-to smaller than -ss").
  - Meldung: neue .info-Felder `es_missing_frames`/`es_missing_ranges`,
    `corrupt_frame_ranges`, `audio_N_silence_ms`/`audio_N_removed_ms` +
    unabhängiger, von der Lücken-Erkennung entkoppelter Zähl-Check-Warn
    (fängt stille Löcher ohne PTS-Sprung ab). TTCut-ng zeigt die Bereiche
    als geclusterte Error-Landezonen ("Videoverlust: X–Y (T s) — Audio
    angepasst", zusätzlicher "Signalverlust-Ende"-Marker bei >2s,
    "Bildstörungen: X–Y" für erhaltene, aber korrupt geflaggte Frames).
  - Gates (`docs/superpowers/sdd/progress.md`, Task 7): 07x11 (real, milde
    Beschädigung) 0 Fehlschläge, Drift −11ms. 07x12 (real, 5 Segmente,
    ≈7,6min Signalverlust) Drift von −35s auf −23ms, 496/496 Splices
    angewendet, alle 4 Segmentgrenz-Großlücken in `es_missing_ranges`.
    Saubere Aufnahmen byte-identisch (beide Codec-Familien).
  - Karte: `docs/code-map/ttcut-demux.md` ("Defekt-Erkennung und
    -Reparatur — Rev 3").

- **ttcut-demux: `repair_audio_with_silence_inserts` bricht bei
  überlappenden Gap-Fenstern ab (Silence-Insert schlägt fehl →
  Fallback-Padding statt echter Reparatur)** → **FIXED** (2026-07-18,
  commit 9c345ea fand den Defekt, Fix-Commit "ttcut-demux: coalesce
  overlapping gap windows before splice assembly")
  - ffmpeg meldete `-to value smaller than -ss; aborting`. Die Funktion ging
    von einer chronologisch sortierten, ÜBERLAPPUNGSFREIEN Partition der
    CLASSIFIED_FILE-Zeilen aus. Bei dicht beieinanderliegenden Mikro-Lücken
    überlappten `compute_audio_gap_silence_ms`-Zeilen und
    `emit_video_only_truncations`-Zeilen im Quell-PTS-Fenster.
  - **Fix**: Nach dem Intake-Loop werden die (pos,dur)-Einträge zuerst nach
    `pos` sortiert (Quell-Zeilen sind nur nach `src_start` sortiert; bei
    überlappenden Quell-Fenstern kann `pos = src_start - accumulated_collapse`
    dadurch NICHT-monoton werden — real auf Gate-1-Daten gemessen, ~13,7 ms
    Rückwärtssprung) und dann per Sweep zusammengeführt, wenn das nächste
    `pos` INNERHALB oder GENAU AUF dem Fensterende des vorherigen Eintrags
    liegt (`<=`, nicht `<` — zwei lückenlos aneinandergrenzende
    Audio-Gap-Zeilen erzeugen sonst einen Nullbreite-Schnitt, den ffmpeg
    ebenfalls als "-to smaller than -ss" ablehnt). Zusammengeführte
    Einträge nahe Null (< 1 ms) werden verworfen.
  - **Zweiter, dadurch erst sichtbarer Defekt mitgefixt**: Sobald die
    Überlappungs-Crashes weg waren, erreichten 3 von 4 Spuren erstmals den
    Concat-Schritt und scheiterten dort NEU (`Impossible to open
    './.gap_repair_PID/./.gap_repair_PID/segN.ext'`) — die Concat-Liste
    enthielt `work_dir`-relative Pfade, aber ffmpegs Concat-Demuxer löst
    relative Pfade IN der Listendatei relativ zu deren EIGENEM Verzeichnis
    auf (das ist bereits `work_dir`), wodurch der Präfix verdoppelt wurde.
    War vorher immer durch den früheren Überlappungs-Crash maskiert.
    Fix: nur `basename` in die Concat-Liste schreiben.
  - Gates (`/usr/local/src/CLAUDE_TMP/TTCut-ng/demuxrepair/gateruns/` [Material verloren 2026-08-16],
    `g1_final`/`g2_final`): 07x11 0 FAILED-Zeilen (39/39 Silence-Inserts
    angewendet, Delta -92ms), 07x12 0 FAILED-Zeilen (496/496 angewendet,
    Delta -35040ms → **-23ms**, alle 4 Großlücken weiterhin in
    `es_missing_ranges`). Clean-H.264-Regression byte-identisch zur alten
    Baseline.

- **ttcut-demux: Video-Dauer falsch gemessen → Über-Padding + irreführende Meldungen**
  → **FIXED 2026-07-12** (`f85b237` + `d7a046b` Skript, `fc2a573` TTCut-GUI)
  - Video-Dauer jetzt aus der Video-PTS-Spanne: `start_time` (erstes dekodierbares
    Bild, schließt Open-GOP-Leading-Bs aus) bis letzte PTS + eine Frame-Dauer, auf
    der reparierten TS. Futurama: 3 419 800 ms = 85 495 Frames, exakt gegen ffprobe
    count_frames (vorher Container 3 420 269 → Über-Padding). Frame-Zahl, Padding-Ziel,
    Drift folgen korrekt; Container-Dauer nur noch als Seek-Hilfe.
  - Warntext neutral: „N pictures with doubled PTS (field-picture pairs or TS corruption)".
  - GUI: Cluster-Klassifikation gleicht die .info-Positionen gegen die MPEG-2-Parser-
    Feldpaare ab → bestätigte heißen „Feldpaare:", nur unbestätigte „Defekt:"; reiner
    Feldpaar-Fall importiert still ohne Dialog. **Reihenfolge-Fix:** Auswahl + Dialog
    laufen in `onOpenVideoFinished` (Parser dann bereit), Pending-Flag verhindert
    Dialog beim Projekt-Reload.
  - Verifiziert: Video-ES byte-identisch, Audio-vor-Padding byte-identisch,
    es_extra_frames unverändert, H.264-Regression läuft, Benders-Audio bit-identisch.
    Protokoll `CLAUDE_TMP/TTCut-ng/demuxfix/REDEMUX.md` [Material verloren 2026-08-16].
  - `/usr/bin/ttcut-demux` aktualisiert (byte-identisch mit dem Repo-Stand, geprüft 2026-07-12).

- **Decode error detection for H.264/H.265 streams during demux** → **DONE** (v0.63.0)
  - Implemented as `ttcut-pts-analyze` (formerly `ttcut-esrepair`): mmap-based start-code scanner,
    per-segment decode testing with custom AVIOContext, multi-threaded, integrated into ttcut-demux and TTCut-ng
  - H.265 false positives fixed: `AV_EF_CAREFUL` only for H.264/H.265 (not MPEG-2)

### Suche und Dekodierung

- **Equal-Frame Search: H.264/H.265-Support fehlt** → **DONE** (commit 24562c0)
  - `TTFrameSearchTask::decoderKindFor()` dispatcht codec-aware: `TTFFmpegWrapper` (YUV-API)
    für H.264/H.265, `TTMpeg2Decoder` für MPEG-2 — für Reference- und Search-Stream.
  - Algorithmus bleibt YUV-byte-delta (SSIM/cross-correlation wäre separate Verbesserung).

- **Parallele Dekodierung mit mehreren FFmpegWrapper-Instanzen** → **DONE** (Search-Performance-Refactor, gemerged d20a070)
  - `TTSearchTask` ist Coordinator mit lokalem `QThreadPool` + `parallelMap`; N Sub-Decoder
    (`TTSettings::searchWorkerCount`, Default 4) für Black-/Scene-/Logo-Suche
  - Scaling-Investigation: Sweet Spot 4-8 Worker, siehe `project_hevc_search_perf_investigation.md`

### GUI und Wiedergabe

- **TTMpv-Wrapper: zwei Folge-Verbesserungen aus den Player-Reviews** →
  **GELÖST**. Verschoben aus `TODO.md` beim v0.82.2-Abgleich; die beiden
  übrigen Punkte des Eintrags (Stop-Rest-Versatz ~5 Frames, erster PLAY ~5 s)
  bleiben dort offen.
  - **`TTMpvWrapper::stop()` „best-effort", gestoppter Frame ~1 Frame
    ungenau** → überholt/erledigt (2026-07-25): die vorgeschlagene synchrone
    Lesung ist längst implementiert. `TTMpvLibBackend::shutdown()` macht ein
    synchrones `pause` + synchrone `time-pos`-Lesung und propagiert sie nach
    `mPlaybackPosition`; `onPlaybackFinished()` nimmt als Stop-Frame
    `lastRenderedTimePos()`. Die verbleibende Ungenauigkeit ist allein der
    ~5-Frame-Pipeline-Versatz, kein Sync-Problem.
  - **`createTempMkvForPlayback` ohne Absicherung gegen `frameRate==0` und
    ohne Destruktor-Cleanup** → erledigt (2026-07-25, `25c966eb`):
    `frameRate <= 0` bricht mit Warn-Log ab und liefert einen leeren Pfad
    (der Aufrufer behandelt das als „Wiedergabe nicht möglich");
    `~TTCurrentFrame()` ruft `cleanupTempPlaybackFile()`. Der Temp-Dateiname
    ist seit v0.71.0 eindeutig (`ttcut-ng_playback_temp.mkv`).

- **„Wiedergabe-MKV dauert 6 Minuten, Fenster reagiert nicht" — Muxer war
  unschuldig, Ursache Konsole-cgroup-Limit** → **AUFGEKLÄRT (2026-08-17)**
  - Messkette: In-App-Mux 22 MB/s; `bench_playback_mux` standalone 55 MB/s
    von btrfs, aber 545 MB/s von tmpfs und linear bis 5,2 GB; `dd` liest
    dieselbe Datei mit 4,8 GB/s; Prozess dauernd State R, 485 µs pro
    32-KB-read, physische Reads 2,5× der logischen (Cache-Thrash).
  - Ursache: Konsole 26.04 Speicher-Monitoring (`konsolerc
    [MemorySettings] MemoryLimitValue=1000`) schreibt `memory.high=1 GB`
    direkt in die cgroup jedes Terminal-Tabs (auch Dolphins eingebettetes
    Terminal; `systemctl --user show` zeigt trotzdem `infinity`). Jeder
    daraus gestartete Prozess erbt das Korsett; das 8-GB-tmpfs-Schreiben
    des Wiedergabe-MKV erzwang Dauer-Reclaim.
  - Gegenbeweis: identischer Lauf unter `systemd-run --user --scope
    -p MemoryHigh=infinity` → 53 s → **2,1 s (1478 MB/s), Faktor 25**.
    Erklärt auch „finaler Schnitt ist schneller": der schreibt auf btrfs
    (Dirty Pages fließen ab), die Wiedergabe in tmpfs (shmem bleibt in der
    cgroup). Werkzeug: `tools/diag/bench_playback_mux` (`ed8f8db8`);
    Messfallen-Memory `reference_konsole_memory_high_cgroup`.
  - Verbleibender struktureller Rest (GUI-Thread-Blockade ~6 s) → TODO.md
    „Wiedergabe-Mux blockiert den GUI-Thread".
  - Nebenbefund derselben Sitzung: Roh-ES-Wiedergabe in mpv ist kein
    Ersatz für das Temp-MKV — libav meldet raw H.264 als timestamp-los
    (Seek „generally broken", gemessen: `--start` scheitert selbst bei
    60 s; Framerate ohne `--demuxer-lavf-o=framerate=50` um Faktor 2
    falsch); Timestamp-Sidecar-Dateien liest kein Player (mkvmerge
    `--timestamps` ist eine Muxer-Eingabe, MP4-`dref` hätte nur eine
    Lese-Hälfte in libav).

- **Untertitel-Delay editierbar + Delay-Vorzeichen auf mkvmerge-Konvention
  gedreht** → **DONE (2026-08-16, Branch `feature/subtitle-delay`)**
  - Die tote „Delay"-Spalte der Untertitelliste (hart `"0"` mit FIXME seit
    ihrer Einführung) ist jetzt eine QSpinBox (±9999 ms) nach dem
    Audio-Muster (v0.66.0). Wirkorte: Overlay-Lookup
    (`getSubtitleTextAtCurrentFrame`, Videozeit − Delay), mpv-Wiedergabe
    (`--sub-delay`, nur Current-Frame-Widget — die Schnittvorschau spielt
    die bereits geschnittene SRT, dort wäre ein `--sub-delay` eine
    Doppelanwendung), Schnitt (`cutSubtitleTracks`: Quellfenster −Delay,
    Ausgabe-Anker `offsett` backt den Versatz ein), Persistenz
    (`<Delay>` in der Subtitle-Sektion + `setPendingSubtitleDelay`).
  - **Vorzeichen-Recherche:** Die Audio-Delay-Richtung (v0.66.0) war
    nirgends entschieden oder dokumentiert (Spec `6c795bff`, Plan, Commit
    `497a103a`, Wiki — alle ohne Richtungsaussage); der in der Spec
    vorgesehene Matroska-Container-Delay wurde nie gebaut. Die
    Ist-Richtung war ein Implementierungsartefakt von `planAudioCut` und
    invers zu mkvmerge/mpv. User-Entscheid 2026-08-16: beide Spalten auf
    mkvmerge-Konvention (positiv = Spur später); Bruch für gespeicherte
    `.ttcut` mit Delay ≠ 0 akzeptiert (CHANGELOG-Hinweis).
  - **Beleg:** Harness `tools/diag/test_subtitle_delay` (im `diag`-Target),
    ALL PASS: SRT-Eintrag Quelle 10.0–11.0 s, Fenster 9.5–12.5 s →
    Ausgabe 500..1500 ms (Delay 0), 1000..2000 ms (+500, später ✓),
    0..1000 ms geklemmt (−500); `planAudioCut`-Fensterstart 10.016 s
    (Delay 0), 9.504 s (+500, früher holen = später spielen ✓),
    10.496 s (−500). Toleranz ±1 AC3-Frame (32 ms), Tux-AC3.
  - Randwissen: negatives `startMs` im Untertitel-Fenster ist zulässig
    (`searchTimeIndex` läuft linear ab Eintrag 0, kein Header vor 0);
    `audio_N_trimmed_ms`/`first_pts` aus der `.info` haben null Konsumenten
    und hängen nicht am Delay; `av_offset_ms` läuft getrennt über
    `setAudioSyncOffset` und wird bewusst nicht verrechnet
    (`ttmkvmergeprovider.cpp:549`).
  - GUI-Abnahme durch den User bestanden (2026-08-16, eigenes Video).

- **Nie gestartete Landezonen-Analysen erklären sich jetzt** → **GELÖST
  (2026-08-15, Commit `3b24be6a` im MPEG-2-Vorschau-Zweig; TODO-Eintrag von
  der Schlussprüfung 2026-08-11 war danach veraltet und wurde erst beim
  Abgleich am selben Abend zurückgezogen)**
  - `onAnalyzeStreamPoints()` legt für jede aktivierte, aber auf diesem
    Material nicht baubare Analyse eine Notiz an (Sequenzkopf-Analyse ohne
    Header-Liste = jedes H.26x-Material; Pillarbox ohne Indexliste; Audio
    ohne Tonspur). Läuft mindestens ein Worker, erscheinen die Notizen nach
    dem ersten `Start` im Detailbereich (nach dem Dialog-Reset, der sie
    sonst löschte — GUI-Abnahme-Nachkorrektur) und im Log; läuft keiner,
    listet ein eigener Dialog die Gründe, statt fälschlich „No detection
    methods enabled" zu behaupten.
  - Der einst unerreichbare Worker-Text „kein MPEG-2-Strom – übersprungen"
    ist durch die Notiz an der Aufrufstelle ersetzt; der Kommentar dort
    dokumentiert die doppelte Unerreichbarkeit.

- **Fortschrittszeilen fluteten den Detailbereich** → **GELÖST (2026-08-15;
  User-Befund vom 2026-08-12, bewusst zurückgestellt gewesen)**
  - War: der Bildformat-Scan meldete alle 20 Proben `Aspect format: %1 of %2
    samples` — der Zähler im Text hebelte die Wiederholungs-Unterdrückung von
    `TTProgressBar` aus (`msg != mLastStepMsg`), bei 5452 Stichproben 272
    Detailzeilen je Lauf. Die anderen beiden Analysen melden seit jeher
    konstante Texte und waren unschuldig — der TODO-Eintrag sprach von „drei
    Tasks", betroffen war eine Stelle in einer Datei.
  - Fix (`data/ttsearchtask_aspectscan.cpp`): Text konstant „Checking aspect
    format...", Zählerstand nur noch im Zahlwert (war dort schon immer —
    Balken und Prozentanzeige unverändert). Preis wie dokumentiert: die
    Aktionszeile über dem Balken zeigt keinen Zählerstand mehr. Übersetzung
    ergänzt (761/761).
  - Beleg: `test_aspectscan` — alle Step-Meldungen gleicher Text, Zähler im
    Wert (64/80/112/120), Abschluss-Statistikzeile unverändert; im Dialog
    entsteht daraus genau eine Zeile.

- **Teilfehlschläge beim Spurenschnitt meldeten sich als Erfolg** →
  **GELÖST (2026-08-15)**
  - War: `cutAudioTracks()` überspringt gescheiterte Spuren still (Index
    außer Bereich, fehlender Stream, leerer Plan) und `cutAudioStream()` kann
    `ok=false` liefern — nur der Audio-only-Pfad verglich seit `c7436a07` die
    Spurenzahl. MPEG-2-Schnitt und `TTH26xCutTask` muxten ein MKV **ohne die
    Spur**, meldeten Erfolg und schrieben einen Kalibrierfaktor auf falscher
    Arbeitsbasis.
  - **Laufzeit-Repro zuerst** (Regel [[feedback_repro_before_code]] — der
    TODO-Eintrag war eine Code-Lesung): neuer Harnisch
    `tools/diag/test_partial_track` erzeugt einen echten Teilfehlschlag —
    zweite Tonspur als Kopie, Datei nach dem Kopflisten-Aufbau gelöscht,
    `cutAudioStream` scheitert echt. Rot auf beiden Pfaden gemessen:
    `lastCutError=''`, „mux stage seen" trotz fehlender Spur.
  - Fix, Entscheid des Users: **Fehlschlag, Stopp vor dem Mux** (wie der
    mplex-Präzedenzfall). H26x: Zählung `cutAudioFiles.size()` gegen
    `audioCount()`, bei Abweichung `fail(...)` + `return` vor dem Mux —
    die bestehende `mError`-Kette meldet `Failed`. MPEG-2: `ok`-Zählung in
    der Rückruf-Lambda, Prüfung **nach** dem Sync-Abort-Zweig (Abbruch
    behält Vorrang) und **vor** dem Pool-Start — weder Videoschnitt noch Mux
    laufen an. Beide Pfade: Meldung „Only %1 of %2 audio track(s) could be
    cut", fertige ES-Dateien bleiben liegen (echter Fehler räumt nicht auf),
    kein Kalibrierfaktor (nur regulärer `Exit` schreibt einen). Übersetzung
    ergänzt (761/761).
  - Belege: Harnisch grün auf H.264 und MPEG-2 (Kontrolllauf mit 2/2 Spuren
    unverändert erfolgreich, Mux-Stage gesehen; Teilfehlschlag: Fehlertext
    „1 of 2", Mux-Stage nie erreicht, `_cut.264` + `_audio1.ac3` liegen).
    Regression grün: `test_cut_outcome`, `test_h26xcut_abort` (audio),
    `test_audioonlycut_abort` (none/audio), `test_cutsequence_abort`.
  - Nicht im Umfang: `cutSubtitleTracks()` (meldet weiter nur über die
    Dateiliste) und die Rest-Randnotiz, dass Empfänger außerhalb des
    Pool-Wegs (Suchen) einen echten Fehler als Abbruch sehen.

- **Schieber: Entprellung + Keyframe-Vorschau beim Ziehen** → **GELÖST
  (2026-08-15, GUI-Abnahme durch den User auf UHD und H.264)**
  - War (auch nach dem EAGAIN-Fix): `onVideoSliderChanged()` dekodierte
    **jede** Rastung eines Zugs synchron im GUI-Faden — 50 Ereignisse ×
    2,6 s auf UHD hieß Minuten Blockade; auf H.264 1080p immerhin 50 × 68 ms
    ≈ 3,4 s. Dazu pumpte `onSliderMoved` → `processEvents()` mitten im
    Dekodieren weitere Schieber-Ereignisse in die Warteschlange.
  - Zur Frage „reichen N Bilder vor dem Ziel?": frei wählbar ist N nicht —
    P/B-Bilder verweisen auf den Keyframe ihrer GOP, dekodiert wird zwingend
    ab Keyframe, und das tat der Code schon. Teuer waren die Zwischenpositionen
    und die DPB-Vorfüll-GOP (Sprung zum Keyframe **vor** dem Ziel-Keyframe).
  - Fix, drei Bausteine:
    1. **Entprellung** (`TTCutMainWindow`, 50-ms-Einzelschuss): `valueChanged`
       merkt nur die neueste Position; veraltete Zwischenpositionen werden nie
       dekodiert. `sliderReleased` feuert sofort.
    2. **Keyframe-Vorschau beim Ziehen** (`TTFFmpegWrapper::
       decodeNearestKeyframe` → `TTMPEG2Window2::showKeyframeFastAt` →
       `TTCurrentFrame::onGotoFramePreview`): solange der Knopf gedrückt ist,
       wird das Keyframe am/vor dem Ziel **ohne** Vorfüllung dekodiert — das
       No-Prefill-Muster des Suchmodus, für I-Bilder dokumentiert sicher.
       Beim Loslassen kommt das exakte Bild auf dem bisherigen Weg (volle
       Vorfüllung, WYSIWYG für Schnitte). MPEG-2 nimmt beim Ziehen bewusst
       den normalen Weg (libmpeg2 ist pro Bild billig).
    3. `processEvents()` im Navigator entfernt (Wiedereintritt).
  - Messwerte (`tools/diag/test_slider_decode_cost`, Median je Ereignis,
    exakt → Vorschau): UHD-SES 2,6 s → **99 ms**; HEVC-4K-CRA 628 → 51 ms;
    H.264 1080p 68 → 8 ms; MBAFF 30 → 5 ms; PAFF 28 → 4 ms. Gates
    `test_stilldisplay` und `test_wrapper_map` (Tux-MBAFF) unverändert grün.
  - Die dokumentierte Immer-Seek/DPB-Entscheidung für das **endgültige** Bild
    ist unangetastet; die Vorschau ist ein zusätzlicher, ehrlich beschrifteter
    Pfad (Positionsanzeige zeigt das wirklich angezeigte Keyframe).

- **UHD-Schieber-Hänger: EAGAIN-Paketverlust in `skipCurrentFrame()`** →
  **GELÖST (2026-08-15)**
  - Symptom (GUI-Abnahme 2026-08-15): eine Schieber-Bewegung auf UHD-HEVC,
    Programm minutenlang nicht bedienbar; gemessen 335 s CPU im GUI-Faden bei
    5 min Laufzeit, alle anderen Fäden bei null.
  - **Widerlegte Erklärungen, jede gemessen** (Sonde
    `tools/diag/test_slider_decode_cost`): (1) „viele billige Ereignisse
    stapeln sich" — ein einzelnes Ereignis lief in den Timeout; (2) „Index
    ohne Keyframes/Offsets → Seek auf Byte 0" — Index hat 155 Keyframes,
    18478/18478 gültige Offsets; (3) „4K-Dekodieren ist eben langsam" —
    ffmpeg dekodiert dieselbe Datei mit ~60 fps, sogar einfädig.
  - **Ursache, per Tag-Logging gezeigt**: `avcodec_send_packet` liefert
    EAGAIN (Ausgabe-Warteschlange voll — bei B-Hierarchie staut der Dekoder
    mehr, als der Ein-Paket-ein-Frame-Takt abholt). Der Code tat
    `if (ret < 0) continue;` — Paket **verworfen**, Decode-Tag verbraucht.
    Die Warteschlange blieb voll, also traf jedes weitere Paket bis zum
    Dateiende dasselbe EAGAIN: Log zeigt „packet with tag 4562 … DROPPED"
    lückenlos bis EOF. Das Ziel-Tag des Skip-Loops erschien nie, `decodeFrame()`
    wiederholte komplett und fiel dann **rekursiv** auf `frameIndex-1` zurück —
    Minuten bis Stunden für einen Aufruf.
  - Fix (`extern/ttffmpegwrapper.cpp`): korrekte libav-Pumpe. Erst
    `avcodec_receive_frame` versuchen (bereitliegende Ausgabe ist das
    Ergebnis dieses Aufrufs); bei Send-EAGAIN das Paket als
    `mPendingPacket` halten und im nächsten Durchgang zuerst senden.
    Schwebendes Paket wird bei Seek (fremdes Tag-Umfeld) und `closeFile()`
    verworfen. Diagnose-Logging hinter `logFFmpegDecoder` bleibt drin.
  - Belege: UHD-Einzelbild **2589 ms statt Timeout** (>240 s, real
    Kaskade ohne Ende), volle Messung Median 2,6 s je Ereignis, Bild
    geliefert. Regression unverändert grün: Tux H.264 progressiv 70 ms,
    MBAFF 30 ms, HEVC-4K-CRA 630 ms (Werte wie vor dem Fix),
    `test_wrapper_map` (mit Tux-MBAFF; der Default-Pfad des Harnisch zeigt
    auf eine gelöschte Datei — Fehlschlag besteht auch ohne den Fix),
    `test_stilldisplay`, `test_h264_leading`, `test_h26xcut_abort`,
    `test_preview_then_cut`-Kette.
  - Rest des UHD-Punkts (synchrones Dekodieren im GUI-Faden, SIGABRT)
    bleibt in `TODO.md`.

- **Ein Abbruch nach dem Abschluss meldete die fertige Aufgabe als
  abgebrochen** → **GELÖST (2026-08-15)**
  - War: `TTThreadTask::abort()` entschied allein an `!mIsRunning &&
    !mIsAborted`. Dieser eine Zweig bediente zwei Zustände, die er nicht
    unterscheiden konnte. **Nie gestartet** (Aufgabe hängt in der
    Pool-Warteschlange) braucht `aborted()`, und `TTCutMainWindow` verlässt
    sich darauf. **Schon fertig** sieht von dort identisch aus: `run()` hat
    `mIsRunning = false` gesetzt, `cleanUp()` gerufen und `finished(this)`
    abgesetzt, das als Ereignis in der Warteschlange des Oberflächen-Fadens
    liegt. Ein Abbruch in diesem Fenster legte ein `aborted()` obendrauf und
    ließ `cleanUp()` ein zweites Mal laufen, diesmal im anderen Faden.
  - **Gemessen**, nicht gelesen: neue Sonde
    `tools/diag/test_abort_after_finish` öffnet das Fenster gezielt statt
    darum zu würfeln — der Hauptfaden wartet **ohne** `processEvents()` auf
    das „operation ist zurück"-Flag der Aufgabe, dann erst kommt der Abbruch.
    Vorher `finished=1 aborted=1 cleanUp=2`, nachher `finished=1 aborted=0
    cleanUp=1`. Der Pool protokollierte vorher „Last thread task aborted" und
    meldete den Lauf als abgebrochen, obwohl er durchgelaufen war.
  - Fix: Abschluss-Merker (`std::atomic<bool> mIsFinished`), gesetzt in
    `run()` unmittelbar **vor** `emit finished`; `abort()` kehrt dann sofort
    zurück und schreibt eine Warnzeile ins Log statt still zu verpuffen
    (`AfterFinish: abort request arrived after the task had finished -
    ignored`) — damit der nächste Bericht „ich hab abgebrochen, es lief
    trotzdem durch" aus dem Log beantwortbar ist.
  - **Die Falle, die den naheliegenden Fix falsch macht:** Aufgaben werden
    wiederverwendet. `TTCutVideoTask` startet **eine** `TTCutTask`-Instanz für
    jeden Schnittlisteneintrag neu (`data/ttcutvideotask.cpp:170`). Ein
    stehenbleibender Merker hätte ab dem zweiten Eintrag jeden Abbruch
    verschluckt — genau der Defekt, den `1f372cca` behoben hat. Der Merker
    wird deshalb bei jedem Eintritt in `run()` zurückgesetzt; Fall 3 der Sonde
    prüft das. **Negativkontrolle gemessen**: ohne das Rücksetzen bleibt
    Fall 3 auf `aborted=0`, das Kriterium kann also fehlschlagen.
  - Regression, weil die Änderung die Basisklasse **jeder** Aufgabe betrifft,
    alle bestanden: `test_pool_abort`, `test_stale_abort`,
    `test_progressbar_reshow`, `test_cut_outcome`, `test_cutsequence_abort`,
    `test_h26xcut_abort` (none/video/audio/mux), `test_mpeg2cut_abort`
    (none/video/mux), `test_audioonlycut_abort` (none/audio). Nicht gelaufen:
    der GUI-Dauerlauf `soak-abort.sh` (braucht XWayland und den Bildschirm,
    und er zielt ohnehin auf den offenen `doH264Cut`-Absturz).

- **Gleichbild-Suche baute den Bildindex ein zweites Mal** → **GELÖST
  (2026-08-15)**; der TODO-Punkt, aus dem das kam („`TTSearchTask` trägt nichts
  zum Fortschritt bei"), **beruhte auf einer falschen Zählung**
  - Behauptet war: `TTFrameSearchTask` melde „1× Start und 3× Step für eine
    ganze Suche", der Balken bewege sich deshalb kaum. Beides falsch. Die
    Zählung stammte aus einer Stichprobe, der Satz über den Balken war eine
    **Ableitung daraus, keine Beobachtung** — in der Oberfläche hat das nie
    jemand gesehen. Neue Sonde `tools/diag/test_framesearch_progress` (führt
    einen echten `TTFrameSearchTask` synchron aus und stempelt jede
    Statusmeldung): die Vergleichsschleife meldet **einen `Step` pro Bild**,
    auf echtem Material 2251 Stück, größte Lücke im Millisekundenbereich.
  - Was die Messung stattdessen fand: zwischen `Start` und dem ersten
    verglichenen Bild lagen auf einer 224 930-Bilder-Aufnahme **5553 ms ohne
    jede Meldung** — 48 % der Laufzeit. Ursache war doppelte Arbeit, nicht
    fehlende Meldung: `initFrameSearch()` übernahm für den **Referenz**-Strom
    einen vorhandenen Bildindex (`TTH26xVideoStream::provideFrameIndexTo()`),
    der **Such**-Strom rief 110 Zeilen weiter unten bedingungslos
    `buildFrameIndex()` — ein voller zweiter Dateiscan desselben Materials,
    dessen Index die Anwendung beim Öffnen schon gebaut hatte (dort 5506 ms,
    von der Sonde getrennt ausgewiesen).
  - Fix: derselbe Übernahmeversuch auch für den Suchstrom, mit Rückfall auf
    den Scan, wenn der Strom noch keinen Index hat (anderes Element, nie
    geöffnet). Eine Datei, `data/ttframesearchtask.cpp`.
  - Belege vorher → nachher, `found index` als Gegenprobe unverändert:

    | Material | Vorbereitung | Laufzeit | `found index` | Steps |
    |---|---|---|---|---|
    | echt, 224 930 Bilder | 5553 → **199 ms** | 11 464 → 5864 ms | 180 = 180 | 2251 |
    | Tux H.264 1080p | 78 → 71 ms | 2997 → 3054 ms | 600 = 600 | 1451 |
    | Tux HEVC 4K | 608 → 495 ms | 22 987 → 22 435 ms | 600 = 600 | 1450 |
    | Tux MPEG-2 576i | 3 → 3 ms | 886 → 876 ms | 300 = 300 | 701 |

    MPEG-2 ist nicht betroffen: dort läuft kein `TTFFmpegWrapper`, der
    `TTMpeg2Decoder` nutzt die Listen des Stroms. Der HEVC-Rest von 495 ms ist
    die Dekodierung des 4K-Referenzbildes, nicht der Index.
  - **Nicht angefasst und kein Fehler:** die Schwarzbild-/Szenen-/Logo-Suche
    (`TTSearchTask`) meldet bewusst nicht an den Dialog. Sie sendet ihr eigenes
    `progress(int)` alle ~20 Bilder in die Statuszeile und hat ihren
    Abbrechen-Knopf im Navigationsbereich; ein Dialog erscheint dort gar nicht,
    weil `TTProgressBar` erst bei `Start` oder `Step` sichtbar wird. Der alte
    TODO-Text warf beide Suchen zusammen.

- **Der „Puls" des Fortschrittsbalkens bewegte sich nicht** → **GELÖST
  (2026-08-15)**
  - War: Nach 5 s ohne `Step` schaltet `TTProgressBar` auf `setRange(0, 0)`
    (unbestimmter Modus). Der Balken zeigte statische helle/dunkle Streifen,
    während die Debug-Uhr im Detailbereich weiterlief — der 1-s-Herzschlag
    arbeitete also, nur die Style-Animation fehlte.
  - Ursache: `gui/ttcutmain.cpp` setzte ein anwendungsweites Stylesheet
    (`QGroupBox::title { subcontrol-position: top center; }`). **Jedes**
    Stylesheet auf der `QApplication` legt `QStyleSheetStyle` über den
    Plattform-Style — für alle Widgets, nicht nur für `QGroupBox`. Unter den
    KDE-Styles hört der unbestimmte `QProgressBar` damit auf zu animieren.
  - **Gemessen** mit `tools/diag/test_pulse_stylesheet` (zählt `QEvent::Paint`
    am Balken selbst, statt jemanden hinschauen zu lassen; 3–5 s je Lauf,
    Wayland):

    | Style | ohne Sheet | mit Sheet | mit Proxy | Produktionsklasse |
    |---|---|---|---|---|
    | Breeze | 63,5 | **1,0 statisch** | 63,7 | 63,7 |
    | Oxygen | 63,8 | **1,0 statisch** | 63,8 | 63,5 |
    | Fusion | 60,4 | 60,7 | 60,8 | 60,7 |
    | Windows | 31,2 | 31,2 | 31,2 | 31,2 |

    Der Fehler war also KDE-spezifisch: Fusion und Windows animieren auch mit
    Stylesheet. Umgekehrt braucht **nur** Fusion die Zentrierung überhaupt —
    Breeze zentriert Gruppentitel von sich aus (Bildbelege je Style über
    `PULSE_GRAB`). Das Stylesheet zahlte den Preis dort, wo es nichts nützte.
  - Fix: `gui/ttcentredtitlestyle.{h,cpp}` — ein `QProxyStyle`, der das
    `textAlignment` der `QStyleOptionGroupBox` in `drawComplexControl` **und**
    `subControlRect` auf `AlignHCenter` setzt (ohne das Rechteck wäre der Text
    mittig gezeichnet, aber linksbündig beschnitten). `install()` erzeugt die
    Basis mit `QStyleFactory::create(objectName)` neu, weil `setStyle()` den
    bisherigen Style löscht und der Proxy ihn sonst als Wildzeiger hielte.
    Liefert die Factory für den Namen nichts — denkbar bei einem
    Fremdanbieter-Style —, wird **kein** Proxy gesetzt: ein `QProxyStyle` auf
    `nullptr` fiele still auf den Standard-Style zurück und tauschte damit das
    Aussehen des Nutzers aus. Lieber ein linksbündiger Titel. Der Zweig ist
    mit `PULSE_FAKE_STYLENAME` gemessen (Style bleibt `breeze`, 63,5 Paints/s).
  - **Vier widerlegte Erklärungen** (nicht wieder aufwärmen): deaktivierter
    Dialog, wiederholtes `Init`, `value=-1` nach `reset()` — alle drei mit
    `tools/diag/test_pulse_animation` widerlegt — und AddressSanitizer, der
    als letzter verbliebener Unterschied galt: der Fehler tritt im normalen
    Build genauso auf, sichtbar sobald man die 5-s-Schwelle vorübergehend auf
    1 s senkt.

- **Ein fehlgeschlagener Schnitt meldete sich als gelungener** → **GELÖST
  (2026-08-12, branch `feature/cut-outcome-reporting`, 11 Commits)**
  - War: Sechs Abschlussstellen in `TTAVData` schlossen eine Schnittoperation
    ab, und jede regelte für sich, in welcher Reihenfolge sie Fehlerfeld,
    Meldung und `cutFinished()` behandelt — teils gar nicht. Der H.26x-Pfad
    setzte `mLastCutError` **nach** dem `Exit`; `TTCutMainWindow::onStatusReport`
    liest es genau dort, um zu entscheiden, ob der Lauf regulär endete, und
    `TTProgressEstimator` schreibt für einen regulären Lauf einen
    Kalibrierfaktor. Ein misslungener Schnitt verzog also die Restzeit-
    schätzung aller folgenden Läufe.
  - **Gemessen statt abgeleitet:** Ein schreibgeschütztes Zielverzeichnis
    erzeugt einen echten Fehlschlag (`Cannot create output file`,
    `extern/ttessmartcut.cpp:589`). Der Lauf endete mit `Exit` und dem Text
    „Cutting failed", während `mLastCutError` erst drei Zeilen später
    zugewiesen wurde.
  - Fix: `TTAVData::finishCutOperation(CutOutcome, message, errorText)` legt
    Reihenfolge und Feldbelegung an **einer** Stelle fest — Ergebnis vor
    Meldung. Alle sechs Abschlussstellen rufen nur noch sie.
    `mCutOperationActive` bleibt bewusst beim Aufrufer, weil die
    Klammer-Logik daran hängt (`docs/code-map/progress-reporting.md`).
  - Dabei geschlossen, was vorher **nie** ein Fehlerfeld setzte: der
    Audio-only-Pfad (die Aufgabe wies ihr Feld an keiner Stelle zu — belegt
    mit `grep -c mError` = 0, ihr eigener Header sagte es), der mplex-Zweig
    und — nach einer Messung, die das Gegenteil zeigte — der **Startfehler**
    des Multiplexers: Bei fehlendem `mplex` schlägt `QProcess::start` fehl,
    Qt sendet **kein** `finished()`, die Exit-Code-Auswertung wird nie
    erreicht. Da mplex optional ist, war das der häufigste Fehlerfall
    überhaupt. Auch Teilfehlschläge sind jetzt sichtbar („Only 1 of 2 audio
    track(s) could be cut", echt ausgelöst).
  - **Prüfmittel:** `tools/diag/test_cut_outcome` (neu) greift `lastCutError()`
    **im** `Exit`-Moment ab, nicht danach. Er war vor der Behebung rot und
    danach grün — der Umschlag ist der Beleg. Zwei weitere Zusicherungen kamen
    dazu, nachdem sich zeigte, dass „Feld ist gefüllt" zu schwach war: Es muss
    sich vom kurzen Klammertext unterscheiden **und** den ausführlichen Grund
    tragen. Gegengewicht waren die vier bestehenden Abbruch-Harnesse, die
    unverändert grün blieben — inklusive der `mux`-Phase, die die neue
    Abbruch-Absicherung unter echter Zeitlage prüft.
  - **Lehre, die diese Arbeit teuer gemacht hat:** Vier Detailannahmen des
    Plans waren falsch, alle vier aus dem TODO-Text übernommen statt am Code
    geprüft — die zwei H.26x-Texte waren vertauscht (`tth26xcuttask.h:64-68`
    dokumentiert das Gegenteil), `TTAudioOnlyCutTask::lastError()` war immer
    leer, der Elementary-Zweig hat gar keine Fehlerquelle, und der
    vorgeschlagene „Rückgabewert von `mplexPart()`" existiert nicht
    (`void mplexPart(int)`). Gefunden hat sie jedes Mal die zweistufige
    Prüfung, indem sie die im Quelltext dokumentierten Verträge las statt der
    Behauptung im Auftrag. Der TODO-Eintrag zeigte auf die richtige Ecke —
    aber auf keine einzige richtige Zeile.
  - **Offen geblieben (Teil B):** Ein echter Fehler meldet sich weiterhin als
    Abbruch, weil `TTThreadTask::run()` aus beiden `catch`-Zweigen dasselbe
    Signal sendet. Wurzel und Behebungsweg stehen in `TODO.md`.

- **Vorschau und MPEG-2-Schnitt gaben nicht frei, was sie anlegten** →
  **GELÖST (2026-08-12)**
  - War: `TTCutPreviewTask::operation()` löschte den geteilten Smart-Cut-Motor
    an **drei** Stellen (Init-Fehler, `catch`, Funktionsende) — und ein
    vierter Ausgang hatte keine: der Abbruchwurf am Kopf der Clip-Schleife
    (`:202`) liegt außerhalb des `try` und springt an allen dreien vorbei.
    Wer *zwischen* zwei Vorschau-Clips abbrach, verlor den Motor samt aller
    darin gehaltenen dekodierten Bilder (im TODO-Eintrag vom 2026-08-10 mit
    ~535 MB gemessen). Dazu vier Objekte, die niemandem gehörten:
    der `TTCutVideoTask` der Vorschau, deren Vorschau-Schnittliste, die
    innere `TTCutTask` und — auf dem Abbruchpfad — der Dateipuffer des
    MPEG-2-Schnitts samt offenem Deskriptor.
  - Fix: Der Motor hängt an einem `qScopeGuard`, der die heikle Reihenfolge
    (Verfolgungszeiger unter `mSmartCutMutex` nullen, **dann** löschen) an
    einer statt an drei Stellen trägt und auf jedem Ausgang greift.
    `TTCutPreviewTask` und `TTCutVideoTask` haben Destruktoren bekommen.
    Nebenbei zwei uninitialisierte Zeiger in `TTCutVideoTask` beseitigt
    (`mpTgtStream`, `mpCutParams`) — ohne die hätte der neue Destruktor auf
    Müll gezeigt; sie waren vorher harmlos, weil sie niemand las.
  - **Beleg (ASAN, `tools/diag/test_previewcut_abort`, Fall `audio`, Tux
    1080p progressive):** die vier Fundstellen `ttcutpreviewtask.cpp:60`,
    `ttcutvideotask.cpp:37` und `createPreviewCutList` im Leak-Bericht
    vorher → **null** nachher. Die **Gesamtsumme taugt hier nicht als Maß**:
    sie wird vom Grundrauschen des Harness bestimmt (der baut in `main`
    Video- und Audiostream auf und gibt sie nie frei) und schwankte schon
    vor dem Eingriff zwischen 4 205 480 und 4 253 944 B. Ein Referenzlauf
    ohne jeden Abbruch (`none`) leckte genauso viel wie die Abbruchläufe —
    erst das zeigte, dass die 4,2 MB nichts mit dem Defekt zu tun haben.
  - **Nicht bewiesen:** Das 535-MB-Leck selbst wurde in sechs Läufen **nie
    ausgelöst** — der Harness armiert den Abbruch an einer festen Dateigröße
    (`size=262144 bytes at 1490 ms`) und landet damit reproduzierbar
    *innerhalb* Clip 1, wo schon vorher der `catch` aufräumte. Belegt ist
    also: die kleinen Lecks sind weg, und der Weg, auf dem das große
    entstand, räumt jetzt auf wie alle anderen. Wer es messen will, muss den
    Auslösepunkt des Harness an den Clip-Übergang hängen.

- **Detailausgabe der Landezonen-Analyse war unvollständig** → **GELÖST
  (2026-08-11, branch `feature/landezonen-detailausgabe`,
  `3c756b9b`..`8714169f` + Übersetzungs-/Doku-Commit)**
  - War: Der Detailbereich zeigte während der Analyse nur einen Zählerstand
    (`Aspect format: N of M samples`, alle 20 Proben). Ob etwas erkannt wurde,
    und vor allem *warum nicht*, war nicht ablesbar — ein Lauf ohne Fund war
    nicht von einem Lauf zu unterscheiden, in dem die Erkennung gar nicht
    ansprang. Die interessanten Zahlen existierten bereits, gingen aber nur
    per `qDebug()` hinter `logCutPipeline()` heraus, also im Normalbetrieb
    nirgendwohin.
  - Betroffen waren alle **drei** parallel laufenden Worker, nicht nur der im
    TODO genannte Pillarbox-Scan: `TTAspectScanTask`,
    `TTStreamPointVideoWorker` (Seitenverhältnis aus MPEG-2-Sequenzköpfen)
    und `TTStreamPointAudioWorker` (Stille, Tonformatwechsel).
  - Umsetzung: neuer GUI-freier Protokollierer `TTAnalysisLog`
    (`data/ttanalysislog.{h,cpp}`, Deckelung bei 20 Ereigniszeilen je
    Abschnitt, Positionsformat wie in der Landezonen-Liste). Transport über
    `StatusReportArgs::AddProcessLine` — der einzige Statuszustand ohne
    Fortschrittswirkung: er passiert `TTThreadTask` und `TTThreadTaskPool`
    ohne Berührung der Schrittzähler und erreicht `TTProgressBar` nur als
    `appendDetailLine()`. Dafür geben zwei bestehende Bausteine erstmals
    heraus, was sie bisher verschluckten: `classifyAspectSample()` seinen
    Ablehnungsgrund (`TTAspectReason`, optionaler Ausgabeparameter mit
    Vorgabewert, damit die ~15 bestehenden Aufrufe gültig bleiben) und
    `TTAspectHysteresis` ihre verworfenen Kandidaten
    (`takeDiscardedCandidate()`, `feed()`-Signatur unverändert).
  - **Beleg für „Fortschritt unverändert":** Vor der ersten Codeänderung
    wurde mit `tools/diag/test_aspectscan` eine Referenzaufzeichnung aller
    Statusmeldungen über `03x01_-_Drunter_und_drüber.264` gezogen (45 Zeilen:
    1× `Start` mit Wert 899, 43× `Step`, 1× `Finished`). Nach der Änderung
    war derselbe Vergleich **zeichengleich leer**; nur `AddProcessLine`-Zeilen
    kamen hinzu. Der Harness schreibt die Meldungen seit `3c756b9b` selbst
    mit (`STATUS|<state>|<value>|<msg>`), der Vergleich ist also wiederholbar.
  - **Nebenbefund, den die neue Ausgabe sofort beantwortete:** Der Lauf über
    `03x01` findet genau *einen* Übergang (16:9 → 4:3pb bei 00:16:34). Ob ein
    Rückübergang fehlte, war vorher nicht zu klären. Die Bilanz zeigt
    192 Proben 16:9 (≈ 16 min, exakt bis zum Übergang) und 669 Proben 4:3pb
    (≈ 56 min): die Aufnahme endet im 4:3-Format, es fehlt nichts.
  - **Sichtbar gemachte Lücke, nicht geschlossen:** `detectAudioChanges()`
    wertet ausschließlich AC3-`acmod` aus, MP2-Kopfdaten werden übersprungen
    (`ttstreampoint_audioworker.cpp`, Kommentar „Skip for now"). Bei einer
    MP2-Tonspur meldet der Dialog das jetzt ausdrücklich, statt wie ein Lauf
    ohne Funde auszusehen.
  - Prüfmittel: `tools/diag/test_analysislog` (neu; Deckelung, `resetCap()`,
    Positionsformat inkl. Bildrate 0), `tools/diag/test_aspectdetect`
    (erweitert: alle fünf Ablehnungsgründe, verworfene Kandidaten inkl. des
    Einzelproben-Laufs mit Haltedauer 0 — dieser Fall war zunächst nur
    `(void)`-gecastet und wurde in einer Nachbesserungsrunde nachgezogen,
    mit Gegenprobe: 7 Fehlschläge bei deaktivierter Aufzeichnung),
    `tools/diag/test_streampoint_order` (gegen echtes MPEG-2-Material mit
    4:3→16:9-Wechsel, Markerpositionen 20305/99695 unverändert).
  - Falle für später: `lupdate` **muss** `ui/` mit scannen. Ohne dieses
    Verzeichnis meldet es 267 Oberflächentexte als verschwunden, und ein
    `-no-obsolete` im selben Lauf hätte sie gelöscht. Aus demselben Grund
    wurde `-no-obsolete` hier bewusst weggelassen — die Datei trägt 64
    Alteinträge, deren Aufräumen nicht in diesen Zweig gehört.

- **„Abbrechen" wirkte nur auf den MPEG-2-Videoschnitt** → **GELÖST
  (2026-08-10, branch `feature/cut-abort`, `f5a22762`..`f9352969`, 17 Commits)**
  - War: Der Abbruch erreichte ausschließlich Pool-Aufgaben. Der H.26x-Schnitt
    lief synchron auf dem GUI-Faden und damit ungebremst zu Ende; ebenso die
    Audio- und Mux-Phasen des MPEG-2-Schnitts, der reine Audioschnitt und die
    Vorschau. Das Hauptfenster blieb bis zum Ende deaktiviert.
  - Jetzt: `TTESSmartCut` und `TTMkvMergeProvider` haben ein
    Abbruchflag (`requestAbort()`/`wasAborted()`), `TTFFmpegWrapper::cutAudioStream`
    ein `shouldAbort`-Prädikat; `doH264Cut` und `doAudioOnlyCut` laufen als
    Pool-Aufgaben (`TTH26xCutTask`, `TTAudioOnlyCutTask`), der MPEG-2-MKV-Mux
    als zweiter Pool-Lauf (`TTMuxTask`). Ein Abbruch löscht alles, was der
    Lauf erzeugt hat, meldet `Canceled` statt `Exit`, sendet kein
    `cutFinished()` und schreibt keine Fehler-, Warn- oder Fatal-Zeile.
    Ein echter Fehler lässt seine Teildateien liegen.
  - Nebenbefund, in Aufgabe 1 als Blocker behoben: **Double-Free in
    `TTESSmartCut::runEncodePass()`** (`4547c300`). Die Schleife band
    `AVFrame*` per Wert, `av_frame_free(&frame)` nullte nur die Kopie; jeder
    frühe `return` übersprang das rettende `framesToEncode.clear()`, und
    `~ReencodeContext` gab dieselben Zeiger erneut frei. Ohne Abbruch nur bei
    einem seltenen Encoderfehler erreichbar — mit Abbruch der Normalfall.
    Belegt: SIGABRT („corrupted size vs. prev_size") reproduzierbar mit
    zurückgenommenem Fix, sauber mit Fix, auf beiden Codecs.
  - Belege: sieben neue Prüf-Harnesses in `tools/diag/`. Drei prüfen die
    Motoren einzeln (`test_smartcut_abort`, `test_audiocut_abort`,
    `test_mkvmux_abort`), vier fahren die echte `TTAVData::onDoCut` →
    Pool → Task-Kette kopflos und brechen über ein in die GUI-Schleife
    gestelltes `onUserAbortRequest()` ab — also über genau den Weg, den der
    Abbrechen-Knopf nimmt (`test_h26xcut_abort`, `test_mpeg2cut_abort`,
    `test_audioonlycut_abort`, `test_previewcut_abort`; die Vorschau kommt
    über `doCutPreview()` statt `onDoCut()`).
    Abschlussmatrix 2026-08-10: 24 Läufe (H.264, H.265, MPEG-2 inkl. mplex,
    Audio-only, Vorschau; je Phase ein Abbruch plus Kontrolllauf) bestanden,
    dieselben 24 unter ASAN ohne einen einzigen
    `ERROR: AddressSanitizer`.
  - Nicht-Vakuität wurde für jede Aufräum-Zusage einzeln gezeigt: mit
    abgeschaltetem Löschen bleiben die Teilprodukte messbar liegen (z. B.
    157 440 von 960 000 B Audio, 2 948 872 B teilweise geschriebene MKV,
    20 492 134 B teilweises Video-ES).
  - A/B gegen `8cc32c6a` (letzter Motor-Commit, **nicht** `master`) über
    `--auto-cut`: Sollwerte zuerst geprüft (40,000 s / 2000 Video- /
    1250 Audiopakete für H.264 und H.265; 84,000 s / 2100 / 3500 für
    MPEG-2), dann verglichen — H.264 und H.265 paketweise identisch in Bild
    und Ton, MPEG-2 identisch in Paketzahl, Dauer und Tonspur.
  - Nach der Gesamtdurchsicht des Zweiges nachgezogen (2026-08-10):
    - Eine **abgebrochene Vorschau** meldete `Exit "exiting thread pool"`
      statt `Canceled` — der Balken sprang damit auf 100 % und der Dialog
      meldete „Finished after …" für einen Lauf, den der Nutzer abgebrochen
      hatte. `onCutPreviewAborted()` setzt jetzt (nur bei einem echten
      Abbruch, nicht bei einem Fehler) `mCutOperationActive` und meldet
      `Canceled "Preview cancelled"`; `onThreadPoolExit()` verbraucht das
      Flag wie auf den vier Schnittpfaden.
    - Die Verbindung `aborted → onCutAborted` wurde auf dem **Erfolgspfad**
      des MPEG-2-Schnitts nie gelöst — jeder fertige MPEG-2-Schnitt ließ
      eine dauerhafte Verbindung zu einem Slot zurück, der Dateien löscht.
      Gelöst in `finishMpeg2Cut()`; dieselbe Disziplin für die Vorschau in
      `onCutPreviewFinished()`.
    - Neues Harness `tools/diag/test_cutsequence_abort`: sechs Operationen
      auf **einem** langlebigen `TTAVData` (wie in der Anwendung). Kein
      bisheriges Harness prüfte zwei Operationen auf demselben Objekt.
    - Alle Abbruch-Harnesses hängen jetzt am Ziel `diag-abort`, damit ein
      Prüflauf nicht wieder unbemerkt alte Binaries aus einer früheren
      Sitzung ausführt.
    Abschlussmatrix danach: 25/25 einfach und 25/25 unter ASAN, jeweils
    ohne `ERROR: AddressSanitizer`.
  - Bleibende Einschränkungen und die dabei gefundenen vorbestehenden
    Defekte stehen in `TODO.md`; wie die Kette heute funktioniert, in
    `docs/code-map/progress-reporting.md` und `docs/code-map/smart-cut.md`.

- **Fortschrittsanzeige-Überholung** (Details-Panel leer, Audio-Cut ohne
  echte Prozente, MPEG-2-Finalschnitt teilweise außerhalb des Dialogs) →
  **GEFIXT (2026-08-06/07, branch `feature/progress-details`)**
  - War: Details-Panel des Fortschrittsdialogs blieb leer — die
    Cut-Pipeline meldete `task==0` und `AddProcessLine` wurde verworfen.
    Audio-Cut sprang pro Spur in einem Sprung (`TTFFmpegWrapper::cutAudioStream`
    hatte keinen Pro-Packet-Callback). MPEG-2-Finalschnitt hatte
    Pool- statt Operationsklammern: Audio wurde VOR dem Dialog stumm
    geschnitten, Mux lief NACH dessen Verstecken unsichtbar, und es gab
    keinen End-Exit im Erfolgspfad. Fensterkreuz (X) ging in drei
    Import-Dialogen ("Defekte Frames erkannt", "Stream-Integritätswarnung")
    nicht (zwei AcceptRole-Buttons → kein RejectRole/Escape-Button; Repro
    `CLAUDE_TMP/TTCut-ng/msgbox_close_repro.cpp` [Material verloren 2026-08-16]).
  - Fix: zentrales Zeitstempel-Log in `TTProgressBar` (Details-Panel als
    Live-Statuslog, inkl. Re-Encoder-/mplex-Ausgabe); bei angehaktem
    „Details anzeigen" bleibt der Dialog nach Abschluss offen
    (Abbrechen→Schließen), Mid-Task-Fehlermeldungen halten ihn ebenfalls
    offen (`d0240fbf`, `91a5d014`, `071800e5`). `cutAudioStream` bekam
    einen optionalen `std::function<void(int percent)>`-Callback an der
    `av_read_frame`-Schleife (`9b8b2746`), verdrahtet in H.26x- und
    Audio-Only-Pipelines (`720c083f`). MPEG-2-Finalschnitt bekommt eigene
    Operationsklammern über `mCutOperationActive`, konsumiert am
    Pool-Exit, um einen doppelten Exit nach Abbruch zu vermeiden
    (`6e71d79c`, `ec45323a`). Deutsche Übersetzungen ergänzt (`3b7b6499`).
  - Nebenbefund: totes `TTTaskProgress` + `isBlocking` entfernt. Statuskette
    komplett inventarisiert: Öffnen / Framesuche / Vorschau (Pool-Klammern
    bereits korrekt), Quick-Jump (eigener Pool, unverbunden),
    Playback-Temp-MKV (sendet nichts) — beide unauffällig, kein Fix nötig.
  - Belege: Gate `tools/diag/test_audioprogress` auf Tux-AC3 — streng
    steigende Prozente, Ende bei 100, ≤101 `av_read_frame`-Aufrufe (Bound
    gegen Busy-Loop-Regression). GUI-Abnahme (Details-Log, Schließen-Button,
    MPEG-2-Dialog-Abdeckung, Stream-Point-Analyse, Fensterkreuz,
    Abbruch-Verhalten) steht beim User noch aus.

- **KWin-Stale-Area-Fehler („eingefrorene" Standbild-Fläche bei fraktionaler
  Skalierung) — App-Auslöser gefunden und entschärft** → **GEFIXT
  (2026-08-06, `f87ea06c`)**
  - Symptom (seit 2026-07-30 verfolgt, zwei Tage als TTCut-Defekt gejagt):
    KWin 6.7.2 + Skalierung 1,5/1,75 + großes Fenster → Teile des Fensters
    werden nicht aufgefrischt; Alt-Tab-Vorschau zeigt den korrekten Puffer.
    Schwellen-Matrix, Wayland-Protokollbeweis, Messfallen: Memory
    `kwin-fractional-scale-bug` + `/usr/local/src/CLAUDE_TMP/TTCut-ng/`
    (`kwin-*`, `wayland-diff/`, `kwin-bugreport/`).
  - Neuer Einstieg war die User-Beobachtung „Fehler ist weg, sobald einmal
    Play gedrückt wurde" + die Frage „warum besteht das Problem bei CutOut
    nicht?". Experiment-Matrix 2026-08-06 (alle User-verifiziert, Qt6):
    native Fenster-Neuerzeugung → Fehler blieb; ein komponierter GL-Frame
    (Warmup v1) → blieb; 25-fps-Frame-Strom über 3 s (Warmup v2) → blieb;
    `TTCUT_DIAG_NO_PLAYER` (kein GL-Widget) → **Fehler weg**. CutOut ist
    strukturell identisch minus GL-Widget und war nie betroffen.
  - Auslöser (Qt6): das permanent sichtbare-aber-verdeckte QOpenGLWidget im
    StackAll-Frame-Stack. Notwendig, aber nicht hinreichend — der minimale
    Testfall mit QOpenGLWidget reproduziert weiterhin nicht (zweite Zutat
    unbekannt, Upstream-Report offen → TODO.md).
  - **Pfad-Abhängigkeit statt Widerspruch:** dieselbe Bisektion unter Qt5
    (2026-08-02) sah den Fehler `TTCUT_DIAG_NO_PLAYER` und das fast leere
    Fenster überleben — Qt5 lief über KWins forced-server-side-scale-Pfad
    (kein fraktionales Protokoll), Qt6 bindet `wp_fractional_scale`. Die
    Auslöser-Mengen der beiden Pfade unterscheiden sich.
  - Fix: Frame-Stack läuft außerhalb der Wiedergabe im Default StackOne
    (GL-Widget wirklich versteckt, Struktur wie CutOut); StackAll nur für
    die Wiedergabe-Session (vor mpv-Load gesetzt, in onPlaybackFinished
    zurück) — dort muss das Widget verdeckt anrendern können, bevor
    firstFrameReady es nach vorn hebt (Henne-Ei-Problem bleibt gelöst).
  - Abnahme (User, alle drei PASS): kein Einfrieren mehr ab Start ohne
    Play; Play→Stop→Play funktioniert ohne Schwarzbild; nach Stop bleibt
    die Navigation sauber. Damit sind xcb-/Ganzzahl-Skalierungs-Workarounds
    für TTCut-ng nicht mehr nötig. Warmup-Diagnoseschalter
    (`diag/gl-warmup`-Branch) nach negativem Ergebnis verworfen,
    `TTCUT_DIAG_NO_PLAYER` bleibt als Bisektionswerkzeug.

- **Hauptfenster wurde beim ersten Video-Open sichtbar neu aufgebaut**
  (User-Beobachtung 2026-08-06: „Oberfläche wird noch mal geladen") →
  **GEFIXT (2026-08-06, `be007c19`)**
  - Beleg per Event-Filter auf dem Top-Level: ~50 ms nach Beginn des
    Video-Opens Hide → 2× PlatformSurface/WinIdChange → Show = native
    Fenster-Neuerzeugung. Ursache: `ensurePlayerCreated()` fügte das erste
    QOpenGLWidget (mpv-Render-Widget) ins bereits sichtbare Fenster ein →
    Qt stellt den Surface-Typ auf RasterGLSurface um und muss das Fenster
    dafür abreißen (dokumentiertes Qt-Verhalten; bestand seit dem
    libmpv-Render-Backend v0.71.0).
  - Fix: `ensurePlayerCreated()` aufgeteilt — Player + Widget entstehen im
    TTCurrentFrame-Konstruktor (vor dem ersten show()), die GL-/mpv-
    Render-Context-Realisierung bleibt lazy beim Stream-Open (neu:
    `realizeRenderContext()`, braucht ein sichtbares Fenster).
    `TTCUT_DIAG_NO_PLAYER`-Bisektionsschalter greift auch im Konstruktor.
  - Abnahme: Event-Trace nach Fix ohne jedes Fenster-Ereignis nach dem
    Open; User-GUI-Abnahme (kein Neuladen, Play/Stop unverändert) PASS.

- **Aspect-Präsentation Standbild vs. Play inkonsistent bei 4:3** → **GEFIXT
  (2026-08-06, `b83cd002` + `9b4b78e2`)**
  - TODO-Hypothese („mpv-Renderpfad füllt ohne Aspekt-Erhalt, keepaspect
    prüfen") per Messung WIDERLEGT: der mpv-Pfad war korrekt (Harness
    `aspecttrace`, 4:3-Clip 720×576 DAR 4:3 → Content-Box 1,333 mit
    Pillarbox). Defekt war der **Standbild-Pfad**: `showVideoFrame()`
    (MPEG-2-Zweig) behandelte nur Aspekt-Code 3 (16:9, in
    Schrumpf-Richtung), Code 2 (4:3) gar nicht → Storage-Aspekt 1,25
    (Harness `stillgrab`, gemessen 1,249).
  - Fix 1 (`b83cd002`): DAR aus dem Aspekt-Code (2→4:3, 3→16:9, 4→2,21:1),
    Korrektur in Upscale-Richtung (Befund-D-Prinzip); 16:9 wechselt von
    Höhe-Schrumpfen auf Breite-Aufweiten.
  - Fix 2 (`9b4b78e2`, von der User-Abnahme aufgedeckter latenter Altfehler):
    Die Header-Suche lief über `currentIndex`, den `invalidateDisplay()` im
    Stop-Pfad auf -1 parkt → `getSequenceHeader(-1)` = ERSTER Header der
    Datei; bei Aufnahmen mit 4:3-Werbeblock am Anfang wurde das
    16:9-Standbild nach Stop/Resize auf 4:3 gequetscht (Repro: konkatenierter
    4:3+16:9-Clip, invalidate+resize → 1,333 statt 1,778). Jetzt eigener
    `mAspectIndex`, gesetzt beim Dekodieren, übersteht die Invalidierung.
  - Abnahme: stillgrab/aspecttrace 4:3 = 1,333, 16:9 = 1,778 auf beiden
    Pfaden, invalidate+resize stabil; User-GUI-Abnahme 16:9 (Play→Stop,
    Resize) + 4:3 PASS. Harnesses in
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/avlog-diag/` [Material verloren 2026-08-16].

- **Vorschau: Play-Button blieb auf „Start" trotz laufender Wiedergabe
  (VOR-Klick)** → **GEFIXT (2026-08-06, `908a672f`)**
  - Symptom (User-Report): Vorschau öffnen → VOR, oder Play → Stop → VOR;
    Wiedergabe startet, Button bleibt im Play-Zustand.
  - Root Cause per instrumentiertem Build + User-Repro (DIAG-Logzeilen):
    `onCutSelectionChanged` setzt bei VOR-Autoplay den Button synchron auf
    „Stop"; unmittelbar danach kippt `onPlayerError` ihn zurück, weil mpv
    error-Level-Meldungen zur Untertiteldatei loggt („Can not open external
    file preview_002.srt", 2×). Der Übergangs-Clip lag im Werbeblock →
    `cutSubtitleTracks` erzeugte eine 0-Byte-`.srt` (Datei wird
    bedingungslos geöffnet), der Dialog prüfte nur `exists()`.
  - Zwei Vor-Hypothesen widerlegt (Wrapper-Signal-Trace + headless
    getriebener echter Dialog, beide korrekt): END_FILE-Replace-Event
    (reason=STOP wird bereits gefiltert) und eof-reached-Race.
  - Fix: `onPlayerError` nur noch loggen (wie `TTCurrentFrame`, dort als
    Lektion schon dokumentiert); `.srt` nur bei Größe > 0 an mpv;
    `cutSubtitleTracks` löscht leere Ausgabe + `ok=false` (auch kein leerer
    Untertitel-Track mehr im finalen MKV).
  - Abnahme: Harness `dlgtrace` (Fall A leere srt: kein Fehler, Button
    „Stop"; Fall B unlesbare srt: 6 mpv-Fehlerzeilen, Button bleibt „Stop")
    + User-GUI-Repro beide Szenarien PASS; Fix-3-Laufzeitbeleg im Log
    („no subtitle entries in cut range, removed empty …", 2×).
  - Harness-Falle fürs nächste Mal: `TTCutPreview::cleanUp()` löscht beim
    Schließen ALLE `preview*` im tempDirPath — tempDirPath für Harnesses
    nie auf ein Verzeichnis mit eigenen `preview*`-Dateien zeigen (hat
    hier die Harness-Quellen gefressen).

- **libav-Konsolenausgaben trotz deaktiviertem libav-Logging** → **GEFIXT
  (2026-08-06, branch `fix/avlog-callback-after-mpv`)**
  - Beobachtet bei der Qt6-GUI-Abnahme 2026-08-04: nach der ersten
    mpv-Wiedergabe erschienen libav-Meldungen im ffmpeg-Default-Format auf
    stderr, obwohl der `logLibav()`-gegatete Callback installiert war.
  - Hypothese bestätigt und präzisiert (Laufzeit-Beleg `avlog_mpv_diag`,
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/avlog-diag/` [Material verloren 2026-08-16]): libmpv übernimmt den
    prozessglobalen av_log-Callback schon bei `mpv_create()` (nicht erst beim
    Playback) und stellt bei `mpv_terminate_destroy()` ffmpegs
    *Default*-Callback wieder her — nicht den der Anwendung.
  - Fix: Callback aus `ttcutmain.cpp` nach `common/ttavlog.{h,cpp}`
    verschoben (`ttInstallAvLogCallback()`); `TTMpvLibBackend` installiert
    ihn nach beiden `mpv_terminate_destroy()`-Stellen neu.
  - Separater zweiter Kanal, vom av_log-Fix unberührt: `Cannot load
    libcuda.so.1` (1× pro Play-Session, schon vor dem ersten Play sichtbar).
    gdb-Beleg: `dlopen("libcuda.so.1")` aus `mpv_render_context_create()` —
    libmpv lädt mit `gpu-hwdec-interop=auto` eifrig ALLE Interops, auch bei
    `hwdec=no`; die Fehlermeldung druckt ffnvcodecs Fallback-`FFNV_LOG_FUNC`
    als nacktes `fprintf(stderr)` (ffmpeg biegt das Makro auf `av_log` um,
    mpv nicht). Fix: `gpu-hwdec-interop=no` wenn `hwdec` auf „no" steht;
    ein `MPV_HWDEC`-Override lässt den Interop-Pfad unangetastet.
  - Abnahme: Mini-Player-Harness aus echten Projektklassen
    (`miniplayer.cpp`, TTMpvWrapper→Backend→RenderWidget, 3 s Playback) —
    stderr vorher 1 cuda-Zeile, nachher leer; GUI-Prüfweg (navigieren →
    Play → navigieren) durch den User bestanden: Terminal durchgehend still.

- **`--auto-cut` beendet sich bei MPEG-2 nicht selbst** → **GEFIXT (2026-08-02,
  `9da00f13`)**
  - Ursache: das Signal wurde auf diesem Pfad schlicht nie gesendet.
    `doH264Cut()` und `doAudioOnlyCut()` laufen synchron und emittieren am
    Ende; der MPEG-2-Schnitt endet dagegen in `TTAVData::onCutFinished()` —
    einem Slot am `exit`-Signal des Thread-Pools —, und die Methode muxte und
    kehrte zurück, ohne `emit cutFinished()`.
  - Zwei Folgen, eine davon bis dahin unbemerkt: `--auto-cut` verbindet
    `cutFinished` mit `QApplication::quit()`, also lief der Schnitt korrekt
    durch und der Prozess blieb danach ewig im Leerlauf stehen. Und
    `TTCutMainWindow::onCutFinished` lief ebenfalls nie — das ist der
    „Cutting Complete"-Dialog: **nach einem interaktiven MPEG-2-Schnitt gab es
    gar keine Abschlussmeldung**, während H.264 eine zeigte.
  - Fix: `emit cutFinished()` am Ende von `onCutFinished()`, dazu vorher
    `setCutVideoName()` auf den tatsächlich geschriebenen `.mkv`-Namen (wie im
    H.264-Pfad), damit der Dialog die richtige Datei nennt. Geprüft, dass sich
    das nicht aufschaukeln kann: der Schnittdialog normalisiert den Namen bei
    jedem Durchlauf über `completeBaseName()` + codec-spezifische ES-Endung.
  - Bewusst **unabhängig vom Mux-Erfolg** emittiert — ein fehlgeschlagener Mux
    darf einen headless-Lauf nicht hängen lassen.
  - **Nachgezogen im selben Zug (2026-08-02):** der H.264-Pfad hatte *drei*
    Fehlerausgänge (Engine-Start, Schnitt, Mux), alle ohne `emit` — derselbe
    Defekt, nur schwerer zu bemerken, weil erst etwas schiefgehen muss. Gemessen
    am selben Fall (Ausgabe in ein nur lesbares Verzeichnis): master hing nach
    30 s immer noch, der Fix beendet sich nach 4 s. Da ein Emittieren im
    Fehlerfall sonst den Abschlussdialog „erfolgreich beendet" melden ließe,
    trägt `TTAVData` jetzt die Fehlerursache (`mLastCutError`, zu Beginn jedes
    Schnitts geleert) und `TTCutMainWindow::onCutFinished` zeigt eine Warnung
    statt der Erfolgsmeldung — dem Vorbild des Audio-Pfads folgend.
  - Belege: Prozess beendet sich nach 4 s selbst (vorher nie), `--auto-cut`-QC
    gegen master bit-identisch (1202 Video-, 2003 Audiopakete). Messfalle auf
    dem Weg dorthin in [[reference_auto_cut_modal_dialogs]]: `timeout` als
    Absicherung täuscht Erfolg vor, es braucht einen Wächter plus Sollwerte.

- **Einmaliger Absturz beim zweiten Landezonen-Analyselauf (2026-08-01)**
  → **GEKLÄRT (2026-08-02), Ursache liegt nicht in TTCut-ng**
  - Ablauf war: MPEG-2 geladen
    (`MPEG2_SD576i25_aspect-switch-4-3-to-16-9_MP2-deu_RTLZWEI.m2v`), Analyse
    gestartet, 4:3-Erkennung aktiviert, Analyse erneut gestartet — das Programm
    verschwand ohne Meldung. Nicht reproduzierbar in zwei `gdb`-Läufen und
    einem ASAN-Lauf.
  - Ursache: **Use-after-free in Qt**, `QtWaylandClient::WlCallback::callback_done`
    (`qwaylandinputdevice.cpp:186`, Qt 5.15.19), aufgerufen über
    `wl_display_dispatch_queue_pending` ← `QWaylandDisplay::flushRequests()`.
    **Kein Frame in TTCut-Code auf dem Absturzstapel**; alle vier
    `setOverrideCursor`-Stellen haben ihr `restoreOverrideCursor`.
  - Beleg für Use-after-free statt Überlauf: der Speicher des Callback-Objekts
    (`_vptr = 0x400000004`, `_M_invoker = 0x1000300010003`) liegt mitten in
    einem ARGB32-Pixelpuffer, der über mehrere KB davor und danach konsistent
    ist (`0x002b5a61` = A/R/G/B). Der freigegebene 64-Byte-Block wurde also neu
    vergeben. Die Bilddaten stammen vom laufenden Scan: Thread 5 stand in
    `data/ttsearchtask_aspectscan.cpp:77`
    (`QImage::convertToFormat(Format_Grayscale8)`), die übrigen 42 Threads
    schliefen im Syscall.
  - **Zwei Irrtümer der damaligen Notiz, beide korrigiert:** (1) „kein
    Core-Abbild trotz `core_pattern=core`" — es lag als `core.291969` im
    Projektwurzelverzeichnis, unsichtbar allein deshalb, weil das Repo
    `status.showUntrackedFiles=no` setzt; `git status` zeigt Cores nicht. (2)
    Der saubere ASAN-Lauf galt als Entlastung für einen Speicherfehler —
    tatsächlich war er folgerichtig, weil ASAN unseren Heap prüft, der
    Fehlgriff aber im Qt-Wayland-Klienten passierte.
  - Vorgehen (ohne sudo reproduzierbar): Build-ID des Cores gegen die des
    Binaries prüfen (`eu-unstrip -n --core=` gegen `readelf -n`), Symbole für
    Systembibliotheken per `DEBUGINFOD_URLS=https://debuginfod.debian.net/` und
    `set debuginfod enabled on` nachladen. Handzuordnung über `nm -D` führt in
    die Irre: die Absturzstelle lag hinter dem Ende des letzten exportierten
    Symbols in einer statischen Funktion.
  - **Auf Nutzerentscheid nicht weiterverfolgt** (2026-08-02); Core gelöscht.

- **Einstellungs-Tab der Landezonen staucht bei knapper Panel-Höhe**
  → **ERLEDIGT (2026-08-01, `1481f1cd`), anders als vorgeschlagen**
  - Der Tab ist weg: die Erkennungseinstellungen sind eine eigene Kategorie im
    Einstellungsdialog, erreichbar über das Symbol neben der
    Landezonen-Überschrift (sie waren immer schon globale `TTSettings`-Felder,
    nie projektbezogen). Damit erledigt sich die Stauchung ebenso wie die
    ursprünglich vorgeschlagene `QScrollArea` — die zielte auf das **Formular**,
    das nicht von selbst scrollt; die verbliebene Liste ist ein `QListView` und
    bringt Scrollbalken mit.
  - **Die Ursache war eine andere als das Symptom nahelegte.** Nicht das Layout
    war falsch: `ttcutmainwindow.ui` deklarierte eine `minimumSize` von 900×700,
    während der Inhalt 1067 verlangte. Qt rechnet die Untergrenze korrekt aus
    und propagiert sie sauber — die feste Angabe überschrieb nur das Ergebnis,
    sodass das Fenster unter seinen Bedarf gezogen werden konnte und Qt die
    Widgets stauchte. Ohne sie erzwingt Qt die echte Untergrenze: **1067 → 863**.
  - Drei Messfallen auf dem Weg: im Konstruktor gemessene Layout-Werte sind
    Zwischenstände (617 statt 1052 — messen per `QTimer::singleShot` nach dem
    ersten `show()`); `minimumSizeHint` ist gecacht (ohne
    `layout()->invalidate()` auf der ganzen Kette liest man veraltete Werte);
    und `QT_QPA_PLATFORM=offscreen` meldet fest 800×600 (nicht konfigurierbar)
    und schneidet jede Beschriftung ab — für Referenzbilder
    `tools/ttcut-screenshots.sh` oder `xvfb-run -s "-screen 0 2560x1440x24"`.

- **Schnittdialog: Button-Leiste überarbeiten + alle Dialoge auf einheitliches Design prüfen**
  → **DONE (2026-07-25, branch `feature/dialog-button-consistency`, GUI-verifiziert)**
  - **Schnittdialog** (`ui/avcutdialog.ui`): Reihenfolge auf `[Reset] ⎯ [Abbrechen] [Starten]`
    (Reset links abgesetzt via neuem Spacer), `okButton` `default=true`/`autoDefault=true`,
    Reset+Cancel `autoDefault=false`. Keine Signal/Slot-Änderung. Konstruktor setzte vorher
    keinen Default.
  - **Vorschau** (`ui/previewwidget.ui`): Transport-Leiste, kein OK/Cancel-Fall. `pbPlay`
    (Start) als Enter-Default; Back/Forward/Close + dynamischer BurstShift `autoDefault=false`.
  - **About** (`ui/aboutdlg.ui`): einziger Button rechtsbündig (zweiter Spacer entfernt),
    `okButton` explizit Default.
  - **Einstellungen** (`ui/ttsettingsdialog.ui`): **bewusst abweichend** — persistiert sofort
    (`accept()` → `saveTabData()`, kein Apply). OK→**Speichern** umbenannt (Label nennt die
    Aktion), und **kein** Enter-Default: eine `findChildren<QPushButton*>`-Schleife im
    Konstruktor setzt alle Buttons `autoDefault=false`, sonst würde Qt einen Tab-eigenen
    „Reset"-Button zum Default machen. Enter inert, Escape verwirft (QDialog-Standard),
    Speichern nur per Klick. Deutsche Übersetzung „Speichern" ergänzt.
  - **QuickJump** bewusst ausgelassen: dynamische Back/Forward-Navigationsleiste mit
    Thumbnail-Auswahl, kein sinnvoller primärer Enter-Default (strukturell wie Vorschau).
  - Zwei begründete Muster statt strikter Uniformität: Aktion-nicht-sofort-persistent
    (Schnitt/About) → primäre Aktion = Enter-Default; sofort-persistent+feldreich
    (Einstellungen) → kein Enter-Default.

- Display the resulting stream lengths after cut → **DONE (2026-07-25)**
  - `TTAVData::computeCutLengths()` speichert Quell- und Ergebnisdauer
    (`mLastCutSourceMs` = `at(0)`-Video, `mLastCutResultMs` = Σ Segmentlängen)
    einmal pro Schnitt; `onCutFinished` zeigt `Quelle → Ergebnis (entfernt)` in
    beiden Abschlussdialogen (Video und Audio-only).
  - Nebeneffekt (vom Schluss-Review gefunden): die MPEG-2-Kapitelmarken bei
    „Auswahl schneiden" summierten vorher die volle Projekt-Schnittliste und
    konnten hinter das Dateiende reichen — jetzt korrekt die tatsächlich
    geschnittene Liste.
  - Spec: `docs/superpowers/specs/2026-07-25-cut-result-length-display-design.md` (lokal)

- Make the current frame position clickable (enter current frame position)
  → **DONE (2026-07-25/26)**
  - Klick auf die Positionsanzeige öffnet `TTGotoFrameDialog`
    (`gui/ttgotoframedialog.{h,cpp}`) mit zwei synchronen Feldern: Frame
    (`QSpinBox`, klemmt auf `0..frameCount-1`) und Timecode (`[hh:]mm:ss[.zzz]`,
    akzeptiert auch Dezimalkomma). OK springt über das bestehende
    `onGotoFrame()`.
  - Geste bewusst Einfachklick statt Doppelklick (User-Entscheid): das Label
    zeigt den Hand-Cursor, der überall Einfachklick signalisiert.
  - Spec: `docs/superpowers/specs/2026-07-25-goto-frame-position-design.md` (lokal)

- Prepare long term processes for user cancellation (abort button) → **DONE**
  - `TTProgressBar` hat Cancel-Button → `TTAVData::onUserAbortRequest()` → `TTThreadTaskPool`;
    Cut-, Search- und QuickJump-Tasks werfen `TTAbortException` bei `onUserAbort()`

- **FastForward-Player-Feature** → **DONE** (TTMpv-Wrapper-Refactor)
  - Geschwindigkeits-Stufen −4×…1×…4× via mpv `speed`/`play-dir`, ◀◀/▶▶-Buttons +
    Tempo-Label im "Aktueller Frame"-Widget. Verwaistes `playSkipFrames`-Setting entfernt.

- **Wayland: Ursache für `QT_QPA_PLATFORM=xcb`-Zwang ermitteln** → **DONE** (v0.71.0, libmpv-Render-Backend)
  - Root Cause war das mpv-`--wid`-Embedding des alten Process-Backends. Mit dem
    libmpv-in-process-Render-Backend (vo=libmpv, `TTMpvRenderWidget` als
    `QOpenGLWidget`) entfällt das Fremdfenster-Embedding; TTCut-ng läuft nativ
    unter Wayland ohne `QT_QPA_PLATFORM=xcb`.

- **Live-Timecode bei mpv-Wiedergabe** → **DONE** (TTMpv-Wrapper-Refactor)
  - `TTMpvWrapper::positionChanged` (aus `observeProperty("time-pos")`) → der Timecode
    im "Aktueller Frame"-Widget läuft während der mpv-Wiedergabe live mit.

### Audio

- **Burst-Erkennung: behobene Defekte** → **DONE** (Detailtexte 2026-08-23 aus
  `docs/code-map/burst-detection.md` hierher verschoben — die Map führt nur noch
  den aktuellen Zustand)

  - **Absoluter statt kontextrelativer Filter** (`48cf828`, empirisch belegt
    2026-07-04): Der frühere ABSOLUTE Filter (Default −30) verwarf reale
    DVB-Bursts (−37,5/−36,5/−27,3 dB bei −79…−87 dB Kontext = 50-dB-Sprung),
    die Skala war kontraintuitiv (−1 = unempfindlichste Stellung), und es gab
    keinen Listen-Refresh bei Threshold-Änderung. Alle drei durch
    kontextrelativen Filter + `refreshBurstIcons` ersetzt; alter Key
    `BurstThresholdDb/` im Orphan-Cleanup.

  - **acmod-Zusatz ging nach dem Settings-Dialog verloren** (`666ed08`):
    `refreshBurstIcons()` rief `updateAcmodIcon` nicht → der acmod-Zusatz in
    Spalte 5 verschwand nach jedem Schließen des Settings-Dialogs (auch bei
    „Abbrechen"), bis der Cut neu angelegt/aktualisiert wurde. GUI-verifiziert
    mit einem Cut über beide acmod-Wechsel von `TEST_deu.ac3` (2075/15624),
    bewusst ohne Burst an den Grenzen: Spalte 5 trug nur „AC3 start+end" und
    wurde komplett leer. Behoben durch `updateHintColumn()` als einzigen Eingang.

  - **Doppelte Relativschwelle** (`a7d1c0e`): `applyBurstDeltaFilter` prüfte die
    Relativschwelle ein zweites Mal, die der Detektor bereits hartcodiert
    (20 dB) enthielt — ein Filter kann nur abweisen, also war jeder Wert < 20
    wirkungslos. Schwelle seither Parameter im Detektor, der Nachfilter entfiel.

  - **Burst-Darstellung im Preview-Dialog dreifach offen codiert**
    (`80a8460f`/`7310407d`): die Beschriftung des Shift-Knopfs stand an drei
    Stellen, sein Icon dagegen nur im Konstruktor — ein Cut-**In**-Burst zeigte
    deshalb „1 Frame" neben einem **Links**-Pfeil. Ebenso stand derselbe
    Stylesheet-String dreimal, plus ein Farb-Reset, der nur eine frühere grüne
    Meldung rückgängig machte. Seither besitzt `configureBurstShiftButton(isCutOut)`
    Beschriftung, Icon und Tooltip.

  - **Final-Warndialog doppelt** (`27f8f29`): audio-only-Pfad und Normalpfad
    waren nahezu identisch; beide seither in `confirmBurstWarnings()`
    konsolidiert, mit GUI/headless-Verzweigung über `mNonInteractive`.

  - **Tote Felder `AcmodInfo::cutInChangeTime`/`cutOutChangeTime`** (2026-07-12,
    `f4d4e66`): nie berechnet, nirgends gelesen — auf User-Entscheid ersatzlos
    entfernt, denn für den Anwendungsfall zählt nur, ob am Schnittpunkt ein
    Burst liegt, nicht wo der Formatwechsel ist. Falls die Angabe „Distanz des
    Formatwechsels zur Schnittgrenze" je gewünscht wird: über die
    `TTAudioHeaderList` ohne File-I/O berechenbar (das war die ursprüngliche
    Idee hinter den Feldern).

- **Manual audio delay/offset per track** → **DONE** (v0.66.0)

- **Schnittliste "Audio-Versatz" Spalte überarbeiten** → **DONE** (v0.66.0)
  - Zeigt seither den aufgelaufenen Versatz an den Audio-Frame-Grenzen je
    Schnitt, berechnet während der Vorschau. Spaltenbezeichnung in der
    Oberfläche: „Audio-Drift".

- **Audio-Drift Minimierung durch optimierte Rundungsstrategie** → **DONE**
  - `TTAVData::planAudioCut()` in `data/ttavdata.cpp` snappt pro Segment auf
    Audio-Frame-Grenzen mit Feed-Forward-Kompensation des akkumulierten Drift
  - Drift bleibt steady-state ±½ Audio-Frame statt monoton zu wachsen
  - Alle drei Sites (MPEG-2 final, H.264 final, Preview) und Drift-Anzeige
    nutzen denselben Plan
  - Tote Funktionen `getStartIndex`/`getEndIndex` und `TTCutAudioTask` entfernt

- **Tonanomalie-Erkennung und -Reparatur (AC3 5.1)** → **DONE (2026-08-19,
  branch `feature/audio-anomaly-repair`)**. Spec:
  `docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md`.
  Automatischer Hintergrund-Scan nach CRC-gültigen Zuspielfehlern
  (Center-Burst + LFE-Puls in sonst LFE-stillem Material), Reparatur-Dialog
  mit Vorher/Nachher-Probehören, zerstörungsfreie Anwendung im Schnittpfad
  über eine Ersatzframe-Tabelle (`extern/ttaudiorepair.{h,cpp}`) und
  `.ttcut`-Persistenz (`<Repair>`-Element, Lade-Validierung deaktiviert statt
  löscht Einträge hinter dem Dateiende).
  - **Kalibrier-Ergebnis** (Prototyp-Lauf über den 5.1-AC3-Korpus, Referenzfall
    ProSieben 02x06 „Das Vermächtnis"): Schwellen LFE-RMS −55 dBFS,
    Kontrastfaktor 4,0 (Center-Diff-Sprung gegen lokalen Median),
    LFE-Nullanteil-Vorbedingung 99 %, Mindest-Peak −22 dBFS. Am Korpusfall
    gefunden: beide user-bestätigten Defekte (Video-Frame ~51120
    Center-Burst/LFE-Puls, ~24221 Abspann-Knacks) plus 1 Fehlalarm.
    Feinsegmentierung meldet 8 AC3-Frames (256 ms) statt der 39 Frames
    (1,2 s) der rohen LFE-Aktivinsel — sie schneidet den unhörbaren
    Abklingschwanz heraus, siehe nächster Punkt.
  - **Korrigierte Falschmessung:** die ursprünglich notierten „1,2 s
    Störung" waren der unhörbare LFE-Abklingschwanz bis rund −90 dBFS
    (Mess-Schwelle beim ersten Prototyp lag bei 1e-6 ≈ −120 dBFS und zählte
    ihn mit); User-Verifikation in Audacity ergab eine echte Störungsdauer
    von ~110 ms. Die Feinsegmentierung (Komponente 1, Schritt 4 der Spec)
    wurde genau deshalb ergänzt: sie markiert die tatsächlichen
    Burst-Blöcke (Center-Diff-Kontrast ODER LFE über Lautschwelle, ±1
    AC3-Frame Marge), nicht die gesamte LFE-Aktivinsel.
  - **Messfalle AC3-Overlap-Verzögerung** (Task-4-Review-Erkenntnis): der
    AC3-Decoder überlappt aufeinanderfolgende MDCT-Blöcke (256 Samples,
    ~5,3 ms bei 48 kHz); eine Messung direkt am dekodierten PCM ohne diesen
    Versatz zu berücksichtigen, sieht am Rand eines Ersatzbereichs den
    Einblend-Fade der Nachbarframes und hält ihn für ein Leck der
    Reparatur in eine unberührte Spur — genau dieser Fehler passierte
    einmal während der Entwicklung. Am Ende des E2E-Gates (unten) sichtbar:
    die AC3-Bytes sind außerhalb des Reparaturbereichs exakt identisch,
    das dekodierte PCM im allerersten Frame danach zeigt trotzdem eine
    Differenz von <0,1 dB — Überlappungseffekt aus dem geänderten
    Vorgängerframe, keine Content-Abweichung.
  - **Ende-zu-Ende-Gate** (`--auto-cut`, isoliertes `XDG_CONFIG_HOME`,
    Projekt `02x06_-_Das_Vermächtnis.ttcut`, Cut 0 = Video-Frame
    24240–51244, Repair-Eintrag AC3-Frames 63894–63901/Channels
    12(C+LFE)/silence-fade): Ausgabe-AC3 exakt 8 Frames Differenz zum
    Referenzlauf ohne `<Repair>` (Paket-MD5 über alle 77987 Frames,
    Ausgabe-Frames 33594–33601 — passt zur Eingabe-Frame-Zahl 8), alle
    übrigen 77979 Frames MD5-identisch. Im Reparaturfenster: LFE-Peak
    −8,88 dBFS → −93,04 dBFS (RMS −25,91 → −116,57 dBFS, praktisch
    digital null), Center-Burst-Peak −2,76 dBFS → −22,25 dBFS (RMS −21,06
    → −46,25 dBFS). Der Restwert von −22,25 dBFS Peak / −46,25 dBFS RMS ist
    NICHT unvollständige Maskierung, sondern die 5-ms-Raised-Cosine-
    Ausblendung am Fensteranfang (voller Pegel bis 0 über die ersten 240
    Samples bei 48 kHz, siehe `applyMaskAndFade` in
    `extern/ttaudiorepair.cpp`) — die über das ganze Fenster gemittelte
    Peak/RMS-Messung zieht diesen kurzen Übergang mit rein. Unabhängig
    nachgemessen: der echte Burst-Peak (Sample-Offset 9652 im Fenster, außerhalb
    der Fade-Flanke) liegt im reparierten Ergebnis bei ~1e-6 ≈ −120 dBFS.
    Segmentnaht unverändert (im MD5-Vergleich enthalten,
    keine weitere Abweichung außerhalb des Reparaturfensters).
  - Tests: `tools/diag/test_audiorepair_persist` (Rundlauf, Reorder-/
    Remove-Regression, Lade-Validierung — Fälle 6 und 7), Scanner- und
    Ersatzframe-Harnesses aus den Vortasks.
  - **Final-Review-Welle (2026-08-20)** — die Fassung, die tatsächlich
    benutzbar ist:
    - **Dialog schloss sich beim ersten „Play"** (Critical): das
      `TTMpvRenderWidget` (ein `QOpenGLWidget`) wurde erst beim Klick ins
      Layout gehängt; Qt erzeugt dafür das native Fenster neu und ruft intern
      `hide()`, und `QDialogPrivate::hide_helper()` beendet die laufende
      `exec()`-Schleife bei JEDEM `hide()`. Ergebnis: `exec()` kehrte mitten
      im Klick mit Rejected zurück, das Stack-Objekt wurde zerstört, während
      mpv lud. Behoben, indem der Player wie in `TTCutPreview` im Konstruktor
      gebaut wird — außer unter `QT_QPA_PLATFORM=offscreen`, wo mpv keinen
      GL-Kontext bekommt (der Grund für die ursprüngliche Faulheit bleibt so
      erhalten). Beleg: `tools/diag/test_repairdialog_mpv_lifecycle` fährt
      jetzt `dlg.exec()` (vorher `show()` + `QApplication::exec()`, was das
      Problem umging statt es zu zeigen) und schlägt fehl, wenn `exec()` vor
      dem dritten Play-Klick zurückkehrt.
    - **Auslösung wie im Brainstorming entschieden**: der Scan hing nur an
      der Landezonen-Analyse, obwohl Spec und CHANGELOG „automatisch nach dem
      Laden" sagten. Jetzt startet er automatisch, sobald Video und Tonspuren
      geladen sind (`TTCutMainWindow::maybeStartAutoAnomalyScan()`, über
      `onAVDataReloaded()` mit Null-Timer). Messfalle dabei: beim
      **Projekt**-Laden feuert der Pool-Exit `avDataReloaded()` VOR
      `TTAVData::onReadProjectFileFinished()`, das die gespeicherten Marker
      zurückholt — der Scan lief also los, bevor die Marker da waren, und
      hätte sie verdoppelt (im Log nachgemessen, nicht vermutet). Deshalb die
      Sperre `mProjectLoadInProgress` plus Nachlauf in
      `onOpenProjectFileFinished()`.
    - **Schwellen im GUI**: die fünf Werte stehen jetzt in der
      Landezonen-Einstellungsseite (`gui/ttcutsettingsstreampoints.cpp`,
      inkl. `resetToDefaults()`), vorher waren es reine QSettings-Schlüssel.
    - **Frame-Genauigkeit im Rundlauf**: `TTStreamPoint` trägt den
      AC3-Bereich des Fundes direkt (`audioFrameFrom()`/`audioFrameTo()`,
      beide inklusiv, optional auch in der `.ttcut`-Datei). Vorher rechnete
      der Dialog aus Videoframe + Dauer zurück — drei Quantisierungen
      (40 ms-Videoraster gegen 32 ms-Tonraster, Dauer exklusiv gegen
      `frameTo()` inklusiv, Dauer auf 2 Nachkommastellen gerundet) ergaben bis
      zu ±1 AC3-Frame. Konvention in beiden Headern festgeschrieben; der
      Rundlauf-Testfall in `test_repairdialog_model` prüft exakt (und den
      Altprojekt-Weg auf ±1).
    - **Toter Regressionstest** (I1): der acmod-Wechsel-Fall hing an einer
      Korpusdatei, die es nicht mehr gibt ⇒ SKIP, und das Programm meldete
      trotzdem ALL PASS. Fixture jetzt synthetisch
      (`tools/diag/make_acmod_change_sample.sh`, 5.1- und 2.0-Abschnitt bei
      GLEICHER Bitrate, damit die CBR-Prüfung nicht vorher zuschlägt), und ein
      SKIP kann sich nicht mehr als Erfolg tarnen (Exit 3, „NOT VERIFIED").
    - Kleinkram mit Belegen: EOF-Bereich wird nicht mehr als
      „implementation bug" gemeldet; Lade-Validierung fängt das abgeschnittene
      letzte Frame (`(frameTo+1)*frameBytes > size`) sowie negative/verdrehte
      Bereiche; Abtastrate ≠ 48 kHz bricht den Scan ab statt falsch zu
      rechnen; die Segmentgrenzen-Meldung erreicht über
      `TTAVData::audioCutFailureReasons()` den Nutzer; vorbestehender
      Linkfehler von `test_startcode_scan` (fehlendes `ttexception.cpp`)
      behoben, damit `cmake --build build --target diag` überhaupt durchläuft.

### Projektdatei

- **Projektdatei-Endung: .prj → .ttcut** → **DONE** (v0.63.0)
  - Neue Dateien: `.ttcut`, bestehende `.prj` behalten Endung
  - File-Dialog Filter: `"TTCut Project (*.ttcut);;Legacy Project (*.prj)"`

- **Projektdatei: Fehlende Einstellungen speichern** → **DONE** (v0.66.0)
  - Ausgabepfad, Dateiname, Suffix-Option, Mux-Settings, Encoder-Settings werden
    jetzt in `<Settings>`-Sektion der `.ttcut` Datei gespeichert
  - Beim Laden: Override der TTCut-Globals, beim Schließen: Restore aus QSettings
  - Codec-spezifisches Encoder-Mapping basierend auf Video-Typ
  - Rückwärtskompatibel: alte .ttcut Dateien ohne Settings-Sektion laden normal

- **Dirty-Tracking: "Neues Projekt" Warnung nur bei echten Änderungen** → Completed (v0.62.1)

### Werkzeuge und Infrastruktur

- **Voller TODO-Abgleich nachgeholt** → **ERLEDIGT (2026-08-24, Release
  v0.82.2)**. Beim Release v0.82.1 war der Abgleich nach Skill-Step 4.5 auf
  die von jenem Release berührten Bereiche eingegrenzt worden
  (`ttcut-demux`, Lückenerkennung, Fortschritt) statt auf alle 45 Einträge.
  Die Begründung deckte nicht ab, was durch **frühere** Refactors
  miterledigt wurde, ohne dass es jemand nachtrug.
  Nachgeholt: alle 45 Einträge einzeln gegen den Code geprüft (nicht gegen
  `git log`, weil ein Eintrag auch durch einen alten Refactor erledigt sein
  kann). Ergebnis: 1 erledigt, 9 teilweise, 33 offen, 2 veraltete
  Referenzen. Vollständiger Bericht:
  `docs/superpowers/release-v0.82.2/todo-abgleich-v0.82.2.md` (gitignored,
  aber auf gesicherter Platte — CLAUDE_TMP ist es nicht).
  Die zwei veralteten Referenzen waren: der MP3/AAC-Warntext sitzt seit dem
  Task-Pool-Umbau in `data/ttaudioonlycuttask.cpp` statt in
  `TTAVData::doAudioOnlyCut`, und der PTS-Umlauf-Eintrag beschrieb
  `detect_segment_boundaries` weiter im Präsens, obwohl der
  Störzonen-Umbau die Funktion entfernt hatte.

- **Dead-Code-Audit — zwei Läufe** → **Erstlauf 2026-07-12, zweiter Lauf
  2026-08-02 (v0.78.0)**
  - **Erstlauf** (Branch `cleanup/dead-code-audit`): ~2.185 Zeilen in den
    Batches A–K entfernt, Runde-2/3-Rescan bis Konvergenz, `--auto-cut`-QC
    bit-identisch zu master (161.844 Pakete). Beispiel-Altfund: `TTCutAudioTask`
    stand nach der v0.60.0-libav-Migration vom 2026-02-21 noch rund zwei Monate,
    bis `f2c4412` am 2026-04-25.
  - **Zweiter Lauf** (Branch `cleanup/dead-code-audit-2026-08`, gemergt
    `99fddc6c`): 677 Zeilen und 16 Bilddateien in neun einzeln geprüften
    Schritten. Größter Fund `TTProcessForm` — in jeden Build kompiliert, nie
    instanziiert, Zeiger dauerhaft null, zwei von drei Methoden auf
    Doc-Kommentare geschrumpft; der Linker verwarf sogar das moc-Metaobjekt.
    Dazu sechs nie geworfene Exception-Klassen, `TTH264PPS`/`TTH265PPS`, drei
    vom Linker verworfene Methoden, 13 Ressourceneinträge und zwei Includes
    (einer davon erst in Runde 2 transitiv tot geworden).
  - **Zwei Werkzeug-Funde wogen schwerer als die Zeilen.**
    (1) `avstream/ttac3audioheader.h` war ISO-8859 kodiert — ein einziges `ü`
    in einem Kommentar —, weshalb `grep` die Datei als binär überspringt und
    **jedes „null Referenzen"-Urteil zunächst über einem Baum ohne diese Datei
    fiel**. Aufgefallen erst, als eine Klasse „nirgends definiert" schien,
    obwohl das Projekt sie baut. Seither UTF-8 (`a2705aba`), einzige
    Nicht-UTF-8-Quelle im Projekt.
    (2) Der eingelagerte ffmpeg-Quellbaum im Projektwurzelverzeichnis lieferte
    699 der 780 Rohkandidaten; er liegt jetzt unter `/usr/local/src/`.
  - **Falsch-Positive, die keine sind:** `tools/diag/*` hat ein eigenes
    Build-Target (`make diag`) und ist damit lebendig — ebenso Methoden, die nur
    von dort aufgerufen werden. Basisklassen kann der Linker nicht verwerfen,
    ein `gc-sections`-Treffer auf eine *virtuelle* Methode beweist also nichts.
  - **Prüfverfahren:** jeder Batch mit gewischtem Build-Verzeichnis gebaut
    (`make clean` genügt nicht — eine überlebende `obj/*.o` kann ein
    falsch-grünes Binary linken), Scanner bis zur Konvergenz (Runde 3 ohne neue
    Funde), Abschluss-Gate `--auto-cut` paketweise bit-identisch zu master
    (1202 Video-, 2003 Audiopakete). Wiederkehrender Lauf: Skill
    `dead-code-audit`.
  - Bereits im Erstlauf miterledigt: veraltete Doc-Kommentare zu `isBlackAt`
    (`73acdf0`), die verwaiste statische `histogramDifference`-Kopie in
    `ttmpeg2window2.cpp` (`17b2ca99`, v0.75.0) und die toten
    `AcmodInfo::cutInChangeTime`/`cutOutChangeTime` (`f4d4e66`; Umsetzungsweg
    oben unter „Burst-Erkennung: behobene Defekte“). **Offen geblieben** ist
    allein die doppelte Mehrheits-acmod-Logik — steht in `TODO.md`.

- **Security Audit Findings beheben** → **25/25 FIXED** (2026-03-28, commits aea1809 + 66eacb2)
  - Siehe [docs/security-audit-2026-03-02.md](docs/security-audit-2026-03-02.md) für alle Findings

- **Code-Review-Follow-ups Redundanz-Branch** → **DONE (2026-07-25,
  branch `refactor/code-review-followups`)**
  - ~~Alle-Spuren-Überladung für `cutAudioTracks`~~ → `4c56d9d9`: Convenience-
    Überladung baut die All-Tracks-Liste und leitet weiter; Boilerplate an 3
    Stellen (doMpeg2Cut/doH264Cut/doAudioOnlyCut) entfernt. Verhaltensneutral.
  - ~~`outPath`-Lambda-Seiteneffekte (Datei-Löschen, statusReport)~~ → `6026f0ab`:
    `outPath` ist überall reine Pfad-Funktion; die Stale-Output-Löschung ist im
    Helfer zentralisiert (einheitlich mit Warn-Log), Fortschritt via neuem
    optionalem `beforeCut`-Hook. Audio-Ausgabe unverändert, nur Diagnose-Logs
    vereinheitlicht (nicht bit-identisch — bewusste Vereinheitlichung).
  - ~~`tools/diag/Makefile` Glob gegen alle obj/*.o~~ → `202160b7`: Kopplung
    `TTMpeg2VideoStream`↔GUI/mpv ist **nicht real** (per Archiv-Link-Map belegt:
    22 interne Objekte, 0 GUI/mpv). Glob durch kuratierte Liste ersetzt,
    Qt5Widgets/Xml/Network/OpenGL/mpv aus den pkg-Libs entfernt; neue GUI/mpv-
    Abhängigkeit im Cut-Pfad bricht jetzt hier den Link statt still absorbiert.

- **Systemanforderungen dokumentieren** → **DONE (2026-07-31, `7f88d484` +
  Wiki `9b3d915`, v0.77.0)**
  - README und `Installation.md` haben jetzt eine Anforderungstabelle
    (Betriebssystem, Architektur, Qt, libav, libmpeg2, Grafik) plus einen
    Absatz zum Plattenplatz.
  - Dabei zwei echte Fehler gefunden, keine reine Fleißarbeit: in der
    Installationszeile fehlten `libqt5opengl5-dev` (`QT += opengl`) und
    `libmpv-dev` (`PKGCONFIG += mpv`) — ohne die bricht schon `qmake` ab;
    `debian/control` führte beide korrekt, nur die Doku hing hinterher. libmpv
    stand außerdem noch unter „optional: Video-Vorschau", obwohl es seit
    v0.71.0 fest gelinkt ist. Raus flog die Behauptung, Wayland brauche
    `QT_QPA_PLATFORM=xcb`.
  - **Der TODO-Vorgabe „Architektur (x86_64)" wurde bewusst nicht gefolgt:**
    `debian/control` deklariert `linux-any`, und im Quelltext gibt es weder
    Intrinsics noch `__x86_64__`-Zweige. Dokumentiert ist daher „getestet wird
    auf x86_64", nicht „erfordert". Die libav-Untergrenze 5.1 hängt an
    `AVChannelLayout`.
  - **Nicht gemessen, sondern hergeleitet:** die Platzangabe (Faustregel
    Zweieinhalbfaches der Aufnahme) folgt aus der Pipeline —
    `createTempMkvForPlayback` übergibt die vollen Dateipfade, muxt also den
    ganzen Strom, keinen Ausschnitt. Eine echte Speichermessung an großem
    Material steht aus.

- **ttcut-ng-Kommandozeilenoptionen im Wiki dokumentieren** → **DONE** (Quickstart.md, 2026-06-03)
  - Nutzerrelevante Optionen (`<datei>`, `--project`, `--help`) als Abschnitt
    in `Quickstart.md` dokumentiert; Entwickler-/QC-Flags (`--screenshots`,
    `--auto-cut`) bewusst als intern gekennzeichnet. Erster Fund des neuen
    `wiki-audit`-Skills.

- **Fresh-Open-Dialoge ohne `mNonInteractive`-Guard** → **KEIN DEFEKT
  (nachgemessen 2026-07-31)**
  - Behauptung war: `showExtraFrameClusterDialog` (`data/ttavdata.cpp:724`)
    ruft `msgBox.exec()` ungeschützt und blockiert damit einen headless
    Fresh-Open. Beim Nachsehen fand sich ein **zweiter** ungeschützter Dialog
    direkt in `openAVStreams` (`:457`, „Import as Stream Points").
  - Beide hängen am Fresh-Open, wenn auch verschieden: der Regionen-Dialog
    steht in `openAVStreams` selbst, der Cluster-Dialog läuft erst in
    `onOpenVideoFinished` (`:749`) — der Frame-Index existiert vorher nicht —
    und nur für Items, die `openAVStreams` in `mpPendingExtraFrameDialog`
    markiert hat (`:422`), weshalb ein Projekt-Reload stumm bleibt.
  - Der Guard wäre trotzdem wirkungslos: `openAVStreams` hat genau einen
    Aufrufer (`onReadVideoStream`, `gui/ttcutmainwindow.cpp:705`), erreichbar
    aus dem Menü und über ein positionales CLI-Argument
    (`gui/ttcutmain.cpp:222`). `setNonInteractive(true)` steht genau einmal im
    Baum (`runAutoCutMode`, `ttcutmainwindow.cpp:1424`), und dieser Modus
    verlangt `--project` (`ttcutmain.cpp:189`), lädt also über
    `openProjectFile`. Auf dem Pfad mit den Dialogen ist `mNonInteractive`
    damit immer `false`. `--screenshots` setzt das Flag gar nicht, lädt aber
    ebenfalls über ein Projekt.
  - Ein headless Fresh-Open müsste erst gebaut werden. Der naheliegende Weg —
    `--auto-cut` ohne `--project`, mit positionaler Videodatei — ist am
    2026-07-31 als sinnlos verworfen worden. Ohne diesen Modus gibt es nichts
    zu schützen; kämen die Dialoge je auf einen headless Pfad, wären **beide**
    zu behandeln, beim Regionen-Dialog zusätzlich mit der Festlegung, welcher
    Zweig ohne Nutzer gilt.
  - Herkunft: Task 6 des `demux-defect-repair`-Feature-Ledgers
    (`docs/superpowers/sdd/progress.md`).

- **qmake → CMake-Migration (Ninja)** → **DONE (2026-08-02, branch
  `feature/cmake-migration`)**
  - Bausystem vollständig auf CMake (Ninja-Backend) umgestellt:
    `qmake ttcut-ng.pro && make` → `cmake -B build -G Ninja && cmake --build
    build`, Binary jetzt `build/ttcut-ng`. Alle qmake-Bauartefakte (`.pro`,
    `moc/`, `ui_h/`, `obj/`, `res/`) entfernt; `compile_commands.json`
    entsteht ab jetzt automatisch bei jedem Konfigurationslauf (kein `bear`
    mehr nötig).
  - Qt6-Veraltungs-Gate `QT_DISABLE_DEPRECATED_BEFORE=0x060000` scharf
    geschaltet, **null Codeänderungen nötig** — der Qt5-Build kompiliert
    unverändert durch. Schließt Schritt 2 („Veraltungs-Gate im Qt5-Build
    einschalten") des Qt6-Migrationsplans in `TODO.md`.
  - `tools/diag/`: die pro-Test kuratierten Quelllisten aus dem alten
    Makefile wurden 1:1 in CMake-Ziele übernommen (bewusst keine gemeinsame
    Archiv-Library — jede neue GUI/mpv-Abhängigkeit im Cut-Pfad soll als
    Linker-Fehler auffallen, nicht still durchgereicht werden). Die Guard-
    Absicht bewährte sich dabei einmal real: `common/ttthreadtask.h` erreichte
    zuvor `gui/ttprogressbar.h` → `QDialog` → `ui_ttprogressform.h`; dieser
    Include-Pfad existiert inzwischen nicht mehr, wodurch die bisherigen
    Handbau-Kommentare in `tools/diag/test_task_cleanup_order.cpp` und
    `tools/diag/test_pool_crossthread.cpp` eine falsche Begründung nannten
    (korrigiert im selben Zug wie die übrigen toten Pfade `-I../../moc`).
  - QC-Beleg: A/B byte-identisch für ES + Audio (H.264-Tux-Testvideo,
    395-Paket-PTS/DTS-Liste identisch vor/nach der Migration).
  - Debian-Paket gebaut und geprüft (`build-package.sh` → `dpkg-buildpackage`).
  - Belege: `.superpowers/sdd/2026-08-02-cmake-migration/` (Task-Briefs/
    -Reports, Review-Diffs), `final-fix-report.md` für die abschließende
    Fix-Welle.

- **Qt5 → Qt6-Migration** → **DONE (2026-08-04, branch
  `feature/qt6-migration`)**
  - **Ausgangslage (gemessen 2026-08-02, 210 Quelldateien + 26 `.ui`):** keiner
    der üblichen Qt6-Blocker im Code (kein `QRegExp`, `QTextCodec`,
    `QDesktopWidget`, `QGLWidget`, `QLinkedList`/`QStringRef`, `setMargin()`,
    `toSet()`/`fromList()`, `qSort`, keine High-DPI-Attribute). C++17 stand
    bereits in `CMakeLists.txt`.
  - **Schritt 1 — Rückfallpunkt:** Tag `qt5-final` auf dem letzten Qt5-Stand
    (v0.79.0) gesetzt und gepusht, bevor der erste Qt6-Commit fiel.
  - **Schritt 2 — Risiko libmpv-Render-Backend zuerst geprüft:** in der
    Machbarkeitsprobe (2026-08-03) lief die Wiedergabe im Qt6-Probe-Binary
    unverändert (User-abgenommen, Wayland nativ) — der
    `QOpenGLWidget`-Umbau in Qt6 verträgt sich mit dem mpv-Render-Kontext.
  - **Schritt 3 — Machbarkeitsprobe:** Configure inkl. AUTOMOC/AUTOUIC aller
    26 `.ui` auf Anhieb grün (Qt 6.10.2, zusätzliche Komponente
    `OpenGLWidgets`). 52 Build-Fehler kollabierten auf drei Einzeiler
    (`QStringList`-Forward-Declaration → `<QtContainerFwd>`,
    `QVariant::type()` → `typeId()`, `QLibraryInfo::location()` → `path()`).
  - **Schritt 4 — Umsetzung, fünf Commits:**
    - `63103ea2` — Cast der codec_id für `av_parser_init` (vorbestehender
      Clean-Build-Bruch mit neuerem libavcodec, unabhängig von Qt6).
    - `6c5a747b` — Umstellung auf Qt6: `find_package(Qt6 6.7 REQUIRED
      COMPONENTS Core Widgets Gui Xml OpenGL OpenGLWidgets)`, `QT +=
      network` entfernt (ungenutzt), die drei Einzeiler aus der
      Machbarkeitsprobe.
    - `b1e65bcf` — Veraltungs-Gate von `0x060000` auf `0x060700` angehoben
      und alles gefixt, was der höhere Gate als Fehler/Warnung markierte:
      4× `QCheckBox::stateChanged` → `checkStateChanged` (Slots auf
      `Qt::CheckState` umgestellt: `ttcutsettingslogging`,
      `ttcutsettingsencoder`, `ttcutsettingsmuxer`, `ttprogressbar`);
      `data/ttavdata.cpp` — der Audio-Burst-Warndialog von der entfernten
      `QMessageBox::warning(parent, title, text, button0Text,
      button1Text)`-Überladung auf die `QMessageBox`-Instanz +
      `addButton(..., role)`-API portiert, mit bewusster
      Verhaltensänderung: Esc schließt den Dialog jetzt als Cancel
      (RejectRole) statt (wie vorher über die alte Überladung, Esc → −1 →
      `ret != 1` → „Cut anyway") den gewarnten Schnitt lautlos zu starten;
      `extern/ttmplexprovider.cpp` — `QFile::open()` ist jetzt
      `[[nodiscard]]`, `writeMuxScript()` prüft das Ergebnis und
      loggt+returned bei Fehlschlag; `QMouseEvent::pos()` →
      `position().toPoint()` (`mpeg2window/ttmpeg2window2.cpp`,
      `gui/ttcurrentframe.cpp`); zusätzlich, außerhalb der ursprünglichen
      Liste, `QLabel::pixmap(Qt::ReturnByValue)` entfernt (Qt6 liefert
      `pixmap()` bereits by value). **Verbleibender Diagnose-Hinweis (als
      G1-Ausnahme akzeptiert, Spec C.5):** GCC-15
      `-Wsfinae-incomplete=1`-Note für `TTAVItem` (`data/ttavlist.h`), eine
      Folge von AUTOMOCs `mocs_compilation.cpp`-Konkatenationsreihenfolge
      (Vorwärtsdeklaration vor der vollständigen Definition in einer
      früheren moc-Übersetzungseinheit) — kein lokales Include-Problem,
      Fix würde CMakeLists.txt-Quelllistenreihenfolge zur Beeinflussung von
      AUTOMOC-Internals ausnutzen, außerhalb des Scopes.
    - `0cef32ba` — Übersetzungen mit den Qt6-`lupdate`/`lrelease`-Werkzeugen
      aufgefrischt.
    - `408586e5` — Debian-Paket auf Qt6 umgestellt (`debian/control`:
      `qtbase5-dev`/`qttools5-dev-tools` → `qt6-base-dev`/`qt6-l10n-tools`;
      `debian/rules` entsprechend angepasst).
  - **QC-Gate: PASS 3/3** (2026-08-04, `qc-qt6.sh`, Log
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/qt6-qc/qc-result-20260804.log` [Material verloren 2026-08-16]) —
    Kandidat (Qt6-Binary) gegen den `qt5-final`-Baseline-Binary, je Codec
    Dauer/Paketzahlen identisch und Video- **und** Audio-Paketlisten
    bit-identisch:
    - H.264: 301 Video-, 94 Audiopakete, BIT-IDENTISCH.
    - MPEG-2: 1202 Video-, 2003 Audiopakete, BIT-IDENTISCH.
    - HEVC: 1162 Video-, 726 Audiopakete, BIT-IDENTISCH.
  - **Warum sich der Sprung über die reine Versionsanhebung hinaus lohnen
    sollte — Ergebnis negativ:** Qt6 spricht `wp_fractional_scale`, Qt5
    nicht; das war die Hypothese zum KWin-Anzeigefehler unter „Known
    Limitations" in `TODO.md`. **Widerlegt in der Machbarkeitsprobe
    (2026-08-03):** das Qt6-Probe-Binary bindet
    `wp_fractional_scale_manager_v1` nachweislich (WAYLAND_DEBUG-Mitschnitt)
    — der Fehler tritt trotzdem auf. Ausgeschlossen ist damit nur die
    Hypothese „Ursache im Qt5-Skalierungspfad"; die Zuordnung KWin vs.
    TTCut-spezifischer Auslöser bleibt offen (weiter unter „Known
    Limitations" in `TODO.md`, nicht Teil dieser Migration).
  - **Nebenbefund aus der Machbarkeitsprobe:** Standbild und mpv-Play-Bild
    stellen 4:3-Material auf dem Qt6-Probe-Binary unterschiedlich dar (Qt5
    zeigt es ebenfalls, nur anders gelagert) — als eigener Low-Priority-Punkt
    in `TODO.md` weitergeführt, kein Migrations-Blocker.
  - **Bauverfahren:** CMake war bereits durch die qmake→CMake-Migration
    (siehe oben) entschieden; die Qt6-Migration hat kein neues Bausystem
    gebraucht, nur `find_package(Qt6 ...)` in der bestehenden
    `CMakeLists.txt`.
  - Belege: `.superpowers/sdd/2026-08-03-qt6-migration/` (Task-Briefs
    /-Reports je Schritt), QC-Werkzeug und -Log unter
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/qt6-qc/` [Material verloren 2026-08-16].

- **Subagent-Driven Development: Build-Permissions für Subagents** → **Konfiguriert 2026-05-19**
  - `.claude/settings.local.json` (lokal, gitignored) erweitert um `Bash(make:*)`,
    `Bash(make clean:*)`, `Bash(qmake:*)`, `Bash(bear -- make:*)`, `Bash(lrelease:*)`.
  - `Bash(bear:*)` und `Bash(lupdate:*)` waren bereits drin.
  - Bei nächstem Subagent-Driven-Run verifizieren ob ausreichend.

## Funktionsverzeichnis

Kurzfassung ausgelieferter Funktionen, neueste zuerst. Ausführlich zu den
Punkten, die oben einen eigenen Abschnitt haben, steht es dort.

- [x] A marker jump lands on the marker itself — `onStreamPointJump` went
      through `onVideoSliderChanged`, whose second argument is a FRAME TYPE,
      not a speed switch, so with FastSlider on (the default) every jump
      searched forward to the next I frame. Marker 7045 landed on 7050, and
      three error markers of one defect all ended up on the same picture.
      Harnesses `tools/diag/test_mpeg2_seek.cpp` (navigation + decoder answer
      per position) and `tools/diag/test_window_jump.cpp` (one layer up: drives
      `TTMPEG2Window2` headless as `TTCurrentFrame::onGotoFrame` does and
      checksums the pixmap the widget actually shows). The second one also
      cleared the widget of the *remaining* "picture stays" report — that turned
      out to be a KWin repaint fault under fractional scaling, not TTCut.
- [x] Aspect markers are reported in display order — `TTStreamPointVideoWorker`
      handed out a `picture_start_code` counter (bitstream order) while
      navigation works with the rank in the display-sorted index list. Measured
      off by +2 on all 626 TELE5 markers and by up to +6 on 268 of 350 Comedy
      Central ones; RTLZWEI was correct only by accident (closed GOPs).
      Computing `base_number + temporal_reference` would be wrong too, because
      field-picture pairs make rank and display_order value diverge — the
      position is now looked up in the sorted list.
      Harness `tools/diag/test_streampoint_order.cpp`
- [x] Task pool: nested tasks no longer touch the queue from a worker thread —
      `TTThreadTaskPool::startNested()` runs a sub-task synchronously with the
      same signal wiring, so `mTaskQueue` stays confined to the pool's own
      thread (TSAN showed data races plus a SEGV on the old path; `start()`
      now asserts the thread). Callers: `TTCutVideoTask`, `TTCutPreviewTask`.
      Harness `tools/diag/test_pool_crossthread.cpp` + `gate_pool_crossthread.sh`
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
- [x] Preview: stay on the last frame at the end; Forward and the cut selector
      start playback, Back is two-stage and always shows a still (start of the
      current clip, then the previous cut)
- [x] Preview: fix the missing mpv render context that left the video frame
      black — initPreview() loaded before exec(), so the render widget had
      never been painted ("No render context set", vo/libmpv broken for the
      whole session). Pre-existing, until now masked by the reload at end of
      playback
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
- [x] Security audit: all 25 findings fixed (v0.63.0)
- [x] German translations (de_DE): all 165 strings, Q_OBJECT standardization (v0.62.0)
- [x] Screenshot automation: `--screenshots` CLI mode with test media generation
- [x] MPEG-2 extra-frame correction for A/V sync and quality-check (v0.63.0)
- [x] Remove redundant F-buttons from navigation widget, add frame-type labels (I, P/I, B/P/I)
- [x] Remove redundant "Set Cut-Out" from cut list context menu, reorder entries
- [x] Logo detection: markad PGM import and manual ROI selection with Sobel edge profiling
- [x] Logo profile persistence in project file (.ttcut)
- [x] Pillarbox detection: 4:3 in 16:9 with 10s hysteresis (all codecs, I-frame analysis)
- [x] Progress dialog for Landezonen analysis
- [x] TTESInfo: parse per-track audio_N_trimmed_ms and first_pts from .info (v0.66.0)
- [x] Fix audio list UI not refreshed after locale-based sorting (v0.66.0)
- [x] Audio language preference list (replaces hardcoded system-locale sort, accepts 2/3-letter codes with alias normalization) (v0.66.0)
- [x] Replace deprecated qSort() with std::sort() in TTSubtitleHeaderList
- [x] Suffix-Checkbox im Cut-Dialog reagiert live auf Toggle (updateOutputFilename slot)
- [x] Remove inactive UI elements: Chapters tabs (spumux-legacy), Configure Muxer button, hidden videoFileInfo widget
- [x] Settings-Dialog Sidebar (7 Kategorien: Navigation, Suche & Preview, Audio & Sprache, Encoder, Multiplexen, Pfade, Logging) — Allgemein-Tab und Files-Tab aufgeteilt (v0.70.0)
- [x] Cut-Dialog 2-Tab Reorg (Schnitt + Encoder), Container-Wahl in gbOutput (v0.70.0)
- [x] Persistent/transient Trennung für Encoder + Mux/Audio: Cut-Dialog überschreibt App-Defaults nicht mehr; working* Variants für 7 Mux/Audio-Settings; .ttcut serialisiert working set (v0.70.0)
- [x] 'Reset to defaults' Buttons in 6 Settings-Tabs + Cut-Dialog (v0.70.0)
