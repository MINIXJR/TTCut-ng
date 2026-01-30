//*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / www.tritime.org                         */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttavdata.cpp                                                    */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 12/09/2008 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTAVDATA
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

#include <algorithm>
#include <cstdio>

#include <QMessageBox>

#include "ttaudiolist.h"
#include "ttcutlist.h"
#include "ttavdata.h"
#include "ttmuxlistdata.h"
#include "ttcutprojectdata.h"
#include "ttcutlist.h"
#include "ttaudiolist.h"
#include "ttcutparameter.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/istatusreporter.h"

#include "../extern/ttmplexprovider.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../avstream/ttesinfo.h"
#include "../extern/ttffmpegwrapper.h"
#include "../extern/ttessmartcut.h"

#include "ttopenvideotask.h"
#include "ttopenaudiotask.h"
#include "ttopensubtitletask.h"
#include "ttcutpreviewtask.h"
#include "ttcutvideotask.h"
#include "ttcutaudiotask.h"
#include "ttcutsubtitletask.h"
#include "ttframesearchtask.h"

#include <QThreadPool>
#include <QList>
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QTime>

#include "../avstream/ttavtypes.h"

/* /////////////////////////////////////////////////////////////////////////////
 * Class TTAVData
 */
TTAVData::TTAVData()
{
	mpThreadTaskPool  = new TTThreadTaskPool();
	cutPreviewTask    = 0;

	log               = TTMessageLogger::getInstance();
	mpCurrentAVItem   = 0;
  mpMuxList         = new TTMuxListData();
	mpAVList          = new TTAVList();
	mpCutList         = new TTCutList();
	mpMarkerList      = new TTMarkerList();

	connect(mpThreadTaskPool, SIGNAL(init()), this, SLOT(onThreadPoolInit()));
  connect(mpThreadTaskPool, SIGNAL(exit()), this, SLOT(onThreadPoolExit()));
  connect(mpThreadTaskPool, SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)),
	                          SIGNAL(statusReport(TTThreadTask*, int, const QString&, quint64)));

  connect(mpAVList,  SIGNAL(itemAppended(const TTAVItem&)),                   SIGNAL(avItemAppended(const TTAVItem&)));
	connect(mpAVList,  SIGNAL(itemRemoved(int)),                                SIGNAL(avItemRemoved(int)));
	connect(mpAVList,  SIGNAL(itemUpdated(const TTAVItem&, const TTAVItem&)),   SIGNAL(avItemUpdated(const TTAVItem&, const TTAVItem&)));
	connect(mpAVList,  SIGNAL(itemsSwapped(int, int)),                          SIGNAL(avItemsSwapped(int, int)));

	connect(mpCutList, SIGNAL(itemAppended(const TTCutItem&)),                  SIGNAL(cutItemAppended(const TTCutItem&)));
	connect(mpCutList, SIGNAL(itemRemoved(int)),                                SIGNAL(cutItemRemoved(int)));
	connect(mpCutList, SIGNAL(orderUpdated(const TTCutItem&, int)),             SIGNAL(cutOrderUpdated(const TTCutItem&, int)));
	connect(mpCutList, SIGNAL(itemUpdated(const TTCutItem&, const TTCutItem&)), SIGNAL(cutItemUpdated(const TTCutItem&, const TTCutItem&)));

	connect(mpMarkerList, SIGNAL(itemAppended(const TTMarkerItem&)),                     SIGNAL(markerAppended(const TTMarkerItem&)));
	connect(mpMarkerList, SIGNAL(itemRemoved(int)),                                      SIGNAL(markerRemoved(int)));
	connect(mpMarkerList, SIGNAL(orderUpdated(const TTMarkerItem&, int)),                SIGNAL(markerUpdated(const TTMarkerItem&, int)));
	connect(mpMarkerList, SIGNAL(itemUpdated(const TTMarkerItem&, const TTMarkerItem&)), SIGNAL(markerUpdated(const TTMarkerItem&, const TTMarkerItem&)));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTAVData::~TTAVData()
{
	clear();

	if (mpAVList         != 0) delete mpAVList;
	if (mpCutList        != 0) delete mpCutList;
	if (mpMarkerList     != 0) delete mpMarkerList;
  if (mpMuxList        != 0) delete mpMuxList;
  if (mpThreadTaskPool != 0) delete mpThreadTaskPool;
}

/* /////////////////////////////////////////////////////////////////////////////
 * clear
 */
void TTAVData::clear()
{
	mpAVList->clear();
	mpCutList->clear();
	mpMarkerList->clear();
}

/*!
 * appendAudioStream
 */
void TTAVData::appendAudioStream(TTAVItem* avItem, const QFileInfo& fInfo, int)
{
  doOpenAudioStream(avItem, fInfo.absoluteFilePath());
}

/*!
 * appendSubtitleStream
 */
void TTAVData::appendSubtitleStream(TTAVItem* avItem, const QFileInfo& fInfo, int)
{
  doOpenSubtitleStream(avItem, fInfo.absoluteFilePath());
}

/* /////////////////////////////////////////////////////////////////////////////
 * Cut list handling
 */

/*!
 * appendCutEntry
 */
void TTAVData::appendCutEntry(TTAVItem* avItem, int cutIn, int cutOut)
{
	for (int i = 0; i < mpAVList->count(); i++) {
		mpAVList->at(i)->canCutWith(avItem, cutIn, cutOut);
	}

	avItem->appendCutEntry(cutIn, cutOut);
}

/*!
 * copyCutEntry
 */
void TTAVData::copyCutEntry(const TTCutItem& cutItem)
{
	if (mpCurrentAVItem == 0)
		throw new TTInvalidOperationException("No current AV-Data set!");

	appendCutEntry(mpCurrentAVItem, cutItem.cutIn(), cutItem.cutOut());
}

/*!
 * sortCutItemsByOrder
 */
