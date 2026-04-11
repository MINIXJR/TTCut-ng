/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutaudiotask.cpp                                              */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/16/2009 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTAUDIOTASK
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#include "ttcutaudiotask.h"

#include <cmath>

#include "../common/ttexception.h"
#include "../avstream/ttfilebuffer.h"
#include "../data/ttcutparameter.h"
#include "../data/ttcutlist.h"
#include "../data/ttavdata.h"
#include "../avstream/ttavstream.h"

//! Cut audio stream task
TTCutAudioTask::TTCutAudioTask() :
                TTThreadTask("CutAudioTask")
{
  mpCutList      = 0;
  mpCutStream    = 0;
  mSrcAudioIndex = 0;
  mMuxListItem   = 0;
  mDelayMs       = 0;
}

/**
 * Init cut audio task
 */
void TTCutAudioTask::init(QString tgtFilePath, TTCutList* cutList, int srcAudioIndex, TTMuxListDataItem* muxListItem,
                          const QString& language, int delayMs)
{
  mTgtFilePath   = tgtFilePath;
  mpCutList      = cutList;
  mpCutStream    = 0;
  mSrcAudioIndex = srcAudioIndex;
  mMuxListItem   = muxListItem;
  mLanguage      = language;
  mDelayMs       = delayMs;
}

//! Operation abort request
void TTCutAudioTask::onUserAbort()
{
  if (mpCutStream == 0) 
    throw new TTAbortException(QString("Task %1 with UUID %2 aborted").arg(taskName()).arg(taskID().toString()));

   mpCutStream->setAbort(true);
}

//! Clean up after operation
void TTCutAudioTask::cleanUp()
{
  if (mpTgtStream != 0) delete mpTgtStream;
  //if (mpCutParams != 0) delete mpCutParams;
}

//! Task operation method
void TTCutAudioTask::operation()
{
  if (mTgtFilePath.isEmpty())
    throw new TTInvalidOperationException(tr("No target file path given for audio cut!"));

  mpTgtStream = new TTFileBuffer(mTgtFilePath, QIODevice::WriteOnly);
	mpCutParams = new TTCutParameter(mpTgtStream);

	float localAudioOffset = 0.0f;

	mpTgtStream->open();

  for (int i = 0; i < mpCutList->count(); i++) {
    TTCutItem cutItem   = mpCutList->at(i);
    float     frameRate = cutItem.avDataItem()->videoStream()->frameRate();

    mpCutStream = cutItem.avDataItem()->audioStreamAt(mSrcAudioIndex);

    connect(mpCutStream, SIGNAL(statusReport(int, const QString&, quint64)),
        this,        SLOT(onStatusReport(int, const QString&, quint64)));

    // Apply per-track audio delay: shift the video frame indices by the delay
    // so that the audio cut window moves accordingly.
    // Positive delay = audio should start later = shift cut window forward.
    int adjustedCutIn  = cutItem.cutInIndex();
    int adjustedCutOut = cutItem.cutOutIndex();
    if (mDelayMs != 0) {
      int delayFrames = (int)round((mDelayMs / 1000.0f) * frameRate);
      adjustedCutIn  = qMax(0, adjustedCutIn  + delayFrames);
      adjustedCutOut = qMax(adjustedCutIn, adjustedCutOut + delayFrames);
    }

    int startIndex = mpCutStream->getStartIndex(adjustedCutIn,
        frameRate, localAudioOffset);
    int endIndex = mpCutStream->getEndIndex(adjustedCutOut,
        frameRate, localAudioOffset);

    log->debugMsg(__FILE__, __LINE__, QString("AudioCut %1/%2 start %3 end %4 (delay %5 ms)").
        arg(i).arg(mpCutList->count()).arg(startIndex).arg(endIndex).arg(mDelayMs));

    if (i == 0)
      mpCutParams->firstCall();

    mpCutStream->cut(startIndex, endIndex, mpCutParams);

    if (i == mpCutList->count() - 1)
      mpCutParams->lastCall();

    disconnect(mpCutStream, SIGNAL(statusReport(int, const QString&, quint64)),
               this,        SLOT(onStatusReport(int, const QString&, quint64)));
  }

  mMuxListItem->appendAudioFile(mTgtFilePath, mLanguage);
  mpTgtStream->close();
  emit finished(mpCutStream->filePath());
}


