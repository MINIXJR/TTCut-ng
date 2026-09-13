/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsencoderdefaults.h"
#include "../common/ttsettings.h"
#include "../common/ttencodernames.h"
#include "ttcombofill.h"

TTCutSettingsEncoderDefaults::TTCutSettingsEncoderDefaults(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);

  // Populate preset combos (MPEG-2 hat kein Preset-Konzept — libavcodec
  // mpeg2video kennt nur global_quality = qscale; Preview-Preset wurde
  // nach Settings/Suche & Vorschau verschoben).
  for (QComboBox* cb : { cbH264Preset, cbH265Preset })
    ttFillCombo(cb, TTEncoderNames::kPresets, TTEncoderNames::kPresetCount);

  populateH264Profiles();
  populateH265Profiles();

  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutSettingsEncoderDefaults::resetToDefaults);
}

void TTCutSettingsEncoderDefaults::resetToDefaults()
{
  // Compile-time defaults — must match common/ttsettings.h
  // (mEncoderMode, mMpeg2Crf, mH264Preset/Crf/Profile, mH265Preset/Crf/Profile).
  cbEncodingMode->setChecked(true);
  sbMpeg2Crf->setValue(2);
  cbH264Preset->setCurrentIndex(4);   // fast
  sbH264Crf->setValue(18);
  cbH264Profile->setCurrentIndex(2);  // high
  cbH265Preset->setCurrentIndex(4);   // fast
  sbH265Crf->setValue(20);
  cbH265Profile->setCurrentIndex(0);  // main
}

TTCutSettingsEncoderDefaults::~TTCutSettingsEncoderDefaults() {}

void TTCutSettingsEncoderDefaults::populateH264Profiles()
{
  // Same table the Smart Cut engine indexes with the stored profile.
  ttFillCombo(cbH264Profile, TTEncoderNames::kH264Profiles, TTEncoderNames::kH264ProfileCount);
}

void TTCutSettingsEncoderDefaults::populateH265Profiles()
{
  // Same table the Smart Cut engine indexes with the stored profile.
  ttFillCombo(cbH265Profile, TTEncoderNames::kH265Profiles, TTEncoderNames::kH265ProfileCount);
}

void TTCutSettingsEncoderDefaults::setTabData()
{
  const TTSettings* s = TTSettings::instance();

  cbEncodingMode->setChecked(s->encoderMode());

  // MPEG-2: nur Qualität (qscale 2-31) — Preset+Profile wirken nicht
  sbMpeg2Crf->setValue(qBound(sbMpeg2Crf->minimum(), s->mpeg2Crf(), sbMpeg2Crf->maximum()));

  // H.264
  cbH264Preset->setCurrentIndex(qBound(0, s->h264Preset(), TTEncoderNames::kPresetCount - 1));
  sbH264Crf->setValue(qBound(sbH264Crf->minimum(), s->h264Crf(), sbH264Crf->maximum()));
  cbH264Profile->setCurrentIndex(qBound(0, s->h264Profile(), cbH264Profile->count() - 1));

  // H.265
  cbH265Preset->setCurrentIndex(qBound(0, s->h265Preset(), TTEncoderNames::kPresetCount - 1));
  sbH265Crf->setValue(qBound(sbH265Crf->minimum(), s->h265Crf(), sbH265Crf->maximum()));
  cbH265Profile->setCurrentIndex(qBound(0, s->h265Profile(), cbH265Profile->count() - 1));
}

void TTCutSettingsEncoderDefaults::saveTabData()
{
  TTSettings* s = TTSettings::instance();

  s->setEncoderMode(cbEncodingMode->isChecked());

  // MPEG-2: nur Crf — Preset+Profile entfallen
  s->setMpeg2Crf(sbMpeg2Crf->value());

  // H.264
  s->setH264Preset(cbH264Preset->currentIndex());
  s->setH264Crf(sbH264Crf->value());
  s->setH264Profile(cbH264Profile->currentIndex());

  // H.265
  s->setH265Preset(cbH265Preset->currentIndex());
  s->setH265Crf(sbH265Crf->value());
  s->setH265Profile(cbH265Profile->currentIndex());
}
