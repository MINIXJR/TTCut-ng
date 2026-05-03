//*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
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

#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QTime>

#include "ttaudiolist.h"
#include "ttcutlist.h"
#include "ttavdata.h"
#include "ttmuxlistdata.h"
#include "ttcutprojectdata.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/istatusreporter.h"

#include "../extern/ttmplexprovider.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttavheader.h"
#include "../extern/ttffmpegwrapper.h"
#include "../extern/ttessmartcut.h"

#include "ttopenvideotask.h"
#include "ttopenaudiotask.h"
#include "ttopensubtitletask.h"
#include "ttcutpreviewtask.h"
#include "ttcutvideotask.h"
#include "ttcutsubtitletask.h"
#include "ttframesearchtask.h"

#include <QThreadPool>
#include <QList>
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QTime>

#include "../avstream/ttavtypes.h"

extern "C" {
#include <libavcodec/codec_id.h>
}

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
  mAvSyncOffsetMs   = 0;
  mCurrentFramePosition = 0;

	connect(mpThreadTaskPool, qOverload<>(&TTThreadTaskPool::init), this, &TTAVData::onThreadPoolInit);
  connect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onThreadPoolExit);
  connect(mpThreadTaskPool, &TTThreadTaskPool::statusReport,
	        this, qOverload<TTThreadTask*, int, const QString&, quint64>(&TTAVData::statusReport));

  connect(mpAVList,  &TTAVList::itemAppended,                   this, &TTAVData::avItemAppended);
	connect(mpAVList,  &TTAVList::itemRemoved,                    this, &TTAVData::avItemRemoved);
	connect(mpAVList,  &TTAVList::itemUpdated,                    this, &TTAVData::avItemUpdated);
	connect(mpAVList,  &TTAVList::itemsSwapped,                   this, &TTAVData::avItemsSwapped);

	connect(mpCutList, &TTCutList::itemAppended,                            this, &TTAVData::cutItemAppended);
	connect(mpCutList, qOverload<int>(&TTCutList::itemRemoved),             this, &TTAVData::cutItemRemoved);
	connect(mpCutList, &TTCutList::orderUpdated,                            this, &TTAVData::cutOrderUpdated);
	connect(mpCutList, &TTCutList::itemUpdated,                             this, &TTAVData::cutItemUpdated);

	connect(mpMarkerList, &TTMarkerList::itemAppended,                      this, &TTAVData::markerAppended);
	connect(mpMarkerList, qOverload<int>(&TTMarkerList::itemRemoved),       this, &TTAVData::markerRemoved);
	connect(mpMarkerList, &TTMarkerList::orderUpdated,                      this, qOverload<const TTMarkerItem&, int>(&TTAVData::markerUpdated));
	connect(mpMarkerList, &TTMarkerList::itemUpdated,                       this, qOverload<const TTMarkerItem&, const TTMarkerItem&>(&TTAVData::markerUpdated));
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
		throw TTInvalidOperationException("No current AV-Data set!");

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
		throw TTInvalidOperationException("No current AV-Data set!");

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

		connect(avItem->mpCutList,  &TTCutList::itemAppended,
            mpCutList,          &TTCutList::onAppendItem);
		connect(avItem->mpCutList,  qOverload<const TTCutItem&>(&TTCutList::itemRemoved),
            mpCutList,          &TTCutList::onRemoveItem);
		connect(avItem->mpCutList,  &TTCutList::itemUpdated,
            mpCutList,          &TTCutList::onUpdateItem);
		connect(mpCutList,          &TTCutList::orderUpdated,
            avItem->mpCutList,  &TTCutList::onUpdateOrder);

		connect(avItem->mpMarkerList, &TTMarkerList::itemAppended,
            mpMarkerList,         &TTMarkerList::onAppendItem);
		connect(avItem->mpMarkerList, qOverload<const TTMarkerItem&>(&TTMarkerList::itemRemoved),
            mpMarkerList,         &TTMarkerList::onRemoveItem);
		connect(avItem->mpMarkerList, &TTMarkerList::itemUpdated,
            mpMarkerList,         &TTMarkerList::onUpdateItem);
		connect(mpMarkerList,         &TTMarkerList::orderUpdated,
            avItem->mpMarkerList, &TTMarkerList::onUpdateOrder);

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
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
          this,             &TTAVData::onOpenAVStreamsAborted);

  TTAVItem* avItem = doOpenVideoStream(videoFilePath);

  // Auto-load audio files (ES workflow: separate audio files)
  QFileInfoList audioInfoList = getAudioNames(QFileInfo(videoFilePath));
  QListIterator<QFileInfo> audioInfo(audioInfoList);

  while (audioInfo.hasNext()) {
    doOpenAudioStream(avItem, audioInfo.next().absoluteFilePath());
  }

  // Auto-load subtitle files
  QFileInfoList subtitleInfoList = getSubtitleNames(QFileInfo(videoFilePath));
  QListIterator<QFileInfo> subtitleInfo(subtitleInfoList);

  while (subtitleInfo.hasNext()) {
    doOpenSubtitleStream(avItem, subtitleInfo.next().absoluteFilePath());
  }

  // Load metadata from .info file (if available)
  QString infoFile = TTESInfo::findInfoFile(videoFilePath);
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {

      // Load audio languages from .info and match to loaded audio files
      if (esInfo.audioTrackCount() > 0 && !audioInfoList.isEmpty()) {
        // Build basename→language map from .info
        QMap<QString, QString> infoLangMap;
        for (int i = 0; i < esInfo.audioTrackCount(); ++i) {
          TTAudioTrackInfo trackInfo = esInfo.audioTrack(i);
          if (!trackInfo.file.isEmpty() && !trackInfo.language.isEmpty()) {
            infoLangMap[QFileInfo(trackInfo.file).fileName()] = trackInfo.language;
          }
        }

        // Match loaded audio files by basename
        int audioOrder = 0;
        for (const QFileInfo& af : audioInfoList) {
          QString lang = infoLangMap.value(af.fileName());
          if (!lang.isEmpty()) {
            setPendingAudioLanguage(avItem, audioOrder, lang);
            qDebug() << "  Audio language from .info:" << af.fileName() << "=" << lang;
          }
          ++audioOrder;
        }
      }

      // Load VDR markers
      if (esInfo.hasMarkers()) {
        qDebug() << "Found VDR markers in info file:" << esInfo.markerCount();

        QList<QPair<int, int>> cutPairs;
        QList<TTMarkerInfo> markers = esInfo.markers();

        for (int i = 0; i < markers.size() - 1; i += 2) {
          int cutIn = markers[i].frame;
          int cutOut = markers[i + 1].frame;

          if (cutIn > 0 && cutOut > cutIn) {
            cutPairs.append(qMakePair(cutIn, cutOut));
            qDebug() << "  VDR cut pair:" << cutIn << "-" << cutOut;
          }
        }

        if (!cutPairs.isEmpty()) {
          mpPendingVdrMarkers[avItem] = cutPairs;
        }
      }

      // Store extra frame indices for audio time correction
      mExtraFrameIndices = esInfo.esExtraFrames();
      if (!mExtraFrameIndices.isEmpty()) {
        qDebug() << "Loaded" << mExtraFrameIndices.size() << "extra frame indices for audio correction";
      }

      // Show clustering dialog if extra frames were detected (not on project reload)
      if (!mExtraFrameIndices.isEmpty() && avItem) {
        TTVideoStream* vs = avItem->videoStream();
        double frameRate = vs ? vs->frameRate() : 25.0;
        int gapFrames = TTCut::extraFrameClusterGapSec * frameRate;
        int offsetFrames = TTCut::extraFrameClusterOffsetSec * frameRate;

        // Cluster extra frames by gap
        QList<TTStreamPoint> clusters;
        int clusterStart = mExtraFrameIndices.first();
        int clusterEnd = clusterStart;
        int clusterCount = 1;

        auto emitCluster = [&]() {
            int pos = qMax(0, clusterStart - offsetFrames);
            double durSec = (clusterEnd - clusterStart + 1) / frameRate;
            QString desc = QString("Defekt: %1\u2013%2 (%3 Frames, %4s)")
                .arg(clusterStart).arg(clusterEnd)
                .arg(clusterCount).arg(durSec, 0, 'f', 1);
            clusters.append(TTStreamPoint(pos, StreamPointType::Error, desc));
        };

        for (int i = 1; i < mExtraFrameIndices.size(); ++i) {
            if (mExtraFrameIndices[i] - clusterEnd <= gapFrames) {
                clusterEnd = mExtraFrameIndices[i];
                clusterCount++;
            } else {
                emitCluster();
                clusterStart = mExtraFrameIndices[i];
                clusterEnd = clusterStart;
                clusterCount = 1;
            }
        }
        emitCluster();

        // Show dialog with group listing
        QString msg = tr("%1 defective frames in %2 groups detected.\n")
            .arg(mExtraFrameIndices.size())
            .arg(clusters.size());

        int showCount = qMin(clusters.size(), 10);
        for (int i = 0; i < showCount; ++i) {
            msg += QString("\n  %1").arg(clusters[i].description());
        }
        if (clusters.size() > 10) {
            msg += QString("\n  ... %1 %2")
                .arg(clusters.size() - 10)
                .arg(tr("more groups"));
        }

        QMessageBox msgBox(QMessageBox::Warning,
                           tr("Defective Frames Detected"),
                           msg, QMessageBox::NoButton, TTCut::mainWindow);
        QPushButton* importBtn = msgBox.addButton(
            tr("Import as Stream Points"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == importBtn) {
            emit vdrMarkersLoaded(clusters);
        }
      }

      // Show warning if decode errors were detected (legacy .info)
      if (esInfo.hasWarnings()) {
        QString warnMsg = tr("%1 decode errors detected in %2 region(s) during demux.\n\n"
                             "This MPEG-2 stream has defective GOPs that may cause A/V sync issues.\n"
                             "Recommendation: Use ProjectX to demux this file instead.")
                          .arg(esInfo.decodeErrors())
                          .arg(esInfo.decodeErrorRegions().size());

        // Add region details (max 10)
        QList<TTDecodeErrorRegion> regions = esInfo.decodeErrorRegions();
        if (!regions.isEmpty()) {
          warnMsg += "\n\n" + tr("Affected regions:");
          int showCount = qMin(regions.size(), 10);
          for (int i = 0; i < showCount; ++i) {
            warnMsg += QString("\n  ~Frame %1 (%2): %3 %4")
                       .arg(regions[i].frame)
                       .arg(regions[i].time)
                       .arg(regions[i].errorCount)
                       .arg(tr("errors"));
          }
          if (regions.size() > 10) {
            warnMsg += QString("\n  ... %1 %2")
                       .arg(regions.size() - 10)
                       .arg(tr("more regions"));
          }
        }

        QMessageBox msgBox(QMessageBox::Warning,
                           tr("Stream Integrity Warning"),
                           warnMsg, QMessageBox::NoButton, TTCut::mainWindow);
        QPushButton* importBtn = msgBox.addButton(tr("Import as Stream Points"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == importBtn && !regions.isEmpty()) {
          QList<TTStreamPoint> errorPoints;
          for (const auto& region : regions) {
            errorPoints.append(TTStreamPoint(region.frame, StreamPointType::Error,
              QString("Decode Error (%1 errors)").arg(region.errorCount)));
          }
          emit vdrMarkersLoaded(errorPoints);
        }
      }
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

  connect(openVideoTask, qOverload<TTAVItem*, TTVideoStream*, int, const QString&>(&TTOpenVideoTask::finished),
          this,          &TTAVData::onOpenVideoFinished,
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

  connect(openAudioTask, qOverload<TTAVItem*, TTAudioStream*, int>(&TTOpenAudioTask::finished),
          this,          &TTAVData::onOpenAudioFinished,
          Qt::QueuedConnection);

  mpThreadTaskPool->start(openAudioTask);
}

/*!
 * doOpenSubtitleStream
 */
void TTAVData::doOpenSubtitleStream(TTAVItem* avItem, const QString& filePath, int order)
{
  TTOpenSubtitleTask* openSubtitleTask = new TTOpenSubtitleTask(avItem, filePath, order);

  connect(openSubtitleTask, qOverload<TTAVItem*, TTSubtitleStream*, int>(&TTOpenSubtitleTask::finished),
          this,             &TTAVData::onOpenSubtitleFinished,
          Qt::QueuedConnection);

  mpThreadTaskPool->start(openSubtitleTask);
}

/*!
 * onOpenVideoFinished
 */
void TTAVData::onOpenVideoFinished(TTAVItem* avItem, TTVideoStream* vStream, int, const QString& demuxedAudio)
{
  if (avItem == nullptr) return;

  avItem->setVideoStream(vStream);

  // Load extra frame indices from .info (for audio time correction)
  // This runs for ALL paths: direct open, project load, etc.
  if (vStream && mExtraFrameIndices.isEmpty()) {
    QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
    if (!infoFile.isEmpty()) {
      TTESInfo esInfo(infoFile);
      if (esInfo.isLoaded() && !esInfo.esExtraFrames().isEmpty()) {
        mExtraFrameIndices = esInfo.esExtraFrames();
        qDebug() << "Loaded" << mExtraFrameIndices.size() << "extra frame indices for audio correction";
      }
    }
  }

  if (mpAVList == nullptr) return;

  mpAVList->append(avItem);

  // Add pending VDR markers as cut entries AND stream points (after video stream is set)
  if (mpPendingVdrMarkers.contains(avItem)) {
    QList<QPair<int, int>> cutPairs = mpPendingVdrMarkers.take(avItem);
    int frameCount = vStream ? vStream->frameCount() : 0;

    // PAFF correction: markad frame numbers may be at field rate (50fps)
    // while the video now has frame-rate indexed frames (25fps).
    // Detect: if any marker exceeds frameCount, markers are at field rate.
    bool markersAtFieldRate = false;
    if (frameCount > 0) {
      for (const auto& pair : cutPairs) {
        if (pair.first >= frameCount || pair.second >= frameCount) {
          markersAtFieldRate = true;
          break;
        }
      }
    }
    if (markersAtFieldRate) {
      qDebug() << "  VDR markers exceed frame count — halving for PAFF field-rate correction";
    }

    qDebug() << "Adding" << cutPairs.size() << "VDR cut entries, video has" << frameCount << "frames";

    QList<TTStreamPoint> vdrPoints;

    for (const auto& pair : cutPairs) {
      int cutIn = markersAtFieldRate ? pair.first / 2 : pair.first;
      int cutOut = markersAtFieldRate ? pair.second / 2 : pair.second;

      // Validate frame numbers against video length
      if (frameCount > 0 && cutOut >= frameCount) {
        cutOut = frameCount - 1;
      }

      if (cutIn >= 0 && cutOut > cutIn) {
        qDebug() << "  Adding VDR cut:" << cutIn << "-" << cutOut;
        avItem->appendCutEntry(cutIn, cutOut);

        // Also add individual markers for the Marker tab
        avItem->appendMarker(cutIn);
        avItem->appendMarker(cutOut);

        // Collect as VDR stream points for Landezonen widget
        vdrPoints.append(TTStreamPoint(cutIn, StreamPointType::VDRImportMarker,
          QString("VDR Mark (Cut-In)")));
        vdrPoints.append(TTStreamPoint(cutOut, StreamPointType::VDRImportMarker,
          QString("VDR Mark (Cut-Out)")));
      }
    }

    if (!vdrPoints.isEmpty()) {
      emit vdrMarkersLoaded(vdrPoints);
    }
  }

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
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
             this,             &TTAVData::onOpenAVStreamsAborted);

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

  // Apply saved language from project file if available
  auto key = qMakePair(avItem, order);
  if (mPendingAudioLanguages.contains(key)) {
    QString lang = mPendingAudioLanguages.take(key);
    int idx = avItem->audioCount() - 1;
    if (idx >= 0) {
      avItem->onAudioLanguageChanged(idx, lang);
    }
  }

  // Apply saved delay from project file if available
  if (mPendingAudioDelays.contains(key)) {
    int delayMs = mPendingAudioDelays.take(key);
    int idx = avItem->audioCount() - 1;
    if (idx >= 0) {
      avItem->onAudioDelayChanged(idx, delayMs);
    }
  }
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

  // Apply saved language from project file if available
  auto key = qMakePair(avItem, order);
  if (mPendingSubtitleLanguages.contains(key)) {
    QString lang = mPendingSubtitleLanguages.take(key);
    int idx = avItem->subtitleCount() - 1;
    if (idx >= 0) {
      avItem->onSubtitleLanguageChanged(idx, lang);
    }
  }
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
	if (index < 0 || index >= mpAVList->count()) return;

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
	if (avItem == 0) return;

	TTFrameSearchTask* frameSearch = new TTFrameSearchTask(
      avItem->videoStream(),          startIndex,
      mpCurrentAVItem->videoStream(), mCurrentFramePosition);

	connect(frameSearch, qOverload<int>(&TTFrameSearchTask::finished), this, &TTAVData::foundEqualFrame, Qt::QueuedConnection);

	mpThreadTaskPool->start(frameSearch);
}

//! Track current frame position from Current Frame widget
void TTAVData::onCurrentFramePositionChanged(int position)
{
	mCurrentFramePosition = position;
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
  // Sort audio lists by priority (AC3 first, locale language first)
  for (int i = 0; i < mpAVList->count(); i++) {
    TTAudioList* audioList = mpAVList->at(i)->audioDataList();
    if (audioList->count() > 1) {
      audioList->sortByOrder();
    }
  }

  emit statusReport(0, StatusReportArgs::Exit, tr("exiting thread pool"), 0);
  emit avDataReloaded();
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
			<< vFileInfo.completeBaseName() + "*" + ".ac3"
			<< vFileInfo.completeBaseName() + "*" + ".aac";

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
void TTAVData::writeProjectFile(const QFileInfo& fInfo,
                                 const QList<TTStreamPoint>& streamPoints,
                                 const TTLogoProjectData& logoData)
{
	TTCutProjectData* prj = new TTCutProjectData(fInfo);

	for (int i = 0; i < mpAVList->count(); i++) {
		prj->serializeAVDataItem(mpAVList->at(i));
	}

	if (!streamPoints.isEmpty()) {
		prj->serializeStreamPoints(streamPoints);
	}

	prj->serializeLogoData(logoData);

	prj->writeXml();

	delete prj;
}

/**
 * Read the TTCut xml project file
 */
void TTAVData::readProjectFile(const QFileInfo& fInfo)
{
  connect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onReadProjectFileFinished);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onReadProjectFileAborted);

  mpProjectData = new TTCutProjectData(fInfo);

  try
  {
	  mpProjectData->readXml();
	  mpProjectData->deserializeAVDataItem(this);
  }
  catch (const TTException& ex)
  {
		log->errorMsg(__FILE__, __LINE__, ex.getMessage());
    onReadProjectFileAborted();
  }
}

/**
 * Reading TTCut project file finished
 */
void TTAVData::onReadProjectFileFinished()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onReadProjectFileAborted);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onReadProjectFileFinished);

  emit avDataReloaded();

  if (avCount() > 0)
    emit currentAVItemChanged(avItemAt(0));

  // Load stream points from project file
  QList<TTStreamPoint> loadedPoints = mpProjectData->deserializeStreamPoints();
  if (!loadedPoints.isEmpty()) {
    emit streamPointsLoaded(loadedPoints);
  }

  // Load logo detection data from project file
  TTLogoProjectData logoData = mpProjectData->deserializeLogoData();
  if (logoData.valid) {
    emit logoDataLoaded(logoData);
  }

  // Restore global settings from project file
  mpProjectData->deserializeSettings();

  // Map generic encoder values to codec-specific fields based on video type
  if (avCount() > 0 && avItemAt(0)->videoStream()) {
    TTAVTypes::AVStreamType st = avItemAt(0)->videoStream()->streamType();
    if (st == TTAVTypes::mpeg2_demuxed_video) {
      TTCut::mpeg2Preset  = TTCut::encoderPreset;
      TTCut::mpeg2Crf     = TTCut::encoderCrf;
      TTCut::mpeg2Profile = TTCut::encoderProfile;
    } else if (st == TTAVTypes::h264_video) {
      TTCut::h264Preset  = TTCut::encoderPreset;
      TTCut::h264Crf     = TTCut::encoderCrf;
      TTCut::h264Profile = TTCut::encoderProfile;
    } else if (st == TTAVTypes::h265_video) {
      TTCut::h265Preset  = TTCut::encoderPreset;
      TTCut::h265Crf     = TTCut::encoderCrf;
      TTCut::h265Profile = TTCut::encoderProfile;
    }
  }

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
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onReadProjectFileFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onReadProjectFileAborted);

  emit currentAVItemChanged(0);

  if (mpProjectData != 0) {
    delete mpProjectData;
    mpProjectData = 0;
  }
}

