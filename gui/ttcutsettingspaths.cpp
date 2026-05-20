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
  btnCutDirOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  btnLogfileOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  connect(btnTmpDirOpen,  &QPushButton::clicked, this, &TTCutSettingsPaths::onTmpDirectoryOpen);
  connect(btnCutDirOpen,  &QPushButton::clicked, this, &TTCutSettingsPaths::onCutDirOpen);
  connect(btnLogfileOpen, &QPushButton::clicked, this, &TTCutSettingsPaths::onLogfileOpen);
}

TTCutSettingsPaths::~TTCutSettingsPaths() {}

void TTCutSettingsPaths::setTabData()
{
  leTempDirectory->setText(TTSettings::instance()->tempDirPath());
  leCutDir->setText(TTSettings::instance()->cutDirPath());
  // Show the currently active logfile path (from TTMessageLogger if empty in settings)
  leLogfile->setText(TTSettings::instance()->logFilePath());
}

void TTCutSettingsPaths::saveTabData()
{
  TTSettings::instance()->setTempDirPath(leTempDirectory->text().trimmed());
  TTSettings::instance()->setCutDirPath(leCutDir->text().trimmed());
  TTSettings::instance()->setLogFilePath(leLogfile->text().trimmed());
  // Apply immediately to TTMessageLogger
  TTMessageLogger::getInstance()->setLogFilePath(leLogfile->text().trimmed());
}

void TTCutSettingsPaths::onTmpDirectoryOpen()
{
  QString dir = QFileDialog::getExistingDirectory(this, tr("Select temporary directory"),
                                                  leTempDirectory->text());
  if (!dir.isEmpty()) leTempDirectory->setText(dir);
}

void TTCutSettingsPaths::onCutDirOpen()
{
  QString dir = QFileDialog::getExistingDirectory(this, tr("Select default output directory"),
                                                  leCutDir->text());
  if (!dir.isEmpty()) leCutDir->setText(dir);
}

void TTCutSettingsPaths::onLogfileOpen()
{
  QString file = QFileDialog::getSaveFileName(this, tr("Select log file"),
                                              leLogfile->text(),
                                              tr("Log files (*.log);;All files (*)"));
  if (!file.isEmpty()) leLogfile->setText(file);
}
