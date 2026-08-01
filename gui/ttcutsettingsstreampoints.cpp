/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsstreampoints.h"
#include "../common/ttsettings.h"

TTCutSettingsStreamPoints::TTCutSettingsStreamPoints(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);

  // A check box gates its own value fields, label included - wiring only the
  // spin box leaves a live-looking label next to a dead field. Pillarbox is
  // deliberately NOT gated by the aspect check box: that one drives
  // header-based detection (MPEG-2 only), and coupling them made pillarbox
  // detection unreachable for anyone who switched the MPEG-2 option off.
  connect(cbSilence,   &QCheckBox::toggled, sbSilenceThreshold,   &QWidget::setEnabled);
  connect(cbSilence,   &QCheckBox::toggled, laSilenceThreshold,   &QWidget::setEnabled);
  connect(cbSilence,   &QCheckBox::toggled, sbSilenceMinDuration, &QWidget::setEnabled);
  connect(cbSilence,   &QCheckBox::toggled, laSilenceMinDuration, &QWidget::setEnabled);
  connect(cbPillarbox, &QCheckBox::toggled, sbPillarboxThreshold, &QWidget::setEnabled);
  connect(cbPillarbox, &QCheckBox::toggled, laPillarboxThreshold, &QWidget::setEnabled);
  connect(cbPillarbox, &QCheckBox::toggled, sbPillarboxSample,    &QWidget::setEnabled);
  connect(cbPillarbox, &QCheckBox::toggled, laPillarboxSample,    &QWidget::setEnabled);

  connect(btnResetDefaults, &QPushButton::clicked,
          this, &TTCutSettingsStreamPoints::resetToDefaults);
}

TTCutSettingsStreamPoints::~TTCutSettingsStreamPoints() {}

void TTCutSettingsStreamPoints::resetToDefaults()
{
  // Compile-time defaults — must match common/ttsettings.h
  // (mSpDetectSilence/mSpSilenceThresholdDb/mSpSilenceMinDuration/
  // mSpDetectAudioChange/mSpDetectAspectChange/mSpDetectPillarbox/
  // mSpPillarboxThreshold/mSpPillarboxSampleSeconds).
  cbSilence->setChecked(true);
  sbSilenceThreshold->setValue(-75);
  sbSilenceMinDuration->setValue(0.3);
  cbAudioChange->setChecked(true);
  cbAspectChange->setChecked(true);
  cbPillarbox->setChecked(true);
  sbPillarboxThreshold->setValue(20);
  sbPillarboxSample->setValue(1.0);
}

void TTCutSettingsStreamPoints::setTabData()
{
  TTSettings* s = TTSettings::instance();
  cbSilence->setChecked(s->spDetectSilence());
  sbSilenceThreshold->setValue(s->spSilenceThresholdDb());
  sbSilenceMinDuration->setValue(s->spSilenceMinDuration());
  cbAudioChange->setChecked(s->spDetectAudioChange());
  cbAspectChange->setChecked(s->spDetectAspectChange());
  cbPillarbox->setChecked(s->spDetectPillarbox());
  sbPillarboxThreshold->setValue(s->spPillarboxThreshold());
  sbPillarboxSample->setValue(s->spPillarboxSampleSeconds());

  // setChecked() does not emit toggled() when the value is unchanged, so the
  // enabled state of the sub-controls cannot be left to the connections above
  // - on the very first load with a detector switched off, nothing would fire.
  sbSilenceThreshold->setEnabled(cbSilence->isChecked());
  laSilenceThreshold->setEnabled(cbSilence->isChecked());
  sbSilenceMinDuration->setEnabled(cbSilence->isChecked());
  laSilenceMinDuration->setEnabled(cbSilence->isChecked());
  sbPillarboxThreshold->setEnabled(cbPillarbox->isChecked());
  laPillarboxThreshold->setEnabled(cbPillarbox->isChecked());
  sbPillarboxSample->setEnabled(cbPillarbox->isChecked());
  laPillarboxSample->setEnabled(cbPillarbox->isChecked());
}

void TTCutSettingsStreamPoints::saveTabData()
{
  TTSettings* s = TTSettings::instance();
  s->setSpDetectSilence(cbSilence->isChecked());
  s->setSpSilenceThresholdDb(sbSilenceThreshold->value());
  s->setSpSilenceMinDuration(sbSilenceMinDuration->value());
  s->setSpDetectAudioChange(cbAudioChange->isChecked());
  s->setSpDetectAspectChange(cbAspectChange->isChecked());
  s->setSpDetectPillarbox(cbPillarbox->isChecked());
  s->setSpPillarboxThreshold(sbPillarboxThreshold->value());
  s->setSpPillarboxSampleSeconds(sbPillarboxSample->value());
}
