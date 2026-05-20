/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINT_VIDEOWORKER_H
#define TTSTREAMPOINT_VIDEOWORKER_H

#include "../common/ttthreadtask.h"
#include "ttstreampoint.h"

#include <QList>
#include <QString>

class TTVideoHeaderList;
class TTVideoIndexList;

class TTStreamPointVideoWorker : public TTThreadTask
{
  Q_OBJECT

public:
  TTStreamPointVideoWorker(const QString& videoFilePath, int streamType,
                           float frameRate, bool detectAspectChange,
                           bool detectPillarbox, int pillarboxThreshold,
                           TTVideoHeaderList* videoHeaderList,
                           TTVideoIndexList* videoIndexList);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QList<TTStreamPoint> detectAspectChanges();
  QList<TTStreamPoint> detectPillarboxChanges();

  bool isPillarboxFrame(const uint8_t* yPlane, int yStride, int width, int height,
                        int threshold, float& barWidthPercent);

  QString              mVideoFilePath;
  int                  mStreamType;
  float                mFrameRate;
  bool                 mDetectAspectChange;
  bool                 mDetectPillarbox;
  int                  mPillarboxThreshold;
  TTVideoHeaderList*   mVideoHeaderList;
  TTVideoIndexList*    mVideoIndexList;
};

#endif // TTSTREAMPOINT_VIDEOWORKER_H
