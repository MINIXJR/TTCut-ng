/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttaudiorepairdialog.h"

#include "ttmpvwrapper.h"
#include "../data/ttavlist.h"
#include "../extern/ttaudiorepairitem.h"
#include "../avstream/ttavstream.h"
#include "../extern/ttaudiorepair.h"
#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QLocale>
#include <QTime>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QUuid>

namespace {

// Same 32 ms AC3-frame contract as TTAudioAnomalyScanTask's kFrameDurSec and
// TTAudioRepairItem's header comment - used only where opening the audio
// file is not worth it (see approxAc3RangeForMarker's doc comment).
constexpr double kApproxFrameDurMs = 1536.0 * 1000.0 / 48000.0; // 32 ms

// Below this match the fill view warns that the fill gains little (measured
// 2026-10-09: under 0.80 it gains less than 6 dB in 94 % of the places).
constexpr double kDonorFillHintMatch = 0.80;

// Real per-file AC3 frame duration (ms), read from the container. Falls
// back to the fixed 32 ms contract if the file cannot be probed - the
// dialog still opens in that case; Play/Accept surface a proper error from
// the real I/O path (writePreviewWindow / buildRepairTable) if the file
// truly is not readable.
double probeFrameDurationMs(const QString& audioFile)
{
  if (audioFile.isEmpty()) return kApproxFrameDurMs;

  AVFormatContext* fmtCtx = nullptr;
  if (avformat_open_input(&fmtCtx, audioFile.toUtf8().constData(), nullptr, nullptr) < 0)
    return kApproxFrameDurMs;
  if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
    avformat_close_input(&fmtCtx);
    return kApproxFrameDurMs;
  }
  double result = kApproxFrameDurMs;
  for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
    const AVCodecParameters* cp = fmtCtx->streams[i]->codecpar;
    if (cp->codec_type == AVMEDIA_TYPE_AUDIO && cp->sample_rate > 0) {
      result = 1536.0 * 1000.0 / cp->sample_rate;
      break;
    }
  }
  avformat_close_input(&fmtCtx);
  return result;
}

// Number of extraFrameIndices entries strictly below frameIndex - identical
// binary search to TTAVData::countExtraFramesBefore() and the anonymous-
// namespace countExtrasBefore() in data/ttaudioanomalyscantask.cpp (not
// shared code: extraFrameIndices arrives here as a plain QList<int>, not a
// TTAVData this dialog's constructor never receives - see the header's doc
// comment on why). Review fix 1.
int countExtrasBefore(const QList<int>& extras, int frameIndex)
{
  int lo = 0, hi = extras.size();
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (extras[mid] < frameIndex) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// Inverse of TTAudioAnomalyScanTask::videoFrameForTime() for a single
// point in time: time = (frameIndex - extrasBefore(frameIndex)) / fps,
// the same formula TTAVData::buildVideoKeepList() uses. Review fix 1: the
// marker's frameIndex is a VIDEO display index that already has
// extraFrameIndices folded in by the scanner; skipping the correction here
// silently reintroduces the same seconds-scale error the fix addresses
// (273 extras / ~10.9s measured on the corpus's "Benders" example, see
// docs/code-map/audio-cut-timing.md).
double videoFrameToSeconds(int frameIndex, double frameRate, const QList<int>& extraFrameIndices)
{
  const int extras = countExtrasBefore(extraFrameIndices, frameIndex);
  return double(frameIndex - extras) / frameRate;
}

} // namespace

void TTAudioRepairDialog::approxAc3RangeForMarker(const TTStreamPoint& point, double frameRate,
                                                    const QList<int>& extraFrameIndices,
                                                    qint64& frameFrom, qint64& frameTo)
{
  // Exact range when the scanner (or the project file) carried one through -
  // no estimate needed at all then (final review I3).
  if (point.hasAudioFrameRange()) {
    frameFrom = point.audioFrameFrom();
    frameTo   = point.audioFrameTo();
    return;
  }

  if (frameRate <= 0.0) frameRate = 25.0;
  const double startMs = videoFrameToSeconds(point.frameIndex(), frameRate, extraFrameIndices) * 1000.0;
  const double endMs   = startMs + double(point.duration()) * 1000.0;
  frameFrom = qint64(qRound(startMs / kApproxFrameDurMs));
  // TTStreamPoint::duration() is end-EXCLUSIVE, frameTo is INCLUSIVE (same
  // convention as TTAudioRepairItem) - hence the -1. Without it the range
  // claimed one AC3 frame more than the finding had.
  frameTo   = qint64(qRound(endMs   / kApproxFrameDurMs)) - 1;
  if (frameTo < frameFrom) frameTo = frameFrom;
}

