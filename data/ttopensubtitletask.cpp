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
// TTOPENSUBTITLETASK
// ----------------------------------------------------------------------------

#include "ttopensubtitletask.h"

#include "../common/ttexception.h"
#include "../common/ttsettings.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttavstream.h"
#include "ttavlist.h"

#include <QDebug>

/**
 * Open subtitle stream task
 */
TTOpenSubtitleTask::TTOpenSubtitleTask(TTAVItem* avItem, QString filePath, int order) :
                    TTThreadTask("OpenSubtitleTask")
{
  mpAVItem         = avItem;
  mOrder           = order;
  mFilePath        = filePath;
  mpSubtitleType   = 0;
  mpSubtitleStream = 0;
}

/**
 * Operation abort request
 */
void TTOpenSubtitleTask::onUserAbort()
{
  abort();

  if (mpSubtitleStream != 0)
  {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "open subtitle stream abort; subtitleStream is not null";
    mpSubtitleStream->setAbort(true);
  }
}

/**
 * Clean up after operation
 */
void TTOpenSubtitleTask::cleanUp()
{
  if (mpSubtitleType   != 0) delete mpSubtitleType;
  if (mpSubtitleStream == 0) return;

  disconnect(mpSubtitleStream, &TTSubtitleStream::statusReport,
             this,             qOverload<int, const QString&, quint64>(&TTOpenSubtitleTask::onStatusReport));
}

/**
 * Task operation method
 */
void TTOpenSubtitleTask::operation()
{  
  try
  {
    mpSubtitleType = new TTSubtitleType(mFilePath);
  }
  catch (const TTException&)
  {
    throw TTException(__FILE__, __LINE__, tr("Unsupported subtitle type or file not found %1!").arg(mFilePath));
  }

  if (mpSubtitleType->avStreamType() != TTAVTypes::srt_subtitle)
    throw TTException(__FILE__, __LINE__, tr("Unsupported subtitle type %1!").arg(mFilePath));

  mpSubtitleStream = (TTSubtitleStream*) mpSubtitleType->createSubtitleStream();

  qDebug("connect subtitle stream step signal");
  connect(mpSubtitleStream, &TTSubtitleStream::statusReport,
          this,             qOverload<int, const QString&, quint64>(&TTOpenSubtitleTask::onStatusReport));
  qDebug("create subtitle stream header list");
  mpSubtitleStream->createHeaderList();

  emit finished(mpAVItem, mpSubtitleStream, mOrder);
}

