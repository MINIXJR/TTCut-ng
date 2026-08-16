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
// TTAVDATA
// ----------------------------------------------------------------------------

#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QTime>

#include <algorithm>

#include "ttaudiolist.h"
#include "ttcutlist.h"
#include "ttavdata.h"
#include "ttmuxlistdata.h"
#include "ttcutprojectdata.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../avstream/ttfilebuffer.h"
#include "ttcutparameter.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../common/istatusreporter.h"

#include "../extern/ttmplexprovider.h"
#include "../extern/ttmkvmergeprovider.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/ttavheader.h"
#include "../extern/ttffmpegwrapper.h"
#include "../extern/ttessmartcut.h"
#include "../avstream/tth26xvideostream.h"

#include "ttopenvideotask.h"
#include "ttopenaudiotask.h"
#include "ttopensubtitletask.h"
#include "ttcutpreviewtask.h"
#include "ttcutvideotask.h"
#include "tth26xcuttask.h"
#include "ttaudioonlycuttask.h"
#include "ttmuxtask.h"
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

// Pick the extra-frame index list for audio time correction. For MPEG-2 the
// bitstream parser knows the truth (picture_structure -> field pairs, in
// display-index space), so prefer it over the .info candidate list
// (raw-AU-numbered, and produced by a PTS heuristic that cannot tell
// field pairs from TS corruption). H.26x candidates are classified
// through the PAFF merge map (raw AU != merged frame): guard on the AU
// count, drop collapsed field pairs, skip candidates without a display
// slot, store real defects in display space. No-op if target is already
// populated.
static void loadExtraFrameIndices(QList<int>& target, const TTESInfo& esInfo,
                                  TTVideoStream* vStream)
{
  if (!target.isEmpty()) return;

  TTMpeg2VideoStream* mpeg2 = dynamic_cast<TTMpeg2VideoStream*>(vStream);
  if (mpeg2 != nullptr && !mpeg2->extraIndices().isEmpty()) {
    target = mpeg2->extraIndices();
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "Extra-frame source: MPEG-2 parser," << target.size() << "indices";
    return;
  }

  // H.26x: classify .info doubled-PTS candidates (raw AU numbering, one AU
  // per PES packet — PAFF fields separate) through the merge map. Only real
  // defects enter the audio-correction list, in display space.
  if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(vStream)) {
    const QList<int> candidates = esInfo.esDoubledPtsAus();
    if (candidates.isEmpty()) return;

    // Guard: the candidate numbering is only usable when the analyzed TS
    // and this ES agree on the AU count. On mismatch (damaged stream,
    // detached .info) discard rather than apply misaligned positions.
    if (esInfo.esTotalAus() != h26x->rawAuCount()) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString(".info es_total_aus=%1 does not match stream AU count %2 "
                  "- discarding %3 doubled-PTS candidates")
              .arg(esInfo.esTotalAus()).arg(h26x->rawAuCount())
              .arg(candidates.size()));
      return;
    }

    int fieldPairs = 0, noDispSkipped = 0;
    for (int raw : candidates) {
      if (h26x->rawAuIsCollapsedField(raw)) { ++fieldPairs; continue; }
      const int disp = h26x->mapRawAuToDisplayIndex(raw);
      if (disp < 0) { ++noDispSkipped; continue; }  // dropped leading pic / bad index
      target.append(disp);
    }
    std::sort(target.begin(), target.end());

    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "Extra-frame source: .info es_doubled_pts_aus classified:"
                 << target.size() << "real defects," << fieldPairs
                 << "legitimate field pairs," << noDispSkipped
                 << "skipped (no display slot)";
    if (noDispSkipped > 0)
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("%1 doubled-PTS candidates without display slot skipped "
                    "(dropped leading pictures)").arg(noDispSkipped));
    return;
  }

  // MPEG-2 fallback when the parser found nothing: the .info candidate list
  // (same numbering space as the field-granular MPEG-2 header list).
  if (mpeg2 != nullptr && esInfo.isLoaded() && !esInfo.esDoubledPtsAus().isEmpty()) {
    target = esInfo.esDoubledPtsAus();
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "Extra-frame source: .info es_doubled_pts_aus,"
                 << target.size() << "indices";
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
            if (TTSettings::instance()->logCutPipeline())
                qDebug() << "  Audio language from .info:" << af.fileName() << "=" << lang;
          }
          ++audioOrder;
        }
      }

      // Load VDR markers
      if (esInfo.hasMarkers()) {
        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "Found VDR markers in info file:" << esInfo.markerCount();

        QList<QPair<int, int>> cutPairs;
        QList<TTMarkerInfo> markers = esInfo.markers();

        for (int i = 0; i < markers.size() - 1; i += 2) {
          int cutIn = markers[i].frame;
          int cutOut = markers[i + 1].frame;

          if (cutIn > 0 && cutOut > cutIn) {
            cutPairs.append(qMakePair(cutIn, cutOut));
            if (TTSettings::instance()->logCutPipeline())
                qDebug() << "  VDR cut pair:" << cutIn << "-" << cutOut;
          }
        }

        if (!cutPairs.isEmpty()) {
          mpPendingVdrMarkers[avItem] = cutPairs;
        }
      }

      // Defer the extra-frame / audio-gap load AND the cluster dialog to
      // onOpenVideoFinished: the MPEG-2 parser's field-pair list
      // (extraIndices()) is only built once the async open task finishes, and
      // both the audio-correction source preference and the field-pair-vs-
      // defect classification need it. Marking the item here (fresh open only)
      // preserves the "no dialog on project reload" behaviour, since project
      // load bypasses openAVStreams. Mirrors mpPendingVdrMarkers.
      // Clear here so the fresh-open reload in onOpenVideoFinished always runs
      // (openAVStreams used to clear + load the list itself).
      mExtraFrameIndices.clear();
      if (avItem)
        mpPendingExtraFrameDialog.insert(avItem);

      // Show warning if decode errors were detected (legacy .info)
      if (esInfo.hasWarnings()) {
        QString warnMsg = tr("%1 decode errors detected in %2 region(s) during demux.\n\n"
                             "This MPEG-2 stream has defective GOPs that may cause A/V sync issues.\n"
                             "Recommendation: demux the recording again with the current "
                             "ttcut-demux - it finds and repairs such gaps.")
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
        QPushButton* okBtn = msgBox.addButton(QMessageBox::Ok);
        // Two AcceptRole buttons leave QMessageBox without an escape button,
        // which silently disables the window close (X) and Esc.
        msgBox.setEscapeButton(okBtn);
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
/*!
 * showExtraFrameClusterDialog
 * Classify .info doubled-PTS clusters against the MPEG-2 parser's field-pair
 * list and show the warning dialog. Runs once the video stream is built, so
 * extraIndices() is available. Fresh open only (see mpPendingExtraFrameDialog).
 */
void TTAVData::showExtraFrameClusterDialog(TTAVItem* avItem, TTVideoStream* vStream,
                                           const TTESInfo& esInfo)
{
  if (avItem == nullptr) return;

  // Audio gap indices (marker visualization only, NOT audio time correction).
  mAudioGapIndices = esInfo.audioGapFrames();
  if (!mAudioGapIndices.isEmpty() && TTSettings::instance()->logCutPipeline())
      qDebug() << "Loaded" << mAudioGapIndices.size() << "audio gap frame indices";

  // Cluster source for the video pass is the .info list itself, so unconfirmed
  // PTS-heuristic hits still surface as "Defekt:" even though mExtraFrameIndices
  // holds the parser list for MPEG-2. The parser's field-pair positions confirm
  // which clusters are legitimate field pairs vs. real suspects.
  // MPEG-2: raw .info candidates, confirmed against the parser field pairs.
  // H.26x: mExtraFrameIndices already holds the classified real defects
  // (display space; legitimate field pairs are structurally excluded by the
  // merge map in loadExtraFrameIndices, which runs before this dialog).
  TTMpeg2VideoStream* mpeg2Vs = dynamic_cast<TTMpeg2VideoStream*>(vStream);
  TTH26xVideoStream*  h26xVs  = dynamic_cast<TTH26xVideoStream*>(vStream);
  QList<int> infoExtras = h26xVs ? mExtraFrameIndices : esInfo.esDoubledPtsAus();
  QList<int> parserPairs = mpeg2Vs ? mpeg2Vs->extraIndices() : QList<int>();

  bool hasAudioCorruptRanges = false;
  for (int t = 0; t < esInfo.audioTrackCount(); ++t) {
    if (!esInfo.audioTrack(t).corruptRanges.isEmpty()) { hasAudioCorruptRanges = true; break; }
  }

  if (infoExtras.isEmpty() && mAudioGapIndices.isEmpty() &&
      esInfo.esMissingRanges().isEmpty() && esInfo.corruptFrameRanges().isEmpty() &&
      !hasAudioCorruptRanges)
      return;

  double frameRate = vStream ? vStream->frameRate() : 25.0;
  int gapFrames    = TTSettings::instance()->extraFrameClusterGapSec() * frameRate;
  int offsetFrames = TTSettings::instance()->extraFrameClusterOffsetSec() * frameRate;

  // A cluster is a confirmed field-pair cluster when at least one parser
  // field-pair position lies within +/-4 of its range (4 = local B-reorder
  // distance M-1 plus slack). Confirmed clusters are counted for the log only;
  // unconfirmed ones become a visible "Defekt:" marker (see emitCluster).
  auto clusterConfirmed = [&](int cs, int ce) -> bool {
      for (int p : parserPairs)
          if (p >= cs - 4 && p <= ce + 4) return true;
      return false;
  };

  QList<TTStreamPoint> clusters;
  int confirmedClusters = 0;
  int unconfirmedClusters = 0;
  int defectVideoFrames = 0;   // frames in unconfirmed (real-defect) clusters only

  // Cluster pass 1: video doubled-PTS frames (.info es_doubled_pts_aus,
  // already classified into display space by loadExtraFrameIndices)
  if (!infoExtras.isEmpty()) {
      int clusterStart = infoExtras.first();
      int clusterEnd = clusterStart;
      int clusterCount = 1;

      auto emitCluster = [&]() {
          bool confirmed = clusterConfirmed(clusterStart, clusterEnd);
          if (confirmed) {
              // Parser-confirmed legitimate field-picture coding, not a defect.
              // Keep the count for logging, but do NOT add a visible timeline
              // marker: field pairs are a normal encoder property (interlaced
              // MPEG-2), not something the user needs flagged. The internal
              // audio correction reads the parser positions via
              // loadExtraFrameIndices(), independent of these markers.
              ++confirmedClusters;
              return;
          }
          ++unconfirmedClusters;
          defectVideoFrames += clusterCount;
          int pos = qMax(0, clusterStart - offsetFrames);
          double durSec = (clusterEnd - clusterStart + 1) / frameRate;
          QString desc = QString("Defekt: %1–%2 (%3 Frames, %4s)")
              .arg(clusterStart).arg(clusterEnd)
              .arg(clusterCount).arg(durSec, 0, 'f', 1);
          clusters.append(TTStreamPoint(pos, StreamPointType::Error, desc));
      };

      for (int i = 1; i < infoExtras.size(); ++i) {
          if (infoExtras[i] - clusterEnd <= gapFrames) {
              clusterEnd = infoExtras[i];
              clusterCount++;
          } else {
              emitCluster();
              clusterStart = infoExtras[i];
              clusterEnd = clusterStart;
              clusterCount = 1;
          }
      }
      emitCluster();
  }

  // Cluster pass 2: audio gap frames
  if (!mAudioGapIndices.isEmpty()) {
      int clusterStart = mAudioGapIndices.first();
      int clusterEnd = clusterStart;

      auto emitGapCluster = [&]() {
          int pos = qMax(0, clusterStart - offsetFrames);
          double durSec = (clusterEnd - clusterStart + 1) / frameRate;
          QString desc = QString("Audio-Gap: %1–%2 (%3s)")
              .arg(clusterStart).arg(clusterEnd).arg(durSec, 0, 'f', 1);
          clusters.append(TTStreamPoint(pos, StreamPointType::Error, desc));
      };

      for (int i = 1; i < mAudioGapIndices.size(); ++i) {
          if (mAudioGapIndices[i] - clusterEnd <= gapFrames) {
              clusterEnd = mAudioGapIndices[i];
          } else {
              emitGapCluster();
              clusterStart = mAudioGapIndices[i];
              clusterEnd = clusterStart;
          }
      }
      emitGapCluster();
  }

  // Cluster pass 3: mid-stream video loss + corrupt regions (defect repair,
  // .info es_missing_ranges / corrupt_frame_ranges). Ranges arrive pre-
  // clustered from the demuxer; only the marker emission happens here.
  int lossZones = 0, corruptZones = 0;
  int lossFrames = 0, corruptFrames = 0;
  const QList<TTESRange> missingRanges = esInfo.esMissingRanges();
  const QList<TTESRange> corruptRanges = esInfo.corruptFrameRanges();
  for (const TTESRange& r : missingRanges) {
    int pos = qMax(0, r.start - offsetFrames);
    double durSec = (r.ms >= 0) ? r.ms / 1000.0
                                : (r.end - r.start + 1) / frameRate;
    QString desc = QString("Videoverlust: %1–%2 (%3 s) — Audio angepasst")
        .arg(r.start).arg(r.end).arg(durSec, 0, 'f', 1);
    clusters.append(TTStreamPoint(pos, StreamPointType::Error, desc));
    ++lossZones;
    lossFrames += (r.end - r.start + 1);
    if (durSec > 2.0) {
      clusters.append(TTStreamPoint(r.end,
          StreamPointType::Error,
          QString("Signalverlust-Ende (≈%1 s fehlen)").arg(durSec, 0, 'f', 0)));
    }
  }
  for (const TTESRange& r : corruptRanges) {
    int pos = qMax(0, r.start - offsetFrames);
    clusters.append(TTStreamPoint(pos, StreamPointType::Error,
        QString("Bildstörungen: %1–%2").arg(r.start).arg(r.end)));
    ++corruptZones;
    corruptFrames += (r.end - r.start + 1);
  }

  // Cluster pass 3b: per-track audio structural damage found by the
  // ttcut-audiofix sanitizer (.info audio_N_corrupt_ranges). Junk bytes
  // were already removed at demux time; the marker shows WHERE, so the
  // user can cut around the audible defect. Ranges arrive pre-clustered.
  int audioCorruptZones = 0, audioCorruptFrames = 0;
  for (int t = 0; t < esInfo.audioTrackCount(); ++t) {
    const QList<TTESRange> ranges = esInfo.audioTrack(t).corruptRanges;
    for (const TTESRange& r : ranges) {
      int pos = qMax(0, r.start - offsetFrames);
      clusters.append(TTStreamPoint(pos, StreamPointType::Error,
          QString("Tonstörungen: %1–%2 (Spur %3)")
              .arg(r.start).arg(r.end).arg(t + 1)));
      ++audioCorruptZones;
      audioCorruptFrames += (r.end - r.start + 1);
    }
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "extra-frame clusters:" << confirmedClusters
               << "confirmed field pairs," << unconfirmedClusters << "unconfirmed,"
               << lossZones << "loss zones," << corruptZones << "corrupt zones,"
               << audioCorruptZones << "audio corrupt zones";

  // Nothing to report: either a clean stream or only parser-confirmed field
  // pairs. Field pairs are legitimate interlaced coding and are intentionally
  // NOT turned into timeline markers (see emitCluster above), so clusters is
  // empty here and we return silently -- no import, no dialog. Only real
  // defects, audio gaps, and demuxer-reported loss/corruption reach clusters.
  if (clusters.isEmpty()) return;

  // Show dialog with group listing (combined defect + gap totals)
  int totalDefects = defectVideoFrames + mAudioGapIndices.size() +
                      lossFrames + corruptFrames + audioCorruptFrames;
  QString msg = tr("%1 defective frames in %2 groups detected.\n")
      .arg(totalDefects)
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
  QPushButton* okBtn = msgBox.addButton(QMessageBox::Ok);
  // Two AcceptRole buttons leave QMessageBox without an escape button,
  // which silently disables the window close (X) and Esc.
  msgBox.setEscapeButton(okBtn);
  msgBox.exec();

  if (msgBox.clickedButton() == importBtn) {
      emit vdrMarkersLoaded(clusters);
  }
}

void TTAVData::onOpenVideoFinished(TTAVItem* avItem, TTVideoStream* vStream, int, const QString& demuxedAudio)
{
  if (avItem == nullptr) return;

  avItem->setVideoStream(vStream);

  // Load extra frame indices for audio time correction, now that the video
  // stream (and, for MPEG-2, the parser's field-pair list) is built. Runs for
  // ALL paths: direct open, project load. The cluster dialog only runs for a
  // fresh open (avItem was marked in openAVStreams), never on project reload.
  if (vStream) {
    QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
    TTESInfo esInfo;
    if (!infoFile.isEmpty()) esInfo.load(infoFile);

    if (mExtraFrameIndices.isEmpty())
      loadExtraFrameIndices(mExtraFrameIndices, esInfo, vStream);

    if (mpPendingExtraFrameDialog.remove(avItem))
      showExtraFrameClusterDialog(avItem, vStream, esInfo);
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
      if (TTSettings::instance()->logCutPipeline())
          qDebug() << "  VDR markers exceed frame count — halving for PAFF field-rate correction";
    }

    if (TTSettings::instance()->logCutPipeline())
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
        if (TTSettings::instance()->logCutPipeline())
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
      if (TTSettings::instance()->logCutPipeline())
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

  // Re-sort the audio list now that language is set — TTAudioItem::operator<
  // priorisiert AC3 > audioLanguagePreference > Discovery-Order. Ohne diese
  // Sortierung würde audioStreamAt(0) bis zum onThreadPoolExit (= Ende aller
  // Tasks) noch auf den falschen Track zeigen, was die Burst-Detection beim
  // initialen VDR-Cut-Add auf der falschen Sprachspur laufen ließe
  // (z.B. eng.mp2 statt deu.mp2 bei deu/eng-Quellen + audioLanguagePreference="deu").
  TTAudioList* audioList = avItem->audioDataList();
  if (audioList && audioList->count() > 1) {
    audioList->sortByOrder();
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
  // Delivered unconditionally: the MPEG-2 branch of onDoCut() has not
  // started the pool yet during its synchronous audio/subtitle phase, so
  // mpThreadTaskPool->onUserAbortRequest() below has no task to cancel.
  // mSyncPhaseAbort is what that phase polls instead.
  mSyncPhaseAbort.store(true, std::memory_order_relaxed);

  // Same situation at the other end of the MPEG-2 cut: the mplex step of an
  // MPG-output cut runs synchronously on this thread inside onCutFinished(),
  // after the pool run is already over, so the pool call below has no task
  // carrying it either. The provider polls its own flag in the loop it waits
  // in and stops the external process (see TTMplexProvider::requestAbort).
  if (mpMplexProvider != 0)
    mpMplexProvider->requestAbort();

	mpThreadTaskPool->onUserAbortRequest();
}

void TTAVData::onThreadPoolInit()
{
  // See mCutOperationActive: during a final cut the operation brackets are
  // emitted by onDoCut/onCutFinished, not by the pool.
  if (mCutOperationActive) return;

  emit statusReport(0, StatusReportArgs::Init, tr("starting thread pool"), 0);
}

void TTAVData::onThreadPoolExit()
{
  // Drop the abort connection openAVStreams() armed - the same discipline
  // finishMpeg2Cut() applies to onCutAborted(). This slot is the single place
  // every pool run ends on BOTH routes: TTThreadTaskPool emits exit() when the
  // queue drains and aborted() + exit() back to back on an abort, so the
  // connection lives exactly as long as the run that armed it. Without this
  // the connection survives every successful stream open for the rest of the
  // session (its only other disconnect sits inside the slot itself), and any
  // later cancelled cut or preview runs onOpenAVStreamsAborted() too: it sets
  // the current AV item to the LAST one and emits currentAVItemChanged(),
  // which in the GUI is a full stream switch (TTCutMainWindow::onAVItemChanged
  // re-wires the subtitle hook, rewrites TTSettings::encoderCodec and reloads
  // both frame widgets) executed inside the cut's abort teardown. With one
  // video open the slot's early return hides it; with two or more open the
  // cancel silently jumps the user to the last video. Disconnecting an
  // already-dropped connection is a no-op, so the abort route (which
  // disconnects it itself) is unaffected.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
             this,             &TTAVData::onOpenAVStreamsAborted);

  // Sort audio lists by priority (AC3 first, locale language first)
  for (int i = 0; i < mpAVList->count(); i++) {
    TTAudioList* audioList = mpAVList->at(i)->audioDataList();
    if (audioList->count() > 1) {
      audioList->sortByOrder();
    }
  }

  // onThreadTaskPool::onThreadTaskAborted emits aborted() then exit() back to
  // back on abort, so onCutAborted's Canceled emit is immediately followed by
  // this slot. Consume mCutOperationActive here (instead of onCutAborted
  // resetting it) so that pool-exit stays suppressed on the abort path too —
  // otherwise a stray "exiting thread pool" Exit would follow the Canceled,
  // breaking the one-closing-bracket guarantee the completion dialog relies
  // on. On the success path this is the only place the flag gets reset;
  // onCutFinished's own reset is then a harmless no-op.
  if (!mCutOperationActive)
    emit statusReport(0, StatusReportArgs::Exit, tr("exiting thread pool"), 0);
  else
    mCutOperationActive = false;

  // The MKV mux of an MPEG-2 cut is a SECOND pool run (see onCutFinished), so
  // without this the reload below fires twice per cut operation and both tree
  // views rebuild twice (measured: 2 instead of the 1 every other path emits).
  // The mux reads the cut elementary streams and writes the .mkv - it changes
  // no AV data at all, so its exit has nothing to reload. Consumed here, the
  // same one-shot arrangement as mCutOperationActive above, which also makes
  // it self-clearing on the abort path (onCutAborted runs before this slot).
  if (mMuxPoolRunActive) {
    mMuxPoolRunActive = false;
    return;
  }

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

  // Restore global settings from project file. Das setzt die transient
  // encoderPreset/Crf/Profile-Werte (TTSettings) auf das, was in .ttcut steht.
  // Der frühere Code mappte diese transient Werte ANSCHLIEßEND auf die
  // codec-spezifischen App-Defaults (setMpeg2Profile etc.) — das hatte den
  // Nebeneffekt dass jedes Project-Load die User-Defaults im Settings-Dialog
  // überschrieb (z.B. <EncoderProfile>2</EncoderProfile> aus alter Session
  // zwingt mpeg2Profile auf 2 bei jedem Reload). Dieser auto-merge ist
  // entfernt: die transient Values werden für DIESE Cut-Session verwendet,
  // die App-Defaults bleiben unter User-Kontrolle.
  mpProjectData->deserializeSettings();

  emit readProjectFileFinished(mpProjectData->filePath());

  delete mpProjectData;
  mpProjectData = 0;
}

/**
 * Reading TTCut project file aborted or error reading project file
 */
void TTAVData::onReadProjectFileAborted()
{
  if (TTSettings::instance()->logCutPipeline())
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
  // Drop the abort connection doCutPreview() made - the same discipline
  // finishMpeg2Cut() applies to onCutAborted(). Without it a completed
  // preview leaves onCutPreviewAborted() connected to the pool, and the next
  // cancelled operation would run it as well: it emits the preview's own
  // Canceled bracket, so a cancelled cut after a completed preview would
  // report TWO closing brackets. Disconnecting an already-dropped connection
  // is a no-op, so the abort path (which disconnects it itself) is unaffected.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted,
             this,             &TTAVData::onCutPreviewAborted);

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
    // Read the abort reason before deleting the task. Non-empty means a real
    // error (e.g. an un-cuttable damaged stream), not a plain user cancel —
    // inform the user with a dialog so the failed preview is not silent.
    const QString previewError = cutPreviewTask->errorMessage();
    delete cutPreviewTask;
    cutPreviewTask = 0;

    if (previewError.isEmpty()) {
      // Plain user cancel: close the operation with the Canceled bracket the
      // four final-cut paths emit, not with the pool's own "exiting thread
      // pool" Exit. That Exit made TTProgressBar force the bar to 100 %
      // (gui/ttprogressbar.cpp, the Exit branch) and TTCutMainWindow report
      // "Finished after ..." for a run the user had just cancelled.
      //
      // Arming mCutOperationActive is the same mechanism onCutAborted() uses:
      // TTThreadTaskPool::onThreadTaskAborted() emits aborted() and exit()
      // back to back, so onThreadPoolExit() runs immediately after this slot
      // and its else-branch consumes the flag instead of emitting the pool's
      // Exit - which keeps the one-closing-bracket guarantee the completion
      // dialog relies on. The preview does NOT own its opening bracket (the
      // pool's Init stays), so the flag is armed here rather than in
      // doCutPreview(): the success path must keep emitting the pool's Exit,
      // and TTCutPreviewTask::finished() is a queued signal whose order
      // against the pool's exit() is not guaranteed.
      mCutOperationActive = true;
      emit statusReport(0, StatusReportArgs::Canceled, tr("Preview cancelled"), 0);
    } else {
      QMessageBox::warning(TTCut::mainWindow, tr("Preview not possible"),
                           previewError);
    }
  }
}

