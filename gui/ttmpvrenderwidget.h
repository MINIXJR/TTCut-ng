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
// keinen mpv_handle (der gehört zum Backend) — es bekommt ihn nur per
// ctor übergeben, damit es beim ersten initializeGL den Render-Context
// erzeugen kann.
class TTMpvRenderWidget : public QOpenGLWidget
{
  Q_OBJECT
public:
  explicit TTMpvRenderWidget(mpv_handle* mpv, QWidget* parent = nullptr);
  ~TTMpvRenderWidget() override;

  // Vom Backend gerufen, wenn shutdown() den Render-Context im
  // GL-Thread freigeben muss. Synchronous, blockierend.
  void destroyRenderContext();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

private slots:
  void onMpvUpdate();   // queued vom mpv-Update-Callback ausgelöst

signals:
  void renderContextFailed(const QString& message);

private:
  // libmpv-Update-Callback (läuft in einem libmpv-Thread!).
  static void onUpdateCallback(void* ctx);

  // Helfer für mpv_opengl_init_params: holt OpenGL-Funktionspointer
  // aus dem aktuellen QOpenGLContext.
  static void* getProcAddress(void* ctx, const char* name);

  mpv_handle*         mMpv          = nullptr;
  mpv_render_context* mRenderCtx    = nullptr;
};

#endif // TTMPVRENDERWIDGET_H
