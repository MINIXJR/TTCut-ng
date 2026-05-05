/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_scenechange.h"

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

void TTSceneChangeSearchTask::onSearchStart(int firstPos)
{
  // Cache the histogram for the first iterated I-frame so the first
  // checkMatch() compares it against the next I-frame.
  if (buildHistogramAt(firstPos, mPrevHist, mPrevTotal))
    mHasPrevHist = true;
}

bool TTSceneChangeSearchTask::checkMatch(int pos)
{
  if (!mHasPrevHist) {
    // onSearchStart failed to build histA; rebuild here as fallback.
    if (!buildHistogramAt(pos, mPrevHist, mPrevTotal)) return false;
    mHasPrevHist = true;
    return false;
  }

  int curHist[256];
  int curTotal = 0;
  if (!buildHistogramAt(pos, curHist, curTotal)) return false;

  float diff = histogramDifference(mPrevHist, curHist, mPrevTotal, curTotal);

  // Cache current histogram for next iteration (replacing the previous one).
  std::memcpy(mPrevHist, curHist, sizeof(mPrevHist));
  mPrevTotal = curTotal;

  return diff > mThreshold;
}

// Per-bin absolute difference of normalized histograms, summed and halved.
// Result lies in [0, 1]; matches the legacy formula at master line 1801-1803.
float TTSceneChangeSearchTask::histogramDifference(const int histA[256], const int histB[256],
                                                   int totalA, int totalB)
{
  if (totalA <= 0 || totalB <= 0) return 0.0f;
  float diff = 0.0f;
  for (int i = 0; i < 256; ++i)
    diff += qAbs((float)histA[i] / totalA - (float)histB[i] / totalB);
  return diff / 2.0f;
}