void TTAVData::sortCutItemsByOrder()
{
	mpCutList->sortByOrder();
	emit cutDataReloaded();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Marker handling
 */

/*!
 * appendMarker
 */
void TTAVData::appendMarker(TTAVItem* avItem, int markerPos)
{
	avItem->appendMarker(markerPos);
}

/*!
 * onAppendMarker
 */
void TTAVData::onAppendMarker(int markerPos)
{
	if (mpCurrentAVItem == 0)
		return;

	mpCurrentAVItem->appendMarker(markerPos);
}

/*!
 * onRemoveMarker
 */
void TTAVData::onRemoveMarker(const TTMarkerItem& mItem)
{
	TTAVItem* avItem = mItem.avDataItem();
	avItem->removeMarker(mItem);
}

/*!
 * copyMarker
 */
void TTAVData::copyMarker(const TTMarkerItem& markerItem)
{
	if (mpCurrentAVItem == 0)
		throw new TTInvalidOperationException("No current AV-Data set!");

	appendMarker(mpCurrentAVItem, markerItem.markerPos());
}

/*!
 * sortMarkerByOrder
 */
void TTAVData::sortMarkerByOrder()
{
	mpMarkerList->sortByOrder();
	emit markerDataReloaded();
}

int TTAVData::totalProcess() const
{
	return mpThreadTaskPool->overallPercentage();
}

QTime TTAVData::totalTime() const
{
	return mpThreadTaskPool->overallTime();
}

/* /////////////////////////////////////////////////////////////////////////////
 * createAVDataItem
 * Create an AVData item, connect Signals and Slots
 */
TTAVItem* TTAVData::createAVItem()
{
	try
  {
		TTAVItem* avItem = new TTAVItem(0);

		connect(avItem->mpCutList,  SIGNAL(itemAppended(const TTCutItem&)),
            mpCutList,          SLOT(onAppendItem(const TTCutItem&)));
		connect(avItem->mpCutList,  SIGNAL(itemRemoved(const TTCutItem&)),
            mpCutList,          SLOT(onRemoveItem(const TTCutItem&)));
		connect(avItem->mpCutList,  SIGNAL(itemUpdated(const TTCutItem&, const TTCutItem&)),
            mpCutList,          SLOT(onUpdateItem(const TTCutItem&, const TTCutItem&)));
		connect(mpCutList,          SIGNAL(orderUpdated(const TTCutItem&, int)),
            avItem->mpCutList,  SLOT(onUpdateOrder(const TTCutItem&, int)));

		connect(avItem->mpMarkerList, SIGNAL(itemAppended(const TTMarkerItem&)),
            mpMarkerList,         SLOT(onAppendItem(const TTMarkerItem&)));
		connect(avItem->mpMarkerList, SIGNAL(itemRemoved(const TTMarkerItem&)),
            mpMarkerList,         SLOT(onRemoveItem(const TTMarkerItem&)));
		connect(avItem->mpMarkerList, SIGNAL(itemUpdated(const TTMarkerItem&, const TTMarkerItem&)),
            mpMarkerList,         SLOT(onUpdateItem(const TTMarkerItem&, const TTMarkerItem&)));
		connect(mpMarkerList,         SIGNAL(orderUpdated(const TTMarkerItem&, int)),
            avItem->mpMarkerList, SLOT(onUpdateOrder(const TTMarkerItem&, int)));

  	return avItem;
	}
  catch (const TTException& ex)
  {
		log->fatalMsg(__FILE__, __LINE__, tr("exception in createAVDataItem!"));
		return 0;
	}
}

/*!
 * openAVStreams
 * Open the video stream and all according audio and subtitle streams and add them to AVData
 */
void TTAVData::openAVStreams(const QString& videoFilePath)
{
  connect(mpThreadTaskPool, SIGNAL(aborted()),
          this,             SLOT(onOpenAVStreamsAborted()));

  TTAVItem* avItem = doOpenVideoStream(videoFilePath);

  // Check if this is a container file - if so, skip auto-loading audio/subtitles
  // because they will be demuxed from the container instead
  QFileInfo fileInfo(videoFilePath);
  QString suffix = fileInfo.suffix().toLower();
  bool isContainer = (suffix == "ts" || suffix == "m2ts" || suffix == "mkv" ||
                      suffix == "mp4" || suffix == "m4v" || suffix == "mpg" ||
                      suffix == "mpeg" || suffix == "vob");

  if (!isContainer) {
    // Only auto-load audio for elementary stream files
    QFileInfoList audioInfoList = getAudioNames(QFileInfo(videoFilePath));
    QListIterator<QFileInfo> audioInfo(audioInfoList);

    while (audioInfo.hasNext()) {
      doOpenAudioStream(avItem, audioInfo.next().absoluteFilePath());
    }

    // Only auto-load subtitles for elementary stream files
    QFileInfoList subtitleInfoList = getSubtitleNames(QFileInfo(videoFilePath));
    QListIterator<QFileInfo> subtitleInfo(subtitleInfoList);

    while (subtitleInfo.hasNext()) {
      doOpenSubtitleStream(avItem, subtitleInfo.next().absoluteFilePath());
    }
  }
}

// *****************************************************************************
//! Just for testing purpose! Remove it!
//! Returns a reference to the thread pool
TTThreadTaskPool* TTAVData::threadTaskPool() const
{
  return mpThreadTaskPool;
}

//! Returns a reference to the main cut list
TTCutList* TTAVData::cutList() const
{
  return mpCutList;
}
// *****************************************************************************


/*!
 * doOpenVideoStream
 */
TTAVItem* TTAVData::doOpenVideoStream(const QString& filePath, int order)
{
  TTAVItem*        avItem        = createAVItem();
  TTOpenVideoTask* openVideoTask = new TTOpenVideoTask(avItem, filePath, order);

  connect(openVideoTask, SIGNAL(finished(TTAVItem*, TTVideoStream*, int, const QString&)),
          this,          SLOT(onOpenVideoFinished(TTAVItem*, TTVideoStream*, int, const QString&)),
          Qt::QueuedConnection);

  int audioCount = getAudioNames(QFileInfo(filePath)).count();

  mpThreadTaskPool->init(audioCount+1);
  mpThreadTaskPool->start(openVideoTask);

  return avItem;
}

/*!
 * doOpenAudioStream
 */
void TTAVData::doOpenAudioStream(TTAVItem* avItem, const QString& filePath, int order)
{
  TTOpenAudioTask* openAudioTask = new TTOpenAudioTask(avItem, filePath, order);

  connect(openAudioTask, SIGNAL(finished(TTAVItem*, TTAudioStream*, int)),
          this,          SLOT(onOpenAudioFinished(TTAVItem*, TTAudioStream*, int)),
          Qt::QueuedConnection);

  mpThreadTaskPool->start(openAudioTask);
}

/*!
 * doOpenSubtitleStream
 */
void TTAVData::doOpenSubtitleStream(TTAVItem* avItem, const QString& filePath, int order)
{
  TTOpenSubtitleTask* openSubtitleTask = new TTOpenSubtitleTask(avItem, filePath, order);

  connect(openSubtitleTask, SIGNAL(finished(TTAVItem*, TTSubtitleStream*, int)),
          this,             SLOT(onOpenSubtitleFinished(TTAVItem*, TTSubtitleStream*, int)),
          Qt::QueuedConnection);

  mpThreadTaskPool->start(openSubtitleTask);
}

/*!
 * onOpenVideoFinished
 */
void TTAVData::onOpenVideoFinished(TTAVItem* avItem, TTVideoStream* vStream, int, const QString& demuxedAudio)
{
  if (avItem == nullptr) {
    return;
  }

  qDebug() << "TTAVData::onOpenVideoFinished: vStream type =" << (vStream ? vStream->streamType() : -1);

  avItem->setVideoStream(vStream);

  qDebug() << "TTAVData::onOpenVideoFinished: avItem->videoStream() type =" << (avItem->videoStream() ? avItem->videoStream()->streamType() : -1);

  if (mpAVList == nullptr) {
    return;
  }

  mpAVList->append(avItem);

  this->avDataReloaded();
  this->cutDataReloaded();
  this->markerDataReloaded();

  mpCurrentAVItem = avItem;
  emit currentAVItemChanged(avItem);

  // Load demuxed audio if available
  if (!demuxedAudio.isEmpty()) {
    QFileInfo audioInfo(demuxedAudio);
    if (audioInfo.exists()) {
      qDebug() << "Loading demuxed audio:" << demuxedAudio;
      doOpenAudioStream(avItem, demuxedAudio);
    }
  }
}

void TTAVData::onOpenAVStreamsAborted()
{
  disconnect(mpThreadTaskPool, SIGNAL(aborted()),
             this,             SLOT(onOpenAVStreamsAborted()));

  mpCurrentAVItem = (mpAVList->count() > 0) ? mpAVList->at(mpAVList->count()-1) : 0;
  emit currentAVItemChanged(mpCurrentAVItem);
}

/*!
 * onOpenAudioFinished
 */
void TTAVData::onOpenAudioFinished(TTAVItem* avItem, TTAudioStream* aStream, int order)
{
  if (avItem  == 0) return;
  if (aStream == 0) return;

  avItem->appendAudioEntry(aStream, order);
}

/*!
 * onOpenAudioAborted
 */
void TTAVData::onOpenAudioAborted(TTAVItem*)
{
  qDebug("TTAVData::onOpenAudioAborted called...");
}

/*!
 * onOpenSubtitleFinished
 */
void TTAVData::onOpenSubtitleFinished(TTAVItem* avItem, TTSubtitleStream* sStream, int order)
{
  if (avItem  == 0) return;
  if (sStream == 0) return;

  avItem->appendSubtitleEntry(sStream, order);
}

/*!
 * onOpenSubtitleAborted
 */
void TTAVData::onOpenSubtitleAborted(TTAVItem*)
{
  qDebug("TTAVData::onOpenSubtitleAborted called...");
}

/*  ////////////////////////////////////////////////////////////////////////////
 * Slots for av data list
 */

void TTAVData::onChangeCurrentAVItem(TTAVItem* avItem)
{
	mpCurrentAVItem = avItem;

	emit currentAVItemChanged(avItem);
}

void TTAVData::onChangeCurrentAVItem(int index)
{
	if (index < 0 || index > mpAVList->count()) return;

	mpCurrentAVItem = avItemAt(index);

	emit currentAVItemChanged(mpCurrentAVItem);
}

void TTAVData::onRemoveAVItem(int index)
{
	if (index-1 >= 0 && avCount() > 1)
		mpCurrentAVItem = avItemAt(index-1);

	if (index+1 < avCount() && avCount() > 1)
		mpCurrentAVItem = avItemAt(index+1);

	if (avCount() > 1)
	  emit currentAVItemChanged(mpCurrentAVItem);

	//  mpCurrentAVItem = (avCount() > 0)
//      ? avItemAt(avCount()-1)
//      : 0;
  //emit currentAVItemChanged(mpCurrentAVItem);

  mpAVList->removeAt(index);

  if (avCount() == 0) {
  	mpCurrentAVItem = 0;
    emit currentAVItemChanged(mpCurrentAVItem);
  }
}

void TTAVData::onSwapAVItems(int oldIndex, int newIndex)
{
	mpAVList->swap(oldIndex, newIndex);
}


void TTAVData::onRemoveCutItem(const TTCutItem& item)
{
	TTAVItem* avItem = item.avDataItem();
	avItem->removeCutEntry(item);
}

void TTAVData::onCutOrderChanged(int oldIndex, int newIndex)
{
	mpCutList->swap(oldIndex, newIndex);
}

void TTAVData::onMarkerOrderChanged(int oldIndex, int newIndex)
{
	mpMarkerList->swap(oldIndex, newIndex);
}

//! Search equal frame
void TTAVData::onDoFrameSearch(TTAVItem* avItem, int startIndex)
{
	if (mpCurrentAVItem == 0) return;

	TTFrameSearchTask* frameSearch = new TTFrameSearchTask(
      avItem->videoStream(),          startIndex,
      mpCurrentAVItem->videoStream(), mpCurrentAVItem->videoStream()->currentIndex());

	connect(frameSearch, SIGNAL(finished(int)), this, SIGNAL(foundEqualFrame(int)));

	mpThreadTaskPool->start(frameSearch);
}

//! User request to abort current operation
void TTAVData::onUserAbortRequest()
{
	mpThreadTaskPool->onUserAbortRequest();
}

void TTAVData::onThreadPoolInit()
{
  emit statusReport(0, StatusReportArgs::Init, tr("starting thread pool"), 0);
}

void TTAVData::onThreadPoolExit()
{
  emit statusReport(0, StatusReportArgs::Exit, tr("exiting thread pool"), 0);
  emit threadPoolExit();
}

/* /////////////////////////////////////////////////////////////////////////////
 * getAudioNames
 * Search for audiofiles acording to the video file name; Valid audio extensions
 * are: mpa, mp2, ac3
 */
QFileInfoList TTAVData::getAudioNames(const QFileInfo& vFileInfo)
{
	QDir audioDir(vFileInfo.absoluteDir());

	QStringList audioFilters;
	audioFilters << vFileInfo.completeBaseName() + "*" + ".mpa"
			<< vFileInfo.completeBaseName() + "*" + ".mp2"
			<< vFileInfo.completeBaseName() + "*" + ".ac3";

	audioDir.setNameFilters(audioFilters);
	audioDir.setFilter(QDir::Files);

	return audioDir.entryInfoList();
}

/* /////////////////////////////////////////////////////////////////////////////
 * getSubtitleNames
 * Search for subtitle files according to the video file name; Valid subtitle extensions
 * are: srt
 */
QFileInfoList TTAVData::getSubtitleNames(const QFileInfo& vFileInfo)
{
	QDir subtitleDir(vFileInfo.absoluteDir());

	QStringList subtitleFilters;
	subtitleFilters << vFileInfo.completeBaseName() + "*" + ".srt";

	subtitleDir.setNameFilters(subtitleFilters);
	subtitleDir.setFilter(QDir::Files);

	return subtitleDir.entryInfoList();
}

// ////////////////////////////////////////////////////////////////////////////
// Project file related methods 
/**
 * Write the xml project file
 */
void TTAVData::writeProjectFile(const QFileInfo& fInfo)
{
	TTCutProjectData* prj = new TTCutProjectData(fInfo);

	for (int i = 0; i < mpAVList->count(); i++) {
		prj->serializeAVDataItem(mpAVList->at(i));
	}

	prj->writeXml();

	delete prj;
}

/**
 * Read the TTCut xml project file
 */
void TTAVData::readProjectFile(const QFileInfo& fInfo)
{
  connect(mpThreadTaskPool, SIGNAL(exit()),    this, SLOT(onReadProjectFileFinished()));
  connect(mpThreadTaskPool, SIGNAL(aborted()), this, SLOT(onReadProjectFileAborted()));

  mpProjectData = new TTCutProjectData(fInfo);

  try
  {
	  mpProjectData->readXml();
	  mpProjectData->deserializeAVDataItem(this);
  }
  catch (TTException* ex)
  {
		log->errorMsg(__FILE__, __LINE__, ex->getMessage());
    onReadProjectFileAborted();
  }
}

/**
 * Reading TTCut project file finished
 */
void TTAVData::onReadProjectFileFinished()
{
  disconnect(mpThreadTaskPool, SIGNAL(aborted()), this, SLOT(onReadProjectFileAborted()));
  disconnect(mpThreadTaskPool, SIGNAL(exit()),    this, SLOT(onReadProjectFileFinished()));

  emit avDataReloaded();

  if (avCount() > 0)
    emit currentAVItemChanged(avItemAt(0));

  emit readProjectFileFinished(mpProjectData->filePath());
  
  delete mpProjectData;
  mpProjectData = 0;
}

/**
 * Reading TTCut project file aborted or error reading project file
 */
void TTAVData::onReadProjectFileAborted()
{
  qDebug() << "TAVData::onReadProjectFileAborted";
  disconnect(mpThreadTaskPool, SIGNAL(exit()), this, SLOT(onReadProjectFileFinished()));
  disconnect(mpThreadTaskPool, SIGNAL(aborted()), this, SLOT(onReadProjectFileAborted()));

  emit currentAVItemChanged(0);

  if (mpProjectData != 0) {
    delete mpProjectData;
    mpProjectData = 0;
  }
}

// /////////////////////////////////////////////////////////////////////////////
// Cut preview
/**
 * Create the cut preview clips
 */
void TTAVData::doCutPreview(TTCutList* cutList)
{
  if (cutPreviewTask != 0) delete cutPreviewTask;
  cutPreviewTask = new TTCutPreviewTask(this, cutList);

  connect(cutPreviewTask,   SIGNAL(finished(TTCutList*)),
          this,             SLOT(onCutPreviewFinished(TTCutList*)));
  connect(mpThreadTaskPool, SIGNAL(aborted()),
					this,             SLOT(onCutPreviewAborted()));

  mpThreadTaskPool->init(cutList->count()*2);
  mpThreadTaskPool->start(cutPreviewTask);
}

//! Finished creating cut preview clips
void TTAVData::onCutPreviewFinished(TTCutList* cutList)
{
	emit cutPreviewFinished(cutList);
}

//! Cut preview aborted by user
void TTAVData::onCutPreviewAborted()
{
  disconnect(cutPreviewTask,   SIGNAL(finished(TTCutList*)),
             this,             SLOT(onCutPreviewFinished(TTCutList*)));
  disconnect(mpThreadTaskPool, SIGNAL(aborted()),
			   		 this,             SLOT(onCutPreviewAborted()));

	if (cutPreviewTask != 0) {
    delete cutPreviewTask;
    cutPreviewTask = 0;
  }
}

/*!
 * createAudioCutFileName
 */
QString TTAVData::createAudioCutFileName(QString cutBaseFileName, QString audioFileName, int index)
{
  QString audioCutFileName = QString("%1_%2.%3").
    arg(QFileInfo(cutBaseFileName).completeBaseName()).
    arg(index, 3, 10, QLatin1Char('0')).
    arg(QFileInfo(audioFileName).suffix());

  return QFileInfo(QDir(TTCut::cutDirPath), audioCutFileName).absoluteFilePath();
}

/*!
 * createSubtitleCutFileName
 */
QString TTAVData::createSubtitleCutFileName(QString cutBaseFileName, QString subtitleFileName, int index)
{
  QString subtitleCutFileName = QString("%1_%2.%3").
    arg(QFileInfo(cutBaseFileName).completeBaseName()).
    arg(index, 3, 10, QLatin1Char('0')).
    arg(QFileInfo(subtitleFileName).suffix());

  return QFileInfo(QDir(TTCut::cutDirPath), subtitleCutFileName).absoluteFilePath();
}

// /////////////////////////////////////////////////////////////////////////////
// Audio and video cut
//
//! Do the audio and video cut for given cut-list
void TTAVData::onDoCut(QString tgtFileName, TTCutList* cutList)
{
  if (cutList == 0) cutList = mpCutList;

  // Detect stream type from first cut item
  TTVideoStream* firstStream = cutList->at(0).avDataItem()->videoStream();
  TTAVTypes::AVStreamType streamType = firstStream->streamType();
  bool isH264H265 = (streamType == TTAVTypes::h264_video || streamType == TTAVTypes::h265_video);

  if (isH264H265) {
    // For H.264/H.265: use ffmpeg directly since native cutting is not implemented
    doH264Cut(tgtFileName, cutList);
    return;
  }

  // For MPEG-2: use traditional cutting workflow
  cutVideoTask = new TTCutVideoTask(this);
  cutVideoTask->init(tgtFileName, cutList);

  connect(mpThreadTaskPool, SIGNAL(exit()),    this, SLOT(onCutFinished()));
  connect(mpThreadTaskPool, SIGNAL(aborted()), this, SLOT(onCutAborted()));

  mpThreadTaskPool->init(cutList->count()*2);
  mpThreadTaskPool->start(cutVideoTask);

  // all video must have the same count of audio streams!
  for (int i = 0; i < cutList->at(0).avDataItem()->audioCount(); i++) {
    TTAudioStream* audioStream = cutList->at(0).avDataItem()->audioStreamAt(i);

    QString tgtAudioFilePath = createAudioCutFileName(tgtFileName, audioStream->fileName(), i+1);

    log->debugMsg(__FILE__, __LINE__, QString("current audio stream %1").arg(audioStream->fileName()));
    log->debugMsg(__FILE__, __LINE__, QString("audio cut file %1").arg(tgtAudioFilePath));

    // audio file exists
    if (QFileInfo(tgtAudioFilePath).exists()) {
      // TODO: Warning about deleting file
      log->warningMsg(__FILE__, __LINE__, QString(tr("deleting existing audio cut file: %1").arg(tgtAudioFilePath)));
      QFile tempFile(tgtAudioFilePath);
      tempFile.remove();
      tempFile.close();
    }

    cutAudioTask = new TTCutAudioTask();
    cutAudioTask->init(tgtAudioFilePath, cutList, i, cutVideoTask->muxListItem());

    mpThreadTaskPool->start(cutAudioTask);
  }

  // cut subtitle streams
  for (int i = 0; i < cutList->at(0).avDataItem()->subtitleCount(); i++) {
    TTSubtitleStream* subtitleStream = cutList->at(0).avDataItem()->subtitleStreamAt(i);

    QString tgtSubtitleFilePath = createSubtitleCutFileName(tgtFileName, subtitleStream->fileName(), i+1);

    log->debugMsg(__FILE__, __LINE__, QString("current subtitle stream %1").arg(subtitleStream->fileName()));
    log->debugMsg(__FILE__, __LINE__, QString("subtitle cut file %1").arg(tgtSubtitleFilePath));

    // subtitle file exists
    if (QFileInfo(tgtSubtitleFilePath).exists()) {
      log->warningMsg(__FILE__, __LINE__, QString(tr("deleting existing subtitle cut file: %1").arg(tgtSubtitleFilePath)));
      QFile tempFile(tgtSubtitleFilePath);
      tempFile.remove();
      tempFile.close();
    }

    cutSubtitleTask = new TTCutSubtitleTask();
    cutSubtitleTask->init(tgtSubtitleFilePath, cutList, i, cutVideoTask->muxListItem());

    mpThreadTaskPool->start(cutSubtitleTask);
  }
}

//! Do H.264/H.265 cut using ffmpeg directly
// Helper: Get stream start time offset using ffprobe
static double getStreamStartTime(const QString& filePath)
{
  QString cmd = QString("ffprobe -v error -select_streams v:0 "
                        "-show_entries stream=start_time -of csv=p=0 \"%1\" 2>/dev/null")
      .arg(filePath);

  FILE* pipe = popen(qPrintable(cmd), "r");
  if (pipe) {
    char buffer[128];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      QString line = QString(buffer).trimmed();
      pclose(pipe);
      if (!line.isEmpty() && line != "N/A") {
        bool ok;
        double startTime = line.toDouble(&ok);
        if (ok) {
          return startTime;
        }
      }
    } else {
      pclose(pipe);
    }
  }
  return 0.0;
}

