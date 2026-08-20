/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTAUDIOREPAIRDIALOG_H
#define TTAUDIOREPAIRDIALOG_H

#include <QDialog>
#include <QList>
#include <QtGlobal>

#include "../data/ttstreampoint.h"

class QCheckBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class TTAVItem;
class TTMpvWrapper;

// Repair dialog for one AudioAnomaly marker (audio-anomaly-repair Task 7,
// design Komponente 4: docs/superpowers/specs/2026-08-19-audio-anomaly-
// repair-design.md). Lets the user narrow the channel mask and AC3-frame
// range the scanner proposed, audition before/after via mpv, jump the main
// window to the marker's video frame for visual context, and on Accept
// write (or update) exactly one TTAudioRepairItem on the given TTAVItem.
//
// Code-based (no .ui file) - deliberately sidesteps this project's AUTOUIC-
// stash-race pitfall (docs/code-map note reference_autouic_stash_race.md).
//
// If a TTAudioRepairItem on trackIndex already overlaps the marker's
// (approximate) AC3-frame range, the dialog edits that entry in place
// instead of creating a second one - this is what makes the same class
// serve both the "Repair..." and "Edit repair..." context-menu actions.
class TTAudioRepairDialog : public QDialog
{
  Q_OBJECT

public:
  // extraFrameIndices: same sorted display-index list TTAVData exposes via
  // extraFrameIndices() (MPEG-2 field-picture extras) - needed to invert
  // TTAudioAnomalyScanTask::videoFrameForTime() correctly (review fix 1:
  // ignoring it can be off by seconds on real DVB material, e.g. 273
  // extras / ~10.9s on the corpus's "Benders" example - see docs/code-map/
  // audio-cut-timing.md, "Bekannte Fallstricke"). Empty is allowed (no
  // extras / caller cannot reach TTAVData) and behaves like the old
  // uncorrected frameIndex/frameRate math.
  TTAudioRepairDialog(TTAVItem* avItem, const TTStreamPoint& point,
                       int trackIndex, const QList<int>& extraFrameIndices,
                       QWidget* parent = nullptr);
  ~TTAudioRepairDialog() override;

  // AC3 frame range of a marker, BOTH BOUNDS INCLUSIVE (the convention
  // TTAudioRepairItem uses).
  //
  // Exact when the marker carries the scanner's own range
  // (TTStreamPoint::hasAudioFrameRange(), final review I3) - that is the
  // normal case for an AudioAnomaly finding and for one restored from a
  // project file written by this version.
  //
  // Otherwise: approximate range implied by the marker's video frameIndex/
  // duration, using the fixed 32 ms AC3-frame contract this project already
  // relies on wherever the audio file itself is not open (see
  // TTAudioRepairItem's header comment and TTAudioAnomalyScanTask's
  // kFrameDurSec). Deliberately NOT stream-derived: it exists only so a
  // context menu can decide "does a repair already cover this marker?"
  // without opening the audio file on every right-click. The dialog itself,
  // once constructed, still reads the real per-file AC3 frame duration for
  // everything it actually writes (ms<->frame conversion, preview window).
  //
  // Still extras-corrected (review fix 1): the marker's frameIndex is a
  // VIDEO display index, and TTAudioAnomalyScanTask::videoFrameForTime()
  // (which produced it) already folds in extraFrameIndices - skipping that
  // correction here would silently reintroduce the same seconds-scale
  // error this fix addresses in the dialog's own ms prefill.
  static void approxAc3RangeForMarker(const TTStreamPoint& point, double frameRate,
                                       const QList<int>& extraFrameIndices,
                                       qint64& frameFrom, qint64& frameTo);