int TTAudioRepairDialog::repairIndexForMarker(const TTAVItem* item, const TTStreamPoint& point,
                                              const QList<int>& extraFrameIndices)
{
  if (!item || point.type() != StreamPointType::AudioAnomaly) return -1;
  const int track = item->firstAc3TrackIndex();
  if (track < 0) return -1;
  const double frameRate = item->videoStream() ? item->videoStream()->frameRate() : 25.0;
  qint64 from = 0, to = 0;
  approxAc3RangeForMarker(point, frameRate, extraFrameIndices, from, to);
  return item->findAudioRepairOverlapping(track, from, to);
}

TTAudioRepairDialog::TTAudioRepairDialog(TTAVItem* avItem, const TTStreamPoint& point,
                                          int trackIndex, const QList<int>& extraFrameIndices,
                                          QWidget* parent)
  : QDialog(parent),
    mAvItem(avItem),
    mPoint(point),
    mTrackIndex(trackIndex),
    mExtraFrameIndices(extraFrameIndices),
    mInstanceId(QUuid::createUuid().toString(QUuid::Id128))
{
  setWindowTitle(tr("Repair audio anomaly"));

  if (mAvItem && mTrackIndex >= 0 && mTrackIndex < mAvItem->audioCount()) {
    TTAudioStream* stream = mAvItem->audioStreamAt(mTrackIndex);
    if (stream) mAudioFile = stream->filePath();
  }
  mFrameDurationMs = probeFrameDurationMs(mAudioFile);

  double frameRate = (mAvItem && mAvItem->videoStream()) ? mAvItem->videoStream()->frameRate() : 25.0;
  if (frameRate <= 0.0) frameRate = 25.0;

  qint64 approxFrom = 0, approxTo = 0;
  approxAc3RangeForMarker(mPoint, frameRate, mExtraFrameIndices, approxFrom, approxTo);
  mMarkerFrom = approxFrom;
  mMarkerTo   = approxTo;

  // The marker names the planes to preset (TTStreamPoint::audioChannelMask):
  // C+LFE for a finding of the LFE search, every plane of its frames for an
  // abrupt stop. A marker without the information (older project files)
  // gets C+LFE, the only kind of finding there was.
  quint8 initialMask = mPoint.audioChannelMask() != 0
      ? mPoint.audioChannelMask()
      : quint8((1u << 2) | (1u << 3));
  qint64 initFrom = approxFrom, initTo = approxTo;
  // True as soon as initFrom/initTo are real AC3 frame numbers rather than a
  // video-frame estimate: either the marker carries the scanner's own range
  // (final review I3) or an existing repair item is being edited. Decides
  // how the ms spin boxes are prefilled below.
  bool haveExactRange = mPoint.hasAudioFrameRange();

  if (mAvItem) {
    mExistingRepairIndex = mAvItem->findAudioRepairOverlapping(mTrackIndex, approxFrom, approxTo);
    if (mExistingRepairIndex >= 0) {
      const TTAudioRepairItem r = mAvItem->audioRepairList().at(mExistingRepairIndex);
      initFrom = r.frameFrom();
      initTo = r.frameTo();
      initialMask = r.channelMask();
      haveExactRange = true;
    }
  }

  // The view follows the repair method: the stored one when a repair is
  // edited, else the marker's kind.
  const bool editing = mExistingRepairIndex >= 0;
  const TTAudioRepairItem existing = editing ? mAvItem->audioRepairList().at(mExistingRepairIndex) : TTAudioRepairItem();
  mFadeOut = editing ? existing.isFadeOut() : mPoint.audioAnomalyKind() == AudioAnomalyKind::LastingStop;
  mDonorFill = editing ? existing.isDonorFill() : mPoint.audioAnomalyKind() == AudioAnomalyKind::Hole;
  mSamplesPerMs = qMax(1, qRound(TTAudioRepair::kAc3FrameSamples / mFrameDurationMs));
  bool stopFound = true;
  int  fadeLenMs = 20;
  if (mFadeOut) {
    if (editing) {
      mFadeEndSample  = existing.fadeEnd();
      mSilenceSamples = existing.silenceLength();
      mFadeMask       = existing.channelMask();
      fadeLenMs       = existing.fadeLength() / mSamplesPerMs;
    } else {
      QString searchError;
      const TTAudioRepair::StopPlacement sp = TTAudioRepair::findStop(mAudioFile, approxFrom, approxTo, &searchError);
      stopFound = sp.found;
      // Nothing found: the start of the marker's middle frame - the frame
      // in which the scan found the stop.
      mFadeEndSample  = sp.found ? sp.fadeEnd : ((approxFrom + approxTo) / 2) * TTAudioRepair::kAc3FrameSamples;
      mSilenceSamples = sp.found ? sp.silence : mSamplesPerMs;
      mFadeMask       = initialMask;
    }
  }

  double startMs, endMs;
  if (haveExactRange) {
    // Editing an existing item, or a marker that carries the scanner's own
    // AC3 frame range: show that range exactly, converted with the real
    // per-file frame duration. frameTo is INCLUSIVE, the End spin box shows
    // the range's exclusive end time - so (initTo + 1) frames, which makes
    // currentFrameTo() give initTo back unchanged (round trip, final review
    // I3).
    startMs = initFrom * mFrameDurationMs;
    endMs   = (initTo + 1) * mFrameDurationMs;
  } else {
    // New marker without an exact range (an older project file, or a
    // hand-placed marker): show the marker's own video-frame range,
    // extras-corrected (review fix 1) - frameIndex/duration are in the
    // video-time domain, but the frameIndex -> time step itself MUST invert
    // videoFrameForTime() the same way approxAc3RangeForMarker does, or a
    // stream with MPEG-2 field-picture extras shows a start time off by
    // seconds. duration() is end-exclusive, matching the End spin box.
    startMs = videoFrameToSeconds(mPoint.frameIndex(), frameRate, mExtraFrameIndices) * 1000.0;
    endMs   = startMs + double(mPoint.duration()) * 1000.0;
  }

  buildUi();

  if (mDonorFill) {
    const QList<int> donors = mAvItem ? mAvItem->donorCandidateTracks(mTrackIndex) : QList<int>();
    for (int track : donors)
      mCmbDonor->addItem(tr("Track %1 - %2").arg(track + 1).arg(QFileInfo(mAvItem->audioStreamAt(track)->filePath()).fileName()), track);
    // An enabled fill whose donor is still there is shown as stored; anything
    // else is searched - with the fill's own donor when that still exists,
    // else with the preset one.
    const bool ownDonor = editing && existing.isDonorFill() && donors.contains(existing.donorTrack());
    const bool stored   = ownDonor && existing.isEnabled();
    const int  preset   = ownDonor ? existing.donorTrack() : (mAvItem ? mAvItem->presetDonorTrack(mTrackIndex) : -1);
    mCmbDonor->setCurrentIndex(mCmbDonor->findData(preset));
    if (stored) {
      mFillItem = existing;
      mFillFound = true;
      showDonorFill();
    } else {
      searchDonorFill();
    }
    connect(mCmbDonor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { searchDonorFill(); });
  } else if (mFadeOut) {
    mShownFadeEndMs = qRound(mFadeEndSample / double(mSamplesPerMs));
    // Inside the marker's frames, so that the repair stays linked to its marker.
    const int lo = qMin(qRound(approxFrom * mFrameDurationMs), mShownFadeEndMs);
    const int hi = qMax(qRound((approxTo + 1) * mFrameDurationMs), mShownFadeEndMs);
    mSpinFadeEnd->setRange(lo, hi);
    mSpinFadeEnd->setSingleStep(1);
    mSpinFadeEnd->setValue(mShownFadeEndMs);
    mSpinFadeLen->setRange(10, 100);
    mSpinFadeLen->setValue(qBound(10, fadeLenMs, 100));
    mLblHint->setVisible(!stopFound);
    // The found position stays sample-exact: a step moves it by whole ms.
    connect(mSpinFadeEnd, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
      mFadeEndSample += qint64(ms - mShownFadeEndMs) * mSamplesPerMs;
      mShownFadeEndMs = ms;
    });
  } else {
    for (int ch = 0; ch < 6; ++ch)
      mChkChannel[ch]->setChecked((initialMask & (1u << ch)) != 0);

    const int step = qMax(1, qRound(mFrameDurationMs));
    const int lo = qMax(0, qRound(qMin(startMs, endMs)) - 5000);
    const int hi = qRound(qMax(startMs, endMs)) + 5000;
    mSpinFrom->setRange(lo, hi);
    mSpinTo->setRange(lo, hi);
    mSpinFrom->setSingleStep(step);
    mSpinTo->setSingleStep(step);
    mSpinFrom->setValue(qRound(startMs));
    mSpinTo->setValue(qRound(endMs));
  }

  QString header = tr("Track %1 - video frame %2, duration %3 ms\n%4")
      .arg(mTrackIndex + 1)
      .arg(mPoint.frameIndex())
      .arg(qRound(double(mPoint.duration()) * 1000.0))
      .arg(mPoint.description());
  if (mFadeOut) header += "\n" + tr("Repair: fade-out before the stop");
  if (mDonorFill) header += "\n" + tr("Repair: fill the hole from a second track");
  mLblHeader->setText(header);

  // Final-review Critical 1: the player MUST be built here, not on the first
  // Play click. Adding mpv's TTMpvRenderWidget (a QOpenGLWidget) to a dialog
  // whose exec() loop is ALREADY running makes Qt recreate the top-level's
  // native window to get a GL-compatible surface, and that recreation calls
  // hide() as an internal step - QDialogPrivate::hide_helper() exits exec()'s
  // event loop on ANY hide(). Production reaches Play through
  // TTStreamPointWidget's `dlg.exec()`, so the first Play click returned
  // Rejected mid-click and destroyed the stack-allocated dialog while mpv was
  // still loading: dialog gone, entries lost, no audition. Reproduced without
  // TTCut/mpv by a standalone Qt probe (QDialog::exec() + QOpenGLWidget added
  // from a click handler) and now covered live by
  // tools/diag/test_repairdialog_mpv_lifecycle, which drives dlg.exec().
  // Same order TTCutPreview uses (render widget built in its constructor,
  // before any show()/exec()).
  //
  // The offscreen exception keeps the documented lazy-construction reason
  // alive: QT_QPA_PLATFORM=offscreen gives mpv no GL context, and starting
  // the backend there hangs the model harness (test_repairdialog_model). Only
  // that platform is excluded - every real platform gets the player up front.
  if (QGuiApplication::platformName() != QLatin1String("offscreen"))
    ensurePlayer();
}

