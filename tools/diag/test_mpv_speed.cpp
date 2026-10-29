// Gate for the playback speed properties (TTMpvWrapper::speedPropertiesFor,
// applied by setSpeed() and, for 1x, by load() before every file).
//
// Two defects, measured 2026-10-28 on MPEG-2 with an external audio ES:
//  - Reverse only muted the audio, the track stayed selected, and mpv decoded
//    the audio ES (no index) backwards too: on a 1800 s recording J took over
//    12 s to start reversing, with aid=no 0.1-0.2 s. Reverse now deselects
//    the track first and forward selects it again.
//  - play-dir, speed, mute and aid are runtime properties and survive
//    loadfile. A reverse run that reaches the file start ends the file
//    without TTCut tearing mpv down, so the next Play ran backwards, at 2x
//    and muted while the label said 1x. load() now resets them first.
//
// Part 1 checks the pure list. Part 2 drives a real libmpv instance (vo=null,
// ao=null, options and async calls as TTMpvLibBackend uses them) on the Tux
// MPEG-2 ES with its external MP2: reverse, back to forward mid-file, reverse
// to the file start, then a reload without and with the reset. No timing
// verdict - the start delay depends on the load of the machine.
//
// Usage: test_mpv_speed <video.m2v> <audio.mp2>
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QVariant>
#include <clocale>
#include <cstdio>
#include <functional>
#include <mpv/client.h>

#include "gui/ttmpvlibbackend.h"
#include "gui/ttmpvwrapper.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, const QString& detail = QString())
{
  printf("%s: %s%s\n", ok ? "PASS" : "FAIL", what,
         detail.isEmpty() ? "" : qPrintable(QString(" (%1)").arg(detail)));
  if (!ok) failures++;
}

QString describe(const QList<QPair<QString, QVariant>>& props)
{
  QStringList parts;
  for (const auto& p : props)
    parts << p.first + "=" + p.second.toString();
  return parts.join(", ");
}

int indexOf(const QList<QPair<QString, QVariant>>& props, const QString& name)
{
  for (int i = 0; i < props.size(); ++i)
    if (props[i].first == name) return i;
  return -1;
}

QVariant valueOf(const QList<QPair<QString, QVariant>>& props, const QString& name)
{
  const int i = indexOf(props, name);
  return i < 0 ? QVariant() : props[i].second;
}

//! Same type mapping and the same async call as TTMpvLibBackend::setProperty,
//! so the order mpv sees is the order the wrapper produces.
void setAsync(mpv_handle* m, const QString& name, const QVariant& value)
{
  const QByteArray n = name.toUtf8();
  switch (value.typeId()) {
    case QMetaType::Bool: {
      int flag = value.toBool() ? 1 : 0;
      mpv_set_property_async(m, 0, n.constData(), MPV_FORMAT_FLAG, &flag);
      break;
    }
    case QMetaType::Double: {
      double d = value.toDouble();
      mpv_set_property_async(m, 0, n.constData(), MPV_FORMAT_DOUBLE, &d);
      break;
    }
    default: {
      const QByteArray v = value.toString().toUtf8();
      const char* p = v.constData();
      mpv_set_property_async(m, 0, n.constData(), MPV_FORMAT_STRING, &p);
      break;
    }
  }
}

void applySpeed(mpv_handle* m, double factor)
{
  for (const auto& p : TTMpvWrapper::speedPropertiesFor(factor))
    setAsync(m, p.first, p.second);
}

//! loadfile the way TTMpvWrapper::load builds it (CLI-form options, folded by
//! ttMpvLoadfileCommand), always paused like the wrapper.
void loadAsync(mpv_handle* m, const QString& video, const QString& audio, double startSec)
{
  const QStringList args = ttMpvLoadfileCommand({
      "loadfile", video, QString("--start=%1").arg(startSec, 0, 'f', 3),
      "--audio-file=" + audio, "--pause=yes"});
  QList<QByteArray> utf8;
  for (const QString& s : args) utf8.append(s.toUtf8());
  QVector<const char*> argv;
  for (const QByteArray& b : utf8) argv.append(b.constData());
  argv.append(nullptr);
  mpv_command_async(m, 0, argv.data());
}

