/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINTMODEL_H
#define TTSTREAMPOINTMODEL_H

#include "ttstreampoint.h"

#include <QAbstractListModel>
#include <QList>

class TTStreamPointModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Roles {
    FrameIndexRole = Qt::UserRole + 1,
    TypeRole,
    DescriptionRole,
    ConfidenceRole,
    DurationRole
  };

  TTStreamPointModel(QObject* parent = 0);

  void setFrameRate(float fps) { mFrameRate = fps; }
  float frameRate() const { return mFrameRate; }

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

  void addPoint(const TTStreamPoint& point);
  void removeAt(int row);
  void clear();
  void clearAutoDetected();

  const TTStreamPoint& pointAt(int row) const;
  const QList<TTStreamPoint>& points() const { return mPoints; }

  // Bulk insert (from worker results), re-sorts after
  void addPoints(const QList<TTStreamPoint>& points);

public slots:
  void onPointDetected(int frameIndex, int type, const QString& description,
                       float confidence, float duration);

private:
  void insertSorted(const TTStreamPoint& point);

  QList<TTStreamPoint> mPoints;
  float mFrameRate;
};

#endif // TTSTREAMPOINTMODEL_H