// Structure to hold keyframe timing info
struct KeyframeInfo {
  double pts;  // Presentation timestamp (display time)
  double dts;  // Decode timestamp (stream position - use this for seeking with -ss before -i)

  KeyframeInfo(double p = 0, double d = 0) : pts(p), dts(d) {}
};

// Helper: Find keyframe timestamps using ffprobe
// Returns both PTS (for re-encoding timing) and DTS (for stream-copy seeking)
static QList<KeyframeInfo> getKeyframeTimestamps(const QString& filePath, double startTime, double endTime)
{
  QList<KeyframeInfo> keyframes;

  // Get stream start time offset (DVB recordings often have large offsets)
  double streamStart = getStreamStartTime(filePath);

  // Adjust times to account for stream offset
  double absStartTime = streamStart + startTime;
  double absEndTime = streamStart + endTime;

  qDebug() << "Keyframe search: relative" << startTime << "-" << endTime
           << "absolute" << absStartTime << "-" << absEndTime;

  // Use ffprobe to get keyframe timestamps with both PTS and DTS
  // PTS = presentation time (when frame is displayed)
  // DTS = decode time (position in stream - needed for seeking with -ss before -i)
  double margin = 2.0; // 2 second margin
  QString cmd = QString("ffprobe -v error -select_streams v:0 -skip_frame nokey "
                        "-read_intervals %1\\%%2 "
                        "-show_entries frame=pts_time,pkt_dts_time,pict_type -of csv=p=0 \"%3\" 2>&1")
      .arg(qMax(0.0, absStartTime - margin), 0, 'f', 3)
      .arg(absEndTime + margin, 0, 'f', 3)
      .arg(filePath);

  qDebug() << "ffprobe command:" << cmd;

  FILE* pipe = popen(qPrintable(cmd), "r");
  if (pipe) {
    char buffer[256];
    int lineCount = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      QString line = QString(buffer).trimmed();
      lineCount++;
      if (lineCount <= 5) {
        qDebug() << "ffprobe output line:" << line;
      }
      if (!line.isEmpty()) {
        // Output format: pts_time,pkt_dts_time,pict_type (e.g., "40529.358722,40529.798722,I")
        QStringList parts = line.split(',');
        if (parts.size() >= 2) {
          bool okPts, okDts;
          double pts = parts[0].toDouble(&okPts);
          double dts = parts[1].toDouble(&okDts);
          if (okPts && pts > 0) {
            // Convert back to relative time (subtract stream start)
            double relPts = pts - streamStart;
            double relDts = okDts ? (dts - streamStart) : relPts; // Use PTS if DTS not available
            keyframes.append(KeyframeInfo(relPts, relDts));
          }
        }
      }
    }
    pclose(pipe);
    qDebug() << "Total lines read:" << lineCount << "keyframes found:" << keyframes.size();
  } else {
    qDebug() << "Failed to open pipe for ffprobe";
  }

  // If no keyframes found with -show_frames, try alternative approach
  if (keyframes.isEmpty()) {
    qDebug() << "Trying alternative keyframe detection method...";

    // Try without -read_intervals (read entire file, filter in code)
    QString cmd2 = QString("ffprobe -v error -select_streams v:0 -skip_frame nokey "
                          "-show_entries frame=pts_time,pkt_dts_time -of csv=p=0 \"%1\" 2>&1 | head -1000")
        .arg(filePath);

    qDebug() << "Alternative ffprobe command:" << cmd2;

    FILE* pipe2 = popen(qPrintable(cmd2), "r");
    if (pipe2) {
      char buffer[256];
      int lineCount = 0;
      while (fgets(buffer, sizeof(buffer), pipe2) != nullptr) {
        QString line = QString(buffer).trimmed();
        lineCount++;
        if (!line.isEmpty()) {
          QStringList parts = line.split(',');
          if (parts.size() >= 1) {
            bool okPts, okDts;
            double pts = parts[0].toDouble(&okPts);
            double dts = (parts.size() >= 2) ? parts[1].toDouble(&okDts) : pts;
            if (!okDts) dts = pts;
            if (okPts && pts > 0) {
              double relPts = pts - streamStart;
              double relDts = dts - streamStart;
              // Only include keyframes in our range (with margin)
              if (relPts >= startTime - margin && relPts <= endTime + margin) {
                keyframes.append(KeyframeInfo(relPts, relDts));
                if (keyframes.size() <= 5) {
                  qDebug() << "Found keyframe at PTS:" << relPts << "DTS:" << relDts;
                }
              }
            }
          }
        }
      }
      pclose(pipe2);
      qDebug() << "Alternative method - lines read:" << lineCount << "keyframes in range:" << keyframes.size();
    }
  }

  // Sort by PTS
  std::sort(keyframes.begin(), keyframes.end(), [](const KeyframeInfo& a, const KeyframeInfo& b) {
    return a.pts < b.pts;
  });
  return keyframes;
}

