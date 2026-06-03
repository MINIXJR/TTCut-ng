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
#include <QCoreApplication>
#include <cstdlib>

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
  // Widget gehört Phase-2-konzeptuell dem Layout-Caller. Wenn niemand es
  // adoptiert hat (kein parent), löschen wir es hier. Sonst übernimmt das
  // der Layout-Owner (z.B. QStackedLayout im TTCurrentFrame).
  if (mWidget && !mWidget->parent()) {
    mWidget->deleteLater();
  }
  mWidget = nullptr;
}

bool TTMpvLibBackend::start()
{
  // Echte Idempotenz: ist mpv schon initialisiert, sind wir fertig.
  // Spec §"Initialisierung (einmal pro Backend-Instanz, in start())".
  // Wichtig fürs Wrapper-Pattern: renderWidget() ruft start() lazy
  // einmal (damit mWidget existiert und ins Layout gehängt werden kann),
  // load() ruft start() nochmal. Im Process-Backend war das
  // Tear-down-Rebuild; im libmpv-Pfad würde der zweite Aufruf den
  // bereits sichtbaren mWidget vom mpv_handle entkoppeln und ein neues,
  // unsichtbares Widget erzeugen (-> Schwarzbild im Preview).
  if (mMpv) return true;

  mMpv = mpv_create();
  if (!mMpv) {
    emit mpvError(QStringLiteral("mpv_create() failed"));
    return false;
  }

  // Optionen VOR mpv_initialize setzen. hwdec via ENV-Variable
  // MPV_HWDEC überschreibbar — z.B. "vaapi"/"vulkan"/"cuda" für
  // spezifische Backends, "auto-safe" für mpv-Auto-Wahl.
  // Default "no" (CPU-Decode): User-verifiziert dass auto-safe/vaapi/
  // vulkan auf manchen H.264-Patterns aus Smart-Cut Macroblock-Garbage
  // produzieren (vermutlich libva/mesa-Bug). CPU-Decode ist bei
  // 1080p25 problemlos und Bug-frei.
  const char* hwdecEnv = std::getenv("MPV_HWDEC");
  const char* hwdecVal = (hwdecEnv && *hwdecEnv) ? hwdecEnv : "no";
  struct OptPair { const char* name; const char* value; };
  const OptPair opts[] = {
    { "osc",                      "no"        },
    { "input-default-bindings",   "no"        },
    { "keep-open",                "no"        },
    { "hr-seek",                  "yes"       },
    { "hr-seek-framedrop",        "no"        },
    { "vo",                       "libmpv"    },
    { "hwdec",                    hwdecVal    },
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

  // Render-Widget anlegen oder reusen. Bei Backend-Restart (z.B. nach
  // Wrapper::stop()) lebt das Widget noch im Layout des Callers; wir
  // hängen ihm nur den neuen mpv_handle an. Beim ersten start() gibt es
  // noch keins → frisch anlegen.
  if (mWidget) {
    mWidget->setMpv(mMpv);
  } else {
    mWidget = new TTMpvRenderWidget(mMpv, nullptr);
  }

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

  // 2. Letzte time-pos noch synchron abfragen und propagieren, BEVOR der
  //    Handle terminiert. Der observe-property-Stream wird nur ~10×/sec
  //    emittiert; ohne dieses Sync-Update lägen die letzten bis zu ~100ms
  //    Wiedergabe für Caller wie TTCurrentFrame::onPlaybackFinished bei
  //    der mpegWindow-Frame-Anzeige im Dunkeln (sichtbarer Rückwärts-
  //    Sprung von wenigen Frames beim Stop). DirectConnection sorgt
  //    dafür, dass der Wrapper sein mPlaybackPosition synchron aktualisiert.
  if (mMpv) {
    // SYNC pause: friert mpv ein, damit die folgende time-pos-Lesung
    // stabil dem zuletzt gerenderten Frame entspricht. mpv_set_property
    // (sync) wartet bis pause angewendet ist; ohne hätte mpv im Hintergrund
    // weitergespielt.
    int paused = 1;
    mpv_set_property(mMpv, "pause", MPV_FORMAT_FLAG, &paused);

    double pos = 0.0;
    if (mpv_get_property(mMpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0)
      emit propertyChanged(QStringLiteral("time-pos"), pos);
  }

  // 3. Render-Context im Widget freigeben und vom mpv-Handle entkoppeln.
  //    Widget BLEIBT bestehen — es gehört Phase-2-konzeptuell dem
  //    Layout-Caller (QStackedLayout im TTCurrentFrame, videoFrame im
  //    TTCutPreview) und darf bei einem späteren start() per setMpv()
  //    den neuen Handle bekommen. Erst der dtor löscht es, und auch nur,
  //    wenn es niemand adoptiert hat.
  if (mWidget)
    mWidget->detachFromMpv();

  // 4. mpv-Handle terminieren (blockierend, sicher; emittiert keine
  //    Qt-Signals → kein use-after-free wie im Process-Backend)
  if (mMpv) {
    mpv_terminate_destroy(mMpv);
    mMpv = nullptr;
  }

  // 4. Bereits in der Qt-Event-Queue stehende drainEvents-/onMpvUpdate-Calls
  //    purgen. Schritt 1 (wakeup-disconnect) und destroyRenderContext
  //    (update-callback-disconnect) verhindern nur NEUE Posts; ein im
  //    Bruchteil einer Mikrosekunde zwischen mpv-Worker-Tick und unserem
  //    Disconnect bereits gequeueter invokeMethod-Call würde sonst nach
  //    der Backend/Widget-Destruktion auf eine dangling vtable feuern
  //    (verifiziert via core-dump: QApplicationPrivate::notify_helper
  //    SEGV mit *receiver = unzugängliche Adresse).
  QCoreApplication::removePostedEvents(this);
  if (mWidget)
    QCoreApplication::removePostedEvents(mWidget);

  mPlaybackEndedEmitted = false;
  mNextObserveId = 1;
}

void TTMpvLibBackend::command(const QStringList& args)
{
  if (!mMpv || args.isEmpty()) return;

  // Sonderfall: loadfile bekommt vom Wrapper Per-File-Optionen im CLI-Format
  // ("--start=10.5", "--pause=yes", "--audio-file=...", "--sub-file=..."). Das
  // ist der Phase-1-Vertrag: der Process-Backend reichte sie als mpv-CLI-Args
  // an den startenden mpv-Prozess durch. libmpv's loadfile-Command will sie
  // hingegen als
  //     loadfile <url> [<flag>] [<index>] [<options key=value,key=value>]
  // ("mpv --input-cmdlist" zeigt die Signatur). Wir transformieren alle
  // "--key=value"-Args zur options-Liste und ergänzen den Default-flag
  // "replace" wenn er fehlt.
  QStringList workArgs = args;
  if (workArgs.size() >= 3 && workArgs[0] == QLatin1String("loadfile")) {
    QStringList optsKv;
    QStringList rest;
    rest << workArgs[0] << workArgs[1];   // "loadfile" + url
    for (int i = 2; i < workArgs.size(); ++i) {
      const QString& a = workArgs[i];
      if (a.startsWith(QLatin1String("--")))
        optsKv << a.mid(2);              // "--key=val" → "key=val"
      else
        rest << a;                       // flag/index/durchreichen
    }
    if (!optsKv.isEmpty()) {
      // Signatur: loadfile <url> [<flag>] [<index>] [<options>].
      // Wenn options gesetzt werden sollen, müssen flag und index als
      // positional args explizit dastehen — sonst versucht mpv den
      // options-String als index zu parsen ("must be an integer").
      if (rest.size() == 2)              // flag fehlt
        rest << QStringLiteral("replace");
      if (rest.size() == 3)              // index fehlt (irrelevant bei replace, aber positional erforderlich)
        rest << QStringLiteral("0");
      rest << optsKv.join(QLatin1Char(','));
    }
    workArgs = rest;
  }

  // argv-Array für mpv_command_async aufbauen: jeweils const char*
  // aus QByteArray (utf8). Pointer-Lebensdauer = die Funktion.
  QList<QByteArray> utf8Holder;
  utf8Holder.reserve(workArgs.size());
  for (const QString& s : workArgs)
    utf8Holder.append(s.toUtf8());

  QVector<const char*> argv;
  argv.reserve(utf8Holder.size() + 1);
  for (const QByteArray& b : utf8Holder)
    argv.append(b.constData());
  argv.append(nullptr);

  int rc = mpv_command_async(mMpv, /*reply_userdata*/0, argv.data());
  if (rc < 0) {
    emit mpvError(QString("mpv_command_async(%1) failed: %2")
                    .arg(workArgs.join(QLatin1Char(' ')))
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
  if (!mMpv) return;

  while (true) {
    mpv_event* ev = mpv_wait_event(mMpv, 0.0);
    if (!ev || ev->event_id == MPV_EVENT_NONE)
      break;

    switch (ev->event_id) {
      case MPV_EVENT_PROPERTY_CHANGE: {
        auto* p = static_cast<mpv_event_property*>(ev->data);
        if (!p || !p->name) break;
        QVariant value;
        if (p->format == MPV_FORMAT_NODE && p->data) {
          mpv_node* node = static_cast<mpv_node*>(p->data);
          switch (node->format) {
            case MPV_FORMAT_DOUBLE: value = node->u.double_; break;
            case MPV_FORMAT_INT64:  value = QVariant::fromValue<qint64>(node->u.int64); break;
            case MPV_FORMAT_FLAG:   value = (node->u.flag != 0);  break;
            case MPV_FORMAT_STRING: value = QString::fromUtf8(node->u.string); break;
            default:                value = QVariant();           break;
          }
        }
        emit propertyChanged(QString::fromUtf8(p->name), value);
        break;
      }

      case MPV_EVENT_FILE_LOADED:
        emit fileLoaded();
        break;

      case MPV_EVENT_PLAYBACK_RESTART:
        // Seek abgeschlossen, erster Frame an Zielposition steht.
        emit playbackRestarted();
        break;

      case MPV_EVENT_END_FILE: {
        auto* e = static_cast<mpv_event_end_file*>(ev->data);
        if (e && e->reason == MPV_END_FILE_REASON_ERROR) {
          emit mpvError(QString("end-file error: %1")
                          .arg(mpv_error_string(e->error)));
        }
        if (e && e->reason == MPV_END_FILE_REASON_QUIT) {
          // Wir terminieren kontrolliert via mpv_terminate_destroy —
          // kein Signal nötig
          break;
        }
        if (!mPlaybackEndedEmitted) {
          mPlaybackEndedEmitted = true;
          emit playbackFinished();
        }
        break;
      }

      case MPV_EVENT_LOG_MESSAGE: {
        auto* m = static_cast<mpv_event_log_message*>(ev->data);
        if (m && m->text)
          emit mpvError(QString("[mpv:%1] %2")
                          .arg(m->prefix ? m->prefix : "?")
                          .arg(QString::fromUtf8(m->text).trimmed()));
        break;
      }

      case MPV_EVENT_COMMAND_REPLY:
      case MPV_EVENT_SET_PROPERTY_REPLY:
        if (ev->error < 0)
          emit mpvError(QString("mpv reply error: %1")
                          .arg(mpv_error_string(ev->error)));
        break;

      case MPV_EVENT_START_FILE:
        // Beim Start eines neuen Files den End-Guard wieder freigeben
        mPlaybackEndedEmitted = false;
        break;

      default:
        break;
    }
  }
}

void TTMpvLibBackend::wakeupCallback(void* ctx)
{
  auto* self = static_cast<TTMpvLibBackend*>(ctx);
  QMetaObject::invokeMethod(self, "drainEvents", Qt::QueuedConnection);
}
