/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTMPVWRAPPER_H
#define TTMPVWRAPPER_H

#include <QObject>
#include <QString>

class ITTMpvBackend;
class QWidget;

// Öffentliche Player-Klasse. Hält ein ITTMpvBackend, bietet die High-Level-
// Player-API und cached beobachtete Properties (z.B. time-pos).
class TTMpvWrapper : public QObject
{
  Q_OBJECT
public:
  explicit TTMpvWrapper(QObject* parent = nullptr);
  ~TTMpvWrapper() override;

  // Keep mpv on the file at EOF instead of unloading it. Must be called before
  // the first renderWidget()/load() (both start the backend, and the option
  // only takes effect before mpv_initialize()). See ITTMpvBackend::setKeepOpen
  // for the measured behaviour.
  void   setKeepOpen(bool keepOpen);

  // Liefert das Render-Widget des Backends (libmpv). Nach der Phase-2-
  // Migration ist renderWidget() immer nicht-nullptr.
  QWidget* renderWidget();

  void   load(const QString& file, double startSec = 0.0,
              const QString& audioFile = QString(),
              bool autoPlay = true);
  void   play();    // resume from pause (file must already be loaded)
  void   pause();   // pause without tearing mpv down
  void   stop();    // hard shutdown: terminates mpv process
  // Absolute seek within the loaded file. Unlike load(), this keeps the file
  // open and the decoder warm; measured to work even from the eof-reached
  // state, which is how Play at the end restarts without a reload.
  void   seek(double seconds);

  // True once playback has reached its end, from either signal source.
  // Cleared by load(), play() and seek(); stop() does not clear it, but that
  // is harmless because every restart goes through one of those three first.
  bool   isAtEnd() const                     { return mAtEnd; }
  bool   isPlaying() const                  { return mPlaying; }

  void   setSpeed(double factor);            // ±-Faktor; <0 → play-dir=backward
  void   setSubtitleFile(const QString& path);
  void   clearSubtitleFile();
  void   setSubtitleDelay(int delayMs);

  double playbackPosition() const            { return mPlaybackPosition; }

signals:
  void playerPlaying();
  void playerFinished();
  void fileLoaded();   // mpv hat das File geladen, Decoder bereit (1. Frame im Anflug)
  // mpv hat den initialen --start-Seek abgeschlossen, der Zielframe ist
  // dekodiert/anzeigebereit. Caller schaltet erst jetzt auf das renderWidget.
  void playbackRestarted();
  void positionChanged(double seconds);
  void playerError(const QString& message);

private slots:
  void onPropertyChanged(const QString& name, const QVariant& value);
  void onBackendConnected();
  void onBackendPlaybackFinished();
  void onPlaybackRestarted();

private:
  ITTMpvBackend* mBackend          = nullptr;
  QString        mSubtitleFile;
  int            mSubtitleDelayMs = 0;  // mpv --sub-delay sign: positive = show later
  double         mPlaybackPosition = 0.0;
  bool           mPlaying          = false;
  bool           mAtEnd            = false;
  // Einmal-Flag: nach dem initialen --start-Seek (PLAYBACK_RESTART) entpausen.
  // Verhindert das Aufblitzen des Lande-Keyframes vor dem gewählten Frame.
  bool           mPendingAutoPlay  = false;
};

#endif // TTMPVWRAPPER_H