/*!
 * createCutFileName — builds "<cutBase>_NNN.<ext>" inside TTSettings::cutDirPath().
 * Used for both per-track audio and subtitle output filenames.
 */
QString TTAVData::createCutFileName(QString cutBaseFileName, QString sourceFileName, int index)
{
  QString cutFileName = QString("%1_%2.%3").
    arg(QFileInfo(cutBaseFileName).completeBaseName()).
    arg(index, 3, 10, QLatin1Char('0')).
    arg(QFileInfo(sourceFileName).suffix());

  return QFileInfo(QDir(TTSettings::instance()->cutDirPath()), cutFileName).absoluteFilePath();
}

// /////////////////////////////////////////////////////////////////////////////
// Audio and video cut
//
//! Do the audio and video cut for given cut-list

// ----------------------------------------------------------------------------
// Burst confirmation before the final cut (shared by the audio-only and the
// normal path). Returns false when the user cancels. In non-interactive mode
// (--auto-cut) there is nobody to click the modal dialog - log the warnings
// and proceed (the "Cut anyway" semantics).
// ----------------------------------------------------------------------------
bool TTAVData::confirmBurstWarnings(TTCutList* cutList)
{
  if (cutList->count() == 0 || cutList->at(0).avDataItem()->audioCount() == 0)
    return true;

  QStringList burstWarnings;
  for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    CutBurstInfo bout = detectCutOutBurst(item);
    if (bout.present) burstWarnings << tr("Cut %1: audio burst at the end (%2 dB)")
                                      .arg(i + 1).arg(bout.burstDb, 0, 'f', 1);
    CutBurstInfo bin = detectCutInBurst(item);
    if (bin.present) burstWarnings << tr("Cut %1: audio burst at the start (%2 dB)")
                                     .arg(i + 1).arg(bin.burstDb, 0, 'f', 1);
  }
  if (burstWarnings.isEmpty()) return true;

  if (mNonInteractive) {
    TTMessageLogger* mlog = TTMessageLogger::getInstance();
    for (const QString& w : burstWarnings)
      mlog->warningMsg(__FILE__, __LINE__, w);
    mlog->warningMsg(__FILE__, __LINE__,
        QString("audio bursts detected at %1 cut boundarie(s) - proceeding (auto-cut)")
            .arg(burstWarnings.size()));
    return true;
  }

  QString msg = tr("The following cuts have detected audio bursts:\n\n")
              + burstWarnings.join("\n")
              + tr("\n\nUse preview to check if shift is needed.");
  QMessageBox warnBox(QMessageBox::Warning, tr("Audio Burst Warning"), msg,
                      QMessageBox::NoButton, TTCut::mainWindow);
  QPushButton* cutButton = warnBox.addButton(tr("Cut anyway"), QMessageBox::AcceptRole);
  warnBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
  warnBox.setDefaultButton(cutButton);
  warnBox.exec();
  return warnBox.clickedButton() == cutButton;
}

