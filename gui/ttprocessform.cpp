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
// TTPROCESSFORM
// ----------------------------------------------------------------------------

#include "ttprocessform.h"


/**
 * Constructor
 */
TTProcessForm::TTProcessForm(QWidget* parent)
  : QDialog(parent)
{
  setupUi(this);

  btnCancel->setAutoDefault(false);
  btnCancel->hide();

  connect(btnCancel, &QPushButton::clicked, this, &TTProcessForm::btnCancelClicked);
}

/**
 * Destructor
 */
TTProcessForm::~TTProcessForm()
{
}

/**
 * Set the process window frame caption
 */
/**
 * Enable the list control for manual scrolling
 */
/**
 * Enable the cancel button
 */
void TTProcessForm::enableButton(bool value)
{
  btnCancel->setEnabled(value);
}

/**
 * Show the cancel button
 */
/**
 * Show the ok button
 */
/**
 * Add a line to the process output
 */
