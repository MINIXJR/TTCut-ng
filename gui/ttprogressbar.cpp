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
#include "tttaskprogress.h"

#include "../common/istatusreporter.h"
#include "../common/ttthreadtask.h"

#include <QDebug>
#include <QApplication>
#include <QCloseEvent>

/**
 * Constructor
 */
TTProgressBar::TTProgressBar(QWidget* parent)
              : QDialog(parent)
{
  setupUi(this);

  scrollArea->hide();
  this->adjustSize();

  processForm    = 0;
  normTotalSteps = 100;
  isBlocking     = false;
  mClosing       = false;

  progressBar->setMinimum( 0 );
  progressBar->setMaximum( normTotalSteps );

  taskProgressHash = new QHash<QUuid, TTTaskProgress*>;

  connect(pbCancel,  &QPushButton::clicked,    this, &TTProgressBar::onBtnCancelClicked);
  connect(cbDetails, &QCheckBox::stateChanged, this, &TTProgressBar::onDetailsStateChanged);
}

/**
 * Destructor
 */
TTProgressBar::~TTProgressBar()
{
  if (processForm != 0) delete processForm;
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
 *
 * Does NOT set the dialog modal on the way out. It used to, which is a strange
 * thing to do to a window one is about to hide - and under Wayland it left an
 * invisible modal grab behind: after a stream-point analysis the application
 * looked frozen, a "goto frame" dialog opened but shivered without taking
 * input, and marker jumps went nowhere. The same session under
 * QT_QPA_PLATFORM=xcb worked, and the process was idle throughout (0.7% CPU
 * over 35 minutes), so nothing was looping - input simply never arrived.
 */
void TTProgressBar::hideBar()
{
  if (isBlocking) return;

  hide();

  qApp->processEvents();
}

/**
 * Handle the window's close button (the X).
 *
 * This behaves exactly like the Cancel button (onBtnCancelClicked()): a
 * QDialog's default closeEvent() maps to reject(), i.e. "discard the
 * operation", not "put the window away" - and Qt's own QProgressDialog
 * reimplements closeEvent() for the same reason. An operation that keeps
 * running with its only progress window gone, with no separate "run in
 * background" affordance, is a usability trap, not a convenience.
 *
 * This also fixes the original complaint (the dialog reopening after being
 * closed): every task emits StatusReportArgs::Start, and the Start branch in
 * TTCutMainWindow::onStatusReport calls showBar(). Cancelling here goes
 * through onBtnCancelClicked(), which - via the cancel() signal -
 * synchronously aborts every task still in the pool's queue
 * (TTThreadTaskPool::onUserAbortRequest()), including ones that have not
 * started running yet. TTThreadTask::run() checks mIsAborted before calling
 * operation() (where Start is reported), so an aborted-but-not-yet-started
 * task never reports Start at all - there is no second Start left to reopen
 * the dialog.
 */
void TTProgressBar::closeEvent(QCloseEvent* event)
{
  // onBtnCancelClicked() calls hideBar(), which only hides the window (never
  // close()s it), so this does not recurse back into closeEvent() today.
  // The guard is defensive in case that ever changes, so the cancel() signal
  // can never fire twice for one user action.
  if (!mClosing) {
    mClosing = true;
    onBtnCancelClicked();
    mClosing = false;
  }

  event->accept();
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
 * Set the task's progress value
 */
void TTProgressBar::setTaskProgress(TTThreadTask* task, const QString& msg)
{
  if (task == 0) return;
  if (!taskProgressHash->contains(task->taskID())) return;

  TTTaskProgress* tp = taskProgressHash->value(task->taskID());
  tp->onRefreshProgress(msg);
}

/**
 * Set task finished
 */
void TTProgressBar::setTaskFinished(TTThreadTask* task, const QString& msg)
{
  if (task == 0) return;
  if (!taskProgressHash->contains(task->taskID())) return;

  TTTaskProgress* tp = taskProgressHash->value(task->taskID());
  tp->onTaskFinished(msg);
}

/**
 * Set the progress value to 100%
 */
/**
 * Reset the progress bar and remove all taskprogress widgets
 */
void TTProgressBar::resetProgress()
{
  progressBar->reset();

  for (TTTaskProgress* value : *taskProgressHash) {
    if (value == 0) continue;

    verticalLayout->removeWidget(value);
    delete value;
    value = 0;
  }
  taskProgressHash->clear();
  scrollArea->adjustSize();
  this->adjustSize();
}

/**
 * Show/hide the details view
 */
void TTProgressBar::onDetailsStateChanged(int)
{
  if (cbDetails->isChecked()) {
    scrollArea->show();
  } else {
    scrollArea->hide();
  }
  this->adjustSize();
}

/**
 * Button cancel clicked
 */
void TTProgressBar::onBtnCancelClicked()
{
  emit cancel();

  isBlocking = false;
  hideProcessForm();
  hideBar();
}

/**
 * Set progress values
 */
void TTProgressBar::onSetProgress(TTThreadTask* task, int state, const QString& msg, int totalProgress, QTime totalTime)
{
  switch (state) {
    case StatusReportArgs::Init:
      isBlocking = false;
      resetProgress();
      setActionText(msg);
      this->setEnabled(true);
      break;

    case StatusReportArgs::Start:
      addTaskProgress(task);
      setActionText(msg);
      break;

    case StatusReportArgs::Step:
      setActionText(msg);
      setTotalProgress(totalProgress, totalTime);
      setTaskProgress(task, msg);
      break;

    case StatusReportArgs::Finished:
      setTaskFinished(task, msg);
      break;

    case StatusReportArgs::ShowProcessForm:
      //showProcessForm();
      break;

    case StatusReportArgs::ShowProcessFormBlocking:
      isBlocking = true;
      //showProcessForm();
      break;

    case StatusReportArgs::AddProcessLine:
      //addProcessLine(msg);
      break;

    case StatusReportArgs::HideProcessForm:
      //hideProcessForm();
      break;

    default:
      break;
  }
}

/**
 * Add progress bar for the given task
 */
void TTProgressBar::addTaskProgress(TTThreadTask* task)
{
  if (task == 0) return;
  if (taskProgressHash->contains(task->taskID())) return;

  TTTaskProgress* taskProgress = new TTTaskProgress(this, task);

  taskProgressHash->insert(task->taskID(), taskProgress);
  verticalLayout->addWidget(taskProgress);
}

// /////////////////////////////////////////////////////////////////////////////
// Process output form
/**
 * showProcessForm
 */
/**
 * addProcessLine
 */
/**
 * hideProcessForm
 */
void TTProgressBar::hideProcessForm()
{
  if (processForm == 0) return;

  if (isBlocking) {
    processForm->enableButton(true);
    return;
  }

	processForm->hide();
	//delete processForm;
	//processForm = 0;
}

