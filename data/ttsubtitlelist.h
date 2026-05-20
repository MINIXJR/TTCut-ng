/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTSUBTITLELISTDATA
// ----------------------------------------------------------------------------

#ifndef TTSUBTITLELISTDATA_H
#define TTSUBTITLELISTDATA_H


#include <QList>
#include <QString>
#include <QFileInfo>

class TTAVItem;
class TTSubtitleStream;

/* ///////////////////////////////////////////////////////////////////////////////////////
 * TTAudioListDataItem
 */
class TTSubtitleItem
{
  friend class TTSubtitleList;

  public:
    TTSubtitleItem(TTAVItem* avDataItem, TTSubtitleStream* sStream);
    TTSubtitleItem(const TTSubtitleItem& item);

    int   order()             const { return mOrder; }
    void  setOrder(int value)       { mOrder = value; }

    QFileInfo         getFileInfo() const;
    TTSubtitleStream* getSubtitleStream() const;
    QString           getFileName() const;
    QString           getLength() const;
    QString           getDelay() const;
    QString           getLanguage() const;
    void              setLanguage(const QString& lang);
    TTAVItem*         avDataItem() const {return mpAVDataItem;}

    const TTSubtitleItem& operator= (const TTSubtitleItem& item);
    bool                  operator< (const TTSubtitleItem& item) const;
    bool                  operator==(const TTSubtitleItem& item) const;

  private:
    void setItemData();

  private:
    TTAVItem*         mpAVDataItem;
    TTSubtitleStream* subtitleStream;
    int               mOrder;
    QString           subtitleLength;
    QString           subtitleDelay;
    QString           mLanguage;  // ISO 639-2/B (deu, eng, fra, ...)
};

/* /////////////////////////////////////////////////////////////////////////////
 * TTAudioListData
 */
class TTSubtitleList : public QObject
{
  Q_OBJECT

  public:
    TTSubtitleList();
    ~TTSubtitleList();

    void append(TTAVItem* avDataItem, TTSubtitleStream* sStream, int order=-1);
    void append(const TTSubtitleItem& item);
    void remove(const TTSubtitleItem& item);
    void update(const TTSubtitleItem& cItem, const TTSubtitleItem& uItem);

    const TTSubtitleItem& at(int index);

    int   count();
    void  clear();
    void  sortByOrder();
    int   indexOf(const TTSubtitleItem& item);
    void  swap(int a, int b);
    void  print();
    void  updateOrder();

  public slots:
    void onAppendItem(const TTSubtitleItem& item);
    void onRemoveItem(const TTSubtitleItem& item);
    void onUpdateItem(const TTSubtitleItem& item, const TTSubtitleItem& uItem);
    void onUpdateOrder(const TTSubtitleItem& item, int order);
    void onRefreshData(TTAVItem* avItem);

  signals:
    void dataChanged();
    void itemAppended(const TTSubtitleItem& item);
    void itemRemoved(const TTSubtitleItem& item);
    void itemRemoved(int index);
    void itemUpdated(const TTSubtitleItem& item, const TTSubtitleItem& uItem);
    void orderUpdated(const TTSubtitleItem& item, int order);
    void itemsSwapped(int oldIndex, int newIndex);

  private:
    QList<TTSubtitleItem>data;
};

#endif //TTSUBTITLELISTDATA_H
