/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCUTSETTINGSAUDIO_H
#define TTCUTSETTINGSAUDIO_H

#include "ui_ttcutsettingsaudio.h"
#include <QGroupBox>

class TTCutSettingsAudio : public QGroupBox, private Ui_TTCutSettingsAudio
{
    Q_OBJECT
public:
    explicit TTCutSettingsAudio(QWidget* parent = nullptr);
    ~TTCutSettingsAudio();
    void setTabData();
    void saveTabData();

private slots:
    void resetToDefaults();
};

#endif
