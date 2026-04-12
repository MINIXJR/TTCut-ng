# Audio Language Preference List Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add user-configurable audio language preference list that replaces the system-locale-based sort in the audio list.

**Architecture:** New `TTCut::audioLanguagePreference` QStringList (persisted in QSettings). Input normalized via new `TTCut::normalizeLangCode()` that accepts 2-letter, canonical 3-letter, and alternative ISO 639-2 forms. Sort logic in `TTAudioItem::operator<` uses preference index (lower = higher priority), AC3-before-others rule unchanged.

**Tech Stack:** Qt5 (QStringList, QSettings, QLineEdit), existing TTCut singleton

**Spec:** `docs/superpowers/specs/2026-04-12-audio-language-preference-design.md`

---

### Task 1: Add normalizeLangCode() with alias table

**Files:**
- Modify: `common/ttcut.h`
- Modify: `common/ttcut.cpp`

- [ ] **Step 1: Add declaration**

In `common/ttcut.h`, in the `ISO 639 language support` section (around line 257):

```cpp
   // --------------------------------------------------------------
   // ISO 639 language support
   // --------------------------------------------------------------
   static QStringList languageCodes();    // {"und","deu","eng","fra",...}
   static QStringList languageNames();    // {"Undetermined","Deutsch","English",...}
   static QString iso639_1to2(const QString& code2);  // "de" → "deu"
   static QString normalizeLangCode(const QString& code);  // "de"/"ger"/"DEU" → "deu", unknown → ""
```

- [ ] **Step 2: Add implementation**

In `common/ttcut.cpp`, after the `iso639_1to2` function (around line 310):

```cpp
QString TTCut::normalizeLangCode(const QString& code)
{
  QString c = code.trimmed().toLower();
  if (c.isEmpty()) return QString();

  // 2-letter ISO 639-1 → use existing map
  if (c.length() == 2) {
    QString result = iso639_1to2(c);
    return (result == "und") ? QString() : result;
  }

  if (c.length() != 3) return QString();

  // Alias table: alternative ISO 639-2 forms → TTCut canonical
  static QMap<QString, QString> alias;
  if (alias.isEmpty()) {
    alias["ger"] = "deu";  // German
    alias["fre"] = "fra";  // French
    alias["nld"] = "dut";  // Dutch
    alias["ces"] = "cze";  // Czech
    alias["zho"] = "chi";  // Chinese
    alias["ell"] = "gre";  // Greek
    alias["slk"] = "slo";  // Slovak
    alias["ron"] = "rum";  // Romanian
    alias["mkd"] = "mac";  // Macedonian
    alias["fas"] = "per";  // Persian
  }
  if (alias.contains(c)) return alias[c];

  // Accept if already canonical (in TTCut's known codes list)
  if (languageCodes().contains(c)) return c;

  return QString();
}
```

- [ ] **Step 3: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: clean build, no warnings.

- [ ] **Step 4: Quick manual verification via temporary debug**

Temporarily add in `gui/ttcutmainwindow.cpp` constructor (after `qRegisterMetaType`):

```cpp
qDebug() << "normalize de:"  << TTCut::normalizeLangCode("de");
qDebug() << "normalize DEU:" << TTCut::normalizeLangCode("DEU");
qDebug() << "normalize ger:" << TTCut::normalizeLangCode("ger");
qDebug() << "normalize fre:" << TTCut::normalizeLangCode("fre");
qDebug() << "normalize xx:"  << TTCut::normalizeLangCode("xx");
qDebug() << "normalize empty:" << TTCut::normalizeLangCode("");
```

Run TTCut-ng briefly, verify output:
```
normalize de:  "deu"
normalize DEU: "deu"
normalize ger: "deu"
normalize fre: "fra"
normalize xx:  ""
normalize empty: ""
```

Remove the debug lines after verification.

- [ ] **Step 5: Commit**

```bash
git add common/ttcut.h common/ttcut.cpp
git commit -m "Add TTCut::normalizeLangCode() with alias table for language variants"
```

---

