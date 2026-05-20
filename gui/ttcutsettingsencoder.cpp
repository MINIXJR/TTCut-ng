/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSENCODER
// ----------------------------------------------------------------------------

#include "ttcutsettingsencoder.h"

#include "../common/ttcut.h"
#include "../common/ttsettings.h"


TTCutSettingsEncoder::TTCutSettingsEncoder(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  // Initialize combo boxes
  initCodecList();
  initPresetList();
  initPreviewPresetList();
  updateProfileList();

  // Connect signals
  connect(cbCodec, qOverload<int>(&QComboBox::currentIndexChanged), this, &TTCutSettingsEncoder::onCodecChanged);
  connect(cbEncodingMode, &QCheckBox::stateChanged, this, &TTCutSettingsEncoder::onEncodingModeChanged);
}

void TTCutSettingsEncoder::setTitle(__attribute__((unused))const QString& title)
{
}

void TTCutSettingsEncoder::initCodecList()
{
  cbCodec->clear();
  cbCodec->insertItem(0, "MPEG-2");
  cbCodec->insertItem(1, "H.264/AVC");
  cbCodec->insertItem(2, "H.265/HEVC");
}

// libx264 / libx265 share the same speed-preset name set.
static void populatePresetCombo(QComboBox* combo)
{
  static const char* const kPresets[] = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium", "slow", "slower", "veryslow"
  };
  combo->clear();
  for (int i = 0; i < int(sizeof(kPresets)/sizeof(kPresets[0])); ++i)
    combo->insertItem(i, QString::fromLatin1(kPresets[i]));
}

void TTCutSettingsEncoder::initPresetList()
{
  populatePresetCombo(cbPreset);
}

void TTCutSettingsEncoder::initPreviewPresetList()
{
  populatePresetCombo(cbPreviewPreset);
}

void TTCutSettingsEncoder::updateProfileList()
{
  int codec = cbCodec->currentIndex();
  cbProfile->clear();

  // Bei MPEG-2 sind Preset und Profile nicht wirksam (libavcodec mpeg2video
  // kennt kein Preset; Profile auto-detected). Komplette Zeilen verstecken,
  // damit der Override-Modus im Cut-Dialog nicht suggeriert dass etwas
  // wählbar wäre was nichts bewirkt.
  bool isMpeg2 = (codec == 0);
  cbPreset->setVisible(!isMpeg2);
  lblPreset->setVisible(!isMpeg2);
  cbProfile->setVisible(!isMpeg2);
  lblProfile->setVisible(!isMpeg2);

  switch (codec) {
    case 0:  // MPEG-2 — keine Items nötig (versteckt)
      break;

    case 1:  // H.264
      cbProfile->insertItem(0, "baseline");
      cbProfile->insertItem(1, "main");
      cbProfile->insertItem(2, "high");
      cbProfile->insertItem(3, "high10");
      cbProfile->insertItem(4, "high422");
      cbProfile->insertItem(5, "high444");
      cbProfile->setEnabled(true);
      break;

    case 2:  // H.265
      cbProfile->insertItem(0, "main");
      cbProfile->insertItem(1, "main10");
      cbProfile->insertItem(2, "main12");
      cbProfile->insertItem(3, "main422-10");
      cbProfile->insertItem(4, "main444-10");
      cbProfile->setEnabled(true);
      break;
  }
}

void TTCutSettingsEncoder::updateQualityUI(int codec)
{
  // Block signals to prevent recursive updates
  slCrf->blockSignals(true);
  sbCrf->blockSignals(true);

  switch (codec) {
    case 0:  // MPEG-2 uses qscale (2-31, lower = better)
      lblCrf->setText(tr("Quality (qscale):"));
      slCrf->setMinimum(2);
      slCrf->setMaximum(31);
      sbCrf->setMinimum(2);
      sbCrf->setMaximum(31);
      lblCrfInfo->setText(tr("MPEG-2 quality scale: 2-31 (lower = better quality, larger file). Typical: 2-6, default: 2"));
      slCrf->setToolTip(tr("MPEG-2 qscale: 2 = best quality (default), 31 = worst quality"));
      sbCrf->setToolTip(tr("MPEG-2 qscale: 2 = best quality (default), 31 = worst quality"));
      break;

    case 1:  // H.264 uses CRF (0-51, lower = better)
      lblCrf->setText(tr("Quality (CRF):"));
      slCrf->setMinimum(0);
      slCrf->setMaximum(51);
      sbCrf->setMinimum(0);
      sbCrf->setMaximum(51);
      lblCrfInfo->setText(tr("H.264 CRF: 0-51 (lower = better quality, larger file). Typical: 18-28, default: 18"));
      slCrf->setToolTip(tr("H.264 CRF: 0 = lossless, 18 = default (visually lossless), 51 = worst quality"));
      sbCrf->setToolTip(tr("H.264 CRF: 0 = lossless, 18 = default (visually lossless), 51 = worst quality"));
      break;

    case 2:  // H.265 uses CRF (0-51, lower = better, but values ~6 higher than H.264)
      lblCrf->setText(tr("Quality (CRF):"));
      slCrf->setMinimum(0);
      slCrf->setMaximum(51);
      sbCrf->setMinimum(0);
      sbCrf->setMaximum(51);
      lblCrfInfo->setText(tr("H.265 CRF: 0-51 (lower = better quality). Typical: 20-34, default: 20"));
      slCrf->setToolTip(tr("H.265 CRF: 0 = lossless, 20 = default (visually lossless), 51 = worst quality"));
      sbCrf->setToolTip(tr("H.265 CRF: 0 = lossless, 20 = default (visually lossless), 51 = worst quality"));
      break;
  }

  slCrf->blockSignals(false);
  sbCrf->blockSignals(false);
}

