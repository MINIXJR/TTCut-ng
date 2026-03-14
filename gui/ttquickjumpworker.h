/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTQUICKJUMPWORKER_H
#define TTQUICKJUMPWORKER_H

#include "../common/ttthreadtask.h"
#include "../extern/ttffmpegwrapper.h"

#include <QList>
#include <QPixmap>
#include <QSize>

class TTVideoIndexList;
class TTVideoHeaderList;

class TTQuickJumpWorker : public TTThreadTask
{
  Q_OBJECT

public:
  TTQuickJumpWorker(const QString& filePath, int streamType,
                    const QList<int>& frameIndices, const QSize& thumbSize,
                    TTVideoIndexList* indexList, TTVideoHeaderList* headerList,
                    const QList<TTFrameInfo>& prebuiltFrameIndex = QList<TTFrameInfo>());

signals:
  void thumbnailReady(int frameIndex, const QImage& thumbnail);

protected:
  void operation() override;
  void cleanUp() override;

public slots:
  void onUserAbort() override;

private:
  QString             mFilePath;
  int                 mStreamType;
  QList<int>          mFrameIndices;
  QSize               mThumbSize;
  TTVideoIndexList*   mIndexList;
  TTVideoHeaderList*  mHeaderList;
  QList<TTFrameInfo>  mPrebuiltFrameIndex;
};

#endif // TTQUICKJUMPWORKER_H
