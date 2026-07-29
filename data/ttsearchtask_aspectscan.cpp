/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttsearchtask_aspectscan.h"

#include "../avstream/ttvideoindexlist.h"
#include "../common/ttsettings.h"
#include "../extern/ttffmpegwrapper.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QImage>

TTAspectScanTask::TTAspectScanTask(const QString& videoFilePath,
                                   TTAVTypes::AVStreamType streamType,
                                   TTVideoIndexList* indexList,
                                   TTVideoHeaderList* headerList,
                                   int frameCount,
                                   float frameRate,
                                   int luminanceThreshold,
                                   double sampleSeconds,
                                   const QList<TTFrameInfo>& preBuiltFrameIndex)
  : TTSearchTask("AspectScan", videoFilePath, streamType,
                 indexList, headerList, 0 /*startPos*/, +1 /*direction*/,
                 frameCount, preBuiltFrameIndex),
    mFrameRate(frameRate > 0.0f ? frameRate : 25.0f),
    mLuminanceThreshold(luminanceThreshold),
    mSampleStride(qMax(1, qRound(sampleSeconds * (double)(frameRate > 0.0f ? frameRate : 25.0f))))
{
  // See kHysteresisWindowSeconds: clamp defensively so a stride wider than
  // the hysteresis window can never silently defeat it. The shipped UI caps
  // the stride at 10 s and the window is 10 s, so this is a no-op today -
  // it only matters if either number changes in the future.
  const int hysteresisWindowFrames = qMax(1, qRound(kHysteresisWindowSeconds * mFrameRate));
  mSampleStride = qMin(mSampleStride, hysteresisWindowFrames);
}

QVector<int> TTAspectScanTask::collectSampleBatch(int& pos)
{
  QVector<int> batch;
  while (pos >= 0 && pos < mFrameCount && batch.size() < mWorkerCount) {
    batch.append(pos);

    // Advance at least mSampleStride frames, always landing on an I-frame.
    const int target = pos + mSampleStride;
    int next = pos;
    while (true) {
      const int n = mIndexList->moveToNextIndexPos(next, 1);
      if (n <= next) { next = -1; break; }
      next = n;
      if (next >= target) break;
    }
    pos = next;
  }
  return batch;
}

QVector<TTAspectSample> TTAspectScanTask::classifyBatch(const QVector<int>& batch)
{
  QVector<TTAspectSample> out(batch.size(), TTAspectSample::NoStatement);

  parallelMap(batch.size(), [&](int i) {
    QImage frame = (i < mSubWrappers.size() && mSubWrappers[i])
                     ? mSubWrappers[i]->decodeFrame(batch[i])
                     : decodeFrameAt(batch[i]);
    if (frame.isNull()) {
      if (TTSettings::instance()->logFFmpegDecoder())
          qDebug() << "AspectScan: decode failure at frame" << batch[i];
      return;   // stays NoStatement
    }
    out[i] = classifyAspectSample(frame.convertToFormat(QImage::Format_Grayscale8),
                                  mLuminanceThreshold);
  });

  return out;
}

int TTAspectScanTask::refineTransition(int oldStatePos, int newStatePos, bool toPillarbox)
{
  if (oldStatePos < 0 || newStatePos <= oldStatePos) return newStatePos;

  // Every I-frame strictly between the two samples.
  QVector<int> window;
  int p = mIndexList->moveToNextIndexPos(oldStatePos, 1);
  while (p > oldStatePos && p < newStatePos) {
    window.append(p);
    const int n = mIndexList->moveToNextIndexPos(p, 1);
    if (n <= p) break;
    p = n;
  }
  if (window.isEmpty()) return newStatePos;

  const TTAspectSample wanted = toPillarbox ? TTAspectSample::Pillarbox
                                            : TTAspectSample::NoPillarbox;

  for (int start = 0; start < window.size() && !mIsAborted; start += mWorkerCount) {
    QVector<int> batch = window.mid(start, mWorkerCount);
    QVector<TTAspectSample> samples = classifyBatch(batch);
    for (int i = 0; i < batch.size(); ++i)
      if (samples[i] == wanted) return batch[i];
  }

  return newStatePos;
}

