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
// TTCUTPREVIEW
// ----------------------------------------------------------------------------

#include "../common/ttexception.h"
#include "ttthemedicon.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "ttcutpreview.h"
#include "../avstream/ttaspectwindow.h"
#include "../avstream/ttmpeg2videoheader.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../data/ttcutpreviewtask.h"
#include "../data/ttpreviewclip.h"
#include "../avstream/ttavtypes.h"

#include "ttmpvwrapper.h"
#include "ttmpvrenderwidget.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>

/* /////////////////////////////////////////////////////////////////////////////
 * TTCutPreview constructor
 */
TTCutPreview::TTCutPreview(QWidget* parent, int prevW, int prevH)
  : QDialog(parent)
{
  setupUi(this);

  mPlayer = new TTMpvWrapper(this);
  // In the preview mpv IS the display: its render widget sits in videoFrame,
  // and there is no still-image fallback like TTCurrentFrame's mpegWindow.
  // Without keep-open mpv unloads the file at EOF and the picture is gone;
  // that alone is why onPlayerFinished() used to reload, snapping back to
  // frame 0. Must precede renderWidget(), which starts the backend.
  mPlayer->setKeepOpen(true);
  if (QWidget* rw = mPlayer->renderWidget()) {
    rw->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (QLayout* fl = videoFrame->layout())
      fl->addWidget(rw);
  }

  setObjectName("TTCutPreview");

  // set desired video width x height
  previewWidth  = prevW;
  previewHeight = prevH;

  cbCutPreview->setEditable( false );
  cbCutPreview->setInsertPolicy( QComboBox::InsertAfterCurrent );

  // Use theme icons with Qt standard icon fallback for cross-platform support
  pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));
  pbExit->setIcon(ttThemedIcon("window-close", QStyle::SP_DialogCloseButton));
  pbPrevCut->setIcon(ttThemedIcon("go-previous", QStyle::SP_ArrowBack));
  pbNextCut->setIcon(ttThemedIcon("go-next", QStyle::SP_ArrowForward));

  connect(mPlayer, &TTMpvWrapper::playerPlaying,  this, &TTCutPreview::onPlayerPlaying);
  connect(mPlayer, &TTMpvWrapper::playerFinished, this, &TTCutPreview::onPlayerFinished);
  connect(mPlayer, &TTMpvWrapper::playerError,    this, &TTCutPreview::onPlayerError);
  connect(cbCutPreview, qOverload<int>(&QComboBox::currentIndexChanged), this, &TTCutPreview::onCutSelectionChanged);
  connect(pbPlay,       &QPushButton::clicked, this, &TTCutPreview::onPlayPreview);
  connect(pbExit,       &QPushButton::clicked, this, &TTCutPreview::onExitPreview);
  connect(pbPrevCut,    &QPushButton::clicked, this, &TTCutPreview::onPrevCut);
  connect(pbNextCut,    &QPushButton::clicked, this, &TTCutPreview::onNextCut);

  // Burst warning widgets — placed in their own grid row further below
  lblBurstWarning = new QLabel(this);
  lblBurstWarning->hide();

  pbBurstShift = new QPushButton(this);
  // Keep Enter bound to Start: this button appears only on a burst warning and
  // must not become the dialog default when it takes focus.
  pbBurstShift->setAutoDefault(false);
  configureBurstShiftButton(/*isCutOut=*/false);
  // Keep the button's space while hidden, so the row keeps its height and the
  // video frame does not jump when stepping between cuts with and without a
  // burst.
  QSizePolicy shiftPolicy = pbBurstShift->sizePolicy();
  shiftPolicy.setRetainSizeWhenHidden(true);
  pbBurstShift->setSizePolicy(shiftPolicy);
  pbBurstShift->hide();
  connect(pbBurstShift, &QPushButton::clicked, this, &TTCutPreview::onBurstShift);

  // Aspect change widgets - their own grid row below the burst row, hidden
  // entirely when there is nothing to report. updateHintRowSpace() lets the
  // empty burst row give up its reserved height while an aspect message is
  // shown, so the message takes the burst row's place instead of sitting
  // below an empty line.
  lblAspectWarning = new QLabel(this);
  lblAspectWarning->hide();
  pbAspectJump = new QPushButton(this);
  pbAspectJump->setAutoDefault(false);   // Enter stays bound to Start
  pbAspectJump->hide();
  connect(pbAspectJump, &QPushButton::clicked, this, &TTCutPreview::onAspectJump);

  // The burst warning gets its own grid row below the controls (row 0 = video
  // frame, row 1 = controls). Sharing the controls row meant competing with
  // the cut selector and four buttons: the spacer collapsed first, then the
  // label was squeezed below its size hint and clipped its text without an
  // ellipsis — which the longer German translation hit first.
  // videoFrame carries verstretch 6 and this row defaults to 0, so the extra
  // row takes no height from the video when the dialog is resized.
  QGridLayout* grid = qobject_cast<QGridLayout*>(layout());
  if (grid) {
    QHBoxLayout* burstLayout = new QHBoxLayout();
    burstLayout->addWidget(lblBurstWarning);
    burstLayout->addStretch(1);
    burstLayout->addWidget(pbBurstShift);
    grid->addLayout(burstLayout, 2, 0);

    QHBoxLayout* aspectLayout = new QHBoxLayout();
    aspectLayout->addWidget(lblAspectWarning);
    aspectLayout->addStretch(1);
    aspectLayout->addWidget(pbAspectJump);
    grid->addLayout(aspectLayout, 3, 0);
  }

  mpCutList = nullptr;
  mpOriginalCutList = nullptr;
  mpAVData = nullptr;
  mBurstSegmentIdx = -1;
  mBurstIsCutOut = false;
  mAspectSegmentIdx = -1;
  mAspectIsCutOut = false;
  mAspectTarget = -1;
  mClipOffset = 0;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destroys the object and frees any allocated resources
 */
