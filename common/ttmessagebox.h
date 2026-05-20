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


#ifndef TTMESSAGEBOX_H
#define TTMESSAGEBOX_H

#include <QMessageBox>

#include "../common/ttcut.h"

class TTMessageBox : public QMessageBox
{
  Q_OBJECT

  public:
    TTMessageBox(QWidget* parent = 0);
    ~TTMessageBox();

    void initSaveRequest(const QString& text, const QString& infoText);
};

#endif // TTMESSAGEBOX_H
