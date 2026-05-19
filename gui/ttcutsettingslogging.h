#ifndef TTCUTSETTINGSLOGGING_H
#define TTCUTSETTINGSLOGGING_H

#include "ui_ttcutsettingslogging.h"
#include <QGroupBox>

class TTCutSettingsLogging : public QGroupBox, private Ui_TTCutSettingsLogging
{
    Q_OBJECT
public:
    explicit TTCutSettingsLogging(QWidget* parent = nullptr);
    ~TTCutSettingsLogging();
    void setTabData();
    void saveTabData();

private slots:
    void onCreateLogStateChanged(int state);
    void resetToDefaults();
};

#endif
