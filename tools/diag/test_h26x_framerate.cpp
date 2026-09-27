// Gate for the frame-rate rule (spec 2026-09-27-frame-rate-source): each
// argument <es-file>=<fps>:<origin>[:warn][:log=<text>] opens the file as
// an H.26x stream and checks frameRate(), frameRateOrigin(), that the open
// logged the ".info disagrees with the SPS timing" warning exactly when
// :warn is given, and with :log= that the open log contains <text>.
//
//   usage: test_h26x_framerate <es-file>=<fps>:<origin>[:warn][:log=<text>] ...
//
// Build via `cmake --build build --target test_h26x_framerate`.
#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryFile>

#include <cmath>
#include <cstdio>
#include <unistd.h>

#include "avstream/tth264videostream.h"
#include "avstream/tth265videostream.h"
#include "common/ttmessagelogger.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

static QString originName(TTFrameRateOrigin o)
{
  switch (o) {
  case TTFrameRateOrigin::Info:         return "Info";
  case TTFrameRateOrigin::StreamTiming: return "StreamTiming";
  case TTFrameRateOrigin::Assumed:      return "Assumed";
  }
  return "?";
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <es-file>=<fps>:<origin>[:warn][:log=<text>] ...\n", argv[0]);
    return 2;
  }
  // Warnings go to stderr in console mode; each open is captured below.
  TTMessageLogger::getInstance()->setLogModeConsole(true);

  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    const int eq = arg.indexOf('=', arg.lastIndexOf('/') + 1);   // file name ends at the first '='
    const QString path = arg.left(eq);
    const QStringList want = arg.mid(eq + 1).split(':');
    const double wantFps = want.value(0).toDouble();
    const QString wantOrigin = want.value(1);
    const bool wantWarn = want.contains("warn");
    QString wantLog;
    for (const QString& w : want)
      if (w.startsWith("log=")) wantLog = w.mid(4);
    const QString name = QFileInfo(path).fileName();

    QFileInfo fi(path);
    TTH26xVideoStream* s = fi.suffix().toLower() == "265"
        ? static_cast<TTH26xVideoStream*>(new TTH265VideoStream(fi))
        : static_cast<TTH26xVideoStream*>(new TTH264VideoStream(fi));

    QTemporaryFile cap;
    cap.open();
    fflush(stderr);
    const int saved = dup(2);
    dup2(cap.handle(), 2);
    const int n = s->createHeaderList();
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    cap.seek(0);
    const QString log = QString::fromUtf8(cap.readAll());

    check(n > 0, name + ": opened");
    check(std::fabs(s->frameRate() - wantFps) < 0.01,
          QString("%1: frameRate %2, want %3").arg(name).arg(s->frameRate()).arg(wantFps));
    check(originName(s->frameRateOrigin()) == wantOrigin,
          QString("%1: origin %2, want %3").arg(name, originName(s->frameRateOrigin()), wantOrigin));
    check(log.contains("disagrees with the SPS timing") == wantWarn,
          name + (wantWarn ? ": mismatch warning logged" : ": no mismatch warning"));
    if (!wantLog.isEmpty())
      check(log.contains(wantLog), QString("%1: log contains \"%2\"").arg(name, wantLog));
    delete s;
  }
  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
