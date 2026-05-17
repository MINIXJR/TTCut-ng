#ifndef TTCUTSETTINGSGENERAL_H
#define TTCUTSETTINGSGENERAL_H

#include "ui_ttcutsettingsgeneral.h"
#include <QGroupBox>

class TTCutSettingsGeneral : public QGroupBox, private Ui_TTCutSettingsGeneral
{
    Q_OBJECT

public:
    explicit TTCutSettingsGeneral(QWidget* parent = nullptr);
    ~TTCutSettingsGeneral();

    void setTabData();
    void saveTabData();

private slots:
    void onDirectoryOpen();
};

#endif
