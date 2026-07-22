# TTCut-ng TODO / Feature Requests

## High Priority

- **H.264 gemischt MBAFF+PAFF (08x04-Korpus) — verbleibende Befunde**
  (Wurzel — TS↔ES-AU-Nummerierungs-Drift der es_extra_frames — GELÖST 2026-07-19,
  siehe Spec `docs/superpowers/specs/2026-07-19-es-extras-field-awareness-design.md`):
  - ~~**Befund B — Decode-Hänger** beim Navigieren auf ein PAFF-Feldpaar-AU~~
    → **GEFIXT 2026-07-19** (`46d3dcb`): Index-Adopter erben jetzt den
    PAFF-Zustand des Owners (`adoptStreamMetadata`); Diag `test_adopt_paff`.
    Restpunkte: die Crash-Variante (`core.456277`, SIGABRT in
    `avcodec_send_packet`) ist als Folge des beseitigten EOF-Drains plausibel,
    aber nicht formal bewiesen (GUI-Soak ohne Crash bestanden); PAFF-
    **Playback**-Fehler beim Play (mpv `reference picture missing during
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

- ~~**H.265 Smart Cut: RASL-Verlust an der Non-IDR/CRA-Naht (Defekt A,
  H.265-Teil)**~~ → **DONE (2026-07-21, branch `feature/hevc-seam-rasl`)**
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

- ~~**H.264 Smart Cut: EOS+Non-IDR-Naht beschädigt Leading-Pics des Copy-Start-Keyframes**~~
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

- ~~**ttcut-demux: Löcher aus TS-Korruption / VDR-Signalverlust werden
  weder erkannt noch repariert**~~ → **DONE** (2026-07-18, branch
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

- ~~**ttcut-demux: `repair_audio_with_silence_inserts` bricht bei
  überlappenden Gap-Fenstern ab (Silence-Insert schlägt fehl →
  Fallback-Padding statt echter Reparatur)**~~ → **FIXED** (2026-07-18,
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

- ~~**H.264 Smart Cut: SPS-Unification zerstört progressive Quellen**~~ →
  **FIXED** (2026-07-16, Defekt B) — Slice-Rewriter ließ bei poc_type-2-Encoder
  (progressiv) das von der Quell-SPS verlangte `pic_order_cnt_lsb` weg → alle
  Header ab dort bit-verschoben (real: 495 Decoder-Fehler, 13/1001 Frames weg).
  Feld wird jetzt eingefügt (verankerte POC-Nummerierung). MBAFF-Unification und
  Standard-Branch byte-identisch. Details CHANGELOG (Unreleased) + Karte.

- ~~**MPEG-2: Cut-Out auf B-Frame verliert bis zu M−1 Frames**~~ → **FIXED** (2026-07-12, `3b087ae`)
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

- ~~**MPEG-2-Re-Encoder: Einzelbild-Encode kann beschädigten letzten Slice liefern**~~
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

- ~~**MPEG-2: Cut der letzten Frames der Datei → Segfault**~~
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

- ~~**MPEG-2 field-picture: Cut-Positionen zählen Felder statt Frames**~~ → **HARMLOS, GESCHLOSSEN** (2026-07-16)
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
  - Latent (sub-perzeptuell, nur an Field-Rändern): `mExtraIndices` speichert
    Stream-Ordnung, wird aber gegen Display-Positionen verglichen → ±1–2 Frame
    Ungenauigkeit der Audio-Korrektur. Irrelevant, da im Standard-Workflow leer.

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

- **ttcut-demux: Audio-Padding bricht bei RELATIVEM output_dir ab** (latent,
  gefunden 2026-07-19 bei den es-extras-Gates; kein Fix ohne Abnahme):
  Der concat-Demuxer löst `file`-Einträge relativ zum Verzeichnis der
  Concat-Liste auf. `TEMP_CONCAT` liegt in `$OUTDIR` und listet
  `$OUTDIR/…`-relative Pfade → bei relativem output_dir wird `dir/dir/file`
  gesucht (repro: „Impossible to open '08x04/08x04/…'"), das Padding-Subshell
  stirbt und `set -e` reißt das Skript am `wait` um (exit 254, keine .info).
  Absolute Aufrufe (VDR_Demux.sh-Workflow) sind nicht betroffen.
  Fix-Richtung: Pfade beim Schreiben der Concat-Liste mit `realpath`
  absolutieren (auch die Listen von `repair_audio_with_silence_inserts` prüfen).

- **Fresh-Open: Extra-Frame-Cluster-Dialog blockt headless ohne `mNonInteractive`-Guard**
  - `showExtraFrameClusterDialog` (`data/ttavdata.cpp`) ruft `msgBox.exec()`
    ohne Prüfung auf `mNonInteractive` — betrifft NICHT `--auto-cut` (das
    Projekt-Laden umgeht `openAVStreams`, wo der Dialog ausgelöst wird),
    blockiert aber einen headless **Fresh-Open**-Workflow (Datei ohne
    `.ttcut`-Projekt direkt öffnen). Follow-up aus Task 6 des
    `demux-defect-repair`-Feature-Ledgers (`docs/superpowers/sdd/progress.md`).

- ~~**ttcut-demux: Video-Dauer falsch gemessen → Über-Padding + irreführende Meldungen**~~
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

- ~~**Smart-Cut Code-Map Findings prüfen**~~ → **ALLE 4 PUNKTE ERLEDIGT** (2026-07-11/12, Details unten)
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

- **Dead-Code-Audit (Medium Priority)** — Erstlauf **DURCHGEFÜHRT 2026-07-12**
  (Branch `cleanup/dead-code-audit`, ~2.185 Zeilen entfernt in Batches A–K +
  Runde-2/3-Rescan bis Konvergenz; `--auto-cut`-QC bit-identisch zu master,
  161.844 Pakete). Jetzt als wiederkehrender Pass automatisiert im Skill
  `dead-code-audit` (claude-skills/global): 4-Quellen-Scanner
  (Build-Abwesenheit, Symbol-Grep, Linker-gc-sections, clang-tidy-Includes)
  + Sonnet-Klassifikation + Review-Gate. Künftige Läufe: Skill invoken.
  - Beispiel-Altfund: `TTCutAudioTask` blieb nach der v0.60.0-libav-Migration
    vom 2026-02-21 noch rund zwei Monate stehen, bis f2c4412 am 2026-04-25.
  - Offene Folge-Funde aus dem Erstlauf (kein toter Code):
    - ~~Stale Doc-Kommentare, die `isBlackAt` namentlich erwähnen~~ →
      **GEFIXT 2026-07-12** (`73acdf0`): auf den echten Mechanismus
      (TTBlackFrameSearchTask / Worker-Decoder, Preview-Fenster-Include)
      umgeschrieben.
    - `ttmpeg2window2.cpp` `histogramDifference` als `-Wunused-function`
      gemeldet (statische Funktion, kein Member) — separat prüfen.
  - Weiterhin offen (unverändert, kein toter Code):
    - ~~`AcmodInfo::cutInChangeTime` / `cutOutChangeTime`~~ → **ENTFERNT 2026-07-12**
      (`f4d4e66`, User-Entscheid: nur Burst-am-Schnittpunkt zählt; Umsetzungsweg
      falls je gewünscht in `docs/code-map/burst-detection.md` konserviert).
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
    (669 Einträge, vollständig). Settings+Cut-Dialog (`ed2a531`/`d716c83`), Rest der App
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
- [x] Preview: stay on the last frame at the end, two-stage Back, cut changes play
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
