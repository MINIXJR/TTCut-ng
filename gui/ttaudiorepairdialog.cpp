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
#include "../data/ttaudiorepairitem.h"
#include "../avstream/ttavstream.h"
#include "../extern/ttaudiorepair.h"
#include "../common/ttsettings.h"
#include "../common/ttmessagelogger.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QCheckBox>
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
    AVCodecParameters* cp = fmtCtx->streams[i]->codecpar;
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

  // v1 default: the scanner currently only ever reports C+LFE bursts (Task
  // 6, TTAudioAnomalyScanTask::operation()). The marker's description is a
  // fully localized tr() string with no locale-stable channel marker to
  // parse back out, so parsing it would break under a translated build;
  // this fixed default matches the spec's "bzw. Default C+LFE" fallback
  // directly instead.
  quint8 initialMask = (1u << 2) | (1u << 3); // C + LFE
  qint64 initFrom = approxFrom, initTo = approxTo;
  // True as soon as initFrom/initTo are real AC3 frame numbers rather than a
  // video-frame estimate: either the marker carries the scanner's own range
  // (final review I3) or an existing repair item is being edited. Decides
  // how the ms spin boxes are prefilled below.
  bool haveExactRange = mPoint.hasAudioFrameRange();

  if (mAvItem) {
    const QList<TTAudioRepairItem> repairs = mAvItem->audioRepairList();
    for (int i = 0; i < repairs.size(); ++i) {
      const TTAudioRepairItem& r = repairs.at(i);
      if (r.trackIndex() != mTrackIndex) continue;
      if (r.frameTo() < approxFrom || r.frameFrom() > approxTo) continue; // no overlap
      mExistingRepairIndex = i;
      initFrom = r.frameFrom();
      initTo = r.frameTo();
      initialMask = r.channelMask();
      haveExactRange = true;
      break;
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

  QString header = tr("Track %1 - video frame %2, duration %3 ms\n%4")
      .arg(mTrackIndex + 1)
      .arg(mPoint.frameIndex())
      .arg(qRound(double(mPoint.duration()) * 1000.0))
      .arg(mPoint.description());
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

  QHBoxLayout* auditionLayout = new QHBoxLayout();
  mBtnPlayOriginal = new QPushButton(tr("Play original"), this);
  mBtnPlayRepaired = new QPushButton(tr("Play repaired"), this);
  mBtnGotoFrame    = new QPushButton(tr("Go to frame"), this);
  auditionLayout->addWidget(mBtnPlayOriginal);
  auditionLayout->addWidget(mBtnPlayRepaired);
  auditionLayout->addWidget(mBtnGotoFrame);
  mMainLayout->addLayout(auditionLayout);

  connect(mBtnPlayOriginal, &QPushButton::clicked, this, &TTAudioRepairDialog::onPlayOriginal);
  connect(mBtnPlayRepaired, &QPushButton::clicked, this, &TTAudioRepairDialog::onPlayRepaired);
  connect(mBtnGotoFrame,    &QPushButton::clicked, this, &TTAudioRepairDialog::onGotoFrame);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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

void TTAudioRepairDialog::onGotoFrame()
{
  emit jumpToFrameRequested(mPoint.frameIndex());
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

void TTAudioRepairDialog::onPlayOriginal()
{
  QString error;
  QString path = writePreviewWindow(false, &error);
  if (path.isEmpty()) {
    QMessageBox::warning(this, tr("Repair preview"), error);
    return;
  }
  playFile(path);
}

void TTAudioRepairDialog::onPlayRepaired()
{
  QString error;
  QString path = writePreviewWindow(true, &error);
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

  const qint64 from = currentFrameFrom();
  const qint64 to = currentFrameTo();
  if (from < 0 || to < from) {
    *error = tr("Invalid repair range.");
    return QString();
  }

  TTAudioRepair::FrameTable table;
  if (repaired) {
    const TTAudioRepairItem item(mTrackIndex, from, to, currentChannelMask());
    QString buildError;
    table = TTAudioRepair::buildRepairTable(mAudioFile, item, /*targetAcmod=*/-1, &buildError);
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

  AVFormatContext* fmtCtx = nullptr;
  if (avformat_open_input(&fmtCtx, mAudioFile.toUtf8().constData(), nullptr, nullptr) < 0) {
    *error = tr("Could not open %1").arg(mAudioFile);
    return QString();
  }
  if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
    avformat_close_input(&fmtCtx);
    *error = tr("Could not read stream information for %1").arg(mAudioFile);
    return QString();
  }
  int audioIdx = -1;
  for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
    if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioIdx = int(i);
      break;
    }
  }
  if (audioIdx < 0) {
    avformat_close_input(&fmtCtx);
    *error = tr("No audio stream found in %1").arg(mAudioFile);
    return QString();
  }

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

  const qint64 from = currentFrameFrom();
  const qint64 to = currentFrameTo();
  if (to < from) {
    QMessageBox::warning(this, tr("Audio repair"), tr("End must not be before start."));
    return; // keep the dialog open, AVItem stays untouched
  }

  if (mExistingRepairIndex >= 0)
    mAvItem->removeAudioRepairAt(mExistingRepairIndex);

  mAvItem->appendAudioRepair(TTAudioRepairItem(mTrackIndex, from, to, currentChannelMask()));

  QDialog::accept();
}
