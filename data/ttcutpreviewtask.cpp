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
// TTCUTPREVIEWTASK
// ----------------------------------------------------------------------------

#include "ttcutpreviewtask.h"

#include <QFileInfo>

#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/istatusreporter.h"
#include "../avstream/ttavtypes.h"
#include "../data/ttcutparameter.h"
#include "../data/ttcutlist.h"
#include "../data/ttavdata.h"
#include "../avstream/ttavstream.h"
#include "../data/ttcutvideotask.h"
#include "../data/ttmuxlistdata.h"
#include "../extern/ttmkvmergeprovider.h"

extern "C" {
#include <libavcodec/codec_id.h>
}
#include "../extern/ttessmartcut.h"
#include "../extern/ttffmpegwrapper.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/tth26xvideostream.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QScopeGuard>

/**
 * Create cut preview clips task
 */
TTCutPreviewTask::TTCutPreviewTask(TTAVData* avData, TTCutList* cutList) :
                  TTThreadTask("CutPreviewTask")
{
	mpAVData        = avData;
	mpCutList       = cutList;
 	cutVideoTask    = new TTCutVideoTask(mpAVData);
}

/**
 * Operation abort requested
 *
 * Runs on the GUI thread while operation() runs on the pool. mpActiveSmartCut
 * is worker-owned (the shared Smart Cut instance for H.264/H.265, or a
 * clip-local one); mSmartCutMutex is what makes reading it here safe against
 * the worker creating, reassigning or destroying it concurrently. The
 * invariant that makes this race-free: the worker always clears the pointer
 * under the mutex BEFORE deleting the pointee, never after (see the header).
 * requestAbort() itself is just an atomic store, so calling it once more than
 * strictly necessary (e.g. between clips, on an instance about to be reused
 * for the next one) is harmless.
 */
void TTCutPreviewTask::onUserAbort()
{
  {
    QMutexLocker lock(&mSmartCutMutex);
    if (mpActiveSmartCut) mpActiveSmartCut->requestAbort();
  }
  abort();
}

/**
 * Clean up after operation
 */
void TTCutPreviewTask::cleanUp()
{
}

/**
 * Task operation method
 */