// Helper: Find nearest keyframe at or before given time (returns index, -1 if not found)
static int findKeyframeIndexBefore(const QList<KeyframeInfo>& keyframes, double time)
{
  int result = -1;
  for (int i = 0; i < keyframes.size(); i++) {
    if (keyframes[i].pts <= time + 0.001) { // Small tolerance for floating point
      result = i;
    } else {
      break;
    }
  }
  return result;
}

// Helper: Find nearest keyframe at or after given time (returns index, -1 if not found)
static int findKeyframeIndexAfter(const QList<KeyframeInfo>& keyframes, double time)
{
  for (int i = 0; i < keyframes.size(); i++) {
    if (keyframes[i].pts >= time - 0.001) { // Small tolerance for floating point
      return i;
    }
  }
  return -1;
}

// Legacy helpers for backwards compatibility (return PTS only)
static double findKeyframeBefore(const QList<KeyframeInfo>& keyframes, double time)
{
  int idx = findKeyframeIndexBefore(keyframes, time);
  return (idx >= 0) ? keyframes[idx].pts : -1;
}

static double findKeyframeAfter(const QList<KeyframeInfo>& keyframes, double time)
{
  int idx = findKeyframeIndexAfter(keyframes, time);
  return (idx >= 0) ? keyframes[idx].pts : -1;
}

