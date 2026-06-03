/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvrenderwidget.h"

#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSet>
#include <QThread>

extern "C" {
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
}

#include "../common/ttmessagelogger.h"

namespace {
// Set aller lebenden TTMpvRenderWidget-Instanzen, mit Mutex synchronisiert.
// libmpv's render-Update-Callback kann aus einem mpv-Worker-Thread feuern,
// auch noch NACHDEM set_update_callback(null) gerufen wurde (mpv-Doku:
// "Setting the update callback to NULL does not guarantee no further calls").
// Wenn das Widget zwischenzeitlich zerstört wurde, hätten wir ein
// use-after-free im invokeMethod-Call. Die Live-Set + Mutex-Guard
// stellt sicher, dass der Callback einen schon-zerstörten Receiver
// erkennt und still abbricht.
static QMutex                          sLiveMutex;
static QSet<TTMpvRenderWidget*>        sLiveWidgets;
}

TTMpvRenderWidget::TTMpvRenderWidget(mpv_handle* mpv, QWidget* parent)
  : QOpenGLWidget(parent), mMpv(mpv)
{
  QMutexLocker l(&sLiveMutex);
  sLiveWidgets.insert(this);
}

TTMpvRenderWidget::~TTMpvRenderWidget()
{
  // 1. Aus Live-Set entfernen — synchron unter Mutex, dadurch wartet ein
  //    gleichzeitiger update-callback auf den Lock und sieht uns dann
  //    nicht mehr im Set. Kein invokeMethod auf dangling pointer mehr.
  {
    QMutexLocker l(&sLiveMutex);
    sLiveWidgets.remove(this);
  }
  // 2. Render-Context selbst freigeben. Im Standardfall (Backend.shutdown
  //    rief vorher detachFromMpv) ist das idempotent — mRenderCtx ist
  //    null und destroyRenderContext returnt sofort.
  //    ABER: bei Qt-Cascade-Delete des Vorschau-Dialogs zerstört Qt
  //    erst videoFrame (mit uns als Grandchild), dann mPlayer/Backend.
  //    Backend.shutdown findet dann mWidget (QPointer) bereits null,
  //    überspringt detachFromMpv — render-ctx bleibt unhandled, und
  //    mpv_terminate_destroy aborts wegen Inkonsistenz. Hier freien wir
  //    rechtzeitig — wir laufen VOR QOpenGLWidget-dtor, GL-Context noch
  //    valid.
  destroyRenderContext();
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

bool TTMpvRenderWidget::ensureRenderContext()
{
  if (mRenderCtx) return true;
  if (!mMpv)     return false;

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
    return false;
  }

  mpv_render_context_set_update_callback(mRenderCtx,
                                          &TTMpvRenderWidget::onUpdateCallback,
                                          this);
  return true;
}

void TTMpvRenderWidget::detachFromMpv()
{
  destroyRenderContext();
  mMpv = nullptr;

  // FBO sofort schwarz löschen. Sonst behält der QOpenGLWidget-Framebuffer
  // den letzten gerenderten Frame dieses Play-Zyklus. Beim nächsten Play
  // wird das renderWidget sichtbar geschaltet, BEVOR mpvs erstes paintGL
  // den neuen Frame eingemalt hat — der Compositor zeigt dann für einen
  // Moment den alten FBO-Inhalt (= stehengebliebenes Bild vom letzten Play).
  if (context() && isValid()) {
    makeCurrent();
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    doneCurrent();
  }
  update();   // paintGL malt jetzt schwarz
}

void TTMpvRenderWidget::setMpv(mpv_handle* mpv)
{
  // Alten Render-Context wegwerfen — der war an den alten Handle gebunden.
  destroyRenderContext();
  mMpv = mpv;
  mRenderedFrames = 0;   // firstFrameReady für neuen Play-Zyklus scharf
  mLastRenderedTimePos = -1.0;

  // Falls das Widget bereits einen QOpenGLContext hat (= initializeGL
  // lief mindestens einmal, z.B. beim ersten Play), den mpv-render-context
  // sofort SYNCHRON für den neuen Handle aufbauen. Sonst kommt der
  // erste loadfile-Command vom Wrapper an mpv durch, bevor paintGL
  // (queued via update()) ensureRenderContext gerufen hat → mpv hat
  // kein Render-Ziel, markiert vo/libmpv als kaputt, Wiedergabe bleibt
  // schwarz für den ganzen Replay-Zyklus.
  if (mpv && context()) {
    makeCurrent();
    ensureRenderContext();
    doneCurrent();
  }
  update();
}

