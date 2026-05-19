#ifndef TTCUTSETTINGSNAVIGATION_H
#define TTCUTSETTINGSNAVIGATION_H

#include "ui_ttcutsettingsnavigation.h"
#include <QGroupBox>

class TTCutSettingsNavigation : public QGroupBox, private Ui_TTCutSettingsNavigation
{
    Q_OBJECT
public:
    explicit TTCutSettingsNavigation(QWidget* parent = nullptr);
    ~TTCutSettingsNavigation();
    void setTabData();
    void saveTabData();

private slots:
    // UI-only reset to compile-time defaults. Persistent App-Defaults are
    // only mutated when the user clicks OK (saveTabData()).
    void resetToDefaults();
};

#endif
