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
// TTMESSAGEBOX
// ----------------------------------------------------------------------------

#include "ttmessagebox.h"


TTMessageBox::TTMessageBox(QWidget* parent)
    : QMessageBox(parent)
{

}

TTMessageBox::~TTMessageBox()
{

}

void TTMessageBox::initSaveRequest(const QString& text, const QString& infoText)
{
  setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  setText(text);
  setInformativeText(infoText);
  setDefaultButton(QMessageBox::Save);
}

