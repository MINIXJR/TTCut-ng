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
// TTCUTLIST
// ----------------------------------------------------------------------------

#ifndef TTCUTLIST_H
#define TTCUTLIST_H

#include <QList>
#include <QUuid>
#include <QTime>
#include <QString>
#include <QFileInfo>

class TTAVItem;

/* //////////////////////////////////////////////////////////////////////////////
 * TTCutListDataItem
 */
class TTCutItem
{
  friend class TTCutList;

  public:
    TTCutItem(TTAVItem* avDataItem, int cutIn, int cutOut);
    TTCutItem(const TTCutItem& item);

    QUuid ID() const { return mID; }

    void  update(int cutIn, int cutout);
    int   order()             const { return mOrder; }
    void  setOrder(int value)       { mOrder = value; }
    int   cutIn()             const { return mCutInIndex; }
    int   cutOut()            const { return mCutOutIndex; }


    QString   fileName() const;
    QString   cutInString() const ;
    QString   cutOutString() const ;
    QString   cutLengthString() const;

    int       cutInIndex() const;
    int       cutOutIndex() const;
    int       cutInFrameType() const;
    int       cutOutFrameType() const;
    QTime     cutInTime() const;
    QTime     cutOutTime() const;
    int       cutLengthFrames() const;
    quint64   cutLengthBytes() const;
    QTime     cutLengthTime() const;
    TTAVItem* avDataItem() const;

    const TTCutItem& operator=(const TTCutItem& item);
    bool operator==(const TTCutItem& item) const;
    bool operator<(const TTCutItem& item) const;

  private:
  	QUuid     mID;
    int       mOrder;
    int       mCutInIndex;
    int       mCutOutIndex;
    TTAVItem* mpAVDataItem;
};

/* //////////////////////////////////////////////////////////////////////////////
 * TTCutListData
 */
class TTCutList : public QObject
{
  Q_OBJECT

  public:
    TTCutList();
    ~TTCutList();

    void append(TTAVItem* avItem, int cutIn, int cutOut, int order=-1);
    void append(const TTCutItem& item);
    void remove(const TTCutItem& item);
    void update(const TTCutItem& cItem, const TTCutItem& uItem);

    QString streamFileName(int index);
    QString cutInPosString(int index);
    QString cutOutPosString(int index);
    QString cutLengthString(int index);

    const TTCutItem& at(int index);

    void clear();
    int  count();
    int  indexOf(const TTCutItem& item);

    void swap(int a, int b);
    void sortByOrder();
    void print();
    void updateOrder();

  public slots:
    void onAppendItem(const TTCutItem& item);
    void onRemoveItem(const TTCutItem& item);
    void onUpdateItem(const TTCutItem& cItem, const TTCutItem& uItem);
    void onUpdateOrder(const TTCutItem& item, int order);
    void onRefreshData(TTAVItem* avItem);

  signals:
    void dataChanged();
    void itemAppended(const TTCutItem& item);
    void itemRemoved(const TTCutItem& item);
    void itemRemoved(int index);
    void itemUpdated(const TTCutItem& cItem, const TTCutItem& uItem);
    void orderUpdated(const TTCutItem& item, int order);

  private:
    QList<TTCutItem> data;
};
#endif
