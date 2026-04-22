# Inaktive UI-Elemente — Cleanup Design

**Datum:** 2026-04-23
**Branch:** master (neuer Feature-Branch für Implementierung)
**Status:** Design freigegeben

## Problem

TTCut-ng schleppt vier UI-Elemente mit, die seit Jahren im Code stehen, aber dem User nicht erreichbar sind oder keine Funktion haben. Sie erzeugen Noise (dead code, TODO-Einträge, Wartungslast) ohne Nutzen.

1. **Chapters-Tab im Settings-Dialog** (`gui/ttcutsettingsdlg.cpp:41` → `settingsTab->removeTab(4);`) — im Konstruktor wird der Tab sofort wieder entfernt. Inhalt: eine einzige Checkbox `cbSpumuxChapter` ("Write spumux (XML) chapter-list") — DVD/spumux-Legacy, aus der dvdauthor-Ära.
2. **Chapters-Tab im Cut-Dialog** (`gui/ttcutavcutdlg.cpp:53` → `tabWidget->removeTab(3);`) — identische Legacy, gleicher Inhalt.
3. **"Configure…"-Button im Muxer-Einstellungsdialog** (`gui/ttcutsettingsmuxer.cpp:50` → `pbConfigureMuxer->setEnabled(false);`) — verbunden mit einem leeren Slot `onConfigureMuxer()`. Stammt aus der mplex-CLI-Ära (vor v0.60.0 libav-Integration). Mit libav als Built-in-Muxer gibt es nichts zu konfigurieren.
4. **`videoFileInfo` (TTCutVideoInfo)** im Hauptfenster (`ui/ttcutmainwindow.ui:103` → `maximumSize height=0`) — funktionsfähiges Widget (Dateiname, Länge, Auflösung, Aspect, Index), aber per 0-Pixel-Höhe unsichtbar. Alle Infos sind bereits in der `TTVideoTreeView` als Spalten sichtbar; das Widget ist seit dem neuen GUI-Layout mit TreeView-Widgets (vor v0.60.0) redundant.

Ein echter Custom-MKV-Chapter-Editor (editierbare Zeitstempel, Namen, Sprache) hätte keine Basis in der aktuellen Checkbox — er wäre eine komplette Neuentwicklung. Entsprechend macht es keinen Sinn, die Legacy-Tabs "für später" zu behalten.

## Design-Entscheidung

Alle vier Elemente komplett entfernen (Dateien, UI-Definitionen, Slots, .pro-Einträge, tote Slots, TODO-Einträge). Kein Ersatz-Widget, keine Fallback-UI. Die Intervall-basierte Auto-Kapitel-Funktion (`cbMkvCreateChapters` + `leChapterInterval` im Muxer-Tab) bleibt unangetastet — sie deckt den häufigen Fall weiterhin ab. Ein Custom-Chapter-Editor wird neu als TODO eingetragen.

Die toten Slots `onNextAVData()` / `onPrevAVData()` in `TTCutMainWindow` werden mitentfernt: sie werden nur von `videoFileInfo` getriggert. AV-Item-Navigation funktioniert weiterhin über Selektion in `TTVideoTreeView`.

## Teil-Änderungen

### A. Chapters-Tab (Settings-Dialog)

`gui/ttcutsettingsdlg.cpp`:
- Zeile 41 (`settingsTab->removeTab(4);`) entfernen.
- Zeile 49 (`//chaptersPage->setTabData();`) entfernen.
- Zeile 70 (`//chaptersPage->getTabData();`) entfernen.

`ui/ttsettingsdialog.ui`:
- `<widget class="QWidget" name="tabChapters">` Block (ca. Zeilen 109-…) entfernen.
- `<customwidget>`-Eintrag `<class>TTCutSettingsChapter</class>` (ca. Zeilen 173-…) entfernen.

Dateien löschen:
- `gui/ttcutsettingschapter.h`
- `gui/ttcutsettingschapter.cpp`
- `ui/ttcutsettingschapter.ui`

`ttcut-ng.pro`:
- FORMS `ui/ttcutsettingschapter.ui` (Zeile 62) entfernen.
- HEADERS `gui/ttcutsettingschapter.h` (Zeile 141) entfernen.
- SOURCES `gui/ttcutsettingschapter.cpp` (Zeile 232) entfernen.

### B. Chapters-Tab (Cut-Dialog)