// /////////////////////////////////////////////////////////////////////////////
// Pending language overrides (from project file, applied after async stream open)

void TTAVData::setPendingAudioLanguage(TTAVItem* avItem, int order, const QString& lang)
{
  mPendingAudioLanguages.insert(qMakePair(avItem, order), lang);
}

void TTAVData::setPendingSubtitleLanguage(TTAVItem* avItem, int order, const QString& lang)
{
  mPendingSubtitleLanguages.insert(qMakePair(avItem, order), lang);
}

void TTAVData::setPendingAudioDelay(TTAVItem* avItem, int order, int delayMs)
{
  mPendingAudioDelays.insert(qMakePair(avItem, order), delayMs);
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

  connect(cutPreviewTask,   qOverload<TTCutList*>(&TTCutPreviewTask::finished),
          this,             &TTAVData::onCutPreviewFinished);
  connect(cutPreviewTask,   &TTCutPreviewTask::audioDriftCalculated,
          this,             &TTAVData::onCutPreviewAudioDrift);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
					this,             &TTAVData::onCutPreviewAborted);

  mpThreadTaskPool->init(cutList->count()*2);
  mpThreadTaskPool->start(cutPreviewTask);
}

//! Finished creating cut preview clips
void TTAVData::onCutPreviewFinished(TTCutList* cutList)
{
	emit cutPreviewFinished(cutList);
}

