/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTSUBTITLETASK
// ----------------------------------------------------------------------------

#include "ttcutsubtitletask.h"

#include "../common/ttexception.h"
#include "../avstream/ttfilebuffer.h"
#include "ttcutparameter.h"
#include "ttcutlist.h"
#include "ttavdata.h"
#include "../avstream/ttavstream.h"

//! Cut subtitle stream task
TTCutSubtitleTask::TTCutSubtitleTask() :
                   TTThreadTask("CutSubtitleTask")
{
  mpCutList         = 0;
  mpCutStream       = 0;
  mSrcSubtitleIndex = 0;
  mMuxListItem      = 0;
}

/**
 * Init cut subtitle task
 */
void TTCutSubtitleTask::init(QString tgtFilePath, TTCutList* cutList, int srcSubtitleIndex, TTMuxListDataItem* muxListItem,
                             const QString& language)
{
  mTgtFilePath      = tgtFilePath;
  mpCutList         = cutList;
  mpCutStream       = 0;
  mSrcSubtitleIndex = srcSubtitleIndex;
  mMuxListItem      = muxListItem;
  mLanguage         = language;
}

//! Operation abort request
void TTCutSubtitleTask::onUserAbort()
{
  if (mpCutStream == 0) 
    throw TTAbortException(QString("Task %1 with UUID %2 aborted").arg(taskName()).arg(taskID().toString()));

   mpCutStream->setAbort(true);
}

//! Clean up after operation
void TTCutSubtitleTask::cleanUp()
{
  if (mpTgtStream != 0) delete mpTgtStream;
  //if (mpCutParams != 0) delete mpCutParams;
}

//! Task operation method
void TTCutSubtitleTask::operation()
{
  if (mTgtFilePath.isEmpty())
    throw TTInvalidOperationException(tr("No target file path given for subtitle cut!"));

  mpTgtStream = new TTFileBuffer(mTgtFilePath, QIODevice::WriteOnly);
  mpCutParams = new TTCutParameter(mpTgtStream);
  mpCutParams->setNumPicturesWritten(0);
  mpCutParams->setCutInIndex(0);
  mpCutParams->setCutOutIndex(0);

  mpTgtStream->open();

  for (int i = 0; i < mpCutList->count(); i++) {
    TTCutItem cutItem   = mpCutList->at(i);

    mpCutStream = cutItem.avDataItem()->subtitleStreamAt(mSrcSubtitleIndex);

    connect(mpCutStream, &TTSubtitleStream::statusReport,
            this,        qOverload<int, const QString&, quint64>(&TTCutSubtitleTask::onStatusReport));

    log->debugMsg(__FILE__, __LINE__,	QString("SubtitleCut %1/%2 start %3 end %4").
        arg(i).arg(mpCutList->count()).arg(cutItem.cutInTime().toString("hh:mm:ss.zzz")).arg(cutItem.cutOutTime().toString("hh:mm:ss.zzz")));

    if (i == 0)
      mpCutParams->firstCall();

    mpCutStream->cut(cutItem.cutInTime().msecsSinceStartOfDay(), cutItem.cutInTime().msecsSinceStartOfDay()+cutItem.cutLengthTime().msecsSinceStartOfDay()-1, mpCutParams);

    mpCutParams->setCutInIndex(mpCutParams->getCutOutIndex()+1);

    if (i == mpCutList->count() - 1)
      mpCutParams->lastCall();

    disconnect(mpCutStream, &TTSubtitleStream::statusReport,
               this,        qOverload<int, const QString&, quint64>(&TTCutSubtitleTask::onStatusReport));
  }

  mMuxListItem->appendSubtitleFile(mTgtFilePath, mLanguage);
  mpTgtStream->close();
  emit finished(mpCutStream->filePath());
}