void TTAVData::computeCutLengths(TTCutList* cutList)
{
  mLastCutSourceMs = 0;
  mLastCutResultMs = 0;
  if (!cutList || cutList->count() == 0) return;

  qint64 resultMs = 0;
  for (int i = 0; i < cutList->count(); i++) {
    QTime t = cutList->at(i).cutLengthTime();
    resultMs += t.hour()*3600000 + t.minute()*60000 + t.second()*1000 + t.msec();
  }
  mLastCutResultMs = resultMs;

  // Source duration from at(0)'s video only (single-source assumption, matching
  // how the cut paths use at(0) throughout). A joined multi-file project would
  // under-count the source; that edge is cosmetic (spec-approved at(0) semantics).
  TTAVItem* avItem = cutList->at(0).avDataItem();
  if (avItem && avItem->videoStream()) {
    double fr = avItem->videoStream()->frameRate();
    if (fr > 0)
      mLastCutSourceMs =
          static_cast<qint64>(avItem->videoStream()->frameCount() / fr * 1000.0 + 0.5);
  }
}

namespace {
  // Calibration key for the audio stage: keyed by container/codec suffix of
  // the FIRST track ("audio/ac3", "audio/mp2", ...). Tracks of one recording
  // share the codec in practice; a mixed set just calibrates on track 1.
  QString audioCalibKey(TTAVItem* avItem)
  {
    if (avItem == 0 || avItem->audioCount() == 0) return QString();
    QString suffix = QFileInfo(avItem->audioStreamAt(0)->fileName()).suffix().toLower();
    if (suffix.isEmpty()) return QString();   // empty key = no persistence
    return QStringLiteral("audio/") + suffix;
  }

  double keepListSeconds(const QList<QPair<double,double>>& keepList)
  {
    double s = 0;
    for (const auto& kp : keepList) s += kp.second - kp.first;
    return qMax(0.001, s);
  }
}