QString prop(mpv_handle* m, const char* name)
{
  char* s = mpv_get_property_string(m, name);
  const QString r = s ? QString::fromUtf8(s) : QStringLiteral("<unavailable>");
  mpv_free(s);
  return r;
}

double timePos(mpv_handle* m)
{
  double t = -1.0;
  if (mpv_get_property(m, "time-pos", MPV_FORMAT_DOUBLE, &t) < 0) return -1.0;
  return t;
}

bool audioTrackSelected(mpv_handle* m)
{
  return prop(m, "current-tracks/audio/id") != QLatin1String("<unavailable>");
}

//! Pump events until \a done returns true for one of them, or until the
//! timeout. A PLAYBACK_RESTART unpauses, as TTMpvWrapper::onPlaybackRestarted
//! does. \a endReason receives the reason of an END_FILE seen on the way.
bool pumpUntil(mpv_handle* m, double timeoutSec,
               const std::function<bool(mpv_event*)>& done,
               int* endReason = nullptr)
{
  for (int i = 0; i < int(timeoutSec / 0.05); ++i) {
    mpv_event* ev = mpv_wait_event(m, 0.05);
    if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) {
      int no = 0;
      mpv_set_property_async(m, 0, "pause", MPV_FORMAT_FLAG, &no);
    }
    if (ev->event_id == MPV_EVENT_END_FILE && endReason)
      *endReason = static_cast<mpv_event_end_file*>(ev->data)->reason;
    if (done(ev)) return true;
  }
  return false;
}

bool waitEvent(mpv_handle* m, mpv_event_id id, double timeoutSec, int* endReason = nullptr)
{
  return pumpUntil(m, timeoutSec, [id](mpv_event* ev) { return ev->event_id == id; },
                   endReason);
}

