# TTCut-ng TODO / Feature Requests

Offene Punkte und bekannte Einschränkungen. Erledigtes steht mit seinen
Belegen in [docs/completed-work.md](docs/completed-work.md).

## High Priority

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

- **Aspect-Präsentation Standbild vs. Play inkonsistent bei 4:3**
  (2026-08-03, beim Qt6-Probelauf aufgefallen; vorbestehend — Qt5 zeigt es
  auch, nur anders)
  - Beobachtung (User, 4:3-Material): **Qt5** — Play-Bild mit Balken
    oben/unten, Standbild mit schmalen schwarzen Streifen rundum. **Qt6-Probe**
    — Standbild mit Balken links/rechts, Play-Bild füllt OHNE Balken.
  - Standbild-Pfad (TTMPEG2Window2, SAR-korrigierte QImage) und mpv-Renderpfad
    (`vo=libmpv` → FBO) skalieren also unterschiedlich in die Widget-Fläche;
    unter Qt6 füllt der mpv-Pfad offenbar ohne Aspekt-Erhalt
    (keepaspect-Verhalten des Render-Kontexts prüfen).
  - Bei der Qt6-Migration mit untersuchen: beide Pfade sollen dieselbe
    Geometrie zeigen (Balkenlage identisch, kein Füllen ohne Aspekt-Erhalt).
    Repro: Tux-MPEG-2-PAL-SD-Testvideo (4:3) aus `tools/test-videos/`.

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
  - **Offen:** Tux-`.ttcut`-Files haben aktuell keine Cut-Entries — `--auto-cut`-Verification
    erfordert dass Cuts via Skript hinzugefügt werden. Helper-Script `make_tux_with_cuts.sh` wäre
    nützlich.

## Known Limitations

