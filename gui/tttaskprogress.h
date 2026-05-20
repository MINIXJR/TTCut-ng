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
// TTTASKPROGRESS
// ----------------------------------------------------------------------------


#ifndef TTTASKPROGRESS_H
#define TTTASKPROGRESS_H

#include "ui_tttaskprogresswidget.h"
#include "../common/ttcut.h"

class TTThreadTask;

class TTTaskProgress : public QFrame, Ui::TTTaskProgressWidget
{
  Q_OBJECT

  public:
    TTTaskProgress(QWidget* parent, TTThreadTask* task);
    ~TTTaskProgress();

  public slots:
    void onRefreshProgress(const QString& msg);
    void onTaskFinished(const QString& msg);

  private:
    TTThreadTask* mpTask;
};

#endif // TTTASKPROGRESS_H
