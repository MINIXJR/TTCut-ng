/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_ASPECTSCAN_H
#define TTSEARCHTASK_ASPECTSCAN_H

#include "ttsearchtask.h"
#include "ttstreampoint.h"
#include "ttaspectdetect.h"

//! Full-stream scan for aspect-format (pillarbox) changes.
//!
//! Unlike the three point searches this collects EVERY transition instead of
//! stopping at the first match, and reports them as stream points. It reuses
//! the base class machinery: N decoders in search mode (one for MPEG-2), the
//! task-local thread pool and the shared frame index.
class TTAspectScanTask : public TTSearchTask
{
  Q_OBJECT

public:
  TTAspectScanTask(const QString& videoFilePath,
                   TTAVTypes::AVStreamType streamType,
                   TTVideoIndexList* indexList,
                   TTVideoHeaderList* headerList,
                   int frameCount,
                   float frameRate,
                   int luminanceThreshold,
                   double sampleSeconds,
                   const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());

signals:
  void pointsDetected(const QList<TTStreamPoint>& points);

protected:
  void operation() override;

private:
  //! Collect up to mWorkerCount I-frame positions, honouring the sample stride.
  //! Advances pos to the next unvisited position; -1 when exhausted.
  QVector<int> collectSampleBatch(int& pos);

  //! Decode and classify a batch in parallel. Result index matches batch index.
  QVector<TTAspectSample> classifyBatch(const QVector<int>& batch);

  //! Narrow a confirmed transition to the exact I-frame. Checks every I-frame
  //! strictly between oldStatePos and newStatePos; returns newStatePos when the
  //! window is empty or contains no frame with the new state.
  int refineTransition(int oldStatePos, int newStatePos, bool toPillarbox);

  float mFrameRate;
  int   mLuminanceThreshold;
  int   mSampleStride;      //!< frames between two samples
  int   mCheckedSamples = 0;
};

#endif // TTSEARCHTASK_ASPECTSCAN_H
