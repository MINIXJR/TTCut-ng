/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcutpreviewtask.cpp                                            */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 01/24/2009 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTPREVIEWTASK
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

#include "ttcutpreviewtask.h"

#include <QFileInfo>

#include "../common/ttexception.h"
#include "../common/ttthreadtaskpool.h"
#include "../common/istatusreporter.h"
#include "../avstream/ttfilebuffer.h"
#include "../avstream/ttavtypes.h"
#include "../data/ttcutparameter.h"
#include "../data/ttcutlist.h"
#include "../data/ttavdata.h"
#include "../avstream/ttavstream.h"
#include "../data/ttcutvideotask.h"
#include "../data/ttcutsubtitletask.h"
#include "../data/ttmuxlistdata.h"
#include "../extern/ttmkvmergeprovider.h"

extern "C" {
#include <libavcodec/codec_id.h>
}
#include "../extern/ttessmartcut.h"
#include "../extern/ttffmpegwrapper.h"
#include "../avstream/ttesinfo.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>

/**
 * Create cut preview clips task
 */
TTCutPreviewTask::TTCutPreviewTask(TTAVData* avData, TTCutList* cutList) :
                  TTThreadTask("CutPreviewTask")
{
	mpAVData        = avData;
	mpCutList       = cutList;
 	cutVideoTask    = new TTCutVideoTask(mpAVData);
	cutSubtitleTask = new TTCutSubtitleTask();
}

/**
 * Operation abort requested
 */
