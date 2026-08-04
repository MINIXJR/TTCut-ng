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
    `CLAUDE_TMP/TTCut-ng/eos_nonidr/`.

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
    `CLAUDE_TMP/TTCut-ng/dupcase/REGRESSION-*.md`.
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
    `CLAUDE_TMP/TTCut-ng/fieldrank/MESSUNG.md`). `mExtraIndices` speichert
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
  - Gates (`/usr/local/src/CLAUDE_TMP/TTCut-ng/demuxrepair/gateruns/`,
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
    Protokoll `CLAUDE_TMP/TTCut-ng/demuxfix/REDEMUX.md`.
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
    in `docs/code-map/burst-detection.md` konserviert). **Offen geblieben** ist
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
  `feature/qt6-migration`, HEAD `408586e5`)**
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
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/qt6-qc/qc-result-20260804.log`) —
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
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/qt6-qc/`.

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
