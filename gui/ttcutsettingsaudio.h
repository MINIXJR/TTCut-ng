#ifndef TTCUTSETTINGSAUDIO_H
#define TTCUTSETTINGSAUDIO_H

#include "ui_ttcutsettingsaudio.h"
#include <QGroupBox>

class TTCutSettingsAudio : public QGroupBox, private Ui_TTCutSettingsAudio
{
    Q_OBJECT
public:
    explicit TTCutSettingsAudio(QWidget* parent = nullptr);
    ~TTCutSettingsAudio();
    void setTabData();
    void saveTabData();

private slots:
    void resetToDefaults();
};

#endif
