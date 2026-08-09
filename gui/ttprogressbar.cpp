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

#include "ttprogressbar.h"

#include "../common/istatusreporter.h"
#include "../common/ttthreadtask.h"

#include <QApplication>
#include <QCloseEvent>
#include <QScrollBar>
#include <QTime>

/**
 * Constructor
 */
TTProgressBar::TTProgressBar(QWidget* parent)
              : QDialog(parent)
{
  setupUi(this);

  detailsView->hide();
  this->adjustSize();

  mFinished = false;
  mClosing  = false;

  progressBar->setMinimum(0);
  progressBar->setMaximum(100);

  connect(pbCancel,  &QPushButton::clicked,         this, &TTProgressBar::onBtnCancelClicked);
  connect(cbDetails, &QCheckBox::checkStateChanged, this, &TTProgressBar::onDetailsStateChanged);
}

/**
 * Destructor
 */
TTProgressBar::~TTProgressBar()
{
}

/**
 * Show the progress form
 */
void TTProgressBar::showBar()
{
  setModal(false);
  show();

  qApp->processEvents();
}

/**
 * Hide the progress form
 */
void TTProgressBar::hideBar()
{
  hide();

  qApp->processEvents();
}

/**
 * Handle the window's close button (the X).
 *
 * While the operation is running this behaves exactly like the Cancel
 * button (see onBtnCancelClicked()): a progress window silently closing
 * over a still-running operation is a usability trap, and Qt's own
 * QProgressDialog reimplements closeEvent() for the same reason.
 * After the operation finished (mFinished), closing is just closing.
 */
void TTProgressBar::closeEvent(QCloseEvent* event)
{
  if (!mFinished && !mClosing) {
    mClosing = true;
    onBtnCancelClicked();
    mClosing = false;
  }

  event->accept();
}

/**
 * Esc arrives here via QDialog::keyPressEvent. Route it through close()
 * so it takes exactly the closeEvent() path: cancel-once while running,
 * plain hide when finished.
 */
void TTProgressBar::reject()
{
  close();
}

/**
 * Set the action text
 */
void TTProgressBar::setActionText( QString action )
{
  actionString->setText( action );
}

/**
 * Set the current total progress values
 */
void TTProgressBar::setTotalProgress(int progress, QTime time)
{
    percentageString->setText(QString("%1%").arg(qMin(progress, 100)));
    progressBar->setValue(progress);
    elapsedTimeString->setText(time.toString("hh:mm:ss"));
}

/**
 * Append one timestamped line to the details log. Keeps the view glued to
 * the newest line unless the user has scrolled up to read older output.
 */
void TTProgressBar::appendDetailLine(const QString& text)
{
  QScrollBar* sb = detailsView->verticalScrollBar();
  const bool wasAtEnd = (sb->value() >= sb->maximum() - 4);

  detailsView->appendPlainText(
      QString("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss"), text));

  if (wasAtEnd)
    sb->setValue(sb->maximum());
}

/**
 * Reset for a new operation: clear the log, restore the Cancel button.
 */
void TTProgressBar::resetForNewOperation()
{
  progressBar->reset();
  percentageString->setText("0%");
  elapsedTimeString->setText("00:00:00");
  detailsView->clear();
  mLastStepMsg.clear();
  mFinished = false;
  pbCancel->setText(tr("Cancel"));
  this->setEnabled(true);
}

/**
 * Operation ended: turn Cancel into Close. With the details pane open the
 * dialog stays visible so the log can be read; otherwise hide as before.
 */
void TTProgressBar::enterFinishedState()
{
  mFinished = true;
  pbCancel->setText(tr("Close"));

  if (!cbDetails->isChecked())
    hideBar();
}

/**
 * Show/hide the details view
 */
void TTProgressBar::onDetailsStateChanged(Qt::CheckState)
{
  if (cbDetails->isChecked()) {
    detailsView->show();
  } else {
    detailsView->hide();
  }
  this->adjustSize();
}

/**
 * Button clicked: Cancel while running, Close when finished
 */
void TTProgressBar::onBtnCancelClicked()
{
  if (!mFinished)
    emit cancel();

  hideBar();
}

/**
 * Central status sink: drives the action line, the total progress bar and
 * the details log. All senders arrive here (task-based and task==0 alike);
 * the log needs no task objects.
 */
void TTProgressBar::onSetProgress(TTThreadTask* task, int state, const QString& msg, int totalProgress, QTime totalTime)
{
  Q_UNUSED(task)

  switch (state) {
    case StatusReportArgs::Init:
      resetForNewOperation();
      setActionText(msg);
      appendDetailLine(msg);
      break;

    case StatusReportArgs::Start:
      // Stream-point scans emit Start without Init, so a new operation can
      // arrive on a dialog still in its finished state - reset then. A
      // Start DURING a running operation (every open task emits one) must
      // NOT clear the log.
      if (mFinished) resetForNewOperation();
      setActionText(msg);
      appendDetailLine(msg);
      break;

    case StatusReportArgs::Step:
      setActionText(msg);
      setTotalProgress(totalProgress, totalTime);
      if (msg != mLastStepMsg) {
        mLastStepMsg = msg;
        appendDetailLine(msg);
      }
      break;

    case StatusReportArgs::Finished:
      appendDetailLine(msg);
      break;

    case StatusReportArgs::AddProcessLine:
      appendDetailLine(msg);
      break;

    case StatusReportArgs::ShowProcessForm:
    case StatusReportArgs::HideProcessForm:
      // Legacy process-form brackets from the MPEG-2 re-encoder and mplex —
      // the form is gone, but the messages (incl. "Encoding failed - encoder
      // setup") belong in the log.
      appendDetailLine(msg);
      break;

    case StatusReportArgs::Error:
      // Error is emitted from inside a single task (e.g. the H.26x frame
      // index build during open) and is never operation-terminal — every
      // operation ends with Exit or Canceled. Log the line and stay in the
      // running state; entering the finished state here would let the next
      // task's Start wipe the log (including this very error line) and
      // would disable cancel while the pool is still running.
      appendDetailLine(tr("Error: %1").arg(msg));
      break;

    case StatusReportArgs::Canceled:
      setActionText(msg);
      appendDetailLine(tr("Cancelled: %1").arg(msg));
      enterFinishedState();
      break;

    case StatusReportArgs::Exit:
      // Finalize the display: the last Step often leaves the bar at a mid
      // value (e.g. the pool's overall percentage) when the operation ends.
      // Without this a kept-open dialog shows a frozen mid-run state
      // forever (GUI acceptance finding 2026-08-07).
      setActionText(msg);
      setTotalProgress(100, totalTime);
      appendDetailLine(msg);
      enterFinishedState();
      break;

    default:
      break;
  }
}
