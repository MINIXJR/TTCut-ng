#include "ttcutsettingssearch.h"
#include "../common/ttsettings.h"

static const char* const kPreviewPresets[] = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium", "slow", "slower", "veryslow"
};
static constexpr int kPreviewPresetCount =
    int(sizeof(kPreviewPresets) / sizeof(kPreviewPresets[0]));

TTCutSettingsSearch::TTCutSettingsSearch(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);

  // Preview-Preset wird nur in TTESSmartCut::setPresetOverride() konsumiert
  // (H.264/H.265). MPEG-2 nutzt für Preview-Re-Encoding TTTranscodeProvider
  // mit dem normalen mpeg2Crf. Tooltip stellt das klar; UI bleibt aktiv
  // weil viele User H.264/H.265-Material schneiden.
  cbPreviewPreset->clear();
  for (int i = 0; i < kPreviewPresetCount; ++i)
    cbPreviewPreset->insertItem(i, QString::fromLatin1(kPreviewPresets[i]));
}

TTCutSettingsSearch::~TTCutSettingsSearch() {}

void TTCutSettingsSearch::setTabData()
{
  TTSettings* s = TTSettings::instance();
  sbSearchIntervall->setValue(s->searchLength());
  sbSearchWorkerCount->setValue(s->searchWorkerCount());
  spPreviewLength->setValue(s->cutPreviewSeconds());
  cbPreviewPreset->setCurrentIndex(qBound(0, s->previewPreset(), kPreviewPresetCount - 1));
  sbClusterGap->setValue(s->extraFrameClusterGapSec());
  sbClusterOffset->setValue(s->extraFrameClusterOffsetSec());
}

void TTCutSettingsSearch::saveTabData()
{
  TTSettings* s = TTSettings::instance();
  s->setSearchLength(sbSearchIntervall->value());
  s->setSearchWorkerCount(sbSearchWorkerCount->value());
  s->setCutPreviewSeconds(spPreviewLength->value());
  s->setPreviewPreset(cbPreviewPreset->currentIndex());
  s->setExtraFrameClusterGapSec(sbClusterGap->value());
  s->setExtraFrameClusterOffsetSec(sbClusterOffset->value());
}
