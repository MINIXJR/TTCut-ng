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
  btnLogfileOpen->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
  connect(btnLogfileOpen, &QPushButton::clicked, this, &TTCutSettingsPaths::onLogfileOpen);
}

TTCutSettingsPaths::~TTCutSettingsPaths() {}

void TTCutSettingsPaths::setTabData()
{
  // Show the currently active logfile path (from TTMessageLogger if empty in settings)
  const QString stored = TTSettings::instance()->logFilePath();
  leLogfile->setText(stored);
}

void TTCutSettingsPaths::saveTabData()
{
  TTSettings::instance()->setLogFilePath(leLogfile->text().trimmed());
  // Apply immediately to TTMessageLogger
  TTMessageLogger::getInstance()->setLogFilePath(leLogfile->text().trimmed());
}

void TTCutSettingsPaths::onLogfileOpen()
{
  QString file = QFileDialog::getSaveFileName(this, tr("Logfile wählen"),
                                              leLogfile->text(),
                                              tr("Log-Dateien (*.log);;Alle Dateien (*)"));
  if (!file.isEmpty()) leLogfile->setText(file);
}