void TTAVData::onDoCut(QString tgtFileName, TTCutList* cutList, bool audioOnly)
{
  // A stale abort request from a previous, already-finished operation must
  // not kill this one (see mSyncPhaseAbort).
  mSyncPhaseAbort.store(false, std::memory_order_relaxed);

  if (cutList == 0) cutList = mpCutList;

  // Remember WHICH list this operation runs on. onCutFinished() used to read
  // mpCutList instead, which is only the same object when the caller passed
  // nothing - the GUI's case. Any caller handing in its own list (the
  // diagnostic harnesses do) had the muxer read frame rate, PAFF state and
  // codec off a different, possibly empty list: with an empty mpCutList the
  // at(0) below is an out-of-range access, which Q_ASSERT turns into an abort
  // and a release build into undefined behaviour.
  mpRunningCutList = cutList;

  computeCutLengths(cutList);

  // Reset last-cut metadata; non-audio-only path leaves it cleared.
  mLastCutWasAudioOnly = false;
  mLastCutOutputSummary.clear();
  mLastCutError.clear();

  if (audioOnly) {
    // Burst warning still useful, dispatch the rest to the audio-only pipeline.
    if (!confirmBurstWarnings(cutList)) {
      emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
      return;
    }
    doAudioOnlyCut(tgtFileName, cutList);
    return;
  }

  // Detect stream type from first cut item
  TTVideoStream* firstStream = cutList->at(0).avDataItem()->videoStream();
  TTAVTypes::AVStreamType streamType = firstStream->streamType();
  bool isH264H265 = (streamType == TTAVTypes::h264_video || streamType == TTAVTypes::h265_video);

  // Check for unresolved audio bursts
  if (!confirmBurstWarnings(cutList)) {
    emit statusReport(StatusReportArgs::Finished, tr("Cut cancelled"), 0);
    return;
  }

  if (isH264H265) {
    // For H.264/H.265: use ffmpeg directly since native cutting is not implemented
    doH264Cut(tgtFileName, cutList);
    return;
  }

  // For MPEG-2: use traditional cutting workflow
  // The cut operation is larger than the thread pool run inside it: audio
  // and subtitles are cut synchronously BEFORE the pool starts, muxing runs
  // AFTER the pool exits (onCutFinished). Emit the operation brackets here
  // and suppress the pool's own Init/Exit until onCutFinished/onCutAborted.
  mCutOperationActive = true;

  // Announce the planned stages for the progress estimator. MPEG-2 runs
  // audio synchronously FIRST, then the pool video task, then mux (mplex or
  // MKV) inside onCutFinished.
  {
    TTAVItem* planItem = cutList->at(0).avDataItem();
    double keptSecs = 0.001;
    int totalFrames = 0;
    double fr = firstStream->frameRate();
    for (int i = 0; i < cutList->count(); i++)
      totalFrames += cutList->at(i).cutOutIndex() - cutList->at(i).cutInIndex() + 1;
    if (fr > 0) keptSecs = qMax(0.001, totalFrames / fr);
    QVector<TTStagePlan> plan;
    if (planItem->audioCount() > 0)
      plan.append({ StatusReportArgs::StageAudio, audioCalibKey(planItem),
                    keptSecs * planItem->audioCount() });
    // Video calib key stores a BLENDED ms-per-media-second factor across the
    // whole cut. For MPEG-2 stream-copy this is fairly stable; it only needs
    // to bridge the stage start and the pre-stage total, since the in-run
    // projection + correction take over within seconds.
    plan.append({ StatusReportArgs::StageVideo, QStringLiteral("video/mpeg2cut"), keptSecs });
    plan.append({ StatusReportArgs::StageMux, QStringLiteral("mux/mpeg2cut"), keptSecs });
    emit operationPlanReady(plan);
  }

  emit statusReport(0, StatusReportArgs::Init, tr("Initializing MPEG-2 cut..."), 0);
  qApp->processEvents();
  emit statusReport(0, StatusReportArgs::Start, tr("Cutting MPEG-2 video..."), 0);
  qApp->processEvents();
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
      // (MPEG-2 parser preferred over .info; see loadExtraFrameIndices).
      loadExtraFrameIndices(mExtraFrameIndices, esInfo, firstStream);
    }
  }

  cutVideoTask = new TTCutVideoTask(this);
  cutVideoTask->init(tgtFileName, cutList);

  // all video must have the same count of audio streams!
  // Cut all audio tracks against the shared, extra-frame-corrected keep list
  // (consolidated onto TTAVData::cutAudioTracks).
  double frameRate = cutList->at(0).avDataItem()->videoStream()->frameRate();
  auto videoKeepList = buildVideoKeepList(cutList, frameRate);
  const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
  TTAVItem* avItem = cutList->at(0).avDataItem();

  // Files this run has actually created, so an abort can clean up after
  // itself - during the synchronous audio/subtitle phase below (handled
  // right here), during the video pool run and up to the moment the mux task
  // takes the list over (both handled in onCutAborted). Cleared here so a
  // previous run's list can never be deleted by this one's abort.
  mCutProducedFiles.clear();

  if (avItem->audioCount() > 0)
    emit statusReport(0, StatusReportArgs::Stage, QString(), StatusReportArgs::StageAudio);
  int audioTracksCut = 0;   // counts ok==true callbacks; compared below
  cutAudioTracks(avItem, videoKeepList, normalizeAcmod,
      [&](int i, const QString& /*ext*/) {
        return createCutFileName(tgtFileName,
                                 avItem->audioStreamAt(i)->fileName(), i + 1);
      },
      [&](int /*i*/, const QString& path, const QString& lang, bool ok) {
        // Register the path even when the cut did NOT succeed: an aborted
        // audio cut leaves a partial file behind (TTFFmpegWrapper::
        // cutAudioStream finalizes the container before returning false —
        // see its own comment "the caller deletes the partial/empty output
        // file"), and the abort cleanup below can only remove what it knows
        // about. Mirrors TTH26xCutTask::doCut's mCreatedFiles handling.
        mCutProducedFiles << path;
        if (ok) {
          cutVideoTask->muxListItem()->appendAudioFile(path, lang);
          audioTracksCut++;
        }
      },
      [&](int i) {
        emit statusReport(0, StatusReportArgs::Step,
            tr("Cutting audio track %1 of %2...").arg(i+1).arg(avItem->audioCount()),
            i * 100 / qMax(1, avItem->audioCount()));
        qApp->processEvents();
      },
      [&](int i, int percent) {
        int overall = (i * 100 + percent) / qMax(1, avItem->audioCount());
        emit statusReport(0, StatusReportArgs::Step,
            tr("Cutting audio track %1 of %2...").arg(i+1).arg(avItem->audioCount()), overall);
        qApp->processEvents();
      },
      [this] { return mSyncPhaseAbort.load(std::memory_order_relaxed); });

  // cut subtitle streams against the same extra-frame-corrected keep list
  // as the audio (consolidated onto TTAVData::cutSubtitleTracks). No abort
  // predicate of its own (see TTH26xCutTask::doCut) — a request arriving
  // during this call is only caught by the check right below, once it
  // returns.
  cutSubtitleTracks(avItem, videoKeepList,
      [&](int i) {
        return createCutFileName(tgtFileName,
                                 avItem->subtitleStreamAt(i)->fileName(), i + 1);
      },
      [&](int /*i*/, const QString& path, const QString& lang, bool ok) {
        // Registered unconditionally, same reasoning as the audio lambda
        // above: a subtitle write interrupted by a genuine I/O failure can
        // leave a partial .srt behind even though cutSubtitleTracks() has
        // no abort predicate of its own. Mirrors TTH26xCutTask::doCut.
        mCutProducedFiles << path;
        if (ok) cutVideoTask->muxListItem()->appendSubtitleFile(path, lang);
      });

  // Abort landed during the synchronous audio/subtitle phase above, before
  // the pool ever started: none of the pool's own machinery (onCutAborted,
  // onThreadPoolExit) will run for this operation, so this is the only
  // place that can close the Canceled bracket and consume
  // mCutOperationActive. cutVideoTask was constructed but never handed to
  // the pool (mpThreadTaskPool->start() is below, still unreached) — the
  // pool never took ownership, so TTAVData is the only owner and must free
  // it itself.
  if (mSyncPhaseAbort.load(std::memory_order_relaxed)) {
    for (const QString& f : mCutProducedFiles) {
      if (!QFile::remove(f))
        log->warningMsg(__FILE__, __LINE__, QString("abort cleanup: could not remove %1").arg(f));
    }
    mCutProducedFiles.clear();
    delete cutVideoTask;
    cutVideoTask = nullptr;
    mCutOperationActive = false;
    finishCutOperation(CutOutcome::Cancelled, tr("Cut cancelled"));
    return;
  }

  // A missing track is a failure, not a footnote - same check as
  // TTH26xCutTask::runCut() and the audio-only path (c7436a07).
  // cutAudioTracks() skips failed tracks silently; audioTracksCut counted the
  // ok callbacks above. Deliberately AFTER the sync-abort branch, so a cancel
  // that raced the audio phase still reports as a cancel, and BEFORE the pool
  // start, so neither the video cut nor the mux ever run: the produced track
  // files stay on disk for a retry (a genuine error never cleans up), and no
  // calibration factor is written (only a regular Exit writes one).
  // Per-track reasons are in the log as errorMsg entries.
  if (audioTracksCut < avItem->audioCount()) {
    delete cutVideoTask;   // constructed above, never handed to the pool
    cutVideoTask = nullptr;
    mCutOperationActive = false;
    finishCutOperation(CutOutcome::Failed, tr("Cutting failed"),
        tr("Only %1 of %2 audio track(s) could be cut - "
           "the finished streams were kept, see the log for the reason")
            .arg(audioTracksCut).arg(avItem->audioCount()));
    return;
  }

  // The audio/subtitle muxListItem appends above must complete before the
  // pool is started: pool exit fires onCutFinished, which COPIES
  // muxListItem and hands that copy to the muxer (today as TTMuxTaskParams
  // for the mux task, before that to an inline mux - either way the copy is
  // taken there and then). cutAudioTracks/cutSubtitleTracks report status via
  // qApp->processEvents(), which can let a fast (cache-hot) video task finish
  // and drain the pool mid-way through these synchronous cuts — muxing a copy
  // taken before a later append landed. There is no way to express this
  // ordering constraint other than literally doing the appends first; do not
  // move pool start earlier.
  connect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onCutFinished);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // From here on the video ES is a product of this run too: TTCutVideoTask
  // creates the file as soon as its operation() starts, and a cancel during
  // the video phase has to take it with it (onCutAborted). Registered before
  // the start, not after, because the task may already be running when
  // start() returns.
  mCutProducedFiles << tgtFileName;

  // Init pool for video task only — audio is cut synchronously via FFmpegWrapper
  emit statusReport(0, StatusReportArgs::Stage, QString(), StatusReportArgs::StageVideo);
  mpThreadTaskPool->init(cutList->count());
  mpThreadTaskPool->start(cutVideoTask);
}

//! Do H.264/H.265 cut using TTESSmartCut (frame-accurate)
void TTAVData::doH264Cut(QString tgtFileName, TTCutList* cutList)
{
  // Reset here too (onDoCut() already does this before dispatching, since
  // doH264Cut() is private and only reachable through it) so the invariant
  // reads locally at every entry point, matching mCutOperationActive below.
  mSyncPhaseAbort.store(false, std::memory_order_relaxed);
  // Same reason: onCutAborted() is shared with the MPEG-2 path and deletes
  // whatever is in this list. TTH26xCutTask cleans up its own products, so
  // the list has to be empty here - stated locally instead of relying on the
  // MPEG-2 path having cleared it.
  mCutProducedFiles.clear();

  log->infoMsg(__FILE__, __LINE__, "Using TTESSmartCut for frame-accurate cutting");

  // Get source file and frame rate from first cut item
  TTAVItem* avItem = cutList->at(0).avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  QString sourceFile = vStream->filePath();
  double frameRate = vStream->frameRate();

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

  // Build cut list as pairs of (startTime, endTime) in seconds - segments to
  // KEEP (extra-frame-corrected, same conversion as every other producer).
  QList<QPair<double, double>> keepList = buildVideoKeepList(cutList, frameRate);
  for (int i = 0; i < keepList.size(); i++) {
    TTCutItem item = cutList->at(i);
    log->infoMsg(__FILE__, __LINE__, QString("Cut %1: frames %2-%3, time %4-%5")
        .arg(i+1).arg(item.cutInIndex()).arg(item.cutOutIndex())
        .arg(keepList[i].first, 0, 'f', 3).arg(keepList[i].second, 0, 'f', 3));
  }

  // The cut runs as a single pool task (TTH26xCutTask) but is a larger
  // operation than the pool run: the brackets below and the closing one in
  // onH26xCutFinished() belong to the cut, not to the pool. Suppress the
  // pool's own Init/Exit for the duration — same arrangement as the MPEG-2
  // branch of onDoCut().
  mCutOperationActive = true;

  // Announce the planned stages + work amounts for the progress estimator.
  {
    double keptSecs = keepListSeconds(keepList);
    QVector<TTStagePlan> plan;
    // Video calib key stores a BLENDED ms-per-media-second factor across the
    // whole cut. This is bound to be rough for H.26x since the re-encode
    // share (Smart Cut boundary GOPs vs. stream-copy interior) varies per
    // cut; that's acceptable because the in-run projection + correction
    // take over within seconds — the stored value only bridges the stage
    // start and the pre-stage totals.
    const QString videoCalibKey = (vStream->streamType() == TTAVTypes::h265_video)
        ? QStringLiteral("video/h265") : QStringLiteral("video/h264");
    plan.append({ StatusReportArgs::StageVideo, videoCalibKey, keptSecs });
    if (avItem->audioCount() > 0)
      plan.append({ StatusReportArgs::StageAudio, audioCalibKey(avItem),
                    keptSecs * avItem->audioCount() });
    plan.append({ StatusReportArgs::StageMux, QStringLiteral("mux/h26xcut"), keptSecs });
    emit operationPlanReady(plan);
  }

  emit statusReport(0, StatusReportArgs::Init, tr("Initializing H.264/H.265 cut..."), 0);
  qApp->processEvents();
  emit statusReport(0, StatusReportArgs::Start, tr("Cutting H.264/H.265 video..."), cutList->count());
  qApp->processEvents();

  QString finalOutput = tgtFileName;
  if (!finalOutput.endsWith(".mkv", Qt::CaseInsensitive)) {
    QFileInfo fi(finalOutput);
    finalOutput = QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
                           fi.completeBaseName() + ".mkv").absoluteFilePath();
  }

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

  // Create temporary video output
  QString tempVideoFile = QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
      QFileInfo(sourceFile).completeBaseName() + "_cut." + QFileInfo(sourceFile).suffix()).absoluteFilePath();

  // Everything the pipeline needs, copied by value while we are still on the
  // GUI thread. The worker must not read the cut list again: the caller owns
  // it (TTCutTreeView hands over a freshly built one, --auto-cut passes
  // mpCutList) and nothing guarantees it outlives the task.
  TTH26xCutParams params;
  params.sourceFile          = sourceFile;
  params.finalOutput         = finalOutput;
  params.tempVideoFile       = tempVideoFile;
  params.frameRate           = frameRate;
  params.avOffsetMs          = avOffsetMs;
  params.isH265              = (vStream->streamType() == TTAVTypes::h265_video);
  params.isPAFF              = vStream->isPAFF();
  params.paffLog2MaxFrameNum = vStream->paffLog2MaxFrameNum();
  params.totalDurationMs     = mLastCutResultMs;
  params.cutFrames           = cutFrames;
  params.keepList            = keepList;

  // Frame-granularity display-order map from the open stream's wrapper.
  // Required for PAFF: TTESSmartCut's buildFromFile fallback is
  // field-granularity and would mismatch the parser's frame count.
  if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(vStream)) {
    params.displayMap    = h26x->displayOrderMap();
    params.hasDisplayMap = true;
  }

  mpH26xCutTask = new TTH26xCutTask(this, avItem);
  mpH26xCutTask->init(params);

  connect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onH26xCutFinished);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // One task: video, audio, subtitles and muxing all run inside it, in the
  // order the synchronous version used.
  mpThreadTaskPool->init(1);
  mpThreadTaskPool->start(mpH26xCutTask);
}

