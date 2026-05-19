#include "ttcutsettingslogging.h"
#include "../common/ttsettings.h"

TTCutSettingsLogging::TTCutSettingsLogging(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
  connect(cbCreateLog, &QCheckBox::stateChanged, this, &TTCutSettingsLogging::onCreateLogStateChanged);
  connect(btnResetDefaults, &QPushButton::clicked, this, &TTCutSettingsLogging::resetToDefaults);
}

void TTCutSettingsLogging::resetToDefaults()
{
  // Compile-time defaults — must match common/ttsettings.h
  // (mCreateLogFile, mLogModeConsole, mLogModeExtended, mLogVideoIndexInfo,
  //  mLogFFmpegDecoder, mLogSmartCut, mLogMkvMux, mLogCutPipeline,
  //  mLogAVStream, mLogUI, mLogLibav).
  cbCreateLog->setChecked(true);
  cbLogConsole->setChecked(false);
  cbLogExtended->setChecked(true);
  cbLogPlusVideoIndex->setChecked(false);
  cbLogPlusFFmpegDecoder->setChecked(false);
  cbLogPlusSmartCut->setChecked(false);
  cbLogPlusMkvMux->setChecked(false);
  cbLogPlusCutPipeline->setChecked(false);
  cbLogPlusAVStream->setChecked(false);
  cbLogPlusUI->setChecked(false);
  cbLogPlusLibav->setChecked(false);
  gbLogPlus->setEnabled(true);
}

TTCutSettingsLogging::~TTCutSettingsLogging() {}

void TTCutSettingsLogging::setTabData()
{
  cbCreateLog->setChecked(TTSettings::instance()->createLogFile());
  cbLogConsole->setChecked(TTSettings::instance()->logModeConsole());
  cbLogExtended->setChecked(TTSettings::instance()->logModeExtended());
  cbLogPlusVideoIndex->setChecked(TTSettings::instance()->logVideoIndexInfo());
  cbLogPlusFFmpegDecoder->setChecked(TTSettings::instance()->logFFmpegDecoder());
  cbLogPlusSmartCut->setChecked(TTSettings::instance()->logSmartCut());
  cbLogPlusMkvMux->setChecked(TTSettings::instance()->logMkvMux());
  cbLogPlusCutPipeline->setChecked(TTSettings::instance()->logCutPipeline());
  cbLogPlusAVStream->setChecked(TTSettings::instance()->logAVStream());
  cbLogPlusUI->setChecked(TTSettings::instance()->logUI());
  cbLogPlusLibav->setChecked(TTSettings::instance()->logLibav());

  gbLogPlus->setEnabled(TTSettings::instance()->createLogFile());
}

void TTCutSettingsLogging::saveTabData()
{
  TTSettings::instance()->setCreateLogFile(cbCreateLog->isChecked());
  TTSettings::instance()->setLogModeConsole(cbLogConsole->isChecked());
  TTSettings::instance()->setLogModeExtended(cbLogExtended->isChecked());
  TTSettings::instance()->setLogVideoIndexInfo(cbLogPlusVideoIndex->isChecked());
  TTSettings::instance()->setLogFFmpegDecoder(cbLogPlusFFmpegDecoder->isChecked());
  TTSettings::instance()->setLogSmartCut(cbLogPlusSmartCut->isChecked());
  TTSettings::instance()->setLogMkvMux(cbLogPlusMkvMux->isChecked());
  TTSettings::instance()->setLogCutPipeline(cbLogPlusCutPipeline->isChecked());
  TTSettings::instance()->setLogAVStream(cbLogPlusAVStream->isChecked());
  TTSettings::instance()->setLogUI(cbLogPlusUI->isChecked());
  TTSettings::instance()->setLogLibav(cbLogPlusLibav->isChecked());
}

void TTCutSettingsLogging::onCreateLogStateChanged(int state)
{
  gbLogPlus->setEnabled(state != Qt::Unchecked);
}
