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

  void   setRenderTarget(QWidget* target);
  QWidget* renderWidget();  // Returns backend's widget (libmpv) or nullptr (process)

  void   load(const QString& file, double startSec = 0.0,
              const QString& audioFile = QString(),
              bool autoPlay = true);
  void   play();    // resume from pause (file must already be loaded)
  void   pause();   // pause without tearing mpv down
  void   stop();    // hard shutdown: terminates mpv process
  bool   isPlaying() const                  { return mPlaying; }

  void   setSpeed(double factor);            // ±-Faktor; <0 → play-dir=backward
  void   setSubtitleFile(const QString& path);
  void   clearSubtitleFile();

  double playbackPosition() const            { return mPlaybackPosition; }

signals:
  void playerPlaying();
  void playerFinished();
  void positionChanged(double seconds);
  void playerError(const QString& message);

private slots:
  void onPropertyChanged(const QString& name, const QVariant& value);
  void onBackendConnected();
  void onBackendPlaybackFinished();

private:
  ITTMpvBackend* mBackend          = nullptr;
  QWidget*       mTarget           = nullptr;
  QString        mSubtitleFile;
  double         mPlaybackPosition = 0.0;
  bool           mPlaying          = false;
};

#endif // TTMPVWRAPPER_H
