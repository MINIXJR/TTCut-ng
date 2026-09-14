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
// TTAVLIST
// ----------------------------------------------------------------------------

#include "ttavlist.h"
#include "ttcutprojectdata.h"
#include "ttcutlist.h"
#include "ttaudiolist.h"
#include "ttsubtitlelist.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavstream.h"
#include "../common/ttexception.h"

#include <QList>
#include <QDir>
#include <QDebug>

/* /////////////////////////////////////////////////////////////////////////////
 * TTAVItem
 */
TTAVItem::TTAVItem(TTVideoStream* videoStream)
{
	mpVideoStream  = videoStream;
	mIsInList      = false;
	mpAudioList    = new TTAudioList();
	mpSubtitleList = new TTSubtitleList();
	mpCutList      = new TTCutList();
	mpMarkerList   = new TTMarkerList();

  connect(mpAudioList, &TTAudioList::itemAppended,                this, &TTAVItem::audioItemAppended);
	connect(mpAudioList, qOverload<int>(&TTAudioList::itemRemoved), this, qOverload<int>(&TTAVItem::audioItemRemoved));
  connect(mpAudioList, &TTAudioList::itemUpdated,                 this, &TTAVItem::audioItemUpdated);
	connect(mpAudioList, &TTAudioList::itemsSwapped,                this, &TTAVItem::audioItemsSwapped);

  connect(mpSubtitleList, &TTSubtitleList::itemAppended,                this, &TTAVItem::subtitleItemAppended);
  connect(mpSubtitleList, qOverload<int>(&TTSubtitleList::itemRemoved), this, qOverload<int>(&TTAVItem::subtitleItemRemoved));
  connect(mpSubtitleList, &TTSubtitleList::itemUpdated,                 this, &TTAVItem::subtitleItemUpdated);
  connect(mpSubtitleList, &TTSubtitleList::itemsSwapped,                this, &TTAVItem::subtitleItemsSwapped);

	connect(this, &TTAVItem::updated, mpAudioList,    &TTAudioList::onRefreshData);
	connect(this, &TTAVItem::updated, mpSubtitleList, &TTSubtitleList::onRefreshData);
	connect(this, &TTAVItem::updated, mpCutList,      &TTCutList::onRefreshData);
	connect(this, &TTAVItem::updated, mpMarkerList,   &TTMarkerList::onRefreshData);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTAVItem::~TTAVItem()
{
  if (mpAudioList    != 0) delete mpAudioList;
  if (mpSubtitleList != 0) delete mpSubtitleList;
  if (mpCutList      != 0) delete mpCutList;
  if (mpMarkerList   != 0) delete mpMarkerList;
  if (mpVideoStream  != 0) delete mpVideoStream;
}

/*!
 * setVideoStream
 */
void TTAVItem::setVideoStream(TTVideoStream* stream)
{
  mpVideoStream = stream;
}

bool TTAVItem::isInList()
{
	return mIsInList;
}

/*
 *  ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::appendAudioEntry(TTAudioStream* aStream, int order)
{
	mpAudioList->append(this, aStream, order);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::onRemoveAudioItem(int index)
{
	mpAudioList->remove(mpAudioList->at(index));

  // Removing a track invalidates every repair's stored index at or after the
  // removed position; the removed track's own repairs go with it.
  remapAudioRepairTracks([index](int track) {
    return track == index ? -1 : (track > index ? track - 1 : track);
  });
}

void TTAVItem::remapAudioRepairTracks(const std::function<int(int)>& newTrack)
{
  // TTAudioRepairItem tags itself with a track index and has no setter for
  // it (extern/ttaudiorepairitem.h) - rebuild via the full constructor
  // (isEnabled() carried over explicitly, the constructor defaults it to
  // true).
  QList<TTAudioRepairItem> updatedRepairs;
  for (const TTAudioRepairItem& repair : mAudioRepairs) {
    const int track = newTrack(repair.trackIndex());
    if (track < 0) continue;
    TTAudioRepairItem rebuilt(track, repair.frameFrom(), repair.frameTo(),
                               repair.channelMask(), repair.method());
    rebuilt.setEnabled(repair.isEnabled());
    updatedRepairs.append(rebuilt);
  }
  mAudioRepairs = updatedRepairs;
}

void TTAVItem::onSwapAudioItems(int oldIndex, int newIndex)
{
	mpAudioList->swap(oldIndex, newIndex);

  // Same reasoning as onRemoveAudioItem: a repair is tagged by track index,
  // so swapping two tracks must swap the indices any repair on those two
  // tracks carries - otherwise a save after a reorder (TTAudioTreeView
  // swapItems, since v0.81.2) attributes the repair to the wrong audio file.
  remapAudioRepairTracks([oldIndex, newIndex](int track) {
    return track == oldIndex ? newIndex : (track == newIndex ? oldIndex : track);
  });
}

void TTAVItem::onAudioLanguageChanged(int index, const QString& language)
{
  TTAudioItem item = mpAudioList->at(index);
  TTAudioItem updated(item);
  updated.setLanguage(language);
  mpAudioList->update(item, updated);
}

void TTAVItem::onAudioDelayChanged(int index, int delayMs)
{
  if (index < 0 || index >= audioCount()) return;
  TTAudioItem item = mpAudioList->at(index);
  TTAudioItem updated(item);
  updated.setDelayMs(delayMs);
  mpAudioList->update(item, updated);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::appendSubtitleEntry(TTSubtitleStream* sStream, int order)
{
	mpSubtitleList->append(this, sStream, order);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::onRemoveSubtitleItem(int index)
{
	mpSubtitleList->remove(mpSubtitleList->at(index));
}

void TTAVItem::onSwapSubtitleItems(int oldIndex, int newIndex)
{
	mpSubtitleList->swap(oldIndex, newIndex);
}

void TTAVItem::onSubtitleLanguageChanged(int index, const QString& language)
{
  TTSubtitleItem item = mpSubtitleList->at(index);
  TTSubtitleItem updated(item);
  updated.setLanguage(language);
  mpSubtitleList->update(item, updated);
}

void TTAVItem::onSubtitleDelayChanged(int index, int delayMs)
{
  if (index < 0 || index >= subtitleCount()) return;
  TTSubtitleItem item = mpSubtitleList->at(index);
  TTSubtitleItem updated(item);
  updated.setDelayMs(delayMs);
  mpSubtitleList->update(item, updated);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::appendCutEntry(int cutIn, int cutOut, int order)
{
	checkCut(cutIn, cutOut);
	mpCutList->append(this, cutIn, cutOut, order);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::removeCutEntry(const TTCutItem& cItem)
{
	mpCutList->remove(cItem);
}

/* ///////////////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::updateCutEntry(const TTCutItem& cItem, int cutIn,	int cutOut)
{
	// Last line of defence for the six gestures that move one end of an
	// existing range (cut-out still, current frame, burst shift, edit branch).
	// None of them stops at the other end, so each can invert the range.
	// Refused rather than thrown: these callers are Qt slots, and an exception
	// leaving one of them ends the application.
	QString reason;
	if (!isValidCut(cutIn, cutOut, &reason)) {
		qWarning("TTAVItem::updateCutEntry -> %d-%d refused: %s",
		         cutIn, cutOut, qPrintable(reason));
		return;
	}

	TTCutItem uItem(this, cutIn, cutOut);
	mpCutList->update(cItem, uItem);
}

/* /////////////////////////////////////////////////////////////////////////////
 *
 */
void TTAVItem::canCutWith(const TTAVItem* avItem, int cutIn, int cutOut)
{
	// Whether TWO videos can be cut into one output. The caller walks the
	// whole AV list, so with a single video loaded this item is also the one
	// being checked - nothing to compare then, and comparing it would pit two
	// positions of the same file against each other, which a changing aspect
	// ratio inside one recording makes fail.
	if (avItem == this) return;

	TTVideoStream*    video1  = videoStream();
	TTVideoStream*    video2  = avItem->videoStream();

	if (video1->frameRate() != video2->frameRate())
		throw TTInvalidOperationException(tr("Video files to cut must have the same framerate!"));

	if (audioCount() != avItem->audioCount())
		throw TTInvalidOperationException(tr("Video files to cut must have the same count of audio files!"));

	// Stream type compatibility check
	TTAVTypes::AVStreamType type1 = video1->streamType();
	TTAVTypes::AVStreamType type2 = video2->streamType();

	if (type1 != type2)
		throw TTInvalidOperationException(tr("Video files to cut must have the same codec type!"));

	// MPEG-2 specific checks using sequence headers
	if (type1 == TTAVTypes::mpeg2_demuxed_video) {
		TTSequenceHeader* seqIn2  = video2->getSequenceHeader(cutIn);
		TTSequenceHeader* seqOut2 = video2->getSequenceHeader(cutOut);
		if (seqIn2 == 0 || seqOut2 == 0) return;

		// Every range already in this item is compared against the new one.
		// cutIn/cutOut are positions in video2 and say nothing about video1,
		// so this side reads its headers at its own entries' positions.
		for (int i = 0; i < cutCount(); i++) {
			const TTCutItem&  own      = cutListItemAt(i);
			TTSequenceHeader* seqIn1   = video1->getSequenceHeader(own.cutInIndex());
			TTSequenceHeader* seqOut1  = video1->getSequenceHeader(own.cutOutIndex());
			if (seqIn1 == 0 || seqOut1 == 0) continue;

			if (seqIn1->aspectRatio() != seqIn2->aspectRatio() || seqOut1->aspectRatio() != seqOut2->aspectRatio())
				throw TTInvalidOperationException(tr("Video files to cut must have the same aspect ratio!"));

			if (seqIn1->horizontalSize() != seqIn2->horizontalSize() || seqOut1->horizontalSize() != seqOut2->horizontalSize())
				throw TTInvalidOperationException(tr("Video files to cut must have the same horizontal size!"));

			if (seqIn1->verticalSize() != seqIn2->verticalSize() || seqOut1->verticalSize() != seqOut2->verticalSize())
				throw TTInvalidOperationException(tr("Video files to cut must have the same vertical size!"));
		}
	}
	// H.264/H.265 streams: basic checks are done via frameRate above
	// More detailed checks (resolution, profile) could be added via SPS comparison if needed

	for (int i = 0; i < audioCount(); i++) {
		const TTAudioItem& audio1 = audioListItemAt(i);
		const TTAudioItem& audio2 = avItem->audioListItemAt(i);

		if (audio1.getBitrate()    != audio2.getBitrate())
			throw TTInvalidOperationException(tr("Audio files to cut must have the same bitrate!"));

		if (audio1.getSamplerate() != audio2.getSamplerate())
			throw TTInvalidOperationException(tr("Audio files to cut must have the same samplerate!"));

		if (audio1.getVersion() != audio2.getVersion())
			throw TTInvalidOperationException(tr("Audio files to cut must have the same version!"));

		//if (audio1.getMode()       != audio2.getMode())
		//	throw TTInvalidOperationException(tr("Audio files to cut must have the same mode!"));
	}
}

int TTAVItem::firstAc3TrackIndex() const
{
  for (int i = 0; i < audioCount(); ++i) {
    const TTAudioStream* candidate = audioStreamAt(i);
    if (candidate && candidate->streamType() == TTAVTypes::ac3_audio) return i;
  }
  return -1;
}

/* /////////////////////////////////////////////////////////////////////////////
 * checkCut
 */
bool TTAVItem::isValidCut(int cutIn, int cutOut, QString* reason)
{
  // The first two tests are stream-independent and therefore always possible -
  // including while the project loader is still building this item, where
  // videoStream() is still 0 because the open task has not run yet.
  if (cutIn < 0 || cutOut < 0) {
    if (reason) *reason = tr("A cut range must not contain a negative frame position (%1-%2)!")
        .arg(cutIn).arg(cutOut);
    return false;
  }

  if (cutOut < cutIn) {
    if (reason) *reason = tr("Cut out must not lie before cut in (%1-%2)!").arg(cutIn).arg(cutOut);
    return false;
  }

  // The upper bound needs an open stream, so it applies to ranges set while
  // the video is loaded, not to those read from a project file.
  if (videoStream() != 0 && cutOut >= videoStream()->frameCount()) {
    if (reason) *reason = tr("Cut out %1 exceeds the video frame count of %2!")
        .arg(cutOut).arg(videoStream()->frameCount());
    return false;
  }

  return true;
}

void TTAVItem::checkCut(int cutIn, int cutOut)
{
  QString reason;
  if (!isValidCut(cutIn, cutOut, &reason))
    throw TTInvalidOperationException(reason);
}

/*!
 * Marker
 */
void TTAVItem::appendMarker(int markerPos, int order)
{
	mpMarkerList->append(this, markerPos, order);
}


/* /////////////////////////////////////////////////////////////////////////////
 * TTAVDataList
 */

/*!
 * TTAVList
 */
TTAVList::TTAVList()
{
}

/*!
 * ~TTAVList
 */
TTAVList::~TTAVList()
{
	clear();
}

/*!
 * append
 */
void TTAVList::append(TTAVItem* item)
{
	if (item == nullptr) {
		return;
	}

	item->mIsInList = true;
	mpAVList.append(item);
	emit itemAppended(*item);
}

/*!
 * at
 */
TTAVItem* TTAVList::at(int i)
{
	return mpAVList.at(i);
}

/*!
 * clear
 */
void TTAVList::clear()
{
	while (mpAVList.count() > 0) {
		TTAVItem* item = mpAVList.takeLast();
		delete item;
		emit itemRemoved(mpAVList.count());
	}
	//removeAt(mpAVList.indexOf(mpAVList.last()));
}

/*!
 * count
 */
int TTAVList::count()
{
	return mpAVList.count();
}

/*!
 * removeAt
 */
void TTAVList::removeAt(int i)
{
	TTAVItem* item = mpAVList.takeAt(i);

	delete item;

	emit itemRemoved(i);
}

/*!
 * swap
 */
void TTAVList::swap(int a, int b)
{
	if ((a < 0 || b < 0) || (a >= count() || b >= count()))
		return; //TODO: throw an index out of bound exception

	mpAVList.swapItemsAt(a, b);

  emit itemsSwapped(a, b);
}