void TTAspectScanTask::operation()
{
  QElapsedTimer timer; timer.start();
  QList<TTStreamPoint> points;

  if (!mIndexList || mIndexList->count() == 0) {
    emit pointsDetected(points);
    return;
  }

  if (!setupWorkers()) {
    // setupWorkers() already logs the path-specific failure reason.
    log->errorMsg(__FILE__, __LINE__,
                  QString("TTAspectScanTask: failed to open decoders"));
    onStatusReport(StatusReportArgs::Finished, tr("Aspect format analysis failed"), 0);
    emit pointsDetected(points);
    return;
  }

  const int plannedSamples = qMax(1, mFrameCount / mSampleStride);
  onStatusReport(StatusReportArgs::Start, tr("Aspect format analysis..."), plannedSamples);

  TTAspectHysteresis hysteresis(qMax(1, qRound(kHysteresisWindowSeconds * mFrameRate)));

  int  pos           = mIndexList->moveToIndexPos(0, 1);
  int  prevPos       = -1;
  bool prevState     = false;
  bool havePrev      = false;
  int  pendingOldPos = -1;   // last sample carrying the state before the run

  while (pos >= 0 && !mIsAborted) {
    QVector<int> batch = collectSampleBatch(pos);
    if (batch.isEmpty()) break;

    QVector<TTAspectSample> samples = classifyBatch(batch);
    if (mIsAborted) break;

    for (int i = 0; i < batch.size(); ++i) {
      if (samples[i] == TTAspectSample::NoStatement) continue;
      const bool isPillarbox = (samples[i] == TTAspectSample::Pillarbox);

      // Remember where the previous state last held, so the refinement window
      // is known once the hysteresis confirms this run.
      if (havePrev && isPillarbox != prevState) pendingOldPos = prevPos;

      TTAspectTransition transition{};
      if (hysteresis.feed(batch[i], samples[i], transition)) {
        const int marker = refineTransition(pendingOldPos, transition.firstFrame,
                                            transition.toPillarbox);
        points.append(TTStreamPoint(
            marker, StreamPointType::PillarboxChange,
            transition.toPillarbox ? QString("16:9 → 4:3pb")
                                   : QString("4:3pb → 16:9"),
            0.0f, 0.0f));
        if (TTSettings::instance()->logCutPipeline())
            qDebug() << "AspectScan: transition at frame" << marker
                     << (transition.toPillarbox ? "-> 4:3pb" : "-> 16:9")
                     << "(sample" << transition.firstFrame << ")";
      }

      prevPos   = batch[i];
      prevState = isPillarbox;
      havePrev  = true;
    }

    mCheckedSamples += batch.size();
    if (mCheckedSamples % 20 < batch.size())
      onStatusReport(StatusReportArgs::Step,
                     tr("Aspect format: %1 of %2 samples")
                         .arg(mCheckedSamples).arg(plannedSamples),
                     qMin(mCheckedSamples, plannedSamples));
  }

  const qint64 ms = timer.elapsed();
  if (TTSettings::instance()->logCutPipeline())
      qDebug() << "AspectScan:" << mCheckedSamples << "samples in" << ms << "ms"
               << QString("(%1 workers, stride %2, %3 transitions)")
                      .arg(mWorkerCount).arg(mSampleStride).arg(points.size());

  onStatusReport(StatusReportArgs::Finished,
                 mIsAborted ? tr("Aspect format analysis cancelled")
                            : tr("Aspect format analysis complete"),
                 plannedSamples);

  // Partial results are delivered on cancel; the widget labels the list
  // as incomplete.
  emit pointsDetected(points);
  teardownWorkers();
}
