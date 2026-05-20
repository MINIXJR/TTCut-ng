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

#include <QDebug>

/**
 * Open audio stream task
 */
TTOpenAudioTask::TTOpenAudioTask(TTAVItem* avItem, QString filePath, int order) :
                 TTThreadTask("OpenAudioTask")
{
  mpAVItem      = avItem;
  mOrder        = order;
	mFilePath     = filePath;
  mpAudioType   = 0;
	mpAudioStream = 0;
}

/**
 * Operation abort request
 */
void TTOpenAudioTask::onUserAbort()
{
  abort();

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

	disconnect(mpAudioStream, &TTAudioStream::statusReport,
			   	   this,          qOverload<int, const QString&, quint64>(&TTOpenAudioTask::onStatusReport));
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

	mpAudioStream = (TTAudioStream*) mpAudioType->createAudioStream();

  connect(mpAudioStream, &TTAudioStream::statusReport,
				  this,          qOverload<int, const QString&, quint64>(&TTOpenAudioTask::onStatusReport));

	mpAudioStream->createHeaderList();

	emit finished(mpAVItem, mpAudioStream, mOrder);
}

