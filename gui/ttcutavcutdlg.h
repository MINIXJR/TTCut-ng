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
// TTCUTAVCUTDLG
// ----------------------------------------------------------------------------

#ifndef TTCUTAVCUTDLG_H
#define TTCUTAVCUTDLG_H

#include "ui_avcutdialog.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"

class QString;

// -----------------------------------------------------------------------------
// TTCut A/V cut dialog;
// -----------------------------------------------------------------------------
class TTCutAVCutDlg : public QDialog, Ui::TTCutAVCutDlg
{
  Q_OBJECT

  public:
    TTCutAVCutDlg( QWidget* parent=0, bool audioOnly = false);
    ~TTCutAVCutDlg();

    void setGlobalData();
    void setCommonData();
    void getCommonData();

  protected slots:
    void onDlgStart();
    void onDlgCancel();
    void onDirectoryOpen();

  private slots:
    void updateOutputFilename();
    void onMuxerProgChanged(int index);
    void onCodecChangedForVisibility(int codecIndex);
    // Reset the working set (Mux/Audio + Encoder) back to the App-Defaults
    // from the Settings dialog, then reload the UI. Does not close the
    // dialog — user can still Cancel or change values before clicking Start.
    void onResetDefaults();

  private:
    void populateMuxerProg();
    void populateMuxTarget();
    void updateMuxerVisibility();
    void updateFreeSpaceLine();
    static QString expectedContainerExtension(int container);
    static QString expectedEsExtension(int container, int codec);

  private:
    TTMessageLogger* log;
};

#endif // TTCUTAVCUTDLG_H
