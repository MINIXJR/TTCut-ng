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

#include "tttaskprogress.h"

#include "../common/ttthreadtask.h"

#include <QDebug>

/**
 * Constructor
 */
TTTaskProgress::TTTaskProgress(QWidget* parent, TTThreadTask* task)
    : QFrame(parent)
{
  setupUi( this );

  progressBar->setMinimum(0);
  progressBar->setMaximum(100);

  mpTask = task;
}

/**
 * Destructor
 */
TTTaskProgress::~TTTaskProgress()
{

}

/**
 * Set progress to complete
 */
void TTTaskProgress::onTaskFinished(const QString& msg)
{
  lblAction->setText(msg);
  lblPercent->setText(tr("completed"));
  progressBar->setValue(progressBar->maximum());
}

/**
 * Refresh progress value
 */
void TTTaskProgress::onRefreshProgress(const QString& msg)
{
  lblAction->setText(msg);
  lblPercent->setText(QString("%1%").arg(mpTask->processValue()));
  progressBar->setValue(mpTask->processValue());
}