//! H.264/H.265 cut task finished (pool exit, GUI thread)
void TTAVData::onH26xCutFinished()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onH26xCutFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // Close the operation bracket opened in doH264Cut(). onThreadPoolExit()
  // runs before this slot (it is connected in the constructor) and already
  // consumed the flag; the reset mirrors onCutFinished() and keeps the
  // invariant readable at both ends of the bracket.
  mCutOperationActive = false;

  if (mpH26xCutTask == 0) return;

  const QString exitMessage = mpH26xCutTask->exitMessage();
  const QString error       = mpH26xCutTask->lastError();
  const QString finalOutput = mpH26xCutTask->finalOutput();

  mpH26xCutTask->deleteLater();
  mpH26xCutTask = 0;

  if (!error.isEmpty()) {
    finishCutOperation(CutOutcome::Failed, exitMessage, error);
    return;
  }

  // Create a mux list item for the finished signal
  TTMuxListDataItem muxItem;
  muxItem.setVideoName(finalOutput);

  mpMuxList->appendItem(muxItem);
  mpMuxList->print();

  // Update cutVideoName with actual output filename for notification
  TTSettings::instance()->setCutVideoName(QFileInfo(finalOutput).fileName());

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "About to emit cutFinished() signal, cutVideoName =" << TTSettings::instance()->cutVideoName();
  finishCutOperation(CutOutcome::Success, exitMessage);
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "cutFinished() signal emitted";
}

//! Closes a cut operation: records the outcome, then reports it. See the
//! header comment on TTAVData::finishCutOperation for the ordering rationale.
void TTAVData::finishCutOperation(CutOutcome outcome, const QString& message,
                                   const QString& errorText)
{
  // Local copy, taken before anything below can mutate mLastCutError: at
  // least one caller (onCutFinished(), MPEG-2 path) passes mLastCutError
  // itself as `message` (finishCutOperation(Failed, mLastCutError)), which
  // binds the reference to the very member the next few lines assign to.
  // Harmless today (the assignment below is then effectively a = a), but a
  // reference that can alias its own future write target is fragile - copy
  // it out instead of relying on that not mattering.
  const QString msg = message;

  // 1. Record the outcome BEFORE any signal - see the header comment.
  //    errorText carries the longer error-dialog wording when the caller has
  //    one; otherwise the bracket text doubles as the error text.
  if (outcome == CutOutcome::Failed)
    mLastCutError = errorText.isEmpty() ? msg : errorText;
  else if (outcome == CutOutcome::Success)
    mLastCutError.clear();
  // Cancelled leaves the field untouched. This is meant for a deliberate
  // user abort, but onCutAborted() - the only caller that passes Cancelled -
  // is reached today by genuine errors too: TTThreadTask::run()'s
  // catch(TTException) sends the same aborted(this) signal as its
  // catch(TTAbortException) (see onCutAborted()'s own comment at
  // :2285-2293). Telling the two apart needs a second, reason-carrying
  // signal from TTThreadTask - not yet added; see TODO.md, Medium Priority,
  // "Eine echte TTException ... wird als Cut cancelled gemeldet". Until
  // then, a previously recorded mLastCutError can survive a run that was
  // actually a real failure misreported as Cancelled.

  // 2. Report it.
  emit statusReport(0,
      outcome == CutOutcome::Cancelled ? StatusReportArgs::Canceled
                                       : StatusReportArgs::Exit,
      msg, 0);

  // 3. cutFinished() follows the outcome, not a parameter. Measured on the
  //    pre-change tree: every Exit site emitted it (ttavdata.cpp:1843, :1859,
  //    :2210, :2405), no Canceled site did (:1631, :2089, :2281). The
  //    coupling is exhaustive, so a per-caller flag would carry the same
  //    derivable value everywhere. Should a future path need to differ, add
  //    the parameter then.
  if (outcome != CutOutcome::Cancelled)
    emit cutFinished();
}

//! Audio video cut finished
void TTAVData::onCutFinished()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onCutFinished);
  emit statusReport(0, StatusReportArgs::Stage, QString(), StatusReportArgs::StageMux);

  mpMuxList->appendItem(*(cutVideoTask->muxListItem()));
  mpMuxList->print();

  int lastIdx = mpMuxList->count() - 1;
  TTMuxListDataItem& muxItem = mpMuxList->itemAt(lastIdx);

  if (TTSettings::instance()->logCutPipeline()) {
    qDebug() << "onCutFinished: workingOutputContainer =" << TTSettings::instance()->workingOutputContainer();
    qDebug() << "onCutFinished: workingMuxMode =" << TTSettings::instance()->workingMuxMode();
    qDebug() << "onCutFinished: video =" << muxItem.getVideoName();
    qDebug() << "onCutFinished: audio =" << muxItem.getAudioNames();
    qDebug() << "onCutFinished: subtitle =" << muxItem.getSubtitleNames();
  }

  // Select muxer based on working container (transient, per-cut/per-project)
  // 0 = MPG (mplex)
  // 1 = MKV (libav matroska muxer)
  // 3 = Elementary (no muxing; not reachable from UI, kept as defensive default)

  switch (TTSettings::instance()->workingOutputContainer()) {
    case 1: // MKV - libav matroska muxer, run as a SECOND pool task
      {
        // Everything the muxer needs is copied by value here, on the GUI
        // thread; the worker never reads mpCutList or the mux list again.
        TTMuxTaskParams params;

        // Set frame duration for raw ES video (required for PTS assignment).
        // The list this operation actually ran on - see mpRunningCutList.
        TTCutList* runList = (mpRunningCutList != nullptr) ? mpRunningCutList : mpCutList;
        if (runList == nullptr || runList->count() == 0) {
          const QString msg = tr("The cut finished but its cut list is empty - "
                                 "cannot mux without knowing what was cut.");
          log->errorMsg(__FILE__, __LINE__, msg);
          mLastCutError = msg;
          finishMpeg2Cut();
          return;
        }
        TTVideoStream* videoStream = runList->at(0).avDataItem()->videoStream();
        double frameRate = videoStream->frameRate();
        int frameDurationNs = (int)(1000000000.0 / frameRate);
        params.defaultDurationNs   = QString("%1ns").arg(frameDurationNs);
        params.isPAFF              = videoStream->isPAFF();
        params.paffLog2MaxFrameNum = videoStream->paffLog2MaxFrameNum();
        AVCodecID codecId;
        switch (videoStream->streamType()) {
          case TTAVTypes::h265_video:  codecId = AV_CODEC_ID_HEVC;       break;
          case TTAVTypes::h264_video:  codecId = AV_CODEC_ID_H264;       break;
          default:                     codecId = AV_CODEC_ID_MPEG2VIDEO; break;
        }
        params.videoCodecId = codecId;

        // Apply A/V sync offset if present
        if (mAvSyncOffsetMs != 0) {
          params.audioSyncOffsetMs = mAvSyncOffsetMs;
          if (TTSettings::instance()->logCutPipeline())
              qDebug() << "MKV muxing: applying A/V sync offset" << mAvSyncOffsetMs << "ms";
        }

        // Note: per-track audio delay is already baked into the audio cut files
        // via the keepList times in onDoCut(). Do NOT apply it again here via
        // setAudioDelays() — that would double-apply the delay.

        // Pass explicit language tags from data model
        params.videoFile         = muxItem.getVideoName();
        params.audioFiles        = muxItem.getAudioNames();
        params.subtitleFiles     = muxItem.getSubtitleNames();
        params.audioLanguages    = muxItem.getAudioLanguages();
        params.subtitleLanguages = muxItem.getSubtitleLanguages();

        // Build MKV output filename
        QFileInfo videoInfo(muxItem.getVideoName());
        params.mkvOutput = QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
                                     videoInfo.completeBaseName() + ".mkv").absoluteFilePath();

        // Generate chapters if enabled. Still done here, on the GUI thread:
        // it is a millisecond text-file write, and keeping it out of the task
        // preserves the exact call order the synchronous version had.
        if (TTSettings::instance()->workingMkvCreateChapters() && TTSettings::instance()->workingMkvChapterInterval() > 0) {
          // mLastCutResultMs is the duration of the segments actually cut (the
          // cut list passed to onDoCut). The earlier code summed the full
          // project cut list here; for a "cut selected" subset this now matches
          // the real output length (the old full-list sum could place chapters
          // past end-of-file).
          qint64 totalDurationMs = mLastCutResultMs;

          if (TTSettings::instance()->logCutPipeline())
              qDebug() << "Total cut duration:" << totalDurationMs << "ms";

          if (totalDurationMs > 0) {
            // Mirrors the old order: setTotalDurationMs() was called whenever
            // chapters were requested and the duration was known, even if
            // generateChapterFile() then returned nothing.
            params.totalDurationMs = totalDurationMs;
            params.chapterFile = TTMkvMergeProvider::generateChapterFile(
                totalDurationMs, TTSettings::instance()->workingMkvChapterInterval(), TTSettings::instance()->cutDirPath());
          }
        }

        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "Muxing to MKV:" << params.mkvOutput;

        // Everything the run has produced so far feeds this mux; a cancel
        // makes all of it useless, so the task deletes it together with its
        // own partial products (spec: delete everything the run created).
        // Handing the list over transfers ownership - onCutAborted() must not
        // delete the same paths a second time.
        params.cleanupOnAbort = mCutProducedFiles;
        mCutProducedFiles.clear();

        mpMuxTask = new TTMuxTask(this);
        mpMuxTask->init(params);

        // Second pool run of this operation. onCutAborted() is still connected
        // to the pool's aborted() from onDoCut() and covers this run too.
        connect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onMpeg2MuxFinished);

        // Re-arm the operation flag. onThreadPoolExit() is connected in the
        // constructor and therefore ran BEFORE this slot; it consumed the flag
        // (its else-branch sets it to false) to suppress the pool's own Exit
        // for run A. The flag is a one-shot, so the second run needs it set
        // again - otherwise the pool would emit "starting thread pool" Init and
        // "exiting thread pool" Exit around the mux, and that stray Exit would
        // close the progress dialog before the real one arrives. The window in
        // which the flag is false is inside this single synchronous emit
        // exit(): no pool signal can occur in it (run A's task has already left
        // mTaskQueue, run B is started below).
        mCutOperationActive = true;
        // ... and mark this run as the mux run, so onThreadPoolExit() does not
        // reload the tree views a second time for one cut operation.
        mMuxPoolRunActive = true;

        mpThreadTaskPool->init(1);
        mpThreadTaskPool->start(mpMuxTask);
        // The operation continues in onMpeg2MuxFinished(); it, not this slot,
        // calls finishMpeg2Cut().
        return;
      }

    case 3: // Elementary - no muxing
      if (TTSettings::instance()->logCutPipeline())
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

        if (TTSettings::instance()->workingMuxMode() == 1) {
          mplexProvider->writeMuxScript();
        }
        else {
          // mplexPart() runs the external muxer synchronously on this thread
          // and pumps the event loop while it waits, so the Cancel button's
          // onUserAbortRequest() executes re-entrantly inside it. Publishing
          // the provider is what gives that slot something to cancel; cleared
          // again the moment the call returns, well before the delete below.
          mpMplexProvider = mplexProvider;

          // Seed the provider with a request that arrived BEFORE it existed.
          // There is a window between the video task's last abort poll and
          // the line above in which nobody is listening: the task polls
          // isAborted() at the top of each cut-list iteration, so a cancel
          // landing during the last iteration is never polled again;
          // TTThreadTask::run() emits finished() regardless of mIsAborted, so
          // the pool reports a normal finish and this slot runs. Without this
          // seed the request is simply lost and the mux completes - the exact
          // symptom this task was written to remove, in a narrower window
          // (measured: 5 of 8 runs on the branch point). mSyncPhaseAbort is
          // the record of that request: onUserAbortRequest() sets it
          // unconditionally and only the cut entry points clear it, so it is
          // still set here. Reaching this slot with it set means the pool did
          // NOT honour the abort (had it done so, aborted() would have fired
          // and onCutAborted() would have disconnected this slot).
          if (mSyncPhaseAbort.load(std::memory_order_relaxed))
            mplexProvider->requestAbort();

          mplexProvider->mplexPart(lastIdx);
          mpMplexProvider = 0;
        }

        const bool mplexAborted = mplexProvider->wasAborted();
        if (!mplexProvider->succeeded())
          mLastCutError = mplexProvider->lastError();
        delete mplexProvider;

        if (mplexAborted) {
          // Same situation as the synchronous audio phase in onDoCut(): the
          // pool run of this operation is already over (onThreadPoolExit()
          // ran before this slot), so none of the pool's abort machinery -
          // onCutAborted(), onThreadPoolExit() - will run for this cancel.
          // This block is therefore the only place that can close the
          // operation, and it does the same three things onDoCut()'s
          // sync-phase block does: delete what the run created, consume
          // mCutOperationActive, emit the single Canceled bracket. Falling
          // through to finishMpeg2Cut() instead would report Exit and
          // cutFinished() for a cancelled run.
          //
          // The disconnect mirrors finishMpeg2Cut()'s: onDoCut() armed
          // onCutAborted() and nothing on this path drops it otherwise, and
          // it is a slot that QFile::remove()s every entry of
          // mCutProducedFiles - a later, unrelated pool abort must not reach
          // it.
          disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

          // The cut elementary streams that fed the mux. The partial .mpg is
          // removed by TTMplexProvider itself (the only place that knows the
          // output path), so no file is deleted twice. Cancel-only by
          // construction: this branch is reached exclusively through
          // wasAborted(), never through an mplex failure - a failed mux keeps
          // its files for diagnosis.
          for (const QString& f : mCutProducedFiles) {
            if (f.isEmpty() || !QFile::exists(f)) continue;
            if (!QFile::remove(f))
              log->warningMsg(__FILE__, __LINE__, QString("abort cleanup: could not remove %1").arg(f));
          }
          mCutProducedFiles.clear();

          // Mirror of finishMpeg2Cut()'s own reset, and a no-op for the same
          // reason: onThreadPoolExit() already consumed the flag for the video
          // pool run that ended just before this slot. Written out so the
          // bracket state is stated locally instead of inferred.
          mCutOperationActive = false;
          finishCutOperation(CutOutcome::Cancelled, tr("Cut cancelled"));
          return;
        }
      }
      break;
  }

  finishMpeg2Cut();
}

