#include "ttcutsettingsgeneral.h"
#include "../common/ttsettings.h"

TTCutSettingsGeneral::TTCutSettingsGeneral(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
}

TTCutSettingsGeneral::~TTCutSettingsGeneral() {}

void TTCutSettingsGeneral::setTabData()
{
  cbQuickSearch->setChecked(TTSettings::instance()->fastSlider());
}

void TTCutSettingsGeneral::saveTabData()
{
  TTSettings::instance()->setFastSlider(cbQuickSearch->isChecked());
}
