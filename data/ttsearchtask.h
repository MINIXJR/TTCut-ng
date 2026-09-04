/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_H
#define TTSEARCHTASK_H

#include "../common/ttthreadtask.h"
#include "../avstream/ttavtypes.h"
#include "../extern/ttffmpegwrapper.h"   // for TTFrameInfo
#include "../mpeg2decoder/ttmpeg2decoder.h"

#include <QImage>
#include <QList>
#include <QRunnable>
#include <QSemaphore>
#include <QString>
#include <QThreadPool>
#include <QVector>
#include <QElapsedTimer>
#include <QDebug>
#include "../common/ttsettings.h"

class TTVideoIndexList;
class TTVideoHeaderList;
class TTMpeg2Decoder;

class TTSearchTask : public TTThreadTask
{
  Q_OBJECT

public:
  TTSearchTask(const QString& taskName,
               const QString& videoFilePath,
               TTAVTypes::AVStreamType streamType,
               TTVideoIndexList* indexList,
               TTVideoHeaderList* headerList,
               int startPos,
               int direction,    // +1 forward, -1 backward
               int frameCount,
               const TTFrameIndexBundle& preBuiltFrameIndex = TTFrameIndexBundle());
  ~TTSearchTask() override;

signals:
  void progress(int checkedFrames);             // every 20 iterations
  void found(int foundPos, bool wasAborted);    // foundPos -1 = not-found, >=0 = position

protected:
  // Worker-thread decode helpers usable from subclass operation() bodies.
  QImage decodeFrameAt(int pos);
  bool   isFrameBlackAt(int pos, int pixelThreshold, float ratioThreshold);
  bool   buildHistogramAt(int pos, int hist[256], int& totalPixels);

  // ---- Batched-parallel helpers (used by subclass operation() bodies) ----

  // Open N TTFFmpegWrapper instances (or 1 TTMpeg2Decoder for MPEG-2),
  // configure them with setAnalysisMode(true) + setSearchMode(true) and
  // populate with the pre-built frame index. Sets mWorkerCount.
  // Returns false if any open fails (and leaves mSubWrappers empty).
  bool setupWorkers();

  // Close + delete all sub-decoders, destroy mDecodePool. Idempotent.
  void teardownWorkers();

  // Common prologue of the directed searches: workers up, then the first
  // index position one step from mStartPos in the search direction. On
  // failure it has already emitted found(-1, false) (and torn the workers
  // down again) and returns -1, so the caller just returns.
  int establishStartPosition();

  // Collect up to mWorkerCount I-frame positions starting at and including
  // currentPos, walking via mIndexList in mDirection. Updates currentPos to
  // the position immediately after the last batch entry (so the caller can
  // pass the same variable back next iteration). Returns empty when
  // exhausted.
  QVector<int> collectNextBatch(int& currentPos);

  // Dispatch count lambdas to mDecodePool, one per worker index [0..count-1].
  // Waits for all to complete via QSemaphore. Falls back to single-threaded
  // inline execution when count==1 or mDecodePool is null.
  template<class Func>
  void parallelMap(int count, Func&& lambda)
  {
    if (count <= 0) return;
    if (count == 1 || !mDecodePool) {
      // Single-worker fallback (also used when mDecodePool not initialised).
      if (!mIsAborted) lambda(0);
      return;
    }

    QSemaphore done(0);
    for (int i = 0; i < count; ++i) {
      if (mIsAborted) {
        done.release(count - i);   // keep acquire() count balanced
        break;
      }
      auto* runnable = QRunnable::create([&, i]() {
        lambda(i);
        done.release(1);
      });
      runnable->setAutoDelete(true);
      mDecodePool->start(runnable);
    }
    done.acquire(count);
  }

  // Directed-search loop shared by the black-frame, logo and scene-change
  // searches. Walks I-frames from `pos` in mDirection in worker-sized
  // batches; `step(batch)` evaluates one batch and returns the first hit
  // position (or -1: no hit, or aborted). Reports progress about every 20
  // frames, logs the throughput under logTag with the caller's timer,
  // emits found() and tears the workers down. The caller has already run
  // setupWorkers() and established its initial state.
  template<class Step>
  void runDirectedSearch(int pos, const char* logTag, const QElapsedTimer& t, Step&& step)
  {
    int checked = 0;
    int foundPos = -1;

    while (pos >= 0 && pos < mFrameCount && !mIsAborted) {
      QVector<int> batch = collectNextBatch(pos);
      if (batch.isEmpty()) break;

      foundPos = step(batch);
      if (mIsAborted) break;
      if (foundPos >= 0) break;

      checked += batch.size();
      if (checked % 20 < batch.size()) emit progress(checked);
    }

    qint64 ms = t.elapsed();
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << logTag << checked << "I-frames in" << ms << "ms"
                 << (checked > 0
                       ? QString("(%1 fps, %2 workers)").arg(1000.0 * checked / ms, 0, 'f', 1).arg(mWorkerCount)
                       : QString());

    emit found(foundPos, mIsAborted);
    teardownWorkers();
  }

  // TTThreadTask interface. Subclasses MUST override operation().
  void operation() override = 0;
  void cleanUp() override;

  // Read by closeProject() to recover stream type without dynamic_cast.
  TTAVTypes::AVStreamType streamType() const { return mStreamType; }

public slots:
  void onUserAbort() override;   // sets mIsAborted (inherited)

protected:
  // Accessible from subclass operation() bodies (declared in ctor-init order).
  TTVideoIndexList*            mIndexList   = nullptr;
  TTVideoHeaderList*           mHeaderList  = nullptr;
  int                          mStartPos    = 0;
  int                          mDirection   = 1;
  int                          mFrameCount  = 0;

  // Batched-parallel state (lifetime = setupWorkers .. teardownWorkers).
  int                          mWorkerCount = 1;
  QVector<TTFFmpegWrapper*>    mSubWrappers;           // N entries (H.264/H.265)

private:
  bool openDecoder();
  void closeDecoder();

  QString                   mFilePath;
  TTAVTypes::AVStreamType   mStreamType;
  TTFrameIndexBundle        mPreBuiltFrameIndex;

  TTFFmpegWrapper*          mFFmpegWrapper = nullptr;
  TTMpeg2Decoder*           mMpeg2Decoder  = nullptr;

  QThreadPool*              mDecodePool  = nullptr; // local pool sized to mWorkerCount
};

#endif // TTSEARCHTASK_H
