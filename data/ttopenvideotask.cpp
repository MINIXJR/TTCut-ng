/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
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

#include "ttopenvideotask.h"

#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttmpeg2videostream.h"

#include <QDir>
#include <QDebug>
#include <QStringList>

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

  // TTCut only works with elementary streams:
  //   Video: .m2v, .264/.h264, .265/.h265
  //   Audio: .ac3, .mp2/.mpa
  //   Subtitles: .srt
  // Container files must be demuxed first using ttcut-demux
  QString suffix = fInfo.suffix().toLower();
  QStringList containerExtensions = {"ts", "m2ts", "mts", "mkv", "mp4", "m4v", "mov", "avi", "mpg", "mpeg", "vob"};

  if (containerExtensions.contains(suffix)) {
    throw new TTDataFormatException(__FILE__, __LINE__,
        QString(tr("Container format detected: %1\n\n"
                   "TTCut only works with elementary streams.\n"
                   "Please demux first using: ttcut-demux %2\n\n"
                   "Supported formats:\n"
                   "  Video: .m2v, .264, .265\n"
                   "  Audio: .ac3, .mp2\n"
                   "  Subtitles: .srt"))
            .arg(suffix.toUpper()).arg(fInfo.fileName()));
  }

  // Open the video stream (elementary stream only)
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

  qDebug() << "TTOpenVideoTask: Created video stream, type =" << mpVideoStream->streamType();

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
