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
#include "../extern/ttaudiorepairitem.h"


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

    //! Index of the first AC3 track, or -1 if none is loaded. AC3-only
    //! (streamType() == TTAVTypes::ac3_audio) because the audio-anomaly
    //! scan (TTAudioAnomalyScanTask) only ever scans one AC3 track this
    //! same way - shared here so the scan dispatch (TTCutMainWindow::
    //! onAnalyzeStreamPoints) and the repair context menu (TTStreamPoint
    //! Widget, audio-anomaly-repair Task 7) agree on which track a repair
    //! belongs to without duplicating the lookup.
    int firstAc3TrackIndex() const;

    void appendAudioEntry(TTAudioStream* aStream, int order=-1);

    //! Planned audio repairs (silence/interpolate fixes for detected
    //! anomalies), one flat list per AV item; each entry carries its own
    //! trackIndex() rather than being nested under the audio list itself.
    QList<TTAudioRepairItem> audioRepairList() const     { return mAudioRepairs; }
    void appendAudioRepair(const TTAudioRepairItem& item) { mAudioRepairs.append(item); }
    void removeAudioRepairAt(int index)                   { if (index >= 0 && index < mAudioRepairs.size()) mAudioRepairs.removeAt(index); }
    void clearAudioRepairs()                              { mAudioRepairs.clear(); }

    void appendSubtitleEntry(TTSubtitleStream* sStream, int order=-1);

    void appendCutEntry(int cutIn, int cutOut, int order=-1);
    void removeCutEntry(const TTCutItem& cItem);
    void updateCutEntry(const TTCutItem& cItem, int cutIn, int cutOut);

    void appendMarker(int markerPos, int order=-1);
    void removeMarker(const TTMarkerItem& cItem);

    //! Audio auto-sort (language preference resp. project order, see
    //! TTAVData::onOpenAudioFinished) runs only while the item's initial
    //! load batch is still on the thread pool. TTAVData::onThreadPoolExit()
    //! latches this flag; afterwards the track order belongs to the user.
    bool initialAudioLoadDone() const     { return mInitialAudioLoadDone; }
    void setInitialAudioLoadDone()        { mInitialAudioLoadDone = true; }

    //! Has the AC3 anomaly scan been started for THIS item yet? Latched by
    //! TTCutMainWindow when it dispatches the scan - by the automatic start
    //! after loading as well as by an explicit stream-point analysis - so
    //! the automatic start fires exactly once per item and never on top of a
    //! scan that is already running or already done (the analysis clears its
    //! own markers first, an automatic re-run would only duplicate them).
    bool anomalyScanStarted() const       { return mAnomalyScanStarted; }
    void setAnomalyScanStarted()          { mAnomalyScanStarted = true; }

  public slots:
    void onRemoveAudioItem(int index);
    void onSwapAudioItems(int oldIndex, int newIndex);
    void onAudioLanguageChanged(int index, const QString& language);
    void onAudioDelayChanged(int index, int delayMs);
    void onRemoveSubtitleItem(int index);
    void onSwapSubtitleItems(int oldIndex, int newIndex);
    void onSubtitleLanguageChanged(int index, const QString& language);
    void onSubtitleDelayChanged(int index, int delayMs);

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
    void            checkCut(int cutIn, int cutOut);

  private:
  	bool             mIsInList;
    bool             mInitialAudioLoadDone = false;
    bool             mAnomalyScanStarted   = false;
    TTVideoStream*   mpVideoStream;
    TTAudioList*     mpAudioList;
    TTSubtitleList*  mpSubtitleList;
    TTCutList*       mpCutList;
    TTMarkerList*    mpMarkerList;
    QList<TTAudioRepairItem> mAudioRepairs;
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