TTCutPreview::~TTCutPreview()
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler to receive widgets close events
 */
void TTCutPreview::closeEvent(QCloseEvent* event)
{
  cleanUp();
  event->accept();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Initialize preview parameter
 */
void TTCutPreview::initPreview(TTCutList* previewCutList, TTCutList* originalCutList, TTAVData* avData, bool skipFirst, bool skipLast)
{
  mpCutList = previewCutList;
  mpOriginalCutList = originalCutList;
  mpAVData = avData;

  int       iPos;
  QString   preview_video_name;
  QFileInfo preview_video_info;
  QString   selectionString;

  int numPreview = previewCutList->count()/2+1;

  // skipFirst/skipLast: skip standalone start/end clips when they are
  // neighbor-only context (not the selected cut). mClipOffset maps
  // combobox index → preview file index.
  mClipOffset = skipFirst ? 1 : 0;

  // Create video and audio preview clips.
  // Signals stay blocked while filling: the first addItem() moves currentIndex
  // from -1 to 0 and would fire currentIndexChanged → onCutSelectionChanged →
  // load(). Loading is deferred to showEvent() (see there), so nothing may
  // reach the player before the dialog is on screen.
  const QSignalBlocker fillBlocker(cbCutPreview);
  for (int i = 0; i < numPreview; i++ ) {
    // first cut-in (skip when first cut is neighbor-only context)
    if (i == 0 && !skipFirst) {
      TTCutItem item = previewCutList->at(i);
      selectionString = QString("Start: %1").arg(item.cutInTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    // cut i-i
    if (numPreview > 1 && i > 0 && i < numPreview-1) {
      iPos = (i-1)*2+1;

      TTCutItem item1 = previewCutList->at(iPos);
      TTCutItem item2 = previewCutList->at(iPos+1);
      selectionString = QString("Cut %1-%2: %3 - %4")
            .arg(i).arg(i+1)
            .arg(item1.cutInTime().toString("hh:mm:ss"))
            .arg(item2.cutOutTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }

    //last cut out (skip when last cut is neighbor-only context)
    if (i == numPreview-1 && !skipLast) {
      iPos = (i-1)*2+1;

      TTCutItem item = previewCutList->at(iPos);
      selectionString = QString("End: %1").arg(item.cutOutTime().toString("hh:mm:ss"));
      cbCutPreview->addItem( selectionString );
    }
  }

  // set the current cut preview to the first cut clip
  // Check for H.264/H.265 (.mkv) or MPEG-2 (.mpg) preview files
  // Try .mkv first (mkvmerge output for H.264/H.265)
  preview_video_name = "preview_001.mkv";
  preview_video_info.setFile(QDir(TTSettings::instance()->tempDirPath()), preview_video_name);
  if (!preview_video_info.exists()) {
    // Fallback to .mpg for MPEG-2
    preview_video_name = "preview_001.mpg";
    preview_video_info.setFile(QDir(TTSettings::instance()->tempDirPath()), preview_video_name);
  }

  current_video_file = preview_video_info.absoluteFilePath();
  // No load here — showEvent() does it once the widget can render.
}

/* /////////////////////////////////////////////////////////////////////////////
 * Load the first clip once the dialog is actually on screen.
 *
 * TTCutMainWindow calls initPreview() before exec(), so at that point the mpv
 * render widget has never been painted: there is no GL context, hence no mpv
 * render context, and mpv marks vo/libmpv as broken for the whole session —
 * "No render context set", a black preview that no later Play can revive.
 * Deferring the first load past show() is what gives the widget its context.
 *
 * The load goes through a zero-timer rather than running inline: showEvent()
 * fires while the dialog is being shown, before its children have been
 * painted. One turn of the event loop later the widget is realised and
 * prepareRenderContext() succeeds (measured: it fails when called any earlier).
 */
void TTCutPreview::showEvent(QShowEvent* event)
{
  QDialog::showEvent(event);

  if (!mInitialLoadPending) return;
  mInitialLoadPending = false;

  QTimer::singleShot(0, this, [this]() {
    if (auto* mrw = qobject_cast<TTMpvRenderWidget*>(mPlayer->renderWidget())) {
      if (!mrw->prepareRenderContext())
        TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
            QString("TTCutPreview: prepareRenderContext failed"));
    }

    // Opening the dialog must not start playback — only cut changes the user
    // asks for afterwards do.
    const int iCut = cbCutPreview->currentIndex();
    if (iCut >= 0) {
      mAutoPlayOnSelect = false;
      onCutSelectionChanged(iCut);
    }
    mAutoPlayOnSelect = true;
  });
}

/* /////////////////////////////////////////////////////////////////////////////
 * ComboBox selectionChanged event handler: load the selected movie
 */
void TTCutPreview::onCutSelectionChanged( int iCut )
{
  QString   preview_video_name;
  QString   preview_subtitle_name;
  QFileInfo preview_video_info;
  QFileInfo preview_subtitle_info;

  // Map combobox index to file index (offset for transitionsOnly mode)
  int fileIndex = iCut + 1 + mClipOffset;

  // Try .mkv first (H.264/H.265 via mkvmerge), then .mpg (MPEG-2 via mplex)
  preview_video_name = QString("preview_%1.mkv").arg(fileIndex, 3, 10, QChar('0'));
  preview_video_info.setFile( QDir(TTSettings::instance()->tempDirPath()), preview_video_name );
  if (!preview_video_info.exists()) {
    preview_video_name = QString("preview_%1.mpg").arg(fileIndex, 3, 10, QChar('0'));
    preview_video_info.setFile( QDir(TTSettings::instance()->tempDirPath()), preview_video_name );
  }
  current_video_file = preview_video_info.absoluteFilePath();

  // Check for subtitle file. size() > 0: a clip range without subtitle
  // entries used to leave a 0-byte .srt behind (cutSubtitleTracks removes
  // those now, but stale files may still exist), and mpv reports an
  // error-level "Can not open external file" on it.
  preview_subtitle_name = QString("preview_%1.srt").arg(fileIndex, 3, 10, QChar('0'));
  preview_subtitle_info.setFile( QDir(TTSettings::instance()->tempDirPath()), preview_subtitle_name );
  if (preview_subtitle_info.exists() && preview_subtitle_info.size() > 0) {
    mPlayer->setSubtitleFile(preview_subtitle_info.absoluteFilePath());
  } else {
    mPlayer->clearSubtitleFile();
  }

  qDebug("load preview %s", qPrintable(current_video_file));
  mPlayer->load(current_video_file, 0.0, QString(), /*autoPlay=*/mAutoPlayOnSelect);
  if (mAutoPlayOnSelect) {
    pbPlay->setText(tr("Stop"));
    pbPlay->setIcon(ttThemedIcon("media-playback-stop", QStyle::SP_MediaStop));
  } else {
    pbPlay->setText(tr("Play"));
    pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));
  }

  // Update prev/next button states
  // Back stays enabled even on the first cut: its first stage — jump to the
  // start of the current clip — is useful there too. Only one combination is
  // inert (first cut, already at position 0), which beats re-evaluating the
  // enabled state on every position update.
  pbPrevCut->setEnabled(cbCutPreview->count() > 0);
  pbNextCut->setEnabled(iCut < cbCutPreview->count() - 1);

  checkBurstForCurrentCut(iCut);
  checkAspectForCurrentCut(iCut);
  updateHintRowSpace();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Play/Pause the selected preview clip
 */
void TTCutPreview::onPlayPreview()
{
  if (mPlayer->isPlaying()) {
    // Pause without tearing mpv down — current frame stays visible.
    mPlayer->pause();
    pbPlay->setText(tr("Play"));
    pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));
    return;
  }

  // Standing on the last frame: one click replays from the start. The seek is
  // enough — the file is still loaded, so no reload is needed.
  if (mPlayer->isAtEnd())
    mPlayer->seek(0.0);

  // Resume (or start, after a preload in pause mode).
  mPlayer->play();
}

