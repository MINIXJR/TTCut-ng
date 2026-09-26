// Gate for the audio ES input (docs/code-map/audio-es-input.md, audit run 11).
// Builds its material with ffmpeg in <work-dir> and checks:
//   H1  frame_time is the frame's duration (samples / sample rate), not its
//       byte length: at 44.1 kHz a 10-segment cut keeps the video's length
//       within one audio frame (it lost ~20 ms per segment before);
//   H5  MPEG-2 (low sampling rate) Layer II opens and parses at its real
//       frame length;
//   H3  one broken MPEG header does not end the header list;
//   H2  the automatic audio search offers only what the parser can read;
//   H4  audio-change markers land on the display frame of the change,
//       extra frames and a 44.1 kHz AC3 track included.
//
// Usage: test_audio_es_input <work-dir>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <cmath>
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
}

#include "avstream/ttavtypes.h"
#include "avstream/ttavstream.h"
#include "avstream/ttaudioheaderlist.h"
#include "common/ttexception.h"
#include "data/ttavdata.h"
#include "data/ttstreampoint.h"
#include "data/ttstreampoint_audioworker.h"
#include "avstream/ttac3audioheader.h"
#include "extern/ttaudiocutter.h"

static int gFail = 0;
static void check(bool ok, const QString& what)
{
  printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++gFail;
}