//! MKV mux task of the MPEG-2 cut finished (second pool run, GUI thread)
//!
//! Slot order on that run's exit(): onThreadPoolExit() (constructor
//! connection, always first; suppresses the pool's own Exit and consumes
//! mCutOperationActive again) -> this slot. On a cancel the pool emits
//! aborted() BEFORE exit(), so onCutAborted() runs first and disconnects this
//! slot - a cancelled mux never gets here.
void TTAVData::onMpeg2MuxFinished()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onMpeg2MuxFinished);

  if (mpMuxTask == 0) {
    // Cannot happen through the wired paths (onCutAborted() disconnects this
    // slot when it clears the pointer), but it must not end the operation
    // silently if it ever does: this slot owns the closing bracket, and
    // without it the progress dialog stays open forever and --auto-cut never
    // reaches its quit. Report it as the defect it would be, then close.
    log->errorMsg(__FILE__, __LINE__,
        QString("MKV mux finished without a task object - closing the cut anyway"));
    mLastCutError = tr("Muxing failed: the mux task was gone");
    finishMpeg2Cut();
    return;
  }

  const QString     error       = mpMuxTask->lastError();
  const QString     mkvOutput   = mpMuxTask->mkvOutput();
  const QString     chapterFile = mpMuxTask->params().chapterFile;
  const QString     videoFile   = mpMuxTask->params().videoFile;
  const QStringList audioFiles  = mpMuxTask->params().audioFiles;
  const QStringList subFiles    = mpMuxTask->params().subtitleFiles;

  mpMuxTask->deleteLater();
  mpMuxTask = 0;

  if (error.isEmpty()) {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "MKV muxing completed successfully";

    // Record the real output name for the completion notification —
    // the same step the H.264 path does before emitting cutFinished().
    TTSettings::instance()->setCutVideoName(QFileInfo(mkvOutput).fileName());

    // Delete elementary streams if option is set
    if (TTSettings::instance()->workingMuxDeleteES()) {
      deleteElementaryStreams(videoFile, audioFiles, subFiles);
    }
  } else {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("MKV muxing failed: %1").arg(error));
    emit statusReport(0, StatusReportArgs::Step, tr("MKV muxing failed"), 0);
    mLastCutError = tr("Muxing failed: %1").arg(error);
  }

  // Clean up chapter file
  if (!chapterFile.isEmpty()) {
    QFile::remove(chapterFile);
  }

  finishMpeg2Cut();
}

//! Close the MPEG-2 cut operation - the single place that ends it.
//!
//! Called from the mplex/Elementary branches of onCutFinished() inline, and
//! from onMpeg2MuxFinished() for the MKV branch (whose mux is a second pool
//! run and therefore cannot finish inside onCutFinished()).
//!
//! Bracket ordering (verified, see onCutFinished()): by the time this runs,
//! onThreadPoolExit() has already consumed mCutOperationActive for the pool
//! run that just ended - it is connected in the constructor and therefore
//! fires before any slot connected later. The reset below is a mirror of that
//! (same situation as onH26xCutFinished()), and the Exit emitted here is the
//! ONE closing bracket of the whole operation.
void TTAVData::finishMpeg2Cut()
{
  // Drop the abort connection onDoCut() made. The two exit connections are
  // dropped by the slots that consume them (onCutFinished(),
  // onMpeg2MuxFinished()), but nothing dropped this one on the SUCCESS path -
  // so every completed MPEG-2 cut used to leave a live connection to
  // onCutAborted(), a slot that iterates mCutProducedFiles and QFile::remove()s
  // every entry. Any later pool abort (a cancelled preview, stream open or
  // frame search) then reached it. Harmless as long as the list is empty
  // between operations, but that invariant is global and stated in a comment
  // rather than enforced, and this is the one place that ends the MPEG-2 cut.
  // The abort path disconnects it itself (onCutAborted()), and disconnecting
  // an already-dropped connection is a no-op, so this is unconditional.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // The run is over: whatever it created is a wanted product now, not
  // abort-cleanup material (see mCutProducedFiles).
  mCutProducedFiles.clear();

  // The MPEG-2 path ends here, in a slot invoked by the thread pool's exit
  // signal — unlike doH264Cut()/doAudioOnlyCut(), which run synchronously and
  // emit at their end. Without this the signal was never sent at all: the GUI
  // never showed the completion dialog for MPEG-2 cuts, and --auto-cut never
  // reached the QApplication::quit() it connects to, so the process sat idle
  // until someone closed the window.
  //
  // Emitted regardless of mux success: a failed mux must not leave a headless
  // run hanging. (The H.264 path still returns early on mux failure - noted as
  // a follow-up, not changed here.)
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "finishMpeg2Cut: emitting cutFinished(), cutVideoName ="
               << TTSettings::instance()->cutVideoName();
  // Close the operation bracket opened in onDoCut(): re-enable the pool's
  // own status brackets and report the single final Exit (success or the
  // recorded error text).
  mCutOperationActive = false;
  if (mLastCutError.isEmpty())
    finishCutOperation(CutOutcome::Success, tr("Cut complete"));
  else
    finishCutOperation(CutOutcome::Failed, mLastCutError);
}

void TTAVData::onCutAborted()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onCutFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // The H.26x branch closes its bracket in onH26xCutFinished(), which the
  // pool's exit() would still reach right after this slot - the queue emits
  // aborted() and exit() back to back. Drop that connection so an aborted cut
  // reports Canceled and nothing else, exactly like the MPEG-2 branch above.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onH26xCutFinished);
  if (mpH26xCutTask != 0) {
    mpH26xCutTask->deleteLater();
    mpH26xCutTask = 0;
  }

  // Audio-only cut: same reasoning as the H.26x branch above - it closes its
  // own bracket in onAudioOnlyCutFinished(), which the pool's exit() would
  // otherwise still reach right after this slot.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onAudioOnlyCutFinished);
  if (mpAudioOnlyCutTask != 0) {
    mpAudioOnlyCutTask->deleteLater();
    mpAudioOnlyCutTask = 0;
  }

  // Same for the MPEG-2 MKV mux, which is a pool run of its own (see
  // onCutFinished): its exit() would otherwise reach onMpeg2MuxFinished()
  // right after this slot and report Exit + cutFinished() on a cancelled run.
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onMpeg2MuxFinished);
  if (mpMuxTask != 0) {
    mpMuxTask->deleteLater();
    mpMuxTask = 0;
  }

  // Delete everything this run created - but ONLY for a deliberate cancel.
  // This slot is not cancel-specific: TTThreadTask::run()'s catch(TTException)
  // emits aborted(this) too, so a genuine I/O failure in the video task lands
  // here as well, and deleting the products of a failed run would take the
  // evidence with it (measured: an injected TTIOException wiped the partial
  // video ES AND the already finished audio track). The standing rule for this
  // feature is: on a cancel delete everything, on a real error leave the files
  // in place. mSyncPhaseAbort is the discriminator - onUserAbortRequest() sets
  // it unconditionally, and every cut entry point resets it - and it is the
  // same distinction TTMuxTask makes with isAborted() for its own products.
  //
  // The list holds the cut audio/subtitle files plus the partial video ES.
  // It is empty on the H.26x and audio-only paths (doH264Cut()/
  // doAudioOnlyCut() never fill it - TTH26xCutTask and TTAudioOnlyCutTask
  // clean up their own products via their own mCreatedFiles, and only the
  // MPEG-2 branch of onDoCut() ever fills this one) and empty once the mux
  // task has taken it over (onCutFinished() hands it to TTMuxTaskParams::
  // cleanupOnAbort and clears it), so no path deletes the same file twice.
  if (mSyncPhaseAbort.load(std::memory_order_relaxed)) {
    for (const QString& f : mCutProducedFiles) {
      if (f.isEmpty() || !QFile::exists(f)) continue;
      if (!QFile::remove(f))
        log->warningMsg(__FILE__, __LINE__, QString("abort cleanup: could not remove %1").arg(f));
    }
  }
  // Cleared either way: the operation is over, and a later abort must never
  // delete a previous run's products.
  mCutProducedFiles.clear();

  // Report the operation's Canceled bracket (see onDoCut), but do NOT reset
  // mCutOperationActive here: TTThreadTaskPool::onThreadTaskAborted emits
  // aborted() then exit() back to back, so onThreadPoolExit runs right after
  // this slot and must still see the flag set to suppress its own "exiting
  // thread pool" Exit. onThreadPoolExit consumes (resets) the flag.
  if (mCutOperationActive) {
    // A cancel and a failure both arrive here - TTThreadTask::run() ends in
    // aborted() either way (the comment above the cleanup block says as much,
    // and used the same distinction to decide whether to delete this run's
    // products). Only the closing bracket did not make the distinction, so a
    // cut that failed for a nameable reason reported "Cut cancelled": the
    // progress dialog said the user had cancelled, the error dialog stayed
    // shut, and lastCutError() stayed empty - the very confusion the H.26x
    // path was fixed for.
    const QString failure = mpThreadTaskPool->lastFailureMessage();
    if (failure.isEmpty()) {
      finishCutOperation(CutOutcome::Cancelled, tr("Cut cancelled"));
    } else {
      finishCutOperation(CutOutcome::Failed, tr("Cut failed"),
                         tr("The cut could not be completed:\n\n%1").arg(failure));
    }
  }
}

