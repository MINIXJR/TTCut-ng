#include "ttcutsettingsaudio.h"
#include "../common/ttsettings.h"
#include "../common/ttcut.h"

TTCutSettingsAudio::TTCutSettingsAudio(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
}

TTCutSettingsAudio::~TTCutSettingsAudio() {}

void TTCutSettingsAudio::setTabData()
{
  leAudioLangPref->setText(TTSettings::instance()->audioLanguagePreference().join(","));
  cbNormalizeAcmod->setChecked(TTSettings::instance()->normalizeAcmod());
  sbBurstThreshold->setValue(TTSettings::instance()->burstThresholdDb());
}

void TTCutSettingsAudio::saveTabData()
{
  // Parse, normalize, drop empties/unknowns
  QStringList newPrefs;
  const QStringList rawEntries = leAudioLangPref->text().split(',', Qt::SkipEmptyParts);
  for (const QString& raw : rawEntries) {
    QString normalized = TTCut::normalizeLangCode(raw);
    if (!normalized.isEmpty())
      newPrefs.append(normalized);
  }
  TTSettings::instance()->setAudioLanguagePreference(newPrefs);
  TTSettings::instance()->setNormalizeAcmod(cbNormalizeAcmod->isChecked());
  TTSettings::instance()->setBurstThresholdDb(sbBurstThreshold->value());
}
