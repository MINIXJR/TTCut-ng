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
#include "../data/ttcutaudiotask.h"
#include "../data/ttcutvideotask.h"
#include "../data/ttcutsubtitletask.h"
#include "../data/ttmuxlistdata.h"
#include "../extern/ttmkvmergeprovider.h"
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

/**
 * Create cut preview clips task
 */
TTCutPreviewTask::TTCutPreviewTask(TTAVData* avData, TTCutList* cutList) :
                  TTThreadTask("CutPreviewTask")
{
	mpAVData        = avData;
	mpCutList       = cutList;
 	cutVideoTask    = new TTCutVideoTask(mpAVData);
	cutAudioTask    = new TTCutAudioTask();
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

	// Choose file extensions based on stream type
	// H.264/H.265: use ffmpeg directly to extract to .mkv (cutting not yet implemented in TTCutVideoTask)
	// MPEG-2: use traditional cutting workflow with .m2v and mplex to .mpg
	QString outputExt = isH264H265 ? "mkv" : "mpg";

	// For H.264/H.265: create shared Smart Cut instance (parses ES file once)
	TTESSmartCut* sharedSmartCut = nullptr;
	if (isH264H265) {
		TTVideoStream* vStream = mpCutList->at(0).avDataItem()->videoStream();
		double frameRate = vStream->frameRate();

		// Get frame rate from .info file if available
		QString infoFile = TTESInfo::findInfoFile(vStream->filePath());
		if (!infoFile.isEmpty()) {
			TTESInfo esInfo(infoFile);
			if (esInfo.isLoaded() && esInfo.frameRate() > 0) {
				frameRate = esInfo.frameRate();
			}
		}

		sharedSmartCut = new TTESSmartCut();
		if (!sharedSmartCut->initialize(vStream->filePath(), frameRate)) {
			qDebug() << "Preview: Shared Smart Cut init failed:" << sharedSmartCut->lastError();
			delete sharedSmartCut;
			sharedSmartCut = nullptr;
		} else {
			qDebug() << "Preview: Shared Smart Cut initialized (ES parsed once for all clips)";
		}
	}

	onStatusReport(this, StatusReportArgs::Start, tr("create cut preview clips"), numPreview);

  for (int i = 0; i < numPreview; i++) {
    if (isAborted())
  		throw new TTAbortException(__FILE__, __LINE__, "Task gets abort signal!");

    onStatusReport(this, StatusReportArgs::Step, QString("create preview cut %1 from %2").
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
          cutAudioTask->init(createPreviewFileName(i + 1, "mpa"), tmpCutList, 0, cutVideoTask->muxListItem());
          mpAVData->threadTaskPool()->start(cutAudioTask, true);
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

        // Mux MPEG-2 with mplex
        QString muxCommand;
        QString videoFile = createPreviewFileName(i + 1, videoExt);
        if (hasAudio) {
          QString audioFile = createPreviewFileName(i + 1, "mpa");
          // mplex -O: offset of timestamps (video-audio) in ms
          // av_offset_ms = audio - video, so we negate it for mplex
          if (avOffsetMs != 0) {
            muxCommand = QString("mplex -f 8 -O %1ms -o \"%2\" \"%3\" \"%4\" 2>/dev/null").
                arg(-avOffsetMs).arg(outputFile).arg(videoFile).arg(audioFile);
          } else {
            muxCommand = QString("mplex -f 8 -o \"%1\" \"%2\" \"%3\" 2>/dev/null").
                arg(outputFile).arg(videoFile).arg(audioFile);
          }
        } else {
          muxCommand = QString("mv \"%1\" \"%2\" 2>/dev/null").
              arg(videoFile).arg(outputFile);
        }
        qDebug() << "MPEG-2 preview mux command:" << muxCommand;
        system(qPrintable(muxCommand));
      }
      catch (TTException* ex)
      {
        qDebug() << "catched exception from cutVideoTask!";
        delete tmpCutList;
        qDebug() << "redirect exception from cutVideoTask...";
        throw ex;
      }
    }

    onStatusReport(this, StatusReportArgs::Step, QString("preview cut %1 from %2 created").
        arg(i+1).arg(numPreview), i+1);
    delete tmpCutList;
  }

  delete sharedSmartCut;

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

  // Get frame rate and A/V offset from .info file
  int avOffsetMs = 0;
  QString infoFile = TTESInfo::findInfoFile(sourceFile);
  if (!infoFile.isEmpty()) {
    TTESInfo esInfo(infoFile);
    if (esInfo.isLoaded()) {
      if (esInfo.frameRate() > 0) {
        frameRate = esInfo.frameRate();
        qDebug() << "Preview: ES frame rate from .info:" << frameRate << "fps";
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
  qDebug() << "Preview: Using Smart Cut (frame-accurate)";

  TTESSmartCut localSmartCut;
  TTESSmartCut* smartCut = sharedSmartCut;
  if (!smartCut) {
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

  qDebug() << "Preview Smart Cut complete:" << smartCut->framesReencoded() << "re-encoded,"
           << smartCut->framesStreamCopied() << "stream-copied";

  // --- Cut audio (same approach as final cut in doH264Cut) ---
  int frameDurationNs = (int)(1000000000.0 / frameRate);
  QStringList cutAudioFiles;
  if (hasAudio && !audioFile.isEmpty()) {
    // Note: B-frame reorder delay correction was removed.
    // With correctly trimmed audio (ttcut-demux aligns audio start to first display frame),
    // the keepList times frame/fps already match the audio ES positions exactly.

    QList<QPair<double, double>> keepList;
    for (int i = 0; i < cutList->count(); i++) {
      TTCutItem item = cutList->at(i);
      double cutInTime = item.cutInIndex() / frameRate;
      double cutOutTime = item.cutOutIndex() / frameRate;
      keepList.append(qMakePair(cutInTime, cutOutTime));
    }

    QString cutAudioFile = QString("%1/preview_audio_temp.%2")
        .arg(TTCut::tempDirPath)
        .arg(QFileInfo(audioFile).suffix());

    TTFFmpegWrapper ffmpeg;
    if (ffmpeg.cutAudioStream(audioFile, cutAudioFile, keepList)) {
      cutAudioFiles.append(cutAudioFile);
      qDebug() << "Preview audio cut complete:" << cutAudioFile;
    } else {
      qDebug() << "Preview audio cut failed";
    }
  }

  // --- Mux video + audio into MKV (same as final cut) ---
  TTMkvMergeProvider mkvProvider;
  mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));

  if (avOffsetMs != 0) {
    mkvProvider.setAudioSyncOffset(avOffsetMs);
  }

  if (mkvProvider.mux(outputFile, tempVideoFile, cutAudioFiles, QStringList())) {
    qDebug() << "Preview mux complete:" << outputFile;
  } else {
    qDebug() << "Preview mux failed:" << mkvProvider.lastError();
  }

  // Clean up temp files
  QFile::remove(tempVideoFile);
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

		// cut should start at an I-frame
		frameType = pVideoStream->frameType(startIndex);
		while (frameType != 1 && startIndex > 0) {
			startIndex--;
			frameType = pVideoStream->frameType(startIndex);
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
