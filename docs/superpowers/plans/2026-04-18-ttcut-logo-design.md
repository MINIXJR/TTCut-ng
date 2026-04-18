# TTCut-ng Logo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Einsetzbares neues TTCut-ng Logo produzieren und ins Projekt integrieren (SVG, PNGs in allen Icon-Größen, GIMP-Quelle, Qt-/Debian-/README-Integration, Tux-Attribution).

**Architecture:** Zwei-Phasen-Produktion: (1) Visuelles Artefakt über den etablierten AI-Workflow (Ideogram/Copilot → Pillow → GIMP via Skill `image-to-xcf`). (2) Integration in den Codebase-Deployment-Pfad (Qt-Ressourcen, hicolor-Theme im Debian-Paket, README, Attribution). Spec: `docs/superpowers/specs/2026-04-18-ttcut-logo-design.md`.

**Tech Stack:** Ideogram / GitHub Copilot Image Gen, Pillow (Python), GIMP (Script-Fu/Batch), Inkscape (SVG-Export), Qt5 QtSvg, dpkg-buildpackage, dh_icons.

**Ausführungs-Besonderheit:** Bild-Generation ist **User-getrieben** (Task 2). Pillow/GIMP-Teile laufen über Skill `image-to-xcf`. Zwischen den Tasks 2 und 4 liegt ein manueller Entscheidungspunkt.

---

## File Structure

**Neue Dateien:**
- `CREDITS.md` — zentrale Attribution-Datei (Tux, ggf. weitere Assets)
- `ui/pixmaps/ttcut-ng.svg` — Master-SVG (quadratisch, mit Metadata)
- `ui/pixmaps/ttcut_logo.xcf` — editierbare GIMP-Quelle
- `ui/pixmaps/ttcut-ng-16.png`, `-22.png`, `-24.png`, `-32.png`, `-48.png`, `-64.png`, `-128.png`, `-256.png`, `-512.png` — Raster-Exporte
- `docs/logo-banner.png` — README-Header-Grafik
- `debian/ttcut-ng.install` — Paketdatei-Mapping für hicolor-Pfade

**Modifizierte Dateien:**
- `ttcut-ng.pro` — QtSvg zur `QT`-Variable
- `ui/mainwindow.qrc` — SVG + PNG-Größen als Qt-Ressource (dabei das alte `ttcut_logo_001.png` ersetzen)
- `gui/ttcutmainwindow.cpp` — `setWindowIcon()` im Konstruktor
- `README.md` — Logo-Header oben einfügen
- `debian/copyright` — Tux-Attribution als eigener Paragraph

---

## Task 1: CREDITS.md mit Tux-Attribution anlegen

**Files:**
- Create: `CREDITS.md`

- [ ] **Step 1: CREDITS.md schreiben**

```markdown
# Credits

## Third-Party Assets

### Tux (Linux Penguin Mascot)

Tux was created in 1996 by **Larry Ewing** (`lewing@isc.tamu.edu`) using The GIMP.

Used in: `ui/pixmaps/Tux.svg`, `ui/pixmaps/ttcut-ng.svg` (application logo, integrated into the film frame).

Original permission statement:

> Permission to use and/or modify this image is granted provided you acknowledge me `lewing@isc.tamu.edu` and The GIMP if someone asks.

## Dependencies

This project links against Qt5, libavformat/libavcodec/libavutil/libswscale (FFmpeg), libmpeg2/libmpeg2convert, and uses libx264/libx265 as libav plugins. See `debian/copyright` and the upstream licenses of the respective projects.
```

- [ ] **Step 2: Verifikation**

Run: `cat CREDITS.md | head`
Expected: Datei existiert, enthält `Larry Ewing` und `lewing@isc.tamu.edu`.

- [ ] **Step 3: Commit**

```bash
git add CREDITS.md
git commit -m "Add CREDITS.md with Tux attribution"
```

---

## Task 2: AI-Bild generieren (User-Aktion)

**Files:**
- Create: `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/prompt.md`
- Create: `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png` (vom User via Ideogram/Copilot)

- [ ] **Step 1: Arbeitsverzeichnis anlegen**

