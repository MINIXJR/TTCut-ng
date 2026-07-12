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
// TTAVDEODATA
// ----------------------------------------------------------------------------

#ifndef TTAVDATA_H
#define TTAVDATA_H

#include <QThread>
#include <QObject>
#include <QList>
#include <QListIterator>
#include <QMap>
#include <QSet>
#include <QPair>
#include <functional>

#include "ttcutlist.h"
#include "ttmarkerlist.h"
#include "ttavlist.h"
#include "ttstreampoint.h"

#include <QMessageBox>
#include "ttcutprojectdata.h"

class TTThreadTaskPool;
class TTThreadTask;
class TTMessageLogger;
class TTAVStream;
class TTAudioStream;
class TTVideoStream;
class TTESInfo;
class TTOpenVideoTask;
class TTOpenAudioTask;
class TTOpenSubtitleTask;
class TTSubtitleStream;
class TTCutPreviewTask;
class TTCutVideoTask;
class TTCutSubtitleTask;
class TTCutProjectData;
class TTMuxListData;
class TTMuxListDataItem;

/* /////////////////////////////////////////////////////////////////////////////
 * TTAVData
 */
class TTAVData : public QObject
{
  Q_OBJECT

  public:
    TTAVData();
    ~TTAVData();

    void clear();

    // Re-emit cutDataReloaded so external observers (e.g. cut list view) can
    // refresh after a deferred state change (audio streams ready, marker
    // imported). Replaces direct external `emit mpAVData->cutDataReloaded()`
    // which is deprecated in Qt 5+.
    void emitCutDataReloaded()  { emit cutDataReloaded(); }

    void      openAVStreams(const QString& videoFilePath);
    void      writeProjectFile(const QFileInfo& fInfo,
                               const QList<TTStreamPoint>& streamPoints = QList<TTStreamPoint>(),
                               const TTLogoProjectData& logoData = TTLogoProjectData());
    void      readProjectFile(const QFileInfo& fInfo);

    void      appendAudioStream(TTAVItem* avItem, const QFileInfo& fInfo, int order=-1);
    void      appendSubtitleStream(TTAVItem* avItem, const QFileInfo& fInfo, int order=-1);

    void      appendCutEntry(TTAVItem* avItem, int cutIn, int cutOut);
    void      copyCutEntry(const TTCutItem& cutItem);
    void      sortCutItemsByOrder();

    void      sortMarkerByOrder();

    TTAVItem* avItemAt(int index)         { return mpAVList->at(index); }
    int       avCount()                   { return mpAVList->count(); }
    int       avIndexOf(TTAVItem* item)   { return mpAVList->indexOf(item); }

    TTCutItem cutItemAt(int index)        { return mpCutList->at(index); }
    int       cutIndexOf(const TTCutItem& item) { return mpCutList->indexOf(item); }
    int       cutCount()                  { return mpCutList->count(); }

    TTMarkerItem markerAt(int index)                     { return mpMarkerList->at(index); }
    int          markerCount()                           { return mpMarkerList->count(); }


    TTAVItem* doOpenVideoStream(const QString& filePath, int order=-1);
    void      doOpenAudioStream(TTAVItem* avItem, const QString& filePath, int order=-1);
    void      doOpenSubtitleStream(TTAVItem* avItem, const QString& filePath, int order=-1);

    void      setPendingAudioLanguage(TTAVItem* avItem, int order, const QString& lang);
    void      setPendingAudioDelay(TTAVItem* avItem, int order, int delayMs);
    void      setPendingSubtitleLanguage(TTAVItem* avItem, int order, const QString& lang);
    void      doCutPreview(TTCutList* cutList);

    int       totalProcess() const;
    QTime     totalTime() const;

    //TODO: just for testing purpose
    TTThreadTaskPool* threadTaskPool() const;
    TTCutList*        cutList() const;
    // Headless mode (--auto-cut): suppress interactive confirmation dialogs;
    // warnings go to TTMessageLogger and the cut proceeds ("Cut anyway").
    void setNonInteractive(bool v) { mNonInteractive = v; }




  public slots:
    void onChangeCurrentAVItem(int index);
    void onChangeCurrentAVItem(TTAVItem* avItem);

    void onRemoveAVItem(int index);
    void onSwapAVItems(int oldIndex, int newIndex);

    void onRemoveCutItem(const TTCutItem& item);
    void onCutOrderChanged(int, int);

    void onAppendMarker(int);
    void onRemoveMarker(const TTMarkerItem& mItem);

    void onDoFrameSearch(TTAVItem* avItem, int startIndex);
    void onCurrentFramePositionChanged(int position);

    void onUserAbortRequest();

    void onDoCut(QString tgtFileName, TTCutList* cutList, bool audioOnly = false);
    void onCutFinished();
    void onCutAborted();


