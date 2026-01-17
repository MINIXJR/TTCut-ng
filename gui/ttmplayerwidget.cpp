/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / www.tritime.org                         */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttmplayerwidget.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 05/03/2008 */
/* MODIFIED: 2026 - Migrated from mplayer to mpv for Wayland support          */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMPLAYERWIDGET (now using mpv)
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 2 of the License, or (at your option) any later version.    */
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

#include "ttmplayerwidget.h"

#include <QLayout>
#include <QFile>

/* /////////////////////////////////////////////////////////////////////////////
 * Constructor for MplayerWidget (now uses mpv)
 */
TTMplayerWidget::TTMplayerWidget(QWidget *parent)
    : TTVideoPlayer(parent)
{
  log = TTMessageLogger::getInstance();
  mplayerProc = nullptr;
  mIsPlaying = false;

  // Ensure this widget gets a native window for embedding
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_DontCreateNativeAncestors);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Destructor: clean up used resources
 */
TTMplayerWidget::~TTMplayerWidget()
{
  cleanUp();
}

void TTMplayerWidget::cleanUp()
{
  stopMplayer();
  if (mplayerProc != nullptr) {
    delete mplayerProc;
    mplayerProc = nullptr;
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Play the current movie; You have to load the current movie first!
 */
void TTMplayerWidget::play()
{
  // Stop any existing playback first
  stopMplayer();

  // Create process if needed
  if (mplayerProc == nullptr) {
    mplayerProc = new QProcess(this);
    connectSignals();
  }

  playMplayer(currentMovie);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Stop playing the movie
 */
void TTMplayerWidget::stop()
{
  stopMplayer();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Load the current movie from file
 */
void TTMplayerWidget::load(QString fileName)
{
  // Stop any existing playback
  stopMplayer();

  currentMovie = fileName;

  if (parentWidget() == nullptr)
    resize(640, 480);
  else
    resize(parentWidget()->width()-1, parentWidget()->height()-1);

  emit optimalSizeChanged();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Set the subtitle file for playback
 */
void TTMplayerWidget::setSubtitleFile(QString subtitleFile)
{
  currentSubtitleFile = subtitleFile;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Clear the subtitle file
 */
void TTMplayerWidget::clearSubtitleFile()
{
  currentSubtitleFile.clear();
}

QSize TTMplayerWidget::sizeHint() const
{
  if (parentWidget() == nullptr)
    return QSize(640, 480);

  return QSize(parentWidget()->width()-1, parentWidget()->height()-1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Returns a value indicating if the native movie controller controls are
 * visible.
 */
bool TTMplayerWidget::getControlsVisible() const
{
  return false;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Set a value indicating if the native movie controller controls are visible.
 */
void TTMplayerWidget::setControlsVisible(bool visible)
{
  areControlsVisible = visible;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Connect signals for the QProcess
 */
void TTMplayerWidget::connectSignals()
{
  if (mplayerProc == nullptr) return;

  connect(mplayerProc, SIGNAL(started()),
          this, SLOT(mplayerStarted()));
  connect(mplayerProc, SIGNAL(readyReadStandardOutput()),
          this, SLOT(readFromStdout()));
  connect(mplayerProc, SIGNAL(readyReadStandardError()),
          this, SLOT(readFromStdout()));
  connect(mplayerProc, SIGNAL(finished(int, QProcess::ExitStatus)),
          this, SLOT(exitMplayer(int, QProcess::ExitStatus)));
  connect(mplayerProc, SIGNAL(errorOccurred(QProcess::ProcessError)),
          this, SLOT(errorMplayer(QProcess::ProcessError)));
  connect(mplayerProc, SIGNAL(stateChanged(QProcess::ProcessState)),
          this, SLOT(stateChangedMplayer(QProcess::ProcessState)));
}

/* /////////////////////////////////////////////////////////////////////////////
 * mpv interface - start mpv playing file <fileName>
 */
bool TTMplayerWidget::playMplayer(QString videoFile)
{
  if (mplayerProc == nullptr)
    return false;

  if (mplayerProc->state() != QProcess::NotRunning) {
    log->errorMsg(__FILE__, __LINE__, "mpv process still running, cannot start new playback");
    return false;
  }

  // Build mpv command line
  QStringList mpv_cmd;

  // mpv options for embedded playback
  // --wid for X11 embedding, --vo=x11 for compatibility
  mpv_cmd << "--really-quiet"
          << "--vo=x11"                           // Force X11 output for embedding
          << QString("--wid=%1").arg(winId())     // Embed in our window
          << "--no-osc"                           // Disable on-screen controller
          << "--no-input-default-bindings"        // Disable keyboard shortcuts
          << "--keep-open=no"                     // Exit when done
          << "--force-window=yes";                // Force window creation

  // Add subtitle file if set
  if (!currentSubtitleFile.isEmpty() && QFile::exists(currentSubtitleFile)) {
    mpv_cmd << QString("--sub-file=%1").arg(currentSubtitleFile);
  }

  mpv_cmd << videoFile;

  log->infoMsg(__FILE__, __LINE__, QString("mpv command: mpv %1").arg(mpv_cmd.join(" ")));

  // Start the mpv process
  mplayerProc->start("mpv", mpv_cmd);

  mIsPlaying = true;
  return true;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Stop mpv playing
 */
bool TTMplayerWidget::stopMplayer()
{
  if (mplayerProc == nullptr)
    return false;

  if (mplayerProc->state() == QProcess::NotRunning)
    return false;

  log->debugMsg(__FILE__, __LINE__, "Stopping mpv process...");

  // Terminate gracefully first
  mplayerProc->terminate();

  // Wait for process to finish
  if (!mplayerProc->waitForFinished(2000)) {
    log->debugMsg(__FILE__, __LINE__, "mpv didn't terminate, killing...");
    mplayerProc->kill();
    mplayerProc->waitForFinished(1000);
  }

  mIsPlaying = false;
  return true;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot called when mpv process was started
 */
void TTMplayerWidget::mplayerStarted()
{
  log->infoMsg(__FILE__, __LINE__, "mpv process started");
}

/* /////////////////////////////////////////////////////////////////////////////
 * Read messages from mpv process stdout/stderr
 */
void TTMplayerWidget::readFromStdout()
{
  if (mplayerProc == nullptr) return;
  if (mplayerProc->state() != QProcess::Running) return;

  QByteArray output = mplayerProc->readAllStandardOutput();
  QByteArray errors = mplayerProc->readAllStandardError();

  if (!output.isEmpty()) {
    log->infoMsg(__FILE__, __LINE__, QString("mpv: %1").arg(QString(output).trimmed()));
  }
  if (!errors.isEmpty()) {
    log->infoMsg(__FILE__, __LINE__, QString("mpv err: %1").arg(QString(errors).trimmed()));
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot called when mpv process exits
 */
void TTMplayerWidget::exitMplayer(int e_code, QProcess::ExitStatus e_status)
{
  log->debugMsg(__FILE__, __LINE__, QString("mpv exit code %1 / status %2").arg(e_code).arg(e_status));

  mIsPlaying = false;
  emit playerFinished();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot called on mpv process error
 */
void TTMplayerWidget::errorMplayer(QProcess::ProcessError error)
{
  // Don't log error if we killed the process ourselves
  if (error != QProcess::Crashed || mplayerProc->state() == QProcess::Running) {
    log->errorMsg(__FILE__, __LINE__, QString("mpv error: %1").arg(error));
  }

  mIsPlaying = false;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Slot called when mpv process state changes
 */
void TTMplayerWidget::stateChangedMplayer(QProcess::ProcessState newState)
{
  log->debugMsg(__FILE__, __LINE__, QString("mpv state changed: %1").arg(newState));

  if (newState == QProcess::NotRunning) {
    mIsPlaying = false;
  }
}
