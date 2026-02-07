/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingsencoder.cpp                                        */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSENCODER
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsencoder.h"

#include "../common/ttcut.h"


TTCutSettingsEncoder::TTCutSettingsEncoder(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  // Initialize combo boxes
  initCodecList();
  initPresetList();
  updateProfileList();

  // Connect signals
  connect(cbCodec, SIGNAL(currentIndexChanged(int)), SLOT(onCodecChanged(int)));
  connect(cbEncodingMode, SIGNAL(stateChanged(int)), SLOT(onEncodingModeChanged(int)));
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

void TTCutSettingsEncoder::initPresetList()
{
  cbPreset->clear();
  cbPreset->insertItem(0, "ultrafast");
  cbPreset->insertItem(1, "superfast");
  cbPreset->insertItem(2, "veryfast");
  cbPreset->insertItem(3, "faster");
  cbPreset->insertItem(4, "fast");
  cbPreset->insertItem(5, "medium");
  cbPreset->insertItem(6, "slow");
  cbPreset->insertItem(7, "slower");
  cbPreset->insertItem(8, "veryslow");
}

void TTCutSettingsEncoder::updateProfileList()
{
  int codec = cbCodec->currentIndex();
  cbProfile->clear();

  switch (codec) {
    case 0:  // MPEG-2
      cbProfile->insertItem(0, "Main Profile");
      cbProfile->insertItem(1, "Simple Profile");
      cbProfile->insertItem(2, "High Profile");
      cbProfile->setEnabled(false);  // MPEG-2 profile is auto-detected
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
      lblCrfInfo->setText(tr("MPEG-2 quality scale: 2-31 (lower = better quality, larger file). Typical: 2-6"));
      slCrf->setToolTip(tr("MPEG-2 qscale: 2 = best quality, 31 = worst quality"));
      sbCrf->setToolTip(tr("MPEG-2 qscale: 2 = best quality, 31 = worst quality"));
      break;

    case 1:  // H.264 uses CRF (0-51, lower = better)
      lblCrf->setText(tr("Quality (CRF):"));
      slCrf->setMinimum(0);
      slCrf->setMaximum(51);
      sbCrf->setMinimum(0);
      sbCrf->setMaximum(51);
      lblCrfInfo->setText(tr("H.264 CRF: 0-51 (lower = better quality, larger file). Typical: 18-28, default: 23"));
      slCrf->setToolTip(tr("H.264 CRF: 0 = lossless, 23 = default, 51 = worst quality"));
      sbCrf->setToolTip(tr("H.264 CRF: 0 = lossless, 23 = default, 51 = worst quality"));
      break;

    case 2:  // H.265 uses CRF (0-51, lower = better, but values ~6 higher than H.264)
      lblCrf->setText(tr("Quality (CRF):"));
      slCrf->setMinimum(0);
      slCrf->setMaximum(51);
      sbCrf->setMinimum(0);
      sbCrf->setMaximum(51);
      lblCrfInfo->setText(tr("H.265 CRF: 0-51 (lower = better quality). Typical: 24-34, default: 28 (≈ H.264 CRF 23)"));
      slCrf->setToolTip(tr("H.265 CRF: 0 = lossless, 28 = default (similar to H.264 CRF 23), 51 = worst"));
      sbCrf->setToolTip(tr("H.265 CRF: 0 = lossless, 28 = default (similar to H.264 CRF 23), 51 = worst"));
      break;
  }

  slCrf->blockSignals(false);
  sbCrf->blockSignals(false);
}

void TTCutSettingsEncoder::setTabData()
{
  cbEncodingMode->setChecked(TTCut::encoderMode);
  cbCodec->setCurrentIndex(TTCut::encoderCodec);

  // Update quality UI (label, range, tooltip) based on current codec
  updateQualityUI(TTCut::encoderCodec);

  cbPreset->setCurrentIndex(TTCut::encoderPreset);
  slCrf->setValue(TTCut::encoderCrf);
  sbCrf->setValue(TTCut::encoderCrf);

  updateProfileList();
  cbProfile->setCurrentIndex(TTCut::encoderProfile);

  // Enable/disable codec settings based on encoder mode
  gbCodecSettings->setEnabled(TTCut::encoderMode);
}

void TTCutSettingsEncoder::getTabData()
{
  TTCut::encoderMode = cbEncodingMode->isChecked();
  TTCut::encoderCodec = cbCodec->currentIndex();

  // Save current UI values to the current codec's settings
  saveCurrentCodecSettings();
}

void TTCutSettingsEncoder::saveCurrentCodecSettings()
{
  int codec = cbCodec->currentIndex();
  int preset = cbPreset->currentIndex();
  int crf = sbCrf->value();
  int profile = cbProfile->currentIndex();

  switch (codec) {
    case 0:  // MPEG-2
      TTCut::mpeg2Preset = preset;
      TTCut::mpeg2Crf = crf;
      TTCut::mpeg2Profile = profile;
      break;
    case 1:  // H.264
      TTCut::h264Preset = preset;
      TTCut::h264Crf = crf;
      TTCut::h264Profile = profile;
      break;
    case 2:  // H.265
      TTCut::h265Preset = preset;
      TTCut::h265Crf = crf;
      TTCut::h265Profile = profile;
      break;
  }

  // Also update the current working values
  TTCut::encoderPreset = preset;
  TTCut::encoderCrf = crf;
  TTCut::encoderProfile = profile;
}

void TTCutSettingsEncoder::loadCodecSettings(int codec)
{
  int preset, crf, profile;

  switch (codec) {
    case 0:  // MPEG-2
      preset = TTCut::mpeg2Preset;
      crf = TTCut::mpeg2Crf;
      profile = TTCut::mpeg2Profile;
      break;
    case 1:  // H.264
      preset = TTCut::h264Preset;
      crf = TTCut::h264Crf;
      profile = TTCut::h264Profile;
      break;
    case 2:  // H.265
    default:
      preset = TTCut::h265Preset;
      crf = TTCut::h265Crf;
      profile = TTCut::h265Profile;
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
  // Save settings of the previous codec before switching
  // The previous codec is stored in TTCut::encoderCodec
  int oldCodec = TTCut::encoderCodec;

  if (oldCodec != newCodec) {
    // Save current UI values to the old codec
    int preset = cbPreset->currentIndex();
    int crf = sbCrf->value();
    int profile = cbProfile->currentIndex();

    switch (oldCodec) {
      case 0:  // MPEG-2
        TTCut::mpeg2Preset = preset;
        TTCut::mpeg2Crf = crf;
        TTCut::mpeg2Profile = profile;
        break;
      case 1:  // H.264
        TTCut::h264Preset = preset;
        TTCut::h264Crf = crf;
        TTCut::h264Profile = profile;
        break;
      case 2:  // H.265
        TTCut::h265Preset = preset;
        TTCut::h265Crf = crf;
        TTCut::h265Profile = profile;
        break;
    }

    // Update current codec
    TTCut::encoderCodec = newCodec;

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