  private slots:
    void onOpenVideoFinished(TTAVItem* avItem, TTVideoStream* vStream, int order, const QString& demuxedAudio);
    void onOpenAVStreamsAborted();

    void onOpenAudioFinished(TTAVItem* avItem, TTAudioStream* aStream, int order);
    void onOpenAudioAborted(TTAVItem* avItem);

    void onOpenSubtitleFinished(TTAVItem* avItem, TTSubtitleStream* sStream, int order);
    void onOpenSubtitleAborted(TTAVItem* avItem);

    void onCutPreviewFinished(TTCutList* cutList);
    void onCutPreviewAudioDrift(const QList<float>& driftsMs);
    void onCutPreviewAborted();

    void onReadProjectFileFinished();
    void onReadProjectFileAborted();

    void onThreadPoolInit();
    void onThreadPoolExit();

    void onStatusReport(int state, const QString& msg, quint64 value);
    void onMuxProgress(int percent, const QString& msg);

  signals:
    void threadPoolExit();
    void statusReport(int state, const QString& msg, quint64 value);
    void statusReport(TTThreadTask* task, int state, const QString& msg, quint64 value);
    void dataReady();

    void readProjectFileFinished(const QString&);
    void streamPointsLoaded(const QList<TTStreamPoint>& points);
    void logoDataLoaded(const TTLogoProjectData& logoData);
    void vdrMarkersLoaded(const QList<TTStreamPoint>& points);

    void avItemAppended(const TTAVItem& item);
    void avItemRemoved(int index);
    void avItemsSwapped(int oldIndex, int newIndex);
    void avItemUpdated(const TTAVItem& cItem, const TTAVItem& uItem);
    void avDataReloaded();
    void currentAVItemChanged(TTAVItem* avData);

    void cutItemAppended(const TTCutItem& item);
    void cutItemRemoved(int index);
    void cutOrderUpdated(const TTCutItem& item, int order);
    void cutItemUpdated(const TTCutItem& citem, const TTCutItem& uitem);
    void cutDataReloaded();

    void markerAppended(const TTMarkerItem& item);
    void markerRemoved(int index);
    void markerUpdated(const TTMarkerItem& item, int order);
    void markerUpdated(const TTMarkerItem& citem, const TTMarkerItem& uitem);
    void markerDataReloaded();

    void foundEqualFrame(int index);
    void cutPreviewFinished(TTCutList* cutList);
    void cutAudioDriftCalculated(const QList<float>& driftsMs);
    void cutFinished();

  private:
    TTAVItem*      createAVItem();
    TTAVList*      videoDataList() { return mpAVList; }
    QFileInfoList  getAudioNames(const QFileInfo& vFileInfo);
    QFileInfoList  getSubtitleNames(const QFileInfo& vFileInfo);
    QString        createCutFileName(QString cutBaseFileName, QString sourceFileName, int index);
    void           deleteElementaryStreams(const QString& videoFilePath,
                                           const QStringList& audioFilePaths,
                                           const QStringList& subtitleFilePaths = QStringList());
    void           doH264Cut(QString tgtFileName, TTCutList* cutList);
    void           doAudioOnlyCut(QString tgtFileName, TTCutList* cutList);
    // Classify the .info doubled-PTS clusters against the MPEG-2 parser's
    // field-pair list and show the warning dialog (or import silently when all
    // clusters are confirmed field pairs). Called from onOpenVideoFinished for
    // freshly-opened items only. Also refreshes mAudioGapIndices from esInfo.
    void           showExtraFrameClusterDialog(TTAVItem* avItem, TTVideoStream* vStream,
                                               const TTESInfo& esInfo);

  private:
  	TTThreadTaskPool* mpThreadTaskPool;
    TTMessageLogger*  log;
    TTAVItem*         mpCurrentAVItem;
    TTAVList*         mpAVList;
    TTCutList*        mpCutList;
    bool mNonInteractive = false;  // --auto-cut: no modal dialogs
    TTMarkerList*     mpMarkerList;
    TTMuxListData*    mpMuxList;
    TTOpenVideoTask*    openVideoTask;
    TTOpenAudioTask*    openAudioTask;
    TTOpenSubtitleTask* openSubtitleTask;
    TTCutPreviewTask*   cutPreviewTask;
    TTCutVideoTask*   cutVideoTask;
    TTCutSubtitleTask* cutSubtitleTask;
    TTCutProjectData* mpProjectData;
    int               mCurrentFramePosition;  // Track Current Frame widget position for frame search

    // Pending VDR markers to be converted to cut entries after video stream is loaded
    // Key: TTAVItem*, Value: List of (cutIn, cutOut) pairs
    QMap<TTAVItem*, QList<QPair<int, int>>> mpPendingVdrMarkers;

