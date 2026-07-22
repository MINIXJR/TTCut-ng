/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef ITTMPVBACKEND_H
#define ITTMPVBACKEND_H

#include <QObject>
#include <QStringList>
#include <QVariant>

class QWidget;

// Abstraktes mpv-Steuer-Interface. Bildet das gemeinsame Modell von
// mpv-IPC-JSON und libmpv-C-API ab (command / set_property / observe_property).
class ITTMpvBackend : public QObject
{
  Q_OBJECT
public:
  explicit ITTMpvBackend(QObject* parent = nullptr) : QObject(parent) {}
  ~ITTMpvBackend() override {}

  // Lifecycle
  virtual bool start()    = 0;   // Backend hochfahren
  virtual void shutdown() = 0;   // sauber beenden

  // Keep mpv on the file at EOF (mpv option "keep-open") instead of unloading
  // it. Must be set BEFORE start(); afterwards it has no effect, because the
  // options are frozen at mpv_initialize().
  //
  // Measured against libmpv 2.5.0 (2026-07-22):
  //   keep-open=no  -> MPV_EVENT_END_FILE reason=EOF, file unloaded
  //                    (filename=none, idle-active=1) - the picture is gone.
  //   keep-open=yes -> NO END_FILE. Instead eof-reached=1, mpv pauses itself,
  //                    the file stays loaded and time-pos rests on the last
  //                    frame.
  // At this backend layer, END_FILE is still the only source of
  // playbackFinished() — a caller of ITTMpvBackend directly (bypassing
  // TTMpvWrapper) using keep-open=yes loses that signal at a natural end and
  // must detect it via the "eof-reached" property instead. TTMpvWrapper
  // already does this itself (observes eof-reached and re-derives its own
  // playerFinished() from it), so this limitation does not reach
  // TTMpvWrapper's own callers.
  virtual void setKeepOpen(bool keepOpen) = 0;

  // mpv-Steuermodell
  virtual void command(const QStringList& args)                     = 0;
  virtual void setProperty(const QString& name, const QVariant& v)  = 0;
  virtual void observeProperty(const QString& name)                 = 0;

  // Render-Widget des Backends. Phase-2-Vertrag: jeder Backend MUSS ein
  // (QOpenGL-)Widget liefern, das vom Caller ins eigene Layout gehängt
  // wird. Lebenszeit des Widgets ist vom mpv-Handle entkoppelt — siehe
  // TTMpvRenderWidget::setMpv / detachFromMpv.
  virtual QWidget* renderWidget() = 0;

signals:
  void propertyChanged(const QString& name, const QVariant& value);
  void connected();        // IPC connection established — safe to send commands
  void fileLoaded();
  // Feuert nach jedem abgeschlossenen Seek, sobald der erste Frame an der
  // Zielposition dekodiert und anzeigebereit ist (mpv PLAYBACK_RESTART).
  // Wird genutzt, um nach dem initialen --start-Seek erst dann zu entpausen,
  // wenn der gewählte Frame steht — sonst blitzt der Lande-Keyframe (vor dem
  // Cut-In, also Werbung) für einen Frame auf.
  void playbackRestarted();
  void playbackFinished();
  void mpvError(const QString& message);
};

#endif // ITTMPVBACKEND_H
