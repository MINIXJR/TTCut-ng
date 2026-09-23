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
// TTCURRENTFRAME
// ----------------------------------------------------------------------------

#ifndef TTCURRENTFRAME_H
#define TTCURRENTFRAME_H

#include "ui_currentframewidget.h"

#include "../common/ttcut.h"
#include "../avstream/ttavstream.h"

class QProgressDialog;
class QStackedLayout;
class TTAVItem;
// moc must see TTAVItem complete to register TTAVItem* in its signals/slots
Q_MOC_INCLUDE("data/ttavlist.h")
class TTCutItem;
class TTMpvWrapper;
class TTPlaybackMuxTask;
struct TTPlaybackMuxParams;
class TTSubtitleStream;

class TTCurrentFrame: public QWidget, Ui::TTCurrentFrameWidget
{
  Q_OBJECT

    public:
    explicit TTCurrentFrame(QWidget* parent = 0);
    ~TTCurrentFrame();

    void setTitle(const QString & title);
    void controlEnabled(bool enabled);
    TTMPEG2Window2* videoWindow() { return mpegWindow; }
    void saveCurrentFrame();
    void setSubtitleStream(TTSubtitleStream* subtitleStream);
    void clearSubtitleStream();
    void setSubtitleDelay(int delayMs);
    // Cheap re-render of the still frame already held by mpegWindow (no
    // re-decode) so a newly wired subtitle overlay appears immediately.
    // No-op while playback is running.
    void refreshCurrentFrame();

    void wheelEvent(QWheelEvent * e);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  public slots:
    void onAVDataChanged(TTAVItem* avData);
    void onCutInChanged(const TTCutItem& cutItem);
    void onPlayVideo();
    void onPrevIFrame();
    void onNextIFrame();
    void onPrevPFrame();
    void onNextPFrame();
    void onPrevBFrame();
    void onNextBFrame();
    void onWidgetPrevFrame();
    void onWidgetNextFrame();
    void onSetMarker();
    void onGotoCutIn(int pos);
    void onGotoCutOut(int pos);
    void onGotoFrame(int pos);
    void onGotoFrame(int pos, int fast);
void onGotoFramePreview(int pos);
    void onMoveNumSteps(int);
    void onMoveToHome();
    void onMoveToEnd();

  signals:
    void newFramePosition(int);
    void setMarker(int);

  private:
    void updateCurrentPosition(int pos = -1);
    //! Show newFramePos, make it the cut position and update the label - the
    //! tail every navigation slot shares once the stream has moved.
    void navigateAndUpdate(int newFramePos);
    //! Stream index for a display frame of the playback (mpv counts display
    //! frames; MPEG-2 field pictures add extra stream entries), clamped to
    //! the stream.
    int  displayToStreamIndex(int displayIndex) const;
    //! Everything the playback mux needs, collected on the GUI thread.
    //! False (with a logged warning) when the stream has no usable frame rate.
    bool buildPlaybackMuxParams(TTPlaybackMuxParams& params);
    //! Mux the temp MKV on a worker with a cancellable progress dialog;
    //! continues in onPlaybackMuxFinished().
    void startPlaybackMux();
    //! Forget a running mux: abort it and let it discard its output.
    void detachPlaybackMux();
    //! Common load path once the source to play is known: wires the first-
    //! frame stack switch, resets the speed, passes the subtitle file and
    //! flips the buttons; the caller then calls mPlayer->load().
    void beginPlayerLoad();
    void startPlaybackFromTempMkv();
    void cleanupTempPlaybackFile();
    QString playbackSourceFingerprint() const;
    // Playback time<->index conversion authority (display-PTS aware).
    double playbackSecondsForCurrentStill() const;
    void ensurePlayerCreated();
    void realizeRenderContext();

  private:
    void                clearCutContext();
    void                setPlayingButtonState(bool playing);

  private slots:
    void                onPlaybackMuxFinished();
    void                onPlaybackMuxAborted();
    void                onPlaybackFinished();
    void                onPlaybackPositionChanged(double seconds);
    void                onPlaySlower();
    void                onPlayFaster();

  private:
    void                applySpeedStep();

  private:
    bool                isControlEnabled;
    TTVideoStream*      videoStream;
    TTAVItem*           mAVItem;
    TTAVItem*           currentCutAVItem;
    int                 currentCutItemIndex;
    int                 currentCutPosition;
    TTMpvWrapper*       mPlayer = nullptr;
    //! True once realizeRenderContext() built the mpv render context.
    bool                mRenderContextRealized = false;
    QString             mTempPlaybackFile;  // Temp MKV for H.264/H.265 playback (cached across STOP→PLAY)
    QString             mCachedPlaybackFingerprint;  // source fingerprint of the cached temp MKV
    // True when the cached temp MKV carries real display PTS (source
    // display-order map was passed to the muxer). ALL playback time<->index
    // conversions key off this flag - no mixed-scale states possible.
    bool                mTempPlaybackHasDisplayPts = false;
    //! The mux preparing the next playback, null when none runs. Deletes
    //! itself on finished()/aborted(); this pointer is dropped in the two
    //! completion slots and in detachPlaybackMux().
    TTPlaybackMuxTask*  mMuxTask = nullptr;
    QProgressDialog*    mMuxProgress = nullptr;
    QString             mPendingPlaybackFingerprint;  // fingerprint of the mux in flight
    int                 mSpeedStep = 2;     // Index into kSpeedSteps[]; 2 = kSpeedStepNormal (1×)
    QWidget*            mFrameStackContainer = nullptr;
    QStackedLayout*     mFrameStack = nullptr;
};

#endif //TTCURRENTFRAME_H
