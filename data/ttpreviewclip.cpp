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
// TTPREVIEWCLIP
// ----------------------------------------------------------------------------

#include "ttpreviewclip.h"

#include "../avstream/ttavstream.h"
#include "../avstream/ttesinfo.h"
#include "../avstream/tth26xvideostream.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../common/ttthreadtaskpool.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttcutlist.h"
#include "../data/ttcutpreviewtask.h"
#include "../data/ttcutvideotask.h"
#include "../extern/ttessmartcut.h"
#include "../extern/ttmkvmergeprovider.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

/**
 * Everything the clip production reads off cut entry 0
 */
TTPreviewSource ttResolvePreviewSource(TTCutList* clipCutList)
{
  TTPreviewSource src;

  if (clipCutList == nullptr || clipCutList->count() == 0) return src;

  TTAVItem* avItem = clipCutList->at(0).avDataItem();
  if (avItem == nullptr) return src;

  src.avItem  = avItem;
  src.vStream = avItem->videoStream();
  if (src.vStream == nullptr) return src;

  src.sourceFile = src.vStream->filePath();
  src.suffix     = QFileInfo(src.sourceFile).suffix().toLower();
  src.frameRate  = src.vStream->frameRate();

  // The stream's frame rate is authoritative - it is already PAFF-corrected.
  // Only a stream that reports none falls back to the .info file.
  const TTESInfoTiming info = TTESInfo::timingForVideo(src.sourceFile);
  if (src.frameRate <= 0 && info.frameRate > 0) {
    src.frameRate         = info.frameRate;
    src.frameRateFromInfo = true;
  }
  src.avOffsetMs = info.avOffsetMs;

  src.hasAudio = (avItem->audioCount() > 0);
  if (src.hasAudio) {
    if (TTAudioStream* aStream = avItem->audioStreamAt(0))
      src.audioFile = aStream->filePath();
    else
      src.hasAudio = false;
  }

  return src;
}

/**
 * Delete every preview* file in the temp directory
 */
int ttRemovePreviewFiles()
{
  QDir tempDir(TTSettings::instance()->tempDirPath());
  QStringList filters;
  filters << "preview*";

  const QFileInfoList previewFiles = tempDir.entryInfoList(filters, QDir::Files);
  for (const QFileInfo& fi : previewFiles) {
    QFile::remove(fi.absoluteFilePath());
  }

  return previewFiles.count();
}

/**
 * Collect the cut entries clip iClip is built from
 */
void ttBuildClipCutList(TTCutList* previewCutList, int iClip, TTCutList* out)
{
  if (previewCutList == nullptr || out == nullptr) return;

  const int numPreview = previewCutList->count() / 2 + 1;
  if (iClip < 0 || iClip >= numPreview) return;

  // First cut-in
  if (iClip == 0) {
    TTCutItem item = previewCutList->at(0);
    out->append(item.avDataItem(), item.cutInIndex(), item.cutOutIndex());
    return;
  }

  // Every clip past the first is anchored on the cut-out entry of its
  // transition, which is the odd entry of the preceding cut.
  const int iPos = (iClip - 1) * 2 + 1;
  if (iPos >= previewCutList->count()) return;

  // Last cut-out: no following segment to show
  if (iClip == numPreview - 1) {
    TTCutItem item = previewCutList->at(iPos);
    out->append(item.avDataItem(), item.cutInIndex(), item.cutOutIndex());
    return;
  }

  // Transition: cut-out of segment X plus cut-in of segment X+1
  TTCutItem item1 = previewCutList->at(iPos);
  TTCutItem item2 = previewCutList->at(iPos + 1);
  out->append(item1.avDataItem(), item1.cutInIndex(), item1.cutOutIndex());
  out->append(item2.avDataItem(), item2.cutInIndex(), item2.cutOutIndex());
}

/**
 * Preview preset and display-order map for the Smart Cut engine
 */
int ttApplyPreviewEncoderSettings(TTESSmartCut& smartCut, const TTPreviewSource& src)
{
  smartCut.setPresetOverride(TTSettings::instance()->previewPreset());

  const auto* h26x = dynamic_cast<const TTH26xVideoStream*>(src.vStream);
  if (h26x == nullptr) return -1;

  smartCut.setDisplayOrderMap(h26x->displayOrderMap());
  return h26x->displayOrderMap().count();
}

/**
 * Muxer configuration for a preview clip
 */
void ttConfigurePreviewMux(TTMkvMergeProvider& mux, const TTPreviewSource& src,
                           const QVector<int>& displayOrder)
{
  // A stream without a frame rate would turn the default duration into a
  // division by zero. Leaving it unset keeps the muxer on its own default
  // instead; on any stream that reports a rate this branch is not reached.
  if (src.frameRate > 0) {
    const int frameDurationNs = static_cast<int>(1000000000.0 / src.frameRate);
    mux.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
  }

  mux.setIsPAFF(src.vStream->isPAFF(), src.vStream->paffLog2MaxFrameNum());
  mux.setVideoCodecId(TTMkvMergeProvider::videoCodecIdFor(src.vStream->streamType()));

  // Display-PTS: Smart Cut supplies the output order; empty means linear PTS.
  mux.setVideoDisplayOrder(displayOrder);

  if (src.avOffsetMs != 0) mux.setAudioSyncOffset(src.avOffsetMs);
}