static bool ffmpeg(const QStringList& args)
{
  QProcess p;
  p.start("ffmpeg", QStringList{"-v", "error", "-y"} + args);
  return p.waitForFinished(300000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static bool makeSine(const QString& out, int seconds, const QStringList& codec)
{
  return ffmpeg(QStringList{"-f", "lavfi", "-i", QString("sine=frequency=440:duration=%1").arg(seconds)}
                + codec + QStringList{out});
}

// Opens like TTOpenAudioTask; nullptr when the type is not readable.
static TTAudioStream* openAudio(const QString& path)
{
  try {
    TTAudioType type(path);
    if (type.avStreamType() != TTAVTypes::mpeg_audio && type.avStreamType() != TTAVTypes::ac3_audio)
      return nullptr;
    TTAudioStream* s = type.createAudioStream();
    s->createHeaderList();
    return s;
  } catch (const TTException&) {
    return nullptr;
  }
}

// Duration of an audio ES by libav: packets x frame size / sample rate.
static double libavDuration(const QString& path)
{
  AVFormatContext* ctx = nullptr;
  if (avformat_open_input(&ctx, path.toUtf8().constData(), nullptr, nullptr) < 0) return -1;
  double sec = -1;
  if (avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->nb_streams > 0) {
    const AVCodecParameters* par = ctx->streams[0]->codecpar;
    AVPacket* pkt = av_packet_alloc();
    long long packets = 0;
    while (av_read_frame(ctx, pkt) >= 0) { ++packets; av_packet_unref(pkt); }
    av_packet_free(&pkt);
    if (par->frame_size > 0 && par->sample_rate > 0)
      sec = double(packets) * par->frame_size / par->sample_rate;
  }
  avformat_close_input(&ctx);
  return sec;
}

// H1: ten kept segments of about 5 min, spread over 1 h like a recording
// with adverts; the cut audio must match the kept video length.
static void checkCutLength(const QString& dir, int rate)
{
  const QString in  = dir + QString("/long_%1.mp2").arg(rate);
  const QString out = dir + QString("/cut_%1.mp2").arg(rate);
  if (!makeSine(in, 3600, {"-ar", QString::number(rate), "-c:a", "mp2", "-b:a", "192k", "-f", "mp2"})) {
    check(false, QString("H1 %1 Hz: ffmpeg could not build the material").arg(rate));
    return;
  }
  TTAudioStream* s = openAudio(in);
  if (!s) { check(false, QString("H1 %1 Hz: stream does not open").arg(rate)); return; }

  const double exactFrameMs = 1152.0 * 1000.0 / rate;
  check(std::fabs(s->headerAt(0)->frame_time - exactFrameMs) < 1e-3,
        QString("H1 %1 Hz: header 0 frame_time %2 ms = 1152 / rate (%3 ms)")
            .arg(rate).arg(s->headerAt(0)->frame_time, 0, 'f', 4).arg(exactFrameMs, 0, 'f', 4));

  QList<QPair<double, double>> video;
  double videoSec = 0;
  for (int i = 0; i < 10; ++i) {
    const double start = i * 360.0 + 17.001, end = start + 300.0 + i * 1.003;
    video.append({start, end});
    videoSec += end - start;
  }
  const TTAVData::AudioCutPlan plan = TTAVData::planAudioCut(s, video, 0);
  TTAudioCutter cutter;
  const bool ok = cutter.cut(in, out, plan.keepList);
  const double cutSec = ok ? libavDuration(out) : -1;
  const double errMs = (cutSec - videoSec) * 1000.0;
  check(ok && std::fabs(errMs) <= exactFrameMs,
        QString("H1 %1 Hz: 10-segment cut keeps %2 s for %3 s of video (%4 ms, limit one frame %5 ms)")
            .arg(rate).arg(cutSec, 0, 'f', 3).arg(videoSec, 0, 'f', 3).arg(errMs, 0, 'f', 1)
            .arg(exactFrameMs, 0, 'f', 1));
  const double plannedMs = plan.drifts.isEmpty() ? 0.0 : plan.drifts.last();
  check(std::fabs(plannedMs - errMs) <= exactFrameMs,
        QString("H1 %1 Hz: planned drift %2 ms matches the cut (%3 ms)")
            .arg(rate).arg(plannedMs, 0, 'f', 1).arg(errMs, 0, 'f', 1));
  delete s;
}

// AC3 at 44.1 kHz alternates 69/70-word frames; its duration is 1536 samples.
static void checkAc3FrameTime(const QString& dir)
{
  const QString in = dir + "/ac3_44k.ac3";
  if (!makeSine(in, 60, {"-ar", "44100", "-ac", "2", "-c:a", "ac3", "-b:a", "192k", "-f", "ac3"})) {
    check(false, "H1 AC3 44.1 kHz: ffmpeg could not build the material");
    return;
  }
  TTAudioStream* s = openAudio(in);
  if (!s) { check(false, "H1 AC3 44.1 kHz: stream does not open"); return; }
  const double exactMs = 1536.0 * 1000.0 / 44100.0;
  bool allExact = true;
  for (int i = 0; i < s->headerList()->count(); ++i)
    if (std::fabs(s->headerAt(i)->frame_time - exactMs) > 1e-3) { allExact = false; break; }
  check(allExact, QString("H1 AC3 44.1 kHz: every frame_time = 1536 / rate (%1 ms)").arg(exactMs, 0, 'f', 4));
  delete s;
}

// H5: MPEG-2 LSF Layer II (1152 samples) and Layer III (576 samples).
static void checkLowRateMpeg(const QString& dir)
{
  struct Case { const char* name; QStringList codec; int samples; int bytes; };
  const Case cases[] = {
    {"mp2 24 kHz 96k", {"-ar", "24000", "-c:a", "mp2", "-b:a", "96k", "-f", "mp2"}, 1152, 576},
    {"mp3 24 kHz 64k", {"-ar", "24000", "-c:a", "libmp3lame", "-b:a", "64k", "-f", "mp3"}, 576, 192},
  };
  for (const Case& c : cases) {
    const QString in = dir + "/lsf_" + QString(c.name).section(' ', 0, 0) + "." + QString(c.name).section(' ', 0, 0);
    if (!makeSine(in, 60, c.codec)) { check(false, QString("H5 %1: ffmpeg could not build the material").arg(c.name)); continue; }
    TTAudioStream* s = openAudio(in);
    if (!s) { check(false, QString("H5 %1: stream does not open").arg(c.name)); continue; }
    const int n = s->headerList()->count();
    const double lengthSec = (s->headerAt(n - 1)->abs_frame_time + s->headerAt(n - 1)->frame_time) / 1000.0;
    check(s->headerAt(n / 2)->frame_length == c.bytes,
          QString("H5 %1: frame_length %2 bytes (expected %3)").arg(c.name).arg(s->headerAt(n / 2)->frame_length).arg(c.bytes));
    check(std::fabs(lengthSec - 60.0) < 0.2,
          QString("H5 %1: header list covers %2 s of 60 s").arg(c.name).arg(lengthSec, 0, 'f', 3));
    delete s;
  }
}

// H3: a header with an invalid bit rate index in the middle of the file.
static void checkBrokenHeader(const QString& dir)
{
  const QString in = dir + "/broken.mp2";
  if (!makeSine(in, 60, {"-ar", "48000", "-c:a", "mp2", "-b:a", "192k", "-f", "mp2"})) {
    check(false, "H3: ffmpeg could not build the material");
    return;
  }
  QFile f(in);
  f.open(QIODevice::ReadWrite);
  QByteArray d = f.readAll();
  const int off = 1000 * 576;                          // frame 1000 of 2500
  d[off + 2] = char((uchar(d[off + 2]) & 0x0F) | 0xF0); // bit rate index 15 (invalid)
  f.seek(0); f.write(d); f.close();

  TTAudioStream* s = openAudio(in);
  if (!s) { check(false, "H3: stream does not open"); return; }
  const int n = s->headerList()->count();
  check(n >= 2498, QString("H3: header list has %1 of 2500 frames after a broken header at frame 1000").arg(n));
  delete s;
}

// H2: the automatic search next to a video.
static void checkAudioSearch(const QString& dir)
{
  const QString sub = dir + "/search";
  QDir().mkpath(sub);
  for (const char* name : {"rec.264", "rec.aac", "rec.eac3", "rec.m4a", "rec.dts",
                           "rec.mp2", "rec_2.mpa", "rec.mp3", "rec_deu.ac3"}) {
    QFile f(sub + "/" + name);
    f.open(QIODevice::WriteOnly);
    f.close();
  }
  QStringList found;
  for (const QFileInfo& fi : TTAVData::getAudioNames(QFileInfo(sub + "/rec.264")))
    found << fi.fileName();
  found.sort();
  const QStringList expected{"rec.mp2", "rec.mp3", "rec_2.mpa", "rec_deu.ac3"};
  check(found == expected, QString("H2: automatic search finds [%1], expected [%2]")
                               .arg(found.join(", "), expected.join(", ")));
}

// H4: stereo then 5.1 AC3; the marker must sit at the display frame of the
// first 5.1 frame: its index x 1536 / rate at 25 fps, plus the three extra
// frames {100, 200, 300} that lie before it.
static void checkAudioChangeMarker(const QString& dir, int rate)
{
  const QString st = dir + QString("/st_%1.ac3").arg(rate), ch = dir + QString("/51_%1.ac3").arg(rate);
  const QString in = dir + QString("/change_%1.ac3").arg(rate);
  if (!makeSine(st, 600, {"-ar", QString::number(rate), "-ac", "2", "-c:a", "ac3", "-b:a", "192k", "-f", "ac3"}) ||
      !makeSine(ch, 30, {"-ar", QString::number(rate), "-ac", "6", "-c:a", "ac3", "-b:a", "384k", "-f", "ac3"})) {
    check(false, QString("H4 %1 Hz: ffmpeg could not build the material").arg(rate));
    return;
  }
  QFile out(in), a(st), b(ch);
  out.open(QIODevice::WriteOnly); a.open(QIODevice::ReadOnly); b.open(QIODevice::ReadOnly);
  out.write(a.readAll()); out.write(b.readAll()); out.close();

  TTAudioStream* s = openAudio(in);
  if (!s) { check(false, QString("H4 %1 Hz: stream does not open").arg(rate)); return; }
  int changeIdx = -1;
  for (int i = 1; i < s->headerList()->count() && changeIdx < 0; ++i)
    if (static_cast<TTAC3AudioHeader*>(s->headerAt(i))->acmod != static_cast<TTAC3AudioHeader*>(s->headerAt(i - 1))->acmod)
      changeIdx = i;
  const int expected = qRound(changeIdx * 1536.0 / rate * 25.0) + 3;

  TTStreamPointAudioWorker worker(in, 25.0f, false, -50, 1.0f, true, s->headerList(), {100, 200, 300});
  QList<TTStreamPoint> points;
  QObject::connect(&worker, &TTStreamPointAudioWorker::pointsDetected,
                   [&points](const QList<TTStreamPoint>& p) { points = p; });
  worker.runSynchron();
  const int got = points.size() == 1 ? points.first().frameIndex() : -1;
  check(got == expected, QString("H4 %1 Hz: audio-change marker at frame %2 (%3 markers), expected %4 "
                                 "(change at AC3 frame %5, 3 extra frames before it)")
                             .arg(rate).arg(got).arg(points.size()).arg(expected).arg(changeIdx));
  delete s;
}

// H4, silence side: 20 s tone, 3 s silence, 5 s tone; the silence marker
// must sit at display frame 20 s x 25 + the three extra frames before it.
static void checkSilenceMarker(const QString& dir)
{
  const QString in = dir + "/silence.mp2";
  if (!ffmpeg({"-f", "lavfi", "-i", "aevalsrc=if(between(t\\,20\\,23)\\,0\\,0.5*sin(2*PI*440*t)):d=28:s=48000",
               "-c:a", "mp2", "-b:a", "192k", "-f", "mp2", in})) {
    check(false, "H4 silence: ffmpeg could not build the material");
    return;
  }
  TTStreamPointAudioWorker worker(in, 25.0f, true, -50, 1.0f, false, nullptr, {100, 200, 300});
  QList<TTStreamPoint> points;
  QObject::connect(&worker, &TTStreamPointAudioWorker::pointsDetected,
                   [&points](const QList<TTStreamPoint>& p) { points = p; });
  worker.runSynchron();
  const int got = points.isEmpty() ? -1 : points.first().frameIndex();
  check(points.size() == 1 && std::abs(got - 503) <= 1,
        QString("H4 silence: marker at frame %1 (%2 markers), expected 503 +-1 (500 + 3 extra frames)")
            .arg(got).arg(points.size()));
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <work-dir>\n", argv[0]); return 2; }
  const QString dir = argv[1];
  QDir().mkpath(dir);

  checkCutLength(dir, 48000);
  checkCutLength(dir, 44100);
  checkAc3FrameTime(dir);
  checkLowRateMpeg(dir);
  checkBrokenHeader(dir);
  checkAudioSearch(dir);
  checkAudioChangeMarker(dir, 48000);
  checkAudioChangeMarker(dir, 44100);
  checkSilenceMarker(dir);

  printf("%s (%d failed)\n", gFail ? "AUDIO_ES_INPUT FAIL" : "AUDIO_ES_INPUT PASS", gFail);
  return gFail ? 1 : 0;
}
