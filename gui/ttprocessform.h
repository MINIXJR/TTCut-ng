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


#ifndef TTPROCESSFORM_H
#define TTPROCESSFORM_H

#include "ui_processviewwidget.h"
#include "../common/ttcut.h"

#include <QApplication>

class TTProcessForm : public QDialog, Ui::ProcessViewWidget
{
  Q_OBJECT

  public:
    TTProcessForm(QWidget* parent);
    ~TTProcessForm();

  public:
    void setFrameCaption(QString& caption);
    void enableList(bool value);
    void enableButton(bool value);
    void addLine(const QString& str_line);
    void showCancelButton(bool show);
    void showOkButton(bool show);

  signals:
    void btnCancelClicked();
};

#endif // TTPROCESSFORM_H