TTAudioRepairDialog::~TTAudioRepairDialog()
{
  // Review fix (Minor): clean up this instance's preview window copies -
  // they are only ever needed while the dialog is open.
  if (!mPreviewPathBefore.isEmpty()) QFile::remove(mPreviewPathBefore);
  if (!mPreviewPathAfter.isEmpty())  QFile::remove(mPreviewPathAfter);
}

void TTAudioRepairDialog::buildUi()
{
  mMainLayout = new QVBoxLayout(this);

  mLblHeader = new QLabel(this);
  mLblHeader->setWordWrap(true);
  mMainLayout->addWidget(mLblHeader);

  if (mDonorFill) {
    QGridLayout* fillLayout = new QGridLayout();
    fillLayout->addWidget(new QLabel(tr("Donor track"), this), 0, 0);
    mCmbDonor = new QComboBox(this);
    fillLayout->addWidget(mCmbDonor, 0, 1);
    fillLayout->addWidget(new QLabel(tr("Hole"), this), 1, 0);
    mLblHole = new QLabel(this);
    fillLayout->addWidget(mLblHole, 1, 1);
    fillLayout->addWidget(new QLabel(tr("Offset"), this), 2, 0);
    mLblShift = new QLabel(this);
    fillLayout->addWidget(mLblShift, 2, 1);
    fillLayout->addWidget(new QLabel(tr("Match"), this), 3, 0);
    mLblMatch = new QLabel(this);
    fillLayout->addWidget(mLblMatch, 3, 1);
    mMainLayout->addLayout(fillLayout);
    mLblFillMessage = new QLabel(this);
    mLblFillMessage->setWordWrap(true);
    mMainLayout->addWidget(mLblFillMessage);
  } else if (mFadeOut) {
    QGridLayout* fadeLayout = new QGridLayout();
    fadeLayout->addWidget(new QLabel(tr("Fade-out ends at (ms)"), this), 0, 0);
    mSpinFadeEnd = new QSpinBox(this);
    mSpinFadeEnd->setSuffix(tr(" ms"));
    fadeLayout->addWidget(mSpinFadeEnd, 0, 1);
    fadeLayout->addWidget(new QLabel(tr("Length (ms)"), this), 1, 0);
    mSpinFadeLen = new QSpinBox(this);
    mSpinFadeLen->setSuffix(tr(" ms"));
    fadeLayout->addWidget(mSpinFadeLen, 1, 1);
    mMainLayout->addLayout(fadeLayout);
    mLblHint = new QLabel(tr("No stop found. Please set the end of the fade-out by ear."), this);
    mLblHint->setWordWrap(true);
    mMainLayout->addWidget(mLblHint);
  } else {
    QGroupBox* channelBox = new QGroupBox(tr("Channels to silence"), this);
    QHBoxLayout* channelLayout = new QHBoxLayout(channelBox);
    static const char* kChannelLabels[6] = {
      QT_TR_NOOP("FL"), QT_TR_NOOP("FR"), QT_TR_NOOP("C"),
      QT_TR_NOOP("LFE"), QT_TR_NOOP("SL"), QT_TR_NOOP("SR")
    };
    for (int ch = 0; ch < 6; ++ch) {
      mChkChannel[ch] = new QCheckBox(tr(kChannelLabels[ch]), channelBox);
      channelLayout->addWidget(mChkChannel[ch]);
    }
    mMainLayout->addWidget(channelBox);

    QGridLayout* rangeLayout = new QGridLayout();
    rangeLayout->addWidget(new QLabel(tr("Start (ms)"), this), 0, 0);
    mSpinFrom = new QSpinBox(this);
    mSpinFrom->setSuffix(tr(" ms"));
    rangeLayout->addWidget(mSpinFrom, 0, 1);
    rangeLayout->addWidget(new QLabel(tr("End (ms)"), this), 1, 0);
    mSpinTo = new QSpinBox(this);
    mSpinTo->setSuffix(tr(" ms"));
    rangeLayout->addWidget(mSpinTo, 1, 1);
    mMainLayout->addLayout(rangeLayout);
  }

  QHBoxLayout* auditionLayout = new QHBoxLayout();
  mBtnPlayOriginal = new QPushButton(tr("Play original"), this);
  mBtnPlayRepaired = new QPushButton(tr("Play repaired"), this);
  auditionLayout->addWidget(mBtnPlayOriginal);
  auditionLayout->addWidget(mBtnPlayRepaired);
  mMainLayout->addLayout(auditionLayout);

  connect(mBtnPlayOriginal, &QPushButton::clicked, this, &TTAudioRepairDialog::onPlayOriginal);
  connect(mBtnPlayRepaired, &QPushButton::clicked, this, &TTAudioRepairDialog::onPlayRepaired);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // Accepting changes nothing yet: the repair is noted for the cut.
  mBtnPlan = buttons->button(QDialogButtonBox::Ok);
  mBtnPlan->setText(tr("Plan repair"));
  connect(buttons, &QDialogButtonBox::accepted, this, &TTAudioRepairDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &TTAudioRepairDialog::reject);
  mMainLayout->addWidget(buttons);
}

