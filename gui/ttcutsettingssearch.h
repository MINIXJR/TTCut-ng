#ifndef TTCUTSETTINGSSEARCH_H
#define TTCUTSETTINGSSEARCH_H

#include "ui_ttcutsettingssearch.h"
#include <QGroupBox>

class TTCutSettingsSearch : public QGroupBox, private Ui_TTCutSettingsSearch
{
    Q_OBJECT
public:
    explicit TTCutSettingsSearch(QWidget* parent = nullptr);
    ~TTCutSettingsSearch();
    void setTabData();
    void saveTabData();

private slots:
    void resetToDefaults();
};

#endif