    // AVItems opened fresh (via openAVStreams) that should show the extra-frame
    // cluster dialog once the video stream — and thus the MPEG-2 parser's
    // field-pair list — is built (in onOpenVideoFinished). Project reload
    // bypasses openAVStreams, so it never gets an entry here (no dialog on
    // reload). Mirrors mpPendingVdrMarkers.
    QSet<TTAVItem*> mpPendingExtraFrameDialog;

    // A/V sync offset in milliseconds (from .info file, used during muxing)
    int mAvSyncOffsetMs;

    // Extra frame indices from PTS analysis (sorted, for audio time correction)
    QList<int> mExtraFrameIndices;

    // Audio gap frame indices (sorted) — for marker visualization only.
    // NOT used for audio cut time correction (separate from mExtraFrameIndices).
    QList<int> mAudioGapIndices;

    // Pending language overrides from project file (applied after async stream open)
    QMap<QPair<TTAVItem*, int>, QString> mPendingAudioLanguages;
    QMap<QPair<TTAVItem*, int>, QString> mPendingSubtitleLanguages;

    // Pending delay overrides from project file (applied after async stream open)
    QMap<QPair<TTAVItem*, int>, int> mPendingAudioDelays;

    // Last-cut metadata (set by the cut path, read by the completion dialog)
    bool    mLastCutWasAudioOnly = false;
    QString mLastCutOutputSummary;

  public:
    // Count extra frames before a given frame index (for audio time correction)
    int countExtraFramesBefore(int frameIndex) const;
    const QList<int>& extraFrameIndices() const { return mExtraFrameIndices; }

    // Burst detection result for a single cut boundary, after threshold filter.
    struct CutBurstInfo {
      bool   present  = false;
      double burstDb  = 0.0;
      double contextDb = 0.0;
    };
    // Detect audio bursts at cut boundaries using extra-frame-corrected probe times.
    CutBurstInfo detectCutOutBurst(const TTCutItem& item) const;
    bool confirmBurstWarnings(TTCutList* cutList);

    CutBurstInfo detectCutInBurst(const TTCutItem& item)  const;

    // Audio-cut plan with audio-frame-boundary snapping and feed-forward drift
    // compensation. keepList holds (startTime, endTime) pairs in seconds whose
    // boundaries align with the source audio's frame grid; cutAudioStream's
    // skip/stop rules then keep exactly the planned frames per segment.
    // drifts holds the cumulative A/V offset in ms after each segment (audio
    // length minus video length, sum of all preceding segments). Bounded to
    // ±½ audio-frame in steady state.
    struct AudioCutPlan {
      QList<QPair<double, double>> keepList;
      QList<float>                 drifts;
    };
    // Plan from a video-domain keep list: (startTime, endTime) per segment in
    // seconds (already extra-frame-corrected, B-frame-adjusted, etc., but
    // without per-track audio delay). Adds the delay and snaps to audio-frame
    // boundaries with feed-forward.
    AudioCutPlan planAudioCut(TTAudioStream* audioStream,
                              const QList<QPair<double, double>>& videoKeepList,
                              int delayMs) const;

    // Build a video-domain keep list (seconds) from cut indices, applying the
    // extra-frame correction: (index - extraBefore)/fps, cut-out uses index+1.
    // Single home for a conversion previously open-coded in >= 6 places.
    QList<QPair<double, double>> buildVideoKeepList(TTCutList* cutList,
                                                    double frameRate) const;

    // Cut the given audio tracks of avItem against videoKeepList. Encapsulates
    // the per-track loop, per-track delay, planAudioCut (audio-frame snapping +
    // feed-forward drift), AC3 acmod target computation, and cutAudioStream.
    // Codec-neutral: only forwards normalizeAcmod (codec-specific normalization
    // lives inside cutAudioStream). outPath names the per-track output file;
    // onCut registers it (mux list / file list / preview). Returns the first
    // requested track's drifts for the caller's drift signal.
    QList<float> cutAudioTracks(
        TTAVItem* avItem,
        const QList<int>& trackIndices,
        const QList<QPair<double, double>>& videoKeepList,
        bool normalizeAcmod,
        const std::function<QString(int trackIdx, const QString& ext)>& outPath,
        const std::function<void(int trackIdx, const QString& path,
                                 const QString& lang, bool ok)>& onCut);

    // Last-cut metadata so the main window can build a meaningful completion
    // message after an audio-only cut (where cutVideoName + container extension
    // do not point at the actual output file).
    bool    lastCutWasAudioOnly()  const { return mLastCutWasAudioOnly; }
    QString lastCutOutputSummary() const { return mLastCutOutputSummary; }

  private:
    // AC3-only per-segment target acmod list (majority acmod per kept window).
    // Empty unless normalizeAcmod && ext == "ac3".
    QList<int> computeTargetAcmods(const QString& audioFile, const QString& ext,
                                   const QList<QPair<double, double>>& keepList,
                                   bool normalizeAcmod) const;
};


#endif //TTAVDATA_H
