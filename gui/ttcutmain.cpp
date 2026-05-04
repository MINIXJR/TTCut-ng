/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : main.cpp                                                        */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/01/2005 */
/* MODIFIED: b. altendorf                                    DATE: 02/23/2005 */
/* MODIFIED: b. altendorf                                    DATE: 05/25/2007 */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
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

// class declaration for the main window class
#include "ttcutmainwindow.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"

#include <QCommandLineParser>
#include <QTimer>

/* /////////////////////////////////////////////////////////////////////////////
 * TTCut main
 */
int main( int argc, char **argv )
{
  try
  {
    QT_REQUIRE_VERSION(argc, argv, "5.0.0");

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

    QTranslator qtTranslator;
    if (!qtTranslator.load("qt_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
      TTMessageLogger* log = TTMessageLogger::getInstance();
      log->warningMsg(__FILE__, __LINE__,
                    QString("Qt translation file %1 for locale %2 could not be found!").
                    arg("qt_" + QLocale::system().name()).
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
