// Project file round trip: load a .ttcut through TTAVData, write it back,
// load the written file again and write it a second time. The two written
// files must be byte-identical (load/save is idempotent), and the first one
// is printed with its MD5 so two builds can be compared on it (code-audit
// run 3, batch D: TTCutProjectData's track sections share one writer).
//
//   usage: test_project_roundtrip <project.ttcut> <workdir> [tag]
//
// Offscreen, no dialogs (TTAVData::setNonInteractive). Build via
// `cmake --build build --target test_project_roundtrip`.
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

#include "data/ttavdata.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

// Load one project and write it to outPath; false when the load aborted or
// did not finish within the timeout.
static bool loadAndWrite(const QString& project, const QString& outPath)
{
  TTAVData avData;
  avData.setNonInteractive(true);

  QEventLoop loop;
  bool finished = false, aborted = false;
  QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                   [&](const QString&) { finished = true; loop.quit(); });
  QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                   [&]() { aborted = true; loop.quit(); });
  QTimer::singleShot(120000, &loop, &QEventLoop::quit);

  avData.readProjectFile(QFileInfo(project));
  loop.exec();

  printf("load %s: finished=%d aborted=%d items=%d\n", qPrintable(project),
         finished, aborted, avData.avCount());
  if (!finished || aborted) return false;

  QFile::remove(outPath);
  avData.writeProjectFile(QFileInfo(outPath));
  return QFileInfo::exists(outPath);
}

static QByteArray fileBytes(const QString& path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 3) {
    fprintf(stderr, "usage: %s <project.ttcut> <workdir> [tag]\n", argv[0]);
    return 2;
  }
  const QString project = QString::fromUtf8(argv[1]);
  const QString workDir = QString::fromUtf8(argv[2]);
  const QString tag     = argc > 3 ? QString::fromUtf8(argv[3]) : QStringLiteral("roundtrip");
  QDir().mkpath(workDir);
  const QString outA = QDir(workDir).absoluteFilePath(tag + "-a.ttcut");
  const QString outB = QDir(workDir).absoluteFilePath(tag + "-b.ttcut");

  check(loadAndWrite(project, outA), "first load writes the project");
  check(loadAndWrite(outA, outB),    "second load (of the written file) writes the project");

  const QByteArray a = fileBytes(outA), b = fileBytes(outB);
  check(!a.isEmpty() && a == b, "both written files are byte-identical");
  printf("written: %s  md5 %s  (%lld bytes)\n", qPrintable(outA),
         QCryptographicHash::hash(a, QCryptographicHash::Md5).toHex().constData(),
         (long long)a.size());

  printf("%s\n", failures ? "PROJECT-ROUNDTRIP FAIL" : "PROJECT-ROUNDTRIP PASS");
  return failures ? 1 : 0;
}
