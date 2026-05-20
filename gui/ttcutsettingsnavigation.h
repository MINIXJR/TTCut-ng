/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

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
