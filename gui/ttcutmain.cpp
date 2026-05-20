/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Qt headers
#include <QApplication>
#include <QMessageBox>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QtGlobal>

// class declaration for the main window class
#include "ttcutmainwindow.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"

#include <QCommandLineParser>
#include <QTimer>

#include <cstdarg>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavutil/log.h>
}

// ---------------------------------------------------------------------------
// Qt message handler: route qDebug/qInfo/qWarning/qCritical/qFatal through
// TTMessageLogger so the configured ~/.cache/ttcut-ng/logfile.log captures
// the same messages a user would otherwise only see on the console.
// ---------------------------------------------------------------------------
static void ttQtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
  TTMessageLogger* log = TTMessageLogger::getInstance();
  const char* file = context.file ? context.file : "qt";
  int line = context.line;
  switch (type) {
    case QtDebugMsg:    log->debugMsg(file, line, msg);   break;
    case QtInfoMsg:     log->infoMsg(file, line, msg);    break;
    case QtWarningMsg:  log->warningMsg(file, line, msg); break;
    case QtCriticalMsg: log->errorMsg(file, line, msg);   break;
    case QtFatalMsg:    log->errorMsg(file, line, msg);
                        std::abort();
  }
}

// ---------------------------------------------------------------------------
// libav log callback: gated on TTSettings::logLibav() (default off, since
// libav is very chatty). When enabled, maps AV_LOG_ levels onto matching
// TTMessageLogger severities and strips trailing newlines that libav emits.
// ---------------------------------------------------------------------------
static void ttAvLogCallback(void* avcl, int level, const char* fmt, va_list vl)
{
  if (!TTSettings::instance()->logLibav()) return;
  if (level > av_log_get_level()) return;
  char buf[1024];
  int prefix = 0;
  av_log_format_line(avcl, level, fmt, vl, buf, sizeof(buf), &prefix);
  size_t n = std::strlen(buf);
  while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
  if (!n) return;
  TTMessageLogger* log = TTMessageLogger::getInstance();
  // libav emits UTF-8; fromLocal8Bit would mangle non-ASCII codec/file
  // names on non-UTF-8 locales.
  QString qmsg = QString::fromUtf8(buf, static_cast<int>(n));
  if      (level <= AV_LOG_ERROR)   log->errorMsg("libav", 0, qmsg);
  else if (level <= AV_LOG_WARNING) log->warningMsg("libav", 0, qmsg);
  else if (level <= AV_LOG_INFO)    log->infoMsg("libav", 0, qmsg);
  else                              log->debugMsg("libav", 0, qmsg);
}

/* /////////////////////////////////////////////////////////////////////////////
 * TTCut main
 */
