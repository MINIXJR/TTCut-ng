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
#include "ttcentredtitlestyle.h"
#include "ttcutmainwindow.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../common/ttavlog.h"

#include <QCommandLineParser>
#include <QTimer>

#include <cstdlib>
#include <clocale>

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
    ttInstallAvLogCallback();

    QApplication a( argc, argv );

    // libmpv (und libavfilter, std::stod, ...) verlangen LC_NUMERIC=C —
    // sonst returnt mpv_create() NULL und Filtergraph-Strings mit "0.5"
    // werden unter de_DE als ungültig abgelehnt. QApplication ruft
    // setlocale(LC_ALL, "") und aktiviert damit die System-Locale; den
    // numerischen Anteil korrigieren wir direkt im Anschluss. Qt's UI-
    // Formatierung läuft über QLocale und bleibt davon unberührt.
    std::setlocale(LC_NUMERIC, "C");

    a.setApplicationName("TTCut-ng");
    // Without an organisation name, an argument-less QSettings resolves to
    // "Unknown Organization" — which is where TTQuickJumpDialog's size used to
    // land, in a second file next to the real one.
    a.setOrganizationName("TTCut-ng");

    // Force the lazy TTSettings singleton to construct and run its first
    // load() before any UI code reads a persisted value.
    (void)TTSettings::instance();

    // Centre QGroupBox titles application-wide: some styles (Fusion, the
    // usual default outside KDE) left-align them, and we want the same look
    // across all panels regardless of the user's theme.
    //
    // This used to be a one-line application stylesheet. It cannot be: any
    // stylesheet on the QApplication wraps the platform style in
    // QStyleSheetStyle for EVERY widget, and under the KDE styles that stops
    // the animation of indeterminate QProgressBars - TTProgressBar's stall
    // pulse stood still (Breeze measured at 1.0 instead of 63.5 repaints per
    // second; tools/diag/test_pulse_stylesheet). The proxy style keeps the
    // real style in charge of drawing and only rewrites the title alignment.
    TTCentredTitleStyle::install();

    // Load qtbase translations (since Qt 5.x the per-module split replaced
    // the legacy qt_<locale>.qm bundle; in modern installs that legacy file
    // is an empty stub. qtbase_<locale>.qm carries QMessageBox button labels
    // like Yes/No/OK/Cancel). Forward-compatible with Qt 6.
    QTranslator qtTranslator;
    if (!qtTranslator.load("qtbase_" + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
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

    // Diagnose-Schalter (KWin-Repaint-Bisektion, 2026-08-02): mit
    // TTCUT_DIAG_HIDE=name1,name2,... werden die benannten Kind-Widgets vor
    // dem Anzeigen versteckt (Objektnamen aus den .ui-Dateien). Erlaubt die
    // binäre Eingrenzung, welcher Fensterteil den KWin-Auffrischfehler
    // braucht, ohne weitere Codeänderungen. Ohne die Variable: wirkungslos.
    if (qEnvironmentVariableIsSet("TTCUT_DIAG_HIDE")) {
      TTMessageLogger* log = TTMessageLogger::getInstance();
      const QStringList names =
          QString::fromLocal8Bit(qgetenv("TTCUT_DIAG_HIDE"))
              .split(',', Qt::SkipEmptyParts);
      for (const QString& rawName : names) {
        const QString name = rawName.trimmed();
        QWidget* w = mainWnd->findChild<QWidget*>(name);
        if (w) {
          w->hide();
          log->warningMsg(__FILE__, __LINE__,
              QString("TTCUT_DIAG_HIDE: hid '%1'").arg(name));
        } else {
          log->warningMsg(__FILE__, __LINE__,
              QString("TTCUT_DIAG_HIDE: no widget named '%1'").arg(name));
        }
      }
    }

    mainWnd->show();

    // No resize() here. There used to be a resize(1024, 768) at this point,
    // dating back to the initial commit, which threw away whatever geometry
    // the constructor had just restored — every start came up 1024x768 and the
    // saved size was silently pointless. The window's size is decided in
    // TTCutMainWindow's constructor: the stored value, or 80% of the screen
    // when there is none.

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