void TTCutPreviewTask::operation()
{
  // Clean up old preview files BEFORE creating new ones
  // This prevents stale files from previous sessions being loaded
  QDir tempDir(TTSettings::instance()->tempDirPath());
  QStringList filters;
  filters << "preview*";
  QFileInfoList oldPreviewFiles = tempDir.entryInfoList(filters, QDir::Files);
  for (const QFileInfo& fi : oldPreviewFiles) {
    QFile::remove(fi.absoluteFilePath());
  }
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Cleaned up" << oldPreviewFiles.count() << "old preview files";

  mpPreviewCutList = createPreviewCutList(mpCutList);

	int  iPos;
	bool hasAudio   = false;
	int  numPreview = mpPreviewCutList->count() / 2 + 1;

	// Detect stream type from first cut item
	TTVideoStream* firstStream = mpCutList->at(0).avDataItem()->videoStream();
	TTAVTypes::AVStreamType streamType = firstStream->streamType();
	bool isH264H265 = (streamType == TTAVTypes::h264_video || streamType == TTAVTypes::h265_video);

	// Always use MKV for preview output (handles all audio formats)
	QString outputExt = "mkv";

	// For H.264/H.265: create shared Smart Cut instance (parses ES file once)
	TTESSmartCut* sharedSmartCut = nullptr;
	if (isH264H265) {
		TTVideoStream* vStream = mpCutList->at(0).avDataItem()->videoStream();
		double frameRate = vStream->frameRate();
		// vStream->frameRate() is authoritative (already PAFF-corrected).
		// Only fall back to .info if stream has no frame rate.
		if (frameRate <= 0) {
			QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
			if (!infoFile.isEmpty()) {
				TTESInfo esInfo(infoFile);
				if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
					frameRate = esInfo.frameRate();
				}
			}
		}

		QElapsedTimer initTimer;
		initTimer.start();
		sharedSmartCut = new TTESSmartCut();
		// Register as the active engine BEFORE initialize(): the ES parse it
		// does can run for seconds on a real recording and polls checkAbort()
		// internally (TTESSmartCut::initialize()), but happens entirely before
		// the clip loop below, so isAborted() has no poll point covering it.
		// Without this, a cancel during the initial parse only took effect
		// after the parse had already run to completion.
		{
			QMutexLocker lock(&mSmartCutMutex);
			mpActiveSmartCut = sharedSmartCut;
		}
		if (!sharedSmartCut->initialize(vStream->filePath(), frameRate)) {
			// A cancel during the ES parse comes back through the same false
			// return as a real parse failure — only the latter is a warning
			// (logging a plain user cancel at warning level would put a
			// failure-looking line in the persistent log for something the
			// user asked for).
			if (sharedSmartCut->wasAborted())
				TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
					"Preview: Shared Smart Cut init aborted by user");
			else
				TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
					QString("Preview: Shared Smart Cut init failed: %1").arg(sharedSmartCut->lastError()));
			// Clear the tracking pointer BEFORE deleting the engine — never
			// after, or onUserAbort() on the GUI thread could dereference a
			// freed object in the (tiny) window between the two.
			{
				QMutexLocker lock(&mSmartCutMutex);
				mpActiveSmartCut = nullptr;
			}
			delete sharedSmartCut;
			sharedSmartCut = nullptr;
		} else {
			const int previewPreset = TTSettings::instance()->previewPreset();
			sharedSmartCut->setPresetOverride(previewPreset);
			// Inject frame-granularity display-order map (PAFF-safe).
			if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(vStream)) {
				sharedSmartCut->setDisplayOrderMap(h26x->displayOrderMap());
				if (TTSettings::instance()->logCutPipeline())
					qDebug() << "Preview shared: Injected display-order map ("
					         << h26x->displayOrderMap().count() << "entries)";
			}
			if (TTSettings::instance()->logCutPipeline())
				qDebug() << "Preview: Shared Smart Cut initialized in" << initTimer.elapsed() << "ms"
				         << "(ES parsed once for all clips, preview preset:" << previewPreset << ")";
		}
	}

	QElapsedTimer totalTimer;
	totalTimer.start();

	onStatusReport(this, StatusReportArgs::Start, tr("create cut preview clips"), numPreview);

  for (int i = 0; i < numPreview; i++) {
    if (isAborted())
      // Message-only constructor deliberately: the (caller, line) overload
      // logs at fatal level on construction (TTException::TTException(caller,
      // line, msg) -> log->fatalMsg(), common/ttexception.cpp:31-37), which
      // would record a deliberate user cancel as the most severe class of
      // log event (same fix as TTH26xCutTask::abortNow(), Task 6).
  		throw TTAbortException("Task gets abort signal!");

    onStatusReport(this, StatusReportArgs::Step, tr("Creating preview clip %1 of %2").
        arg(i+1).arg(numPreview), i+1);

    TTCutList* tmpCutList = new TTCutList();

    // first cut-in
    if (i == 0) {
      iPos = i;
      TTCutItem item = mpPreviewCutList->at(i);
      tmpCutList->append(item.avDataItem(), item.cutInIndex(),	item.cutOutIndex());
    }

    // cut i-i
    if (numPreview > 1 && i > 0 && i < numPreview - 1) {
      iPos = (i - 1) * 2 + 1;
      TTCutItem item1 = mpPreviewCutList->at(iPos);
      TTCutItem item2 = mpPreviewCutList->at(iPos + 1);
      tmpCutList->append(item1.avDataItem(), item1.cutInIndex(),	item1.cutOutIndex());
      tmpCutList->append(item2.avDataItem(), item2.cutInIndex(),	item2.cutOutIndex());
    }

    //last cut out
    if (i == numPreview - 1) {
      iPos = (i - 1) * 2 + 1;
      TTCutItem item = mpPreviewCutList->at(iPos);
      tmpCutList->append(item.avDataItem(), item.cutInIndex(),item.cutOutIndex());
    }

    QString outputFile = createPreviewFileName(i + 1, outputExt);

    if (isH264H265) {
      // For H.264/H.265: use ffmpeg directly to extract preview clips
      // The TTCutVideoTask cutting is not yet implemented for H.264/H.265
      try
      {
        createH264PreviewClip(tmpCutList, outputFile, sharedSmartCut);
      }
      catch (const TTException&)
      {
        // Smart Cut aborted (e.g. un-cuttable damaged stream) or a plain user
        // cancel routed through createH264PreviewClip's own TTAbortException
        // (see there for how the two are told apart before this point). Clean
        // up the per-iteration cut list and the shared Smart Cut engine (both
        // are otherwise deleted at the end of operation(), which we now
        // skip), then re-raise so TTThreadTask::run() reports it as a clean
        // abort.
        delete tmpCutList;
        // Clear the tracking pointer BEFORE deleting the engine — same
        // ordering requirement as the init-failure branch above.
        {
          QMutexLocker lock(&mSmartCutMutex);
          mpActiveSmartCut = nullptr;
        }
        delete sharedSmartCut;
        throw;
      }
      hasAudio = (tmpCutList->at(0).avDataItem()->audioCount() > 0);
    } else {
      // For MPEG-2: use traditional cutting workflow
      try
      {
        QString videoExt = "m2v";
        cutVideoTask->init(createPreviewFileName(i + 1, videoExt), tmpCutList);
        // This operation() runs in a pool thread; startNested() keeps the
        // pool's task queue in its own thread.
        mpAVData->threadTaskPool()->startNested(cutVideoTask);

        TTAVItem* pvItem = tmpCutList->at(0).avDataItem();
        double fps = pvItem->videoStream()->frameRate();
        auto videoKeepList = mpAVData->buildVideoKeepList(tmpCutList, fps);

        if (tmpCutList->at(0).avDataItem()->audioCount() > 0) {
          hasAudio = true;
          // Cut the first audio track for preview (consolidated onto cutAudioTracks).
          //
          // NOT wired to isAborted() here (unlike the H.264/H.265 path below):
          // this MPEG-2 branch's video phase (cutVideoTask, above) already runs
          // via TTThreadTaskPool::startNested(), which is deliberately not
          // enqueued (see its own comment) and therefore never receives the
          // pool's onUserAbort() broadcast — a cancel during MPEG-2 preview
          // video generation is not currently forwarded at all. Adding the
          // abort predicate only here, without also handling an aborted
          // mid-track cutAudioStream (partial file cleanup, bailing out before
          // the mux below still runs on it), would produce a worse outcome
          // than today - a truncated audio track silently muxed into a clip
          // reported as "created". Left as a follow-up covering the whole
          // MPEG-2 preview branch, not attempted piecemeal here (out of this
          // task's scope - see task-10-brief.md, which only covers the
          // H.264/H.265 Smart Cut path).
          const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
          mpAVData->cutAudioTracks(pvItem, {0}, videoKeepList, normalizeAcmod,
              [&](int, const QString& ext) { return createPreviewFileName(i + 1, ext); },
              [&](int, const QString&, const QString&, bool) {});
        }

        // Cut subtitle track 0 for preview (consolidated onto
        // cutSubtitleTracks; the preview dialog picks the file up by its
        // name, and only one --sub-file is supported, so only the first
        // track is cut)
        if (pvItem->subtitleCount() > 0) {
          mpAVData->cutSubtitleTracks(pvItem, {0}, videoKeepList,
              [&](int /*trk*/) { return createPreviewFileName(i + 1, "srt"); },
              [](int, const QString&, const QString&, bool) {});
        }

        // Get A/V sync offset from .info file
        int avOffsetMs = 0;
        TTVideoStream* vStream = tmpCutList->at(0).avDataItem()->videoStream();
        QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
        if (!infoFile.isEmpty()) {
          TTESInfo esInfo(infoFile);
          if (esInfo.isLoaded() && esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
            avOffsetMs = esInfo.avOffsetMs();
            if (TTSettings::instance()->logCutPipeline())
                qDebug() << "MPEG-2 preview: A/V sync offset from .info:" << avOffsetMs << "ms";
          }
        }

        // Mux MPEG-2 video + audio into MKV
        QString videoFile = createPreviewFileName(i + 1, videoExt);
        if (hasAudio) {
          TTAudioStream* aStr = tmpCutList->at(0).avDataItem()->audioStreamAt(0);
          QString audioExt2 = QFileInfo(aStr->filePath()).suffix();
          QString audioFile = createPreviewFileName(i + 1, audioExt2);

          double frameRate2 = tmpCutList->at(0).avDataItem()->videoStream()->frameRate();
          int frameDurationNs = static_cast<int>(1000000000.0 / frameRate2);
          QStringList audioFiles;
          audioFiles.append(audioFile);
          TTMkvMergeProvider mkvProv;
          mkvProv.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
          {
            AVCodecID codecId;
            switch (vStream->streamType()) {
              case TTAVTypes::h265_video: codecId = AV_CODEC_ID_HEVC;       break;
              case TTAVTypes::h264_video: codecId = AV_CODEC_ID_H264;       break;
              default:                    codecId = AV_CODEC_ID_MPEG2VIDEO; break;
            }
            mkvProv.setVideoCodecId(codecId);
          }
          if (avOffsetMs != 0) mkvProv.setAudioSyncOffset(avOffsetMs);
          mkvProv.mux(outputFile, videoFile, audioFiles, QStringList());
          if (TTSettings::instance()->logCutPipeline())
              qDebug() << "MPEG-2 preview mux (MKV):" << outputFile;
        } else {
          // No audio — just rename video file to output
          QFile::rename(videoFile, outputFile);
          if (TTSettings::instance()->logCutPipeline())
              qDebug() << "MPEG-2 preview (no audio):" << outputFile;
        }
      }
      catch (const TTException&)
      {
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("catched exception from cutVideoTask!"));
        delete tmpCutList;
        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "redirect exception from cutVideoTask...";
        throw;
      }
    }

    onStatusReport(this, StatusReportArgs::Step, tr("Preview clip %1 of %2 created").
        arg(i+1).arg(numPreview), i+1);
    delete tmpCutList;
  }

  // Clear the tracking pointer BEFORE deleting the engine — same ordering
  // requirement as the two earlier deletion sites above.
  {
    QMutexLocker lock(&mSmartCutMutex);
    mpActiveSmartCut = nullptr;
  }
  delete sharedSmartCut;

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Preview: Total time for all clips:" << totalTimer.elapsed() << "ms";

  // Report the cumulative A/V drift after each segment as produced by the
  // audio cut planner (audio-frame-aligned with feed-forward compensation).
  // This matches what cutAudioStream actually outputs — no separate model.
  QList<float> audioDrifts;
  if (mpCutList->count() > 0) {
    TTAVItem* driftAvItem = mpCutList->at(0).avDataItem();
    if (driftAvItem && driftAvItem->audioCount() > 0 && driftAvItem->videoStream()) {
      TTAudioStream* firstAudio = driftAvItem->audioStreamAt(0);
      double fr = driftAvItem->videoStream()->frameRate();
      int    delayMs = driftAvItem->audioListItemAt(0).getDelayMs();

      auto videoKeepList = mpAVData->buildVideoKeepList(mpCutList, fr);
      audioDrifts = mpAVData->planAudioCut(firstAudio, videoKeepList, delayMs).drifts;
    }
  }
  emit audioDriftCalculated(audioDrifts);

  onStatusReport(this, StatusReportArgs::Finished, tr("preview cuts done"), 0);
  emit finished(mpPreviewCutList);
}

