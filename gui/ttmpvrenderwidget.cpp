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

// libmpv-Header werden in Task 6 angezogen.

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
  // wird in Task 6 implementiert
}

void TTMpvRenderWidget::initializeGL()
{
  // wird in Task 6 implementiert
}

void TTMpvRenderWidget::paintGL()
{
  // wird in Task 6 implementiert
}

void TTMpvRenderWidget::resizeGL(int /*w*/, int /*h*/)
{
  // wird in Task 6 implementiert
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
