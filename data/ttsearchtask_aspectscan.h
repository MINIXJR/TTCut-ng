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
#include "ttanalysislog.h"

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
  //! When reasons is non-null it is resized to the batch size and receives the
  //! per-sample reason. Written per index from the worker threads - never
  //! aggregate inside the parallel lambda.
  QVector<TTAspectSample> classifyBatch(const QVector<int>& batch,
                                        QVector<TTAspectReason>* reasons = nullptr);

  //! Narrow a confirmed transition to the exact I-frame. Checks every I-frame
  //! strictly between oldStatePos and newStatePos; returns newStatePos when the
  //! window is empty or contains no frame with the new state.
  int refineTransition(int oldStatePos, int newStatePos, bool toPillarbox);

  //! A confirmed state must hold for this many seconds before the hysteresis
  //! in operation() reports a transition. mSampleStride is clamped to this
  //! window in the constructor: a stride wider than the window would let a
  //! whole short episode fall between two samples and be missed entirely, or
  //! anchor a transition on the wrong sample. Both operation() and the
  //! constructor read this single constant, so changing the hysteresis
  //! duration can never silently outrun the clamp (or vice versa).
  static constexpr float kHysteresisWindowSeconds = 10.0f;

  float mFrameRate;
  int   mLuminanceThreshold;
  int   mSampleStride;      //!< frames between two samples
  int   mCheckedSamples = 0;
  int   mCountNoPillarbox = 0;
  int   mCountPillarbox   = 0;
  int   mCountBarsTooWide = 0;
  int   mCountCentreDark  = 0;
  int   mCountUnusable    = 0;   //!< incl. failed decodes
  int   mCountDiscarded   = 0;
  TTAnalysisLog mLog;
};

#endif // TTSEARCHTASK_ASPECTSCAN_H
