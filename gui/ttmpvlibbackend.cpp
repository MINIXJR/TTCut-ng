/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttmpvlibbackend.h"
#include "ttmpvrenderwidget.h"

#include <QStringList>
#include <QVariant>
#include <QTimer>

extern "C" {
#include <mpv/client.h>
}

#include "../common/ttmessagelogger.h"

TTMpvLibBackend::TTMpvLibBackend(QObject* parent)
  : ITTMpvBackend(parent)
{
}

TTMpvLibBackend::~TTMpvLibBackend()
{
  shutdown();
}

bool TTMpvLibBackend::start()
{
  // Idempotent: voriges Backend sauber abreissen
  shutdown();

  mMpv = mpv_create();
  if (!mMpv) {
    emit mpvError(QStringLiteral("mpv_create() failed"));
    return false;
  }

  // Optionen VOR mpv_initialize setzen
  struct OptPair { const char* name; const char* value; };
  const OptPair opts[] = {
    { "osc",                      "no"        },
    { "input-default-bindings",   "no"        },
    { "keep-open",                "no"        },
    { "hr-seek",                  "yes"       },
    { "hr-seek-framedrop",        "no"        },
    { "vo",                       "libmpv"    },
    { "hwdec",                    "auto-safe" },
    { "idle",                     "yes"       },
  };
  for (const auto& o : opts) {
    int rc = mpv_set_option_string(mMpv, o.name, o.value);
    if (rc < 0) {
      TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("mpv_set_option_string(%1=%2) failed: %3")
          .arg(o.name).arg(o.value).arg(mpv_error_string(rc)));
    }
  }

  // Log-Level filtern (nur Errors/Fatal in mpvError leiten)
  mpv_request_log_messages(mMpv, "error");

  int initRc = mpv_initialize(mMpv);
  if (initRc < 0) {
    emit mpvError(QString("mpv_initialize failed: %1").arg(mpv_error_string(initRc)));
    mpv_terminate_destroy(mMpv);
    mMpv = nullptr;
    return false;
  }

  // Wakeup-Callback hängen
  mpv_set_wakeup_callback(mMpv, &TTMpvLibBackend::wakeupCallback, this);

  // Render-Widget anlegen; OpenGL-Context-Init passiert beim ersten
  // initializeGL (sobald das Widget einem Layout sichtbar hängt).
  mWidget = new TTMpvRenderWidget(mMpv, nullptr);

  // connected() asynchron emittieren, damit der Wrapper sich auch
  // dann anhängen kann, wenn er erst direkt nach start() den Slot
  // verbindet. Phase-1-Vertrag bleibt.
  QTimer::singleShot(0, this, [this](){ emit connected(); });

  return true;
}

void TTMpvLibBackend::shutdown()
{
  // 1. Wakeup-Callback abklemmen (verhindert späte drainEvents-Queues)
  if (mMpv)
    mpv_set_wakeup_callback(mMpv, nullptr, nullptr);

  // 2. Render-Widget abbauen — der mpv_render_context (falls schon
  //    erzeugt) wird im GL-Thread freigegeben (Task 6).
  if (mWidget) {
    mWidget->destroyRenderContext();
    if (!mWidget->parent())
      mWidget->deleteLater();
    mWidget = nullptr;
  }

  // 3. mpv-Handle terminieren (blockierend, sicher; emittiert keine
  //    Qt-Signals → kein use-after-free wie im Process-Backend)
  if (mMpv) {
    mpv_terminate_destroy(mMpv);
    mMpv = nullptr;
  }

  mPlaybackEndedEmitted = false;
  mNextObserveId = 1;
}

void TTMpvLibBackend::command(const QStringList& args)
{
  if (!mMpv || args.isEmpty()) return;

  // argv-Array für mpv_command_async aufbauen: jeweils const char*
  // aus QByteArray (utf8). Pointer-Lebensdauer = die Funktion.
  QList<QByteArray> utf8Holder;
  utf8Holder.reserve(args.size());
  for (const QString& s : args)
    utf8Holder.append(s.toUtf8());

  QVector<const char*> argv;
  argv.reserve(utf8Holder.size() + 1);
  for (const QByteArray& b : utf8Holder)
    argv.append(b.constData());
  argv.append(nullptr);

  int rc = mpv_command_async(mMpv, /*reply_userdata*/0, argv.data());
  if (rc < 0) {
    emit mpvError(QString("mpv_command_async(%1) failed: %2")
                    .arg(args.join(QLatin1Char(' ')))
                    .arg(mpv_error_string(rc)));
  }
}

void TTMpvLibBackend::setProperty(const QString& name, const QVariant& value)
{
  if (!mMpv) return;

  const QByteArray nameUtf8 = name.toUtf8();
  int rc = -1;

  switch (static_cast<int>(value.type())) {
    case QMetaType::Bool: {
      int flag = value.toBool() ? 1 : 0;
      rc = mpv_set_property_async(mMpv, 0, nameUtf8.constData(),
                                  MPV_FORMAT_FLAG, &flag);
      break;
    }
    case QMetaType::Double:
    case QMetaType::Float: {
      double d = value.toDouble();
      rc = mpv_set_property_async(mMpv, 0, nameUtf8.constData(),
                                  MPV_FORMAT_DOUBLE, &d);
      break;
    }
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong: {
      int64_t i = value.toLongLong();
      rc = mpv_set_property_async(mMpv, 0, nameUtf8.constData(),
                                  MPV_FORMAT_INT64, &i);
      break;
    }
    default: {
      // Fallback: als String. mpv parsed selber wenn nötig.
      const QByteArray valUtf8 = value.toString().toUtf8();
      const char* p = valUtf8.constData();
      rc = mpv_set_property_async(mMpv, 0, nameUtf8.constData(),
                                  MPV_FORMAT_STRING, &p);
      break;
    }
  }

  if (rc < 0) {
    emit mpvError(QString("mpv_set_property_async(%1) failed: %2")
                    .arg(name).arg(mpv_error_string(rc)));
  }
}

void TTMpvLibBackend::observeProperty(const QString& name)
{
  if (!mMpv) return;
  const QByteArray nameUtf8 = name.toUtf8();
  int rc = mpv_observe_property(mMpv, mNextObserveId++,
                                nameUtf8.constData(),
                                MPV_FORMAT_NODE);
  if (rc < 0) {
    emit mpvError(QString("mpv_observe_property(%1) failed: %2")
                    .arg(name).arg(mpv_error_string(rc)));
  }
}

QWidget* TTMpvLibBackend::renderWidget()
{
  return mWidget;
}

void TTMpvLibBackend::drainEvents()
{
  // wird in Task 8 implementiert
}

void TTMpvLibBackend::wakeupCallback(void* ctx)
{
  auto* self = static_cast<TTMpvLibBackend*>(ctx);
  QMetaObject::invokeMethod(self, "drainEvents", Qt::QueuedConnection);
}