int main( int argc, char **argv )
{
  try
  {
    QT_REQUIRE_VERSION(argc, argv, "5.0.0");

    // Install Qt message handler + libav log callback BEFORE QApplication
    // is constructed so the very first qDebug/qWarning from Qt-internal
    // bootstrap and any libav probe-output get routed through
    // TTMessageLogger. TTMessageLogger::getInstance() is lazy and the
    // handlers degrade gracefully if invoked pre-singleton-init.
    qInstallMessageHandler(ttQtMessageHandler);
    av_log_set_callback(ttAvLogCallback);

    QApplication a( argc, argv );

    a.setApplicationName("TTCut-ng");

    // Force the lazy TTSettings singleton to construct and run its first
    // load() before any UI code reads a persisted value.
    (void)TTSettings::instance();

    // Centre QGroupBox titles application-wide. Most styles (including
    // current Breeze) default to left-aligned titles; we want the visual
    // consistency of centred titles across all panels regardless of the
    // user's KDE theme. Per-widget alignment properties in .ui files
    // would still override this.
    a.setStyleSheet(a.styleSheet() +
                    "\nQGroupBox::title { subcontrol-position: top center; }");

    // Load qtbase translations (since Qt 5.x the per-module split replaced
    // the legacy qt_<locale>.qm bundle; in modern installs that legacy file
    // is an empty stub. qtbase_<locale>.qm carries QMessageBox button labels
    // like Yes/No/OK/Cancel). Forward-compatible with Qt 6.
    QTranslator qtTranslator;
    if (!qtTranslator.load("qtbase_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
      TTMessageLogger* log = TTMessageLogger::getInstance();
      log->warningMsg(__FILE__, __LINE__,
                    QString("Qt translation file %1 for locale %2 could not be found!").
                    arg("qtbase_" + QLocale::system().name()).
                    arg(QLocale::system().name()));
    }

    a.installTranslator(&qtTranslator);

    QTranslator appTranslator;
    QString transFile = "ttcut-ng_" + QLocale::system().name();
    // Try local trans directory first, then installed location
    if (!appTranslator.load(transFile, "trans") &&
        !appTranslator.load(transFile, "/usr/share/ttcut-ng/trans")) {
      TTMessageLogger* log = TTMessageLogger::getInstance();
      log->warningMsg(__FILE__, __LINE__,
                    QString("Translation file %1 for locale %2 could not be found!").
                    arg(transFile).
                    arg(QLocale::system().name()));
    }

    a.installTranslator(&appTranslator);

    // Application main widget
    TTCutMainWindow* mainWnd = new TTCutMainWindow();

    // Caption text in applications title bar
    mainWnd->setWindowTitle( TTCut::versionString );
    mainWnd->show();

    // set initial size of applications main window
    mainWnd->resize(1024, 768);

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription("TTCut-ng - Frame-accurate video cutter");
    parser.addHelpOption();

    QCommandLineOption screenshotOpt("screenshots",
        "Capture all widget screenshots to <dir> and exit.", "dir");
    QCommandLineOption projectOpt("project",
        "Load project file <file>.", "file");
    QCommandLineOption autoCutOpt("auto-cut",
        "Load --project, perform A/V cut, write MKV to <out>, and exit. "
        "For headless QC regression.", "out");
    parser.addOption(screenshotOpt);
    parser.addOption(projectOpt);
    parser.addOption(autoCutOpt);
    parser.addPositionalArgument("file", "Video or project file to open.");
    parser.process(a);

    // Screenshot mode
    if (parser.isSet(screenshotOpt)) {
      TTSettings::instance()->setScreenshotDir(parser.value(screenshotOpt));
      TTSettings::instance()->setScreenshotProject(parser.value(projectOpt));
      QTimer::singleShot(500, mainWnd, &TTCutMainWindow::runScreenshotMode);
    } else if (parser.isSet(autoCutOpt) && parser.isSet(projectOpt)) {
      const QString prj = parser.value(projectOpt);
      const QString out = parser.value(autoCutOpt);
      QTimer::singleShot(500, mainWnd, [mainWnd, prj, out]() {
        mainWnd->runAutoCutMode(prj, out);
      });
    } else {
      // Process positional arguments for video/project file
      QString videoFile;
      QStringList positional = parser.positionalArguments();
      for (const QString& arg : positional) {
        QFileInfo fInfo(arg);
        if (fInfo.exists() && fInfo.isFile()) {
          videoFile = fInfo.absoluteFilePath();
          TTSettings::instance()->setLastDirPath(fInfo.absolutePath());
          break;
        }
      }

      // Also check --project option for normal mode
      if (videoFile.isEmpty() && parser.isSet(projectOpt)) {
        videoFile = parser.value(projectOpt);
      }

      // Open file from command line after event loop starts
      if (!videoFile.isEmpty()) {
        if (TTSettings::instance()->logUI())
            qDebug() << "Opening file from command line:" << videoFile;
        QTimer::singleShot(100, [mainWnd, videoFile]() {
          if (videoFile.endsWith(".prj", Qt::CaseInsensitive) ||
              videoFile.endsWith(".ttcut", Qt::CaseInsensitive)) {
            mainWnd->openProjectFile(videoFile);
          } else {
            mainWnd->onReadVideoStream(videoFile);
          }
        });
      }
    }

    a.connect( &a, &QApplication::lastWindowClosed, &a, &QApplication::quit );
    // Execute application and start event loop
    return a.exec();

    delete mainWnd;
  }
  catch (...)
  {
    qWarning("Unhandled exception occurred!");
    qWarning("TTCut exited unexpectectly!");
  }
}
