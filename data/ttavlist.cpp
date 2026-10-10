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
#include "../avstream/ttac3audioheader.h"
#include "../avstream/ttmpegaudioheader.h"
#include "../avstream/ttesinfo.h"
#include "../common/ttmessagelogger.h"
#include "../extern/ttdonorfill.h"

#include <QFileInfo>
#include <QTime>

#include <QList>
#include <QDir>
#include <QDebug>

namespace {

// What a donor fill can read: two channels at 48 kHz, MPEG layer II or AC3
// 2.0. An MP2 track in dual-channel mode counts, deliberately (spec
// 2026-10-10).
bool isDonorCandidate(TTAudioStream* stream)
{
  TTAudioHeader* header = stream ? stream->headerAt(0) : nullptr;
  if (!header || header->sampleRate() != 48000) return false;
  if (const TTAC3AudioHeader* ac3 = dynamic_cast<const TTAC3AudioHeader*>(header))
    return AC3AudioCodingMode[ac3->acmod & 7] == 2 && !ac3->lfeon;
  if (const TTMpegAudioHeader* mpeg = dynamic_cast<const TTMpegAudioHeader*>(header))
    return mpeg->layer == 2 && mpeg->mode != 3;       // header bits: layer 2 = II, mode 3 = one channel
  return false;
}

// Samples the track holds, to one frame: from the start time of its last frame.
qint64 trackSamples(TTAudioStream* stream)
{
  const qint64 perFrame = stream->streamType() == TTAVTypes::ac3_audio ? 1536 : 1152;
  return qint64(QTime(0, 0).msecsTo(stream->streamLengthTime())) * 48 + 2 * perFrame;
}

} // namespace

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
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor
 */
TTAVItem::~TTAVItem()
{
  if (mpAudioList    != 0) delete mpAudioList;
  if (mpSubtitleList != 0) delete mpSubtitleList;
  if (mpCutList      != 0) delete mpCutList;
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
  QList<TTAudioRepairItem> updatedRepairs;
  for (const TTAudioRepairItem& repair : mAudioRepairs) {
    const int track = newTrack(repair.trackIndex());
    if (track < 0) continue;
    TTAudioRepairItem moved = repair;     // keeps method, values and the enabled flag
    moved.setTrackIndex(track);
    if (moved.isDonorFill() && moved.donorTrack() >= 0 && !moved.donorIsSavedOrder()) {
      const int donor = newTrack(moved.donorTrack());
      moved.setDonorTrack(donor);
      if (donor < 0) moved.setEnabled(false);   // the donor is gone: keep the repair, do not apply it
    }
    updatedRepairs.append(moved);
  }
  mAudioRepairs = updatedRepairs;
  emit audioRepairsChanged();
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

	checkStreamCompat(avItem);

	// MPEG-2 specific checks using sequence headers. H.264/H.265 streams:
	// basic checks are done via frameRate above. More detailed checks
	// (resolution, profile) could be added via SPS comparison if needed.
	if (videoStream()->streamType() == TTAVTypes::mpeg2_demuxed_video &&
	    !checkMpeg2SequenceCompat(avItem, cutIn, cutOut))
		return;

	checkAudioCompat(avItem);
}

void TTAVItem::checkStreamCompat(const TTAVItem* avItem) const
{
	TTVideoStream*    video1  = videoStream();
	TTVideoStream*    video2  = avItem->videoStream();

	if (video1->frameRate() != video2->frameRate())
		throw TTInvalidOperationException(tr("Video files to cut must have the same framerate!"));

	if (audioCount() != avItem->audioCount())
		throw TTInvalidOperationException(tr("Video files to cut must have the same count of audio files!"));

	if (video1->streamType() != video2->streamType())
		throw TTInvalidOperationException(tr("Video files to cut must have the same codec type!"));
}

