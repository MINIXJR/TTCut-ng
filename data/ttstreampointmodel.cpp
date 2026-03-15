/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttstreampointmodel.h"

#include <algorithm>
#include <QTime>

TTStreamPointModel::TTStreamPointModel(QObject* parent)
  : QAbstractListModel(parent),
    mFrameRate(25.0f)
{
}

int TTStreamPointModel::rowCount(const QModelIndex& parent) const
{
  if (parent.isValid()) return 0;
  return mPoints.size();
}

QVariant TTStreamPointModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.row() >= mPoints.size())
    return QVariant();

  const TTStreamPoint& pt = mPoints.at(index.row());

  switch (role) {
    case Qt::DisplayRole: {
      int ms = (mFrameRate > 0) ? qRound(pt.frameIndex() / mFrameRate * 1000) : 0;
      QString time = QTime(0, 0).addMSecs(ms).toString("hh:mm:ss");
      return QString("%1  %2").arg(time, pt.description());
    }
    case FrameIndexRole:
      return pt.frameIndex();
    case TypeRole:
      return static_cast<int>(pt.type());
    case DescriptionRole:
      return pt.description();
    case ConfidenceRole:
      return pt.confidence();
    case DurationRole:
      return pt.duration();
  }

  return QVariant();
}

void TTStreamPointModel::addPoint(const TTStreamPoint& point)
{
  insertSorted(point);
}

void TTStreamPointModel::addPoints(const QList<TTStreamPoint>& points)
{
  if (points.isEmpty()) return;

  beginResetModel();
  for (const TTStreamPoint& pt : points) {
    mPoints.append(pt);
  }
  std::sort(mPoints.begin(), mPoints.end());
  endResetModel();
}

void TTStreamPointModel::removeAt(int row)
{
  if (row < 0 || row >= mPoints.size()) return;

  beginRemoveRows(QModelIndex(), row, row);
  mPoints.removeAt(row);
  endRemoveRows();
}

void TTStreamPointModel::clear()
{
  if (mPoints.isEmpty()) return;

  beginResetModel();
  mPoints.clear();
  endResetModel();
}

void TTStreamPointModel::clearAutoDetected()
{
  beginResetModel();
  QList<TTStreamPoint> kept;
  for (const TTStreamPoint& pt : mPoints) {
    if (!pt.isAutoDetected())
      kept.append(pt);
  }
  mPoints = kept;
  endResetModel();
}

const TTStreamPoint& TTStreamPointModel::pointAt(int row) const
{
  return mPoints.at(row);
}

void TTStreamPointModel::onPointDetected(int frameIndex, int type,
                                          const QString& description,
                                          float confidence, float duration)
{
  TTStreamPoint pt(frameIndex, static_cast<StreamPointType>(type),
                   description, confidence, duration);
  insertSorted(pt);
}

void TTStreamPointModel::insertSorted(const TTStreamPoint& point)
{
  int pos = 0;
  while (pos < mPoints.size() && mPoints.at(pos) < point)
    pos++;

  beginInsertRows(QModelIndex(), pos, pos);
  mPoints.insert(pos, point);
  endInsertRows();
}