void TTAVData::doH264Cut(QString tgtFileName, TTCutList* cutList)
{
  log->infoMsg(__FILE__, __LINE__, "Using avcut-style smart cut (direct writing, no global headers, GOP-based)");

  // Get source file and frame rate from first cut item
  TTAVItem* avItem = cutList->at(0).avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  QString sourceFile = vStream->filePath();
  double frameRate = vStream->frameRate();

  // Check if source is an elementary stream
  QString suffix = QFileInfo(sourceFile).suffix().toLower();
  bool isES = (suffix == "264" || suffix == "h264" ||
               suffix == "265" || suffix == "h265" || suffix == "hevc" ||
               suffix == "m2v" || suffix == "mpv");

  // For ES files: get frame rate from .info file (vStream->frameRate() may be wrong)
  if (isES) {
    QString infoFile = TTESInfo::findInfoFile(sourceFile);
    if (!infoFile.isEmpty()) {
      TTESInfo esInfo(infoFile);
      if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
        log->infoMsg(__FILE__, __LINE__, QString("ES frame rate from .info: %1 fps").arg(frameRate));
      }
    }
  }

  // Get audio file for ES workflow
  QString audioFile;
  if (isES && avItem->audioCount() > 0) {
    audioFile = avItem->audioStreamAt(0)->filePath();
    log->infoMsg(__FILE__, __LINE__, QString("Audio file: %1").arg(audioFile));
  }

  emit statusReport(StatusReportArgs::Start, tr("Cutting H.264/H.265 video..."), cutList->count());

  QString finalOutput = tgtFileName;
  if (!finalOutput.endsWith(".mkv", Qt::CaseInsensitive)) {
    QFileInfo fi(finalOutput);
    finalOutput = QFileInfo(QDir(TTCut::cutDirPath),
                           fi.completeBaseName() + ".mkv").absoluteFilePath();
  }

  // Build cut list as pairs of (startTime, endTime) in seconds - segments to KEEP
  QList<QPair<double, double>> keepList;
  for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    int startFrame = item.cutInIndex();
    int endFrame = item.cutOutIndex();

    double cutInTime = startFrame / frameRate;
    double cutOutTime = endFrame / frameRate;  // No +1, cutOut is the last frame to include

    log->infoMsg(__FILE__, __LINE__, QString("Cut %1: frames %2-%3, time %4-%5")
        .arg(i+1).arg(startFrame).arg(endFrame)
        .arg(cutInTime, 0, 'f', 3).arg(cutOutTime, 0, 'f', 3));

    keepList.append(qMakePair(cutInTime, cutOutTime));
  }

  TTFFmpegWrapper ffmpeg;
  bool success;

  // For ES files: use TTESSmartCut for frame-accurate cutting
  if (isES) {
    log->infoMsg(__FILE__, __LINE__, "ES file detected - using TTESSmartCut (frame-accurate)");
    log->infoMsg(__FILE__, __LINE__, QString("  Video: %1").arg(sourceFile));
    log->infoMsg(__FILE__, __LINE__, QString("  Frame rate: %1 fps").arg(frameRate));

    // Build frame-based cut list
    QList<QPair<int, int>> cutFrames;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      cutFrames.append(qMakePair(item.cutInIndex(), item.cutOutIndex()));
      log->infoMsg(__FILE__, __LINE__, QString("  Segment %1: frames %2-%3")
          .arg(i+1).arg(item.cutInIndex()).arg(item.cutOutIndex()));
    }

    // Initialize Smart Cut engine
    TTESSmartCut smartCut;
    connect(&smartCut, &TTESSmartCut::progressChanged, this, [this](int percent, const QString& msg) {
      emit statusReport(StatusReportArgs::Step, msg, percent);
    });

    if (!smartCut.initialize(sourceFile, frameRate)) {
      log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut init failed: %1").arg(smartCut.lastError()));
      emit statusReport(StatusReportArgs::Finished, tr("Cutting failed - could not initialize"), 0);
      return;
    }

    // Create temporary video output
    QString tempVideoFile = QFileInfo(QDir(TTCut::cutDirPath),
        QFileInfo(sourceFile).completeBaseName() + "_cut." + QFileInfo(sourceFile).suffix()).absoluteFilePath();

    // Perform frame-accurate video cut
    emit statusReport(StatusReportArgs::Step, tr("Cutting video (Smart Cut)..."), 0);
    if (!smartCut.smartCutFrames(tempVideoFile, cutFrames)) {
      log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut failed: %1").arg(smartCut.lastError()));
      emit statusReport(StatusReportArgs::Finished, tr("Cutting failed"), 0);
      return;
    }

    log->infoMsg(__FILE__, __LINE__, QString("Smart Cut complete: %1 frames re-encoded, %2 frames stream-copied")
        .arg(smartCut.framesReencoded()).arg(smartCut.framesStreamCopied()));

    // Cut audio tracks
    QStringList cutAudioFiles;
    for (int i = 0; i < avItem->audioCount(); i++) {
      TTAudioStream* audioStream = avItem->audioStreamAt(i);
      QString srcAudioFile = audioStream->filePath();

      emit statusReport(StatusReportArgs::Step, tr("Cutting audio track %1...").arg(i+1), 0);

      // Calculate audio cut times from video frame rate
      QString audioExt = QFileInfo(srcAudioFile).suffix();
      QString cutAudioFile = QFileInfo(QDir(TTCut::cutDirPath),
          QFileInfo(sourceFile).completeBaseName() + QString("_audio%1.").arg(i+1) + audioExt).absoluteFilePath();

      // Use FFmpeg wrapper for audio cutting (stream-copy)
      if (ffmpeg.cutAudioStream(srcAudioFile, cutAudioFile, keepList)) {
        cutAudioFiles.append(cutAudioFile);
        log->infoMsg(__FILE__, __LINE__, QString("Audio track %1 cut: %2").arg(i+1).arg(cutAudioFile));
      } else {
        log->errorMsg(__FILE__, __LINE__, QString("Audio track %1 cut failed").arg(i+1));
      }
    }

    // Mux video and audio into final MKV
    emit statusReport(StatusReportArgs::Step, tr("Muxing video and audio..."), 0);
    TTMkvMergeProvider mkvProvider;

    // Calculate frame duration in nanoseconds (e.g., "0:20000000ns" for 50fps)
    int frameDurationNs = (int)(1000000000.0 / frameRate);
    mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));

    success = mkvProvider.mux(finalOutput, tempVideoFile, cutAudioFiles, QStringList());

    if (success) {
      log->infoMsg(__FILE__, __LINE__, QString("Muxing complete: %1").arg(finalOutput));
      // Clean up temporary files
      QFile::remove(tempVideoFile);
      for (const QString& f : cutAudioFiles) {
        QFile::remove(f);
      }
    } else {
      log->errorMsg(__FILE__, __LINE__, QString("Muxing failed: %1").arg(mkvProvider.lastError()));
    }
  } else {
    // For container files: use smartCut (avcut-style processing)
    if (!ffmpeg.openFile(sourceFile)) {
      log->errorMsg(__FILE__, __LINE__, QString("Failed to open source file: %1").arg(ffmpeg.lastError()));
      emit statusReport(StatusReportArgs::Finished, tr("Cutting failed - could not open source"), 0);
      return;
    }

    log->infoMsg(__FILE__, __LINE__, QString("Starting smartCut: %1 segments to keep").arg(keepList.size()));
    success = ffmpeg.smartCut(finalOutput, keepList);
  }

  if (!success) {
    log->errorMsg(__FILE__, __LINE__, QString("smartCut failed: %1").arg(ffmpeg.lastError()));
    emit statusReport(StatusReportArgs::Finished, tr("Cutting failed"), 0);
    return;
  }

  log->infoMsg(__FILE__, __LINE__, "smartCut completed successfully");

  // Add chapters if enabled (MKV only)
  if (TTCut::mkvCreateChapters && TTCut::mkvChapterInterval > 0 &&
      finalOutput.endsWith(".mkv", Qt::CaseInsensitive)) {

    // Calculate total duration from cut list
    qint64 totalDurationMs = 0;
    for (int i = 0; i < cutList->count(); i++) {
      QTime cutLength = cutList->at(i).cutLengthTime();
      totalDurationMs += cutLength.hour() * 3600000 +
                         cutLength.minute() * 60000 +
                         cutLength.second() * 1000 +
                         cutLength.msec();
    }

    log->infoMsg(__FILE__, __LINE__, QString("Total cut duration: %1 ms").arg(totalDurationMs));

    if (totalDurationMs > 0) {
      QString chapterFile = TTMkvMergeProvider::generateChapterFile(
          totalDurationMs, TTCut::mkvChapterInterval, TTCut::cutDirPath);

      if (!chapterFile.isEmpty()) {
        // Use mkvmerge to add chapters to the existing MKV file
        QString tempOutput = finalOutput + ".tmp.mkv";

        emit statusReport(StatusReportArgs::Step, tr("Adding chapters..."), cutList->count());

        TTMkvMergeProvider mkvProvider;
        mkvProvider.setChapterFile(chapterFile);

        // Mux existing file with chapters
        bool muxSuccess = mkvProvider.mux(tempOutput, finalOutput, QStringList(), QStringList());

        if (muxSuccess) {
          // Replace original with version containing chapters
          QFile::remove(finalOutput);
          QFile::rename(tempOutput, finalOutput);
          log->infoMsg(__FILE__, __LINE__, "Chapters added successfully");
        } else {
          log->errorMsg(__FILE__, __LINE__, QString("Failed to add chapters: %1").arg(mkvProvider.lastError()));
          QFile::remove(tempOutput);
        }

        // Clean up chapter file
        QFile::remove(chapterFile);
      }
    }
  }

  emit statusReport(StatusReportArgs::Finished, tr("H.264/H.265 cutting complete"), 0);

  // Create a mux list item for the finished signal
  TTMuxListDataItem muxItem;
  muxItem.setVideoName(finalOutput);

  mpMuxList->appendItem(muxItem);
  mpMuxList->print();

  // Update cutVideoName with actual output filename for notification
  TTCut::cutVideoName = QFileInfo(finalOutput).fileName();

  qDebug() << "About to emit cutFinished() signal, cutVideoName =" << TTCut::cutVideoName;
  emit cutFinished();
  qDebug() << "cutFinished() signal emitted";
}

