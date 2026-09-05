/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttabortabletask.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                       DATE: 09/05/2026 */
/*----------------------------------------------------------------------------*/

#include "ttabortabletask.h"
#include "ttavdata.h"

#include "../avstream/ttcommon.h"
#include "../common/ttexception.h"

TTAbortableTask::TTAbortableTask(TTAVData* avData, const QString& name) :
                 TTThreadTask(name)
{
  mpAVData = avData;
}

void TTAbortableTask::requestCancel()
{
  mCancelRequested.store(true, std::memory_order_relaxed);
}

/**
 * Delete everything this run created.
 *
 * Abort only: on a real failure the products of the run stay on disk, which
 * is the behaviour the synchronous cut had. Failures to remove a file are
 * logged and skipped - an abort must always reach the Canceled bracket and
 * must never hang or fail on cleanup.
 */
void TTAbortableTask::abortCleanup()
{
  ttRemoveFiles(mCreatedFiles, log);
  mCreatedFiles.clear();
}

/**
 * Leave operation() through the abort path.
 *
 * TTThreadTask::run() catches TTAbortException, calls cleanUp() and emits
 * aborted() - which reaches TTAVData's abort slot and closes the operation
 * with the Canceled bracket. No failure text is recorded: a deliberate
 * cancel is not a failure, so neither the error dialog nor the log must
 * show one.
 *
 * The message-only TTAbortException constructor is used on purpose. Its
 * (caller, line, msg) sibling logs the text through TTMessageLogger::
 * fatalMsg() - measured: a cancel wrote "[][..][tth26xcuttask:124] user
 * abort" into the persistent log, i.e. a fatal-level line for something the
 * user asked for. (TTCutVideoTask and TTCutPreviewTask still use that
 * constructor; their pre-existing fatal line on cancel is out of scope here.)
 */
void TTAbortableTask::abortNow()
{
  abortCleanup();
  throw TTAbortException("user abort");
}

/**
 * Poll point between two phases of the pipeline
 */
void TTAbortableTask::abortIfRequested()
{
  if (!cancelRequested()) return;
  abortNow();
}

/**
 * Forward one Step report to the GUI.
 *
 * TTAVData::onStatusReport re-emits TTAVData::statusReport with a null task
 * pointer, which is what the progress dialog treats as "value IS the
 * percent" - exactly what the synchronous code did. The signal crosses to
 * the GUI thread through the queued connection of its receivers, in
 * emission order.
 */
void TTAbortableTask::reportStep(const QString& msg, quint64 percent)
{
  mpAVData->onStatusReport(StatusReportArgs::Step, msg, percent);
}

/**
 * Announce a stage change (feeds the remaining-time estimator)
 */
void TTAbortableTask::reportStage(int stage)
{
  mpAVData->onStatusReport(StatusReportArgs::Stage, QString(), stage);
}