// Start spin box = start time of the FIRST repaired AC3 frame.
qint64 TTAudioRepairDialog::currentFrameFrom() const
{
  return qint64(qRound(mSpinFrom->value() / mFrameDurationMs));
}

// End spin box = EXCLUSIVE end time of the range, i.e. the start time of the
// first frame that is no longer repaired; TTAudioRepairItem::frameTo() is
// INCLUSIVE, hence the -1 (final review I3: the two conventions used to be
// mixed, which cost one AC3 frame on every round trip).
qint64 TTAudioRepairDialog::currentFrameTo() const
{
  return qint64(qRound(mSpinTo->value() / mFrameDurationMs)) - 1;
}

quint8 TTAudioRepairDialog::currentChannelMask() const
{
  quint8 mask = 0;
  for (int ch = 0; ch < 6; ++ch)
    if (mChkChannel[ch]->isChecked()) mask |= quint8(1u << ch);
  return mask;
}

TTAudioRepairItem TTAudioRepairDialog::currentItem() const
{
  // Nothing found: the marker's frames, so that the original can be played.
  if (mDonorFill)
    return mFillFound ? mFillItem : TTAudioRepairItem(mTrackIndex, mMarkerFrom, mMarkerTo, 0);
  if (mFadeOut)
    return TTAudioRepair::makeFadeOutItem(mTrackIndex, mFadeMask, mFadeEndSample,
                                          mSpinFadeLen->value() * mSamplesPerMs, mSilenceSamples,
                                          mSamplesPerMs * 1000);
  return TTAudioRepairItem(mTrackIndex, currentFrameFrom(), currentFrameTo(), currentChannelMask());
}

