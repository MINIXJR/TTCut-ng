/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvrenderwidget.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>

extern "C" {
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
}

#include "../common/ttmessagelogger.h"

TTMpvRenderWidget::TTMpvRenderWidget(mpv_handle* mpv, QWidget* parent)
  : QOpenGLWidget(parent), mMpv(mpv)
{
}

TTMpvRenderWidget::~TTMpvRenderWidget()
{
  // mRenderCtx muss vorher per destroyRenderContext() aus dem GL-Thread
  // freigegeben worden sein; wenn das Backend das vergisst, ist der
  // d'tor zu spät dran. Leak-Schutz: nichts tun.
}

void TTMpvRenderWidget::destroyRenderContext()
{
  if (!mRenderCtx) return;

  // Update-Callback abklemmen, damit kein Spät-Update mehr reinkommt
  mpv_render_context_set_update_callback(mRenderCtx, nullptr, nullptr);

  // mpv_render_context_free MUSS im GL-Thread laufen → makeCurrent davor
  if (QThread::currentThread() == this->thread()) {
    makeCurrent();
    mpv_render_context_free(mRenderCtx);
    doneCurrent();
  } else {
    // Aus fremdem Thread (z.B. shutdown() vom Qt-Thread, wenn dieser
    // nicht zugleich Render-Thread ist): blockierend in Widget-Thread
    // queuen. Bei QOpenGLWidget ist Widget-Thread = Qt-Thread, daher
    // nutzen wir invokeMethod nur als Sicherheitsnetz.
    mpv_render_context* ctx = mRenderCtx;
    QMetaObject::invokeMethod(this, [this, ctx](){
      makeCurrent();
      mpv_render_context_free(ctx);
      doneCurrent();
    }, Qt::BlockingQueuedConnection);
  }
  mRenderCtx = nullptr;
}

void TTMpvRenderWidget::initializeGL()
{
  if (!mMpv) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
      QStringLiteral("TTMpvRenderWidget::initializeGL: mpv handle is null"));
    return;
  }

  mpv_opengl_init_params glInit{};
  glInit.get_proc_address     = &TTMpvRenderWidget::getProcAddress;
  glInit.get_proc_address_ctx = this;

  mpv_render_param params[] = {
    { MPV_RENDER_PARAM_API_TYPE,
      const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL) },
    { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
    { MPV_RENDER_PARAM_INVALID, nullptr },
  };

  int rc = mpv_render_context_create(&mRenderCtx, mMpv, params);
  if (rc < 0 || !mRenderCtx) {
    const QString msg = QString("mpv_render_context_create failed: %1")
                          .arg(mpv_error_string(rc));
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__, msg);
    emit renderContextFailed(msg);
    mRenderCtx = nullptr;
    return;
  }

  mpv_render_context_set_update_callback(mRenderCtx,
                                          &TTMpvRenderWidget::onUpdateCallback,
                                          this);
}

void TTMpvRenderWidget::paintGL()
{
  if (!mRenderCtx) {
    // Fallback: schwarz, damit kein Müll-Frame stehenbleibt
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    return;
  }

  const qreal dpr = devicePixelRatioF();
  mpv_opengl_fbo fbo{};
  fbo.fbo = static_cast<int>(defaultFramebufferObject());
  fbo.w   = static_cast<int>(width()  * dpr);
  fbo.h   = static_cast<int>(height() * dpr);
  fbo.internal_format = 0;   // 0 = mpv wählt

  int flipY = 1;
  mpv_render_param params[] = {
    { MPV_RENDER_PARAM_OPENGL_FBO, &fbo  },
    { MPV_RENDER_PARAM_FLIP_Y,     &flipY },
    { MPV_RENDER_PARAM_INVALID,    nullptr },
  };
  mpv_render_context_render(mRenderCtx, params);
}

void TTMpvRenderWidget::resizeGL(int /*w*/, int /*h*/)
{
  // FBO-Maße werden in jedem paintGL aus width()/height()/devicePixelRatio
  // neu gebaut — hier nichts zu tun. Hook bleibt für künftige Anpassungen.
  update();
}

void TTMpvRenderWidget::onMpvUpdate()
{
  update();
}

void TTMpvRenderWidget::onUpdateCallback(void* ctx)
{
  // libmpv-Thread → in den Qt-Thread queuen
  auto* self = static_cast<TTMpvRenderWidget*>(ctx);
  QMetaObject::invokeMethod(self, "onMpvUpdate", Qt::QueuedConnection);
}

void* TTMpvRenderWidget::getProcAddress(void* /*ctx*/, const char* name)
{
  QOpenGLContext* glctx = QOpenGLContext::currentContext();
  if (!glctx) return nullptr;
  return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}
