/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTMPVLIBBACKEND_H
#define TTMPVLIBBACKEND_H

#include "ittmpvbackend.h"
#include <QPointer>

struct mpv_handle;
class TTMpvRenderWidget;

// ITTMpvBackend-Implementierung: mpv als libmpv-Library in-process,
// gerendert in einen Qt-OpenGL-Kontext über die MPV_RENDER_API.
// Erlaubt Wayland-Betrieb ohne XCB-Zwang.
class TTMpvLibBackend : public ITTMpvBackend
{
  Q_OBJECT
public:
  explicit TTMpvLibBackend(QObject* parent = nullptr);
  ~TTMpvLibBackend() override;

  bool start() override;
  void shutdown() override;
  void attachToWidget(QWidget* /*target*/) override {}   // No-op: wir nutzen renderWidget()
  void detach() override {}                              // No-op
  void command(const QStringList& args) override;
  void setProperty(const QString& name, const QVariant& value) override;
  void observeProperty(const QString& name) override;

  QWidget* renderWidget() override;

private slots:
  void drainEvents();

private:
  // libmpv-Wakeup-Callback (läuft in einem libmpv-Thread!).
  static void wakeupCallback(void* ctx);

  mpv_handle*                 mMpv                  = nullptr;
  // QPointer statt raw: das Widget gehört dem Layout-Caller und wird im
  // Dialog-Cascade-Delete (videoFrame zuerst, dann Wrapper/Backend) freed.
  // QPointer nullt sich automatisch nach der Widget-Destruktion, sodass
  // shutdown()-Aufrufe im Backend-dtor nicht in dangling memory greifen.
  QPointer<TTMpvRenderWidget> mWidget;
  int                         mNextObserveId        = 1;
  bool                        mPlaybackEndedEmitted = false;
};

#endif // TTMPVLIBBACKEND_H
