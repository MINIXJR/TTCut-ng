/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_AUDIOWORKER_H
#define TTSTREAMPOINT_AUDIOWORKER_H

#include "../common/ttthreadtask.h"
#include "ttstreampoint.h"

#include <QList>
#include <QString>

struct AVFrame;
class TTAudioHeaderList;

class TTStreamPointAudioWorker : public TTThreadTask
{
  Q_OBJECT

public:
  TTStreamPointAudioWorker(const QString& audioFilePath, float videoFrameRate,
                           bool detectSilence, int silenceThresholdDb,
                           float silenceMinDuration,
                           bool detectAudioChange,
                           TTAudioHeaderList* audioHeaderList);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QList<TTStreamPoint> detectSilencePoints();
  QList<TTStreamPoint> detectAudioChanges();
  void collectSilenceResult(AVFrame* filtFrame, QList<TTStreamPoint>& results);

  QString              mAudioFilePath;
  float                mVideoFrameRate;
  bool                 mDetectSilence;
  int                  mSilenceThresholdDb;
  float                mSilenceMinDuration;
  bool                 mDetectAudioChange;
  TTAudioHeaderList*   mAudioHeaderList;
};

#endif // TTSTREAMPOINT_AUDIOWORKER_H