- **Stale image in "Current Frame" under KWin at fractional scaling
  (attribution OPEN: compositor bug vs. an app-specific trigger — the painted
  buffer itself is proven correct either way).** With KWin 6.7.2 on Wayland, a fractional display scale
  (150 %, 175 %) and a large or maximized window, parts of the window are not
  refreshed on screen. Large painted areas are affected — the still-frame pixmap
  keeps showing the previous picture while frame number and timecode advance;
  small painted areas arrive normally, and dialogs drawn over the affected area
  appear to shake. It reads exactly like a frozen GUI, and it cost two days of
  hunting on 2026-07-30/31 before the compositor was identified.
  - **Quickest proof it is not the application:** the Alt-Tab window preview
    shows the *correct* frame while the screen shows stale pixels. The buffer is
    right, only its presentation is not. Everything downstream of the decoder was
    measured correct headless (decoder, navigation, widget), and every paint path
    — QLabel, direct painting, with and without the GL widget, software GL —
    ends in the same verified-correct buffer. There is nothing to fix in TTCut-ng.
  - **Gemessen 2026-08-01:** Gemischte Skalierung ist NICHT der Auslöser — bei
    einheitlichen 150 % auf beiden Monitoren besteht der Fehler weiter, und
    zwar auf beiden Schirmen. Unter `QT_QPA_PLATFORM=xcb` bei denselben 150 %
    ist alles korrekt. Ein Bericht muss also "fractional scaling" nennen, nicht
    "mixed scale factors".
  - **Schwelle vermessen (2026-08-02, Skalierung 1,5, alle Punkte einzeln
    reproduziert):** Die Grenze hängt von BEIDEN Dimensionen ab — weder die
    Breite allein noch die Fläche allein erklärt sie. Beide Ein-Größen-Modelle
    wurden ausprobiert und sind an den Messwerten gescheitert (Flächen an der
    Schwelle: 2,50–2,57 Mio. bei fester Höhe gegen 2,01–2,05 Mio. bei fester
    Breite — 25 % auseinander). Gemessene Punkte (Fensterinhalt logisch;
    KWin-Frame = +28 px Titelleiste, keine Seitenränder):

    | Breite \ Höhe | ≤1103 | 1128 | 1412 |
    |---|---|---|---|
    | 1774 | sauber | sauber | sauber |
    | 1819 | sauber | FEHLER | FEHLER |
    | 2500 | — | — | FEHLER |

    Bei Breite 1774 tritt der Fehler in keiner getesteten Höhe auf; bei 1819
    ab Höhe 1128. Der Effekt ist deterministisch: fünf Wiederholungen in
    geänderter Reihenfolge, alle identisch zum Erstbefund. Randnotiz für den
    Bericht, ausdrücklich ohne These: 1774 logisch = 2661,0 physische Pixel,
    1819 logisch = 2728,5 — ungerade logische Breite ergibt bei 1,5 eine
    halbe physische Pixelbreite; 1819×1103 war jedoch trotz halber Breite
    sauber. Messwerkzeuge: `tools/diag/window-geometry.sh` (liest die
    Ist-Geometrie bei KWin ab) und die Messskripte
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/kwin-{threshold,verify,area-test,repeat-test}.sh`
    samt Protokollen (`kwin-*-2026*.log`) im selben Verzeichnis.
  - **Messfalle aus dem ersten Durchlauf:** die Startfassung des Messskripts
    beendete die TTCut-Instanzen nicht (`kill` traf die Subshell statt des
    Programms — `$!` nach `( cd … && ./prog ) &` ist die Subshell-PID; Fix:
    `exec` in der Subshell). Dadurch liefen bis zu fünf Instanzen parallel und
    die erste Messreihe war wertlos; alle obigen Zahlen stammen aus
    Wiederholungen mit genau einer Instanz.
  - **Ein Skalierungsvergleich 1,5 gegen 1,75 steht noch aus** (er hätte
    logische von physischen Grenzen getrennt). Er scheiterte daran, dass bei
    1,75 nur ~1234 logische Zeilen verfügbar sind und die Schwelle dort mit
    anderer Höhe gemessen werden müsste — nicht vergleichbar, solange das
    Zusammenspiel von Breite und Höhe unverstanden ist. Erst sinnvoll, wenn
    die KDE-Entwickler sagen, welche Größe intern die relevante ist.
  - **Qt5-Hypothese WIDERLEGT, Zuordnung OFFEN (2026-08-03,
    Qt6-Machbarkeitsprobe):** Das Qt6-Probe-Binary bindet
    `wp_fractional_scale_manager_v1` nachweislich (WAYLAND_DEBUG: bind +
    `get_fractional_scale`) — und der Fehler tritt trotzdem auf (User-Repro,
    maximiert bei 150 %). Ausgeschlossen ist damit nur der
    Qt5-Skalierungspfad. **Ob KWin oder ein TTCut-spezifischer Auslöser,
    ist offen** (User-Einschätzung: eher TTCut). Für einen App-Auslöser
    sprechen: fünf gescheiterte Minimalbeispiele (irgendeine
    TTCut-spezifische Zutat ist notwendig), die Abhängigkeit von der
    Größen-Historie (Maximieren WÄHREND laufender Wiedergabe heilt
    dauerhaft — ein reiner Compositor-Schwellenfehler sollte den Weg zur
    Größe nicht kennen), und: die Qt6-Repro schwächt den bisher vermuteten
    KWin-Mechanismus (forced server side scale factor greift nur bei
    Klienten OHNE das Protokoll). Dagegen sprechen: Alt-Tab zeigt den
    korrekten Puffer, und der Qt5-Protokollmitschnitt des Fehlerlaufs war
    klientenseitig korrekt (attach + voller damage + commit). Da die
    Qt6-Probe dieselbe Codebasis ist, wäre ein TTCut-Auslöser in
    Qt-versionsunabhängigem App-Code zu suchen (Fensteraufbau, Widget-Stack,
    mpv-Subsurface). **Nächster Diskriminator bei Wiederaufnahme:**
    WAYLAND_DEBUG-Vergleich Qt6-Fehlerlauf gegen den „geheilten" Zustand
    nach Maximieren-bei-Wiedergabe — unterscheidet sich die
    Protokollsequenz, liegt die Spur in der App.
  - **Protokollbeweis vorhanden** (`WAYLAND_DEBUG=1`, Logs unter
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/wayland-diff/`): Qt setzt
    `set_buffer_scale(2)` auf dem 1,5-Schirm und bindet
    `wp_fractional_scale_manager_v1` nie, obwohl angeboten. Zum nicht
    erschienenen Bild gehören `attach` + `damage_buffer(0,0,3638,2416)`
    (gesamte Bildfläche) + `commit`, danach 14 s lang kein weiterer Commit.
    Client-seitig also korrekt. **Auswertungsfalle:** der Log enthält zwei
    getrennte Wayland-Verbindungen (Qt und mpvs EGL) mit unabhängigen
    Objekt-IDs — es gibt zwei `wl_surface#25`; nur getrennt nach Queue
    (`{Default Queue}` vs `{mesa egl *}`) auswertbar.
  - **Als Ursache ausgeschlossen** (Testfälle `kwin-repaint-testcase{2,3}.cpp`
    mit Abschalt-Optionen, plus Bisektion an TTCut über die Diagnose-Schalter
    `TTCUT_DIAG_NO_PLAYER` und `TTCUT_DIAG_HIDE`): GL-Widget/mpv-Kontext,
    `QLabel::setPixmap`, `QStackedLayout::StackAll`, `QMainWindow`,
    App- und Widget-Stylesheets, `QGroupBox`/`QTabWidget`, Fenstergröße allein,
    Fläche allein, Teilschaden, Dekodierlatenz, Fortschrittsdialog,
    berührungsloser Betrieb ohne Eingaben. Der Fehler überlebt sogar das fast
    leere Fenster.
  - **Minimaler Testfall fehlt weiterhin.** `kwin-repaint-testcase.cpp` unter
    `/usr/local/src/CLAUDE_TMP/TTCut-ng/kwin-bugreport/` reproduziert den
    Fehler NICHT — weder mit großer einfarbiger Fläche noch als `QPixmap`,
    weder mit `QOpenGLWidget` noch maximiert. Ausgeschlossen ist damit, dass
    diese Zutaten genügen. Ohne so ein Beispiel fragen die KDE-Entwickler
    erfahrungsgemäß zuerst danach.
  - **Workarounds:** integer scaling (100 %, 200 %), do not maximize the window,
    or run `QT_QPA_PLATFORM=xcb build/ttcut-ng`. New (found 2026-08-03, works
    on Qt5 and the Qt6 probe alike): start with a small window and maximize
    **while a video is playing** — the display then stays correct, including
    navigation, until the program is closed.
  - **Suspected mechanism:** Qt 5 cannot speak the fractional-scale Wayland
    protocol, so KWin uses its *forced server side scale factor* path — the
    Plasma 6.7.0 changelog lists "making forced server side scale factor single
    buffered". Same failure class as (fixed) KDE bug 482987. No matching bug
    report existed as of 2026-07-31; 6.7.3 contains nothing relevant. On
    recurrence, check for a KWin update first (`apt policy kwin-wayland`).

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
