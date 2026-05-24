/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvwrapper.h"
#include "ittmpvbackend.h"
#include "ttmpvlibbackend.h"
#include <QVariant>
#include <QStringList>

TTMpvWrapper::TTMpvWrapper(QObject* parent)
  : QObject(parent)
{
  mBackend = new TTMpvLibBackend(this);

  connect(mBackend, &ITTMpvBackend::propertyChanged,
          this,     &TTMpvWrapper::onPropertyChanged);
  connect(mBackend, &ITTMpvBackend::connected,
          this,     &TTMpvWrapper::onBackendConnected);
  connect(mBackend, &ITTMpvBackend::playbackFinished,
          this,     &TTMpvWrapper::onBackendPlaybackFinished);
  connect(mBackend, &ITTMpvBackend::mpvError,
          this,     &TTMpvWrapper::playerError);
}

TTMpvWrapper::~TTMpvWrapper()
{
  if (mBackend)
    mBackend->shutdown();
  // mBackend is parented to this — Qt deletes it
}

void TTMpvWrapper::setRenderTarget(QWidget* target)
{
  mTarget = target;
  mBackend->attachToWidget(target);
}

QWidget* TTMpvWrapper::renderWidget()
{
  // Backend liefert sein eigenes Widget (libmpv) oder nullptr (Process).
  // Lazy start: das libmpv-Backend muss start() gelaufen sein, damit
  // mWidget existiert. Wenn der Caller renderWidget() vor load() ruft,
  // ziehen wir start() hier vor.
  if (mBackend) {
    if (QWidget* w = mBackend->renderWidget())
      return w;
    // Backend kennt noch keinen Widget → start() lazy aufrufen
    mBackend->start();
    return mBackend->renderWidget();
  }
  return nullptr;
}

void TTMpvWrapper::load(const QString& file, double startSec,
                        const QString& audioFile, bool autoPlay)
{
  // mPlaying tracks "is mpv actively rendering frames". With autoPlay=false
  // mpv is launched paused (still shows first frame), so we are not playing.
  mPlaying = autoPlay;

  if (mTarget) mBackend->attachToWidget(mTarget);
  mBackend->start();

  // Start position, extra audio track, subtitle file and pause state are
  // passed as mpv CLI options. mpv applies them when it loads the file —
  // race-free, independent of when the IPC socket connects.
  QStringList loadArgs;
  loadArgs << QStringLiteral("loadfile") << file;
  if (startSec > 0.0)
    loadArgs << QString("--start=%1").arg(startSec, 0, 'f', 3);
  if (!audioFile.isEmpty())
    loadArgs << QString("--audio-file=%1").arg(audioFile);
  if (!mSubtitleFile.isEmpty())
    loadArgs << QString("--sub-file=%1").arg(mSubtitleFile);
  if (!autoPlay)
    loadArgs << QStringLiteral("--pause=yes");
  mBackend->command(loadArgs);
}

void TTMpvWrapper::play()
{
  // Resume from pause without tearing mpv down. The file must already be
  // loaded via load(); calling play() without a prior load() is a no-op.
  mBackend->setProperty("pause", false);
  // mpv quirk: when started with --pause=yes and then resumed via pause=no,
  // the video renderer can freeze on the first frame while audio plays. An
  // exact 0-offset seek forces a refresh and wakes the renderer up.
  mBackend->command(QStringList{ QStringLiteral("seek"), QStringLiteral("0"),
                                  QStringLiteral("relative+exact") });
  mPlaying = true;
  emit playerPlaying();
}

void TTMpvWrapper::pause()
{
  // Pause without shutting mpv down — the current frame stays on screen and
  // play() can resume in place.
  mBackend->setProperty("pause", true);
  mPlaying = false;
}

void TTMpvWrapper::stop()
{
  // Hard shutdown: terminates the mpv process. Used by TTCurrentFrame's
  // Play/Stop toggle, where stopping returns control to the frame navigator.
  // For pause-style stopping that keeps the frame visible, use pause().
  mBackend->setProperty("pause", true);
  mBackend->shutdown();
  // Emit playerFinished explicitly: shutdown() disconnects mProcess to avoid
  // use-after-free, so onProcessFinished → playbackFinished can no longer
  // fire. Without this emit, callers connected to playerFinished (e.g. the
  // Play/Stop button reset in TTCurrentFrame) would never wake up.
  if (mPlaying) {
    mPlaying = false;
    emit playerFinished();
  }
}

void TTMpvWrapper::setSpeed(double factor)
{
  if (factor >= 0.0) {
    mBackend->setProperty("play-dir", QString("forward"));
    mBackend->setProperty("speed", factor == 0.0 ? 1.0 : factor);
  } else {
    mBackend->setProperty("play-dir", QString("backward"));
    mBackend->setProperty("speed", -factor);
  }
  // Mute audio at any non-normal speed (fast forward / reverse)
  mBackend->setProperty("mute", factor != 1.0);
}

void TTMpvWrapper::setSubtitleFile(const QString& path)
{
  mSubtitleFile = path;
}

void TTMpvWrapper::clearSubtitleFile()
{
  mSubtitleFile.clear();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void TTMpvWrapper::onPropertyChanged(const QString& name, const QVariant& value)
{
  if (name == QLatin1String("time-pos") && value.isValid()) {
    mPlaybackPosition = value.toDouble();
    emit positionChanged(mPlaybackPosition);
  }
}

void TTMpvWrapper::onBackendConnected()
{
  // IPC socket is up — register the time-pos observer that drives the live
  // timecode. Start position / audio / subtitle were already passed as CLI
  // options in load(), so nothing else is needed here.
  mBackend->observeProperty("time-pos");
  // Only signal "playing" when load() was called with autoPlay=true. In the
  // preloaded-paused case (autoPlay=false), the caller drives play()/pause()
  // explicitly and does not want a stray playerPlaying() at startup.
  if (mPlaying)
    emit playerPlaying();
}

void TTMpvWrapper::onBackendPlaybackFinished()
{
  mPlaying = false;
  emit playerFinished();
}
