/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

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
    void onTmpDirectoryOpen();
    void onCutDirOpen();
    void onLogfileOpen();
};

#endif