namespace {

void report(const TTPreviewProgressFn& progress, TTPreviewStage stage)
{
  if (progress) progress(stage);
}

} // namespace

/**
 * Rebuild one MPEG-2 preview clip
 */
bool ttRebuildMpeg2PreviewClip(TTAVData* avData, TTCutList* clipCutList, int fileIndex,
                               const TTPreviewProgressFn& progress)
{
  if (avData == nullptr) return false;

  const TTPreviewSource src = ttResolvePreviewSource(clipCutList);
  if (!src.isValid()) return false;

  report(progress, TTPreviewStage::CutVideo);

  // --- Cut video ---
  const QString videoFile = TTCutPreviewTask::createPreviewFileName(fileIndex, "m2v");
  TTCutVideoTask cutVideoTask(avData);
  cutVideoTask.init(videoFile, clipCutList);
  avData->threadTaskPool()->start(&cutVideoTask, true);

  // --- Cut audio ---
  QStringList cutAudioFiles;
  if (src.hasAudio) {
    report(progress, TTPreviewStage::CutAudio);

    // Cut the first audio track for preview (consolidated onto cutAudioTracks).
    auto videoKeepList = avData->buildVideoKeepList(clipCutList, src.vStream->frameRate());
    const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
    avData->cutAudioTracks(src.avItem, {0}, videoKeepList, normalizeAcmod,
        [&](int, const QString& ext) {
          return TTCutPreviewTask::createPreviewFileName(fileIndex, ext);
        },
        [&](int, const QString& path, const QString&, bool ok) {
          if (ok) cutAudioFiles.append(path);
        });
  }

  report(progress, TTPreviewStage::Mux);

  // --- Mux to MKV ---
  const QString outputFile = TTCutPreviewTask::createPreviewFileName(fileIndex, "mkv");
  if (src.hasAudio && !cutAudioFiles.isEmpty()) {
    TTMkvMergeProvider mkvProv;
    ttConfigurePreviewMux(mkvProv, src);
    mkvProv.mux(outputFile, videoFile, cutAudioFiles, QStringList());
  } else {
    // No audio - just rename the video file to the output
    QFile::rename(videoFile, outputFile);
  }

  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "Rebuild MPEG-2 preview (MKV):" << outputFile;

  return true;
}

/**
 * Rebuild one H.264/H.265 preview clip
 */
bool ttRebuildSmartCutPreviewClip(TTAVData* avData, TTCutList* clipCutList, int fileIndex,
                                  const TTPreviewProgressFn& progress)
{
  if (avData == nullptr) return false;

  const TTPreviewSource src = ttResolvePreviewSource(clipCutList);
  if (!src.isValid()) return false;

  const double frameRate = src.frameRate;

  report(progress, TTPreviewStage::SmartCutVideo);

  // --- Smart Cut video ---
  TTESSmartCut smartCut;
  if (!smartCut.initialize(src.sourceFile, frameRate)) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Rebuild: Smart Cut init failed: %1").arg(smartCut.lastError()));
    return false;
  }

  const int mapEntries = ttApplyPreviewEncoderSettings(smartCut, src);
  if (mapEntries >= 0 && TTSettings::instance()->logCutPipeline())
      qDebug() << "Rebuild: Injected display-order map (" << mapEntries << "entries)";

  const QList<QPair<int, int>> cutFrames = clipCutList->frameRanges();

  const QString tempVideoFile = QString("%1/preview_video_temp.%2")
      .arg(TTSettings::instance()->tempDirPath()).arg(src.suffix);

  if (!smartCut.smartCutFrames(tempVideoFile, cutFrames)) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Rebuild: Smart Cut failed: %1").arg(smartCut.lastError()));
    return false;
  }

  report(progress, TTPreviewStage::CutAudio);

  // --- Cut audio ---
  //
  // Through cutAudioTracks, the same spine every final cut uses: extra-frame
  // correction in the keep list, per-track delay, audio-frame snapping with
  // feed-forward drift, AC3 acmod normalization. Until 2026-09-22 this path
  // open-coded a raw keep list and the three-argument TTAudioCutter::cut, and
  // a rebuilt clip could therefore sound different from the one it replaced -
  // measured on an AC3 that switches channel mode: the rebuild carried the
  // switch into the clip, the preview task's clip did not.
  QStringList cutAudioFiles;
  if (src.hasAudio) {
    const auto videoKeepList = avData->buildVideoKeepList(clipCutList, frameRate);

    avData->cutAudioTracks(src.avItem, {0}, videoKeepList,
        TTSettings::instance()->normalizeAcmod(),
        [&](int, const QString& ext) {
          return QString("%1/preview_audio_temp.%2")
              .arg(TTSettings::instance()->tempDirPath()).arg(ext);
        },
        [&](int, const QString& path, const QString&, bool ok) {
          if (ok) cutAudioFiles.append(path);
        });
  }

  report(progress, TTPreviewStage::Mux);

  // --- Mux to MKV ---
  const QString outputFile = TTCutPreviewTask::createPreviewFileName(fileIndex, "mkv");

  TTMkvMergeProvider mkvProvider;
  ttConfigurePreviewMux(mkvProvider, src, smartCut.outputDisplayOrder());
  mkvProvider.mux(outputFile, tempVideoFile, cutAudioFiles, QStringList());

  // Clean up temp files
  QFile::remove(tempVideoFile);
  for (const QString& f : cutAudioFiles) {
    QFile::remove(f);
  }

  return true;
}
