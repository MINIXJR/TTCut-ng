/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTQUICKJUMPMODEL_H
#define TTQUICKJUMPMODEL_H

#include <QAbstractListModel>
#include <QPixmap>
#include <QList>
#include <QMap>
#include <QSet>

class TTVideoStream;
class TTVideoIndexList;

class TTQuickJumpModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles {
    FrameIndexRole = Qt::UserRole + 1
  };

  TTQuickJumpModel(TTVideoStream* videoStream, QObject* parent = 0);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

  void setStartIndex(int startIdx);
  int startIndex() const;
  int pageCount() const;
  int currentPage() const;
  bool canPageBack() const;
  bool canPageForward() const;
  void setItemsPerPage(int count);
  int itemsPerPage() const;

  void setAnchorFrame(int frameIndex);
  void setIntervalSeconds(int seconds);
  int intervalSeconds() const;

  int keyframeListIndex(int frameIndex) const;
  int keyframeCount() const;
  const QList<int>& keyframeIndices() const;
  bool isFailedFrame(int frameIndex) const;

public slots:
  void onThumbnailReady(int frameIndex, const QImage& thumbnail);

private:
  void buildKeyframeIndex();

private:
  TTVideoStream*   mVideoStream;
  TTVideoIndexList* mIndexList;
  QList<int>       mKeyframeIndices;
  QMap<int, QPixmap> mThumbnails;
  QSet<int>        mFailedFrames;
  int              mStartIndex;
  int              mItemsPerPage;
  float            mFrameRate;
  int              mAnchorFrame;
  int              mIntervalSec;
};

#endif // TTQUICKJUMPMODEL_H