//! Audio video cut finished
void TTAVData::onCutFinished()
{
  disconnect(mpThreadTaskPool, SIGNAL(exit()), this, SLOT(onCutFinished()));

  mpMuxList->appendItem(*(cutVideoTask->muxListItem()));
  mpMuxList->print();

  int lastIdx = mpMuxList->count() - 1;
  TTMuxListDataItem& muxItem = mpMuxList->itemAt(lastIdx);

  qDebug() << "onCutFinished: outputContainer =" << TTCut::outputContainer;
  qDebug() << "onCutFinished: muxMode =" << TTCut::muxMode;
  qDebug() << "onCutFinished: video =" << muxItem.getVideoName();
  qDebug() << "onCutFinished: audio =" << muxItem.getAudioNames();
  qDebug() << "onCutFinished: subtitle =" << muxItem.getSubtitleNames();

  // Select muxer based on outputContainer setting
  // 0 = TS (Transport Stream, mplex)
  // 1 = MKV (mkvmerge)
  // 2 = MP4 (FFmpeg)
  // 3 = Elementary (no muxing)

  switch (TTCut::outputContainer) {
    case 1: // MKV - use mkvmerge
      {
        TTMkvMergeProvider* mkvProvider = new TTMkvMergeProvider();

        connect(mkvProvider, SIGNAL(progressChanged(int, const QString&)),
                this,        SLOT(onMuxProgress(int, const QString&)));

        // Build MKV output filename
        QFileInfo videoInfo(muxItem.getVideoName());
        QString mkvOutput = QFileInfo(QDir(TTCut::cutDirPath),
                                       videoInfo.completeBaseName() + ".mkv").absoluteFilePath();

        // Generate chapters if enabled
        QString chapterFile;
        if (TTCut::mkvCreateChapters && TTCut::mkvChapterInterval > 0) {
          // Calculate total duration from cut list
          qint64 totalDurationMs = 0;
          for (int i = 0; i < mpCutList->count(); i++) {
            QTime cutLength = mpCutList->at(i).cutLengthTime();
            totalDurationMs += cutLength.hour() * 3600000 +
                               cutLength.minute() * 60000 +
                               cutLength.second() * 1000 +
                               cutLength.msec();
          }

          qDebug() << "Total cut duration:" << totalDurationMs << "ms";

          if (totalDurationMs > 0) {
            chapterFile = TTMkvMergeProvider::generateChapterFile(
                totalDurationMs, TTCut::mkvChapterInterval, TTCut::cutDirPath);
            if (!chapterFile.isEmpty()) {
              mkvProvider->setChapterFile(chapterFile);
            }
          }
        }

        qDebug() << "Muxing to MKV:" << mkvOutput;

        bool muxSuccess = mkvProvider->mux(mkvOutput,
                             muxItem.getVideoName(),
                             muxItem.getAudioNames(),
                             muxItem.getSubtitleNames());

        if (muxSuccess) {
          qDebug() << "MKV muxing completed successfully";

          // Delete elementary streams if option is set
          if (TTCut::muxDeleteES) {
            deleteElementaryStreams(muxItem.getVideoName(),
                                    muxItem.getAudioNames(),
                                    muxItem.getSubtitleNames());
          }
        } else {
          qDebug() << "MKV muxing failed:" << mkvProvider->lastError();
        }

        // Clean up chapter file
        if (!chapterFile.isEmpty()) {
          QFile::remove(chapterFile);
        }

        delete mkvProvider;
      }
      break;

    case 2: // MP4 - use FFmpeg
      {
        // Build MP4 output filename
        QFileInfo videoInfo(muxItem.getVideoName());
        QString mp4Output = QFileInfo(QDir(TTCut::cutDirPath),
                                       videoInfo.completeBaseName() + ".mp4").absoluteFilePath();

        qDebug() << "Muxing to MP4:" << mp4Output;

        QStringList ffmpegArgs;
        ffmpegArgs << "-y";  // Overwrite

        // Input video
        ffmpegArgs << "-i" << muxItem.getVideoName();

        // Input audio files
        QStringList audioNames = muxItem.getAudioNames();
        for (const QString& audio : audioNames) {
          ffmpegArgs << "-i" << audio;
        }

        // Input subtitle files
        QStringList subtitleNames = muxItem.getSubtitleNames();
        for (const QString& sub : subtitleNames) {
          ffmpegArgs << "-i" << sub;
        }

        // Map all streams
        int inputIdx = 0;
        ffmpegArgs << "-map" << QString::number(inputIdx++);  // Video
        for (int i = 0; i < audioNames.count(); i++) {
          ffmpegArgs << "-map" << QString::number(inputIdx++);
        }
        for (int i = 0; i < subtitleNames.count(); i++) {
          ffmpegArgs << "-map" << QString::number(inputIdx++);
        }

        // Copy streams (no re-encoding)
        ffmpegArgs << "-c" << "copy";

        // Output
        ffmpegArgs << mp4Output;

        qDebug() << "FFmpeg command:" << ffmpegArgs.join(" ");

        QProcess ffmpegProc;
        ffmpegProc.start("/usr/bin/ffmpeg", ffmpegArgs);

        bool muxSuccess = false;
        if (ffmpegProc.waitForStarted(5000) && ffmpegProc.waitForFinished(600000)) {
          if (ffmpegProc.exitCode() == 0) {
            qDebug() << "MP4 muxing completed successfully";
            muxSuccess = true;
          } else {
            qDebug() << "MP4 muxing failed, exit code:" << ffmpegProc.exitCode();
            qDebug() << "stderr:" << QString::fromUtf8(ffmpegProc.readAllStandardError());
          }
        } else {
          qDebug() << "FFmpeg process error";
        }

        // Delete elementary streams if option is set and muxing succeeded
        if (muxSuccess && TTCut::muxDeleteES) {
          deleteElementaryStreams(muxItem.getVideoName(),
                                  audioNames,
                                  subtitleNames);
        }
      }
      break;

    case 3: // Elementary - no muxing
      qDebug() << "Elementary output selected, skipping muxing";
      break;

    case 0: // TS - use mplex (default, existing behavior)
    default:
      {
        TTMplexProvider* mplexProvider = new TTMplexProvider(mpMuxList);

        connect(mplexProvider, SIGNAL(statusReport(int, const QString&, quint64)),
                this,          SLOT(onStatusReport(int, const QString&, quint64)));

        if (TTCut::muxMode == 1)
          mplexProvider->writeMuxScript();
        else
          mplexProvider->mplexPart(lastIdx);

        delete mplexProvider;
      }
      break;
  }
}

