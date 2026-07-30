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
class TTVideoIndexList;

class TTStreamPointVideoWorker : public TTThreadTask
{
  Q_OBJECT

public:
  // Header-based aspect detection is the worker's only job; the caller
  // decides whether to create it at all (spDetectAspectChange() plus a
  // non-empty header list), so there is nothing left to switch off here.
  //
  // videoIndexList is the display-sorted index list of the same stream. It
  // turns the header position of a sequence header into the position the
  // navigation works with; see detectAspectChanges(). May be null, in which
  // case the marker keeps its bitstream position.
  TTStreamPointVideoWorker(int streamType, TTVideoHeaderList* videoHeaderList,
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
  int                  displayPositionAfter(int headerIndex) const;

  int                  mStreamType;
  TTVideoHeaderList*   mVideoHeaderList;
  TTVideoIndexList*    mVideoIndexList;
};

#endif // TTSTREAMPOINT_VIDEOWORKER_H