/**
 * Create H.264/H.265 preview clip using Smart Cut (frame-accurate)
 * Uses TTESSmartCut for elementary stream files
 */
void TTCutPreviewTask::createH264PreviewClip(TTCutList* cutList, const QString& outputFile,
                                              TTESSmartCut* sharedSmartCut)
{
  if (cutList->count() == 0) return;

  // Get source file and frame rate from first cut item
  TTCutItem firstItem = cutList->at(0);
  TTAVItem* avItem = firstItem.avDataItem();
  TTVideoStream* vStream = avItem->videoStream();
  QString sourceFile = vStream->filePath();
  double frameRate = vStream->frameRate();

  // Check for audio stream
  QString audioFile;
  bool hasAudio = (avItem->audioCount() > 0);
  if (hasAudio) {
    audioFile = avItem->audioStreamAt(0)->filePath();
  }

  // Get file extension for output
  QString suffix = QFileInfo(sourceFile).suffix().toLower();

  // Get A/V offset from .info file (frame rate comes from vStream, already PAFF-corrected)
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(sourceFile);
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (frameRate <= 0 && esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "Preview: ES frame rate from .info (fallback):" << frameRate << "fps";
      }
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        avOffsetMs = esInfo.avOffsetMs();
        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "Preview: A/V sync offset from .info:" << avOffsetMs << "ms";
      }
    }
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "H.264 preview: source=" << sourceFile << "fps=" << frameRate
               << "hasAudio=" << hasAudio;

  // Build frame-based cut list
  QList<QPair<int, int>> cutFrames;
  for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    cutFrames.append(qMakePair(item.cutInIndex(), item.cutOutIndex()));
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "  Preview segment" << i+1 << ": frames" << item.cutInIndex() << "->" << item.cutOutIndex();
  }

  // --- Video Smart Cut (use shared instance or create local) ---
  QElapsedTimer clipTimer;
  clipTimer.start();

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Preview: Using Smart Cut (frame-accurate)";

  // Local fallback engine, used only when the shared one failed to initialise
  // (operation() then leaves sharedSmartCut null). KNOWN GAP, pre-existing:
  // this engine is never published to mpActiveSmartCut, so onUserAbort() cannot
  // reach it and its initialize() — a full ES parse — is not cancellable. It is
  // reached for EVERY clip, so a recording damaged enough to fail the shared
  // init produces N uncancellable full-ES parses in a row, with a cancel only
  // landing at the clip-loop top in operation(). Registering it would mean
  // publishing/clearing it under mSmartCutMutex around both the initialize()
  // and the smartCutFrames() call below, the same shape the shared engine uses.
  TTESSmartCut localSmartCut;
  TTESSmartCut* smartCut = sharedSmartCut;
  if (!smartCut) {
    localSmartCut.setPresetOverride(TTSettings::instance()->previewPreset());
    if (!localSmartCut.initialize(sourceFile, frameRate)) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("Preview Smart Cut init failed: %1").arg(localSmartCut.lastError()));
      return;
    }
    // Inject frame-granularity display-order map (PAFF-safe).
    if (auto* h26x = dynamic_cast<TTH26xVideoStream*>(vStream)) {
      localSmartCut.setDisplayOrderMap(h26x->displayOrderMap());
      if (TTSettings::instance()->logCutPipeline())
          qDebug() << "Preview local: Injected display-order map ("
                   << h26x->displayOrderMap().count() << "entries)";
    }
    smartCut = &localSmartCut;
  }

  // Create temporary video output
  QString tempVideoFile = QString("%1/preview_video_temp.%2")
      .arg(TTSettings::instance()->tempDirPath())
      .arg(suffix);

  // Perform frame-accurate video cut. On failure (e.g. a display-order map
  // that cannot be built consistently on a badly damaged recording) abort the
  // WHOLE preview instead of silently returning: the caller looped over every
  // cut segment, and a swallowed failure let it keep spawning decoder/encoder
  // instances per segment and then mux/play non-existent clips — on a large
  // corrupt stream this exhausted threads/memory and crashed the process.
  bool smartCutOk;
  {
    // Register this engine as the active one for the duration of THIS call
    // only, so onUserAbort() (GUI thread) can reach into it - this is what
    // makes a cancel take effect INSIDE a clip instead of only being noticed
    // between clips. qScopeGuard covers both the normal return and the throw
    // below: the guard only clears the tracking pointer, it never deletes the
    // engine (that stays owned by the caller — sharedSmartCut in operation(),
    // or the local stack instance above), so there is no ordering hazard here
    // like the ones in operation() (which DOES delete its engine and has to
    // clear the pointer strictly before that).
    {
      QMutexLocker lock(&mSmartCutMutex);
      mpActiveSmartCut = smartCut;
    }
    auto clearActiveSmartCut = qScopeGuard([this] {
      QMutexLocker lock(&mSmartCutMutex);
      mpActiveSmartCut = nullptr;
    });
    smartCutOk = smartCut->smartCutFrames(tempVideoFile, cutFrames);
  }
  if (!smartCutOk) {
    if (smartCut->wasAborted()) {
      // Plain user cancel: NOT a failure, so mErrorMessage stays empty -
      // onCutPreviewAborted() reads a non-empty mErrorMessage as "tell the
      // user the recording is too damaged", and a cancel must not raise that
      // dialog. Delete what this clip has produced so far (this is the ONLY
      // thing smartCutFrames could have created before the abort landed).
      if (TTSettings::instance()->logCutPipeline())
          qDebug() << "Preview Smart Cut aborted by user";
      QFile::remove(tempVideoFile);
      QFile::remove(outputFile);
      // Message-only constructor: the (caller, line) overload logs at fatal
      // level on construction, which would record a deliberate cancel as the
      // most severe class of log event (see TTH26xCutTask::abortNow()).
      throw TTAbortException("user abort");
    }
    const QString err = smartCut->lastError();
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Preview Smart Cut failed: %1").arg(err));
    // Record the reason so onCutPreviewAborted() can tell this real error
    // apart from a plain user cancel (which leaves mErrorMessage empty) and
    // surface it to the user in a dialog.
    mErrorMessage = tr("The preview could not be created: this recording is "
                       "too damaged for frame-accurate cutting.\n\n%1").arg(err);
    throw TTAbortException(__FILE__, __LINE__,
        QString("Preview not possible — recording too damaged for frame-accurate cutting: %1").arg(err));
  }

  qint64 smartCutMs = clipTimer.elapsed();
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Preview Smart Cut complete in" << smartCutMs << "ms:"
               << smartCut->framesReencoded() << "re-encoded,"
               << smartCut->framesStreamCopied() << "stream-copied";

  // --- Cut audio (same approach as final cut in doH264Cut) ---
  int frameDurationNs = (int)(1000000000.0 / frameRate);
  QStringList cutAudioFiles;

  // Build video-domain keep list (no delay baked in) and let planAudioCut
  // align to audio frame boundaries with feed-forward drift compensation.
  // KNOWN DIVERGENCE: unlike the consolidated paths (buildVideoKeepList),
  // this build applies NO extra-frame correction — deliberately left as-is
  // during the audio-cut consolidation because changing it would alter
  // preview output (see docs/code-map/audio-cut-timing.md, redundancy
  // section, "Option A"). Align only as a deliberate preview-correctness
  // fix with its own verification.
  QList<QPair<double, double>> videoKeepList;
  for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    double cutInTime  = item.cutInIndex() / frameRate;
    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
    videoKeepList.append(qMakePair(cutInTime, cutOutTime));
  }

  if (hasAudio && !audioFile.isEmpty()) {
    // Apply per-track audio delay for the first audio track.
    // Preview only uses a single audio track (track 0), so we only need
    // the delay for that track. Multi-track preview is not supported.
    TTAudioStream* aStream = avItem->audioStreamAt(0);
    int audioDelayMs = avItem->audioListItemAt(0).getDelayMs();

    TTAVData::AudioCutPlan plan = mpAVData->planAudioCut(aStream, videoKeepList, audioDelayMs);
    QList<QPair<double, double>> audioKeepList = plan.keepList;

    QString audioExt = QFileInfo(audioFile).suffix();
    QString cutAudioFile = QString("%1/preview_audio_temp.%2")
        .arg(TTSettings::instance()->tempDirPath())
        .arg(audioExt);

    QList<int> targetAcmods;
    const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
    if (normalizeAcmod && audioExt.toLower() == "ac3") {
      for (int s = 0; s < audioKeepList.size(); s++) {
        TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
            audioFile, audioKeepList[s].first, audioKeepList[s].second);
        targetAcmods.append(aInfo.mainAcmod);
      }
    }

    QElapsedTimer audioTimer;
    audioTimer.start();
    TTFFmpegWrapper ffmpeg;
    // shouldAbort is polled inside cutAudioStream's per-segment packet loop,
    // so a cancel stops it at the next packet instead of only being noticed
    // once the whole track has been copied. isAborted() is TTThreadTask's own
    // flag, read here on the same worker thread that is about to act on it -
    // same cross-thread contract TTCutVideoTask/TTCutTask already rely on
    // (onUserAbort() on the GUI thread only ever sets it, never reads it back).
    if (ffmpeg.cutAudioStream(audioFile, cutAudioFile, audioKeepList,
                              normalizeAcmod, targetAcmods, nullptr,
                              [this] { return isAborted(); })) {
      cutAudioFiles.append(cutAudioFile);
      if (TTSettings::instance()->logCutPipeline())
          qDebug() << "Preview audio cut complete in" << audioTimer.elapsed() << "ms:" << cutAudioFile;
    } else if (isAborted()) {
      // Plain user cancel: cutAudioStream still finalizes the container
      // before returning false, so cutAudioFile is a partial/empty file on
      // disk (same behavior TTAVData::cutAudioTracks's own comment documents
      // for the consolidated path). Stop here rather than falling through to
      // subtitle cut and mux with a truncated clip - same treatment as the
      // video phase above.
      if (TTSettings::instance()->logCutPipeline())
          qDebug() << "Preview audio cut aborted by user";
      QFile::remove(cutAudioFile);
      QFile::remove(tempVideoFile);
      QFile::remove(outputFile);
      throw TTAbortException("user abort");
    } else {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
          QString("Preview audio cut failed"));
    }
  }

  // Cut subtitle track 0 for the preview clip (same uncorrected keep list as
  // the preview audio — see KNOWN DIVERGENCE above). The preview dialog
  // finds the file by name; only one --sub-file is supported, so only the
  // first track is cut.
  if (avItem->subtitleCount() > 0) {
    mpAVData->cutSubtitleTracks(avItem, {0}, videoKeepList,
        [&](int /*trk*/) {
          return QFileInfo(outputFile).absolutePath() + "/"
               + QFileInfo(outputFile).completeBaseName() + ".srt";
        },
        [](int, const QString&, const QString&, bool) {});
  }

  // --- Mux video + audio into MKV (same as final cut) ---
  //
  // Deliberately NOT wired to TTMkvMergeProvider::requestAbort() here, unlike
  // TTH26xCutTask's final-cut mux. A preview clip is at most
  // 2 * cutPreviewSeconds() (default 25s each side) of already-cut ES plus one
  // short audio track - muxing that is on the order of tens of milliseconds,
  // not the many seconds a full-recording final-cut mux can take. Adding a
  // third mutex-guarded cross-thread pointer (next to mpActiveSmartCut) for a
  // phase that finishes before a cancel could realistically land inside it
  // would not make cancellation any more responsive; the outer isAborted()
  // check at the top of operation()'s clip loop already stops the NEXT clip
  // promptly.
  QElapsedTimer muxTimer;
  muxTimer.start();
  TTMkvMergeProvider mkvProvider;
  mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
  mkvProvider.setIsPAFF(vStream->isPAFF(), vStream->paffLog2MaxFrameNum());
  {
    AVCodecID codecId;
    switch (vStream->streamType()) {
      case TTAVTypes::h265_video: codecId = AV_CODEC_ID_HEVC;       break;
      case TTAVTypes::h264_video: codecId = AV_CODEC_ID_H264;       break;
      default:                    codecId = AV_CODEC_ID_MPEG2VIDEO; break;
    }
    mkvProvider.setVideoCodecId(codecId);
  }
  // Display-PTS: SmartCut-supplied output order (empty = legacy linear PTS)
  mkvProvider.setVideoDisplayOrder(smartCut->outputDisplayOrder());

  if (avOffsetMs != 0) {
    mkvProvider.setAudioSyncOffset(avOffsetMs);
  }

  // Per-track audio delay is already baked into the cut audio file's keepList times,
  // so we do NOT add it again here via setAudioDelays.

  if (mkvProvider.mux(outputFile, tempVideoFile, cutAudioFiles, QStringList())) {
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "Preview mux complete in" << muxTimer.elapsed() << "ms:" << outputFile;
  } else {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Preview mux failed: %1").arg(mkvProvider.lastError()));
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Preview clip total time:" << clipTimer.elapsed() << "ms";

  // Clean up temp files (KEEP video for debugging)
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "DEBUG: Keeping temp video file:" << tempVideoFile;
  //QFile::remove(tempVideoFile);
  for (const QString& f : cutAudioFiles) {
    QFile::remove(f);
  }
}

