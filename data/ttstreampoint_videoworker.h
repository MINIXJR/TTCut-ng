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

class TTVideoHeaderList;

class TTStreamPointVideoWorker : public TTThreadTask
{
  Q_OBJECT

public:
  // Header-based aspect detection is the worker's only job; the caller
  // decides whether to create it at all (spDetectAspectChange() plus a
  // non-empty header list), so there is nothing left to switch off here.
  TTStreamPointVideoWorker(int streamType, TTVideoHeaderList* videoHeaderList);

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QList<TTStreamPoint> detectAspectChanges();

  int                  mStreamType;
  TTVideoHeaderList*   mVideoHeaderList;
};

#endif // TTSTREAMPOINT_VIDEOWORKER_H
