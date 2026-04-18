# TTCut-ng Logo Design — Spec

**Datum:** 2026-04-18
**Status:** Design-Freigabe erteilt, Implementierung ausstehend
**Vorgänger:** `ui/pixmaps/ttcut_logo_001.png` (287×254 PNG, flach-illustrativ)

## Ziel

Ein wiedererkennbares SVG-Logo für TTCut-ng, das von 16×16 bis 512×512 skaliert und sowohl als App-Icon (hicolor-Theme, `.desktop`-Integration) als auch in GitHub-README und Debian-Paket einsetzbar ist. Das neue Logo modernisiert das bestehende Motiv (Schere + Filmstreifen) und integriert den Tux als Linux-spezifische Signatur.

## Motiv und Komposition

- **Hauptmotiv:** Schere schneidet einen perspektivisch verjüngten Filmstreifen — das Motiv aus `ttcut_logo_001.png` als visueller Kernanker (konstruktiver Schnitt, nicht zerstörerisch).
- **Tux-Integration:** Tux sitzt im **untersten, größten Filmfenster** des Filmstreifens. Er „ist im Film" — Metapher für „Linux-native App schneidet dein Linux-Video".
- **Anker-Position** (relativ zum Motiv-Bounding-Box, nicht zum Canvas):
  - Tux-Oberkante bei **61 %** der Motiv-Höhe
  - Tux-Linkskante bei **66 %** der Motiv-Breite
  - Tux-Breite **15 %** der Motiv-Breite, Höhe proportional (SVG-Ratio 216:256 = 0,844)
- **Referenz-Pixel** für Implementierer (im Koordinatenraum des bestehenden 287×254 PNG): Tux-Bounding-Box **left=189, top=155, width=43, height=51**. Prozentuale Werte sind maßgeblich; Pixelangabe dient nur zur Veranschaulichung im Original-Bezugsrahmen. Für quadratisches SVG-Canvas mit zentriertem Motiv gelten die Prozentwerte bezogen auf die tatsächliche Motiv-Bounding-Box (nicht auf das Canvas).

## Stil

- **Moderner Render-Look:** Gradienten, weiche Highlights, sanfte Schatten. Kein flach-illustrativer Zeichenstil.
- **Einheitliche Stil-Signatur über alle Elemente:** Schere, Filmstreifen und Tux werden im **gleichen** Render-Stil gezeichnet. Insbesondere wird Tux nicht in seinem klassischen Render-Look eingebettet und dann mit einem andersstilistischen Rest kombiniert — alle Elemente erhalten dieselbe Licht-/Schattenbehandlung.
- **Keine Corporate-Sterilität.** Illustrativer Charakter bleibt erhalten (vergleichbar mit Kdenlive, GIMP, Inkscape).

## Farbpalette

Basiert auf dem bestehenden Logo, **satter und kräftiger** für den Render-Look:

| Element          | Grundton bestehend | Richtung für Neuzeichnung     |
|------------------|--------------------|-------------------------------|
| Schere           | Helles Lila        | Tieferes, satteres Lila       |
| Filmfenster dunkel | Mittleres Blau   | Kräftigeres, klares Blau      |
| Filmfenster hell | Helles Blau/Violett | Etwas satter, weniger pastell |
| Filmrand         | Hellweiß           | bleibt hell                   |
| Tux              | Schwarz / Weiß / Gelb (Schnabel, Füße) | Standard Tux-Farben, auf neue Palette abgestimmt |

Keine neuen Leitfarben. Ziel: Wiedererkennbarkeit gegenüber bisherigem Logo.

## Größenstrategie

**Ein Logo für alle Größen** (kein Dual-Size). Tux ist von 16×16 bis 512×512 dargestellt. Entscheidung begründet: Konsistenz in Icon-Sets wiegt schwerer als marginale Lesbarkeit im Tray.

Zu exportierende PNG-Größen für hicolor-Theme:
- 16, 22, 24, 32, 48, 64, 128, 256, 512

## Canvas-Format