//! Relay audio drift values from preview task to main window
void TTAVData::onCutPreviewAudioDrift(const QList<float>& driftsMs)
{
    emit cutAudioDriftCalculated(driftsMs);
}

//! Cut preview aborted by user
void TTAVData::onCutPreviewAborted()
{
  disconnect(cutPreviewTask,   qOverload<TTCutList*>(&TTCutPreviewTask::finished),
             this,             &TTAVData::onCutPreviewFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
			   		 this,             &TTAVData::onCutPreviewAborted);

	if (cutPreviewTask != 0) {
    delete cutPreviewTask;
    cutPreviewTask = 0;
  }
}

/*!
 * createCutFileName — builds "<cutBase>_NNN.<ext>" inside TTCut::cutDirPath.
 * Used for both per-track audio and subtitle output filenames.
 */
QString TTAVData::createCutFileName(QString cutBaseFileName, QString sourceFileName, int index)
{
  QString cutFileName = QString("%1_%2.%3").
    arg(QFileInfo(cutBaseFileName).completeBaseName()).
    arg(index, 3, 10, QLatin1Char('0')).
    arg(QFileInfo(sourceFileName).suffix());

  return QFileInfo(QDir(TTCut::cutDirPath), cutFileName).absoluteFilePath();
}

// /////////////////////////////////////////////////////////////////////////////
// Audio and video cut
//
//! Do the audio and video cut for given cut-list
void TTAVData::onDoCut(QString tgtFileName, TTCutList* cutList, bool audioOnly)
{
  if (cutList == 0) cutList = mpCutList;

  // Reset last-cut metadata; non-audio-only path leaves it cleared.
  mLastCutWasAudioOnly = false;
  mLastCutOutputSummary.clear();

  if (audioOnly) {
    // Burst warning still useful, dispatch the rest to the audio-only pipeline.
    if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
      QStringList burstWarnings;
      for (int i = 0; i < cutList->count(); i++) {
        TTCutItem item = cutList->at(i);
        CutBurstInfo bout = detectCutOutBurst(item);
        if (bout.present) burstWarnings << tr("Schnitt %1: Audio-Burst am Ende (%2 dB)")
                                          .arg(i + 1).arg(bout.burstDb, 0, 'f', 1);
        CutBurstInfo bin = detectCutInBurst(item);
        if (bin.present) burstWarnings << tr("Schnitt %1: Audio-Burst am Anfang (%2 dB)")
                                          .arg(i + 1).arg(bin.burstDb, 0, 'f', 1);
      }
      if (!burstWarnings.isEmpty()) {
        QString msg = tr("The following cuts have detected audio bursts:\n\n")
                    + burstWarnings.join("\n")
                    + tr("\n\nUse preview to check if shift is needed.");
        int ret = QMessageBox::warning(TTCut::mainWindow, tr("Audio Burst Warning"),
                                       msg, tr("Cut anyway"), tr("Cancel"));
        if (ret == 1) {
          emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
          return;
        }
      }
    }
    doAudioOnlyCut(tgtFileName, cutList);
    return;
  }

  // Detect stream type from first cut item
  TTVideoStream* firstStream = cutList->at(0).avDataItem()->videoStream();
  TTAVTypes::AVStreamType streamType = firstStream->streamType();
  bool isH264H265 = (streamType == TTAVTypes::h264_video || streamType == TTAVTypes::h265_video);

  // Check for unresolved audio bursts
  if (cutList->count() > 0 && cutList->at(0).avDataItem()->audioCount() > 0) {
    QStringList burstWarnings;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      CutBurstInfo bout = detectCutOutBurst(item);
      if (bout.present) {
        burstWarnings << tr("Schnitt %1: Audio-Burst am Ende (%2 dB)")
                         .arg(i + 1).arg(bout.burstDb, 0, 'f', 1);
      }
      CutBurstInfo bin = detectCutInBurst(item);
      if (bin.present) {
        burstWarnings << tr("Schnitt %1: Audio-Burst am Anfang (%2 dB)")
                         .arg(i + 1).arg(bin.burstDb, 0, 'f', 1);
      }
    }

    if (!burstWarnings.isEmpty()) {
      QString msg = tr("The following cuts have detected audio bursts:\n\n")
                  + burstWarnings.join("\n")
                  + tr("\n\nUse preview to check if shift is needed.");

      int ret = QMessageBox::warning(TTCut::mainWindow, tr("Audio Burst Warning"),
                    msg, tr("Cut anyway"), tr("Cancel"));
      if (ret == 1) {
        emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
        return;
      }
    }
  }

  if (isH264H265) {
    // For H.264/H.265: use ffmpeg directly since native cutting is not implemented
    doH264Cut(tgtFileName, cutList);
    return;
  }

  // For MPEG-2: use traditional cutting workflow
  // Read A/V sync offset from .info file if available
  mAvSyncOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(firstStream->filePath());
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        mAvSyncOffsetMs = esInfo.avOffsetMs();
        log->infoMsg(__FILE__, __LINE__, QString("A/V sync offset from .info: %1 ms").arg(mAvSyncOffsetMs));
      }
      // Ensure extra frame indices are loaded for audio time correction
      if (mExtraFrameIndices.isEmpty() && !esInfo.esExtraFrames().isEmpty()) {
        mExtraFrameIndices = esInfo.esExtraFrames();
        qDebug() << "Loaded" << mExtraFrameIndices.size() << "extra frame indices for audio correction (cut path)";
      }
    }
  }

  cutVideoTask = new TTCutVideoTask(this);
  cutVideoTask->init(tgtFileName, cutList);

  connect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onCutFinished);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // Init pool for video task only — audio is cut synchronously via FFmpegWrapper
  mpThreadTaskPool->init(cutList->count());
  mpThreadTaskPool->start(cutVideoTask);

  // all video must have the same count of audio streams!
  for (int i = 0; i < cutList->at(0).avDataItem()->audioCount(); i++) {
    TTAudioStream* audioStream = cutList->at(0).avDataItem()->audioStreamAt(i);

    QString tgtAudioFilePath = createCutFileName(tgtFileName, audioStream->fileName(), i+1);

    log->debugMsg(__FILE__, __LINE__, QString("current audio stream %1").arg(audioStream->fileName()));
    log->debugMsg(__FILE__, __LINE__, QString("audio cut file %1").arg(tgtAudioFilePath));

    // audio file exists
    if (QFileInfo(tgtAudioFilePath).exists()) {
      // TODO: Warning about deleting file
      log->warningMsg(__FILE__, __LINE__, tr("deleting existing audio cut file: %1").arg(tgtAudioFilePath));
      QFile tempFile(tgtAudioFilePath);
      tempFile.remove();
      tempFile.close();
    }

    // Use FFmpegWrapper for audio cutting (same as H.264/H.265 path)
    // This enables AC3 acmod normalization at cut boundaries
    TTVideoStream* vStream = cutList->at(0).avDataItem()->videoStream();
    double frameRate = vStream->frameRate();
    int delayMs = cutList->at(0).avDataItem()->audioListItemAt(i).getDelayMs();

    // Build video-domain keep list (extra-frame-corrected, no delay yet)
    QList<QPair<double, double>> videoKeepList;
    for (int c = 0; c < cutList->count(); c++) {
      TTCutItem ci = cutList->at(c);
      int extraIn  = countExtraFramesBefore(ci.cutInIndex());
      int extraOut = countExtraFramesBefore(ci.cutOutIndex() + 1);
      double cutInTime  = (ci.cutInIndex()      - extraIn)  / frameRate;
      double cutOutTime = (ci.cutOutIndex() + 1 - extraOut) / frameRate;
      videoKeepList.append(qMakePair(cutInTime, cutOutTime));
    }
    AudioCutPlan plan = planAudioCut(audioStream, videoKeepList, delayMs);
    QList<QPair<double, double>> audioKeepList = plan.keepList;
    if (delayMs != 0) {
      log->infoMsg(__FILE__, __LINE__, QString("Audio track %1: applying delay %2 ms").arg(i+1).arg(delayMs));
    }

    // Build per-segment target acmod list for AC3 normalization
    QList<int> targetAcmods;
    QString audioExt = QFileInfo(audioStream->filePath()).suffix().toLower();
    if (TTCut::normalizeAcmod && audioExt == "ac3") {
      for (int s = 0; s < audioKeepList.size(); s++) {
        TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
            audioStream->filePath(), audioKeepList[s].first, audioKeepList[s].second);
        targetAcmods.append(aInfo.mainAcmod);
      }
    }

    TTFFmpegWrapper ffmpegAudio;
    TTAudioItem audioItem = cutList->at(0).avDataItem()->audioListItemAt(i);
    if (ffmpegAudio.cutAudioStream(audioStream->filePath(), tgtAudioFilePath,
                                     audioKeepList, TTCut::normalizeAcmod, targetAcmods)) {
      // Register audio file with mux list item so onCutFinished can mux it
      cutVideoTask->muxListItem()->appendAudioFile(tgtAudioFilePath, audioItem.getLanguage());
    } else {
      log->errorMsg(__FILE__, __LINE__, QString("Audio cut failed for track %1").arg(i+1));
    }
  }

  // cut subtitle streams
  for (int i = 0; i < cutList->at(0).avDataItem()->subtitleCount(); i++) {
    TTSubtitleStream* subtitleStream = cutList->at(0).avDataItem()->subtitleStreamAt(i);

    QString tgtSubtitleFilePath = createCutFileName(tgtFileName, subtitleStream->fileName(), i+1);

    log->debugMsg(__FILE__, __LINE__, QString("current subtitle stream %1").arg(subtitleStream->fileName()));
    log->debugMsg(__FILE__, __LINE__, QString("subtitle cut file %1").arg(tgtSubtitleFilePath));

    // subtitle file exists
    if (QFileInfo(tgtSubtitleFilePath).exists()) {
      log->warningMsg(__FILE__, __LINE__, tr("deleting existing subtitle cut file: %1").arg(tgtSubtitleFilePath));
      QFile tempFile(tgtSubtitleFilePath);
      tempFile.remove();
      tempFile.close();
    }

    cutSubtitleTask = new TTCutSubtitleTask();
    TTSubtitleItem subtitleItem = cutList->at(0).avDataItem()->subtitleListItemAt(i);
    cutSubtitleTask->init(tgtSubtitleFilePath, cutList, i, cutVideoTask->muxListItem(), subtitleItem.getLanguage());

    mpThreadTaskPool->start(cutSubtitleTask);
  }
}