bool TTMpvRenderWidget::prepareRenderContext()
{
  if (!mMpv) return false;
  // makeCurrent() realisiert ein noch nicht gezeigtes QOpenGLWidget und
  // erzeugt seinen GL-Context (löst bei Bedarf initializeGL aus). Danach steht
  // der GL-Context bereit, sodass ensureRenderContext den mpv-Render-Context
  // synchron aufbauen kann — VOR dem ersten loadfile, das sonst auf "No render
  // context set" liefe.
  makeCurrent();
  bool ok = ensureRenderContext();
  doneCurrent();
  return ok;
}

void TTMpvRenderWidget::initializeGL()
{
  // Erstmaliges Setup nach Show. Wenn mMpv noch nicht da ist (Widget wurde
  // im Backend-Restart-Zyklus mit setMpv() refurbisht), übernimmt paintGL
  // die Lazy-Init.
  ensureRenderContext();
}

void TTMpvRenderWidget::paintGL()
{
  if (!ensureRenderContext()) {
    // Fallback: schwarz, damit kein Müll-Frame stehenbleibt
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    return;
  }

  // libmpv Render-API-konform (render.h §258-261): mpv_render_context_render
  // NUR aufrufen, wenn der Core wirklich einen neuen Frame bereit hat
  // (MPV_RENDER_UPDATE_FRAME). Qt ruft paintGL auch bei show/switch/resize OHNE
  // neuen Frame; ein blinder render-Aufruf gibt dann einen Zwischen-/Altframe
  // aus — bei Play 2 den GOP-Keyframe vor dem Cut-In, also die Werbung. Ohne
  // neuen Frame behalten wir den bestehenden FBO-Inhalt und kehren zurück.
  uint64_t updFlags = mpv_render_context_update(mRenderCtx);
  bool hasNewFrame = (updFlags & MPV_RENDER_UPDATE_FRAME) != 0;

  if (!hasNewFrame)
    return;   // kein neuer Frame → FBO behält letzten Inhalt, kein Altframe

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

  // time-pos des GERADE gerenderten Frames merken. Bei vo=libmpv hängt die
  // sichtbare Anzeige der mpv-Clock um eine feste Pipeline-Tiefe hinterher;
  // dieser Wert ist die Zeit, die zum zuletzt tatsächlich gemalten Frame
  // gehört. TTCurrentFrame::onPlaybackFinished nutzt ihn als Stop-Position,
  // damit das Standbild dort stehen bleibt, wo der Nutzer es beim Stop sah
  // (statt zur voreilenden time-pos zu springen).
  if (mMpv) {
    double tp = -1.0;
    if (mpv_get_property(mMpv, "time-pos", MPV_FORMAT_DOUBLE, &tp) >= 0)
      mLastRenderedTimePos = tp;
  }

  // Erst der ZWEITE echte Render trägt garantiert korrekten Inhalt: mpv liefert
  // nach dem Lade-Seek den ersten Frame stale (Werbe-Keyframe vor dem Ziel).
  // Sobald wir ihn haben, einmalig signalisieren — der Caller schaltet dann
  // vom mpegWindow-Standbild auf dieses Widget um.
  ++mRenderedFrames;
  if (mRenderedFrames == 2)
    emit firstFrameReady();
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
  // libmpv-Thread → in den Qt-Thread queuen.
  // Guard: ctx könnte ein bereits zerstörtes Widget sein, weil
  // set_update_callback(null) NICHT garantiert dass mpv wartet bis
  // alle laufenden Callbacks fertig sind. Über sLiveWidgets prüfen,
  // unter sLiveMutex, der vom Widget-dtor gehalten wird.
  auto* self = static_cast<TTMpvRenderWidget*>(ctx);
  QMutexLocker l(&sLiveMutex);
  if (!sLiveWidgets.contains(self))
    return;   // Widget bereits zerstört, callback verwerfen
  QMetaObject::invokeMethod(self, "onMpvUpdate", Qt::QueuedConnection);
}

void* TTMpvRenderWidget::getProcAddress(void* /*ctx*/, const char* name)
{
  QOpenGLContext* glctx = QOpenGLContext::currentContext();
  if (!glctx) return nullptr;
  return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}
