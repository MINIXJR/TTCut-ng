#include "ttcutsettingspaths.h"
#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"
#include <QFileDialog>
#include <QApplication>
#include <QStyle>

TTCutSettingsPaths::TTCutSettingsPaths(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  btnTmpDirOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  btnLogfileOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  connect(btnTmpDirOpen,  &QPushButton::clicked, this, &TTCutSettingsPaths::onTmpDirectoryOpen);
  connect(btnLogfileOpen, &QPushButton::clicked, this, &TTCutSettingsPaths::onLogfileOpen);
}

TTCutSettingsPaths::~TTCutSettingsPaths() {}

void TTCutSettingsPaths::setTabData()
{
  leTempDirectory->setText(TTSettings::instance()->tempDirPath());
  // Show the currently active logfile path (from TTMessageLogger if empty in settings)
  leLogfile->setText(TTSettings::instance()->logFilePath());
}

void TTCutSettingsPaths::saveTabData()
{
  TTSettings::instance()->setTempDirPath(leTempDirectory->text().trimmed());
  TTSettings::instance()->setLogFilePath(leLogfile->text().trimmed());
  // Apply immediately to TTMessageLogger
  TTMessageLogger::getInstance()->setLogFilePath(leLogfile->text().trimmed());
}

void TTCutSettingsPaths::onTmpDirectoryOpen()
{
  QString dir = QFileDialog::getExistingDirectory(this, tr("Tmp-Verzeichnis wählen"),
                                                  leTempDirectory->text());
  if (!dir.isEmpty()) leTempDirectory->setText(dir);
}

void TTCutSettingsPaths::onLogfileOpen()
{
  QString file = QFileDialog::getSaveFileName(this, tr("Logfile wählen"),
                                              leLogfile->text(),
                                              tr("Log-Dateien (*.log);;Alle Dateien (*)"));
  if (!file.isEmpty()) leLogfile->setText(file);
}
