// Gate for contract findings 3+4 of code-audit run 3 (ruled 2026-09-12): a
// project whose video loads but whose audio track cannot be opened must
// still count as loaded, with the failed track reported - not silently
// dropped (the open tasks' aborted() used to reach nobody) and not turned
// into an aborted project load (the pool says aborted() whenever the failed
// task happens to leave the queue last).
//
// The project carries one good audio copy, one file that exists but is not
// audio, and one that does not exist. Expected on every iteration, whichever
// task leaves the pool last: readProjectFileFinished, one video item with
// exactly one audio track, and trackOpenFailed naming the two others.
//
//   usage: test_open_track_failure <video-es> <audio-es> <workdir> [iterations]
//
// Offscreen, non-interactive. Build via
// `cmake --build build --target test_open_track_failure`.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

#include "common/ttthreadtaskpool.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"

static int failures = 0;
static void check(bool ok, const char* what, int iteration)
{
  printf("%s: [%d] %s\n", ok ? "PASS" : "FAIL", iteration, what);
  if (!ok) failures++;
}

static void pump(int ms)
{
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 4) {
    fprintf(stderr, "usage: %s <video-es> <audio-es> <workdir> [iterations]\n", argv[0]);
    return 2;
  }
  const QString videoFile = QFileInfo(QString::fromUtf8(argv[1])).absoluteFilePath();
  const QString audioFile = QFileInfo(QString::fromUtf8(argv[2])).absoluteFilePath();
  const QString workDir   = QString::fromUtf8(argv[3]);
  const int iterations    = argc > 4 ? atoi(argv[4]) : 3;
  QDir().mkpath(workDir);

  const QString ext     = QFileInfo(audioFile).suffix();
  const QString good    = QDir(workDir).absoluteFilePath("otf_good." + ext);
  const QString junk    = QDir(workDir).absoluteFilePath("otf_junk." + ext);
  const QString missing = QDir(workDir).absoluteFilePath("otf_missing." + ext);
  QFile::remove(good); QFile::remove(junk); QFile::remove(missing);
  if (!QFile::copy(audioFile, good)) { fprintf(stderr, "cannot copy %s\n", qPrintable(good)); return 2; }
  {
    QFile j(junk);
    if (!j.open(QIODevice::WriteOnly | QIODevice::Text)) return 2;
    QTextStream(&j) << QString("this is not an audio stream\n").repeated(64);
  }

  const QString project = QDir(workDir).absoluteFilePath("otf.ttcut");
  {
    QFile p(project);
    if (!p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return 2;
    QTextStream(&p)
      << "<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n <Video>\n"
      << "  <Order>0</Order>\n  <Name>" << videoFile << "</Name>\n"
      << "  <Audio><Order>0</Order><Name>" << good    << "</Name></Audio>\n"
      << "  <Audio><Order>1</Order><Name>" << junk    << "</Name></Audio>\n"
      << "  <Audio><Order>2</Order><Name>" << missing << "</Name></Audio>\n"
      << " </Video>\n</TTCut-Projectfile>\n";
  }

  int abortedRouteRuns = 0;
  for (int it = 1; it <= iterations; ++it) {
    TTAVData avData;
    avData.setNonInteractive(true);

    QEventLoop loop;
    bool finished = false, aborted = false, poolAborted = false;
    QStringList reported;
    QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                     [&](const QString&) { finished = true; loop.quit(); });
    QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                     [&]() { aborted = true; loop.quit(); });
    QObject::connect(&avData, &TTAVData::trackOpenFailed, &loop,
                     [&](const QStringList& msgs) { reported = msgs; });
    QObject::connect(avData.threadTaskPool(), &TTThreadTaskPool::aborted, &loop,
                     [&]() { poolAborted = true; });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);

    avData.readProjectFile(QFileInfo(project));
    loop.exec();
    pump(500);   // the report follows on exit(), which may come after finished
    if (poolAborted) abortedRouteRuns++;

    printf("iteration %d: route=%s finished=%d aborted=%d items=%d audio=%d reported=%d\n",
           it, poolAborted ? "pool-aborted" : "pool-exit", finished, aborted, avData.avCount(),
           avData.avCount() > 0 ? avData.avItemAt(0)->audioCount() : -1, int(reported.size()));
    for (const QString& r : reported) printf("    %s\n", qPrintable(r));

    check(finished && !aborted, "project counts as loaded", it);
    check(avData.avCount() == 1, "one video item", it);
    check(avData.avCount() == 1 && avData.avItemAt(0)->audioCount() == 1, "exactly the good track is loaded", it);
    check(reported.size() == 2, "two failed tracks reported", it);
    check(reported.join("\n").contains(junk) && reported.join("\n").contains(missing),
          "the report names the junk file and the missing file", it);
  }
  printf("routes: %d of %d iterations ended with the pool's aborted()\n", abortedRouteRuns, iterations);
  printf("%s\n", failures ? "OPEN-TRACK-FAILURE FAIL" : "OPEN-TRACK-FAILURE PASS");
  return failures ? 1 : 0;
}