/**
 * Creates the preview cut list
 */
TTCutList* TTCutPreviewTask::createPreviewCutList(TTCutList* cutList)
{
	TTVideoStream* vStream        = cutList->at(0).avDataItem()->videoStream();
	TTCutList*     previewCutList = new TTCutList();
	QTime          previewTime;
	long           previewFrames;

	previewTime.setHMS(0, 0, 0);
	previewTime   = previewTime.addSecs(TTSettings::instance()->cutPreviewSeconds());
	previewFrames = ttTimeToFrames(previewTime, vStream->frameRate()) / 2;

	for (int i = 0; i < cutList->count(); i++) {
		TTCutItem      cutItem      = cutList->at(i);
		TTVideoStream* pVideoStream = cutItem.avDataItem()->videoStream();
		int            startIndex   = cutItem.cutInIndex();
		int            endIndex     = startIndex + previewFrames;

		if (endIndex >= pVideoStream->frameCount())
			endIndex = pVideoStream->frameCount() - 1;

		// cut should end at an I-frame or P-frame
		int frameType = pVideoStream->frameType(endIndex);

		while (frameType == 3 && endIndex < pVideoStream->frameCount() - 1) {
			endIndex++;
			frameType = pVideoStream->frameType(endIndex);
		}

		previewCutList->append(cutItem.avDataItem(), startIndex, endIndex);

		endIndex   = cutItem.cutOutIndex();
		startIndex = (endIndex - previewFrames >= 0) ? endIndex - previewFrames	: 0;

		// Prefer IDR frame for stutter-free preview (non-IDR I-frames cause decoder stall)
		int idrPos = pVideoStream->findIDRBefore(startIndex);
		if (idrPos >= 0) {
			startIndex = idrPos;
		}
		previewCutList->append(cutItem.avDataItem(), startIndex, endIndex);
	}
	return previewCutList;
}

/**
 * Creates the filenames used for preview clips
 */
QString TTCutPreviewTask::createPreviewFileName(int index, QString extension)
{
	 QString   previewFileName;

	 previewFileName = QString("preview_%1.%2").arg(index, 3, 10, QChar('0')).arg(extension);

	 return QFileInfo(QDir(TTSettings::instance()->tempDirPath()), previewFileName).absoluteFilePath();
}
