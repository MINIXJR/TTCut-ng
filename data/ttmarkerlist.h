/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMARKERLIST
// ----------------------------------------------------------------------------

#ifndef TTMARKERLIST_H
#define TTMARKERLIST_H

#include <QList>
#include <QUuid>
#include <QTime>
#include <QString>
#include <QFileInfo>

class TTAVItem;

/* //////////////////////////////////////////////////////////////////////////////
 * TTCutListDataItem
 */
class TTMarkerItem
{
  friend class TTMarkerList;

  public:
    TTMarkerItem(TTAVItem* avDataItem, int markerPos);
    TTMarkerItem(const TTMarkerItem& item);

    QUuid ID() const { return mID; }

    int   order()             const { return mOrder; }
    void  setOrder(int value)       { mOrder = value; }
    int   markerPos()         const { return mMarkerPos; }

    QString fileName() const;
    QTime   markerTime() const;

    TTAVItem* avDataItem() const;

    const TTMarkerItem& operator=(const TTMarkerItem& item);
    bool operator==(const TTMarkerItem& item) const;
    bool operator<(const TTMarkerItem& item) const;

  private:
  	QUuid     mID;
    int       mOrder;
    int       mMarkerPos;
    TTAVItem* mpAVDataItem;
};

/* //////////////////////////////////////////////////////////////////////////////
 * TTMarkerList
 */
class TTMarkerList : public QObject
{
  Q_OBJECT

  public:
    TTMarkerList();
    ~TTMarkerList();

    void append(TTAVItem* avItem, int markerPos, int order=-1);
    void append(const TTMarkerItem& item);
    void remove(const TTMarkerItem& item);
    void update(const TTMarkerItem& cItem, const TTMarkerItem& uItem);

    const TTMarkerItem& at(int index);

    void clear();
    int  count();

    void swap(int a, int b);
    void sortByOrder();
    void updateOrder();

  public slots:
		void onRefreshData(TTAVItem* avItem);
    void onAppendItem(const TTMarkerItem& item);
    void onRemoveItem(const TTMarkerItem& item);
    void onUpdateItem(const TTMarkerItem& cItem, const TTMarkerItem& uItem);
    void onUpdateOrder(const TTMarkerItem& item, int order);

  signals:
		void dataChanged();
    void itemAppended(const TTMarkerItem& item);
    void itemRemoved(const TTMarkerItem& item);
    void itemRemoved(int index);
    void itemUpdated(const TTMarkerItem& cItem, const TTMarkerItem& uItem);
    void orderUpdated(const TTMarkerItem& item, int order);

  private:
    QList<TTMarkerItem> data;
};
#endif
