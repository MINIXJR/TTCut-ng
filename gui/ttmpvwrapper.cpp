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
  connect(mBackend, &ITTMpvBackend::fileLoaded,
          this,     &TTMpvWrapper::fileLoaded);
  connect(mBackend, &ITTMpvBackend::playbackRestarted,
          this,     &TTMpvWrapper::onPlaybackRestarted);
  connect(mBackend, &ITTMpvBackend::mpvError,
          this,     &TTMpvWrapper::playerError);
}

TTMpvWrapper::~TTMpvWrapper()
{
  if (mBackend)
    mBackend->shutdown();
  // mBackend is parented to this — Qt deletes it
}

void TTMpvWrapper::setKeepOpen(bool keepOpen)
{
  if (mBackend)
    mBackend->setKeepOpen(keepOpen);
}

QWidget* TTMpvWrapper::renderWidget()
{
  // Backend liefert sein eigenes Widget (libmpv). Lazy start: damit das
  // mWidget existiert, muss Backend.start() gelaufen sein. Wenn der
  // Caller renderWidget() vor load() ruft, ziehen wir start() hier vor.
  if (mBackend) {
    if (QWidget* w = mBackend->renderWidget())
      return w;
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
  mPlaying         = autoPlay;
  mPendingAutoPlay = autoPlay;
  mAtEnd = false;
  // The cached position belongs to the file being replaced. Without this it
  // survives into the new file until mpv's first time-pos arrives (~100 ms),
  // and TTCutPreview::onPrevCut() would read a stale "not at the start" and
  // jump to the beginning instead of to the previous cut.
  mPlaybackPosition = startSec;

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
  if (!mSubtitleFile.isEmpty()) {
    loadArgs << QString("--sub-file=%1").arg(mSubtitleFile);
    if (mSubtitleDelayMs != 0)
      loadArgs << QString("--sub-delay=%1").arg(mSubtitleDelayMs / 1000.0, 0, 'f', 3);
  }
  // IMMER pausiert laden. mpv führt den --start-Seek beim Laden aus und bleibt
  // auf dem Zielframe stehen. Bei sofortigem Abspielen rendert die Render-API
  // während des Seeks kurz den Lande-Keyframe (= Frame vor dem Cut-In, also
  // Werbung). Wir entpausen erst in onPlaybackRestarted(), wenn der gewählte
  // Frame dekodiert und anzeigebereit ist.
  loadArgs << QStringLiteral("--pause=yes");
  mBackend->command(loadArgs);
}

void TTMpvWrapper::play()
{
  // Resume from pause without tearing mpv down. The file must already be
  // loaded via load(); calling play() without a prior load() is a no-op.
  mBackend->setProperty("pause", false);
  mPlaying = true;
  mAtEnd = false;
  emit playerPlaying();
}

void TTMpvWrapper::pause()
{
  // Pause without shutting mpv down — the current frame stays on screen and
  // play() can resume in place.
  mPendingAutoPlay = false;   // expliziter Pause schlägt den verzögerten Unpause
  mBackend->setProperty("pause", true);
  mPlaying = false;
}

void TTMpvWrapper::seek(double seconds)
{
  // Absolute seek; mpv clears eof-reached itself when it lands.
  mBackend->command(QStringList() << QStringLiteral("seek")
                                  << QString::number(seconds, 'f', 3)
                                  << QStringLiteral("absolute"));
  mAtEnd = false;
  // Mirrors load(): keep the cached position in sync with the seek target
  // right away, instead of leaving it stale until mpv's next time-pos update
  // (~100 ms later).
  mPlaybackPosition = seconds;
}

void TTMpvWrapper::stop()
{
  // Hard shutdown: terminates the mpv process. Used by TTCurrentFrame's
  // Play/Stop toggle, where stopping returns control to the frame navigator.
  // For pause-style stopping that keeps the frame visible, use pause().
  //
  // NOTE: we do not pause() here because the backend's shutdown() already
  // handles it internally — a synchronous pause followed by a synchronous
  // time-pos read — so the last playback position is captured stably (and
  // propagated to mPlaybackPosition) before mpv_terminate_destroy tears the
  // handle down. A separate pause() here would be redundant.
  mPendingAutoPlay = false;
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
  mSubtitleDelayMs = 0;
}

void TTMpvWrapper::setSubtitleDelay(int delayMs)
{
  mSubtitleDelayMs = delayMs;
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

  if (name == QLatin1String("eof-reached") && value.isValid()) {
    const bool atEnd = value.toBool();
    if (atEnd && !mAtEnd) {
      // mpv pauses itself here (measured: pause=1 about 12 ms later). Report
      // the same playerFinished() the END_FILE path reports, so callers need
      // not know which mode they are in.
      mAtEnd   = true;
      mPlaying = false;
      emit playerFinished();
    } else if (!atEnd) {
      mAtEnd = false;
    }
  }
}

void TTMpvWrapper::onBackendConnected()
{
  // IPC socket is up — register the time-pos observer that drives the live
  // timecode. Start position / audio / subtitle were already passed as CLI
  // options in load(), so nothing else is needed here.
  mBackend->observeProperty("time-pos");
  // With keep-open the file is not unloaded at the end, so mpv emits no
  // END_FILE and the backend's playbackFinished() never fires. eof-reached
  // is the replacement source. Observing it unconditionally is harmless:
  // without keep-open, mpv does deliver an eof-reached change at EOF too, but
  // as an unavailable/NONE node (the file is already gone by then) —
  // onPropertyChanged()'s value.isValid() guard drops it, so TTCurrentFrame
  // (keep-open=no) never sees a spurious playerFinished() from this path.
  mBackend->observeProperty(QStringLiteral("eof-reached"));
  // Only signal "playing" when load() was called with autoPlay=true. In the
  // preloaded-paused case (autoPlay=false), the caller drives play()/pause()
  // explicitly and does not want a stray playerPlaying() at startup.
  if (mPlaying)
    emit playerPlaying();
}

void TTMpvWrapper::onBackendPlaybackFinished()
{
  // END_FILE path. With keep-open this does not fire at a natural end, but it
  // still does on error or shutdown. Set mAtEnd here as well as in the
  // eof-reached handler, so whichever source reports an ending first also
  // blocks the other one — a caller must never see two playerFinished() for
  // one ending.
  if (mAtEnd) return;
  mAtEnd   = true;
  mPlaying = false;
  emit playerFinished();
}

//! mpv hat den initialen --start-Seek abgeschlossen und zeigt den Zielframe.
//! Jetzt — und erst jetzt — entpausen, falls die Wiedergabe gewünscht war.
//! So ist der erste sichtbare Frame garantiert der gewählte, nie der
//! Lande-Keyframe davor.
void TTMpvWrapper::onPlaybackRestarted()
{
  // mpv hat den initialen --start-Seek abgeschlossen, der Zielframe ist
  // dekodiert und anzeigebereit. ZUERST das Signal weiterreichen, damit der
  // Caller (TTCurrentFrame) jetzt — und nicht früher — auf das renderWidget
  // umschaltet. DANN entpausen. Würden wir vor dem Stack-Switch entpausen,
  // liefe die Wiedergabe kurz hinter dem noch sichtbaren mpegWindow.
  emit playbackRestarted();

  if (mPendingAutoPlay) {
    mPendingAutoPlay = false;
    if (mBackend)
      mBackend->setProperty(QStringLiteral("pause"), false);
  }
}
