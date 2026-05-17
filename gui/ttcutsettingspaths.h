#ifndef TTCUTSETTINGSPATHS_H
#define TTCUTSETTINGSPATHS_H

#include "ui_ttcutsettingspaths.h"
#include <QGroupBox>

class TTCutSettingsPaths : public QGroupBox, private Ui_TTCutSettingsPaths
{
    Q_OBJECT
public:
    explicit TTCutSettingsPaths(QWidget* parent = nullptr);
    ~TTCutSettingsPaths();
    void setTabData();
    void saveTabData();

private slots:
    void onLogfileOpen();
};

#endif
