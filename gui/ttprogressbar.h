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
// TTPROGRESSBAR
// ----------------------------------------------------------------------------

#ifndef TTPROGRESSBAR_H
#define TTPROGRESSBAR_H

#include <QDialog>
#include <QDateTime>
#include <QUuid>
#include <QHash>

#include "ui_ttprogressform.h"
#include "../common/ttcut.h"

class TTThreadTask;
class TTTaskProgress;
class QCloseEvent;

class TTProgressBar : public QDialog, Ui::TTProgressForm
{
  Q_OBJECT

  public:
    TTProgressBar(QWidget* parent = 0);
    ~TTProgressBar();

    void setActionText( QString action );
    void showBar();
    void hideBar();

    public slots:
      void onDetailsStateChanged(int);
      void onBtnCancelClicked();
      void onSetProgress(TTThreadTask* task, int state, const QString& msg, int totalProgress, QTime totalTime);

    protected:
      void closeEvent(QCloseEvent* event) override;

    private:
      void addTaskProgress(TTThreadTask* task);
      void setTotalSteps(quint64  t_steps, int r_int=0);
      void setTotalProgress(int progress, QTime time);
      void setTaskProgress(TTThreadTask* task, const QString& msg);
      void setTaskFinished(TTThreadTask* task, const QString& msg);

      void resetProgress();

      void updateProgressBar();

  signals:
    void cancel();

  private:
    QHash<QUuid, TTTaskProgress*>* taskProgressHash;
    int            normTotalSteps;
    bool           isBlocking;

    // Re-entrancy guard for closeEvent() -> onBtnCancelClicked(): makes sure
    // the cancel() signal fires only once per user action even if
    // onBtnCancelClicked()'s own path ever ends up closing the window too.
    bool           mClosing;
};
#endif // TTPROGRESSBAR_H
