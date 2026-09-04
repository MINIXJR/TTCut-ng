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
                                                 const TTFrameIndexBundle& preBuiltFrameIndex)
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

  int firstPos = establishStartPosition();
  if (firstPos < 0) return;

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

  runDirectedSearch(pos, "SceneChangeSearch:", t, [&](const QVector<int>& batch) -> int {
    QVector<HistResult> hists(batch.size());
    for (auto& h : hists) { std::memset(h.hist, 0, sizeof(h.hist)); h.total = 0; }

    parallelMap(batch.size(), [&](int i) {
      if (i < mSubWrappers.size() && mSubWrappers[i]) {
        mSubWrappers[i]->buildHistogram(batch[i], hists[i].hist, hists[i].total);
      } else {
        buildHistogramAt(batch[i], hists[i].hist, hists[i].total);
      }
    });

    if (mIsAborted) return -1;

    // Sequential diff against mPrevHist.
    for (int i = 0; i < batch.size(); ++i) {
      if (hists[i].total <= 0) continue;
      float d = histogramDifference(mPrevHist, hists[i].hist, mPrevTotal, hists[i].total);
      if (d > mThreshold) return batch[i];
      std::memcpy(mPrevHist, hists[i].hist, sizeof(mPrevHist));
      mPrevTotal = hists[i].total;
    }
    return -1;
  });
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
