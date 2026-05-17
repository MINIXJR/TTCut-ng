#include "ttcutsettingsgeneral.h"
#include "../common/ttsettings.h"
#include <QFileDialog>
#include <QApplication>
#include <QStyle>

TTCutSettingsGeneral::TTCutSettingsGeneral(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  btnDirOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  connect(btnDirOpen, &QPushButton::clicked, this, &TTCutSettingsGeneral::onDirectoryOpen);
}

TTCutSettingsGeneral::~TTCutSettingsGeneral() {}

void TTCutSettingsGeneral::setTabData()
{
  leTempDirectory->setText(TTSettings::instance()->tempDirPath());
  cbQuickSearch->setChecked(TTSettings::instance()->fastSlider());
}

void TTCutSettingsGeneral::saveTabData()
{
  TTSettings::instance()->setTempDirPath(leTempDirectory->text());
  TTSettings::instance()->setFastSlider(cbQuickSearch->isChecked());
}

void TTCutSettingsGeneral::onDirectoryOpen()
{
  QString dir = QFileDialog::getExistingDirectory(this, tr("Tmp-Verzeichnis wählen"),
                                                   leTempDirectory->text());
  if (!dir.isEmpty()) leTempDirectory->setText(dir);
}
