/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

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
