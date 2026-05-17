#ifndef TTCUTSETTINGSDLG_H
#define TTCUTSETTINGSDLG_H

#include "ui_ttsettingsdialog.h"
#include <QDialog>

class TTCutSettingsNavigation;
class TTCutSettingsSearch;
class TTCutSettingsAudio;
class TTCutSettingsEncoderDefaults;
class TTCutSettingsMuxer;
class TTCutSettingsPaths;
class TTCutSettingsLogging;

class TTSettingsDialog : public QDialog, private Ui_TTSettingsDialog
{
    Q_OBJECT

public:
    explicit TTSettingsDialog(QWidget* parent = nullptr);
    ~TTSettingsDialog();

public slots:
    void accept() override;

private:
    TTCutSettingsNavigation* pageNavigation;
    TTCutSettingsSearch*     pageSearch;
    TTCutSettingsAudio*      pageAudio;
    TTCutSettingsEncoderDefaults* pageEncoder;
    TTCutSettingsMuxer*      pageMuxer;
    TTCutSettingsPaths*      pagePaths;
    TTCutSettingsLogging*    pageLogging;
};

// Legacy compatibility alias — existing call sites use TTCutSettingsDlg
using TTCutSettingsDlg = TTSettingsDialog;

#endif