// /////////////////////////////////////////////////////////////////////////////
// Audio-only cut: extracts the audio track(s) for the kept segments without
// touching video. Output format is selected by TTSettings::audioOnlyFormat():
//   AOF_OriginalES   — one ES file per track (.ac3, .mp2, ...)
//   AOF_OriginalMKA  — one .mka with all tracks (stream-copy)
//   AOF_MP3          — one .mp3 per track (re-encode, Stage 2)
//   AOF_AAC          — one .m4a per track (re-encode, Stage 2)
// /////////////////////////////////////////////////////////////////////////////
void TTAVData::doAudioOnlyCut(QString tgtFileName, TTCutList* cutList)
{
  // Reset here too (onDoCut() already does this before dispatching, since
  // doAudioOnlyCut() is private and only reachable through it) so the
  // invariant reads locally at every entry point. Unused by this path (the
  // audio-only cut runs entirely inside TTAudioOnlyCutTask and is cancelled
  // through its own onUserAbort(), like the H.26x/MPEG-2 final cuts) but kept
  // for the same defensive reason doH264Cut() keeps it.
  mSyncPhaseAbort.store(false, std::memory_order_relaxed);
  // Same reason again, and the same wording as doH264Cut(): onCutAborted() is
  // shared with the MPEG-2 path and deletes whatever is in this list.
  // TTAudioOnlyCutTask cleans up its own products via its own mCreatedFiles,
  // so the list has to be empty here - re-established locally at every entry
  // point instead of relying on the previous operation having cleared it.
  mCutProducedFiles.clear();

  mLastCutWasAudioOnly = true;
  mLastCutOutputSummary.clear();

  if (cutList == 0 || cutList->count() == 0) return;
  TTAVItem* avItem = cutList->at(0).avDataItem();
  if (!avItem || avItem->audioCount() == 0) return;
  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return;
  double frameRate = vStream->frameRate();

  {
    double keptSecs = 0.001;
    if (frameRate > 0) {
      int totalFrames = 0;
      for (int i = 0; i < cutList->count(); i++)
        totalFrames += cutList->at(i).cutOutIndex() - cutList->at(i).cutInIndex() + 1;
      keptSecs = qMax(0.001, totalFrames / frameRate);
    }
    QVector<TTStagePlan> plan;
    plan.append({ StatusReportArgs::StageAudio, audioCalibKey(avItem),
                  keptSecs * avItem->audioCount() });
    if (TTSettings::instance()->workingAudioOnlyFormat() == TTCut::AOF_OriginalMKA)
      plan.append({ StatusReportArgs::StageMux, QStringLiteral("mux/audiomka"), keptSecs });
    emit operationPlanReady(plan);
  }

  // The cut runs as a single pool task (TTAudioOnlyCutTask) but is a larger
  // operation than the pool run itself: the brackets below and the closing
  // one in onAudioOnlyCutFinished() belong to the cut, not to the pool - same
  // arrangement as doH264Cut()/onH26xCutFinished().
  mCutOperationActive = true;

  emit statusReport(0, StatusReportArgs::Init, tr("Initializing audio cut..."), 0);
  qApp->processEvents();

  // Build video-domain keep list (extra-frame-corrected, no delay yet)
  auto videoKeepList = buildVideoKeepList(cutList, frameRate);

  emit statusReport(0, StatusReportArgs::Start, tr("Cutting audio tracks..."), avItem->audioCount());
  qApp->processEvents();

  // Everything the pipeline needs, copied by value while we are still on the
  // GUI thread. The worker must not read the cut list or TTSettings' working
  // set again after this point - see TTH26xCutParams for the same rule.
  TTAudioOnlyCutParams params;
  params.targetFileName  = tgtFileName;
  params.videoKeepList   = videoKeepList;
  params.normalizeAcmod  = TTSettings::instance()->normalizeAcmod();
  params.audioOnlyFormat = TTSettings::instance()->workingAudioOnlyFormat();
  if (params.audioOnlyFormat == TTCut::AOF_OriginalMKA) {
    params.mkaOutputPath = QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
        QFileInfo(tgtFileName).completeBaseName() + ".mka").absoluteFilePath();
  }

  mpAudioOnlyCutTask = new TTAudioOnlyCutTask(this, avItem);
  mpAudioOnlyCutTask->init(params);

  connect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onAudioOnlyCutFinished);
  connect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  mpThreadTaskPool->init(1);
  mpThreadTaskPool->start(mpAudioOnlyCutTask);
}

//! Audio-only cut task finished (pool exit, GUI thread)
void TTAVData::onAudioOnlyCutFinished()
{
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit,    this, &TTAVData::onAudioOnlyCutFinished);
  disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onCutAborted);

  // Close the operation bracket opened in doAudioOnlyCut(). onThreadPoolExit()
  // runs before this slot (connected in the constructor, so it fires first)
  // and already consumed the flag; the reset mirrors onH26xCutFinished() and
  // keeps the invariant readable at both ends of the bracket.
  mCutOperationActive = false;

  if (mpAudioOnlyCutTask == 0) return;

  const QString exitMessage   = mpAudioOnlyCutTask->exitMessage();
  const QString outputSummary = mpAudioOnlyCutTask->outputSummary();
  const QList<float> drifts   = mpAudioOnlyCutTask->drifts();
  // Empty on success and on a deliberate abort; set to a reason-carrying text
  // by TTAudioOnlyCutTask::runAudioCut() on a genuine failure (no track
  // produced an output file, or the MKA mux failed) - see lastError() in
  // ttaudioonlycuttask.h.
  const QString error         = mpAudioOnlyCutTask->lastError();

  mpAudioOnlyCutTask->deleteLater();
  mpAudioOnlyCutTask = 0;

  // Drift belongs to TTAVData and is emitted here, on the GUI thread, in the
  // same relative order (before the closing Exit) as the synchronous version
  // had right after cutAudioTracks() returned.
  emit cutAudioDriftCalculated(drifts);

  mLastCutOutputSummary = outputSummary;
  if (error.isEmpty())
    finishCutOperation(CutOutcome::Success, exitMessage);
  else
    finishCutOperation(CutOutcome::Failed, exitMessage, error);
}

// This forwarder is reached from two kinds of caller: the one remaining
// synchronous GUI-thread provider (mplex, run inline in onCutFinished for MPG
// output), which needs the event loop pumped so the progress window repaints
// while it blocks the GUI thread, and the cut tasks' worker threads
// (TTH26xCutTask, TTAudioOnlyCutTask, TTMuxTask), which must not. On a worker,
// processEvents() would pump *that* thread's queue - it would not repaint
// anything, but it would dispatch its deferred deletions at an arbitrary
// point inside the cut. The receivers get the signal through their own queued
// connections either way.
void TTAVData::onStatusReport(int state, const QString& msg, quint64 value)
{
  emit statusReport(0, state, msg, value);
  if (QThread::currentThread() == qApp->thread())
    qApp->processEvents();
}

// Mux progress. Since the MKV mux of both cut paths runs in a pool task
// (TTMuxTask for MPEG-2, TTH26xCutTask for H.26x), this is only ever called
// from a worker thread - the processEvents() the GUI-thread version used to
// do here would be dead code (and on a worker it would pump the wrong
// queue; see onStatusReport). The receivers get the report through their own
// queued connections.
void TTAVData::onMuxProgress(int percent, const QString& msg)
{
  emit statusReport(0, StatusReportArgs::Step, msg, percent);
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

  // burstMinDeltaDb == 0 disables burst detection entirely. Bail out before
  // touching the audio file: detectAudioBurst() would otherwise open and decode
  // it only for the result to be discarded. Read fresh on every call -- the
  // settings dialog triggers refreshHintIcons(), which re-evaluates all cuts.
  const int minDelta = TTSettings::instance()->burstMinDeltaDb();
  if (minDelta <= 0) return info;

  TTAVItem* avItem = item.avDataItem();
  if (!avItem || avItem->audioCount() == 0) return info;

  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return info;
  double frameRate = vStream->frameRate();
  if (frameRate <= 0) return info;

  QString audioFile = avItem->audioStreamAt(0)->filePath();

  int extraOut = countExtraFramesBefore(item.cutOutIndex() + 1);
  double cutOutTime = (item.cutOutIndex() + 1 - extraOut) / frameRate;

  info.present = TTFFmpegWrapper::detectAudioBurst(
      audioFile, cutOutTime, true, minDelta, info.burstDb, info.contextDb);

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

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "[DRIFT] planAudioCut start: audioFrameMs" << audioFrameMs
               << "delayMs" << delayMs << "segments" << videoKeepList.size();

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

    double snapMs = (videoStartSec - audioStartSec) * 1000.0;  // start-snap quantization
    runningDriftMs += actualAudioMs - videoSegMs;

    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "[DRIFT] planAudioCut seg" << c
                 << "videoStartSec" << videoStartSec << "videoEndSec" << videoEndSec
                 << "videoSegMs" << videoSegMs
                 << "audioStartSec" << audioStartSec << "audioEndSec" << audioEndSec
                 << "numFrames" << numFrames << "actualAudioMs" << actualAudioMs
                 << "snapMs" << snapMs << "runningDriftMs" << runningDriftMs;

    plan.keepList.append(qMakePair(audioStartSec, audioEndSec));
    plan.drifts.append(static_cast<float>(runningDriftMs));
  }

  return plan;
}

