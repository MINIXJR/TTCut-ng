// Gate for the logger (audit run 15, map logging.md). One mode per run,
// because the logger rotates once per process:
//
//   test_logging levels <log>   a located exception logs as [error], a
//                               FATAL line carries [fatal], a libav ERROR is
//                               logged with "libav loggen" off and a libav
//                               WARNING is not (H3, H4);
//   test_logging fileoff        with the settings' "create log file" off
//                               (XDG_CONFIG_HOME prepared by the caller),
//                               loading the settings is enough to keep the
//                               logger from rotating or writing (H2);
//   test_logging mpv <log>      a libav ERROR still reaches the log after a
//                               libmpv context was started (H3);
//   test_logging rotate <log>   write one line; the caller runs it twice and
//                               checks the rotated generations (H5).
//
// Build via `cmake --build build --target test_logging`.
#include <QApplication>
#include <QFile>

#include <clocale>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/log.h>
}

#include "common/ttavlog.h"
#include "common/ttexception.h"
#include "common/ttmessagelogger.h"
#include "common/ttsettings.h"
#include "gui/ttmpvlibbackend.h"

static int failures = 0;
static void check(bool ok, const QString& what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
  if (!ok) ++failures;
}

// The log line carrying marker, or an empty string.
static QString lineWith(const QString& log, const QString& marker)
{
  QFile f(log);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  for (const QString& l : QString::fromUtf8(f.readAll()).split('\n'))
    if (l.contains(marker)) return l;
  return {};
}

static int finish()
{
  printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");   // libmpv refuses to start otherwise, as in main()
  const char* mode = argc > 1 ? argv[1] : "";
  const QString log = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
  TTMessageLogger* logger = TTMessageLogger::getInstance();

  // The settings load pushes its log path into the logger; set ours after it.
  (void)TTSettings::instance();

  if (!std::strcmp(mode, "levels") && !log.isEmpty()) {
    logger->setLogFilePath(log);
    TTIOException located(__FILE__, __LINE__, "MARK-LOCATED-EXCEPTION");
    logger->fatalMsg(__FILE__, __LINE__, "MARK-FATAL");
    ttInstallAvLogCallback();
    av_log(nullptr, AV_LOG_ERROR, "MARK-AV-ERROR\n");
    av_log(nullptr, AV_LOG_WARNING, "MARK-AV-WARNING\n");

    const QString exc = lineWith(log, "MARK-LOCATED-EXCEPTION");
    check(exc.startsWith("[error]"), "H4: a located exception logs as [error] - got: " + exc);
    const QString fatal = lineWith(log, "MARK-FATAL");
    check(fatal.startsWith("[fatal]"), "H4: a FATAL line carries [fatal] - got: " + fatal);
    const QString averr = lineWith(log, "MARK-AV-ERROR");
    check(averr.startsWith("[error]") && averr.contains("[libav]"),
          "H3: a libav ERROR is logged with libav logging off - got: " + averr);
    check(lineWith(log, "MARK-AV-WARNING").isEmpty(),
          "H3: a libav WARNING stays out with libav logging off");
    return finish();
  }

  if (!std::strcmp(mode, "fileoff")) {
    check(!TTSettings::instance()->createLogFile(),
          "precondition: the prepared settings have \"create log file\" off");
    logger->infoMsg(__FILE__, __LINE__, "MARK-FILEOFF-INFO");
    logger->warningMsg(__FILE__, __LINE__, "MARK-FILEOFF-WARNING");
    printf("log path: %s\n", qPrintable(logger->logFilePath()));
    return finish();   // the caller checks that the log was left alone
  }

  if (!std::strcmp(mode, "mpv") && !log.isEmpty()) {
    logger->setLogFilePath(log);
    TTMpvLibBackend backend;
    check(backend.start(), "precondition: the libmpv context started");
    av_log(nullptr, AV_LOG_ERROR, "MARK-AV-ERROR-MPV\n");
    const QString averr = lineWith(log, "MARK-AV-ERROR-MPV");
    check(averr.startsWith("[error]") && averr.contains("[libav]"),
          "H3: a libav ERROR reaches the log while libmpv runs - got: " + averr);
    return finish();
  }

  if (!std::strcmp(mode, "rotate") && !log.isEmpty()) {
    logger->setLogFilePath(log);
    logger->infoMsg(__FILE__, __LINE__, "MARK-ROTATE-RUN");
    return finish();
  }

  fprintf(stderr, "usage: %s levels|mpv|rotate <log> | fileoff\n", argv[0]);
  return 2;
}