QString TTAudioRepairDialog::donorFile() const
{
  const int donor = mFillFound ? mFillItem.donorTrack() : -1;
  if (!mAvItem || donor < 0 || donor >= mAvItem->audioCount() || !mAvItem->audioStreamAt(donor)) return QString();
  return mAvItem->audioStreamAt(donor)->filePath();
}

void TTAudioRepairDialog::searchDonorFill()
{
  mFillFound = false;
  mFillMessage.clear();
  const int donor = mCmbDonor->currentIndex() >= 0 ? mCmbDonor->currentData().toInt() : -1;
  if (!mAvItem || donor < 0 || donor >= mAvItem->audioCount()) {
    mFillMessage = tr("There is no suitable donor track.");
    showDonorFill();
    return;
  }

  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  const TTAudioRepair::DonorFillSearch found = TTAudioRepair::findDonorFill(
      mAudioFile, mTrackIndex, mMarkerFrom, mMarkerTo, mAvItem->audioStreamAt(donor)->filePath(), donor,
      mAvItem->expectedDonorShift(mTrackIndex, donor));
  QGuiApplication::restoreOverrideCursor();

  switch (found.status) {
  case TTAudioRepair::DonorFillStatus::Found:
    // Inside the marker's frames, so that the repair stays linked to its marker.
    if (found.item.frameTo() < mMarkerFrom || found.item.frameFrom() > mMarkerTo) {
      mFillMessage = tr("The hole lies next to the marker.");
    } else {
      mFillItem = found.item;
      mFillFound = true;
    }
    break;
  case TTAudioRepair::DonorFillStatus::NoHole:            mFillMessage = tr("No hole found."); break;
  case TTAudioRepair::DonorFillStatus::NoSound:           mFillMessage = tr("There is no sound next to the hole to compare with."); break;
  case TTAudioRepair::DonorFillStatus::FormatChange:      mFillMessage = tr("The audio format changes next to the hole."); break;
  case TTAudioRepair::DonorFillStatus::UnsupportedLayout: mFillMessage = tr("This audio format cannot be filled."); break;
  case TTAudioRepair::DonorFillStatus::DonorUnreadable:   mFillMessage = tr("The donor track cannot be read."); break;
  case TTAudioRepair::DonorFillStatus::Error:             mFillMessage = found.error; break;   // the track's file error, as it is
  }
  if (!found.error.isEmpty())
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Donor-fill search on track %1 with donor track %2: %3").arg(mTrackIndex + 1).arg(donor + 1).arg(found.error));
  showDonorFill();
}