```bash
mkdir -p /usr/local/src/CLAUDE_TMP/TTCut-ng/logo
```

- [ ] **Step 2: Prompt-Dokumentation schreiben**

Dateiinhalt `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/prompt.md`:

```markdown
# Logo-Prompt

**Tool:** Ideogram (oder Copilot / DALL-E als Fallback)

**Referenzbild:** /usr/local/src/TTCut-ng/ui/pixmaps/ttcut_logo_001.png

**Prompt (EN):**

A modern glossy application icon: a pair of lilac-purple scissors cutting a perspective-foreshortened filmstrip that arcs from upper-right to lower-left. The filmstrip has deep blue film frames with lighter blue highlights. In the bottom-most, largest film frame sits Tux the Linux penguin (small, centered, looking friendly, correctly shaped — black body, white belly, yellow beak and feet). Rendered style with soft gradients, highlights, subtle drop shadows. Clean transparent background, centered composition, square canvas. No text, no letters, no logos, no watermarks.

**Negative prompt:** text, letters, words, watermark, signature, cropped, blurry, low quality, disfigured tux, wrong tux colors.

**Tux-Anker (Referenz für User-Review):** Tux sitzt im untersten, größten Filmfenster. Bounding-Box-Oberkante bei ~61 %, Linkskante bei ~66 %, Breite ~15 % der Motiv-Bounding-Box.

**Farben-Referenz:**
- Schere: tiefes Lila (etwa #7A5AA8 → kräftiger)
- Filmfenster dunkel: sattes Blau (etwa #3B4BA8)
- Filmfenster hell: mittleres Blau/Violett (etwa #5F6ECC)
- Filmrand: hell

**Output:** PNG, mindestens 1024×1024, transparenter Hintergrund wenn möglich.

**Zielpfad nach Generation:** /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png
```

- [ ] **Step 3: USER-AKTION — Bild generieren**

Manuell:
1. Prompt aus `prompt.md` an Ideogram oder Copilot geben
2. Mehrere Varianten erzeugen lassen
3. Beste Variante nach `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png` speichern
4. Iterieren falls die erste Runde nicht überzeugt (Tux-Proportionen, Farben, Kanten)

- [ ] **Step 4: Verifikation**

Run: `file /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png`
Expected: `PNG image data, <width> x <height>, ...` mit mindestens 1024×1024.

- [ ] **Step 5: Prompt-Dokumentation committen (Bild selbst nicht)**

Bild bleibt außerhalb des Repos im CLAUDE_TMP. Nur die Prompt-Dokumentation ins Repo (optional — sinnvoll für Reproduzierbarkeit):

```bash
mkdir -p docs/logo
cp /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/prompt.md docs/logo/prompt.md
git add docs/logo/prompt.md
git commit -m "Document logo generation prompt"
```

---

## Task 3: Farbseparation + XCF-Assembly via Skill

**Files:**
- Input: `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png`
- Output: `/usr/local/src/CLAUDE_TMP/TTCut-ng/logo/ttcut_logo.xcf`

- [ ] **Step 1: Skill `image-to-xcf` aufrufen**

Im Claude-Code Chat oder interaktiv:

```
/image-to-xcf /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/generated.png
```

Der Skill dokumentiert den eigenen Pillow-Separations-Workflow (`~/.claude/skills/image-to-xcf/SKILL.md`). Output ist eine XCF-Datei mit editierbaren Ebenen.

- [ ] **Step 2: XCF Review in GIMP**

