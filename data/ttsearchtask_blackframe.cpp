/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_blackframe.h"

#include "../avstream/ttvideoindexlist.h"
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
                                               const QList<TTFrameInfo>& preBuiltFrameIndex)
  : TTSearchTask("BlackFrameSearch", videoFilePath, streamType,
                 indexList, headerList, startPos, direction, frameCount,
                 preBuiltFrameIndex),
    mRatioThreshold(ratioThreshold)
{
}

void TTBlackFrameSearchTask::operation()
{
  QElapsedTimer t; t.start();

  if (!setupWorkers()) {
    emit found(-1, false);
    return;
  }

  int pos = (mDirection > 0)
          ? mIndexList->moveToNextIndexPos(mStartPos, 1)
          : mIndexList->moveToPrevIndexPos(mStartPos, 1);
  if (pos < 0) {
    emit found(-1, false);
    teardownWorkers();
    return;
  }

  int checked = 0;
  int foundPos = -1;

  while (pos >= 0 && pos < mFrameCount && !mIsAborted) {
    QVector<int> batch = collectNextBatch(pos);
    if (batch.isEmpty()) break;

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

    if (mIsAborted) break;

    for (int i = 0; i < batch.size(); ++i) {
      if (matches[i]) { foundPos = batch[i]; break; }
    }
    if (foundPos >= 0) break;

    checked += batch.size();
    if (checked % 20 < batch.size()) emit progress(checked);
  }

  qint64 ms = t.elapsed();
  qDebug() << "BlackFrameSearch:" << checked << "I-frames in" << ms << "ms"
           << (checked > 0
                 ? QString("(%1 fps, %2 workers)").arg(1000.0 * checked / ms, 0, 'f', 1).arg(mWorkerCount)
                 : QString());

  emit found(foundPos, mIsAborted);
  teardownWorkers();
}