void TTAudioRepairDialog::showDonorFill()
{
  if (mFillFound) {
    const double rate = mSamplesPerMs * 1000.0;
    const qint64 expected = mAvItem ? mAvItem->expectedDonorShift(mTrackIndex, mFillItem.donorTrack()) : 0;
    const QLocale locale;
    mLblHole->setText(QTime(0, 0).addMSecs(int(mFillItem.holeStart() * 1000.0 / rate)).toString("h:mm:ss.zzz") + ", "
                      + locale.toString((mFillItem.holeEnd() - mFillItem.holeStart()) * 1000.0 / rate, 'f', 1) + tr(" ms"));
    mLblShift->setText(locale.toString((mFillItem.donorShift() - expected) * 1000.0 / rate, 'f', 1) + tr(" ms"));
    mLblMatch->setText(locale.toString(mFillItem.match(), 'f', 2));
    const bool hint = mFillItem.match() < kDonorFillHintMatch;
    mLblFillMessage->setText(hint ? tr("The donor track hardly matches here, the fill gains little.") : QString());
    mLblFillMessage->setVisible(hint);
  } else {
    mLblHole->setText(QStringLiteral("-"));
    mLblShift->setText(QStringLiteral("-"));
    mLblMatch->setText(QStringLiteral("-"));
    mLblFillMessage->setText(mFillMessage);
    mLblFillMessage->setVisible(true);
  }
  mBtnPlan->setEnabled(mFillFound);
  mBtnPlayRepaired->setEnabled(mFillFound);
}

