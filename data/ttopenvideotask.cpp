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
// TTOPENVIDEOTASK
// ----------------------------------------------------------------------------

#include "ttopenvideotask.h"

#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../common/ttsettings.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavtypes.h"

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

  disconnect(mpVideoStream, &TTVideoStream::statusReport,
             this,          qOverload<int, const QString&, quint64>(&TTOpenVideoTask::onStatusReport));
}

/**
 * Task operation method
 */
void TTOpenVideoTask::operation()
{
  QFileInfo fInfo(mFileName);

  if (!fInfo.exists())
    throw TTFileNotFoundException(__FILE__, __LINE__, tr("file %1 does not exists!").arg(fInfo.filePath()));

  QString videoFilePath = fInfo.absoluteFilePath();

  // TTCut only works with elementary streams:
  //   Video: .m2v, .264/.h264, .265/.h265
  //   Audio: .ac3, .mp2/.mpa
  //   Subtitles: .srt
  // Container files must be demuxed first using ttcut-demux
  QString suffix = fInfo.suffix().toLower();
  QStringList containerExtensions = {"ts", "m2ts", "mts", "mkv", "mp4", "m4v", "mov", "avi", "mpg", "mpeg", "vob"};

  if (containerExtensions.contains(suffix)) {
    throw TTDataFormatException(__FILE__, __LINE__,
        tr("Container format detected: %1\n\n"
                   "TTCut only works with elementary streams.\n"
                   "Please demux first using: ttcut-demux %2\n\n"
                   "Supported formats:\n"
                   "  Video: .m2v, .264, .265\n"
                   "  Audio: .ac3, .mp2\n"
                   "  Subtitles: .srt")
            .arg(suffix.toUpper()).arg(fInfo.fileName()));
  }

  // Open the video stream (elementary stream only)
  mpVideoType = new TTVideoType(videoFilePath);

  TTAVTypes::AVStreamType streamType = mpVideoType->avStreamType();
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Video stream type:" << streamType;

  // Check for supported video types
  if (streamType != TTAVTypes::mpeg2_demuxed_video &&
      streamType != TTAVTypes::h264_video &&
      streamType != TTAVTypes::h265_video) {
    throw TTDataFormatException(__FILE__, __LINE__,
        tr("unsupported video type %1").arg(fInfo.filePath()));
  }

  // Use factory method to create the appropriate video stream
  mpVideoStream = mpVideoType->createVideoStream();

  if (mpVideoStream == nullptr) {
    throw TTDataFormatException(__FILE__, __LINE__,
        tr("failed to create video stream for %1").arg(fInfo.filePath()));
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "TTOpenVideoTask: Created video stream, type =" << mpVideoStream->streamType();

  connect(mpVideoStream, &TTVideoStream::statusReport,
          this,          qOverload<int, const QString&, quint64>(&TTOpenVideoTask::onStatusReport));

  int headerCount = mpVideoStream->createHeaderList();
  if (headerCount <= 0) {
    throw TTDataFormatException(__FILE__, __LINE__,
        tr("Failed to parse video stream headers: %1").arg(fInfo.filePath()));
  }

  int indexCount = mpVideoStream->createIndexList();
  if (indexCount <= 0) {
    throw TTDataFormatException(__FILE__, __LINE__,
        tr("Failed to create video index: %1").arg(fInfo.filePath()));
  }

  if (mpVideoStream->indexList() != nullptr) {
    mpVideoStream->indexList()->sortDisplayOrder();
  }

  if (mpVideoType != 0) delete mpVideoType;
  mpVideoType = 0;

  emit finished(mpAVItem, mpVideoStream, mOrder, mDemuxedAudio);
}