//! Do H.264/H.265 cut using TTESSmartCut (frame-accurate)
void TTAVData::doH264Cut(QString tgtFileName, TTCutList* cutList)
{
  log->infoMsg(__FILE__, __LINE__, "Using TTESSmartCut for frame-accurate cutting");

  // Get source file and frame rate from first cut item
  TTAVItem* avItem = cutList->at(0).avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  QString sourceFile = vStream->filePath();
  double frameRate = vStream->frameRate();
  QString suffix = QFileInfo(sourceFile).suffix().toLower();

  // Get A/V offset from .info file (frame rate comes from vStream, already PAFF-corrected)
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(sourceFile);
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (frameRate <= 0 && esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
        log->infoMsg(__FILE__, __LINE__, QString("ES frame rate from .info (fallback): %1 fps").arg(frameRate));
      }
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        avOffsetMs = esInfo.avOffsetMs();
        log->infoMsg(__FILE__, __LINE__, QString("A/V sync offset from .info: %1 ms").arg(avOffsetMs));
      }
    }
  }

  // Get audio file (ES workflow: separate audio files)
  QString audioFile;
  if (avItem->audioCount() > 0) {
    audioFile = avItem->audioStreamAt(0)->filePath();
    log->infoMsg(__FILE__, __LINE__, QString("Audio file: %1").arg(audioFile));
  }

  emit statusReport(0, StatusReportArgs::Init, tr("Initializing H.264/H.265 cut..."), 0);
  qApp->processEvents();
  emit statusReport(0, StatusReportArgs::Start, tr("Cutting H.264/H.265 video..."), cutList->count());
  qApp->processEvents();

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

    int extraIn  = countExtraFramesBefore(startFrame);
    int extraOut = countExtraFramesBefore(endFrame + 1);
    double cutInTime  = (startFrame - extraIn) / frameRate;
    double cutOutTime = (endFrame + 1 - extraOut) / frameRate;  // +1 because endFrame is inclusive

    log->infoMsg(__FILE__, __LINE__, QString("Cut %1: frames %2-%3, time %4-%5")
        .arg(i+1).arg(startFrame).arg(endFrame)
        .arg(cutInTime, 0, 'f', 3).arg(cutOutTime, 0, 'f', 3));

    keepList.append(qMakePair(cutInTime, cutOutTime));
  }

  TTFFmpegWrapper ffmpeg;

  // Use TTESSmartCut for frame-accurate cutting
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
      emit statusReport(0, StatusReportArgs::Step, msg, percent);
      qApp->processEvents();
    });

    if (!smartCut.initialize(sourceFile, frameRate)) {
      log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut init failed: %1").arg(smartCut.lastError()));
      emit statusReport(0, StatusReportArgs::Exit, tr("Cutting failed - could not initialize"), 0);
      return;
    }

    // SPS boundary check (H.264/H.265 only)
    for (int i = 0; i < cutFrames.size(); i++) {
      // Check CutOut (skip last segment)
      if (i < cutFrames.size() - 1) {
        if (smartCut.hasSPSChangeAtBoundary(cutFrames[i].second, true)) {
          log->warningMsg(__FILE__, __LINE__,
              QString("SPS change at CutOut segment %1 (frame %2) - possible aspect ratio change")
              .arg(i + 1).arg(cutFrames[i].second));
        }
      }
      // Check CutIn (skip first segment)
      if (i > 0) {
        if (smartCut.hasSPSChangeAtBoundary(cutFrames[i].first, false)) {
          log->warningMsg(__FILE__, __LINE__,
              QString("SPS change at CutIn segment %1 (frame %2) - possible aspect ratio change")
              .arg(i + 1).arg(cutFrames[i].first));
        }
      }
    }

    // Create temporary video output
    QString tempVideoFile = QFileInfo(QDir(TTCut::cutDirPath),
        QFileInfo(sourceFile).completeBaseName() + "_cut." + QFileInfo(sourceFile).suffix()).absoluteFilePath();

    // Perform frame-accurate video cut
    emit statusReport(0, StatusReportArgs::Step, tr("Cutting video (Smart Cut)..."), 0);
    qApp->processEvents();
    if (!smartCut.smartCutFrames(tempVideoFile, cutFrames)) {
      log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut failed: %1").arg(smartCut.lastError()));
      emit statusReport(0, StatusReportArgs::Exit, tr("Cutting failed"), 0);
      return;
    }

    log->infoMsg(__FILE__, __LINE__, QString("Smart Cut complete: %1 frames re-encoded, %2 frames stream-copied")
        .arg(smartCut.framesReencoded()).arg(smartCut.framesStreamCopied()));

    // Adjust audio keepList to match actual video output ranges.
    // B-frame reorder delay can shift the display-order CutIn forward, causing
    // the video Smart Cut to output fewer frames than the cut list specifies.
    // Without adjustment, audio would be cut for the original (wider) range,
    // resulting in cumulative A/V drift across segments.
    QList<QPair<int, int>> actualRanges = smartCut.actualOutputFrameRanges();
    if (actualRanges.size() == keepList.size()) {
      for (int i = 0; i < keepList.size(); i++) {
        double origStart = keepList[i].first;
        double newStart = actualRanges[i].first / frameRate;
        if (qAbs(newStart - origStart) > 0.001) {
          log->infoMsg(__FILE__, __LINE__, QString("Audio segment %1: adjusting start %2 -> %3 (B-frame reorder shift: %4 frames)")
              .arg(i+1).arg(origStart, 0, 'f', 3).arg(newStart, 0, 'f', 3)
              .arg(actualRanges[i].first - cutFrames[i].first));
          keepList[i].first = newStart;
        }
      }
    }

    // Cut audio tracks
    QStringList cutAudioFiles;
    for (int i = 0; i < avItem->audioCount(); i++) {
      TTAudioStream* audioStream = avItem->audioStreamAt(i);
      QString srcAudioFile = audioStream->filePath();

      emit statusReport(0, StatusReportArgs::Step, tr("Cutting audio track %1...").arg(i+1), 0);
      qApp->processEvents();

      // Calculate audio cut times from video frame rate
      QString audioExt = QFileInfo(srcAudioFile).suffix();
      QString cutAudioFile = QFileInfo(QDir(TTCut::cutDirPath),
          QFileInfo(sourceFile).completeBaseName() + QString("_audio%1.").arg(i+1) + audioExt).absoluteFilePath();

      // Build per-track audioKeepList from the (B-frame-adjusted) video keepList,
      // applying delay and snapping to audio-frame boundaries with feed-forward
      // drift compensation.
      int audioDelayMs = avItem->audioListItemAt(i).getDelayMs();
      AudioCutPlan plan = planAudioCut(audioStream, keepList, audioDelayMs);
      QList<QPair<double, double>> audioKeepList = plan.keepList;
      if (audioDelayMs != 0) {
        log->infoMsg(__FILE__, __LINE__, QString("Audio track %1: applying delay %2 ms to keepList").arg(i+1).arg(audioDelayMs));
      }

      // Build per-segment target acmod list for AC3 normalization
      QList<int> targetAcmods;
      if (TTCut::normalizeAcmod && audioExt.toLower() == "ac3") {
        for (int s = 0; s < audioKeepList.size(); s++) {
          TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
              srcAudioFile, audioKeepList[s].first, audioKeepList[s].second);
          targetAcmods.append(aInfo.mainAcmod);
        }
      }

      // Use FFmpeg wrapper for audio cutting (stream-copy + optional acmod normalization)
      if (ffmpeg.cutAudioStream(srcAudioFile, cutAudioFile, audioKeepList,
                                 TTCut::normalizeAcmod, targetAcmods)) {
        cutAudioFiles.append(cutAudioFile);
        log->infoMsg(__FILE__, __LINE__, QString("Audio track %1 cut: %2").arg(i+1).arg(cutAudioFile));
      } else {
        log->errorMsg(__FILE__, __LINE__, QString("Audio track %1 cut failed").arg(i+1));
      }
    }

    // Collect audio languages from data model
    QStringList cutAudioLanguages;
    for (int i = 0; i < avItem->audioCount(); i++) {
      cutAudioLanguages.append(avItem->audioListItemAt(i).getLanguage());
    }

    // Mux video and audio into final MKV
    log->infoMsg(__FILE__, __LINE__, QString("tempVideoFile: %1 (%2 bytes)")
        .arg(tempVideoFile).arg(QFileInfo(tempVideoFile).size()));
    for (int i = 0; i < cutAudioFiles.size(); i++) {
      log->infoMsg(__FILE__, __LINE__, QString("cutAudioFile[%1]: %2 (%3 bytes)")
          .arg(i).arg(cutAudioFiles[i]).arg(QFileInfo(cutAudioFiles[i]).size()));
    }
    emit statusReport(0, StatusReportArgs::Step, tr("Muxing video and audio..."), 0);
    qApp->processEvents();
    TTMkvMergeProvider mkvProvider;
    connect(&mkvProvider, &TTMkvMergeProvider::progressChanged,
            this,        &TTAVData::onMuxProgress);

    // Calculate frame duration in nanoseconds (e.g., "0:20000000ns" for 50fps)
    int frameDurationNs = (int)(1000000000.0 / frameRate);
    mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
    mkvProvider.setIsPAFF(vStream->isPAFF(), vStream->paffLog2MaxFrameNum());
    AVCodecID codecId = (vStream->streamType() == TTAVTypes::h265_video)
                      ? AV_CODEC_ID_HEVC
                      : AV_CODEC_ID_H264;
    mkvProvider.setVideoCodecId(codecId);

    // Apply A/V sync offset if present
    if (avOffsetMs != 0) {
      mkvProvider.setAudioSyncOffset(avOffsetMs);
    }

    // Note: per-track audio delay is already baked into each track's cut audio
    // file via audioKeepList above. Do NOT apply it again here via setAudioDelays()
    // — that would double-apply the delay.

    mkvProvider.setAudioLanguages(cutAudioLanguages);

    // Add chapters in first mux pass (no second container remux needed)
    QString chapterFile;
    if (TTCut::mkvCreateChapters && TTCut::mkvChapterInterval > 0 &&
        finalOutput.endsWith(".mkv", Qt::CaseInsensitive)) {

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
        mkvProvider.setTotalDurationMs(totalDurationMs);
        chapterFile = TTMkvMergeProvider::generateChapterFile(
            totalDurationMs, TTCut::mkvChapterInterval, TTCut::cutDirPath);
        if (!chapterFile.isEmpty()) {
          mkvProvider.setChapterFile(chapterFile);
        }
      }
    }

    bool success = mkvProvider.mux(finalOutput, tempVideoFile, cutAudioFiles, QStringList());

    if (success) {
      log->infoMsg(__FILE__, __LINE__, QString("Muxing complete: %1").arg(finalOutput));
      // Clean up temporary files
      QFile::remove(tempVideoFile);
      for (const QString& f : cutAudioFiles) {
        QFile::remove(f);
      }
    } else {
      log->errorMsg(__FILE__, __LINE__, QString("Muxing failed: %1").arg(mkvProvider.lastError()));
      emit statusReport(0, StatusReportArgs::Exit, tr("Muxing failed"), 0);
      if (!chapterFile.isEmpty()) QFile::remove(chapterFile);
      return;
    }

    // Clean up chapter file
    if (!chapterFile.isEmpty()) QFile::remove(chapterFile);

  emit statusReport(0, StatusReportArgs::Exit, tr("H.264/H.265 cutting complete"), 0);

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
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onCutFinished);

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
  // 0 = MPG (mplex)
  // 1 = MKV (libav matroska muxer)
  // 3 = Elementary (no muxing; not reachable from UI, kept as defensive default)

  switch (TTCut::outputContainer) {
    case 1: // MKV - use mkvmerge
      {
        TTMkvMergeProvider* mkvProvider = new TTMkvMergeProvider();

        connect(mkvProvider, &TTMkvMergeProvider::progressChanged,
                this,        &TTAVData::onMuxProgress);

        // Set frame duration for raw ES video (required for PTS assignment)
        TTVideoStream* videoStream = mpCutList->at(0).avDataItem()->videoStream();
        double frameRate = videoStream->frameRate();
        int frameDurationNs = (int)(1000000000.0 / frameRate);
        mkvProvider->setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
        mkvProvider->setIsPAFF(videoStream->isPAFF(), videoStream->paffLog2MaxFrameNum());
        AVCodecID codecId = (videoStream->streamType() == TTAVTypes::h265_video)
                          ? AV_CODEC_ID_HEVC
                          : AV_CODEC_ID_H264;
        mkvProvider->setVideoCodecId(codecId);

        // Apply A/V sync offset if present
        if (mAvSyncOffsetMs != 0) {
          mkvProvider->setAudioSyncOffset(mAvSyncOffsetMs);
          qDebug() << "MKV muxing: applying A/V sync offset" << mAvSyncOffsetMs << "ms";
        }

        // Note: per-track audio delay is already baked into the audio cut files
        // via the keepList times in onDoCut(). Do NOT apply it again here via
        // setAudioDelays() — that would double-apply the delay.

        // Pass explicit language tags from data model
        mkvProvider->setAudioLanguages(muxItem.getAudioLanguages());
        mkvProvider->setSubtitleLanguages(muxItem.getSubtitleLanguages());

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
            mkvProvider->setTotalDurationMs(totalDurationMs);
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

    case 3: // Elementary - no muxing
      qDebug() << "Elementary output selected, skipping muxing";
      break;

    case 0: // TS - use mplex (default, existing behavior)
    default:
      {
        TTMplexProvider* mplexProvider = new TTMplexProvider(mpMuxList);

        // Apply A/V sync offset if present
        if (mAvSyncOffsetMs != 0) {
          mplexProvider->setAudioSyncOffset(mAvSyncOffsetMs);
        }

        connect(mplexProvider, &TTMplexProvider::statusReport,
                this,          &TTAVData::onStatusReport);

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
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onCutFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);
}

// /////////////////////////////////////////////////////////////////////////////
// Audio-only cut: extracts the audio track(s) for the kept segments without
// touching video. Output format is selected by TTCut::audioOnlyFormat:
//   AOF_OriginalES   — one ES file per track (.ac3, .mp2, ...)
//   AOF_OriginalMKA  — one .mka with all tracks (stream-copy)
//   AOF_MP3          — one .mp3 per track (re-encode, Stage 2)
//   AOF_AAC          — one .m4a per track (re-encode, Stage 2)
// /////////////////////////////////////////////////////////////////////////////
void TTAVData::doAudioOnlyCut(QString tgtFileName, TTCutList* cutList)
{
  mLastCutWasAudioOnly = true;
  mLastCutOutputSummary.clear();

  if (cutList == 0 || cutList->count() == 0) return;
  TTAVItem* avItem = cutList->at(0).avDataItem();
  if (!avItem || avItem->audioCount() == 0) return;
  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return;
  double frameRate = vStream->frameRate();

  emit statusReport(0, StatusReportArgs::Init, tr("Initializing audio cut..."), 0);
  qApp->processEvents();

  // Build video-domain keep list (extra-frame-corrected, no delay yet)
  QList<QPair<double, double>> videoKeepList;
  for (int c = 0; c < cutList->count(); c++) {
    TTCutItem ci = cutList->at(c);
    int extraIn  = countExtraFramesBefore(ci.cutInIndex());
    int extraOut = countExtraFramesBefore(ci.cutOutIndex() + 1);
    double cutIn  = (ci.cutInIndex()      - extraIn)  / frameRate;
    double cutOut = (ci.cutOutIndex() + 1 - extraOut) / frameRate;
    videoKeepList.append(qMakePair(cutIn, cutOut));
  }

  emit statusReport(0, StatusReportArgs::Start, tr("Cutting audio tracks..."), avItem->audioCount());
  qApp->processEvents();

  // Stage 1: stream-copy each track to its source codec
  QStringList trackFiles;
  QStringList trackLanguages;
  QList<float> firstTrackDrifts;

  for (int i = 0; i < avItem->audioCount(); i++) {
    TTAudioStream* audioStream = avItem->audioStreamAt(i);
    int delayMs = avItem->audioListItemAt(i).getDelayMs();

    AudioCutPlan plan = planAudioCut(audioStream, videoKeepList, delayMs);
    if (plan.keepList.isEmpty()) {
      log->errorMsg(__FILE__, __LINE__, QString("Audio track %1: empty plan").arg(i+1));
      continue;
    }
    if (i == 0) firstTrackDrifts = plan.drifts;

    QString tgtAudioFilePath = createCutFileName(tgtFileName, audioStream->fileName(), i+1);
    if (QFileInfo(tgtAudioFilePath).exists()) QFile::remove(tgtAudioFilePath);

    QList<int> targetAcmods;
    QString audioExt = QFileInfo(audioStream->filePath()).suffix().toLower();
    if (TTCut::normalizeAcmod && audioExt == "ac3") {
      for (int s = 0; s < plan.keepList.size(); s++) {
        TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
            audioStream->filePath(), plan.keepList[s].first, plan.keepList[s].second);
        targetAcmods.append(aInfo.mainAcmod);
      }
    }

    TTFFmpegWrapper ffmpegAudio;
    if (ffmpegAudio.cutAudioStream(audioStream->filePath(), tgtAudioFilePath,
                                    plan.keepList, TTCut::normalizeAcmod, targetAcmods)) {
      trackFiles     << tgtAudioFilePath;
      trackLanguages << avItem->audioListItemAt(i).getLanguage();
      log->infoMsg(__FILE__, __LINE__, QString("Audio track %1 cut: %2").arg(i+1).arg(tgtAudioFilePath));
    } else {
      log->errorMsg(__FILE__, __LINE__, QString("Audio cut failed for track %1").arg(i+1));
    }

    emit statusReport(0, StatusReportArgs::Step, tr("Audio track %1 done").arg(i+1), i+1);
    qApp->processEvents();
  }

  // Drift display (first track only, matches existing convention)
  emit cutAudioDriftCalculated(firstTrackDrifts);

  if (trackFiles.isEmpty()) {
    log->errorMsg(__FILE__, __LINE__, "Audio-only cut produced no output files");
    mLastCutOutputSummary = tr("Audio cut failed");
    emit statusReport(0, StatusReportArgs::Exit, tr("Audio cut failed"), 0);
    emit cutFinished();
    return;
  }

  // Dispatch by chosen output format
  switch (TTCut::audioOnlyFormat) {
    case TTCut::AOF_OriginalES: {
      QString dir = QFileInfo(trackFiles.first()).absolutePath();
      log->infoMsg(__FILE__, __LINE__,
                   QString("Audio-only cut complete: %1 ES file(s) in %2")
                     .arg(trackFiles.size()).arg(dir));
      mLastCutOutputSummary = tr("%1 audio file(s) in %2").arg(trackFiles.size()).arg(dir);
      break;
    }

    case TTCut::AOF_OriginalMKA: {
      QString mkaPath = QFileInfo(QDir(TTCut::cutDirPath),
                                  QFileInfo(tgtFileName).completeBaseName() + ".mka").absoluteFilePath();
      if (QFileInfo(mkaPath).exists()) QFile::remove(mkaPath);

      emit statusReport(0, StatusReportArgs::Step, tr("Muxing audio tracks into MKA..."), 0);
      qApp->processEvents();

      TTMkvMergeProvider mkvProv;
      if (!mkvProv.muxAudioOnly(mkaPath, trackFiles, trackLanguages)) {
        log->errorMsg(__FILE__, __LINE__,
                      QString("MKA mux failed: %1").arg(mkvProv.lastError()));
        mLastCutOutputSummary = tr("MKA mux failed: %1").arg(mkvProv.lastError());
      } else {
        log->infoMsg(__FILE__, __LINE__, QString("Audio-only cut complete: %1").arg(mkaPath));
        for (const QString& f : trackFiles) QFile::remove(f);
        mLastCutOutputSummary = mkaPath;
      }
      break;
    }

    case TTCut::AOF_MP3:
    case TTCut::AOF_AAC: {
      QString dir = QFileInfo(trackFiles.first()).absolutePath();
      log->warningMsg(__FILE__, __LINE__,
                      "MP3/AAC re-encoding not implemented yet — leaving original ES files");
      mLastCutOutputSummary = tr("MP3/AAC re-encoding not implemented yet — original ES files in %1").arg(dir);
      break;
    }
  }

  emit statusReport(0, StatusReportArgs::Exit, tr("Audio cut complete"), 0);
  emit cutFinished();
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

// *****************************************************************************
// Count extra frames before a given frame index (binary search)
// Used for audio time correction: time = (frame - extras_before) / fps
// *****************************************************************************
int TTAVData::countExtraFramesBefore(int frameIndex) const
{
  if (mExtraFrameIndices.isEmpty()) return 0;

  int lo = 0, hi = mExtraFrameIndices.size();
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (mExtraFrameIndices[mid] < frameIndex)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

// *****************************************************************************
// Audio-burst detection helpers shared by cut list, preview dialog, and the
// final-cut warning. All sites must probe the same boundary time, including
// the extra-frame correction; otherwise threshold checks land on different
// audio frames and produce inconsistent warnings.
// *****************************************************************************
TTAVData::CutBurstInfo TTAVData::detectCutOutBurst(const TTCutItem& item) const
{
  CutBurstInfo info;
  TTAVItem* avItem = item.avDataItem();
  if (!avItem || avItem->audioCount() == 0) return info;

  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return info;
  double frameRate = vStream->frameRate();
  if (frameRate <= 0) return info;

  QString audioFile = avItem->audioStreamAt(0)->filePath();

  int extraOut = countExtraFramesBefore(item.cutOutIndex() + 1);
  double cutOutTime = (item.cutOutIndex() + 1 - extraOut) / frameRate;

  bool detected = TTFFmpegWrapper::detectAudioBurst(
      audioFile, cutOutTime, true, info.burstDb, info.contextDb);

  int threshold = TTCut::burstThresholdDb;
  if (detected && threshold != 0 && info.burstDb < threshold) detected = false;

  info.present = detected;
  return info;
}

// *****************************************************************************
// Build the audio cut plan: per-segment (startTime, endTime) snapped to the
// source audio's frame grid, with feed-forward drift compensation across
// segments. Without this, each segment loses up to one audio frame at start
// and end (cutAudioStream's "fit completely" rule), and the loss accumulates
// monotonically over the whole timeline. With feed-forward, the cumulative
// drift stays bounded ±½ audio-frame in steady state.
//
// The resulting (startTime, endTime) pairs are exact multiples of the audio
// frame duration, so cutAudioStream's skip/stop rules keep precisely the
// planned frames per segment.
// *****************************************************************************
TTAVData::AudioCutPlan TTAVData::planAudioCut(TTAudioStream* audioStream,
                                              const QList<QPair<double, double>>& videoKeepList,
                                              int delayMs) const
{
  AudioCutPlan plan;
  if (!audioStream || videoKeepList.isEmpty()) return plan;

  TTAudioHeader* hdr = audioStream->headerAt(0);
  if (!hdr) return plan;

  double audioFrameMs = hdr->frame_time;       // ms per audio frame, codec-aware
  if (audioFrameMs <= 0) return plan;
  double audioFrameSec = audioFrameMs / 1000.0;

  double delaySec = delayMs / 1000.0;
  double runningDriftMs = 0.0;                 // audio_so_far - video_so_far, in ms

  for (int c = 0; c < videoKeepList.size(); c++) {
    double videoStartSec = qMax(0.0, videoKeepList[c].first  + delaySec);
    double videoEndSec   = qMax(videoStartSec, videoKeepList[c].second + delaySec);

    double videoSegMs = (videoEndSec - videoStartSec) * 1000.0;

    // Snap segment start to the nearest audio-frame boundary in the source.
    int    startFrame    = (int)qMax<double>(0.0, qRound(videoStartSec / audioFrameSec));
    double audioStartSec = startFrame * audioFrameSec;

    // Choose the number of audio frames so that, after this segment, the
    // accumulated audio length matches the accumulated video length as
    // closely as possible. Compensates the drift carried in from previous
    // segments (Feed-Forward).
    double targetAudioMs = videoSegMs - runningDriftMs;
    int    numFrames     = (int)qMax<double>(1.0, qRound(targetAudioMs / audioFrameMs));
    double actualAudioMs = numFrames * audioFrameMs;
    double audioEndSec   = audioStartSec + actualAudioMs / 1000.0;

    runningDriftMs += actualAudioMs - videoSegMs;

    plan.keepList.append(qMakePair(audioStartSec, audioEndSec));
    plan.drifts.append(static_cast<float>(runningDriftMs));
  }

  return plan;
}

TTAVData::CutBurstInfo TTAVData::detectCutInBurst(const TTCutItem& item) const
{
  CutBurstInfo info;
  TTAVItem* avItem = item.avDataItem();
  if (!avItem || avItem->audioCount() == 0) return info;

  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return info;
  double frameRate = vStream->frameRate();
  if (frameRate <= 0) return info;

  QString audioFile = avItem->audioStreamAt(0)->filePath();

  int extraIn = countExtraFramesBefore(item.cutInIndex());
  double cutInTime = (item.cutInIndex() - extraIn) / frameRate;

  bool detected = TTFFmpegWrapper::detectAudioBurst(
      audioFile, cutInTime, false, info.burstDb, info.contextDb);

  int threshold = TTCut::burstThresholdDb;
  if (detected && threshold != 0 && info.burstDb < threshold) detected = false;

  info.present = detected;
  return info;
}
