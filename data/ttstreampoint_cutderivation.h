/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_CUTDERIVATION_H
#define TTSTREAMPOINT_CUTDERIVATION_H

#include "ttstreampoint.h"

#include <QList>
#include <QPair>
#include <QString>

class TTVideoStream;

struct TTDerivedCutPair {
  int cutInFrame;
  int cutOutFrame;
  QString reason;
};

class TTStreamPointCutDerivation
{
public:
  static QList<TTDerivedCutPair> deriveCutPairs(
    const QList<TTStreamPoint>& points,
    float frameRate,
    float minDistance,
    TTVideoStream* videoStream);

private:
  struct Cluster {
    float  startTimeSec;
    float  endTimeSec;
    QList<const TTStreamPoint*> points;
    bool   hasBlack;
    bool   hasSilence;
    bool   hasAudioChange;
    bool   hasSceneChange;
    int    audioChangeDirection;  // +1 = to surround, -1 = to stereo, 0 = unknown
  };

  static QList<Cluster> buildClusters(const QList<TTStreamPoint>& points,
                                       float frameRate, float minDistance);
  static int snapToCutInFrame(int frame, TTVideoStream* vs);
  static int snapToCutOutFrame(int frame, TTVideoStream* vs);
};

#endif // TTSTREAMPOINT_CUTDERIVATION_H
