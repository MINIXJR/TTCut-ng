/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_VIDEOWORKER_H
#define TTSTREAMPOINT_VIDEOWORKER_H

#include "../common/ttthreadtask.h"
#include "ttstreampoint.h"

#include <QList>
#include <QString>

class TTVideoHeaderList;

class TTStreamPointVideoWorker : public TTThreadTask
{
  Q_OBJECT

public:
  TTStreamPointVideoWorker(const QString& videoFilePath, int streamType,
                           float frameRate, bool detectAspectChange,
                           TTVideoHeaderList* videoHeaderList);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QList<TTStreamPoint> detectAspectChanges();

  QString              mVideoFilePath;
  int                  mStreamType;
  float                mFrameRate;
  bool                 mDetectAspectChange;
  TTVideoHeaderList*   mVideoHeaderList;
};

#endif // TTSTREAMPOINT_VIDEOWORKER_H
