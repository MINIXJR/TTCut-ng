/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_H
#define TTSEARCHTASK_H

#include "../common/ttthreadtask.h"
#include "../avstream/ttavtypes.h"
#include "../extern/ttffmpegwrapper.h"   // for TTFrameInfo

#include <QImage>
#include <QList>
#include <QRunnable>
#include <QSemaphore>
#include <QString>
#include <QThreadPool>
#include <QVector>

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
               const QList<TTFrameInfo>& preBuiltFrameIndex = QList<TTFrameInfo>());
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
  QList<TTFrameInfo>        mPreBuiltFrameIndex;

  TTFFmpegWrapper*          mFFmpegWrapper = nullptr;
  TTMpeg2Decoder*           mMpeg2Decoder  = nullptr;

  QThreadPool*              mDecodePool  = nullptr; // local pool sized to mWorkerCount
};

#endif // TTSEARCHTASK_H
