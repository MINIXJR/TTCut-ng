# Project Settings Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store output, muxing, and encoder settings per-project in `.ttcut` files, restoring global defaults on project close.

**Architecture:** New `<Settings>` XML section in `.ttcut`, serialized/deserialized in `TTCutProjectData`. Codec-specific encoder mapping based on video stream type. On close, `TTCutSettings::readSettings()` restores globals from QSettings.

**Tech Stack:** Qt5 XML DOM, QSettings, TTCut singleton globals

**Spec:** `docs/superpowers/specs/2026-04-11-project-settings-design.md`

---

### Task 1: Write Settings section in project file

**Files:**
- Modify: `data/ttcutprojectdata.h`
- Modify: `data/ttcutprojectdata.cpp`

- [ ] **Step 1: Add serializeSettings declaration**

In `data/ttcutprojectdata.h`, add to private section:

```cpp
void serializeSettings();
```

- [ ] **Step 2: Implement serializeSettings**

In `data/ttcutprojectdata.cpp`, add after `serializeLogoData`:

```cpp
void TTCutProjectData::serializeSettings()
{
  QDomElement root = xmlDocument->documentElement();
  QDomElement settings = xmlDocument->createElement("Settings");
  root.appendChild(settings);

  auto addElement = [&](const QString& name, const QString& value) {
    QDomElement el = xmlDocument->createElement(name);
    settings.appendChild(el);
    el.appendChild(xmlDocument->createTextNode(value));
  };

  // Output
  addElement("CutDirPath",    TTCut::cutDirPath);
  addElement("CutVideoName",  TTCut::cutVideoName);
  addElement("CutAddSuffix",  TTCut::cutAddSuffix ? "true" : "false");

  // Muxing
  addElement("OutputContainer",    QString::number(TTCut::outputContainer));
  addElement("MkvCreateChapters",  TTCut::mkvCreateChapters ? "true" : "false");
  addElement("MkvChapterInterval", QString::number(TTCut::mkvChapterInterval));
  addElement("MuxDeleteES",        TTCut::muxDeleteES ? "true" : "false");

  // Encoder (active codec values)
  addElement("EncoderPreset",  QString::number(TTCut::encoderPreset));
  addElement("EncoderCrf",     QString::number(TTCut::encoderCrf));
  addElement("EncoderProfile", QString::number(TTCut::encoderProfile));
}
```

- [ ] **Step 3: Call serializeSettings from writeXml**

Find where `writeXml()` calls `serializeAVDataItem`, `serializeStreamPoints`, `serializeLogoData`. Add `serializeSettings()` at the end (after logo data).

Look at the existing pattern in `writeXml()` or in the caller — the serialization methods append to the document root.

- [ ] **Step 4: Add #include for TTCut**

In `data/ttcutprojectdata.cpp`, ensure `#include "../common/ttcut.h"` is present (it may already be included transitively).

- [ ] **Step 5: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 6: Commit**

```bash
git add data/ttcutprojectdata.h data/ttcutprojectdata.cpp
git commit -m "Write project settings to .ttcut XML file"
```

---

### Task 2: Read Settings section from project file

**Files:**
- Modify: `data/ttcutprojectdata.h`
- Modify: `data/ttcutprojectdata.cpp`

- [ ] **Step 1: Add parseSettingsSection and deserializeSettings declarations**

In `data/ttcutprojectdata.h`, add to public section:

```cpp
void deserializeSettings();
```

Add to private section:

```cpp
void parseSettingsSection(QDomElement settingsElement);
```

- [ ] **Step 2: Implement deserializeSettings**

In `data/ttcutprojectdata.cpp`:

```cpp
void TTCutProjectData::deserializeSettings()
{
  QDomElement root = xmlDocument->documentElement();
  QDomNodeList settingsList = root.elementsByTagName("Settings");

  if (settingsList.isEmpty()) return;

  parseSettingsSection(settingsList.at(0).toElement());
}
```

- [ ] **Step 3: Implement parseSettingsSection**

