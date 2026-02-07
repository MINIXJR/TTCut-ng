/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttframesearchtask.cpp                                           */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/16/2009 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTFRAMESEARCHTASK
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

#include "ttframesearchtask.h"

#include <QDebug>

#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../common/istatusreporter.h"
#include "../avstream/ttavstream.h"

//! Search for an equal frame
TTFrameSearchTask::TTFrameSearchTask(TTVideoStream* referenceStream, int referenceIndex,
                                 TTVideoStream* searchStream, int searchIndex)
                  : TTThreadTask("FrameSearchTask")
{
  mAbort            = false;
  mpReferenceStream = referenceStream;
  mpSearchStream    = searchStream;
  mReferenceIndex   = referenceIndex;
  mSearchIndex      = searchIndex;
}

//! Init frame search task
void TTFrameSearchTask::initFrameSearch()
{
  TTMpeg2Decoder* refDecoder = new TTMpeg2Decoder(
      mpReferenceStream->filePath(),
      mpReferenceStream->indexList(),
      mpReferenceStream->headerList(),
      formatYV12);

  refDecoder->moveToFrameIndex(mReferenceIndex);

  mpRefFrameInfo  = refDecoder->getFrameInfo();
  mpReferenceData = new quint8 [mpRefFrameInfo->size+2*mpRefFrameInfo->chroma_size];
  mpSearchData    = new quint8 [mpRefFrameInfo->size+2*mpRefFrameInfo->chroma_size];

  refDecoder->getCurrentFrameData(mpReferenceData);

  delete refDecoder;
}

//! Compare two frames in YUV12 pixel format
quint64 TTFrameSearchTask::compareFrames(TFrameInfo* frameInfo, quint8* refData, quint8* searchData)
{
  qint64 delta = 0;

  for(int j=0; j<frameInfo->size+2*frameInfo->chroma_size; j++)
    delta += (qint64)((searchData[j]-refData[j])*(searchData[j]-refData[j]));

  return delta;
}

//! Clean up after operation
void TTFrameSearchTask::cleanUp()
{
  if (mpReferenceData != 0) delete mpReferenceData;
  if (mpSearchData    != 0) delete mpSearchData;
}

//! Abort request
void TTFrameSearchTask::onUserAbort()
{
  mAbort = true;
}

//! Task operation method
void TTFrameSearchTask::operation()
{
  initFrameSearch();

  TTMpeg2Decoder* searchDecoder = new TTMpeg2Decoder(
      mpSearchStream->filePath(),
      mpSearchStream->indexList(),
      mpSearchStream->headerList(),
      formatYV12);

  searchDecoder->decodeFirstMPEG2Frame(formatYV12);
  searchDecoder->moveToFrameIndex(mSearchIndex);

  QTime   searchTime(0,0,0,0);
  int     searchFrameCount = ttTimeToFrames(searchTime.addSecs(TTCut::searchLength), mpSearchStream->frameRate());
  int     index            = 0;
  int     foundPosition    = 0;
  // Threshold based on frame size: allow ~10% average difference per pixel
  // For YV12: size + 2*chroma_size bytes, squared difference per byte
  // threshold = totalBytes * (25^2) where 25 is ~10% of 255
  quint64 threshold        = (quint64)(mpRefFrameInfo->size + 2 * mpRefFrameInfo->chroma_size) * 625;
  quint64 minDelta         = threshold;

  onStatusReport(this, StatusReportArgs::Start, "search frame", searchFrameCount);

  do
  {
  	if (mAbort)
    {
      delete searchDecoder;
  		throw new TTAbortException("User abort in TTFrameSearchTask!");
    }

    TFrameInfo* frameInfo = searchDecoder->getFrameInfo();
    searchDecoder->getCurrentFrameData(mpSearchData);

    // we can not compare frames of different size
    if (mpRefFrameInfo->size        != frameInfo->size ||
        mpRefFrameInfo->chroma_size != frameInfo->chroma_size) {
      continue;
    }

    quint64 delta = compareFrames(frameInfo, mpReferenceData, mpSearchData);

    if (delta == 0) {
      foundPosition = index;
      break;
    }

    if (delta < minDelta) {
      minDelta      = delta;
      foundPosition = index;
    }

    index++;
    onStatusReport(this, StatusReportArgs::Step, "search frame", index);
    searchDecoder->moveToFrameIndex(mSearchIndex+index);
  } while (index < searchFrameCount);

  delete searchDecoder;

  // Check if we found a match below threshold
  if (minDelta >= threshold) {
    log->debugMsg(__FILE__, __LINE__, QString("no matching frame found (minDelta %1 >= threshold %2)")
        .arg(minDelta).arg(threshold));
    onStatusReport(this, StatusReportArgs::Step, "no match found", searchFrameCount);
    emit finished(-1);  // -1 indicates no match
    return;
  }

  log->debugMsg(__FILE__, __LINE__, QString("found equal frame at %1 searchIndex %2 foundPos %3 delta %4")
      .arg(mSearchIndex+foundPosition).arg(mSearchIndex).arg(foundPosition).arg(minDelta));

  onStatusReport(this, StatusReportArgs::Step, "found equal frame", searchFrameCount);

  emit finished(mSearchIndex+foundPosition);
}
