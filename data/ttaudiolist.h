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
// TTAUDIOLISTDATA
// ----------------------------------------------------------------------------

#ifndef TTAUDIOLISTDATA_H
#define TTAUDIOLISTDATA_H


#include <QList>
#include <QString>
#include <QFileInfo>

class TTAVItem;
class TTAudioStream;

/* ///////////////////////////////////////////////////////////////////////////////////////
 * TTAudioListDataItem
 */
class TTAudioItem
{
  friend class TTAudioList;

  public:
    TTAudioItem(TTAVItem* avDataItem, TTAudioStream* aStream);
    TTAudioItem(const TTAudioItem& item);

    int   order()                   const { return mOrder; }
    void  setOrder(int value)             { mOrder = value; }

    QFileInfo      getFileInfo() const;
    TTAudioStream* getAudioStream() const;
    QString        getFileName() const;
    QString        getLength() const;
    QString        getVersion() const;
    QString        getMode() const;
    QString        getBitrate() const;
    QString        getSamplerate() const;
    int            getDelayMs() const;
    void           setDelayMs(int ms);
    QString        getLanguage() const;
    void           setLanguage(const QString& lang);
    TTAVItem*  avDataItem() const {return mpAVDataItem;}

    const TTAudioItem& operator=(const TTAudioItem& item);
    bool operator<(const TTAudioItem& item) const;
    bool operator==(const TTAudioItem& item) const;

  private:
    void setItemData();

  private:
    TTAVItem*  mpAVDataItem;
    TTAudioStream* audioStream;
    int            mOrder;
    QString        audioLength;
    int            mAudioDelayMs;
    QString        mLanguage;  // ISO 639-2/B (deu, eng, fra, ...)
};

/* /////////////////////////////////////////////////////////////////////////////
 * TTAudioListData
 */
class TTAudioList : public QObject
{
  Q_OBJECT

  public:
    TTAudioList();
    ~TTAudioList();

    void append(TTAVItem* avDataItem, TTAudioStream* aStream, int order=-1);
    void append(const TTAudioItem& item);
    void remove(const TTAudioItem& item);
    void update(const TTAudioItem& cItem, const TTAudioItem& uItem);

    const TTAudioItem& at(int index);

    int   count();
    void  clear();
    void  sortByOrder();
    int   indexOf(const TTAudioItem& item);
    void  swap(int a, int b);
    void  print();
    void  updateOrder();

  public slots:
    void onAppendItem(const TTAudioItem& item);
    void onRemoveItem(const TTAudioItem& item);
    void onUpdateItem(const TTAudioItem& item, const TTAudioItem& uItem);
    void onUpdateOrder(const TTAudioItem& item, int order);
    void onRefreshData(TTAVItem* avItem);

  signals:
		void dataChanged();
    void itemAppended(const TTAudioItem& item);
    void itemRemoved(const TTAudioItem& item);
    void itemRemoved(int index);
    void itemUpdated(const TTAudioItem& item, const TTAudioItem& uItem);
    void orderUpdated(const TTAudioItem& item, int order);
    void itemsSwapped(int oldIndex, int newIndex);

  private:
    QList<TTAudioItem>data;
};

#endif //TTAUDIOLISTDATA_H
