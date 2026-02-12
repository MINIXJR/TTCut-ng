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

#ifdef MACX
#include <QMacStyle>
#endif

// class declaration for the main window class
#include "ttcutmainwindow.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"

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

    //QPlastiqueStyle* style = new QPlastiqueStyle();
#ifdef MACX
    QMacStyle* style = new QMacStyle();
    a.setStyle(style);
#endif

    // Caption text in applications title bar
    mainWnd->setWindowTitle( TTCut::versionString );
    mainWnd->show();

    // set initial size of applications main window
    mainWnd->resize(1024, 768);

    // Process command line arguments for video file
    QStringList args = a.arguments();
    QString videoFile;
    for (int i = 1; i < args.size(); i++) {
      QString arg = args.at(i);
      // Skip options starting with -
      if (arg.startsWith("-")) continue;
      // Check if it's a file that exists
      QFileInfo fInfo(arg);
      if (fInfo.exists() && fInfo.isFile()) {
        videoFile = fInfo.absoluteFilePath();
        TTCut::lastDirPath = fInfo.absolutePath();
        break;
      }
    }

    // Open file from command line after event loop starts
    if (!videoFile.isEmpty()) {
      qDebug() << "Opening file from command line:" << videoFile;
      QTimer::singleShot(100, [mainWnd, videoFile]() {
        if (videoFile.endsWith(".prj", Qt::CaseInsensitive)) {
          mainWnd->openProjectFile(videoFile);
        } else {
          mainWnd->onReadVideoStream(videoFile);
        }
      });
    }

    a.connect( &a, SIGNAL(lastWindowClosed()), &a, SLOT(quit()) );
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
