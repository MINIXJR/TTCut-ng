/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSENCODER
// ----------------------------------------------------------------------------

#ifndef TTCUTSETTINGSENCODER_H
#define TTCUTSETTINGSENCODER_H

#include "ui_ttcutsettingsencoder.h"

#include "../common/ttcut.h"


class TTCutSettingsEncoder : public QWidget, Ui::TTCutSettingsEncoder
{
  Q_OBJECT

  public:
    enum Mode { Defaults, Override };

    TTCutSettingsEncoder(QWidget* parent=0);

    void setTitle( const QString& title );
    void setTabData();
    void getTabData();
    void saveTabData() { getTabData(); }  // alias for new Settings-Dialog pattern
    void setMode(Mode m);

  private:
    void initCodecList();
    void initPresetList();
    void initPreviewPresetList();
    void updateProfileList();
    void updateQualityUI(int codec);
    void saveCurrentCodecSettings(int codec);
    void loadCodecSettings(int codec);

  signals:
    void codecChanged(int codecIndex);

  protected slots:
    void onCodecChanged(int index);
    void onEncodingModeChanged(int state);
};

#endif
