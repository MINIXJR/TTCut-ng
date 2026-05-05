/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2026 / TTCut-ng                                    */
/*----------------------------------------------------------------------------*/

#ifndef TTSEARCHTASK_H
#define TTSEARCHTASK_H

#include "../common/ttthreadtask.h"
#include "../avstream/ttavtypes.h"

#include <QImage>
#include <QString>

class TTVideoIndexList;
class TTVideoHeaderList;
class TTFFmpegWrapper;
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
               int frameCount);
  ~TTSearchTask() override;

signals:
  void progress(int checkedFrames);             // every 20 iterations
  void found(int foundPos, bool wasAborted);    // foundPos -1 = not-found, >=0 = position

protected:
  // Subclass implements the per-frame test.
  virtual bool checkMatch(int pos) = 0;
  // Optional hook called once with the first iterated I-frame position.
  virtual void onSearchStart(int firstPos) { Q_UNUSED(firstPos); }

  // Worker-thread decode helpers usable from checkMatch / onSearchStart.
  QImage decodeFrameAt(int pos);
  bool   isFrameBlackAt(int pos, int pixelThreshold, float ratioThreshold);
  bool   buildHistogramAt(int pos, int hist[256], int& totalPixels);

  // TTThreadTask interface.
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;   // sets mIsAborted (inherited)

protected:
  // Read by closeProject() to recover stream type without dynamic_cast.
  TTAVTypes::AVStreamType streamType() const { return mStreamType; }

private:
  bool openDecoder();
  void closeDecoder();

  QString                   mFilePath;
  TTAVTypes::AVStreamType   mStreamType;
  TTVideoIndexList*         mIndexList;
  TTVideoHeaderList*        mHeaderList;
  int                       mStartPos;
  int                       mDirection;
  int                       mFrameCount;

  TTFFmpegWrapper*          mFFmpegWrapper = nullptr;
  TTMpeg2Decoder*           mMpeg2Decoder  = nullptr;
};

#endif // TTSEARCHTASK_H
