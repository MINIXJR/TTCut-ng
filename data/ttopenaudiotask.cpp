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
// TTOPENAUDIOTASK
// ----------------------------------------------------------------------------

#include "ttopenaudiotask.h"

#include "../common/ttexception.h"
#include "../common/ttsettings.h"
#include "../avstream/ttavtypes.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavlist.h"

#include <QPointer>
#include <QDebug>

/**
 * Open audio stream task
 */
TTOpenAudioTask::TTOpenAudioTask(TTAVItem* avItem, const QString& filePath, int order) :
                 TTThreadTask("OpenAudioTask"),
                 mFilePath(filePath)
{
  mpAVItem      = avItem;
  mOrder        = order;
  mpAudioType   = 0;
	mpAudioStream = 0;
}

/**
 * Operation abort request
 */
void TTOpenAudioTask::onUserAbort()
{
  // Defensive, as in TTOpenVideoTask::onUserAbort: abort() of a queued task
  // lets the pool release it and runs processEvents() (not measured to delete).
  QPointer<TTOpenAudioTask> self(this);
  abort();
  if (!self) return;

  QMutexLocker lock(&mStreamMutex);
	if (mpAudioStream != 0) {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "open audio stream abort; audioStream is not null";
    mpAudioStream->setAbort(true);
  }
}

/**
 * Clean up after operation
 */
void TTOpenAudioTask::cleanUp()
{
  if (mpAudioType   != 0) delete mpAudioType;
  if (mpAudioStream == 0) return;

  unlinkStatusSource(mpAudioStream);

  // Cancelled or failed: nobody else ever saw the stream (and its open file).
  if (!mStreamHandedOver) {
    QMutexLocker lock(&mStreamMutex);
    delete mpAudioStream;
    mpAudioStream = nullptr;
  }
}

/**
 * Task operation method
 */
void TTOpenAudioTask::operation()
{  
	try
	{
		mpAudioType = new TTAudioType(mFilePath);
	}
	catch (const TTException&)
	{
    throw TTException(__FILE__, __LINE__, tr("Unsupported audio type or file not found %1!").arg(mFilePath));
	}

	if (mpAudioType->avStreamType() != TTAVTypes::mpeg_audio &&
			mpAudioType->avStreamType() != TTAVTypes::ac3_audio) 
    throw TTException(__FILE__, __LINE__, tr("Unsupported audio type %1!").arg(mFilePath));

  {
    QMutexLocker lock(&mStreamMutex);
    mpAudioStream = static_cast<TTAudioStream*>(mpAudioType->createAudioStream());
  }

  linkStatusSource(mpAudioStream);

	mpAudioStream->createHeaderList();

  mStreamHandedOver = true;
	emit finished(mpAVItem, mpAudioStream, mOrder);
}

