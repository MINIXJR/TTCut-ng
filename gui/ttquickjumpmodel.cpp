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
    mStartIndex(0),
    mItemsPerPage(30),
    mFrameRate(videoStream->frameRate()),
    mAnchorFrame(0),
    mIntervalSec(0)
{
  buildKeyframeIndex();
}

void TTQuickJumpModel::buildKeyframeIndex()
{
  mKeyframeIndices.clear();

  // Collect all I-frames
  QList<int> allKeyframes;
  int pos = -1;
  while (true) {
    pos = mIndexList->moveToNextIndexPos(pos, 1);  // frame_type=1 = I-frame
    if (pos < 0) break;
    allKeyframes.append(pos);
  }

  if (allKeyframes.isEmpty()) return;

  // No interval filtering — use all keyframes
  if (mIntervalSec <= 0 || mFrameRate <= 0) {
    mKeyframeIndices = allKeyframes;
    return;
  }

  // Find anchor: last I-frame <= mAnchorFrame
  int anchorIdx = 0;
  for (int i = 0; i < allKeyframes.size(); ++i) {
    if (allKeyframes.at(i) <= mAnchorFrame)
      anchorIdx = i;
    else
      break;
  }

  float anchorTimeSec = (float)allKeyframes.at(anchorIdx) / mFrameRate;
  float intervalSec = (float)mIntervalSec;

  // Backward from anchor
  QList<int> backward;
  float lastTimeSec = anchorTimeSec;
  for (int i = anchorIdx - 1; i >= 0; --i) {
    float timeSec = (float)allKeyframes.at(i) / mFrameRate;
    if (lastTimeSec - timeSec >= intervalSec) {
      backward.prepend(allKeyframes.at(i));
      lastTimeSec = timeSec;
    }
  }

  mKeyframeIndices = backward;

  // Anchor itself — always included
  mKeyframeIndices.append(allKeyframes.at(anchorIdx));

  // Forward from anchor
  lastTimeSec = anchorTimeSec;
  for (int i = anchorIdx + 1; i < allKeyframes.size(); ++i) {
    float timeSec = (float)allKeyframes.at(i) / mFrameRate;
    if (timeSec - lastTimeSec >= intervalSec) {
      mKeyframeIndices.append(allKeyframes.at(i));
      lastTimeSec = timeSec;
    }
  }
}

int TTQuickJumpModel::rowCount(const QModelIndex& parent) const
{
  if (parent.isValid()) return 0;

  int remaining = mKeyframeIndices.size() - mStartIndex;
  return qMin(remaining, mItemsPerPage);
}

QVariant TTQuickJumpModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid()) return QVariant();

  int keyframeIdx = mStartIndex + index.row();
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

void TTQuickJumpModel::setStartIndex(int startIdx)
{
  int maxStart = qMax(0, mKeyframeIndices.size() - 1);
  startIdx = qBound(0, startIdx, maxStart);

  beginResetModel();
  mStartIndex = startIdx;
  endResetModel();
}

int TTQuickJumpModel::startIndex() const
{
  return mStartIndex;
}

int TTQuickJumpModel::pageCount() const
{
  if (mKeyframeIndices.isEmpty()) return 1;
  return (mKeyframeIndices.size() + mItemsPerPage - 1) / mItemsPerPage;
}

int TTQuickJumpModel::currentPage() const
{
  if (mItemsPerPage <= 0) return 0;
  return mStartIndex / mItemsPerPage;
}

bool TTQuickJumpModel::canPageBack() const
{
  return mStartIndex > 0;
}

bool TTQuickJumpModel::canPageForward() const
{
  return mStartIndex + mItemsPerPage < mKeyframeIndices.size();
}

void TTQuickJumpModel::setItemsPerPage(int count)
{
  mItemsPerPage = qMax(1, count);
}

int TTQuickJumpModel::itemsPerPage() const
{
  return mItemsPerPage;
}

void TTQuickJumpModel::setAnchorFrame(int frameIndex)
{
  mAnchorFrame = frameIndex;
}

void TTQuickJumpModel::setIntervalSeconds(int seconds)
{
  mIntervalSec = qMax(0, seconds);
  beginResetModel();
  buildKeyframeIndex();
  mStartIndex = 0;
  endResetModel();
}

int TTQuickJumpModel::intervalSeconds() const
{
  return mIntervalSec;
}

int TTQuickJumpModel::keyframeListIndex(int frameIndex) const
{
  for (int i = 0; i < mKeyframeIndices.size(); ++i) {
    if (mKeyframeIndices.at(i) >= frameIndex) {
      // Exact match or first keyframe
      if (i == 0 || mKeyframeIndices.at(i) == frameIndex)
        return i;
      // Pick the closer keyframe (previous vs next)
      int distPrev = frameIndex - mKeyframeIndices.at(i - 1);
      int distNext = mKeyframeIndices.at(i) - frameIndex;
      return (distPrev <= distNext) ? i - 1 : i;
    }
  }
  return qMax(0, mKeyframeIndices.size() - 1);
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
  int offset = mStartIndex;
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