bool waitTimePos(mpv_handle* m, double timeoutSec, const std::function<bool(double)>& cond)
{
  return pumpUntil(m, timeoutSec, [m, &cond](mpv_event*) {
    const double t = timePos(m);
    return t >= 0.0 && cond(t);
  });
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses a comma-decimal locale

  if (argc < 3) {
    fprintf(stderr, "usage: %s <video.m2v> <audio.mp2>\n", argv[0]);
    return 2;
  }
  const QString video = argv[1];
  const QString audio = argv[2];

  // --- Part 1: the property list ------------------------------------------
  const auto rev2 = TTMpvWrapper::speedPropertiesFor(-2.0);
  printf("  -2x: %s\n", qPrintable(describe(rev2)));
  check(valueOf(rev2, "aid").toString() == "no", "reverse deselects the audio track");
  check(indexOf(rev2, "aid") >= 0 && indexOf(rev2, "aid") < indexOf(rev2, "play-dir"),
        "reverse deselects the track before switching direction");
  check(valueOf(rev2, "play-dir").toString() == "backward" &&
        valueOf(rev2, "speed").toDouble() == 2.0 && valueOf(rev2, "mute").toBool(),
        "reverse -2x: backward, speed 2, muted");
  check(valueOf(TTMpvWrapper::speedPropertiesFor(-4.0), "speed").toDouble() == 4.0,
        "reverse -4x: speed 4");

  const auto fwd1 = TTMpvWrapper::speedPropertiesFor(1.0);
  printf("  1x: %s\n", qPrintable(describe(fwd1)));
  check(valueOf(fwd1, "play-dir").toString() == "forward" &&
        valueOf(fwd1, "speed").toDouble() == 1.0 &&
        valueOf(fwd1, "aid").toString() == "auto" &&
        valueOf(fwd1, "mute").isValid() && !valueOf(fwd1, "mute").toBool(),
        "1x: forward, speed 1, track selected, not muted");

  const auto fwd2 = TTMpvWrapper::speedPropertiesFor(2.0);
  check(valueOf(fwd2, "play-dir").toString() == "forward" &&
        valueOf(fwd2, "aid").toString() == "auto" && valueOf(fwd2, "mute").toBool(),
        "2x: forward, track selected, muted");

  // --- Part 2: live libmpv ------------------------------------------------
  if (!QFile::exists(video) || !QFile::exists(audio)) {
    check(false, "fixture files exist", video + " / " + audio);
    return 1;
  }

  mpv_handle* m = mpv_create();
  // The options of TTMpvLibBackend::start that matter here (keep-open=no as
  // in TTCurrentFrame), output to null devices.
  mpv_set_option_string(m, "vo", "null");
  mpv_set_option_string(m, "ao", "null");
  mpv_set_option_string(m, "idle", "yes");
  mpv_set_option_string(m, "terminal", "no");
  mpv_set_option_string(m, "keep-open", "no");
  mpv_set_option_string(m, "hr-seek", "yes");
  mpv_set_option_string(m, "hr-seek-framedrop", "no");
  if (mpv_initialize(m) < 0) {
    check(false, "libmpv initialised");
    mpv_destroy(m);
    return 1;
  }

  // Play forward from 5 s, as a Play from a still does.
  loadAsync(m, video, audio, 5.0);
  check(waitEvent(m, MPV_EVENT_PLAYBACK_RESTART, 20.0), "file loaded and seeked to 5 s");
  check(audioTrackSelected(m), "external audio track selected on load");
  check(waitTimePos(m, 10.0, [](double t) { return t >= 5.3; }), "plays forward");

  // J: reverse.
  const double atReverse = timePos(m);
  applySpeed(m, -2.0);
  const bool reversed = waitTimePos(m, 15.0, [atReverse](double t) { return t < atReverse - 0.3; });
  check(reversed, "reverses", QString("from %1 s to %2 s").arg(atReverse).arg(timePos(m)));
  check(prop(m, "aid") == "no" && !audioTrackSelected(m), "no audio track while reversing",
        "aid=" + prop(m, "aid"));

  // L: forward again, mid-file - the track must come back.
  const double atForward = timePos(m);
  applySpeed(m, 1.0);
  const bool forwardAgain = waitTimePos(m, 15.0, [atForward](double t) { return t > atForward + 0.3; });
  check(forwardAgain, "forward again after reverse",
        QString("from %1 s to %2 s").arg(atForward).arg(timePos(m)));
  check(audioTrackSelected(m) && prop(m, "mute") == "no",
        "audio track selected and not muted after reverse", "aid=" + prop(m, "aid"));

  // J again, until the file start ends the file.
  applySpeed(m, -2.0);
  int reason = -1;
  const bool ended = waitEvent(m, MPV_EVENT_END_FILE, 30.0, &reason);
  check(ended && reason == MPV_END_FILE_REASON_EOF,
        "reverse ends the file at its start", QString("reason %1").arg(reason));

  // Non-vacuity: without the reset the reverse state does survive the end of
  // the file and the next loadfile - otherwise the reset proves nothing.
  check(prop(m, "play-dir") == "backward" && prop(m, "aid") == "no",
        "reverse state survives the end of the file",
        "play-dir=" + prop(m, "play-dir") + " aid=" + prop(m, "aid"));
  loadAsync(m, video, audio, 2.0);
  check(waitEvent(m, MPV_EVENT_FILE_LOADED, 20.0), "reload without reset");
  check(prop(m, "play-dir") == "backward" && !audioTrackSelected(m),
        "without the reset the next file plays backwards without audio",
        "play-dir=" + prop(m, "play-dir") + " aid=" + prop(m, "aid"));

  // What TTMpvWrapper::load does: reset, then load.
  applySpeed(m, 1.0);
  loadAsync(m, video, audio, 2.0);
  check(waitEvent(m, MPV_EVENT_PLAYBACK_RESTART, 20.0), "reload with reset");
  const QString detail = QString("play-dir=%1 speed=%2 mute=%3 aid=%4")
      .arg(prop(m, "play-dir"), prop(m, "speed"), prop(m, "mute"), prop(m, "aid"));
  check(prop(m, "play-dir") == "forward" && prop(m, "speed").toDouble() == 1.0 &&
        prop(m, "mute") == "no" && audioTrackSelected(m),
        "after the reset: forward, 1x, not muted, audio track selected", detail);
  check(waitTimePos(m, 10.0, [](double t) { return t >= 2.3; }), "plays forward after the reload");

  mpv_terminate_destroy(m);

  printf(failures == 0 ? "PASS: all checks\n" : "FAIL: %d check(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
