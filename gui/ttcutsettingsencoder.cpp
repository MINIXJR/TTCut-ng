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
/* either version 2 of the License, or (at your option) any later version.    */
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

void TTCutSettingsEncoder::setTabData()
{
  cbEncodingMode->setChecked(TTCut::encoderMode);
  cbCodec->setCurrentIndex(TTCut::encoderCodec);
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
  TTCut::encoderPreset = cbPreset->currentIndex();
  TTCut::encoderCrf = sbCrf->value();
  TTCut::encoderProfile = cbProfile->currentIndex();
}

void TTCutSettingsEncoder::onCodecChanged(int index)
{
  Q_UNUSED(index);
  updateProfileList();

  // Reset profile to a sensible default for the new codec
  switch (cbCodec->currentIndex()) {
    case 0:  // MPEG-2
      cbProfile->setCurrentIndex(0);
      break;
    case 1:  // H.264
      cbProfile->setCurrentIndex(2);  // high
      break;
    case 2:  // H.265
      cbProfile->setCurrentIndex(0);  // main
      break;
  }
}

void TTCutSettingsEncoder::onEncodingModeChanged(int state)
{
  gbCodecSettings->setEnabled(state == Qt::Checked);
}