void TTCutPreviewTask::onUserAbort()
{
	//if (cutVideoTask != 0) cutVideoTask->onUserAbort();
	//if (cutAudioTask != 0) cutAudioTask->onUserAbort();
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
  QDir tempDir(TTCut::tempDirPath);
  QStringList filters;
  filters << "preview*";
  QFileInfoList oldPreviewFiles = tempDir.entryInfoList(filters, QDir::Files);
  for (const QFileInfo& fi : oldPreviewFiles) {
    QFile::remove(fi.absoluteFilePath());
  }
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
		if (!sharedSmartCut->initialize(vStream->filePath(), frameRate)) {
			qDebug() << "Preview: Shared Smart Cut init failed:" << sharedSmartCut->lastError();
			delete sharedSmartCut;
			sharedSmartCut = nullptr;
		} else {
			sharedSmartCut->setPresetOverride(TTCut::previewPreset);
			qDebug() << "Preview: Shared Smart Cut initialized in" << initTimer.elapsed() << "ms"
			         << "(ES parsed once for all clips, preview preset:" << TTCut::previewPreset << ")";
		}
	}

	QElapsedTimer totalTimer;
	totalTimer.start();

	onStatusReport(this, StatusReportArgs::Start, tr("create cut preview clips"), numPreview);

  for (int i = 0; i < numPreview; i++) {
    if (isAborted())
  		throw TTAbortException(__FILE__, __LINE__, "Task gets abort signal!");

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
      createH264PreviewClip(tmpCutList, outputFile, sharedSmartCut);
      hasAudio = (tmpCutList->at(0).avDataItem()->audioCount() > 0);
    } else {
      // For MPEG-2: use traditional cutting workflow
      try
      {
        QString videoExt = "m2v";
        cutVideoTask->init(createPreviewFileName(i + 1, videoExt), tmpCutList);
        mpAVData->threadTaskPool()->start(cutVideoTask, true);

        if (tmpCutList->at(0).avDataItem()->audioCount() > 0) {
          hasAudio = true;
          // Use FFmpegWrapper for audio cutting (enables AC3 acmod normalization)
          TTAudioStream* aStream = tmpCutList->at(0).avDataItem()->audioStreamAt(0);
          TTVideoStream* vs = tmpCutList->at(0).avDataItem()->videoStream();
          double fps = vs->frameRate();
          int audioDelayMs = tmpCutList->at(0).avDataItem()->audioListItemAt(0).getDelayMs();

          // Build video-domain keep list (extra-frame-corrected, no delay yet)
          QList<QPair<double, double>> videoKeepList;
          for (int c = 0; c < tmpCutList->count(); c++) {
            TTCutItem ci = tmpCutList->at(c);
            int extraIn  = mpAVData->countExtraFramesBefore(ci.cutInIndex());
            int extraOut = mpAVData->countExtraFramesBefore(ci.cutOutIndex() + 1);
            double cutIn  = (ci.cutInIndex()      - extraIn)  / fps;
            double cutOut = (ci.cutOutIndex() + 1 - extraOut) / fps;
            videoKeepList.append(qMakePair(cutIn, cutOut));
          }
          TTAVData::AudioCutPlan plan = mpAVData->planAudioCut(aStream, videoKeepList, audioDelayMs);
          QList<QPair<double, double>> audioKeepList = plan.keepList;
          QString audioExt = QFileInfo(aStream->filePath()).suffix();
          QString cutAudioFile = createPreviewFileName(i + 1, audioExt);

          QList<int> targetAcmods;
          if (TTCut::normalizeAcmod && audioExt.toLower() == "ac3") {
            for (int s = 0; s < audioKeepList.size(); s++) {
              TTFFmpegWrapper::AcmodInfo aInfo = TTFFmpegWrapper::analyzeAcmod(
                  aStream->filePath(), audioKeepList[s].first, audioKeepList[s].second);
              targetAcmods.append(aInfo.mainAcmod);
            }
          }

          TTFFmpegWrapper ffmpegAudio;
          ffmpegAudio.cutAudioStream(aStream->filePath(), cutAudioFile,
                                      audioKeepList, TTCut::normalizeAcmod, targetAcmods);
        }

        // Cut subtitle stream if available (use first subtitle stream)
        if (tmpCutList->at(0).avDataItem()->subtitleCount() > 0) {
          cutSubtitleTask->init(createPreviewFileName(i + 1, "srt"), tmpCutList, 0, cutVideoTask->muxListItem());
          mpAVData->threadTaskPool()->start(cutSubtitleTask, true);
        }

        // Get A/V sync offset from .info file
        int avOffsetMs = 0;
        TTVideoStream* vStream = tmpCutList->at(0).avDataItem()->videoStream();
        QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
        if (!infoFile.isEmpty()) {
          TTESInfo esInfo(infoFile);
          if (esInfo.isLoaded() && esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
            avOffsetMs = esInfo.avOffsetMs();
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
          qDebug() << "MPEG-2 preview mux (MKV):" << outputFile;
        } else {
          // No audio — just rename video file to output
          QFile::rename(videoFile, outputFile);
          qDebug() << "MPEG-2 preview (no audio):" << outputFile;
        }
      }
      catch (const TTException&)
      {
        qDebug() << "catched exception from cutVideoTask!";
        delete tmpCutList;
        qDebug() << "redirect exception from cutVideoTask...";
        throw;
      }
    }

    onStatusReport(this, StatusReportArgs::Step, tr("Preview clip %1 of %2 created").
        arg(i+1).arg(numPreview), i+1);
    delete tmpCutList;
  }

  delete sharedSmartCut;

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

      QList<QPair<double, double>> videoKeepList;
      for (int i = 0; i < mpCutList->count(); i++) {
        TTCutItem item = mpCutList->at(i);
        int extraIn  = mpAVData->countExtraFramesBefore(item.cutInIndex());
        int extraOut = mpAVData->countExtraFramesBefore(item.cutOutIndex() + 1);
        double cutIn  = (item.cutInIndex()      - extraIn)  / fr;
        double cutOut = (item.cutOutIndex() + 1 - extraOut) / fr;
        videoKeepList.append(qMakePair(cutIn, cutOut));
      }
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
        qDebug() << "Preview: ES frame rate from .info (fallback):" << frameRate << "fps";
      }
      if (esInfo.hasTimingInfo() && esInfo.avOffsetMs() != 0) {
        avOffsetMs = esInfo.avOffsetMs();
        qDebug() << "Preview: A/V sync offset from .info:" << avOffsetMs << "ms";
      }
    }
  }

  qDebug() << "H.264 preview: source=" << sourceFile << "fps=" << frameRate
           << "hasAudio=" << hasAudio;

  // Build frame-based cut list
  QList<QPair<int, int>> cutFrames;
  for (int i = 0; i < cutList->count(); i++) {
    TTCutItem item = cutList->at(i);
    cutFrames.append(qMakePair(item.cutInIndex(), item.cutOutIndex()));
    qDebug() << "  Preview segment" << i+1 << ": frames" << item.cutInIndex() << "->" << item.cutOutIndex();
  }

  // --- Video Smart Cut (use shared instance or create local) ---
  QElapsedTimer clipTimer;
  clipTimer.start();

  qDebug() << "Preview: Using Smart Cut (frame-accurate)";

  TTESSmartCut localSmartCut;
  TTESSmartCut* smartCut = sharedSmartCut;
  if (!smartCut) {
    localSmartCut.setPresetOverride(TTCut::previewPreset);
    if (!localSmartCut.initialize(sourceFile, frameRate)) {
      qDebug() << "Preview Smart Cut init failed:" << localSmartCut.lastError();
      return;
    }
    smartCut = &localSmartCut;
  }

  // Create temporary video output
  QString tempVideoFile = QString("%1/preview_video_temp.%2")
      .arg(TTCut::tempDirPath)
      .arg(suffix);

  // Perform frame-accurate video cut
  if (!smartCut->smartCutFrames(tempVideoFile, cutFrames)) {
    qDebug() << "Preview Smart Cut failed:" << smartCut->lastError();
    return;
  }

  qint64 smartCutMs = clipTimer.elapsed();
  qDebug() << "Preview Smart Cut complete in" << smartCutMs << "ms:"
           << smartCut->framesReencoded() << "re-encoded,"
           << smartCut->framesStreamCopied() << "stream-copied";

  // --- Cut audio (same approach as final cut in doH264Cut) ---
  int frameDurationNs = (int)(1000000000.0 / frameRate);
  QStringList cutAudioFiles;
  if (hasAudio && !audioFile.isEmpty()) {
    // Note: B-frame reorder delay correction was removed.
    // With correctly trimmed audio (ttcut-demux aligns audio start to first display frame),
    // the keepList times frame/fps already match the audio ES positions exactly.

    // Apply per-track audio delay for the first audio track.
    // Preview only uses a single audio track (track 0), so we only need
    // the delay for that track. Multi-track preview is not supported.
    int audioDelayMs = avItem->audioListItemAt(0).getDelayMs();
    double audioDelaySec = audioDelayMs / 1000.0;

    QList<QPair<double, double>> keepList;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      double cutInTime  = item.cutInIndex() / frameRate + audioDelaySec;
      double cutOutTime = (item.cutOutIndex() + 1) / frameRate + audioDelaySec;
      if (cutInTime < 0.0) cutInTime = 0.0;
      if (cutOutTime < 0.0) cutOutTime = 0.0;
      keepList.append(qMakePair(cutInTime, cutOutTime));
    }

    QString cutAudioFile = QString("%1/preview_audio_temp.%2")
        .arg(TTCut::tempDirPath)
        .arg(QFileInfo(audioFile).suffix());

    QElapsedTimer audioTimer;
    audioTimer.start();
    TTFFmpegWrapper ffmpeg;
    if (ffmpeg.cutAudioStream(audioFile, cutAudioFile, keepList)) {
      cutAudioFiles.append(cutAudioFile);
      qDebug() << "Preview audio cut complete in" << audioTimer.elapsed() << "ms:" << cutAudioFile;
    } else {
      qDebug() << "Preview audio cut failed";
    }
  }

  // --- Mux video + audio into MKV (same as final cut) ---
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

  if (avOffsetMs != 0) {
    mkvProvider.setAudioSyncOffset(avOffsetMs);
  }

  // Per-track audio delay is already baked into the cut audio file's keepList times,
  // so we do NOT add it again here via setAudioDelays.

  if (mkvProvider.mux(outputFile, tempVideoFile, cutAudioFiles, QStringList())) {
    qDebug() << "Preview mux complete in" << muxTimer.elapsed() << "ms:" << outputFile;
  } else {
    qDebug() << "Preview mux failed:" << mkvProvider.lastError();
  }

  qDebug() << "Preview clip total time:" << clipTimer.elapsed() << "ms";

  // Clean up temp files (KEEP video for debugging)
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
	previewTime   = previewTime.addSecs(TTCut::cutPreviewSeconds);
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

	 return QFileInfo(QDir(TTCut::tempDirPath), previewFileName).absoluteFilePath();
}
