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

  const QList<TTStreamPoint>& points() const { return mPoints; }
  //! Copy of the point at row, or a default-constructed TTStreamPoint if
  //! row is out of range. Used by callers (context menus, dialogs) that
  //! need the full point rather than one role at a time.
  TTStreamPoint pointAt(int row) const;
  //! Updates one point's description in place (e.g. to append/remove the
  //! "(repair planned)" suffix after an audio repair is planned/removed)
  //! without touching frameIndex/type/confidence/duration or re-sorting.
  void setDescriptionAt(int row, const QString& description);

  // Bulk insert (from worker results), re-sorts after
  void addPoints(const QList<TTStreamPoint>& points);

private:
  void insertSorted(const TTStreamPoint& point);

  QList<TTStreamPoint> mPoints;
  float mFrameRate;
};

#endif // TTSTREAMPOINTMODEL_H