void TTAudioRepairDialog::onMpvError(const QString& message)
{
  // Review fix 2: TTMpvLibBackend forwards EVERY mpv "error"-level log line
  // through playerError, most of it transient/non-fatal noise mid-playback
  // (h264 mmco warnings, "changing audio frame properties on the fly" -
  // see TTCutPreview::onPlayerError's and TTCurrentFrame's identical
  // log-only handling, both explicitly documented as deliberate). Always
  // log; only pop the QMessageBox the spec's Fehlerbild point 3 asks for
  // when this error arrives BEFORE playback ever started for the current
  // attempt - i.e. a genuine load/start failure, not chatter from an
  // audition that is actually working.
  TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
      QString("Repair preview player error: %1").arg(message));

  if (!mAwaitingPlaybackStart) {
    if (TTSettings::instance()->logUI())
      TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
          "Repair preview: error arrived after playback was already confirmed - log only, no popup");
    return;
  }
  mAwaitingPlaybackStart = false; // one dialog per failed attempt, not one per log line
  if (TTSettings::instance()->logUI())
    TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
        "Repair preview: error arrived before playback was ever confirmed for this attempt - showing popup");
  QMessageBox::warning(this, tr("Playback error"), message);
}

void TTAudioRepairDialog::onPlaybackConfirmed()
{
  if (TTSettings::instance()->logUI())
    TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
        "Repair preview: playbackRestarted received - this attempt is confirmed playing, clearing mAwaitingPlaybackStart");
  mAwaitingPlaybackStart = false;
}

void TTAudioRepairDialog::onPlayOriginal() { playPreview(false); }
void TTAudioRepairDialog::onPlayRepaired() { playPreview(true); }

void TTAudioRepairDialog::playPreview(bool repaired)
{
  QString error;
  QString path = writePreviewWindow(repaired, &error);
  if (path.isEmpty()) {
    QMessageBox::warning(this, tr("Repair preview"), error);
    return;
  }
  playFile(path);
}

void TTAudioRepairDialog::ensurePlayer()
{
  if (mPlayer) return;

  // Normally already done by the constructor (see the Critical-1 comment
  // there); this stays idempotent so the offscreen path - the one case the
  // constructor skips - still gets a player if something ever plays there.
  //
  // mpv needs a real render surface even for an audio-only file (in-process
  // libmpv backend renders into it); kept small since there is nothing to
  // actually show here (same wrapper TTCutPreview uses for video).
  mPlayer = new TTMpvWrapper(this);
  connect(mPlayer, &TTMpvWrapper::playerError,       this, &TTAudioRepairDialog::onMpvError);
  // playbackRestarted, NOT playerPlaying - see onPlaybackConfirmed()'s doc
  // comment (review fix 2, round 2) for why playerPlaying only fires once
  // per TTMpvWrapper instance and is therefore wrong for a dialog whose
  // Play buttons can be clicked more than once against the same mPlayer.
  connect(mPlayer, &TTMpvWrapper::playbackRestarted, this, &TTAudioRepairDialog::onPlaybackConfirmed);
  if (QWidget* rw = mPlayer->renderWidget()) {
    rw->setFixedHeight(1);
    mMainLayout->addWidget(rw);
  }
}

void TTAudioRepairDialog::playFile(const QString& path)
{
  ensurePlayer();
  // Review fix 2 (round 2): armed here, cleared by onPlaybackConfirmed()
  // once mpv actually confirms playback started for THIS load() call
  // (TTMpvWrapper::playbackRestarted, per-load - not the one-shot
  // playerPlaying the first round wrongly relied on).
  mAwaitingPlaybackStart = true;
  if (TTSettings::instance()->logUI())
    TTMessageLogger::getInstance()->infoMsg(__FILE__, __LINE__,
        QString("Repair preview: playFile(%1) - arming mAwaitingPlaybackStart").arg(path));
  mPlayer->load(path);
}

