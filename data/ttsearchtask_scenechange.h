/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_SCENECHANGE_H
#define TTSEARCHTASK_SCENECHANGE_H

#include "ttsearchtask.h"

class TTSceneChangeSearchTask : public TTSearchTask
{
  Q_OBJECT
public:
  TTSceneChangeSearchTask(const QString& videoFilePath,
                          TTAVTypes::AVStreamType streamType,
                          TTVideoIndexList* indexList,
                          TTVideoHeaderList* headerList,
                          int startPos, int direction, int frameCount,
                          float threshold,
                          const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());

protected:
  void onSearchStart(int firstPos) override;
  bool checkMatch(int pos) override;

private:
  // Difference of normalized histograms in [0, 1]; >= mThreshold = match.
  static float histogramDifference(const int histA[256], const int histB[256],
                                   int totalA, int totalB);

  float mThreshold;

  int   mPrevHist[256];
  int   mPrevTotal = 0;
  bool  mHasPrevHist = false;
};

#endif // TTSEARCHTASK_SCENECHANGE_H