Manuell:
1. `gimp /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/ttcut_logo.xcf`
2. Ebenenpanel prüfen: sinnvolle Trennung (Hintergrund, Schere, Filmstreifen, Tux, Highlights)?
3. Rand-Cleanups falls nötig
4. Tux-Ebene kommentieren (Ebenen-Attribute → Kommentar: „Tux by Larry Ewing, 1996, per Tux-License")

- [ ] **Step 3: XCF ins Repo kopieren**

```bash
cp /usr/local/src/CLAUDE_TMP/TTCut-ng/logo/ttcut_logo.xcf ui/pixmaps/ttcut_logo.xcf
```

- [ ] **Step 4: Commit**

```bash
git add ui/pixmaps/ttcut_logo.xcf
git commit -m "Add editable GIMP master for new logo"
```

---

## Task 4: PNG-Exporte in allen Icon-Größen

**Files:**
- Create: `ui/pixmaps/ttcut-ng-{16,22,24,32,48,64,128,256,512}.png`

- [ ] **Step 1: Export-Skript schreiben**

Ein Shell-Skript mit GIMP-Batch (oder ImageMagick falls XCF nach PNG-1024 exportiert wurde):

Via GIMP Script-Fu (direktes XCF → PNG je Größe):

```bash
for size in 16 22 24 32 48 64 128 256 512; do
  gimp -i -b "(let* ((image (car (gimp-file-load RUN-NONINTERACTIVE \"$(pwd)/ui/pixmaps/ttcut_logo.xcf\" \"ttcut_logo.xcf\")))) \
    (gimp-image-flatten image) \
    (gimp-image-scale image $size $size) \
    (file-png-save RUN-NONINTERACTIVE image (car (gimp-image-get-active-drawable image)) \"$(pwd)/ui/pixmaps/ttcut-ng-$size.png\" \"ttcut-ng-$size\" 0 9 1 1 1 1 1))" -b '(gimp-quit 0)'
done
```

Falls XCF nicht quadratisch ist, muss vor `gimp-image-scale` ein Canvas-Resize stattfinden — sonst wird das Motiv verzerrt.

- [ ] **Step 2: Exporte ausführen und prüfen**

```bash
ls -la ui/pixmaps/ttcut-ng-*.png
file ui/pixmaps/ttcut-ng-16.png ui/pixmaps/ttcut-ng-256.png
```

Expected: Alle 9 PNGs existieren, jeweils `PNG image data, NxN, 8-bit/color RGBA`.

- [ ] **Step 3: Visual Sanity Check**

Manuell je ein kleines (16) und großes (256) PNG in einem Bildbetrachter öffnen. Bei 16×16: Umrisse klar erkennbar, kein matschiger Fleck. Bei 256: alle Render-Details sichtbar (Glanz, Gradienten, Tux im Fenster).

- [ ] **Step 4: Commit**

```bash
git add ui/pixmaps/ttcut-ng-*.png
git commit -m "Add hicolor-sized PNG exports for new logo"
```

---

## Task 5: SVG mit Metadata erzeugen

**Files:**
- Create: `ui/pixmaps/ttcut-ng.svg`

- [ ] **Step 1: SVG erzeugen**

Zwei Wege:

**Option A (empfohlen):** In Inkscape das 512er-PNG als Bitmap einbetten und als SVG speichern. Rudimentär, aber skaliert für Qt ausreichend.

```bash
inkscape --export-filename=ui/pixmaps/ttcut-ng.svg --export-plain-svg --export-dpi=300 ui/pixmaps/ttcut-ng-512.png
```

**Option B (besser):** Manuelle Vektorisierung in Inkscape (Pfade tracen: Schere, Filmstreifen, Tux). Aufwendiger — als Folge-Task planbar.

- [ ] **Step 2: SVG-Metadata ergänzen**

Mit einem Text-Editor im erzeugten SVG zwischen `<svg>` und erstem Zeichen-Element folgendes einfügen:

```xml
<metadata>
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
           xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:cc="http://creativecommons.org/ns#">
    <cc:Work rdf:about="">
      <dc:title>TTCut-ng Application Icon</dc:title>
      <dc:creator><cc:Agent><dc:title>TTCut-ng Project</dc:title></cc:Agent></dc:creator>
      <dc:description>Scissors cutting a filmstrip with Tux integrated into the bottom-most film frame.</dc:description>
      <dc:contributor><cc:Agent><dc:title>Larry Ewing (original Tux, lewing@isc.tamu.edu)</dc:title></cc:Agent></dc:contributor>
      <dc:rights>See CREDITS.md in the TTCut-ng repository</dc:rights>
    </cc:Work>
  </rdf:RDF>
</metadata>
```

- [ ] **Step 3: SVG-Validierung**

```bash
xmllint --noout ui/pixmaps/ttcut-ng.svg && echo "SVG ok"
```

Expected: `SVG ok` (exit 0).

- [ ] **Step 4: Commit**

```bash
git add ui/pixmaps/ttcut-ng.svg
git commit -m "Add SVG master with Tux attribution metadata"
```

---

## Task 6: README-Banner erzeugen

**Files:**
- Create: `docs/logo-banner.png`

- [ ] **Step 1: Banner mit ImageMagick komponieren**

512er-Logo links, Text rechts, transparenter Hintergrund, breitformatig (z.B. 1280×320):

```bash
convert -size 1280x320 xc:none \
  \( ui/pixmaps/ttcut-ng-256.png -resize 256x256 \) -gravity west -geometry +40+0 -composite \
  -gravity west -pointsize 64 -fill '#3B4BA8' -annotate +320+0 'TTCut-ng' \
  -gravity west -pointsize 24 -fill '#555555' -annotate +320+80 'Frame-accurate video cutter' \
  docs/logo-banner.png
```

- [ ] **Step 2: Visuelle Kontrolle**

Manuell `docs/logo-banner.png` öffnen und prüfen ob Text lesbar und Logo scharf ist.

- [ ] **Step 3: Commit**

```bash
git add docs/logo-banner.png
git commit -m "Add README logo banner"
```

---

## Task 7: Qt-Integration (QtSvg, Ressourcen, MainWindow-Icon)

**Files:**
- Modify: `ttcut-ng.pro`
- Modify: `ui/mainwindow.qrc`
- Modify: `gui/ttcutmainwindow.cpp`

- [ ] **Step 1: QtSvg zu `ttcut-ng.pro` hinzufügen**

Zeile 16 in `ttcut-ng.pro` ändern:

```
# Vorher:
QT          += core widgets gui xml network

# Nachher:
QT          += core widgets gui xml network svg
```

- [ ] **Step 2: Ressourcen in `ui/mainwindow.qrc` ergänzen**

Die Zeile `<file>pixmaps/ttcut_logo_001.png</file>` ersetzen durch:

```xml
    <file>pixmaps/ttcut-ng.svg</file>
    <file>pixmaps/ttcut-ng-16.png</file>
    <file>pixmaps/ttcut-ng-22.png</file>
    <file>pixmaps/ttcut-ng-24.png</file>
    <file>pixmaps/ttcut-ng-32.png</file>
    <file>pixmaps/ttcut-ng-48.png</file>
    <file>pixmaps/ttcut-ng-64.png</file>
    <file>pixmaps/ttcut-ng-128.png</file>
    <file>pixmaps/ttcut-ng-256.png</file>
    <file>pixmaps/ttcut-ng-512.png</file>
```

- [ ] **Step 3: `setWindowIcon` im MainWindow-Konstruktor einfügen**

Datei `gui/ttcutmainwindow.cpp`, im Konstruktor nach der `QMainWindow()`-Basisklassen-Init (Zeile ~100), direkt vor dem ersten UI-Setup-Call:

```cpp
// Application icon (scalable SVG with PNG fallbacks from hicolor theme)
QIcon appIcon;
appIcon.addFile(":/pixmaps/ttcut-ng.svg");
for (int size : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
    appIcon.addFile(QString(":/pixmaps/ttcut-ng-%1.png").arg(size), QSize(size, size));
}
setWindowIcon(appIcon);
```

Falls nicht schon vorhanden, oben in der Datei:

```cpp
#include <QIcon>
```

- [ ] **Step 4: `ttcut_logo_001.png` entfernen**

Das alte Logo ist jetzt obsolet:

```bash
git rm ui/pixmaps/ttcut_logo_001.png
```

Prüfen ob es noch referenziert wird:

```bash
grep -rn "ttcut_logo_001" --include="*.cpp" --include="*.h" --include="*.qrc" --include="*.ui" .
```

Expected: keine Treffer mehr (außer dieser Plan und CHANGELOG-Einträgen).

- [ ] **Step 5: Clean Build**

```bash
make clean
qmake ttcut-ng.pro
bear -- make -j$(nproc)
```

Expected: Build erfolgreich, keine `QtSvg`-Fehler, keine ungelöste Referenz auf das alte Logo.

- [ ] **Step 6: Icon-Test**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
```

Fenster öffnen, Icon im Titelbalken und in der Taskleiste prüfen.

- [ ] **Step 7: Commit**

```bash
git add ttcut-ng.pro ui/mainwindow.qrc gui/ttcutmainwindow.cpp
git rm ui/pixmaps/ttcut_logo_001.png
git commit -m "Wire new logo into Qt resources and MainWindow"
```

---

## Task 8: Debian-Paket: hicolor-Icons + Copyright

**Files:**
- Create: `debian/ttcut-ng.install`
- Modify: `debian/copyright`

- [ ] **Step 1: `debian/ttcut-ng.install` anlegen**

```
ttcut-ng usr/bin
ttcut.desktop usr/share/applications
ui/pixmaps/ttcut-ng.svg usr/share/icons/hicolor/scalable/apps
ui/pixmaps/ttcut-ng-16.png usr/share/icons/hicolor/16x16/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-22.png usr/share/icons/hicolor/22x22/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-24.png usr/share/icons/hicolor/24x24/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-32.png usr/share/icons/hicolor/32x32/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-48.png usr/share/icons/hicolor/48x48/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-64.png usr/share/icons/hicolor/64x64/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-128.png usr/share/icons/hicolor/128x128/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-256.png usr/share/icons/hicolor/256x256/apps/ttcut-ng.png
ui/pixmaps/ttcut-ng-512.png usr/share/icons/hicolor/512x512/apps/ttcut-ng.png
```

Hinweis: `dh_icons` wird von `debhelper` automatisch durch den `dh $@`-Aufruf in `debian/rules` eingebunden, sofern das Paket Icons in `/usr/share/icons/hicolor/` installiert. Nichts weiter zu tun.

- [ ] **Step 2: `debian/copyright` ergänzen**

Nach dem letzten Paragraph folgenden neuen Paragraph anhängen:

```
Files: ui/pixmaps/Tux.svg ui/pixmaps/ttcut-ng.svg ui/pixmaps/ttcut-ng-*.png ui/pixmaps/ttcut_logo.xcf
Copyright: 1996 Larry Ewing <lewing@isc.tamu.edu> (Tux), TTCut-ng Project (composition)
License: Tux-License
 Permission to use and/or modify this image is granted provided you
 acknowledge Larry Ewing <lewing@isc.tamu.edu> and The GIMP if someone
 asks. See CREDITS.md.
```

- [ ] **Step 3: Debian-Paket bauen**

```bash
./build-package.sh
```

(oder `echo "logo integration" | bash build-package.sh` falls interaktiv).

Expected: `.deb` entsteht, kein Fehler bei `dh_install` oder `dh_icons`.

- [ ] **Step 4: debian/changelog rückgängig machen (pro Projekt-Konvention)**

```bash
git checkout -- debian/changelog
```

- [ ] **Step 5: Lokaltest des gebauten Pakets (optional, nicht in CI)**

```bash
ls ../ttcut-ng_*.deb
# sudo dpkg -i ../ttcut-ng_*.deb    # User führt das manuell aus, siehe CLAUDE.md "no sudo"
```

Desktop-Launcher neu starten, prüfen ob Icon im Menü erscheint.

- [ ] **Step 6: Commit**

```bash
git add debian/ttcut-ng.install debian/copyright
git commit -m "Install hicolor icons via Debian package and document Tux license"
```

---

## Task 9: README Logo-Header + Finalisierung CREDITS

**Files:**
- Modify: `README.md`
- Modify: `CREDITS.md`

- [ ] **Step 1: Logo-Banner oben in README einfügen**

Erste Zeile von `README.md` prüfen (meist `# TTCut-ng`). Direkt **über** der ersten Zeile einfügen:

```markdown
<p align="center">
  <img src="docs/logo-banner.png" alt="TTCut-ng" width="640">
</p>

```

- [ ] **Step 2: CREDITS-Link aus README referenzieren**

Im README einen Abschnitt ergänzen (z.B. vor „License"):

```markdown
## Credits

See [CREDITS.md](CREDITS.md) for third-party asset attributions (notably Tux by Larry Ewing).
```

- [ ] **Step 3: Verifikation durch Markdown-Preview**

```bash
grep -n "logo-banner.png\|CREDITS.md" README.md
```

Expected: beide Referenzen gefunden.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "Add logo banner and credits reference to README"
```

---

## Task 10: End-to-End Verifikation

**Files:** keine

- [ ] **Step 1: Full Rebuild**

```bash
make clean
qmake ttcut-ng.pro
bear -- make -j$(nproc)
```

Expected: Clean build, keine Warnings zu fehlenden Ressourcen.

- [ ] **Step 2: App starten und visuell prüfen**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng &
```

Checkliste:
- Icon im Titelbalken sichtbar
- Icon in der Taskleiste sichtbar (je nach DE)
- Alt+Tab zeigt das neue Icon
- About-Dialog (falls vorhanden, sonst dieser Punkt entfällt) zeigt Logo

- [ ] **Step 3: Desktop-Entry prüfen (nach Paket-Install)**

Falls `.deb` lokal installiert wurde (außerhalb dieses Plans, vom User):

```bash
update-desktop-database ~/.local/share/applications 2>/dev/null
gtk-launch ttcut.desktop || xdg-open /usr/share/applications/ttcut.desktop
```

Visuell: Application-Menü zeigt Icon korrekt, Größen 16/24/48 werden automatisch aus hicolor gezogen.

- [ ] **Step 4: Final Review**

Manuell durchgehen:
- `CREDITS.md` enthält Tux-Attribution? ✓
- `debian/copyright` enthält Tux-Paragraph? ✓
- SVG-Metadata erwähnt Larry Ewing? ✓
- README zeigt Banner? ✓
- Alle 9 PNG-Größen existieren? (`ls ui/pixmaps/ttcut-ng-*.png | wc -l` → 9)

- [ ] **Step 5: CHANGELOG.md ergänzen**

Unter der nächsten offenen Version (oder `## Unreleased`) in `CHANGELOG.md`:

```markdown
### Added
- New application logo (modernized scissors+filmstrip render with integrated Tux in the bottom-most film frame)
- SVG master + hicolor PNG sizes 16/22/24/32/48/64/128/256/512
- `CREDITS.md` with third-party asset attribution (Tux by Larry Ewing)

### Changed
- README now features a logo banner header

### Removed
- Old `ui/pixmaps/ttcut_logo_001.png` (replaced by `ttcut-ng.svg` + PNG raster set)
```

- [ ] **Step 6: Abschluss-Commit (falls Kleinigkeiten in vorigen Tasks nachgezogen wurden)**

```bash
git status
# falls Änderungen:
git add -A
git commit -m "Document logo rework in CHANGELOG and finalize verification"
```

---

## Rollback-Pfad

Falls bei einem Task gravierende Probleme auftreten:

```bash
git log --oneline | head -15    # Commits dieses Plans identifizieren
git reset --hard <letzter-guter-commit>
```

Das alte `ttcut_logo_001.png` bleibt in der Git-History abrufbar (`git show <commit>:ui/pixmaps/ttcut_logo_001.png > /tmp/old-logo.png`).

---

## Akzeptanzkriterien (aus Spec)

- [ ] SVG öffnet fehlerfrei in Inkscape, Firefox, Qt
- [ ] Bei 16×16 Schere + Film-Silhouette klar erkennbar
- [ ] Bei 48×48 Tux als Pinguin erkennbar
- [ ] Bei 256×256 alle Render-Details sichtbar
- [ ] Farben kräftiger als bestehendes PNG, Wiedererkennung bleibt
- [ ] Tux-Anker in allen Größen identisch positioniert
- [ ] Debian-Paket installiert Icons ins hicolor-Theme, `.desktop` zeigt Icon im Launcher
- [ ] Tux-Attribution an allen geforderten Stellen (CREDITS.md, SVG-Metadata, XCF-Ebene, debian/copyright)
