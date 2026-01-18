/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / www.tritime.org                         */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttopenvideotask.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/16/2009 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTOPENVIDEOTASK
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 2 of the License, or (at your option) any later version.    */
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

#include "ttopenvideotask.h"

#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../extern/ttffmpegwrapper.h"

#include <QDir>
#include <QDebug>

/**
 * Open video stream task
 */
TTOpenVideoTask::TTOpenVideoTask(TTAVItem* avItem, QString fileName, int order) :
                 TTThreadTask("OpenVideoTask")
{
  mpAVItem          = avItem;
  mOrder            = order;
  mFileName         = fileName;
  mOriginalFileName = fileName;
  mDemuxedAudio     = "";
  mpVideoStream     = 0;
  mpVideoType       = 0;
}

/**
 * Request for aborting current operation
 */
void TTOpenVideoTask::onUserAbort()
{
  abort();

  if (mpVideoStream != 0)
    mpVideoStream->setAbort(true);

  if (!mpAVItem->isInList())
    delete mpAVItem;
}

/**
 * Clean up after operation
 */
void TTOpenVideoTask::cleanUp()
{
  if (mpVideoType   != 0) delete mpVideoType;
  if (mpVideoStream == 0) return;

  disconnect(mpVideoStream, SIGNAL(statusReport(int, const QString&, quint64)),
             this,          SLOT(onStatusReport(int, const QString&, quint64)));
}

/**
 * Task operation method
 */
void TTOpenVideoTask::operation()
{
  QFileInfo fInfo(mFileName);

  if (!fInfo.exists())
    throw new TTFileNotFoundException(__FILE__, __LINE__, QString(tr("file %1 does not exists!")).arg(fInfo.filePath()));

  QString videoFilePath = fInfo.absoluteFilePath();

  // Check if this is a container format that needs demuxing
  TTFFmpegWrapper ffmpeg;
  if (ffmpeg.openFile(videoFilePath)) {
    TTContainerType containerType = ffmpeg.detectContainer();

    qDebug() << "Container type:" << TTFFmpegWrapper::containerTypeToString(containerType);

    if (containerType != CONTAINER_ELEMENTARY && containerType != CONTAINER_UNKNOWN) {
      // This is a container (TS, MKV, MP4, PS) - need to demux first
      qDebug() << "Detected container format, demuxing to elementary streams...";

      QString demuxDir = TTCut::tempDirPath;
      QString demuxedVideo;
      QString demuxedAudio;

      if (ffmpeg.demuxToElementary(demuxDir, &demuxedVideo, &demuxedAudio)) {
        qDebug() << "Demuxed video:" << demuxedVideo;
        qDebug() << "Demuxed audio:" << demuxedAudio;

        // Use the demuxed elementary stream instead
        if (!demuxedVideo.isEmpty()) {
          videoFilePath = demuxedVideo;
          fInfo = QFileInfo(videoFilePath);
          // Store demuxed audio path for later loading
          mDemuxedAudio = demuxedAudio;
        } else {
          ffmpeg.closeFile();
          throw new TTDataFormatException(__FILE__, __LINE__,
              QString(tr("Failed to extract video stream from container: %1")).arg(mFileName));
        }
      } else {
        ffmpeg.closeFile();
        throw new TTDataFormatException(__FILE__, __LINE__,
            QString(tr("Failed to demux container: %1 - %2")).arg(mFileName).arg(ffmpeg.lastError()));
      }
    }
    ffmpeg.closeFile();
  }

  // Now open the (possibly demuxed) elementary stream
  mpVideoType = new TTVideoType(videoFilePath);

  TTAVTypes::AVStreamType streamType = mpVideoType->avStreamType();
  qDebug() << "Video stream type:" << streamType;

  // Check for supported video types
  if (streamType != TTAVTypes::mpeg2_demuxed_video &&
      streamType != TTAVTypes::mpeg2_mplexed_video &&
      streamType != TTAVTypes::h264_video &&
      streamType != TTAVTypes::h265_video) {
    throw new TTDataFormatException(__FILE__, __LINE__,
        QString(tr("unsupported video type %1")).arg(fInfo.filePath()));
  }

  // Use factory method to create the appropriate video stream
  mpVideoStream = mpVideoType->createVideoStream();

  if (mpVideoStream == nullptr) {
    throw new TTDataFormatException(__FILE__, __LINE__,
        QString(tr("failed to create video stream for %1")).arg(fInfo.filePath()));
  }

  connect(mpVideoStream, SIGNAL(statusReport(int, const QString&, quint64)),
          this,          SLOT(onStatusReport(int, const QString&, quint64)));

  int headerCount = mpVideoStream->createHeaderList();
  if (headerCount <= 0) {
    throw new TTDataFormatException(__FILE__, __LINE__,
        QString(tr("Failed to parse video stream headers: %1")).arg(fInfo.filePath()));
  }

  int indexCount = mpVideoStream->createIndexList();
  if (indexCount <= 0) {
    throw new TTDataFormatException(__FILE__, __LINE__,
        QString(tr("Failed to create video index: %1")).arg(fInfo.filePath()));
  }

  if (mpVideoStream->indexList() != nullptr) {
    mpVideoStream->indexList()->sortDisplayOrder();
  }

  if (mpVideoType != 0) delete mpVideoType;
  mpVideoType = 0;

  emit finished(mpAVItem, mpVideoStream, mOrder, mDemuxedAudio);
}
