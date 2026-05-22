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
#include "ttmpvprocessbackend.h"
#include <QVariant>
#include <QStringList>

TTMpvWrapper::TTMpvWrapper(QObject* parent)
  : QObject(parent)
{
  mBackend = new TTMpvProcessBackend(this);

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

void TTMpvWrapper::load(const QString& file, double startSec,
                        const QString& audioFile)
{
  // We are playing from now until stop()/playbackFinished — do not wait for a
  // (potentially missed) IPC "file-loaded" event to establish this.
  mPlaying = true;

  if (mTarget) mBackend->attachToWidget(mTarget);
  mBackend->start();

  // Start position, extra audio track and subtitle file are passed as mpv CLI
  // options. mpv applies them when it loads the file — race-free, independent
  // of when the IPC socket connects.
  QStringList loadArgs;
  loadArgs << QStringLiteral("loadfile") << file;
  if (startSec > 0.0)
    loadArgs << QString("--start=%1").arg(startSec, 0, 'f', 3);
  if (!audioFile.isEmpty())
    loadArgs << QString("--audio-file=%1").arg(audioFile);
  if (!mSubtitleFile.isEmpty())
    loadArgs << QString("--sub-file=%1").arg(mSubtitleFile);
  mBackend->command(loadArgs);
}

void TTMpvWrapper::stop()
{
  // Best-effort pause; mPlaybackPosition keeps the last value observed during playback.
  mBackend->setProperty("pause", true);
  mBackend->shutdown();
  mPlaying = false;
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
  emit playerPlaying();
}

void TTMpvWrapper::onBackendPlaybackFinished()
{
  mPlaying = false;
  emit playerFinished();
}
