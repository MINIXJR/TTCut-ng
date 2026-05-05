/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_logo.h"
#include "ttlogodetector.h"

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

void TTLogoSearchTask::onSearchStart(int firstPos)
{
  QImage f = decodeFrameAt(firstPos);
  // Null image -> score=0 -> "logo absent" as the initial state. Acceptable.
  float score = mDetector ? mDetector->matchScore(f) : 0.0f;
  mInitialLogoPresent = (score >= mThreshold);
}

bool TTLogoSearchTask::checkMatch(int pos)
{
  if (!mDetector) return false;
  QImage f = decodeFrameAt(pos);
  float score = mDetector->matchScore(f);
  bool present = (score >= mThreshold);
  return present != mInitialLogoPresent;
}