void TTCutPreview::onPlayerPlaying()
{
  pbPlay->setText(tr("Stop"));
  pbPlay->setIcon(ttThemedIcon("media-playback-stop", QStyle::SP_MediaStop));
}

void TTCutPreview::onPlayerFinished()
{
  // No reload. With keep-open the file stays loaded and mpv holds the last
  // frame on screen — that is the point of this dialog change. Reloading here
  // is what used to snap the picture back to frame 0.
  pbPlay->setText(tr("Play"));
  pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));
}

void TTCutPreview::onPlayerError(const QString& message)
{
  // Log only — do NOT reset the Play button. mpv classifies many non-fatal
  // messages as "error" level (an unreadable --sub-file, transient libmpv
  // OpenGL texture errors, h264 mmco warnings) while playback continues
  // fine; resetting here is what left the button on "Play" during running
  // playback. A real abnormal termination is followed by playerFinished,
  // which resets the button. Same reasoning as TTCurrentFrame's
  // playerError handler.
  TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
      QString("Preview player error: %1").arg(message));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Exit the preview window
 */
void TTCutPreview::onExitPreview()
{
  close();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Back: first stage jumps to the start of the current clip and shows it as a
 * still; only when already at the start does a click move to the previous cut.
 * Same idea as the skip-back button on a CD player. The trigger is the
 * position, not a time window, so the two stages stay predictable.
 */
void TTCutPreview::onPrevCut()
{
  const double kAtStartSec = 0.5;   // time-pos arrives ~10x/s; ample for this

  if (mPlayer->playbackPosition() >= kAtStartSec) {
    // Stage 1: back to the start, as a still image.
    // Pause unconditionally: isPlaying() is not a reliable statement about
    // mpv here (at EOF it is false while mpv may still be unpaused), and a
    // redundant pause costs nothing.
    mPlayer->pause();
    mPlayer->seek(0.0);
    pbPlay->setText(tr("Play"));
    pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));
    return;
  }

  // Stage 2: previous cut, loaded paused on its first frame.
  // Back stays silent in both stages, so what the button does never depends on
  // where playback happens to stand — a state the dialog does not show. Forward
  // is the one that plays; press Play here to hear the cut.
  int currentIndex = cbCutPreview->currentIndex();
  if (currentIndex > 0) {
    mAutoPlayOnSelect = false;
    cbCutPreview->setCurrentIndex(currentIndex - 1);
    mAutoPlayOnSelect = true;
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Forward: straight to the next cut, which loads and starts playing. Single
 * stage on purpose — unlike Back, there is no sensible intermediate target.
 */
void TTCutPreview::onNextCut()
{
  int currentIndex = cbCutPreview->currentIndex();
  if (currentIndex < cbCutPreview->count() - 1) {
    cbCutPreview->setCurrentIndex(currentIndex + 1);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Show a message in the burst label
 *
 * Single owner of text, colour and tooltip. The tooltip repeats the text so
 * the message stays reachable should a future translation ever outgrow the
 * row. Visibility stays with the caller: regeneratePreviewClip() reads
 * isVisible() to tell "burst gone" from "burst still there".
 */
void TTCutPreview::applyHintMessage(QLabel* label, const QString& message, bool resolved)
{
  label->setStyleSheet(resolved
      ? "QLabel { color: #228B22; font-weight: bold; }"
      : "QLabel { color: #FF8C00; font-weight: bold; }");
  label->setText(message);
  label->setToolTip(message);
}

void TTCutPreview::setBurstMessage(const QString& message, bool resolved)
{
  applyHintMessage(lblBurstWarning, message, resolved);
}

void TTCutPreview::setAspectMessage(const QString& message, bool resolved)
{
  applyHintMessage(lblAspectWarning, message, resolved);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Set caption, icon and tooltip of the burst shift button
 *
 * Caption and icon must always be set together — setting them apart is what
 * made a cut-in burst show "+1 Frame" next to a left arrow. A cut-in burst
 * moves the cut point later (arrow right), a cut-out burst moves it earlier
 * (arrow left). The warning left of the button already names the affected cut
 * point, so the caption only carries the step size.
 */
void TTCutPreview::configureBurstShiftButton(bool isCutOut)
{

  if (isCutOut) {
    pbBurstShift->setIcon(ttThemedIcon("go-previous", QStyle::SP_ArrowBack));
    pbBurstShift->setToolTip(tr("Move cut-out one frame earlier"));
  }
  else {
    pbBurstShift->setIcon(ttThemedIcon("go-next", QStyle::SP_ArrowForward));
    pbBurstShift->setToolTip(tr("Move cut-in one frame later"));
  }

  pbBurstShift->setText(tr("1 Frame"));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Check for audio burst at the currently selected cut transition
 */
void TTCutPreview::checkBurstForCurrentCut(int iCut)
{
  // No colour reset needed: setBurstMessage() sets the colour with every
  // message, so an earlier green "Burst resolved" cannot bleed into the next
  // warning.
  lblBurstWarning->hide();
  pbBurstShift->hide();
  mBurstSegmentIdx = -1;

  if (!mpAVData || !mpCutList || mpCutList->count() < 2) return;

  int numPreview = mpCutList->count() / 2 + 1;

  if (iCut < 0 || iCut >= numPreview) return;

  // Start preview (iCut == 0): only CutIn of first cut is relevant.
  if (iCut == 0) {
    TTCutItem cutInItem = mpCutList->at(0);
    TTAVData::CutBurstInfo bin = mpAVData->detectCutInBurst(cutInItem);
    if (bin.present) {
      setBurstMessage(tr("\xe2\x9a\xa0 Audio burst at start of cut 1 (%1 dB)")
          .arg(bin.burstDb, 0, 'f', 1), /*resolved=*/false);
      lblBurstWarning->show();
      configureBurstShiftButton(/*isCutOut=*/false);
      pbBurstShift->show();
      mBurstSegmentIdx = 0;
      mBurstIsCutOut = false;
    }
    return;
  }

  // iPos = index of the CutOut entry in the cut list for this transition (or End preview)
  int iPos = (iCut - 1) * 2 + 1;
  if (iPos >= mpCutList->count()) return;

  TTCutItem cutOutItem = mpCutList->at(iPos);
  TTAVData::CutBurstInfo bout = mpAVData->detectCutOutBurst(cutOutItem);

  if (bout.present) {
    setBurstMessage(tr("\xe2\x9a\xa0 Audio burst at end of cut %1 (%2 dB)")
        .arg(iCut).arg(bout.burstDb, 0, 'f', 1), /*resolved=*/false);
    lblBurstWarning->show();
    configureBurstShiftButton(/*isCutOut=*/true);
    pbBurstShift->show();
    mBurstSegmentIdx = iPos;
    mBurstIsCutOut = true;
    return;  // Show only one burst per transition (CutOut takes priority)
  }

  // Check CutIn of right segment (skipped automatically for End preview)
  if (iPos + 1 < mpCutList->count()) {
    TTCutItem cutInItem = mpCutList->at(iPos + 1);
    TTAVData::CutBurstInfo bin = mpAVData->detectCutInBurst(cutInItem);
    if (bin.present) {
      setBurstMessage(tr("\xe2\x9a\xa0 Audio burst at start of cut %1 (%2 dB)")
          .arg(iCut + 1).arg(bin.burstDb, 0, 'f', 1), /*resolved=*/false);
      lblBurstWarning->show();
      configureBurstShiftButton(/*isCutOut=*/false);
      pbBurstShift->show();
      mBurstSegmentIdx = iPos + 1;
      mBurstIsCutOut = false;
    }
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Check for an aspect change at the currently selected cut transition
 *
 * Same transition rule as the burst check: clip 0 = cut-in of cut 1, clip i
 * = cut-out of cut i first, then cut-in of cut i+1; one finding shown.
 * The analysis reads the ORIGINAL cut: the preview list holds only the short
 * pieces around each edge (two entries per cut), whose majority would say
 * nothing about the cut.
 */
void TTCutPreview::checkAspectForCurrentCut(int iCut)
{
  lblAspectWarning->hide();
  pbAspectJump->hide();
  mAspectSegmentIdx = -1;
  mAspectTarget = -1;

  if (!mpCutList || !mpOriginalCutList || mpCutList->count() < 2) return;
  const int numPreview = mpCutList->count() / 2 + 1;
  if (iCut < 0 || iCut >= numPreview) return;

  auto report = [this](int segmentIdx, bool isCutOut, int cutNumber) {
    const std::optional<TTCutItem> original = originalCutItem(segmentIdx);
    if (!original || !original->avDataItem()) return false;
    const TTCutItem& cut = *original;
    const TTAspectWindowInfo info = ttAnalyzeAspectWindow(
        cut.avDataItem()->videoStream(), cut.cutInIndex(), cut.cutOutIndex());
    const int target = isCutOut ? info.cutOutTarget : info.cutInTarget;
    if (target < 0) return false;

    if (isCutOut) {
      setAspectMessage(tr("\xe2\x9a\xa0 Cut %1 ends in %2 - the cut is %3 up to frame %4 (-%5)")
          .arg(cutNumber)
          .arg(TTSequenceHeader::aspectText(info.cutOutAspect), TTSequenceHeader::aspectText(info.mainAspect))
          .arg(target).arg(cut.cutOutIndex() - target), /*resolved=*/false);
      pbAspectJump->setIcon(ttThemedIcon("go-previous", QStyle::SP_ArrowBack));
      pbAspectJump->setToolTip(tr("Move the cut-out to frame %1").arg(target));
    } else {
      setAspectMessage(tr("\xe2\x9a\xa0 Cut %1 starts in %2 - the cut is %3 from frame %4 (+%5)")
          .arg(cutNumber)
          .arg(TTSequenceHeader::aspectText(info.cutInAspect), TTSequenceHeader::aspectText(info.mainAspect))
          .arg(target).arg(target - cut.cutInIndex()), /*resolved=*/false);
      pbAspectJump->setIcon(ttThemedIcon("go-next", QStyle::SP_ArrowForward));
      pbAspectJump->setToolTip(tr("Move the cut-in to frame %1").arg(target));
    }
    pbAspectJump->setText(tr("Frame %1").arg(target));
    lblAspectWarning->show();
    pbAspectJump->show();
    mAspectSegmentIdx = segmentIdx;
    mAspectIsCutOut = isCutOut;
    mAspectTarget = target;
    return true;
  };

  if (iCut == 0) {
    report(0, /*isCutOut=*/false, 1);
    return;
  }
  const int iPos = (iCut - 1) * 2 + 1;
  if (iPos >= mpCutList->count()) return;
  if (report(iPos, /*isCutOut=*/true, iCut)) return;   // cut-out takes priority
  if (iPos + 1 < mpCutList->count())
    report(iPos + 1, /*isCutOut=*/false, iCut + 1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Height of the two hint rows
 *
 * The burst row keeps its height while empty (the burst button retains its
 * size when hidden), so the video frame does not jump between clips with and
 * without a burst. That reservation only holds while the aspect row is
 * hidden: with an aspect message and no burst, the empty burst row would sit
 * above the message. Releasing it lets the aspect row take its place - one
 * row in every case except burst AND aspect change at the same transition.
 * Call after every change of the aspect row's visibility.
 */
void TTCutPreview::updateHintRowSpace()
{
  QSizePolicy policy = pbBurstShift->sizePolicy();
  policy.setRetainSizeWhenHidden(lblAspectWarning->isHidden());
  pbBurstShift->setSizePolicy(policy);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Move one edge of a cut: shared by the burst shift and the aspect jump
 */
// Maps a preview-list index to its cut in the copy of the original list:
// the preview list holds two entries per cut.
std::optional<TTCutItem> TTCutPreview::originalCutItem(int segmentIdx) const
{
  if (segmentIdx < 0 || !mpCutList || !mpOriginalCutList) return std::nullopt;
  if (segmentIdx >= mpCutList->count()) return std::nullopt;
  const int originalIdx = segmentIdx / 2;
  if (originalIdx >= mpOriginalCutList->count()) return std::nullopt;
  return mpOriginalCutList->at(originalIdx);
}

// Update the REAL model data via TTAVItem::updateCutEntry. The copy's
// avDataItem() points to the real TTAVItem in the model; the real cut item
// is found by matching its position.
void TTCutPreview::updateRealCutItem(const TTCutItem& copyItem, bool isCutOut, int oldIdx, int newIdx)
{
  TTAVItem* avItem = copyItem.avDataItem();
  if (!mpAVData || !avItem) return;
  for (int i = 0; i < mpAVData->cutCount(); i++) {
    TTCutItem realItem = mpAVData->cutItemAt(i);
    if (realItem.cutInIndex() == copyItem.cutInIndex() &&
        realItem.cutOutIndex() == copyItem.cutOutIndex() &&
        realItem.avDataItem() == avItem) {
      if (isCutOut) {
        avItem->updateCutEntry(realItem, realItem.cutInIndex(), newIdx);
      } else {
        avItem->updateCutEntry(realItem, newIdx, realItem.cutOutIndex());
      }
      if (TTSettings::instance()->logUI())
          qDebug() << "Edge move: Updated REAL model cut" << i
                   << (isCutOut ? "CutOut" : "CutIn")
                   << "Frame" << oldIdx << "->" << newIdx;
      break;
    }
  }
}

// Mirror the moved edge into the preview's own two lists: the copy of the
// original cut list and the preview entry the re-checks read.
void TTCutPreview::applyEdgeMoveToLists(const TTCutItem& copyItem, int segmentIdx,
                                        bool isCutOut, int newIdx)
{
  TTCutItem updatedCopy(copyItem);
  if (isCutOut) {
    updatedCopy.update(copyItem.cutInIndex(), newIdx);
  } else {
    updatedCopy.update(newIdx, copyItem.cutOutIndex());
  }
  mpOriginalCutList->update(copyItem, updatedCopy);

  TTCutItem previewItem = mpCutList->at(segmentIdx);
  TTCutItem updatedPreview(previewItem);
  if (isCutOut) {
    updatedPreview.update(previewItem.cutInIndex(), newIdx);
  } else {
    updatedPreview.update(newIdx, previewItem.cutOutIndex());
  }
  mpCutList->update(previewItem, updatedPreview);
}

// Move one edge in model and preview lists, rebuild the current clip (which
// re-runs both checks) and put the shared videoStream back.
void TTCutPreview::moveCutEdge(int segmentIdx, bool isCutOut, int oldIdx, int newIdx)
{
  const std::optional<TTCutItem> original = originalCutItem(segmentIdx);
  if (!original) return;
  const TTCutItem copyItem = *original;

  // updateCutEntry() emits a signal chain that ends in
  // TTCutOutFrame::onCutOutChanged → videoStream->moveToIndexPos(cutOut),
  // moving the shared videoStream off the UI's current frame. Save the
  // stream index up front and restore it after the whole operation
  // (including the preview regen) is done — otherwise Play in
  // TTCurrentFrame would start at the cut-out instead of the visible frame.
  TTVideoStream* savedStream = nullptr;
  int savedStreamIndex = -1;
  if (TTAVItem* avItem = copyItem.avDataItem()) {
    savedStream = avItem->videoStream();
    if (savedStream)
      savedStreamIndex = savedStream->currentIndex();
  }

  updateRealCutItem(copyItem, isCutOut, oldIdx, newIdx);
  applyEdgeMoveToLists(copyItem, segmentIdx, isCutOut, newIdx);

  regeneratePreviewClip(cbCutPreview->currentIndex());

  if (savedStream && savedStreamIndex >= 0)
    savedStream->moveToIndexPos(savedStreamIndex);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Shift the cut point by one frame to avoid the audio burst
 */
void TTCutPreview::onBurstShift()
{
  const std::optional<TTCutItem> original = originalCutItem(mBurstSegmentIdx);
  if (!original) return;
  const TTCutItem copyItem = *original;

  const int oldIdx = mBurstIsCutOut ? copyItem.cutOutIndex() : copyItem.cutInIndex();
  const int newIdx = mBurstIsCutOut ? oldIdx - 1 : oldIdx + 1;

  // A one-frame cut leaves the shift no room: moving either end puts it past
  // the other one, which updateCutEntry refuses - say so instead of letting
  // the button look as if it had worked.
  const bool wouldInvert = mBurstIsCutOut ? (newIdx < copyItem.cutInIndex())
                                          : (newIdx > copyItem.cutOutIndex());
  if (wouldInvert) {
    setBurstMessage(tr("The cut is only one frame long - its %1 cannot be shifted.")
                    .arg(mBurstIsCutOut ? tr("cut-out") : tr("cut-in")), false);
    return;
  }

  if (TTSettings::instance()->logUI())
      qDebug() << "Burst shift:" << (mBurstIsCutOut ? "CutOut" : "CutIn")
               << "Frame" << oldIdx << "->" << newIdx
               << "(original cut" << mBurstSegmentIdx / 2 << ")";

  // Show feedback (the re-check at the end of the regeneration replaces it)
  QString label = mBurstIsCutOut ? tr("CutOut updated") : tr("CutIn updated");
  setBurstMessage(tr("\xe2\x9c\x93 %1 (frame %2 \xe2\x86\x92 %3)")
      .arg(label).arg(oldIdx).arg(newIdx), /*resolved=*/true);
  pbBurstShift->hide();

  moveCutEdge(mBurstSegmentIdx, mBurstIsCutOut, oldIdx, newIdx);

  // regeneratePreviewClip() re-ran the check; nothing visible means gone.
  if (!lblBurstWarning->isVisible()) {
    setBurstMessage(tr("\xe2\x9c\x93 Burst resolved"), /*resolved=*/true);
    lblBurstWarning->show();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Move the cut edge to the first (last) picture of the cut's majority aspect
 */
void TTCutPreview::onAspectJump()
{
  const std::optional<TTCutItem> original = originalCutItem(mAspectSegmentIdx);
  if (mAspectTarget < 0 || !original) return;
  const TTCutItem copyItem = *original;

  const bool isCutOut = mAspectIsCutOut;
  const int oldIdx = isCutOut ? copyItem.cutOutIndex() : copyItem.cutInIndex();
  const int newIdx = mAspectTarget;

  // The target lies inside the cut by construction (it is a picture of the
  // cut's majority aspect), so the move cannot invert the cut.
  if (TTSettings::instance()->logUI())
      qDebug() << "Aspect jump:" << (isCutOut ? "CutOut" : "CutIn")
               << "Frame" << oldIdx << "->" << newIdx
               << "(original cut" << mAspectSegmentIdx / 2 << ")";

  pbAspectJump->hide();
  moveCutEdge(mAspectSegmentIdx, isCutOut, oldIdx, newIdx);

  // regeneratePreviewClip() re-ran the check; nothing visible means solved.
  if (!lblAspectWarning->isVisible()) {
    setAspectMessage(tr("\xe2\x9c\x93 %1 moved (frame %2 \xe2\x86\x92 %3)")
        .arg(isCutOut ? tr("Cut-out") : tr("Cut-in")).arg(oldIdx).arg(newIdx),
        /*resolved=*/true);
    lblAspectWarning->show();
    updateHintRowSpace();
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Regenerate a single preview clip after an edge move
 */
void TTCutPreview::regeneratePreviewClip(int iCut)
{
  if (!mpCutList || mpCutList->count() < 2) return;

  int numPreview = mpCutList->count() / 2 + 1;
  if (iCut < 0 || iCut >= numPreview) return;

  // Stop player if running
  if (mPlayer->isPlaying()) {
    mPlayer->stop();
  }

  // Show progress dialog — repaint() forces synchronous painting before blocking work
  QProgressDialog progress(tr("Regenerating preview..."), QString(), 0, 0, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.show();
  progress.repaint();
  QApplication::processEvents();

  // Build temporary cut list for this clip (shared with TTCutPreviewTask)
  TTCutList tmpCutList;
  ttBuildClipCutList(mpCutList, iCut, &tmpCutList);

  if (tmpCutList.count() == 0) return;

  // Get source info
  const TTPreviewSource src = ttResolvePreviewSource(&tmpCutList);
  if (!src.isValid()) return;

  TTVideoStream* vStream = src.vStream;
  bool isMpeg2 = (vStream->streamType() == TTAVTypes::mpeg2_demuxed_video);

  // File index must match onCutSelectionChanged()'s lookup, including
  // mClipOffset (1 in transitionsOnly mode), or the regen overwrites
  // the wrong file. All preview output is .mkv (set by TTCutPreviewTask).
  int fileIndex = iCut + 1 + mClipOffset;
  QString outputFile = TTCutPreviewTask::createPreviewFileName(fileIndex, "mkv");

  // The cut/regen pipeline shares this videoStream with TTCurrentFrame and
  // will leave its currentIndex on the last cut frame. Save and restore so
  // the main UI's "play from current frame" position is not silently moved.
  const int savedStreamIndex = vStream->currentIndex();

  // The rebuild itself lives in data/ttpreviewclip.cpp, GUI-free. Only the
  // wording of the phases stays here - moving the strings down would move
  // their translation context with them.
  auto showStage = [&](TTPreviewStage stage) {
    switch (stage) {
      case TTPreviewStage::CutVideo:      progress.setLabelText(tr("Cutting MPEG-2 video...")); break;
      case TTPreviewStage::SmartCutVideo: progress.setLabelText(tr("Video Smart Cut..."));      break;
      case TTPreviewStage::CutAudio:      progress.setLabelText(tr("Cutting audio..."));        break;
      case TTPreviewStage::Mux:           progress.setLabelText(tr("Creating MKV..."));         break;
    }
    QApplication::processEvents();
  };

  if (isMpeg2) {
    ttRebuildMpeg2PreviewClip(mpAVData, &tmpCutList, fileIndex, showStage);
  } else {
    ttRebuildSmartCutPreviewClip(&tmpCutList, fileIndex, showStage);
  }

  vStream->moveToIndexPos(savedStreamIndex);

  if (TTSettings::instance()->logUI())
      qDebug() << "Regenerate: Preview clip" << iCut + 1 << "rebuilt:" << outputFile;

  // Reload clip in player, preloaded paused — user has to press Play.
  current_video_file = outputFile;
  mPlayer->load(current_video_file, 0.0, QString(), /*autoPlay=*/false);
  pbPlay->setText(tr("Play"));
  pbPlay->setIcon(ttThemedIcon("media-playback-start", QStyle::SP_MediaPlay));

  // Re-check both edge findings for the current cut. The "resolved"
  // confirmation belongs to the caller that knows what it moved: after an
  // aspect jump a "Burst resolved" would claim a burst that never existed.
  checkBurstForCurrentCut(iCut);
  checkAspectForCurrentCut(iCut);
  updateHintRowSpace();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Housekeeping: Remove the temporary created preview clips
 */
void TTCutPreview::cleanUp()
{
  mPlayer->stop();

  ttRemovePreviewFiles();
}