// *****************************************************************************
// Build a video-domain keep list (seconds) from cut indices, with the
// extra-frame correction. Single home for a conversion previously open-coded
// in the final-cut, audio-only, and preview producers.
// *****************************************************************************
QList<QPair<double, double>> TTAVData::buildVideoKeepList(TTCutList* cutList,
                                                          double frameRate) const
{
  QList<QPair<double, double>> videoKeepList;
  if (!cutList || frameRate <= 0) return videoKeepList;
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "[DRIFT] buildVideoKeepList: frameRate" << frameRate
               << "extras_total" << mExtraFrameIndices.size()
               << "cuts" << cutList->count();
  for (int c = 0; c < cutList->count(); c++) {
    TTCutItem ci = cutList->at(c);
    int extraIn  = countExtraFramesBefore(ci.cutInIndex());
    int extraOut = countExtraFramesBefore(ci.cutOutIndex() + 1);
    double cutInTime  = (ci.cutInIndex()      - extraIn)  / frameRate;
    double cutOutTime = (ci.cutOutIndex() + 1 - extraOut) / frameRate;
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "[DRIFT] Cut" << c
                 << "cutInIndex" << ci.cutInIndex() << "extraIn" << extraIn
                 << "cutOutIndex" << ci.cutOutIndex() << "extraOut" << extraOut
                 << "cutInTime" << cutInTime << "cutOutTime" << cutOutTime
                 << "segMs" << ((cutOutTime - cutInTime) * 1000.0);
    videoKeepList.append(qMakePair(cutInTime, cutOutTime));
  }
  return videoKeepList;
}

// *****************************************************************************
// AC3-only per-segment target acmod list (majority acmod per kept window),
// used by cutAudioStream to normalize acmod across segments. Empty for
// non-AC3 or when normalization is off.
// *****************************************************************************
QList<int> TTAVData::computeTargetAcmods(const QString& audioFile, const QString& ext,
                                         const QList<QPair<double, double>>& keepList,
                                         bool normalizeAcmod) const
{
  QList<int> targetAcmods;
  if (normalizeAcmod && ext.toLower() == "ac3") {
    for (int s = 0; s < keepList.size(); s++) {
      TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
          audioFile, keepList[s].first, keepList[s].second);
      targetAcmods.append(aInfo.mainAcmod);
    }
  }
  return targetAcmods;
}

// *****************************************************************************
// Convenience overload: cut ALL of avItem's audio tracks. Builds the all-tracks
// index list (empty for a null avItem, which the main overload rejects) and
// forwards. Replaces the `QList<int> tracks; for(...) tracks << i;` boilerplate
// that stood at every all-tracks call site.
QList<float> TTAVData::cutAudioTracks(
    TTAVItem* avItem,
    const QList<QPair<double, double>>& videoKeepList,
    bool normalizeAcmod,
    const std::function<QString(int, const QString&)>& outPath,
    const std::function<void(int, const QString&, const QString&, bool)>& onCut,
    const std::function<void(int)>& beforeCut,
    const std::function<void(int, int)>& onProgress,
    const std::function<bool()>& shouldAbort)
{
  QList<int> allTracks;
  if (avItem)
    for (int i = 0; i < avItem->audioCount(); i++) allTracks << i;
  return cutAudioTracks(avItem, allTracks, videoKeepList, normalizeAcmod, outPath, onCut, beforeCut, onProgress, shouldAbort);
}

// Cut all requested audio tracks against a shared video keep list. Absorbs the
// per-track loop, per-track delay, planAudioCut, AC3 acmod targets, and
// cutAudioStream that the six producers used to duplicate. Output naming and
// registration are supplied by the caller (mux list / file list / preview).
// Returns the first requested track's drifts (legacy "track 0" semantics).
// *****************************************************************************
QList<float> TTAVData::cutAudioTracks(
    TTAVItem* avItem,
    const QList<int>& trackIndices,
    const QList<QPair<double, double>>& videoKeepList,
    bool normalizeAcmod,
    const std::function<QString(int, const QString&)>& outPath,
    const std::function<void(int, const QString&, const QString&, bool)>& onCut,
    const std::function<void(int)>& beforeCut,
    const std::function<void(int, int)>& onProgress,
    const std::function<bool()>& shouldAbort)
{
  QList<float> firstDrifts;
  TTMessageLogger* log = TTMessageLogger::getInstance();
  if (!avItem || trackIndices.isEmpty()) return firstDrifts;

  for (int idx : trackIndices) {
    if (shouldAbort && shouldAbort()) break;

    // Range-check before audioStreamAt/audioListItemAt (QList::at asserts on
    // out-of-range) — this is a public method, callers may pass stale indices.
    if (idx < 0 || idx >= avItem->audioCount()) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("Audio track index %1 out of range (count %2)")
                        .arg(idx).arg(avItem->audioCount()));
      continue;
    }
    TTAudioStream* stream = avItem->audioStreamAt(idx);
    if (!stream) continue;

    int delayMs = avItem->audioListItemAt(idx).getDelayMs();
    AudioCutPlan plan = planAudioCut(stream, videoKeepList, delayMs);
    if (plan.keepList.isEmpty()) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("Audio track %1: empty plan").arg(idx + 1));
      continue;
    }
    // Legacy "first track" drift semantics: only the first requested index
    // contributes, and only if its plan was non-empty.
    if (idx == trackIndices.first()) firstDrifts = plan.drifts;

    const QString ext     = QFileInfo(stream->filePath()).suffix();
    const QString outFile = outPath(idx, ext);
    // Overwrite any stale output from a previous run before cutting. Centralized
    // here so outPath stays a pure path computation across all callers.
    if (QFileInfo(outFile).exists()) {
      log->infoMsg(__FILE__, __LINE__,
                   tr("deleting existing audio cut file: %1").arg(outFile));
      QFile::remove(outFile);
    }
    if (beforeCut) beforeCut(idx);

    QList<int> targetAcmods =
        computeTargetAcmods(stream->filePath(), ext, plan.keepList, normalizeAcmod);

    TTFFmpegWrapper ff;
    std::function<void(int)> perTrackCb;
    if (onProgress)
      perTrackCb = [&onProgress, idx](int p) { onProgress(idx, p); };
    bool ok = ff.cutAudioStream(stream->filePath(), outFile,
                                plan.keepList, normalizeAcmod, targetAcmods,
                                perTrackCb, shouldAbort);
    if (!ok) {
      // A deliberate cancel returns false through the same path as a real
      // failure (TTFFmpegWrapper::cutAudioStream). Only the latter is an
      // error - logging a user cancel at error level would put a failure line
      // in the persistent log for something the user asked for.
      if (shouldAbort && shouldAbort())
        log->infoMsg(__FILE__, __LINE__,
                     QString("Audio cut for track %1 aborted by user").arg(idx + 1));
      else
        log->errorMsg(__FILE__, __LINE__,
                      QString("Audio cut failed for track %1").arg(idx + 1));
    }
    onCut(idx, outFile, avItem->audioListItemAt(idx).getLanguage(), ok);
  }
  return firstDrifts;
}

// Convenience overload: cut ALL of avItem's subtitle tracks. Builds the
// all-tracks index list (empty for a null avItem, which the main overload
// tolerates via the loop guard below) and forwards. Mirrors the
// cutAudioTracks all-tracks convenience overload.
void TTAVData::cutSubtitleTracks(
    TTAVItem* avItem,
    const QList<QPair<double, double>>& keepList,
    const std::function<QString(int trackIdx)>& outPath,
    const std::function<void(int trackIdx, const QString& path,
                             const QString& lang, bool ok)>& onCut)
{
  QList<int> allTracks;
  if (avItem)
    for (int i = 0; i < avItem->subtitleCount(); i++) allTracks << i;
  cutSubtitleTracks(avItem, allTracks, keepList, outPath, onCut);
}

// Cut the given subtitle tracks of avItem against a shared video keep list.
// Mirrors cutAudioTracks' shape (outPath/onCut callbacks) but stays
// synchronous — no task pool — and deliberately skips
// TTCutParameter::lastCall(), which writes the MPEG-2 sequence-end trailer
// that has no place in an SRT file.
void TTAVData::cutSubtitleTracks(
    TTAVItem* avItem,
    const QList<int>& trackIndices,
    const QList<QPair<double, double>>& keepList,
    const std::function<QString(int trackIdx)>& outPath,
    const std::function<void(int trackIdx, const QString& path,
                             const QString& lang, bool ok)>& onCut)
{
  if (!avItem) return;
  for (int i : trackIndices) {
    if (i < 0 || i >= avItem->subtitleCount()) {
      log->errorMsg(__FILE__, __LINE__,
                    QString("Subtitle track index %1 out of range (count %2)")
                        .arg(i).arg(avItem->subtitleCount()));
      continue;
    }
    TTSubtitleStream* subStream = avItem->subtitleStreamAt(i);
    QString target = outPath(i);
    QString lang   = avItem->subtitleListItemAt(i).getLanguage();

    if (QFileInfo(target).exists()) {
      log->warningMsg(__FILE__, __LINE__,
          QString("deleting existing subtitle cut file: %1").arg(target));
      QFile::remove(target);
    }

    TTFileBuffer tgtStream(target, QIODevice::WriteOnly);
    TTCutParameter cutParams(&tgtStream);
    cutParams.setNumPicturesWritten(0);
    cutParams.setCutInIndex(0);
    cutParams.setCutOutIndex(0);

    bool ok = true;
    try {
      tgtStream.open();
      for (int s = 0; s < keepList.size(); s++) {
        int startMs = qRound(keepList[s].first  * 1000.0);
        int endMs   = qRound(keepList[s].second * 1000.0) - 1;
        subStream->cut(startMs, endMs, &cutParams);
        cutParams.setCutInIndex(cutParams.getCutOutIndex() + 1);
      }
      // deliberately NO cutParams.lastCall(): that writes the MPEG-2
      // sequence-end code, which has no place in an SRT file
      tgtStream.close();
    }
    catch (TTAbortException&) {
      tgtStream.close();
      throw;                       // user abort propagates like before
    }
    catch (TTException& ex) {
      tgtStream.close();
      log->errorMsg(__FILE__, __LINE__,
          QString("subtitle cut failed for %1: %2").arg(target).arg(ex.getMessage()));
      ok = false;
    }

    // A cut range without any subtitle entry yields a 0-byte file. Drop it
    // and report ok=false: an empty .srt is useless as a mux input, and mpv
    // errors out on it as a --sub-file ("Can not open external file").
    if (ok && QFileInfo(target).size() == 0) {
      QFile::remove(target);
      log->infoMsg(__FILE__, __LINE__,
          QString("no subtitle entries in cut range, removed empty %1").arg(target));
      ok = false;
    }

    onCut(i, target, lang, ok);
  }
}

TTAVData::CutBurstInfo TTAVData::detectCutInBurst(const TTCutItem& item) const
{
  CutBurstInfo info;

  // See detectCutOutBurst(): 0 means detection off, short-circuit before I/O.
  const int minDelta = TTSettings::instance()->burstMinDeltaDb();
  if (minDelta <= 0) return info;

  TTAVItem* avItem = item.avDataItem();
  if (!avItem || avItem->audioCount() == 0) return info;

  TTVideoStream* vStream = avItem->videoStream();
  if (!vStream) return info;
  double frameRate = vStream->frameRate();
  if (frameRate <= 0) return info;

  QString audioFile = avItem->audioStreamAt(0)->filePath();

  int extraIn = countExtraFramesBefore(item.cutInIndex());
  double cutInTime = (item.cutInIndex() - extraIn) / frameRate;

  info.present = TTFFmpegWrapper::detectAudioBurst(
      audioFile, cutInTime, false, minDelta, info.burstDb, info.contextDb);

  return info;
}
