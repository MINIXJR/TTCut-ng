/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_blackframe.h"

#include "../avstream/ttvideoindexlist.h"
#include "../common/ttsettings.h"
#include "../extern/ttffmpegwrapper.h"
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QDebug>
#include <QElapsedTimer>

TTBlackFrameSearchTask::TTBlackFrameSearchTask(const QString& videoFilePath,
                                               TTAVTypes::AVStreamType streamType,
                                               TTVideoIndexList* indexList,
                                               TTVideoHeaderList* headerList,
                                               int startPos, int direction, int frameCount,
                                               float ratioThreshold,
                                               const TTFrameIndexBundle& preBuiltFrameIndex)
  : TTSearchTask("BlackFrameSearch", videoFilePath, streamType,
                 indexList, headerList, startPos, direction, frameCount,
                 preBuiltFrameIndex),
    mRatioThreshold(ratioThreshold)
{
}

void TTBlackFrameSearchTask::operation()
{
  QElapsedTimer t; t.start();

  int pos = establishStartPosition();
  if (pos < 0) return;

  runDirectedSearch(pos, "BlackFrameSearch:", t, [&](const QVector<int>& batch) -> int {
    QVector<bool> matches(batch.size(), false);

    parallelMap(batch.size(), [&](int i) {
      if (i < mSubWrappers.size() && mSubWrappers[i]) {
        // H.264/H.265 path
        matches[i] = mSubWrappers[i]->isFrameBlack(batch[i], kPixelThreshold, mRatioThreshold);
      } else {
        // MPEG-2 fallback (mWorkerCount = 1)
        matches[i] = isFrameBlackAt(batch[i], kPixelThreshold, mRatioThreshold);
      }
    });

    if (mIsAborted) return -1;

    for (int i = 0; i < batch.size(); ++i) {
      if (matches[i]) return batch[i];
    }
    return -1;
  });
}
