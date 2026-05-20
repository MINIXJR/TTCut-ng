/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_scenechange.h"

#include "../avstream/ttvideoindexlist.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../extern/ttffmpegwrapper.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
#include <cstring>

TTSceneChangeSearchTask::TTSceneChangeSearchTask(const QString& videoFilePath,
                                                 TTAVTypes::AVStreamType streamType,
                                                 TTVideoIndexList* indexList,
                                                 TTVideoHeaderList* headerList,
                                                 int startPos, int direction, int frameCount,
                                                 float threshold,
                                                 const QList<TTFrameInfo>& preBuiltFrameIndex)
  : TTSearchTask("SceneChangeSearch", videoFilePath, streamType,
                 indexList, headerList, startPos, direction, frameCount,
                 preBuiltFrameIndex),
    mThreshold(threshold)
{
  std::memset(mPrevHist, 0, sizeof(mPrevHist));
}

void TTSceneChangeSearchTask::operation()
{
  QElapsedTimer t; t.start();

  if (!setupWorkers()) {
    emit found(-1, false);
    return;
  }

  int firstPos = (mDirection > 0)
               ? mIndexList->moveToNextIndexPos(mStartPos, 1)
               : mIndexList->moveToPrevIndexPos(mStartPos, 1);
  if (firstPos < 0) {
    emit found(-1, false);
    teardownWorkers();
    return;
  }

  // Build initial histogram via worker 0 (or MPEG-2 base helper).
  bool ok = (mSubWrappers.isEmpty())
              ? buildHistogramAt(firstPos, mPrevHist, mPrevTotal)
              : mSubWrappers[0]->buildHistogram(firstPos, mPrevHist, mPrevTotal);
  if (!ok) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("SceneChangeSearch: failed to build initial histogram at frame %1").arg(firstPos));
    emit found(-1, false);
    teardownWorkers();
    return;
  }
  mHasPrevHist = true;

  int pos = (mDirection > 0)
          ? mIndexList->moveToNextIndexPos(firstPos, 1)
          : mIndexList->moveToPrevIndexPos(firstPos, 1);

  struct HistResult { int hist[256]; int total; };

  int checked = 0;
  int foundPos = -1;

  while (pos >= 0 && pos < mFrameCount && !mIsAborted) {
    QVector<int> batch = collectNextBatch(pos);
    if (batch.isEmpty()) break;

    QVector<HistResult> hists(batch.size());
    for (auto& h : hists) { std::memset(h.hist, 0, sizeof(h.hist)); h.total = 0; }

    parallelMap(batch.size(), [&](int i) {
      if (i < mSubWrappers.size() && mSubWrappers[i]) {
        mSubWrappers[i]->buildHistogram(batch[i], hists[i].hist, hists[i].total);
      } else {
        buildHistogramAt(batch[i], hists[i].hist, hists[i].total);
      }
    });

    if (mIsAborted) break;

    // Sequential diff against mPrevHist.
    for (int i = 0; i < batch.size(); ++i) {
      if (hists[i].total <= 0) continue;
      float d = histogramDifference(mPrevHist, hists[i].hist, mPrevTotal, hists[i].total);
      if (d > mThreshold) { foundPos = batch[i]; break; }
      std::memcpy(mPrevHist, hists[i].hist, sizeof(mPrevHist));
      mPrevTotal = hists[i].total;
    }
    if (foundPos >= 0) break;

    checked += batch.size();
    if (checked % 20 < batch.size()) emit progress(checked);
  }

  qint64 ms = t.elapsed();
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "SceneChangeSearch:" << checked << "I-frames in" << ms << "ms"
               << (checked > 0
                     ? QString("(%1 fps, %2 workers)").arg(1000.0 * checked / ms, 0, 'f', 1).arg(mWorkerCount)
                     : QString());

  emit found(foundPos, mIsAborted);
  teardownWorkers();
}

float TTSceneChangeSearchTask::histogramDifference(const int histA[256], const int histB[256],
                                                   int totalA, int totalB)
{
  if (totalA <= 0 || totalB <= 0) return 0.0f;
  float diff = 0.0f;
  for (int i = 0; i < 256; ++i)
    diff += qAbs((float)histA[i] / totalA - (float)histB[i] / totalB);
  return diff / 2.0f;
}