**Quadratisches SVG-Canvas** (z.B. 512×512 viewBox). Das Motiv wird zentriert mit ausreichend transparentem Rand platziert, damit das Logo in runden/quadratischen Icon-Masken vieler Desktop-Environments sauber wirkt.

## Produktionsworkflow

Nach dem etablierten Verfahren (Skill `image-to-xcf`):

1. **Generierung:** Ideogram oder Copilot erzeugt eine erste Render-Variante anhand Prompt + Referenzbild (`ttcut_logo_001.png`)
2. **Farbseparation:** Pillow-Skript zerlegt das generierte Bild in editierbare Farbebenen
3. **XCF-Assembly:** GIMP fügt die Ebenen zu einer strukturierten Master-Datei zusammen (Hintergrund → Filmstreifen → Schere → Tux → Highlights)
4. **Vektorisierung:** Pfade in Inkscape nachziehen (oder GIMP-Pfade exportieren) → sauberes SVG
5. **Export:** SVG + PNG-Rasterisierung für alle hicolor-Größen

## AI-Prompt (Initialentwurf)

Für Ideogram/Copilot, als Startpunkt (verfeinert im Implementierungsprozess):

> A modern glossy application icon: a pair of lilac-purple scissors cutting a perspective-foreshortened filmstrip that arcs from upper-right to lower-left. The filmstrip has deep blue film frames with lighter blue highlights. In the bottom-most, largest film frame sits Tux the Linux penguin (small, centered, looking friendly). Rendered style with soft gradients, highlights, subtle drop shadows. Clean transparent background, centered composition, square canvas. No text, no letters.

Referenzbild: `ui/pixmaps/ttcut_logo_001.png`

## Deliverables

| Datei                                 | Zweck                                    |
|---------------------------------------|------------------------------------------|
| `ui/pixmaps/ttcut-ng.svg`             | Master-Vektor (für Inkscape-Editierung)  |
| `ui/pixmaps/ttcut_logo.xcf`           | Editierbare GIMP-Quelle mit Ebenen       |
| `ui/pixmaps/ttcut-ng-16.png`          | Tray / kleine Anzeige                    |
| `ui/pixmaps/ttcut-ng-22.png`          | Panel-Icon (GNOME)                       |
| `ui/pixmaps/ttcut-ng-24.png`          | Panel-Icon (KDE)                         |
| `ui/pixmaps/ttcut-ng-32.png`          | Fenster-Icon                             |
| `ui/pixmaps/ttcut-ng-48.png`          | Taskleiste Standard                      |
| `ui/pixmaps/ttcut-ng-64.png`          | Alt-Tab, Menüs                           |
| `ui/pixmaps/ttcut-ng-128.png`         | Desktop-Datei-Manager                    |
| `ui/pixmaps/ttcut-ng-256.png`         | About-Dialog, hochauflösende Displays    |
| `ui/pixmaps/ttcut-ng-512.png`         | Store-Listings, Retina-Assets            |
| `docs/logo-banner.png`                | README-Header (optional, breit formatiert) |

## Integration

### Qt-Ressourcen
- `ui/ttcut.qrc` um das neue SVG + relevante PNG-Größen erweitern
- Fenster-Icon in `gui/ttcutmainwindow.cpp`: `setWindowIcon(QIcon(":/pixmaps/ttcut-ng.svg"))`
- About-Dialog: großes PNG (256 oder 512) als Header-Grafik
- **Dependency-Check:** QtSvg muss im `ttcut-ng.pro` als Modul gelistet sein (`QT += svg`), sonst rendert `QIcon` das SVG nicht. Alternativ ein hochauflösendes PNG als primäres Icon-Asset verwenden.

### Desktop-Entry
- `ttcut.desktop` bereits korrekt: `Icon=ttcut-ng` → Theme löst automatisch auf, sobald PNGs im hicolor-Pfad installiert sind. Keine Änderung nötig.

### Debian-Paket
- `debian/install` oder `debian/ttcut-ng.install` erweitern: PNGs nach `/usr/share/icons/hicolor/<size>/apps/ttcut-ng.png` ausliefern
- SVG nach `/usr/share/icons/hicolor/scalable/apps/ttcut-ng.svg`
- Post-Install-Cache-Aktualisierung: `update-icon-caches /usr/share/icons/hicolor` (via `dh_icons` in `debian/rules` oder via `.triggers`-Datei)

