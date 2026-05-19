#ifndef TTCUTSETTINGSENCODERDEFAULTS_H
#define TTCUTSETTINGSENCODERDEFAULTS_H

#include "ui_ttcutsettingsencoderdefaults.h"
#include <QGroupBox>

/**
 * Settings-Dialog-only Encoder-Standards page.
 * Shows all 3 codec groups (MPEG-2, H.264, H.265) simultaneously
 * so all per-codec defaults can be edited at once.
 *
 * Writes the same TTSettings keys as TTCutSettingsEncoder
 * (mpeg2Preset/Crf/Profile, h264Preset/Crf/Profile, h265Preset/Crf/Profile,
 * encoderMode, previewPreset) — the two classes are interchangeable
 * from the persistence perspective.
 */
class TTCutSettingsEncoderDefaults : public QGroupBox,
                                     private Ui_TTCutSettingsEncoderDefaults
{
    Q_OBJECT

public:
    explicit TTCutSettingsEncoderDefaults(QWidget* parent = nullptr);
    ~TTCutSettingsEncoderDefaults();

    void setTabData();
    void saveTabData();

private slots:
    void resetToDefaults();

private:
    void populatePresetCombo(QComboBox* cb);
    void populateMpeg2Profiles();
    void populateH264Profiles();
    void populateH265Profiles();
};

#endif