void TTCutSettingsEncoder::setTabData()
{
  TTSettings* s = TTSettings::instance();
  cbEncodingMode->setChecked(s->encoderMode());
  cbCodec->setCurrentIndex(s->encoderCodec());

  // Update quality UI (label, range, tooltip) based on current codec
  updateQualityUI(s->encoderCodec());

  cbPreset->setCurrentIndex(s->encoderPreset());
  slCrf->setValue(s->encoderCrf());
  sbCrf->setValue(s->encoderCrf());

  updateProfileList();
  cbProfile->setCurrentIndex(s->encoderProfile());

  // Preview preset
  cbPreviewPreset->setCurrentIndex(s->previewPreset());

  // Enable/disable codec settings based on encoder mode
  gbCodecSettings->setEnabled(s->encoderMode());
}

void TTCutSettingsEncoder::getTabData()
{
  TTSettings* s = TTSettings::instance();
  s->setEncoderMode(cbEncodingMode->isChecked());
  s->setEncoderCodec(cbCodec->currentIndex());

  // Save current UI values into the transient working set ONLY. The
  // codec-specific App-Defaults (Mpeg2Crf, H264Preset/Crf/Profile,
  // H265Preset/Crf/Profile) live in the Settings dialog and must not be
  // mutated by a per-cut override.
  saveCurrentCodecSettings(cbCodec->currentIndex());

  // Preview-Preset is an App-Default (lives in Settings → Suche & Vorschau).
  // The cut dialog hides gbPreviewSettings in Override mode, so any value
  // read from cbPreviewPreset here is meaningless and used to corrupt the
  // App-Default with the default index.
}

// Per-cut override: copy the encoder UI's preset/crf/profile values into the
// transient working set (encoderPreset/Crf/Profile). The codec-specific
// App-Defaults are deliberately NOT written — they belong to the Settings
// dialog. The `codec` parameter is unused for transient writes (kept for
// signature compatibility with onCodecChanged callers).
void TTCutSettingsEncoder::saveCurrentCodecSettings(int /*codec*/)
{
  int preset = cbPreset->currentIndex();
  int crf = sbCrf->value();
  int profile = cbProfile->currentIndex();

  TTSettings* s = TTSettings::instance();
  s->setEncoderPreset(preset);
  s->setEncoderCrf(crf);
  s->setEncoderProfile(profile);
}

void TTCutSettingsEncoder::loadCodecSettings(int codec)
{
  int preset, crf, profile;

  TTSettings* s = TTSettings::instance();
  switch (codec) {
    case 0:  // MPEG-2: Preset/Profile entfallen — Defaults für UI-Konsistenz
      preset  = 4;  // "fast" (UI shows preset list aber wirkt nicht für MPEG-2)
      crf     = s->mpeg2Crf();
      profile = 0;  // "Main Profile" (auto-detected im Cut)
      break;
    case 1:  // H.264
      preset  = s->h264Preset();
      crf     = s->h264Crf();
      profile = s->h264Profile();
      break;
    case 2:  // H.265
    default:
      preset  = s->h265Preset();
      crf     = s->h265Crf();
      profile = s->h265Profile();
      break;
  }

  // Update quality UI (label, range, tooltip) before setting value
  updateQualityUI(codec);

  // Update UI values
  cbPreset->setCurrentIndex(preset);
  slCrf->setValue(crf);
  sbCrf->setValue(crf);

  // Update profile list first, then set profile
  updateProfileList();
  if (profile < cbProfile->count()) {
    cbProfile->setCurrentIndex(profile);
  }
}

void TTCutSettingsEncoder::onCodecChanged(int newCodec)
{
  // Save settings of the previous codec before switching.
  // The previous codec is stored in TTSettings::encoderCodec().
  TTSettings* s = TTSettings::instance();
  int oldCodec = s->encoderCodec();

  if (oldCodec != newCodec) {
    // Save current UI values to the codec we are leaving
    saveCurrentCodecSettings(oldCodec);

    // Update current codec
    s->setEncoderCodec(newCodec);

    // Load settings for the new codec
    loadCodecSettings(newCodec);
  }

  // Notify other tabs about codec change (for muxer visibility)
  emit codecChanged(newCodec);
}

void TTCutSettingsEncoder::onEncodingModeChanged(int state)
{
  gbCodecSettings->setEnabled(state == Qt::Checked);
}

void TTCutSettingsEncoder::setMode(Mode m)
{
  bool isOverride = (m == Override);
  cbCodec->setEnabled(!isOverride);
  // Preview-Preset ist eine App-Default-Entscheidung — irrelevant per cut.
  // Komplette GroupBox verstecken, sonst zeigt der Cut-Dialog einen leeren
  // Block mit nur erklärendem Text.
  gbPreviewSettings->setVisible(!isOverride);
}
