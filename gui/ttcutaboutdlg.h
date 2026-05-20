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
// TTCUTABOUTDLG
// ----------------------------------------------------------------------------


#ifndef TTCUTABOUTDLG_H
#define TTCUTABOUTDLG_H

#include "ui_aboutdlg.h"
#include "../common/ttcut.h"

class TTCutAboutDlg : public QDialog, Ui::TTCutAboutDlg
{
  Q_OBJECT

  public:
    TTCutAboutDlg(QWidget* parent = 0);
    ~TTCutAboutDlg();

  protected slots:
    void onDlgClose();
};

#endif // TTCUTABOUTDLG_H
