# Audio-Sprachpräferenz-Liste

**Date:** 2026-04-12
**Status:** Design approved

## Overview

Ermöglicht dem User eine kommaseparierte Prioritätsliste für Audio-Sprachen, die die bisherige System-Locale-basierte Sortierung ersetzt. Die AC3-vor-andere-Codecs Regel bleibt erhalten.

## Motivation

Die Audio-Liste sortiert aktuell nach einer hardcodierten Priorität: `AC3 > System-Locale > Discovery-Order`. Das trifft für User, die primär nicht in ihrer Desktop-Sprache konsumieren (z.B. englischsprachige Serien auf einem deutschen Desktop), nicht zu. Eine konfigurierbare Präferenz-Liste löst das.

## UI

Neues `QLineEdit` im Einstellungsdialog → Allgemein-Tab:

```
Audio-Sprachpräferenz: [ deu,eng,fra                    ]
```

- Kommaseparierte Sprachcodes
- Akzeptiert ISO 639-1 (2 Zeichen: `de`), kanonische TTCut-Codes (3 Zeichen: `deu`) und alternative ISO 639-2 Varianten (`ger`, `fre`, ...)
- Default: leer
- Tooltip mit Beispiel: `"z.B. deu,eng,fra — akzeptiert 2- oder 3-Buchstaben Codes. Leer = System-Locale"`
- Beim Speichern: splitten, trimmen, **via `normalizeLangCode()` normalisieren**, ungültige Einträge verwerfen

## Neue globale Einstellung

`common/ttcut.h`:
```cpp
static QStringList audioLanguagePreference;
static QString normalizeLangCode(const QString& code);  // "de"/"ger"/"deu" → "deu"
```

Default für `audioLanguagePreference`: leere Liste.

Persistiert in `QSettings` unter `Common/AudioLanguagePreference` (Gruppe `Common`, Key `AudioLanguagePreference`). Lese/Schreibe-Code in `gui/ttcutsettings.cpp` analog zu bestehenden Common-Einträgen.

## Sprachcode-Normalisierung

`TTCut::normalizeLangCode(const QString& code)` in `common/ttcut.cpp`:

1. Eingabe auf lowercase + trimmed
2. Länge 2 → Lookup in bestehender `iso639_1to2` Map (`de` → `deu`, `en` → `eng`, ...)
3. Länge 3 → Alias-Tabelle prüfen, Rückgabe der kanonischen TTCut-Form:

| Alias (Eingabe) | Kanonisch (TTCut) | Sprache |
|-----------------|-------------------|---------|
| `ger`           | `deu`             | Deutsch |
| `fre`           | `fra`             | Französisch |
| `nld`           | `dut`             | Niederländisch |
| `ces`           | `cze`             | Tschechisch |
| `zho`           | `chi`             | Chinesisch |
| `ell`           | `gre`             | Griechisch |
| `slk`           | `slo`             | Slowakisch |
| `ron`           | `rum`             | Rumänisch |
| `mkd`           | `mac`             | Mazedonisch |
| `fas`           | `per`             | Persisch |

4. Länge 3 und nicht in Alias-Tabelle → Prüfung ob der Code in der Liste der TTCut bekannten Codes (`languageCodes()`) enthalten ist; wenn ja zurückgeben, sonst leerer String
5. Alle anderen Längen → leerer String

Die Normalisierung wird beim **Einlesen des Settings-Feldes** angewendet, nicht zur Vergleichszeit. So ist `TTCut::audioLanguagePreference` intern immer kanonisch.

Beispiel: Eingabe `"de, eng, fre, XX"` → Normalisierung → `["deu", "eng", "fra"]` (`XX` verworfen).

## Sortierlogik

`data/ttaudiolist.cpp`, `TTAudioItem::operator<`:

Neue Priorität:
1. **AC3 vor anderen Codecs** (unverändert)
2. **Sprach-Präferenz** (neu, ersetzt Locale-Check):
   - Wenn `TTCut::audioLanguagePreference` leer ist → Fallback auf System-Locale (bisheriges Verhalten)
   - Sonst: Index in der Präferenz-Liste; `-1` (nicht in Liste) wird als niedrigste Priorität behandelt
3. **Discovery-Order** (unverändert)

### Hilfsfunktion

Eine private `static` Hilfsfunktion in `TTAudioItem` (oder inline im `operator<`):

```cpp
// Returns priority index: 0..N-1 for preference match, INT_MAX if not in list
// or if preference list is empty and language != system locale
static int languagePriority(const QString& lang)
{
    if (TTCut::audioLanguagePreference.isEmpty()) {
        QString localeLang = TTCut::iso639_1to2(QLocale::system().name().left(2));
        return (lang == localeLang) ? 0 : INT_MAX;
    }
    int idx = TTCut::audioLanguagePreference.indexOf(lang);
    return (idx >= 0) ? idx : INT_MAX;
}
```

Der `operator<` nutzt dann:
```cpp
int thisPrio = languagePriority(mLanguage);
int otherPrio = languagePriority(item.mLanguage);
if (thisPrio != otherPrio) return thisPrio < otherPrio;
```

## Beispielverhalten

Material: `show_deu.ac3`, `show_deu.mp2`, `show_eng.ac3`, `show_fra.mp2`, `show_ita.mp2`

**Präferenz `"eng,deu"`:**
1. `show_eng.ac3` — AC3, eng=Index 0
2. `show_deu.ac3` — AC3, deu=Index 1
3. `show_deu.mp2` — MP2, deu=Index 1
4. `show_fra.mp2` — MP2, nicht in Liste, Discovery-Order
5. `show_ita.mp2` — MP2, nicht in Liste, Discovery-Order

**Präferenz leer, Locale `de_DE`:**
1. `show_deu.ac3` — AC3, locale match
2. `show_eng.ac3` — AC3, kein locale match
3. `show_deu.mp2` — MP2, locale match
4. `show_fra.mp2` / `show_ita.mp2` — MP2, Discovery-Order

## Betroffene Dateien

- `common/ttcut.h` — Deklaration `audioLanguagePreference` und `normalizeLangCode()`
- `common/ttcut.cpp` — Definitionen, Alias-Tabelle in `normalizeLangCode()`
- `gui/ttcutsettings.cpp` — Lesen/Schreiben im `Common`-Block, Normalisierung beim Schreiben
- `gui/ttcutsettingscommon.h/.cpp` — Getter/Setter für UI
- `ui/ttcutsettingscommon.ui` — Neues `QLineEdit` mit Label
- `data/ttaudiolist.h` — Private Hilfsfunktion (oder inline im .cpp)
- `data/ttaudiolist.cpp` — Sortierlogik in `operator<` umbauen

## Rückwärtskompatibilität

- Fehlende `QSettings`-Einstellung → leere Liste → bisheriges Locale-basiertes Verhalten
- Kein Änderung am Projektdateiformat — die Präferenz ist global, nicht projektspezifisch

## Separates TODO

Als neuer TODO-Punkt aufnehmen: **"Einstellungsdialog neu strukturieren"** — der Allgemein-Tab wird mit dem neuen Feld zunehmend überladen. Eine logische Gruppierung (Navigation / Sprache / Stream-Points / Logs) wäre sinnvoll.