QString TTAudioRepairDialog::writePreviewWindow(bool repaired, QString* error)
{
  error->clear();

  if (mAudioFile.isEmpty()) {
    *error = tr("No audio file available for this track.");
    return QString();
  }

  const TTAudioRepairItem item = currentItem();
  const qint64 from = item.frameFrom();
  const qint64 to = item.frameTo();
  // A fade-out that would start before the track has from == -1: the builder
  // says so below; the original still plays, from the start of the track.
  if (!mFadeOut && (from < 0 || to < from)) {
    *error = tr("Invalid repair range.");
    return QString();
  }

  TTAudioRepair::FrameTable table;
  if (repaired) {
    QString buildError;
    table = TTAudioRepair::buildRepairTable(mAudioFile, item, /*targetAcmod=*/-1, &buildError, donorFile());
    if (!buildError.isEmpty()) {
      *error = buildError;
      return QString();
    }
  }

  // +/-3s window around the range, in AC3 frames - derived from the real
  // per-file frame duration (never hardcoded), see class comment.
  const qint64 marginFrames = qMax<qint64>(1, qint64(qRound(3000.0 / mFrameDurationMs)));
  const qint64 windowFrom = qMax<qint64>(0, from - marginFrames);
  const qint64 windowTo   = to + marginFrames;

  // Same open as TTAudioRepair::buildRepairTable; its message names the
  // libav error, which the dialog shows as is.
  AVFormatContext* fmtCtx = nullptr;
  int audioIdx = -1;
  if (!TTAudioRepair::openFirstAudioStream(mAudioFile, &fmtCtx, &audioIdx, error))
    return QString();

  // Review fix (Minor): mInstanceId makes this unique per dialog instance -
  // two dialogs open at once (or a stale file from a crashed prior run)
  // must never collide (reference_shared_tempdir_parallel_runs.md).
  const QString outPath = QDir(TTSettings::instance()->tempDirPath())
      .filePath(QStringLiteral("ttcut_repair_preview_%1_%2.ac3")
                    .arg(mInstanceId, repaired ? "after" : "before"));
  QFile out(outPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    avformat_close_input(&fmtCtx);
    *error = tr("Could not write %1").arg(outPath);
    return QString();
  }

  AVPacket* pkt = av_packet_alloc();
  if (!pkt) {
    avformat_close_input(&fmtCtx);
    out.close();
    *error = tr("Could not allocate packet for %1").arg(mAudioFile);
    return QString();
  }
  qint64 frameIdx = -1;
  while (av_read_frame(fmtCtx, pkt) >= 0) {
    if (pkt->stream_index != audioIdx) {
      av_packet_unref(pkt);
      continue;
    }
    ++frameIdx;
    if (frameIdx > windowTo) {
      av_packet_unref(pkt);
      break;
    }
    if (frameIdx >= windowFrom) {
      if (repaired && frameIdx >= from && frameIdx <= to && table.contains(frameIdx)) {
        const QByteArray replacement = table.value(frameIdx);
        out.write(replacement.constData(), replacement.size());
      } else {
        out.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
      }
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&fmtCtx);
  out.close();

  if (repaired) mPreviewPathAfter = outPath;
  else          mPreviewPathBefore = outPath;

  return outPath;
}

void TTAudioRepairDialog::accept()
{
  if (!mAvItem) {
    QDialog::accept();
    return;
  }

  const TTAudioRepairItem item = currentItem();
  // Nothing to plan (the button is disabled then; a key press must not get through either).
  if (mDonorFill && !mFillFound) return;
  if (!mFadeOut && !mDonorFill) {
    if (item.frameTo() < item.frameFrom()) {
      QMessageBox::warning(this, tr("Audio repair"), tr("End must not be before start."));
      return; // keep the dialog open, AVItem stays untouched
    }
    if (item.channelMask() == 0) {
      QMessageBox::warning(this, tr("Audio repair"), tr("Select at least one channel to silence."));
      return;
    }
  }
  // Build the replacement frames once, as the cut will: a range across a
  // channel-layout or frame-size change, a channel the track does not have,
  // a range past the file's end or a fade-out before its start is refused here, while the user can still
  // change it - not when the cut runs and fails (audio-repair.md H3). The
  // cut's target layout is not known here; like the audition, this builds
  // in the source layout.
  if (!mAudioFile.isEmpty()) {
    QString buildError;
    TTAudioRepair::buildRepairTable(mAudioFile, item, /*targetAcmod=*/-1, &buildError, donorFile());
    if (!buildError.isEmpty()) {
      QMessageBox::warning(this, tr("Audio repair"),
                           tr("This repair cannot be applied:\n%1").arg(buildError));
      return;
    }
  }

  if (mExistingRepairIndex >= 0)
    mAvItem->removeAudioRepairAt(mExistingRepairIndex);

  mAvItem->appendAudioRepair(item);

  QDialog::accept();
}
