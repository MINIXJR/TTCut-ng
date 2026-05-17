#include "ttcutsettingssearch.h"
#include "../common/ttsettings.h"

TTCutSettingsSearch::TTCutSettingsSearch(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);
}

TTCutSettingsSearch::~TTCutSettingsSearch() {}

void TTCutSettingsSearch::setTabData()
{
  sbSearchIntervall->setValue(TTSettings::instance()->searchLength());
  sbSearchWorkerCount->setValue(TTSettings::instance()->searchWorkerCount());
  spPreviewLength->setValue(TTSettings::instance()->cutPreviewSeconds());
  sbClusterGap->setValue(TTSettings::instance()->extraFrameClusterGapSec());
  sbClusterOffset->setValue(TTSettings::instance()->extraFrameClusterOffsetSec());
}

void TTCutSettingsSearch::saveTabData()
{
  TTSettings::instance()->setSearchLength(sbSearchIntervall->value());
  TTSettings::instance()->setSearchWorkerCount(sbSearchWorkerCount->value());
  TTSettings::instance()->setCutPreviewSeconds(spPreviewLength->value());
  TTSettings::instance()->setExtraFrameClusterGapSec(sbClusterGap->value());
  TTSettings::instance()->setExtraFrameClusterOffsetSec(sbClusterOffset->value());
}
