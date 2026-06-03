/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTMPVRENDERWIDGET_H
#define TTMPVRENDERWIDGET_H

#include <QOpenGLWidget>

struct mpv_handle;
struct mpv_render_context;

// QOpenGLWidget-Host für die libmpv-MPV_RENDER_API. Das Widget besitzt
// keinen mpv_handle (der gehört zum Backend) — es bekommt ihn per
// ctor oder setMpv() übergeben und baut beim nächsten paintGL den
// Render-Context auf. Der Widget-Lifecycle ist vom mpv-Handle-Lifecycle
// entkoppelt: ein Backend-Restart darf das Widget wiederverwenden.
class TTMpvRenderWidget : public QOpenGLWidget
{
  Q_OBJECT
public:
  explicit TTMpvRenderWidget(mpv_handle* mpv, QWidget* parent = nullptr);
  ~TTMpvRenderWidget() override;

  // Vom Backend gerufen, wenn shutdown() den Render-Context im
  // GL-Thread freigeben muss. Synchronous, blockierend.
  void destroyRenderContext();

  // Render-Context wegwerfen und mpv-Handle abkoppeln. Widget bleibt
  // bestehen (zeigt schwarz bis ein neuer Handle per setMpv() kommt).
  void detachFromMpv();

  // Neuen mpv-Handle zuweisen. Existierender Render-Context wird vorher
  // freigegeben (war an den alten Handle gebunden); der nächste paintGL
  // baut für den neuen Handle einen frischen Context auf.
  void setMpv(mpv_handle* mpv);

  // time-pos des zuletzt tatsächlich in paintGL gerenderten Frames (-1 = keiner).
  double lastRenderedTimePos() const { return mLastRenderedTimePos; }

  // Erzwingt GL- und mpv-Render-Context VOR dem ersten paintGL — auch wenn das
  // Widget (noch) nicht sichtbar ist. Muss vom Caller VOR dem mpv-loadfile
  // gerufen werden: Im StackAll-Modus bekommt ein verstecktes QOpenGLWidget
  // beim ersten Play kein initializeGL/paintGL, sodass mpv das File ohne
  // Render-Ziel lädt ("No render context set" → Video-Output tot). makeCurrent()
  // realisiert das Widget bei Bedarf und triggert die GL-Initialisierung.
  // Liefert true, wenn der Render-Context steht.
  bool prepareRenderContext();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

private slots:
  void onMpvUpdate();      // queued vom mpv-Update-Callback ausgelöst

signals:
  void renderContextFailed(const QString& message);
  // Feuert genau einmal pro Play-Zyklus, sobald das Widget seinen ZWEITEN
  // echten mpv-Frame gerendert hat. Hintergrund (per Log belegt): mpv liefert
  // nach einem Lade-Seek den ersten Frame stale (GOP-Keyframe vor dem Ziel =
  // Werbe-Frame bei Cut-In nach Werbung), erst ab dem zweiten Render stimmt
  // der Inhalt. Der Caller schaltet erst auf dieses Widget um, wenn ein
  // nachweislich korrekter Frame drinsteht — bis dahin bleibt das mpegWindow-
  // Standbild sichtbar. Zähler wird in setMpv() je Play-Zyklus genullt.
  void firstFrameReady();

private:
  // libmpv-Update-Callback (läuft in einem libmpv-Thread!).
  static void onUpdateCallback(void* ctx);

  // Helfer für mpv_opengl_init_params: holt OpenGL-Funktionspointer
  // aus dem aktuellen QOpenGLContext.
  static void* getProcAddress(void* ctx, const char* name);

  // Idempotent: wenn mRenderCtx schon existiert oder mMpv null ist,
  // wird nichts gemacht. Sonst Render-Context bauen. Muss im GL-Thread
  // laufen (initializeGL oder paintGL).
  bool ensureRenderContext();

  mpv_handle*         mMpv          = nullptr;
  mpv_render_context* mRenderCtx    = nullptr;
  // Zähler echter mpv-Renders seit dem letzten setMpv (Play-Zyklus).
  // firstFrameReady feuert genau einmal, sobald er 2 erreicht.
  int                 mRenderedFrames = 0;
  // time-pos des zuletzt in paintGL gerenderten Frames (Stop-Position bei
  // vo=libmpv, siehe paintGL/onPlaybackFinished).
  double              mLastRenderedTimePos = -1.0;
};

#endif // TTMPVRENDERWIDGET_H