```cpp
void TTCutProjectData::parseSettingsSection(QDomElement settingsElement)
{
  QDomNodeList children = settingsElement.childNodes();

  for (int i = 0; i < children.size(); i++) {
    QDomElement el = children.at(i).toElement();
    if (el.isNull()) continue;

    QString name = el.tagName();
    QString value = el.text();

    // Output
    if (name == "CutDirPath")         TTCut::cutDirPath = value;
    else if (name == "CutVideoName")  TTCut::cutVideoName = value;
    else if (name == "CutAddSuffix")  TTCut::cutAddSuffix = (value == "true");

    // Muxing
    else if (name == "OutputContainer")    TTCut::outputContainer = value.toInt();
    else if (name == "MkvCreateChapters")  TTCut::mkvCreateChapters = (value == "true");
    else if (name == "MkvChapterInterval") TTCut::mkvChapterInterval = value.toInt();
    else if (name == "MuxDeleteES")        TTCut::muxDeleteES = (value == "true");

    // Encoder (generic values — mapped to codec-specific in Task 3)
    else if (name == "EncoderPreset")  TTCut::encoderPreset = value.toInt();
    else if (name == "EncoderCrf")     TTCut::encoderCrf = value.toInt();
    else if (name == "EncoderProfile") TTCut::encoderProfile = value.toInt();
  }
}
```

- [ ] **Step 4: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 5: Commit**

```bash
git add data/ttcutprojectdata.h data/ttcutprojectdata.cpp
git commit -m "Parse project settings from .ttcut XML file"
```

---

### Task 3: Apply settings after project load with codec mapping

**Files:**
- Modify: `data/ttavdata.cpp`

- [ ] **Step 1: Call deserializeSettings in onReadProjectFileFinished**

In `data/ttavdata.cpp`, in `onReadProjectFileFinished()` (around line 925-950), after `avDataReloaded` is emitted and `currentAVItemChanged` is emitted, add settings loading.

The settings must be loaded AFTER streams are loaded (so we know the video codec) but BEFORE the project data object is deleted. Add before `delete mpProjectData`:

```cpp
  // Load project-specific settings (output path, encoder, muxer)
  mpProjectData->deserializeSettings();

  // Map generic encoder values to codec-specific fields based on video type
  if (avCount() > 0) {
    TTVideoStream* vs = avItemAt(0)->videoStream();
    if (vs) {
      int codecType = vs->streamType();  // 2=MPEG-2, 5=H.264, 6=H.265
      switch (codecType) {
        case TTAVTypes::mpeg2_demuxed_video:
          TTCut::mpeg2Preset  = TTCut::encoderPreset;
          TTCut::mpeg2Crf     = TTCut::encoderCrf;
          TTCut::mpeg2Profile = TTCut::encoderProfile;
          break;
        case TTAVTypes::h264_video:
          TTCut::h264Preset  = TTCut::encoderPreset;
          TTCut::h264Crf     = TTCut::encoderCrf;
          TTCut::h264Profile = TTCut::encoderProfile;
          break;
        case TTAVTypes::h265_video:
          TTCut::h265Preset  = TTCut::encoderPreset;
          TTCut::h265Crf     = TTCut::encoderCrf;
          TTCut::h265Profile = TTCut::encoderProfile;
          break;
      }
    }
  }
```

**Note:** Check the actual enum values for stream types by reading `avstream/ttavtypes.h`. The names above are guesses — verify them.

- [ ] **Step 2: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Apply project settings after load with codec-specific encoder mapping"
```

---

### Task 4: Restore global settings on project close

**Files:**
- Modify: `gui/ttcutmainwindow.cpp`

- [ ] **Step 1: Restore settings in closeProject**

In `gui/ttcutmainwindow.cpp`, in `closeProject()` (around line 1113), add after `mpAVData->clear()`:

```cpp
  // Restore global settings from QSettings (discard project overrides)
  settings->readSettings();
```

The `settings` variable is `TTCutSettings*` member of `TTCutMainWindow`. `readSettings()` reads all globals from `QSettings` back to their saved values, effectively discarding any project-specific overrides.

- [ ] **Step 2: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add gui/ttcutmainwindow.cpp
git commit -m "Restore global settings from QSettings on project close"
```

---

### Task 5: Manual test and commit

- [ ] **Step 1: Test save round-trip**

1. Start TTCut-ng, open a video
2. Change: output path, CRF, output container, chapter settings
3. Save project as `.ttcut`
4. Close project (File → New)
5. Verify: settings restored to global defaults
6. Re-open the saved project
7. Verify: settings match what was saved in step 2

- [ ] **Step 2: Test backward compatibility**

Open an old `.ttcut` file without `<Settings>` section. Verify it loads without errors and global settings remain unchanged.

- [ ] **Step 3: Inspect saved XML**

Open the `.ttcut` file in a text editor and verify the `<Settings>` section is present with correct values.

- [ ] **Step 4: Update TODO**

Mark "Projektdatei (.prj): Fehlende Einstellungen speichern" as DONE in `TODO.md`.

- [ ] **Step 5: Commit**

```bash
git add TODO.md
git commit -m "Mark project settings persistence as done in TODO"
```
