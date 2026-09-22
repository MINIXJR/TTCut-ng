// Gate for the playback channel-layout setting (TTSettings::
// playbackAudioChannels, applied via TTMpvWrapper::channelsOptionFor).
//
// Why the setting exists, measured 2026-09-22: mpv follows the audio stream, so
// an AC3 track that switches its channel mode inside the recording (5.1 <-> 2.0,
// common in DVB) makes it close and reopen the audio output at every switch -
// three openings for two switches, on PipeWire and on ao=null alike. Playback
// feeds mpv the SOURCE audio, uncut and un-normalized, so the switch reaches it
// unchanged. Pinning the layout keeps the output at one opening.
//
// Part 1 checks the pure mapping - which is where the codec rule lives: a track
// that is stereo by definition never makes mpv switch, so pinning it would gain
// nothing and would hand the sink four silent channels for no reason.
//
// Part 2 feeds the result to a real libmpv instance on the synthetic AC3 that
// switches channel mode twice (the fixture gate_acmod_majority builds) and
// counts how often the audio output is initialised. Without the option three,
// with it one. ao=null reproduces the reinit - checked before relying on it.
//
// Usage: test_mpv_channels <mixed.ac3>
#include <QCoreApplication>
#include <QFile>
#include <clocale>
#include <cstdio>
#include <mpv/client.h>

#include "avstream/ttavtypes.h"
#include "common/ttsettings.h"
#include "gui/ttmpvwrapper.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, const QString& detail = QString())
{
  printf("%s: %s%s\n", ok ? "PASS" : "FAIL", what,
         detail.isEmpty() ? "" : qPrintable(QString(" (%1)").arg(detail)));
  if (!ok) failures++;
}

//! Play the file through libmpv and count how often the audio output is
//! initialised. mpv logs one "Trying audio driver" per initialisation at
//! verbose level on the "ao" prefix.
int countAoInits(const QString& file, const QString& channels)
{
  mpv_handle* m = mpv_create();
  if (!m) return -1;

  mpv_set_option_string(m, "vo", "null");
  mpv_set_option_string(m, "ao", "null");
  mpv_set_option_string(m, "video", "no");
  mpv_set_option_string(m, "terminal", "no");
  mpv_set_option_string(m, "idle", "yes");
  // Faster than real time: the fixture is 20 s of audio and the gate must not
  // sit through it. The reinit happens where the stream switches, not on a
  // clock, so the speed does not change what is counted.
  mpv_set_option_string(m, "speed", "100");
  mpv_request_log_messages(m, "v");

  if (mpv_initialize(m) < 0) { mpv_destroy(m); return -1; }

  // The property is set AFTER mpv_initialize on purpose - that is how
  // TTMpvWrapper::setOutputChannels does it, and the point of this check is
  // that it works there.
  if (!channels.isEmpty())
    mpv_set_property_string(m, "audio-channels", channels.toUtf8().constData());

  const QByteArray path = file.toUtf8();
  const char* cmd[] = {"loadfile", path.constData(), nullptr};
  mpv_command(m, cmd);

  int inits = 0;
  bool done = false;
  while (!done) {
    mpv_event* ev = mpv_wait_event(m, 30.0);
    if (ev->event_id == MPV_EVENT_NONE) break;          // timeout
    if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
      auto* msg = static_cast<mpv_event_log_message*>(ev->data);
      if (QByteArray(msg->prefix) == "ao" &&
          QByteArray(msg->text).contains("Trying audio driver"))
        inits++;
    }
    if (ev->event_id == MPV_EVENT_END_FILE) done = true;
    if (ev->event_id == MPV_EVENT_SHUTDOWN) done = true;
  }

  mpv_destroy(m);
  return inits;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses a comma-decimal locale

  if (argc < 2) {
    fprintf(stderr, "usage: %s <mixed.ac3>\n", argv[0]);
    return 2;
  }
  const QString mixed = argv[1];

  // --- Part 1: the mapping ------------------------------------------------
  TTSettings::instance()->setPlaybackAudioChannels(TTSettings::PlaybackChannelsOriginal);
  check(TTMpvWrapper::channelsOptionFor(TTAVTypes::ac3_audio).isEmpty(),
        "original leaves AC3 alone");
  check(TTMpvWrapper::channelsOptionFor(TTAVTypes::mpeg_audio).isEmpty(),
        "original leaves MP2 alone");

  TTSettings::instance()->setPlaybackAudioChannels(TTSettings::PlaybackChannels51);
  check(TTMpvWrapper::channelsOptionFor(TTAVTypes::ac3_audio) == "5.1",
        "5.1 pins an AC3 track");
  check(TTMpvWrapper::channelsOptionFor(TTAVTypes::mpeg_audio).isEmpty(),
        "5.1 does NOT pin a stereo-only codec",
        "MP2 never makes mpv switch layouts");

  // --- Part 2: live libmpv ------------------------------------------------
  if (!QFile::exists(mixed)) {
    check(false, "fixture exists", mixed);
    return failures == 0 ? 0 : 1;
  }

  const int loose = countAoInits(mixed, QString());
  const int pinned = countAoInits(mixed, QStringLiteral("5.1"));
  printf("  audio output initialised: %d without the option, %d with it\n", loose, pinned);

  // Non-vacuity: the unpinned run MUST show the reinit, otherwise the pinned
  // run proves nothing (a fixture without a channel switch would pass both).
  check(loose >= 2, "the fixture reproduces the reinit without the option",
        QString("%1 initialisations").arg(loose));
  check(pinned == 1, "pinning the layout leaves a single initialisation",
        QString("%1 initialisations").arg(pinned));

  printf(failures == 0 ? "PASS: all checks\n" : "FAIL: %d check(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