void TTAVData::onCutAborted()
{
  disconnect(mpThreadTaskPool, SIGNAL(exit()),    this, SLOT(onCutFinished()));
  disconnect(mpThreadTaskPool, SIGNAL(aborted()), this, SLOT(onCutAborted()));
}

void TTAVData::onStatusReport(int state, const QString& msg, quint64 value)
{
  emit statusReport(0, state, msg, value);
  qApp->processEvents();
}

void TTAVData::onMplexStep(const QString& msg, quint64 value)
{
  emit statusReport(0, StatusReportArgs::Step, msg, value);
  qApp->processEvents();
}

void TTAVData::onMuxProgress(int percent, const QString& msg)
{
  emit statusReport(0, StatusReportArgs::Step, msg, percent);
  qApp->processEvents();
}

void TTAVData::deleteElementaryStreams(const QString& videoFilePath,
                                        const QStringList& audioFilePaths,
                                        const QStringList& subtitleFilePaths)
{
  // Delete video file
  QFile videoFile(videoFilePath);
  bool success = videoFile.remove();
  log->debugMsg(__FILE__, __LINE__, QString("Removing video stream %1 (%2)").
      arg(videoFilePath).arg(success ? "ok" : "failed"));

  // Delete audio files
  for (const QString& audioPath : audioFilePaths) {
    QFile audioFile(audioPath);
    success = audioFile.remove();
    log->debugMsg(__FILE__, __LINE__, QString("Removing audio stream %1 (%2)").
        arg(audioPath).arg(success ? "ok" : "failed"));
  }

  // Delete subtitle files
  for (const QString& subtitlePath : subtitleFilePaths) {
    QFile subtitleFile(subtitlePath);
    success = subtitleFile.remove();
    log->debugMsg(__FILE__, __LINE__, QString("Removing subtitle stream %1 (%2)").
        arg(subtitlePath).arg(success ? "ok" : "failed"));
  }
}