  // Test/inspection accessors for the "model" part of this dialog (Task 7
  // brief Step 1: prefill + accept() must be verifiable offscreen without a
  // human eye/ear). Not used by production code, which only ever calls
  // exec()/accept()/reject().
  QSpinBox* startSpinBoxForTest() const  { return mSpinFrom; }
  QSpinBox* endSpinBoxForTest() const    { return mSpinTo; }
  QCheckBox* channelCheckBoxForTest(int channel) const { return mChkChannel[channel]; }
  // Review fix 2 (round 2): drives playFile() directly, bypassing
  // writePreviewWindow()'s own file-existence pre-check, so a harness can
  // provoke a genuine mpv-level load failure (nonexistent/unreadable path)
  // without needing a real repair range/AVItem set up first.
  void playFileForTest(const QString& path) { playFile(path); }

signals:
  // Relayed by the widget that owns this dialog into its own jumpToFrame
  // signal, which TTCutMainWindow already connects to onStreamPointJump -
  // no separate main-window wiring needed for navigation (see
  // TTStreamPointWidget::onContextMenu).
  void jumpToFrameRequested(int frameIndex);

private slots:
  void onPlayOriginal();
  void onPlayRepaired();
  void onGotoFrame();
  void onMpvError(const QString& message);
  // Clears mAwaitingPlaybackStart - see that member's doc comment (review
  // fix 2, round 2). Connected to TTMpvWrapper::playbackRestarted, NOT
  // playerPlaying: playerPlaying only ever fires once per TTMpvWrapper
  // instance (from TTMpvLibBackend's one-shot, idempotent start()/
  // connected() handshake - verified by reading ttmpvlibbackend.cpp), so a
  // second Play click on the same dialog would never clear the flag again.
  // playbackRestarted comes straight from mpv's MPV_EVENT_PLAYBACK_RESTART,
  // which fires once per load()/loadfile call when that specific file's
  // initial seek completes and its first frame is ready - a genuine
  // per-load "this attempt is actually playing" confirmation, and it does
  // NOT fire for a failed load (that goes out through mpvError/END_FILE-
  // error instead). See task-7-report.md's Fix 2 round 2 for the offscreen-
  // vs-real-display verification this relies on.
  void onPlaybackConfirmed();

public slots:
  void accept() override;

private:
  void buildUi();
  // Creates mPlayer (and starts the mpv backend via renderWidget()). Called
  // from the CONSTRUCTOR on every platform except QT_QPA_PLATFORM=offscreen,
  // which gives mpv no GL context (see tools/diag/test_mainwindow_then_cut
  // .cpp's "mpv gets no GL context" note) - constructing the dialog itself
  // (prefill, accept()) must stay usable offscreen for
  // test_repairdialog_model.
  //
  // Building it up front is not cosmetic: adding a QOpenGLWidget to a dialog
  // whose exec() loop is already running terminates that loop (Qt recreates
  // the native window and calls hide(); QDialogPrivate::hide_helper() exits
  // exec() on any hide()). See the Critical-1 comment in the constructor.
  // Idempotent, and still called from playFile() so the offscreen path keeps
  // its old lazy behaviour.
  void ensurePlayer();
  qint64 currentFrameFrom() const;
  qint64 currentFrameTo() const;
  quint8 currentChannelMask() const;
  // Writes a +/-3s window copy of the source AC3 around the current
  // [from,to] range to TTSettings::tempDirPath(), optionally substituting
  // buildRepairTable()'s replacement bytes for frames inside that range.
  // Byte-copies whole AC3 frames as read via avformat packet iteration
  // (one packet == one AC3 frame for this codec, see extern/
  // ttaudiorepair.cpp) - never assumes a fixed byte frame size, since that
  // depends on the source bit rate (384/448 kbit/s both occur in the
  // corpus). Returns the written path, or an empty string + *error.
  QString writePreviewWindow(bool repaired, QString* error);
  void playFile(const QString& path);

  TTAVItem*     mAvItem;
  TTStreamPoint mPoint;
  int           mTrackIndex;
  QList<int>    mExtraFrameIndices;        // review fix 1: needed to invert videoFrameForTime()
  QString       mAudioFile;
  double        mFrameDurationMs;          // real AC3 frame duration, derived from the opened stream
  int           mExistingRepairIndex = -1; // index into mAvItem->audioRepairList(), -1 = new item
  // Review fix 2 (round 2): true from a Play click until either
  // onPlaybackConfirmed() (TTMpvWrapper::playbackRestarted, a per-load
  // signal - see that slot's doc comment) confirms THIS load actually
  // started playing, or a QMessageBox has already been shown for the
  // current failed attempt. TTMpvLibBackend forwards EVERY mpv "error"-
  // level log line as playerError - most are transient/non-fatal noise
  // during otherwise-fine playback (see TTCutPreview::onPlayerError's and
  // TTCurrentFrame's identical log-only handling). Only an error that
  // arrives before playback ever started for this attempt is a genuine
  // "audition failed" per the spec's Fehlerbild point 3.
  bool          mAwaitingPlaybackStart = false;
  // Review fix (Minor): unique per dialog instance so two dialogs open at
  // once (or a stale file from a crashed prior run) never collide - see
  // reference_shared_tempdir_parallel_runs.md. Removed in the destructor.
  QString       mInstanceId;
  QString       mPreviewPathBefore;
  QString       mPreviewPathAfter;

  QLabel*      mLblHeader      = nullptr;
  QCheckBox*   mChkChannel[6]  = {};       // order: FL FR C LFE SL SR
  QSpinBox*    mSpinFrom       = nullptr;
  QSpinBox*    mSpinTo         = nullptr;
  QPushButton* mBtnPlayOriginal = nullptr;
  QPushButton* mBtnPlayRepaired = nullptr;
  QPushButton* mBtnGotoFrame    = nullptr;
  QVBoxLayout* mMainLayout      = nullptr;
  TTMpvWrapper* mPlayer         = nullptr;
};

#endif // TTAUDIOREPAIRDIALOG_H
