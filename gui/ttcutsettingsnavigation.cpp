#include "ttcutsettingsnavigation.h"
#include "../common/ttsettings.h"

TTCutSettingsNavigation::TTCutSettingsNavigation(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
}

TTCutSettingsNavigation::~TTCutSettingsNavigation() {}

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