`gui/ttcutavcutdlg.cpp`:
- Zeile 53 (`tabWidget->removeTab(3);`) entfernen.
- Zeile 75 (`//chaptersPage->setTabData();`) entfernen.
- Falls vorhanden: auskommentierter `getTabData()` Aufruf in der Save-Methode (`setGlobalData()`) entfernen.

`ui/avcutdialog.ui`:
- `<widget class="QWidget" name="tabChapters">` Block (Zeilen 466-…) entfernen.
- `<customwidget>`-Eintrag `<class>TTCutSettingsChapter</class>` (Zeilen 508-…) entfernen.

### C. "Configure..."-Button (Muxer-Einstellungen)

`gui/ttcutsettingsmuxer.cpp`:
- Zeile 50 (`pbConfigureMuxer->setEnabled(false);`) entfernen.
- Zeile 54 (`connect(pbConfigureMuxer, SIGNAL(clicked()), SLOT(onConfigureMuxer()));`) entfernen.
- Zeilen 166-168 (leere Methode `TTCutSettingsMuxer::onConfigureMuxer()`) entfernen.

`gui/ttcutsettingsmuxer.h`:
- Zeile 66 (Slot-Deklaration `void onConfigureMuxer();`) entfernen.

`ui/ttcutsettingsmuxer.ui`:
- `<widget class="QPushButton" name="pbConfigureMuxer">` Block entfernen.

### D. `videoFileInfo` (TTCutVideoInfo)

`ui/ttcutmainwindow.ui`:
- `<widget class="TTCutVideoInfo" name="videoFileInfo">` Block (Zeilen 86-110) entfernen.
- `<customwidget>`-Eintrag `<class>TTCutVideoInfo</class>` (Zeile 595+) entfernen.
- GridLayout-Zeilenindizes anpassen: der Widget saß in `row="1" column="0"`. Nachfolgende Items (`row="2"` und höher) um 1 dekrementieren, damit keine leere Layout-Zeile entsteht. Qt würde eine leere Zeile zwar ohne Höhenbedarf rendern, aber das Row-Offset wäre unsauber und könnte spätere Layout-Änderungen verwirren.

`gui/ttcutmainwindow.cpp`:
- Zeilen 265-267: die 3 `connect(videoFileInfo, …)` entfernen. Die `openFile()`-Verbindung ist redundant, da `videoFileList` denselben Slot bedient (Zeile 269) und `actionOpenVideo` ihn aus dem Menü aufruft (Zeile 237).
- Zeilen 806, 822, 1128, 1159, 1248: alle Aufrufe `videoFileInfo->…` entfernen.
- Methoden-Definitionen `onNextAVData()` (Zeile 1207-…) und `onPrevAVData()` (Zeile 1221-…) entfernen (nur von videoFileInfo getriggert).

`gui/ttcutmainwindow.h`:
- Zeilen 132-133: Slot-Deklarationen `onNextAVData()` und `onPrevAVData()` entfernen.

Dateien löschen:
- `gui/ttcutvideoinfo.h`
- `gui/ttcutvideoinfo.cpp`
- `ui/ttcutvideoinfowidget.ui`
- `ui/ttcutvideoinfowidget.qrc`

`ttcut-ng.pro`:
- RESOURCES `ui/ttcutvideoinfowidget.qrc` (Zeile 39) entfernen.
- FORMS `ui/ttcutvideoinfowidget.ui` (Zeile 74) entfernen.
- HEADERS `gui/ttcutvideoinfo.h` (Zeile 147) entfernen.
- SOURCES `gui/ttcutvideoinfo.cpp` (Zeile 238) entfernen.

### E. TODO-Update

`TODO.md`:
- Den Eintrag "Inaktive UI-Elemente prüfen und ggf. entfernen oder implementieren" (Low-Priority-Liste) komplett entfernen.
- Neuer Eintrag in Medium Priority, Formulierung ca.:

  > **Custom MKV Chapter Editor**
  > - Dialog mit Liste editierbarer Kapitel: Zeitstempel (hh:mm:ss.zzz), Name, Sprache
  > - Vor-Populierung aus Cut-Ins (jeder Cut-In wird Default-Kapitel)
  > - Persistenz in `.ttcut`-Projektdatei
  > - Ersetzt die Intervall-basierte Auto-Generierung nicht — bleibt als einfacher Default im Muxer-Tab bestehen

## Betroffene Dateien (Gesamtübersicht)

