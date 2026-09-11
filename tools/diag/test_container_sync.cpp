// Gate for the transient output container after TTSettings::load().
//
// The cut pipeline reads TTSettings::workingOutputContainer(). It is meant to
// follow the codec-specific muxer default (Mpeg2Muxer / H264Muxer /
// H265Muxer) of the stream being cut: the main window and the headless
// --auto-cut path call setEncoderCodec(<stream codec>) for that. Measured
// 2026-08-16 in gate_audiofix.sh: a fresh configuration with only
// Encoder\Mpeg2Muxer=0 still cut MPEG-2 to MKV, because setEncoderCodec(0)
// returns early when the codec is already 0 (the compiled default) and
// load() had initialised the working container from the legacy global key
// Muxer\OutputContainer (default 1 = MKV) instead of the codec default.
//
// Each case runs in its own process (TTSettings is a singleton, and load()
// falls back to the CURRENT field value for every absent key - a second
// load() in the same process would inherit the previous case's values):
// an empty XDG_CONFIG_HOME, one TTCut-ng.conf, load(),
// setEncoderCodec(<stream codec>) like runAutoCutMode(), check the working
// container. Without an argument the harness spawns itself once per case.
//
//   usage: test_container_sync [caseIndex]
//
// Build: cmake --build build --target test_container_sync
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>

#include "common/ttsettings.h"

static int failures = 0;
static void expect(const char* name, int got, int want)
{
  if (got == want) printf("PASS  %s (container=%d)\n", name, got);
  else { failures++; printf("FAIL  %s (got=%d want=%d)\n", name, got, want); }
}

// Writes the ini, loads it into the singleton and applies the stream codec.
static int run(const QString& cfgDir, const QString& iniBody, int streamCodec)
{
  QDir().mkpath(cfgDir + "/TTCut-ng");
  QFile f(cfgDir + "/TTCut-ng/TTCut-ng.conf");
  f.open(QIODevice::WriteOnly | QIODevice::Truncate);
  QTextStream(&f) << "[Settings]\n" << iniBody;
  f.close();
  TTSettings* s = TTSettings::instance();
  s->load();
  s->setEncoderCodec(streamCodec);
  return s->workingOutputContainer();
}

struct Case { const char* name; const char* ini; int streamCodec; int want; };
static const Case kCases[] = {
  // The measured case: MPEG-2 stream, only the MPEG-2 muxer default set.
  { "mpeg2 stream, Mpeg2Muxer=0 only",                       "Encoder\\Mpeg2Muxer=0\n",                             0, 0 },
  // Same shape for a persisted codec that matches the stream.
  { "h264 stream, EncoderCodec=1 + H264Muxer=0",             "Encoder\\EncoderCodec=1\nEncoder\\H264Muxer=0\n",    1, 0 },
  // Control: the codec CHANGES, so the setter's resync runs.
  { "mpeg2 stream, EncoderCodec=1 persisted, Mpeg2Muxer=0",  "Encoder\\EncoderCodec=1\nEncoder\\Mpeg2Muxer=0\n",   0, 0 },
  // Control: defaults only - MPEG-2 default muxer is mplex (0).
  { "mpeg2 stream, empty config",                            "",                                                     0, 0 },
  // Control: H.264 default muxer is MKV (1).
  { "h264 stream, empty config",                             "",                                                     1, 1 },
  // The legacy global key alone must not override the codec default.
  { "mpeg2 stream, Muxer\\OutputContainer=1 only",            "Muxer\\OutputContainer=1\n",                          0, 0 },
};
static const int kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

int main(int argc, char** argv)
{
  if (argc > 1) {
    const int i = QString(argv[1]).toInt();
    if (i < 0 || i >= kCaseCount) return 2;
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());
    QCoreApplication app(argc, argv);
    const Case& c = kCases[i];
    expect(c.name, run(tmp.path(), QString::fromLatin1(c.ini), c.streamCodec), c.want);
    return failures ? 1 : 0;
  }

  QCoreApplication app(argc, argv);
  for (int i = 0; i < kCaseCount; ++i) {
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start(QCoreApplication::applicationFilePath(), { QString::number(i) });
    p.waitForFinished(30000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) failures++;
  }
  printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
