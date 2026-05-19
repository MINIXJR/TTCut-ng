#include "ttcutsettingsnavigation.h"
#include "../common/ttsettings.h"

TTCutSettingsNavigation::TTCutSettingsNavigation(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutSettingsNavigation::resetToDefaults);
}

TTCutSettingsNavigation::~TTCutSettingsNavigation() {}

void TTCutSettingsNavigation::resetToDefaults()
{
  // Compile-time defaults — must match the member initialisers in
  // common/ttsettings.h (mStep*, mFastSlider, mQuickJumpIntervalSec).
  sbArrowKeyPlacement->setValue(1);
  sbSliderClickPlacement->setValue(40);
  sbPgUpDown->setValue(80);
  sbAltDistance->setValue(100);
  sbCtrlDistance->setValue(200);
  sbMouseWheel->setValue(120);
  sbQuickJumpInterval->setValue(30);
  cbQuickSearch->setChecked(false);
}

void TTCutSettingsNavigation::setTabData()
{
  sbArrowKeyPlacement->setValue(TTSettings::instance()->stepArrowKeys());
  sbSliderClickPlacement->setValue(TTSettings::instance()->stepSliderClick());
  sbPgUpDown->setValue(TTSettings::instance()->stepPgUpDown());
  sbAltDistance->setValue(TTSettings::instance()->stepPlusAlt());
  sbCtrlDistance->setValue(TTSettings::instance()->stepPlusCtrl());
  sbMouseWheel->setValue(TTSettings::instance()->stepMouseWheel());
  sbQuickJumpInterval->setValue(TTSettings::instance()->quickJumpIntervalSec());
  cbQuickSearch->setChecked(TTSettings::instance()->fastSlider());
}

void TTCutSettingsNavigation::saveTabData()
{
  TTSettings::instance()->setStepArrowKeys(sbArrowKeyPlacement->value());
  TTSettings::instance()->setStepSliderClick(sbSliderClickPlacement->value());
  TTSettings::instance()->setStepPgUpDown(sbPgUpDown->value());
  TTSettings::instance()->setStepPlusAlt(sbAltDistance->value());
  TTSettings::instance()->setStepPlusCtrl(sbCtrlDistance->value());
  TTSettings::instance()->setStepMouseWheel(sbMouseWheel->value());
  TTSettings::instance()->setQuickJumpIntervalSec(sbQuickJumpInterval->value());
  TTSettings::instance()->setFastSlider(cbQuickSearch->isChecked());
}