### Task 2: Add audioLanguagePreference global + QSettings persistence

**Files:**
- Modify: `common/ttcut.h`
- Modify: `common/ttcut.cpp`
- Modify: `gui/ttcutsettings.cpp`

- [ ] **Step 1: Add declaration**

In `common/ttcut.h`, in the `Common options` section (near `burstThresholdDb`, around line 170):

```cpp
   static int burstThresholdDb;      // dB RMS threshold for burst detection (-30 default, 0=disabled)
   static bool normalizeAcmod;       // Re-encode AC3 frames at cuts when acmod changes (default: true)
   static QStringList audioLanguagePreference;  // e.g. {"deu","eng"}, empty = use system locale
```

- [ ] **Step 2: Add definition**

In `common/ttcut.cpp`, near `normalizeAcmod` definition (around line 170):

```cpp
QStringList TTCut::audioLanguagePreference = QStringList();
```

- [ ] **Step 3: Add QSettings read**

In `gui/ttcutsettings.cpp`, in `readSettings()` in the `/Common` block (around line 73), after `quickJumpIntervalSec`:

```cpp
  TTCut::audioLanguagePreference = value("AudioLanguagePreference/", TTCut::audioLanguagePreference).toStringList();
```

- [ ] **Step 4: Add QSettings write**

In `gui/ttcutsettings.cpp`, in `writeSettings()` in the `/Common` block (around line 260), after `quickJumpIntervalSec`:

```cpp
  setValue("AudioLanguagePreference/", TTCut::audioLanguagePreference);
```

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add common/ttcut.h common/ttcut.cpp gui/ttcutsettings.cpp
git commit -m "Add TTCut::audioLanguagePreference global with QSettings persistence"
```

---

### Task 3: Update TTAudioItem sort logic

**Files:**
- Modify: `data/ttaudiolist.cpp`

- [ ] **Step 1: Replace operator< implementation**

In `data/ttaudiolist.cpp`, replace the existing `TTAudioItem::operator<` (around line 119):

```cpp
bool TTAudioItem::operator<(const TTAudioItem& item) const
{
  // Primary: AC3 before other codecs
  bool thisIsAC3  = audioStream->fileExtension().toLower() == "ac3";
  bool otherIsAC3 = item.audioStream->fileExtension().toLower() == "ac3";
  if (thisIsAC3 != otherIsAC3) return thisIsAC3;

  // Secondary: Language priority
  // - If preference list is set: index in list (lower = higher priority)
  // - If empty: system locale language = priority 0, others = lowest
  // Not in list / no match = INT_MAX
  auto languagePriority = [](const QString& lang) -> int {
    if (TTCut::audioLanguagePreference.isEmpty()) {
      QString localeLang = TTCut::iso639_1to2(QLocale::system().name().left(2));
      return (lang == localeLang) ? 0 : INT_MAX;
    }
    int idx = TTCut::audioLanguagePreference.indexOf(lang);
    return (idx >= 0) ? idx : INT_MAX;
  };

  int thisPrio  = languagePriority(mLanguage);
  int otherPrio = languagePriority(item.mLanguage);
  if (thisPrio != otherPrio) return thisPrio < otherPrio;

  // Tertiary: Original discovery order
  return mOrder < item.mOrder;
}
```

- [ ] **Step 2: Add `<climits>` include**

At the top of `data/ttaudiolist.cpp`, add after the existing includes:

```cpp
#include <climits>  // INT_MAX
```

(Only add if not already present.)

- [ ] **Step 3: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 4: Quick manual verification**

Run TTCut-ng with the test video:
```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng /media/Daten/Video_Tmp/ProjectX_Temp/06x20_-_Dead_Presidents.m2v
```

With empty preference list (QSettings default), deu should still come first (system locale = de_DE).

- [ ] **Step 5: Commit**

```bash
git add data/ttaudiolist.cpp
git commit -m "Use audio language preference list for sort order"
```

---

### Task 4: Add UI input field in settings dialog

**Files:**
- Modify: `gui/ttcutsettingscommon.h`
- Modify: `gui/ttcutsettingscommon.cpp`

- [ ] **Step 1: Add QLineEdit member**

In `gui/ttcutsettingscommon.h`, add `QLineEdit` include and new member:

```cpp
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>

