// Gate for the frame-rate hint (spec 2026-09-27-frame-rate-source): a
// project whose video has neither .info nor SPS timing loads, and TTAVData
// reports the file through frameRateAssumed (the GUI shows it in a
// warning; headless only the log). A video with SPS timing reports nothing.
//
//   usage: test_framerate_hint <no-timing-es> <timed-es> <workdir>
//
// Offscreen, non-interactive. Build via
// `cmake --build build --target test_framerate_hint`.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "data/ttavdata.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

static void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Copies the video into workDir (no .info next to it), writes a one-video
// project and loads it; returns what frameRateAssumed reported.
static QStringList loadAndReport(const QString& video, const QString& workDir,
                                 const QString& tag, bool* finished)
{
  const QString copy = QDir(workDir).absoluteFilePath(tag + "." + QFileInfo(video).suffix());
  QFile::remove(copy);
  QFile::copy(video, copy);
  const QString project = QDir(workDir).absoluteFilePath(tag + ".ttcut");
  {
    QFile p(project);
    p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    QTextStream(&p)
      << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n"
      << "  <Order>0</Order>\n  <Name>" << copy << "</Name>\n"
      << " </Video>\n</TTCut-Projectfile>\n";
  }

  TTAVData avData;
  avData.setNonInteractive(true);
  QEventLoop loop;
  QStringList reported;
  *finished = false;
  QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                   [&](const QString&) { *finished = true; loop.quit(); });
  QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop, [&]() { loop.quit(); });
  QObject::connect(&avData, &TTAVData::frameRateAssumed, &loop,
                   [&](const QStringList& files) { reported = files; });
  QTimer::singleShot(60000, &loop, &QEventLoop::quit);
  avData.readProjectFile(QFileInfo(project));
  loop.exec();
  pump(500);   // the report follows on the pool's exit()
  return reported;
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 4) {
    fprintf(stderr, "usage: %s <no-timing-es> <timed-es> <workdir>\n", argv[0]);
    return 2;
  }
  const QString noTiming = QFileInfo(QString::fromUtf8(argv[1])).absoluteFilePath();
  const QString timed    = QFileInfo(QString::fromUtf8(argv[2])).absoluteFilePath();
  const QString workDir  = QString::fromUtf8(argv[3]);
  QDir().mkpath(workDir);

  bool finished = false;
  QStringList r = loadAndReport(noTiming, workDir, "fr_notiming", &finished);
  check(finished, "project without SPS timing loads");
  check(r.size() == 1 && r.first().endsWith("fr_notiming.264"),
        QString("frameRateAssumed names the video (%1)").arg(r.join(", ")));

  r = loadAndReport(timed, workDir, "fr_timed", &finished);
  check(finished, "project with SPS timing loads");
  check(r.isEmpty(), QString("no hint for a video with SPS timing (%1)").arg(r.join(", ")));

  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
