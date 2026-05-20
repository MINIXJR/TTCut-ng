/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_logo.h"
#include "ttlogodetector.h"

#include "../avstream/ttvideoindexlist.h"
#include "../common/ttsettings.h"
#include "../extern/ttffmpegwrapper.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QImage>

TTLogoSearchTask::TTLogoSearchTask(const QString& videoFilePath,
                                   TTAVTypes::AVStreamType streamType,
                                   TTVideoIndexList* indexList,
                                   TTVideoHeaderList* headerList,
                                   int startPos, int direction, int frameCount,
                                   const TTLogoDetector* detector,
                                   float threshold,
                                   const QList<TTFrameInfo>& preBuiltFrameIndex)
  : TTSearchTask("LogoSearch", videoFilePath, streamType,
                 indexList, headerList, startPos, direction, frameCount,
                 preBuiltFrameIndex),
    mDetector(detector),
    mThreshold(threshold)
{
}

void TTLogoSearchTask::operation()
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

  // Establish initial logo state via worker 0 (or MPEG-2 base helper).
  QImage f0 = (mSubWrappers.isEmpty())
                ? decodeFrameAt(firstPos)
                : mSubWrappers[0]->decodeFrame(firstPos);
  float initialScore = mDetector ? mDetector->matchScore(f0) : 0.0f;
  mInitialLogoPresent = (initialScore >= mThreshold);

  int pos = (mDirection > 0)
          ? mIndexList->moveToNextIndexPos(firstPos, 1)
          : mIndexList->moveToPrevIndexPos(firstPos, 1);

  int checked = 0;
  int foundPos = -1;

  while (pos >= 0 && pos < mFrameCount && !mIsAborted) {
    QVector<int> batch = collectNextBatch(pos);
    if (batch.isEmpty()) break;

    QVector<bool> matches(batch.size(), false);

    parallelMap(batch.size(), [&](int i) {
      QImage frame = (i < mSubWrappers.size() && mSubWrappers[i])
                       ? mSubWrappers[i]->decodeFrame(batch[i])
                       : decodeFrameAt(batch[i]);
      if (!mDetector) return;
      float score = mDetector->matchScore(frame);   // const, thread-safe
      bool present = (score >= mThreshold);
      matches[i] = (present != mInitialLogoPresent);
    });

    if (mIsAborted) break;

    for (int i = 0; i < batch.size(); ++i) {
      if (matches[i]) { foundPos = batch[i]; break; }
    }
    if (foundPos >= 0) break;

    checked += batch.size();
    if (checked % 20 < batch.size()) emit progress(checked);
  }

  qint64 ms = t.elapsed();
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "LogoSearch:" << checked << "I-frames in" << ms << "ms"
               << (checked > 0
                     ? QString("(%1 fps, %2 workers)").arg(1000.0 * checked / ms, 0, 'f', 1).arg(mWorkerCount)
                     : QString());

  emit found(foundPos, mIsAborted);
  teardownWorkers();
}
