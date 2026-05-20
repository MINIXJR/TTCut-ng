/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

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
