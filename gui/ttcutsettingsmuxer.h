/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTCUTSETTINGSMUXER_H
#define TTCUTSETTINGSMUXER_H

#include "ui_ttcutsettingsmuxer.h"
#include <QGroupBox>

class TTCutSettingsMuxer : public QGroupBox, private Ui_TTCutSettingsMuxer
{
  Q_OBJECT

  public:
    enum Mode { Defaults, Override };

    explicit TTCutSettingsMuxer(QWidget* parent = nullptr);

    void setTabData();
    void saveTabData();

  private:
    void populateCodecMuxers();
    void populateMpgTarget();
    void populateMpgMode();

  protected slots:
    void onMkvChaptersChanged(int state);

  private slots:
    void resetToDefaults();
};

#endif
