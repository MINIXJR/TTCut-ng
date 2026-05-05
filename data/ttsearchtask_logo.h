/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_LOGO_H
#define TTSEARCHTASK_LOGO_H

#include "ttsearchtask.h"

class TTLogoDetector;

class TTLogoSearchTask : public TTSearchTask
{
  Q_OBJECT
public:
  TTLogoSearchTask(const QString& videoFilePath,
                   TTAVTypes::AVStreamType streamType,
                   TTVideoIndexList* indexList,
                   TTVideoHeaderList* headerList,
                   int startPos, int direction, int frameCount,
                   const TTLogoDetector* detector,
                   float threshold,
                   const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());

protected:
  void onSearchStart(int firstPos) override;
  bool checkMatch(int pos) override;

private:
  const TTLogoDetector* mDetector;     // owned by main thread; const-only access
  float mThreshold;
  bool  mInitialLogoPresent = false;
};

#endif // TTSEARCHTASK_LOGO_H
