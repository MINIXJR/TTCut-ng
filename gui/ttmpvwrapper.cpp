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
  connect(mBackend, &ITTMpvBackend::fileLoaded,
          this,     &TTMpvWrapper::onBackendFileLoaded);
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
  mPendingStartSec  = startSec;
  mPendingAudioFile = audioFile;

  if (mTarget) mBackend->attachToWidget(mTarget);
  mBackend->start();
  mBackend->command(QStringList{"loadfile", file});
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

void TTMpvWrapper::onBackendFileLoaded()
{
  mPlaying = true;

  mBackend->observeProperty("time-pos");

  // 0.0 means "from the start" — no seek needed
  if (mPendingStartSec > 0.0)
    mBackend->setProperty("time-pos", mPendingStartSec);

  if (!mPendingAudioFile.isEmpty())
    mBackend->command(QStringList{"audio-add", mPendingAudioFile});

  if (!mSubtitleFile.isEmpty())
    mBackend->command(QStringList{"sub-add", mSubtitleFile});

  emit playerPlaying();
}

void TTMpvWrapper::onBackendPlaybackFinished()
{
  mPlaying = false;
  emit playerFinished();
}
