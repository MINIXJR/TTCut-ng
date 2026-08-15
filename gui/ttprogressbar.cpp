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
#include <QFontMetrics>
#include <QScrollBar>
#include <QTime>
#include <QTimer>

/**
 * Constructor
 */
TTProgressBar::TTProgressBar(QWidget* parent)
              : QDialog(parent)
{
  setupUi(this);

  // Rows 1 and 3 are now independent QHBoxLayouts (ttprogressform.ui) so a
  // hidden laRemaining releases its column width instead of leaving
  // remainingString indented at row 3's "Elapsed:" label width. Equalize
  // both label widths in code (font-independent) so the value columns of
  // the two rows still line up while both labels are visible.
  int lw = qMax(laRemaining->sizeHint().width(), laDebugClock->sizeHint().width());
  laRemaining->setMinimumWidth(lw);
  laDebugClock->setMinimumWidth(lw);

  detailsView->hide();
  this->adjustSize();

  mFinished = false;
  mClosing  = false;

  progressBar->setMinimum(0);
  progressBar->setMaximum(100);

  connect(pbCancel,  &QPushButton::clicked,         this, &TTProgressBar::onBtnCancelClicked);
  connect(cbDetails, &QCheckBox::checkStateChanged, this, &TTProgressBar::onDetailsStateChanged);

  // Debug wall clock: only visible with the details pane (checkbox).
  laDebugClock->hide();
  debugClockString->hide();

  mTickTimer = new QTimer(this);
  mTickTimer->setInterval(1000);
  connect(mTickTimer, &QTimer::timeout, this, &TTProgressBar::onTick);

  // Start the lifecycle here too, not only in resetForNewOperation(): a
  // freshly created dialog whose first message is Start (stream-point
  // scans emit no Init) hit the "if (mFinished) resetForNewOperation()"
  // branch in onSetProgress() with mFinished already false (just set above),
  // so the tick timer/wall clock never started - the remaining label and
  // debug clock stayed frozen for the whole scan. Idempotent: Init's own
  // resetForNewOperation() (or a finished-dialog Start) just restarts both.
  mWallClock.restart();
  mSinceLastStep.restart();
  mTickTimer->start();
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

  // Stay enabled even though the main window is not. This dialog is a child of
  // TTCutMainWindow, which disables itself for the duration of an operation
  // (setEnabled(false) in its Init branch) - and Qt disables children with it,
  // child windows included. A disabled dialog is painted in its disabled state,
  // and no style animates that: the pulse mode below (setRange(0, 0), used when
  // no Step has arrived for 5 s) then showed frozen stripes instead of movement.
  // Reported from the GUI as "soll das wirklich eine statische Anzeige sein?".
  //
  // It went unnoticed because resetForNewOperation() already calls
  // setEnabled(true) - but only for a dialog that is in its finished state, so
  // the FIRST long operation after program start had a disabled dialog and every
  // later one an enabled one. Measured with tools/diag/test_pulse_animation:
  // an indeterminate bar animates while enabled (even with a disabled parent)
  // and stands still while disabled.
  //
  // Being enabled is also what the Cancel button needs - the same reason
  // resetForNewOperation() does this.
  setEnabled(true);

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
 * Set the action text. Now that the label owns a full-width row (v0.81
 * layout fix), this rarely elides in practice — but very long messages
 * (segment counters plus a long file name) can still exceed the width, so
 * QFontMetrics::elidedText() stays as a safety net. ElideMiddle keeps both
 * ends: the start ("Encoding segment ...") and the end (counters / file
 * name) each carry information a trailing-only elide would drop. The full
 * text always goes into the tooltip so nothing is lost from view.
 */
void TTProgressBar::setActionText( QString action )
{
  actionString->setToolTip( action );

  if (actionString->width() > 0) {
    QFontMetrics fm(actionString->font());
    actionString->setText(fm.elidedText(action, Qt::ElideMiddle, actionString->width()));
  } else {
    // First call can arrive before the widget has been laid out (width 0).
    actionString->setText( action );
  }
}

/**
 * Set the current total progress value. Also feeds the stall detection:
 * every Step restarts the silence clock and leaves pulse mode.
 */
void TTProgressBar::setTotalProgress(int progress)
{
  if (mIndeterminate) {
    progressBar->setRange(0, 100);
    mIndeterminate = false;
  }
  percentageString->setText(QString("%1%").arg(qMin(progress, 100)));
  progressBar->setValue(progress);
  mSinceLastStep.restart();
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
 * Buffered remaining-time text: stored here, applied by the 1 s tick so the
 * label never flickers with per-frame Step messages.
 */
void TTProgressBar::setRemaining(const QString& text)
{
  mPendingRemaining = text;
}

/**
 * 1 s heartbeat: debug wall clock, remaining-time label, stall detection.
 * The wall clock keeps ticking during a stall - that is its debug value
 * (frozen bar + ticking clock = stage hangs, application alive).
 */
void TTProgressBar::onTick()
{
  if (mWallClock.isValid())
    debugClockString->setText(
        QTime(0, 0).addMSecs(int(mWallClock.elapsed())).toString("hh:mm:ss"));

  if (!mPendingRemaining.isEmpty()) {
    remainingString->setText(mPendingRemaining);
    mPendingRemaining.clear();
  }

  // Stall: >5 s without a Step -> indeterminate (pulsing) bar. The percent
  // text stays as the last known value.
  if (!mFinished && mSinceLastStep.isValid()
      && mSinceLastStep.elapsed() > 5000 && !mIndeterminate) {
    progressBar->setRange(0, 0);
    mIndeterminate = true;
  }
}

/**
 * Reset for a new operation: clear the log, restore the Cancel button.
 */
void TTProgressBar::resetForNewOperation()
{
  if (mIndeterminate) {
    progressBar->setRange(0, 100);
    mIndeterminate = false;
  }
  progressBar->reset();
  percentageString->setText("0%");
  remainingString->setText(tr("calculating..."));
  debugClockString->setText("00:00:00");
  detailsView->clear();
  mLastStepMsg.clear();
  mPendingRemaining.clear();
  mFinished = false;
  pbCancel->setText(tr("Cancel"));
  laRemaining->show();
  mWallClock.restart();
  mSinceLastStep.restart();
  mTickTimer->start();
  this->setEnabled(true);
}

/**
 * Operation ended: turn Cancel into Close. With the details pane open the
 * dialog stays visible so the log can be read; otherwise hide as before.
 */
void TTProgressBar::enterFinishedState()
{
  mTickTimer->stop();
  if (mIndeterminate) {
    progressBar->setRange(0, 100);
    mIndeterminate = false;
  }
  if (!mPendingRemaining.isEmpty()) {          // show the final value
    remainingString->setText(mPendingRemaining);
    mPendingRemaining.clear();
  }
  if (mWallClock.isValid())
    debugClockString->setText(
        QTime(0, 0).addMSecs(int(mWallClock.elapsed())).toString("hh:mm:ss"));

  // The value label keeps showing "Finished after ..." / "Cancelled ..." -
  // hide the "Remaining:" caption so the row does not read as a run-on
  // ("Remaining: Finished after 00:00:07"). Restored in
  // resetForNewOperation() for the next run.
  laRemaining->hide();

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
  laDebugClock->setVisible(cbDetails->isChecked());
  debugClockString->setVisible(cbDetails->isChecked());
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
void TTProgressBar::onSetProgress(TTThreadTask* task, int state, const QString& msg, int totalProgress)
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
      // A running operation must never leave the user with a hidden dialog
      // AND a disabled main window (dead UI): closing the dialog mid-run
      // (X or Cancel) hides it, but the synchronous H.26x cut cannot be
      // aborted and keeps reporting - bring the dialog back on the next
      // Step. Also improves the MPEG-2 audio-phase quirk (dialog used to
      // stay hidden until the pool video task's Start).
      if (!mFinished && !isVisible())
        showBar();
      setActionText(msg);
      setTotalProgress(totalProgress);
      if (msg != mLastStepMsg) {
        mLastStepMsg = msg;
        appendDetailLine(msg);
      }
      break;

    case StatusReportArgs::Finished:
      appendDetailLine(msg);
      break;

    case StatusReportArgs::AddProcessLine:
      // Same guard as Step, for the same reason. It used to sit only in the
      // Step branch, and a phase that reports exclusively through
      // AddProcessLine - mplex is the one that does - therefore never brought
      // the dialog back: closing it mid-run left the main window disabled
      // (Init disables it, only Exit/Canceled re-enable it) with nothing
      // visible for the rest of the mux. The application looked dead, and a
      // GUI acceptance run read a completed cut as an aborted one because of
      // it. Safe against re-showing a legitimately finished, user-closed
      // dialog because enterFinishedState() sets mFinished before hideBar().
      if (!mFinished && !isVisible())
        showBar();
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
      setTotalProgress(100);
      appendDetailLine(msg);
      enterFinishedState();
      break;

    default:
      break;
  }
}