bool TTAVItem::checkMpeg2SequenceCompat(const TTAVItem* avItem, int cutIn, int cutOut) const
{
	TTVideoStream*    video1  = videoStream();
	TTVideoStream*    video2  = avItem->videoStream();

	TTSequenceHeader* seqIn2  = video2->getSequenceHeader(cutIn);
	TTSequenceHeader* seqOut2 = video2->getSequenceHeader(cutOut);
	if (seqIn2 == 0 || seqOut2 == 0) return false;

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
	return true;
}

void TTAVItem::checkAudioCompat(const TTAVItem* avItem) const
{
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

QList<int> TTAVItem::donorCandidateTracks(int repairedTrack) const
{
  QList<int> tracks;
  if (repairedTrack < 0 || repairedTrack >= audioCount()) return tracks;
  TTAudioStream* repaired = audioStreamAt(repairedTrack);
  TTAudioHeader* header = repaired ? repaired->headerAt(0) : nullptr;
  if (!header || header->sampleRate() != 48000) return tracks;
  for (int i = 0; i < audioCount(); ++i)
    if (i != repairedTrack && isDonorCandidate(audioStreamAt(i))) tracks.append(i);
  return tracks;
}

int TTAVItem::presetDonorTrack(int repairedTrack) const
{
  const QList<int> candidates = donorCandidateTracks(repairedTrack);
  if (candidates.isEmpty()) return -1;
  const QString language = audioListItemAt(repairedTrack).getLanguage();
  if (!language.isEmpty())
    for (int track : candidates)
      if (audioListItemAt(track).getLanguage() == language) return track;
  return candidates.first();
}

qint64 TTAVItem::expectedDonorShift(int repairedTrack, int donorTrack) const
{
  if (!mpVideoStream || repairedTrack < 0 || repairedTrack >= audioCount()
      || donorTrack < 0 || donorTrack >= audioCount()) return 0;
  const TTESInfoTiming timing = TTESInfo::timingForVideo(mpVideoStream->filePath());
  auto offsetMs = [&](int track) {
    return timing.trackStartOffsetMs.value(QFileInfo(audioStreamAt(track)->filePath()).fileName(), 0);
  };
  // A positive offset: the track belongs later than its file plays it. A
  // donor that belongs later carries the same sound earlier in its file.
  return qint64(offsetMs(repairedTrack) - offsetMs(donorTrack)) * 48;
}

int TTAVItem::resolveLoadedDonorFills()
{
  int disabled = 0;
  for (TTAudioRepairItem& repair : mAudioRepairs) {
    if (!repair.isDonorFill() || !repair.donorIsSavedOrder()) continue;
    repair.setDonorIsSavedOrder(false);

    // The saved position is the <Order> the donor's track was loaded with.
    const int savedOrder = repair.donorTrack();
    int donor = -1;
    for (int i = 0; i < audioCount() && donor < 0; ++i)
      if (audioListItemAt(i).order() == savedOrder) donor = i;
    repair.setDonorTrack(donor);

    QString reason;
    if (donor < 0) {
      reason = QString("its donor track (saved at position %1) is not among the loaded tracks").arg(savedOrder + 1);
    } else if (!donorCandidateTracks(repair.trackIndex()).contains(donor)) {
      reason = QString("its donor track %1 is not a two-channel 48 kHz track of this video").arg(donor + 1);
    } else if (repair.isEnabled()) {      // its values passed the load validation
      const int crossFade = TTDonorFill::crossFadeSamples(48000);
      const qint64 first = repair.holeStart() - crossFade + repair.donorShift();
      const qint64 last  = repair.holeEnd() + crossFade + repair.donorShift();
      if (first < 0 || last > trackSamples(audioStreamAt(donor)))
        reason = QString("its values point outside the donor track %1").arg(donor + 1);
    }
    if (reason.isEmpty() || !repair.isEnabled()) continue;
    repair.setEnabled(false);
    ++disabled;
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Audio track %1: donor-fill repair %2-%3 is disabled: %4")
            .arg(repair.trackIndex() + 1).arg(repair.frameFrom()).arg(repair.frameTo()).arg(reason));
  }
  return disabled;
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


