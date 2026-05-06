/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_BLACKFRAME_H
#define TTSEARCHTASK_BLACKFRAME_H

#include "ttsearchtask.h"

class TTBlackFrameSearchTask : public TTSearchTask
{
  Q_OBJECT
public:
  TTBlackFrameSearchTask(const QString& videoFilePath,
                         TTAVTypes::AVStreamType streamType,
                         TTVideoIndexList* indexList,
                         TTVideoHeaderList* headerList,
                         int startPos, int direction, int frameCount,
                         float ratioThreshold,
                         const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());

protected:
  void operation() override;

private:
  static constexpr int kPixelThreshold = 18;   // Y <= 17 counts as black, matches legacy
  float mRatioThreshold;
};

#endif // TTSEARCHTASK_BLACKFRAME_H