**Gelöscht (7 Dateien):**
- `gui/ttcutsettingschapter.h`
- `gui/ttcutsettingschapter.cpp`
- `ui/ttcutsettingschapter.ui`
- `gui/ttcutvideoinfo.h`
- `gui/ttcutvideoinfo.cpp`
- `ui/ttcutvideoinfowidget.ui`
- `ui/ttcutvideoinfowidget.qrc`

**Geändert (12 Dateien):**
- `gui/ttcutsettingsdlg.cpp`
- `gui/ttcutavcutdlg.cpp`
- `gui/ttcutsettingsmuxer.cpp`
- `gui/ttcutsettingsmuxer.h`
- `gui/ttcutmainwindow.cpp`
- `gui/ttcutmainwindow.h`
- `ui/ttsettingsdialog.ui`
- `ui/avcutdialog.ui`
- `ui/ttcutsettingsmuxer.ui`
- `ui/ttcutmainwindow.ui`
- `ttcut-ng.pro`
- `TODO.md`

(= 12 geänderte Einträge, 7 gelöschte)

## Reihenfolge & Commit-Strategie

Ein Commit (alles UI-Cleanup, semantisch zusammengehörig).

Implementierung in Teilschritten A → B → C → D → E mit Zwischen-Builds nach jedem Teil:
- `make clean && qmake ttcut-ng.pro && make -j$(nproc)` nach Teilschritt A
- `make -j$(nproc)` (inkrementell) nach B, C, D, E

Grund: qmake-Dependency-Tracking ist bekannt unzuverlässig. Ein Fehler (z. B. vergessener `#include`) soll sofort bemerkt werden, nicht erst am Ende. Nach der finalen Änderung noch einmal ein kompletter Full-Rebuild als Absicherung.

## Invarianten nach Fix

- **Settings-Dialog**: öffnet ohne Fehlermeldung, zeigt 4 Tabs (Allgemein, Dateien, Encoder, Muxer) statt vorher 5 (mit verstecktem Chapters).
- **Cut-Dialog**: öffnet ohne Fehlermeldung, zeigt 3 Tabs (Common, Encoder, Muxer) statt vorher 4.
- **Muxer-Tab**: kein "Configure…"-Button mehr — übrige Controls unverändert.
- **Hauptfenster**: unveränderter visueller Eindruck (videoFileInfo war schon 0 Pixel hoch).
- **AV-Item-Navigation**: funktioniert weiterhin über Klick in TTVideoTreeView.
- **MKV-Kapitel-Auto-Generierung**: unverändert (Muxer-Tab `cbMkvCreateChapters`).
- **Keine Binär-/Datei-Format-Änderung**: `.ttcut` Projektdateien bleiben unverändert kompatibel.

## Out of Scope

- **Custom MKV Chapter Editor** — als neuer TODO-Eintrag vorgesehen, keine Implementierung in diesem Cleanup.
- **Settings-Dialog neu strukturieren** — separater TODO-Punkt ("Einstellungsdialog neu strukturieren"), nicht Teil dieses Cleanups.
- **Andere Dead-Code-Verdachtsstellen** — kein proaktives Aufräumen weiterer Codebereiche; nur die vier in TODO.md benannten.

## Testplan

Manuelle Verifikation:

1. **Full Clean Build**: `make clean && qmake ttcut-ng.pro && make -j$(nproc)` → keine Warnings, keine Errors.
2. **App starten**: `QT_QPA_PLATFORM=xcb ./ttcut-ng` → Hauptfenster öffnet normal.
3. **Settings-Dialog** (Menü: Bearbeiten → Einstellungen): Öffnet ohne Crash, 4 Tabs sichtbar, kein Chapters-Tab, kein Configure-Button im Muxer-Tab.
4. **Cut-Dialog** (Schnitt ausführen): Öffnet ohne Crash, 3 Tabs sichtbar, kein Chapters-Tab.
5. **Projekt öffnen**: Bestehende `.ttcut`-Datei laden — funktioniert wie vorher.
6. **AV-Navigation**: Mehrere Videos per "Datei → Video öffnen" laden, zwischen ihnen per Klick in Video-Liste wechseln — funktioniert.
7. **MKV-Schnitt mit Kapitel-Option**: `cbMkvCreateChapters` aktivieren, Schnitt ausführen → Kapitel werden erzeugt (wie bisher).

Keine automatisierten Tests — reines UI-Cleanup, keine Logik-Pfade geändert.

## Rollback

Zwölf geänderte Dateien + sieben gelöschte Dateien, keine Daten- oder Format-Änderungen. Revert über `git revert <commit>` oder `git checkout <vor-Commit> -- .` wiederherstellbar.
