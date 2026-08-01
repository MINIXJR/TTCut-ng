/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCUTSETTINGSSTREAMPOINTS_H
#define TTCUTSETTINGSSTREAMPOINTS_H

#include "ui_ttcutsettingsstreampoints.h"
#include <QGroupBox>

class TTCutSettingsStreamPoints : public QGroupBox, private Ui_TTCutSettingsStreamPoints
{
    Q_OBJECT
public:
    explicit TTCutSettingsStreamPoints(QWidget* parent = nullptr);
    ~TTCutSettingsStreamPoints();
    void setTabData();
    void saveTabData();

private slots:
    void resetToDefaults();
};

#endif
