/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTMPLAYERWIDGET (now using mpv)
// ----------------------------------------------------------------------------

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

  connect(mplayerProc, &QProcess::started,
          this, &TTMplayerWidget::mplayerStarted);
  connect(mplayerProc, &QProcess::readyReadStandardOutput,
          this, &TTMplayerWidget::readFromStdout);
  connect(mplayerProc, &QProcess::readyReadStandardError,
          this, &TTMplayerWidget::readFromStdout);
  connect(mplayerProc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, &TTMplayerWidget::exitMplayer);
  connect(mplayerProc, &QProcess::errorOccurred,
          this, &TTMplayerWidget::errorMplayer);
  connect(mplayerProc, &QProcess::stateChanged,
          this, &TTMplayerWidget::stateChangedMplayer);
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
