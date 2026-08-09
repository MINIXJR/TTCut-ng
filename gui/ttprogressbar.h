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
#include <QElapsedTimer>

#include "ui_ttprogressform.h"
#include "../common/ttcut.h"

class TTThreadTask;
class QCloseEvent;
class QTimer;

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
      void onDetailsStateChanged(Qt::CheckState);
      void onBtnCancelClicked();
      void onSetProgress(TTThreadTask* task, int state, const QString& msg, int totalProgress);
      void setRemaining(const QString& text);

    private slots:
      void onTick();

    protected:
      void closeEvent(QCloseEvent* event) override;
      void reject() override;

    private:
      void setTotalProgress(int progress);
      void appendDetailLine(const QString& text);
      void resetForNewOperation();
      void enterFinishedState();

  signals:
    void cancel();

  private:
    // Finished state: operation ended (Exit/Canceled received — Error is
    // mid-task, not operation-terminal, and does not set this). The Cancel
    // button becomes Close, and closing no longer emits cancel().
    bool           mFinished;

    // Last Step message appended to the details log. Step fires per frame
    // with mostly identical text; only text CHANGES become log lines.
    QString        mLastStepMsg;

    // Re-entrancy guard for closeEvent() -> onBtnCancelClicked(): makes sure
    // the cancel() signal fires only once per user action even if
    // onBtnCancelClicked()'s own path ever ends up closing the window too.
    bool           mClosing;

    QTimer*        mTickTimer     = nullptr;   // 1 s: clock, remaining, stall
    QElapsedTimer  mWallClock;                 // true operation wall time
    QElapsedTimer  mSinceLastStep;             // stall detection
    QString        mPendingRemaining;          // applied on next tick
    bool           mIndeterminate = false;     // bar in pulse mode
};
#endif // TTPROGRESSBAR_H
