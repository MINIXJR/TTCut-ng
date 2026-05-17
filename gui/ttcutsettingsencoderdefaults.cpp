#include "ttcutsettingsencoderdefaults.h"
#include "../common/ttsettings.h"

#include <QComboBox>

static const char* const kPresets[] = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium", "slow", "slower", "veryslow"
};
static constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

TTCutSettingsEncoderDefaults::TTCutSettingsEncoderDefaults(QWidget* parent)
    : QGroupBox(parent)
{
  setupUi(this);

  // Populate all preset combos
  for (QComboBox* cb : { cbMpeg2Preset, cbH264Preset, cbH265Preset, cbPreviewPreset })
    populatePresetCombo(cb);

  populateMpeg2Profiles();
  populateH264Profiles();
  populateH265Profiles();
}

TTCutSettingsEncoderDefaults::~TTCutSettingsEncoderDefaults() {}

void TTCutSettingsEncoderDefaults::populatePresetCombo(QComboBox* cb)
{
  cb->clear();
  for (int i = 0; i < kPresetCount; ++i)
    cb->insertItem(i, QString::fromLatin1(kPresets[i]));
}

void TTCutSettingsEncoderDefaults::populateMpeg2Profiles()
{
  cbMpeg2Profile->clear();
  cbMpeg2Profile->insertItem(0, "main");
  cbMpeg2Profile->insertItem(1, "simple");
}

void TTCutSettingsEncoderDefaults::populateH264Profiles()
{
  cbH264Profile->clear();
  cbH264Profile->insertItem(0, "baseline");
  cbH264Profile->insertItem(1, "main");
  cbH264Profile->insertItem(2, "high");
}

void TTCutSettingsEncoderDefaults::populateH265Profiles()
{
  cbH265Profile->clear();
  cbH265Profile->insertItem(0, "main");
  cbH265Profile->insertItem(1, "main10");
}

void TTCutSettingsEncoderDefaults::setTabData()
{
  TTSettings* s = TTSettings::instance();

  cbEncodingMode->setChecked(s->encoderMode());

  // MPEG-2
  cbMpeg2Preset->setCurrentIndex(qBound(0, s->mpeg2Preset(), kPresetCount - 1));
  sbMpeg2Crf->setValue(qBound(sbMpeg2Crf->minimum(), s->mpeg2Crf(), sbMpeg2Crf->maximum()));
  cbMpeg2Profile->setCurrentIndex(qBound(0, s->mpeg2Profile(), cbMpeg2Profile->count() - 1));

  // H.264
  cbH264Preset->setCurrentIndex(qBound(0, s->h264Preset(), kPresetCount - 1));
  sbH264Crf->setValue(qBound(sbH264Crf->minimum(), s->h264Crf(), sbH264Crf->maximum()));
  cbH264Profile->setCurrentIndex(qBound(0, s->h264Profile(), cbH264Profile->count() - 1));

  // H.265
  cbH265Preset->setCurrentIndex(qBound(0, s->h265Preset(), kPresetCount - 1));
  sbH265Crf->setValue(qBound(sbH265Crf->minimum(), s->h265Crf(), sbH265Crf->maximum()));
  cbH265Profile->setCurrentIndex(qBound(0, s->h265Profile(), cbH265Profile->count() - 1));

  // Preview
  cbPreviewPreset->setCurrentIndex(qBound(0, s->previewPreset(), kPresetCount - 1));
}

void TTCutSettingsEncoderDefaults::saveTabData()
{
  TTSettings* s = TTSettings::instance();

  s->setEncoderMode(cbEncodingMode->isChecked());

  // MPEG-2
  s->setMpeg2Preset(cbMpeg2Preset->currentIndex());
  s->setMpeg2Crf(sbMpeg2Crf->value());
  s->setMpeg2Profile(cbMpeg2Profile->currentIndex());

  // H.264
  s->setH264Preset(cbH264Preset->currentIndex());
  s->setH264Crf(sbH264Crf->value());
  s->setH264Profile(cbH264Profile->currentIndex());

  // H.265
  s->setH265Preset(cbH265Preset->currentIndex());
  s->setH265Crf(sbH265Crf->value());
  s->setH265Profile(cbH265Profile->currentIndex());

  // Preview
  s->setPreviewPreset(cbPreviewPreset->currentIndex());
}