// ...

  private:
    QSpinBox*   sbBurstThreshold;
    QCheckBox*  cbNormalizeAcmod;
    QSpinBox*   sbQuickJumpInterval;
    QSpinBox*   sbClusterGap;
    QSpinBox*   sbClusterOffset;
    QLineEdit*  leAudioLangPref;
```

- [ ] **Step 2: Create widget in constructor**

In `gui/ttcutsettingscommon.cpp`, add to the constructor after the `cbNormalizeAcmod` block (around line 75):

```cpp
  // Audio language preference
  leAudioLangPref = new QLineEdit(this);
  leAudioLangPref->setPlaceholderText(tr("e.g. deu,eng,fra"));
  leAudioLangPref->setToolTip(tr(
      "Comma-separated audio language codes (2- or 3-letter). "
      "Empty = use system locale. Unknown codes are silently dropped."));
  QLabel* lblAudioLangPref = new QLabel(tr("Audio language preference"), this);
  if (gl) {
    int rowLang = gl->rowCount();
    gl->addWidget(lblAudioLangPref, rowLang, 0);
    gl->addWidget(leAudioLangPref, rowLang, 1, 1, 2);
  }
```

Add `#include <QLineEdit>` at the top of the file if not already present.

- [ ] **Step 3: Read value in setTabData**

In `gui/ttcutsettingscommon.cpp`, `setTabData()`, after the `sbClusterOffset->setValue(...)` line:

```cpp
  // Audio language preference
  leAudioLangPref->setText(TTCut::audioLanguagePreference.join(","));
```

- [ ] **Step 4: Write value in getTabData with normalization**

In `gui/ttcutsettingscommon.cpp`, `getTabData()`, after the `sbClusterOffset` block:

```cpp
  // Audio language preference — parse, normalize, drop empties/unknowns
  TTCut::audioLanguagePreference.clear();
  QStringList rawEntries = leAudioLangPref->text().split(',', Qt::SkipEmptyParts);
  for (const QString& raw : rawEntries) {
    QString normalized = TTCut::normalizeLangCode(raw);
    if (!normalized.isEmpty()) {
      TTCut::audioLanguagePreference.append(normalized);
    }
  }
```

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add gui/ttcutsettingscommon.h gui/ttcutsettingscommon.cpp
git commit -m "Add audio language preference input field in settings dialog"
```

---

### Task 5: Manual test

- [ ] **Step 1: Test round-trip**

1. Start TTCut-ng, open Einstellungen → Allgemein
2. Audio-Sprachpräferenz Feld: leer → OK → Video öffnen → deu kommt zuerst (Locale-Fallback)
3. Einstellungen → Feld auf `eng,deu` setzen → OK → Projekt schließen (File → New) → Video wieder öffnen → eng sollte jetzt zuerst kommen
4. Einstellungen öffnen → Feld sollte `eng,deu` enthalten
5. Feld auf `de, DEU, fre, XX` setzen → OK → wieder öffnen → Feld zeigt `deu,fra` (normalisiert)

- [ ] **Step 2: Update TODO**

In `TODO.md`, add to the Completed section at the end (around line 224):

```markdown
- [x] Audio language preference list in settings (replaces hardcoded system-locale sort)
```

Add a new entry in "Medium Priority" for the dialog restructure:

```markdown
- **Einstellungsdialog neu strukturieren**
  - Der Allgemein-Tab wird zunehmend überladen (Navigation, Preview, Search, Audio, Defect Grouping, Language Preference ...)
  - Logische Gruppierung in Unter-Sektionen oder mehrere Tabs
  - Ziel: Bessere Übersicht, schnelleres Finden relevanter Einstellungen
```

- [ ] **Step 3: Commit**

```bash
git add TODO.md
git commit -m "Update TODO: audio language preference done, add settings dialog restructure"
```
