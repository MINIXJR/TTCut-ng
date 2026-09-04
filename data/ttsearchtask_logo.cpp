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
                                   const TTFrameIndexBundle& preBuiltFrameIndex)
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

  int firstPos = establishStartPosition();
  if (firstPos < 0) return;

  // Establish initial logo state via worker 0 (or MPEG-2 base helper).
  QImage f0 = (mSubWrappers.isEmpty())
                ? decodeFrameAt(firstPos)
                : mSubWrappers[0]->decodeFrame(firstPos);
  float initialScore = mDetector ? mDetector->matchScore(f0) : 0.0f;
  mInitialLogoPresent = (initialScore >= mThreshold);

  int pos = (mDirection > 0)
          ? mIndexList->moveToNextIndexPos(firstPos, 1)
          : mIndexList->moveToPrevIndexPos(firstPos, 1);

  runDirectedSearch(pos, "LogoSearch:", t, [&](const QVector<int>& batch) -> int {
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

    if (mIsAborted) return -1;

    for (int i = 0; i < batch.size(); ++i) {
      if (matches[i]) return batch[i];
    }
    return -1;
  });
}
