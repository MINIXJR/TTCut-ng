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
#include <atomic>

#include "ttcutlist.h"
#include "ttmarkerlist.h"
#include "ttavlist.h"
#include "ttstreampoint.h"
#include "../common/ttprogressestimator.h"

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
class TTH26xCutTask;
class TTAudioOnlyCutTask;
class TTMuxTask;
class TTMplexProvider;
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
    void onMpeg2MuxFinished();
    void onH26xCutFinished();
    void onAudioOnlyCutFinished();
    void onCutAborted();

    // Status forwarding for engines that report from a worker thread
    // (TTH26xCutTask) as well as for the synchronous GUI-thread providers.
    void onStatusReport(int state, const QString& msg, quint64 value);
    void onMuxProgress(int percent, const QString& msg);


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

    //! Emitted before a cut operation's Init: the planned stage sequence
    //! with work amounts, for the progress estimator.
    void operationPlanReady(const QVector<TTStagePlan>& plan);

  private:
    TTAVItem*      createAVItem();
    TTAVList*      videoDataList() { return mpAVList; }
    QFileInfoList  getAudioNames(const QFileInfo& vFileInfo);
    QFileInfoList  getSubtitleNames(const QFileInfo& vFileInfo);
    void           deleteElementaryStreams(const QString& videoFilePath,
                                           const QStringList& audioFilePaths,
                                           const QStringList& subtitleFilePaths = QStringList());
    //! Close the MPEG-2 cut operation: reset mCutOperationActive, emit the
    //! single final Exit bracket and cutFinished(). Called inline by
    //! onCutFinished()'s mplex/Elementary branches and by onMpeg2MuxFinished()
    //! for the MKV branch, whose mux is a second pool run.
    void           finishMpeg2Cut();
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
    //! The cut list the RUNNING operation was started with. onDoCut() takes a
    //! list as a parameter and only falls back to mpCutList when none is given
    //! - so the two are not the same thing, and onCutFinished() must not read
    //! mpCutList to describe what was just cut. Owned by the caller; valid for
    //! the duration of one operation.
    TTCutList*        mpRunningCutList = nullptr;
    bool mNonInteractive = false;  // --auto-cut: no modal dialogs
    TTMarkerList*     mpMarkerList;
    TTMuxListData*    mpMuxList;
    TTOpenVideoTask*    openVideoTask;
    TTOpenAudioTask*    openAudioTask;
    TTOpenSubtitleTask* openSubtitleTask;
    TTCutPreviewTask*   cutPreviewTask;
    TTCutVideoTask*   cutVideoTask;
    TTH26xCutTask*    mpH26xCutTask = nullptr;
    //! Audio-only cut, running as a single pool task. Non-null only between
    //! doAudioOnlyCut() and onAudioOnlyCutFinished()/onCutAborted() - same
    //! lifetime shape as mpH26xCutTask.
    TTAudioOnlyCutTask* mpAudioOnlyCutTask = nullptr;
    //! MKV mux of the MPEG-2 cut, running as the operation's second pool task.
    //! Non-null only between onCutFinished() and onMpeg2MuxFinished()/
    //! onCutAborted().
    TTMuxTask*        mpMuxTask = nullptr;
    //! mplex mux of an MPG-output MPEG-2 cut. Unlike every other cut engine
    //! this one is NOT a pool task: it drives an external process
    //! synchronously on the GUI thread inside onCutFinished(), so
    //! mpThreadTaskPool->onUserAbortRequest() has nothing to deliver a cancel
    //! to. Published here for exactly the duration of that mplexPart() call so
    //! onUserAbortRequest() can reach it; null at every other moment. GUI
    //! thread only - onUserAbortRequest() runs re-entrantly from mplexPart()'s
    //! own qApp->processEvents(), never from another thread.
    TTMplexProvider*  mpMplexProvider = nullptr;
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
    // Empty means the cut succeeded. Set on every failing exit so the
    // completion notification can tell success from failure — without it a
    // failed cut still reported "finished successfully".
    QString mLastCutError;
    qint64  mLastCutSourceMs = 0;
    qint64  mLastCutResultMs = 0;

    //! How a cut operation ended.
    enum class CutOutcome { Success, Failed, Cancelled };

    //! Closes a cut operation: records the outcome, then reports it.
    //!
    //! The order is the point. TTCutMainWindow::onStatusReport reads
    //! lastCutError() while handling Exit to decide whether the run was
    //! regular, and TTProgressEstimator writes a calibration factor for a
    //! regular one. Setting the field after the signal - which the H.26x path
    //! did - makes every failed cut look regular and teaches the estimator
    //! from a broken run.
    //!
    //! Does NOT touch mCutOperationActive: which path sets and which consumes
    //! that flag is delicate (see docs/code-map/progress-reporting.md) and
    //! stays with the caller.
    //!
    //! message   - text of the closing bracket (progress window).
    //! errorText - text for mLastCutError (error dialog); when empty, the
    //!             bracket text is used, which is what callers with only one
    //!             wording pass. Some engines (e.g. TTH26xCutTask) keep a
    //!             longer error-dialog text separate from the shorter
    //!             progress-window text - pass both so neither is lost.
    void finishCutOperation(CutOutcome outcome, const QString& message,
                             const QString& errorText = QString());

    // True while a final cut is running (onDoCut MPEG-2 branch until
    // onCutFinished/onCutAborted, doH264Cut until onH26xCutFinished).
    // Suppresses the thread pool's own
    // Init/Exit status brackets: the pool is an inner stage of the cut
    // (audio is cut before it, muxing runs after it), and its brackets
    // would reset resp. prematurely finish the progress dialog.
    bool mCutOperationActive = false;

    // True while the MPEG-2 cut's MKV mux run (the operation's second pool
    // run) is in flight. Consumed by onThreadPoolExit(), which uses it to
    // skip the per-pool-run avDataReloaded() for that run: the mux changes no
    // AV data, and the views must reload once per operation.
    // Unlike mSyncPhaseAbort and mCutProducedFiles, this one is NOT re-cleared
    // at the cut entry points: its "false between operations" invariant is
    // global rather than local. It holds because it is set immediately before
    // the pool start and consumed by a slot connected in the constructor, and
    // TTThreadTaskPool always emits exit() when its queue drains — on the
    // finished route and on the aborted one alike. Measured across operations
    // (a leaked flag would swallow the next operation's avDataReloaded(); the
    // two operations following a two-pool-run MPEG-2 cut both reported one).
    bool mMuxPoolRunActive = false;

    // Set by onUserAbortRequest(), polled by the MPEG-2 branch of onDoCut()
    // during its synchronous audio/subtitle phase (before the pool -- and
    // thus TTThreadTaskPool::onUserAbortRequest()'s own abort delivery --
    // has anything to cancel). qApp->processEvents() inside that phase's
    // progress callbacks lets a queued Cancel click reach
    // onUserAbortRequest() re-entrantly while cutAudioTracks/cutSubtitleTracks
    // are still on the stack. Reset at the top of onDoCut()/doH264Cut()/
    // doAudioOnlyCut() so a stale request from a previous operation can't
    // kill the next one. std::atomic to match the shouldAbort predicate
    // shape TTFFmpegWrapper::cutAudioStream polls (Task 3); the write and
    // every read happen on the GUI thread, nested via processEvents(), not
    // across threads.
    std::atomic<bool> mSyncPhaseAbort { false };

    // Files the running MPEG-2 cut has created (cut audio/subtitle tracks and
    // the video ES). An abort deletes all of them - the user's decision for
    // this feature is "on abort, delete everything the run created", and after
    // a cancel none of these is useful on its own. Filled in onDoCut(),
    // consumed by the abort paths (onDoCut's own sync-phase block,
    // onCutAborted) or handed over to the mux task
    // (TTMuxTaskParams::cleanupOnAbort) when that run starts, and cleared by
    // finishMpeg2Cut() on the success path. GUI thread only.
    QStringList mCutProducedFiles;

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

    //! Builds "<cutBase>_NNN.<ext>" inside TTSettings::cutDirPath(). Used for
    //! per-track audio and subtitle output filenames. Public (rather than
    //! private, as it used to be) so TTAudioOnlyCutTask's worker thread can
    //! call it too - it is pure aside from reading the TTSettings singleton,
    //! the same thing cutAudioTracks/cutSubtitleTracks already do from there.
    QString createCutFileName(QString cutBaseFileName, QString sourceFileName, int index);

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
                                 const QString& lang, bool ok)>& onCut,
        // Optional hook run just before each track is cut (progress/UI). Keeps
        // outPath a pure path computation; existing output is deleted centrally.
        const std::function<void(int trackIdx)>& beforeCut = {},
        // Optional per-track progress hook (0..100), forwarded to
        // cutAudioStream's own progress callback.
        const std::function<void(int trackIdx, int percent)>& onProgress = nullptr,
        // Optional abort predicate, polled once per track before it starts
        // and forwarded into cutAudioStream's own in-loop poll.
        const std::function<bool()>& shouldAbort = {});

    // Convenience overload for the common case: cut ALL of avItem's audio
    // tracks. Builds the all-tracks index list and forwards to the overload
    // above.
    QList<float> cutAudioTracks(
        TTAVItem* avItem,
        const QList<QPair<double, double>>& videoKeepList,
        bool normalizeAcmod,
        const std::function<QString(int trackIdx, const QString& ext)>& outPath,
        const std::function<void(int trackIdx, const QString& path,
                                 const QString& lang, bool ok)>& onCut,
        const std::function<void(int trackIdx)>& beforeCut = {},
        const std::function<void(int trackIdx, int percent)>& onProgress = nullptr,
        const std::function<bool()>& shouldAbort = {});

    //! Cut the given subtitle tracks of avItem against the video keep list
    //! (seconds, end-exclusive). Synchronous, no task pool, no MPEG-2
    //! sequence-end trailer. outPath names the target file per track;
    //! onCut reports path/language/success per track.
    void cutSubtitleTracks(
        TTAVItem* avItem,
        const QList<int>& trackIndices,
        const QList<QPair<double, double>>& keepList,
        const std::function<QString(int trackIdx)>& outPath,
        const std::function<void(int trackIdx, const QString& path,
                                 const QString& lang, bool ok)>& onCut);

    // Convenience overload for the common case: cut ALL of avItem's subtitle
    // tracks. Builds the all-tracks index list and forwards to the overload
    // above.
    void cutSubtitleTracks(
        TTAVItem* avItem,
        const QList<QPair<double, double>>& keepList,
        const std::function<QString(int trackIdx)>& outPath,
        const std::function<void(int trackIdx, const QString& path,
                                 const QString& lang, bool ok)>& onCut);

    // Last-cut metadata so the main window can build a meaningful completion
    // message after an audio-only cut (where cutVideoName + container extension
    // do not point at the actual output file).
    bool    lastCutWasAudioOnly()  const { return mLastCutWasAudioOnly; }
    QString lastCutOutputSummary() const { return mLastCutOutputSummary; }
    QString lastCutError()         const { return mLastCutError; }
    qint64  lastCutSourceMs()      const { return mLastCutSourceMs; }
    qint64  lastCutResultMs()      const { return mLastCutResultMs; }

  private:
    // AC3-only per-segment target acmod list (majority acmod per kept window).
    // Empty unless normalizeAcmod && ext == "ac3".
    QList<int> computeTargetAcmods(const QString& audioFile, const QString& ext,
                                   const QList<QPair<double, double>>& keepList,
                                   bool normalizeAcmod) const;
    // Source (at(0) video duration) + result (Σ kept-segment lengths) in ms
    // from the cut list, into mLastCutSourceMs/mLastCutResultMs (0 if unknown).
    void computeCutLengths(TTCutList* cutList);
};


#endif //TTAVDATA_H
