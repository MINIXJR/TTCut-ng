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
// TTCUTVIDEOTASK
// ----------------------------------------------------------------------------

#include "ttcutvideotask.h"

#include "../common/ttexception.h"
#include "../common/ttthreadtaskpool.h"
#include "../avstream/ttfilebuffer.h"
#include "../data/ttcutparameter.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavdata.h"
#include "../data/ttcutlist.h"

#include <QDebug>

/**
 * Cut video stream task
 */
//TTCutVideoTask::TTCutVideoTask(TTAVData* avData, QString tgtFilePath, TTCutList* cutList) :
TTCutVideoTask::TTCutVideoTask(TTAVData* avData) :
                TTThreadTask("CutVideoTask")
{
  mpAVData     = avData;
  mpCutList    = 0;
  mpCutStream  = 0;
  mpTgtStream  = nullptr;   // was uninitialised - the destructor reads it
  mpCutParams  = nullptr;
  mpCutTask    = new TTCutTask();
}

/**
 * Owns three things and now releases all of them: the inner cut task from the
 * constructor, and the target buffer plus cut parameters from operation().
 * The buffer is deleted at the end of a successful run and its pointer
 * cleared there, so the delete below only fires when operation() left early -
 * an abort between cut-list entries, or an exception out of the cut itself.
 * Before this, that path leaked the buffer with its file descriptor still
 * open: a deleted video ES vanished from the directory but kept its blocks
 * until the program ended.
 */
TTCutVideoTask::~TTCutVideoTask()
{
  delete mpCutTask;
  delete mpCutParams;
  delete mpTgtStream;
}

/**
 * Init task
 */
void TTCutVideoTask::init(QString tgtFilePath, TTCutList* cutList)
{
  mTgtFilePath = tgtFilePath;
  mpCutList    = cutList;
  
  mMuxListItem.setVideoName(tgtFilePath);
}

/**
 * Operation abort request
 */
void TTCutVideoTask::onUserAbort()
{
  abort();

  // Mid-cut abort: the MPEG-2 transfer loop polls TTAVStream::mAbort and
  // throws TTAbortException; without this the abort only took effect
  // BETWEEN cut-list entries - a single long cut was effectively
  // unabortable (same forwarding as TTCutTask::onUserAbort below).
  if (mpCutStream != 0)
    mpCutStream->setAbort(true);
}

/**
 * Clean up after operation
 */
void TTCutVideoTask::cleanUp()
{
  // Ownership is handled in the destructor - cleanUp() runs on paths where
  // the task object stays alive (see TTThreadTask::run()).
}

/**
 * Returns the mux list item
 */
TTMuxListDataItem* TTCutVideoTask::muxListItem()
{
  return &mMuxListItem;
}

/**
 * Task operation method
 */
void TTCutVideoTask::operation()
{
  if (mTgtFilePath.isEmpty())
    throw TTInvalidOperationException(__FILE__, __LINE__, tr("No target file path given for video cut!"));

 	mpTgtStream = new TTFileBuffer(mTgtFilePath, QIODevice::WriteOnly);
  mpCutParams = new TTCutParameter(mpTgtStream);

	mpTgtStream->open();

  onStatusReport(this, StatusReportArgs::Start, tr("Cut 1 of %1").arg(mpCutList->count()), mpCutList->count());

  for (int i = 0; i < mpCutList->count(); i++) {

    if (isAborted())
      // Message-only constructor deliberately: the (caller, line) overload
      // logs at fatal level on construction (TTException::TTException(caller,
      // line, msg) -> log->fatalMsg(), common/ttexception.cpp:30-36), which
      // would record a deliberate user cancel as the most severe class of
      // log event. Same fix as Task 6's TTH26xCutTask::abortNow().
      //
      // This outer, per-segment check is coarse and in practice loses the
      // race to the inner, message-only TTAbortException throws in
      // TTAVStream::copySegment() and TTMpeg2VideoStream::
      // transferCutObjects(), which poll far more often (per chunk, not per
      // cut-list entry) and normally catch the abort first — confirmed by
      // reverting this fix and instrumenting the throw site: across 12 runs
      // (6 Release, 6 Debug -O0) it never fired. So this line is currently
      // defensive, not load-bearing: it exists so the coarse outer path
      // cannot reintroduce a fatal-level line for a plain cancel if that
      // polling-frequency ordering ever changes (e.g. a future caller that
      // aborts between segments with no inner throw in between).
      throw TTAbortException(tr("Operation aborted!"));

	  TTCutItem cutItem = mpCutList->at(i);
	  int       cutIn   = cutItem.cutInIndex();
	  int       cutOut  = cutItem.cutOutIndex();

    mpCutStream = cutItem.avDataItem()->videoStream();

	  mpCutParams->setCutInIndex(cutIn);
	  mpCutParams->setCutOutIndex(cutOut);

		log->debugMsg(__FILE__, __LINE__,	QString("VideoCut %1/%2 start %3 end %4").
        arg(i).arg(mpCutList->count()).arg(cutIn).arg(cutOut));
		log->debugMsg(__FILE__, __LINE__, QString("VideoCut length %1").
        arg((cutOut - cutIn + 1) * 1000.0 / 25.0));

		if (i == 0)
      mpCutParams->firstCall();

    mpCutTask->init(mpCutStream, mpCutParams);
    // This operation() runs in a pool thread; startNested() keeps the pool's
    // task queue in its own thread (see TTThreadTaskPool::startNested).
    mpAVData->threadTaskPool()->startNested(mpCutTask);

		if (i == mpCutList->count() - 1)
		  mpCutParams->lastCall();

    onStatusReport(this, StatusReportArgs::Step, tr("Cut %1 of %2").arg(i+1).arg(mpCutList->count()), i+1);
	}

  log->debugMsg(__FILE__, __LINE__, QString("close target stream %1").arg(mTgtFilePath));

  delete mpTgtStream;
  mpTgtStream = nullptr;   // so the destructor does not free it twice

  qDebug("cut video task -> emit finished signal!");
  emit finished(mMuxListItem);
}

/**
 * Video cut task
 */
TTCutTask::TTCutTask() : TTThreadTask("CutTask")
{
  mpCutStream    = 0;
  mpCutParameter = 0;
}

/**
 * Init the video cut task
 */
void TTCutTask::init(TTVideoStream* cutStream, TTCutParameter* cutParameter)
{
  mpCutStream    = cutStream;
  mCutIn         = cutParameter->getCutInIndex();
  mCutOut        = cutParameter->getCutOutIndex();
  mpCutParameter = cutParameter;
}

/**
 * Clean up after task operation
 */
void TTCutTask::cleanUp()
{
  if (mpCutStream == 0) return;

  disconnect(mpCutStream, &TTVideoStream::statusReport,
	    			 this,        qOverload<int, const QString&, quint64>(&TTCutTask::onStatusReport));
}

/**
 * User abort request
 */
void TTCutTask::onUserAbort()
{
  abort();

  if (mpCutStream != 0)
    mpCutStream->setAbort(true);
}

/**
 * Task operation
 */
void TTCutTask::operation()
{
  if (mpCutStream == 0)
    throw TTInvalidOperationException(__FILE__, __LINE__, tr("No cut stream specified!"));

	connect(mpCutStream, &TTVideoStream::statusReport,
	  			this,        qOverload<int, const QString&, quint64>(&TTCutTask::onStatusReport));

  mpCutStream->cut(mCutIn, mCutOut, mpCutParameter);
}



