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
// TTAVDATALIST
// ----------------------------------------------------------------------------

#ifndef TTAVDATALIST_H
#define TTAVDATALIST_H

#include <QObject>
#include <QList>
#include <QListIterator>

#include "ttcutlist.h"
#include "ttmarkerlist.h"
#include "ttaudiolist.h"
#include "ttsubtitlelist.h"


class TTVideoStream;
class TTAudioStream;
class TTSubtitleStream;
class TTMessageLogger;

/* /////////////////////////////////////////////////////////////////////////////
 * TTAVDataItem
 */
class TTAVItem : public QObject
{
  Q_OBJECT

  friend class TTAVList;
  friend class TTAVData;

  public:
    TTAVItem(TTVideoStream* videoStream);
    ~TTAVItem();

    bool           isInList();
    TTVideoStream* videoStream()              const { return mpVideoStream; };
    void           setVideoStream(TTVideoStream* videoStream);
    TTAudioStream* audioStreamAt(int index)   const { return mpAudioList->at(index).getAudioStream(); }
    int            audioCount()               const { return mpAudioList->count(); }
    TTSubtitleStream* subtitleStreamAt(int index) const { return mpSubtitleList->at(index).getSubtitleStream(); }
    int            subtitleCount()            const { return mpSubtitleList->count(); }
    int            cutCount()                 const { return mpCutList->count(); }
    int            markerCount()              const { return mpMarkerList->count(); }
    TTAudioItem    audioListItemAt(int index) const { return mpAudioList->at(index); }
    TTSubtitleItem subtitleListItemAt(int index) const { return mpSubtitleList->at(index); }
    TTCutItem      cutListItemAt(int index)   const { return mpCutList->at(index); }
    int            cutIndexOf(const TTCutItem& item) const { return mpCutList->indexOf(item); }
    TTMarkerItem   markerAt(int index)               const { return mpMarkerList->at(index); }

    void canCutWith(const TTAVItem* avItem, int cutIn, int cutOut);

    void appendAudioEntry(TTAudioStream* aStream, int order=-1);
    void appendAudioEntry(const TTAudioItem& aItem);

    void appendSubtitleEntry(TTSubtitleStream* sStream, int order=-1);
    void appendSubtitleEntry(const TTSubtitleItem& sItem);

    void appendCutEntry(int cutIn, int cutOut, int order=-1);
    void appendCutEntry(const TTCutItem& cItem);
    void removeCutEntry(const TTCutItem& cItem);
    void updateCutEntry(const TTCutItem& cItem, int cutIn, int cutOut);
    void updateCutEntry(const TTCutItem& cItem, const TTCutItem& uItem);

    void appendMarker(int markerPos, int order=-1);
    void appendMarker(const TTMarkerItem& cItem);
    void removeMarker(const TTMarkerItem& cItem);

  public slots:
    void onRemoveAudioItem(int index);
    void onSwapAudioItems(int oldIndex, int newIndex);
    void onAudioLanguageChanged(int index, const QString& language);
    void onAudioDelayChanged(int index, int delayMs);
    void onRemoveSubtitleItem(int index);
    void onSwapSubtitleItems(int oldIndex, int newIndex);
    void onSubtitleLanguageChanged(int index, const QString& language);

  signals:
		void updated(TTAVItem* avItem);
    void audioItemAppended(const TTAudioItem& item);
    void audioItemRemoved(const TTAudioItem& item);
    void audioItemRemoved(int index);
    void audioItemUpdated(const TTAudioItem& cItem, const TTAudioItem& uItem);
    void audioOrderUpdated(const TTAudioItem& item, int order);
    void audioItemsSwapped(int oldIndex, int newIndex);
    void subtitleItemAppended(const TTSubtitleItem& item);
    void subtitleItemRemoved(const TTSubtitleItem& item);
    void subtitleItemRemoved(int index);
    void subtitleItemUpdated(const TTSubtitleItem& cItem, const TTSubtitleItem& uItem);
    void subtitleOrderUpdated(const TTSubtitleItem& item, int order);
    void subtitleItemsSwapped(int oldIndex, int newIndex);

  private:
    TTAudioList*    audioDataList()    { return mpAudioList; }
    TTSubtitleList* subtitleDataList() { return mpSubtitleList; }
    TTCutList*      cutDataList()      { return mpCutList; }
    TTMarkerList*   markerList()       { return mpMarkerList; }
    void            checkCut(int cutIn, int cutOut);

  private:
  	bool             mIsInList;
    TTVideoStream*   mpVideoStream;
    TTAudioList*     mpAudioList;
    TTSubtitleList*  mpSubtitleList;
    TTCutList*       mpCutList;
    TTMarkerList*    mpMarkerList;
};

/* /////////////////////////////////////////////////////////////////////////////
 * TTAVDataList
 */
class TTAVList : public QObject
{
  Q_OBJECT

  public:
    TTAVList();
    ~TTAVList();

    void      append(TTAVItem* item);
    TTAVItem* at(int i);
    int       indexOf(TTAVItem* item) { return mpAVList.indexOf(item); }
    void      clear();
    int       count();
    void      removeAt(int i);
    void      swap(int a, int b);

  signals:
    void itemAppended(const TTAVItem& item);
    void itemRemoved(int index);
    void itemUpdated(const TTAVItem&, const TTAVItem&);
    void itemsSwapped(int oldIndex, int newIndex);

  private:
    QList<TTAVItem*> mpAVList;
};

#endif //TTAVDATALIST_H
