/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsaudio.h"
#include "../common/ttsettings.h"
#include "../common/ttcut.h"

TTCutSettingsAudio::TTCutSettingsAudio(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutSettingsAudio::resetToDefaults);
}

TTCutSettingsAudio::~TTCutSettingsAudio() {}

void TTCutSettingsAudio::resetToDefaults()
{
  // Compile-time defaults — must match common/ttsettings.h
  // (mNormalizeAcmod, mBurstMinDeltaDb). Audio language preference is
  // intentionally left unchanged — user-curated setting that survives
  // factory resets.
  cbNormalizeAcmod->setChecked(true);
  sbBurstMinDelta->setValue(20);
}

void TTCutSettingsAudio::setTabData()
{
  leAudioLangPref->setText(TTSettings::instance()->audioLanguagePreference().join(","));
  cbNormalizeAcmod->setChecked(TTSettings::instance()->normalizeAcmod());
  sbBurstMinDelta->setValue(TTSettings::instance()->burstMinDeltaDb());
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
  TTSettings::instance()->setBurstMinDeltaDb(sbBurstMinDelta->value());
}
