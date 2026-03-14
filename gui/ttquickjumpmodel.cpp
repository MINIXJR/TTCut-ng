/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttquickjumpmodel.h"

#include "../avstream/ttavstream.h"
#include "../avstream/ttvideoindexlist.h"

#include <QTime>

TTQuickJumpModel::TTQuickJumpModel(TTVideoStream* videoStream, QObject* parent)
  : QAbstractListModel(parent),
    mVideoStream(videoStream),
    mIndexList(videoStream->indexList()),
    mCurrentPage(0),
    mItemsPerPage(30),
    mFrameRate(videoStream->frameRate()),
    mIntervalSec(0)
{
  buildKeyframeIndex();
}

void TTQuickJumpModel::buildKeyframeIndex()
{
  mKeyframeIndices.clear();
  int pos = -1;
  float lastTimeSec = -999999.0f;

  while (true) {
    pos = mIndexList->moveToNextIndexPos(pos, 1);  // frame_type=1 = I-frame
    if (pos < 0) break;

    if (mIntervalSec > 0 && mFrameRate > 0) {
      float timeSec = (float)pos / mFrameRate;
      if (timeSec - lastTimeSec < (float)mIntervalSec) continue;
      lastTimeSec = timeSec;
    }

    mKeyframeIndices.append(pos);
  }
}

int TTQuickJumpModel::rowCount(const QModelIndex& parent) const
{
  if (parent.isValid()) return 0;

  int offset = pageOffset();
  int remaining = mKeyframeIndices.size() - offset;
  return qMin(remaining, mItemsPerPage);
}

int TTQuickJumpModel::pageOffset() const
{
  return mCurrentPage * mItemsPerPage;
}

QVariant TTQuickJumpModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid()) return QVariant();

  int keyframeIdx = pageOffset() + index.row();
  if (keyframeIdx < 0 || keyframeIdx >= mKeyframeIndices.size())
    return QVariant();

  int frameIndex = mKeyframeIndices.at(keyframeIdx);

  switch (role) {
    case Qt::DecorationRole:
      if (mThumbnails.contains(frameIndex))
        return mThumbnails.value(frameIndex);
      return QVariant();  // null -> delegate draws placeholder

    case Qt::DisplayRole: {
      QTime time = mVideoStream->frameTime(frameIndex);
      QString timecode = time.toString("HH:mm:ss.zzz");
      return QString("%1\n[%2]").arg(timecode).arg(frameIndex);
    }

    case FrameIndexRole:
      return frameIndex;

    default:
      return QVariant();
  }
}

void TTQuickJumpModel::setPage(int pageNum)
{
  if (pageNum < 0 || pageNum >= pageCount()) return;

  beginResetModel();
  mCurrentPage = pageNum;
  endResetModel();
}

int TTQuickJumpModel::pageCount() const
{
  if (mKeyframeIndices.isEmpty()) return 1;
  return (mKeyframeIndices.size() + mItemsPerPage - 1) / mItemsPerPage;
}

int TTQuickJumpModel::currentPage() const
{
  return mCurrentPage;
}

void TTQuickJumpModel::setItemsPerPage(int count)
{
  mItemsPerPage = qMax(1, count);
}

int TTQuickJumpModel::itemsPerPage() const
{
  return mItemsPerPage;
}

void TTQuickJumpModel::setIntervalSeconds(int seconds)
{
  mIntervalSec = qMax(0, seconds);
  beginResetModel();
  buildKeyframeIndex();
  mCurrentPage = 0;
  endResetModel();
}

int TTQuickJumpModel::intervalSeconds() const
{
  return mIntervalSec;
}

int TTQuickJumpModel::pageForFrameIndex(int frameIndex) const
{
  for (int i = 0; i < mKeyframeIndices.size(); ++i) {
    if (mKeyframeIndices.at(i) >= frameIndex)
      return i / mItemsPerPage;
  }
  return pageCount() - 1;
}

int TTQuickJumpModel::keyframeCount() const
{
  return mKeyframeIndices.size();
}

const QList<int>& TTQuickJumpModel::keyframeIndices() const
{
  return mKeyframeIndices;
}

void TTQuickJumpModel::onThumbnailReady(int frameIndex, const QImage& thumbnail)
{
  if (thumbnail.isNull()) {
    // Decode failed -- mark as error placeholder
    mFailedFrames.insert(frameIndex);
  } else {
    // Convert QImage -> QPixmap in main thread (QPixmap is not thread-safe)
    mThumbnails.insert(frameIndex, QPixmap::fromImage(thumbnail));
  }

  // Find the row for this frameIndex on the current page
  int offset = pageOffset();
  for (int row = 0; row < rowCount(); ++row) {
    if (mKeyframeIndices.at(offset + row) == frameIndex) {
      QModelIndex idx = index(row);
      emit dataChanged(idx, idx, {Qt::DecorationRole});
      break;
    }
  }
}

bool TTQuickJumpModel::isFailedFrame(int frameIndex) const
{
  return mFailedFrames.contains(frameIndex);
}
