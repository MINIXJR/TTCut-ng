/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_blackframe.h"

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

bool TTBlackFrameSearchTask::checkMatch(int pos)
{
  return isFrameBlackAt(pos, kPixelThreshold, mRatioThreshold);
}
