// Gate for finding C4 of code-audit run 5: a project file from which no
// <Video> section can start an open task must end the load instead of
// leaving it open forever.
//
// TTAVData::readProjectFile arms TTThreadTaskPool::exit and ::aborted and
// then parses the document. The pool emits neither signal unless a task was
// started, so a project with no <Video> element, or one whose every path
// TTCutProjectData::resolveProjectPath rejects (".." segments, control
// bytes), used to leave readProjectFileFinished/Aborted unsent: the main
// window's mProjectLoadInProgress stayed set (blocking the automatic
// anomaly scan), waitForProjectLoad ran into its timeout, and the
// TTCutProjectData object was leaked by the next read.
//
// Note the case this does NOT cover: a <Video> whose path is well-formed but
// whose file is missing on disk DOES start a task, which then fails through
// the task abort route - a different path with its own reporting.
//
// The last case uses exactly that to reach the <Audio>/<Subtitle> guards
// without any media: doOpenVideoStream hands back its AVItem before the task
// runs, so the track sections are parsed even though the video file does not
// exist. It pins TTCutProjectData::parseSectionHeader, which all three
// sections share, on both of its refusals and on the section name it logs.
//
// Material-free (the projects are written here), offscreen, no dialogs.
//   usage: test_project_load_rejected <workdir>
// Build via `cmake --build build --target test_project_load_rejected`.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QStringList>
#include <QTimer>
#include <cstdio>

#include "data/ttavdata.h"

namespace {

int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

QMutex      logMutex;
QStringList logMessages;

void collectMessages(QtMsgType, const QMessageLogContext&, const QString& msg)
{
  QMutexLocker lock(&logMutex);
  logMessages << msg;
}

// True when some collected message contains 'needle'.
bool logged(const char* needle)
{
  QMutexLocker lock(&logMutex);
  for (const QString& m : std::as_const(logMessages))
    if (m.contains(QLatin1String(needle))) return true;
  return false;
}

bool writeFile(const QString& path, const QByteArray& content)
{
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(content);
  return true;
}

// Loads one project and reports which terminal signal arrived within
// timeoutMs. Returns 'f' (finished), 'a' (aborted) or '-' (neither).
char loadOutcome(const QString& project, int timeoutMs)
{
  TTAVData avData;
  avData.setNonInteractive(true);
  QEventLoop loop;
  char outcome = '-';
  QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                   [&](const QString&) { outcome = 'f'; loop.quit(); });
  QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                   [&]() { outcome = 'a'; loop.quit(); });
  QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);

  // The signal can be emitted from inside readProjectFile itself (the
  // synchronous end), i.e. before exec() would run - so only wait when it
  // has not already arrived.
  avData.readProjectFile(QFileInfo(project));
  if (outcome == '-') loop.exec();
  return outcome;
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <workdir>\n", argv[0]);
    return 2;
  }
  const QDir work(QString::fromUtf8(argv[1]));
  QDir().mkpath(work.absolutePath());

  const QString noVideo  = work.absoluteFilePath("no-video.ttcut");
  const QString rejected = work.absoluteFilePath("rejected-path.ttcut");
  const QString broken   = work.absoluteFilePath("not-xml.ttcut");
  const QString tracks   = work.absoluteFilePath("rejected-tracks.ttcut");

  if (!writeFile(noVideo,
          "<TTCut-Projectfile><Version>1.0</Version></TTCut-Projectfile>\n") ||
      !writeFile(rejected,
          "<TTCut-Projectfile><Version>1.0</Version>"
          "<Video><Order>0</Order><Name>../outside.m2v</Name></Video>"
          "</TTCut-Projectfile>\n") ||
      !writeFile(tracks,
          "<TTCut-Projectfile><Version>1.0</Version>"
          "<Video><Order>0</Order><Name>/nonexistent-ttcut-gate/video.m2v</Name>"
          "<Audio><Order>0</Order><Name>../outside.mp2</Name></Audio>"
          "<Audio><Order>1</Order></Audio>"
          "<Subtitle><Order>0</Order><Name>../outside.srt</Name></Subtitle>"
          "</Video></TTCut-Projectfile>\n") ||
      !writeFile(broken, "this is not xml\n")) {
    fprintf(stderr, "cannot write the test projects into %s\n", qPrintable(work.absolutePath()));
    return 2;
  }

  check(loadOutcome(noVideo, 5000) == 'a',  "a project without a <Video> section ends as aborted");
  check(loadOutcome(rejected, 5000) == 'a', "a project whose only video path is rejected ends as aborted");
  check(loadOutcome(broken, 5000) == 'a',   "an unparsable project ends as aborted");

  // The shared section guard: both refusals, named per section. qDebug has to
  // be enabled explicitly - a gate run may carry QT_LOGGING_RULES.
  QLoggingCategory::setFilterRules(QStringLiteral("default.debug=true"));
  QtMessageHandler previous = qInstallMessageHandler(collectMessages);
  loadOutcome(tracks, 5000);   // outcome is the task-abort route, not this test
  qInstallMessageHandler(previous);
  check(logged("parseAudioSection -> rejected unsafe path: ../outside.mp2"),
        "a rejected <Audio> path is refused and logged as parseAudioSection");
  check(logged("parseAudioSection -> insufficient nodes"),
        "an <Audio> section without a Name is refused as insufficient nodes");
  check(logged("parseSubtitleSection -> rejected unsafe path: ../outside.srt"),
        "a rejected <Subtitle> path is refused and logged as parseSubtitleSection");

  printf("%s\n", failures ? "PROJECT-LOAD-REJECTED FAIL" : "PROJECT-LOAD-REJECTED PASS");
  return failures ? 1 : 0;
}