### README
- Header-Logo in `README.md` einfügen (relativ verlinkt auf `ui/pixmaps/ttcut-ng.svg` oder ein dediziertes Banner-PNG)
- Screenshots-Bereich bleibt unverändert

## Erfolgskriterien

- [ ] SVG öffnet fehlerfrei in Inkscape, Firefox, Qt (QSvgRenderer)
- [ ] Bei 16×16 ist zumindest Schere und Film-Silhouette klar erkennbar (Tux darf verschwimmen, darf aber nicht als unerkennbarer Fleck die Komposition stören)
- [ ] Bei 48×48 ist Tux erkennbar als Pinguin (weißer Bauch vs. blauem Fenster-Hintergrund)
- [ ] Bei 256×256 sind Render-Details (Highlights, Gradienten) klar sichtbar
- [ ] Farben wirken kräftiger als bestehendes PNG, ohne die Wiedererkennbarkeit zu brechen
- [ ] Tux-Anker-Position ist in allen Größen visuell identisch (selbes „im-Film"-Gefühl)
- [ ] Debian-Paket installiert Icons korrekt ins hicolor-Theme; `ttcut.desktop` zeigt das Icon im Launcher

## Offene Punkte / Risiken

- **Ideogram-Output-Qualität:** Generative Modelle produzieren häufig Logos mit Text-Artefakten, unsauberen Kanten oder inkonsistenten Perspektiven. Plan: mehrere Iterationen, dabei Negative-Prompt für Text, gegebenenfalls manuelle Nachbearbeitung in GIMP/Inkscape.
- **Tux-Proportionen in generierten Renders:** AI-Modelle kennen Tux, bekommen aber oft Proportionen falsch (zu fett, falsche Farben, falsche Haltung). Fallback: Tux aus bestehendem `Tux.svg` als Inkscape-Vektor einbauen und stilistisch an den Render-Stil anpassen (Schatten, Licht), anstatt ihn vom Generator zeichnen zu lassen.
- **Vektorisierung vs. Raster-only:** Wenn die Vektorisierung aufwendig wird, ist ein erstes Release mit hochauflösenden PNGs (512→skaliert) pragmatisch vertretbar. SVG kann in späterem Iterationsschritt folgen.
- **Wiedererkennbarkeit:** Durch die Stil-Umstellung von flach zu glossy besteht das Risiko, dass Bestandsanwender das Logo nicht mehr sofort wiedererkennen. Kompositorisch bleibt Schere+Film erhalten, was das mildert.

## Lizenzierung und Attribution

Tux wurde 1996 von **Larry Ewing** erstellt. Die Nutzungsbedingung lautet:

> *Permission to use and/or modify this image is granted provided you acknowledge me `lewing@isc.tamu.edu` and The GIMP if someone asks.*

Verpflichtend umzusetzen:

- **`CREDITS.md` oder Abschnitt in `README.md`** mit Attribution an Larry Ewing, E-Mail-Adresse und Hinweis auf GIMP als ursprüngliches Werkzeug
- **Ebenen-Metadaten in `ttcut_logo.xcf`**: Tux-Ebene kommentieren mit Attribution
- **SVG-Metadaten** (`<metadata>`-Element im `ttcut-ng.svg`): Attribution als `dc:creator` und Lizenzhinweis
- **Debian-Paket**: Attribution in `debian/copyright` unter einem eigenen Paragraph für die Tux-Asset-Herkunft aufnehmen

Die Quelldatei `ui/pixmaps/Tux.svg` ist bereits im Repo — wenn dort keine Attribution im SVG-Header steht, ist das zu ergänzen.

## Nicht-Ziele

- Keine Animation (kein animiertes SVG, kein Lottie)
- Keine Darkmode-Variante in v1 (Tux funktioniert auf hellem wie dunklem Hintergrund)
- Keine App-Store-spezifischen Varianten (Flatpak, Snap) — erst wenn aktuell geplant
