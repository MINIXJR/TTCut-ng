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

#include "ttcutaboutdlg.h"


TTCutAboutDlg::TTCutAboutDlg(QWidget* parent)
    : QDialog(parent)
{

  setupUi( this );
  
  // signals and slot connection
  // ------------------------------------------------------------------
  connect( okButton, &QPushButton::clicked,  this, &TTCutAboutDlg::onDlgClose );
}

TTCutAboutDlg::~TTCutAboutDlg()
{

}

void TTCutAboutDlg::onDlgClose()
{
  done( 0 );
}
