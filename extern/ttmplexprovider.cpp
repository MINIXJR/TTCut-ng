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
// TTMPLEXPROVIDER
// ----------------------------------------------------------------------------

#include "ttmplexprovider.h"

#include "../common/ttsettings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#define EVENT_LOOP_INTERVALL 10

/**
 *Usage: mplex [params] -o <output filename pattern> <input file>...
 *%d in the output file name is by segment count
 *where possible params are:
 *--verbose|-v num
 *  Level of verbosity. 0 = quiet, 1 = normal 2 = verbose/debug
 *--format|-f fmt
 *  Set defaults for particular MPEG profiles
 *  [0 = Generic MPEG1, 1 = VCD, 2 = user-rate VCD, 3 = Generic MPEG2,
 *   4 = SVCD, 5 = user-rate SVCD
 *   6 = VCD Stills, 7 = SVCD Stills, 8 = DVD with NAV sectors, 9 = DVD]
 *--mux-bitrate|-r num
 *  Specify data rate of output stream in kbit/sec
 *  (default 0=Compute from source streams)
 *--video-buffer|-b num [, num...]
 *  Specifies decoder buffers size in kB.  [ 20...2000]
 *--lpcm-params | -L samppersec:chan:bits [, samppersec:chan:bits]
 *--mux-limit|-l num
 *  Multiplex only num seconds of material (default 0=multiplex all)
 *--sync-offset|-O num ms|s|mpt
 *  Specify offset of timestamps (video-audio) in mSec
 *--sector-size|-s num
 *  Specify sector size in bytes for generic formats [256..16384]
 *--vbr|-V
 *  Multiplex variable bit-rate video
 *--packets-per-pack|-p num
 *  Number of packets per pack generic formats [1..100]
 *--system-headers|-h
 *  Create System header in every pack in generic formats
 *--max-segment-size|-S size
 *  Maximum size of output file(s) in Mbyte (default: 0) (no limit)
 *--split-segment|-M
 *  Simply split a sequence across files rather than building run-out/run-in
 *--workaround|-W workaround [, workaround ]
 *--help|-?
 *  Print this lot out!
 */

/*! \brief Provides access to the external multiplexer <mplex>
 *
 * In the future, this modul should be implemented as plugin
 */
TTMplexProvider::TTMplexProvider(TTMuxListData* muxList) : IStatusReporter()
{
  log       = TTMessageLogger::getInstance();
  mpMuxList = muxList;
  mAudioSyncOffsetMs = 0;
  proc      = nullptr;
}

//! Clean up used resources
TTMplexProvider::~TTMplexProvider()
{
  delete proc;
  proc = nullptr;
}

/*! \brief Write mux data in muxscript
 *
 * Theire should be only one muxscript for one session. Purpose of the muxscript
 * is batch-muxing all cutted videos from the current session, after(!) quitting
 * TTCut.
 */
void TTMplexProvider::writeMuxScript()
{
  static const QString muxFileName = QStringLiteral("muxscript.sh");

  QStringList mplexCmd;
  QFileInfo   muxInfo(QDir(TTSettings::instance()->cutDirPath()), muxFileName);
  QFile       muxFile(muxInfo.absoluteFilePath());

  muxFile.remove();
  if (!muxFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    log->errorMsg(__FILE__, __LINE__,
        QString("Cannot open mux script for writing: %1").arg(muxFile.fileName()));
    return;
  }

  QTextStream muxOutStream(&muxFile);

  muxOutStream << "# TTCut - Mplex script ver. 1.0" << "\n";
  muxOutStream << "#!/bin/sh" << "\n";
  muxOutStream << "#\n";

  for (int i=0; i < mpMuxList->count(); i++)
  {
    mplexCmd.clear();
    mplexCmd << "mplex";
    mplexCmd << createMplexArguments(mpMuxList->videoFilePathAt(i), mpMuxList->audioFilePathsAt(i), true);

    muxOutStream << mplexCmd.join(" ") << "\n#\n";
  }

  muxFile.flush();
  muxFile.close();

  // make muxscript executeable by the owner/user
  bool result = muxFile.setPermissions(
        QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|
        QFile::ReadUser |QFile::WriteUser |QFile::ExeUser |
        QFile::ReadGroup|QFile::ExeGroup);

  if (!result)
    log->warningMsg(__FILE__, __LINE__, "Cannot modify permissions of the muxscript!");
}

//! Call mplex to mux current stream.
void TTMplexProvider::mplexPart(int index)
{
  mCurrentMuxIndex = index;
  int  update      = EVENT_LOOP_INTERVALL;
  // mWasAborted is an output, not an input: cleared at this run's entry point
  // (see the header for why mAbortRequested has no clearing point of its own).
  mWasAborted      = false;
  delete proc;             // free any QProcess from a previous startMplex
  proc             = new QProcess();

  proc->setProcessChannelMode( QProcess::MergedChannels );
  qApp->processEvents();

  // Functor connects: errorOccurred replaces the old 'error' signal, and
  // the new-style syntax is type-checked at compile time so typos no
  // longer fail silently at runtime.
  connect(proc, &QProcess::errorOccurred, this, &TTMplexProvider::onProcError);
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &TTMplexProvider::onProcFinished);
  connect(proc, &QProcess::readyRead,    this, &TTMplexProvider::onProcReadOut);
  connect(proc, &QProcess::started,      this, &TTMplexProvider::onProcStarted);
  connect(proc, &QProcess::stateChanged, this, &TTMplexProvider::onProcStateChanged);

  QString     mplexCmd  = "mplex";
  QStringList mplexArgs = createMplexArguments(mpMuxList->videoFilePathAt(index), mpMuxList->audioFilePathsAt(index), false);

  log->debugMsg(__FILE__, __LINE__, QString("Mplex command string: %1").arg(mplexArgs.join(" ")));

  emit statusReport(StatusReportArgs::ShowProcessForm, "Starting mplex", 0);
  qApp->processEvents();

  // A cancel can already have arrived in one of the qApp->processEvents()
  // calls above - this method runs on the GUI thread, so the Cancel button's
  // slot chain executes inside them. Then there is nothing to start, and the
  // partial-output cleanup below has nothing to remove either.
  if (!checkAbort()) {
    proc->start(mplexCmd, mplexArgs);

    // just a very simple event loop ;-)
    while (proc->state() == QProcess::Starting ||
           proc->state() == QProcess::Running     )
    {
      // The abort poll the loop was missing. Nothing here ever looked at a
      // flag and nobody stopped the child, so a Cancel click was delivered
      // (the processEvents() below runs the whole slot chain) and then had no
      // effect - the mux ran to completion and only the progress dialog
      // disappeared. Polled on every iteration, not only every
      // EVENT_LOOP_INTERVALL: a relaxed atomic load is far cheaper than the
      // QProcess::state() call the loop condition already does.
      if (checkAbort()) {
        stopProcess();
        break;
      }

      update--;
      if ( update == 0 )
      {
        qApp->processEvents();
        update = EVENT_LOOP_INTERVALL;
      }
    }
  }

  if (mWasAborted) {
    // Rule for this feature: a cancel deletes everything the run created, a
    // genuine error leaves the files for diagnosis. The partial .mpg is this
    // provider's own product and this is the only place that knows its path;
    // the cut elementary streams that fed it are deleted by the abort block in
    // TTAVData::onCutFinished(), which owns that list - so nothing is removed
    // twice.
    const QString outputFile = createOutputFilePath(mpMuxList->videoFilePathAt(index));

    if (!QFile::exists(outputFile))
      // The cancel arrived before proc->start(), so mplex never created it.
      log->debugMsg(__FILE__, __LINE__,
          QString("No partial mplex output to remove (%1)").arg(outputFile));
    else if (!QFile::remove(outputFile))
      log->warningMsg(__FILE__, __LINE__,
          QString("abort cleanup: could not remove %1").arg(outputFile));
    else
      log->debugMsg(__FILE__, __LINE__,
          QString("Removed partial mplex output %1").arg(outputFile));

    // Not "Mplex finished": a cancelled mux must not read as a completed one
    // anywhere, not even in the details log. Still no error/warning level -
    // the cancel is a deliberate user action.
    emit statusReport(StatusReportArgs::HideProcessForm, tr("Mplex cancelled"), 0);
    qApp->processEvents();
    return;
  }

  emit statusReport(StatusReportArgs::HideProcessForm, tr("Mplex finished"), 0);

  qApp->processEvents();
}

// -----------------------------------------------------------------------------
// Poll point for the cooperative abort. Returns true (and records the abort)
// once a requestAbort() has arrived. Idempotent - the wait loop calls it on
// every iteration.
// -----------------------------------------------------------------------------
bool TTMplexProvider::checkAbort()
{
  if (!mAbortRequested.load(std::memory_order_relaxed)) return false;
  mWasAborted = true;
  return true;
}

// -----------------------------------------------------------------------------
// Stop the running mplex child process.
//
// SIGTERM first: mplex installs no signal handler, so the default action ends
// it right away and the kernel closes its output file descriptor - the partial
// .mpg is then a closed, quiescent file when the caller removes it, with no
// write still in flight. It is also what a user pressing Ctrl-C on a shell
// invocation would get, so a future mplex that DOES clean up after itself
// keeps that chance.
//
// SIGKILL only as the fallback, because SIGTERM can be ignored or blocked and
// a cancel must never be able to leave an mplex process running (it would keep
// writing into the file we are about to delete). Both waits are bounded.
//
// The GUI-thread block this can cost is 2 s + 1 s ONLY as long as the SIGKILL
// is reaped inside its second. If it is not, ~QProcess (via close()) waits for
// the child again with Qt's own 30 s budget and prints a qWarning - so the
// honest worst case is ~33 s, not 3 s. It needs a process that survives
// SIGKILL, i.e. one wedged in uninterruptible I/O; mplex on a local file
// cannot get there in practice, and the alternative (leaving the child running
// while we delete the file it writes) is worse. Do not restate the bound as
// "3 s" without this caveat.
// -----------------------------------------------------------------------------
void TTMplexProvider::stopProcess()
{
  emit statusReport(StatusReportArgs::AddProcessLine,
                    tr("Cancel requested - stopping the mplex process"), 0);
  qApp->processEvents();

  proc->terminate();

  // The state test is not redundant: mplex can finish on its own between the
  // abort poll and the terminate(), and waitForFinished() returns false for an
  // ALREADY finished process just as it does for a timeout. Without it that
  // ordinary race logged "did not exit on terminate()" and sent a SIGKILL to a
  // dead process - a misleading line about a wholly normal event.
  if (!proc->waitForFinished(2000) && proc->state() != QProcess::NotRunning) {
    log->debugMsg(__FILE__, __LINE__, "mplex did not exit on terminate() - killing it");
    proc->kill();
    proc->waitForFinished(1000);
  }
}

//! Creates the muxed video output file path
QString TTMplexProvider::createOutputFilePath(const QString& videoFilePath)
{
  QFileInfo outputFileInfo(videoFilePath);
  QDir      outputDirectory((TTSettings::instance()->muxOutputPath().isEmpty())
        ? outputFileInfo.absolutePath()
        : TTSettings::instance()->muxOutputPath());

  outputFileInfo.setFile(outputDirectory, outputFileInfo.fileName());

  return ttChangeFileExt(outputFileInfo.absoluteFilePath(), "mpg");
}

//! Create the mplex arguments for given video and audio files
QStringList TTMplexProvider::createMplexArguments(const QString& videoFilePath, const QStringList& audioFilePaths, bool escapeFileNames)
{
  QStringList mplexArgs;

  mplexArgs << "-f8";

  // Add A/V sync offset if set (--sync-offset num ms)
  // mplex -O: offset of timestamps (video-audio) in ms
  // av_offset_ms = audio_pts - video_pts; positive = audio starts after video
  // Positive -O value delays audio → correct for audio that is too early
  if (mAudioSyncOffsetMs != 0) {
    mplexArgs << "-O" << QString("%1ms").arg(mAudioSyncOffsetMs);
    qDebug() << "TTMplexProvider: Using A/V sync offset" << mAudioSyncOffsetMs << "ms";
  }

  auto shellEscape = [](const QString& s) -> QString {
    QString escaped = s;
    escaped.replace("'", "'\\''");
    return QString("'%1'").arg(escaped);
  };

  mplexArgs << "-o"
            << (escapeFileNames
                  ? shellEscape(createOutputFilePath(videoFilePath))
                  : createOutputFilePath(videoFilePath))
            << (escapeFileNames
                  ? shellEscape(videoFilePath)
                  : videoFilePath);

  QStringListIterator listIterator(audioFilePaths);
  while(listIterator.hasNext()) {
    QString audioPath = listIterator.next();
    mplexArgs << (escapeFileNames
                    ? shellEscape(audioPath)
                    : audioPath);
  }

  return mplexArgs;
}

//! Delete the elementary streams from disk
void TTMplexProvider::deleteElementaryStreams(const QString& videoFilePath, const QStringList& audioFilePaths)
{
  QFile videoFile(videoFilePath);
  bool  success = videoFile.remove();

  log->debugMsg(__FILE__, __LINE__, QString("Removing video stream %1 (%2)").
        arg(videoFilePath).
        arg(success ? "success" : "failed"));

  QStringListIterator listIterator(audioFilePaths);
  while(listIterator.hasNext()) {
    QString audioFilePath = listIterator.next();
    QFile   audioFile(audioFilePath);
    success = audioFile.remove();

    log->debugMsg(__FILE__, __LINE__, QString("Removing audio stream %1 (%2)").
        arg(audioFilePath).
        arg(success ? "success" : "failed"));
  }
}

// /////////////////////////////////////////////////////////////////////////////
// Event Processing
//
//! This signal is emitted when an error occurs with the process
void TTMplexProvider::onProcError(QProcess::ProcessError procError)
{
  // stopProcess() ends the child on purpose and QProcess reports that as
  // QProcess::Crashed. A deliberate cancel must not write an error line (the
  // standing rule for the whole cut-abort feature), so record it at debug
  // level instead; the cancel itself is reported by mplexPart().
  if (mWasAborted) {
    log->debugMsg(__FILE__, __LINE__,
        QString("QProcess error %1 after a user cancel - expected").arg(procError));
    return;
  }

  log->errorMsg(__FILE__, __LINE__, QString("QProcess error %1").arg(procError));
}

//! This signal is emitted once every time new data is available for reading from
//! the device
void TTMplexProvider::onProcReadOut()
{
  procOutput();
}

//! This signal is emitted when the process has started;
//! state() returns running
void TTMplexProvider::onProcStarted()
{
  emit statusReport(StatusReportArgs::AddProcessLine, tr("Starting mplex process"), 0);
  qApp->processEvents();
  procOutput();
}

//! This signal is emitted when the process finishes
void TTMplexProvider::onProcFinished(int, QProcess::ExitStatus exitStatus)
{
  // A cancelled mux has no elementary streams to consume: they are deleted by
  // the abort cleanup, not kept as the input of a finished mux. Stated
  // explicitly rather than left to the exit-status check below (a terminated
  // process reports CrashExit, so that check already covers it today) because
  // the two conditions mean different things.
  if (mWasAborted) return;

  if (exitStatus != QProcess::NormalExit) return;
  if (!TTSettings::instance()->workingMuxDeleteES()) return;

  // Only delete ES files for the current mux item, not all items in the list
  deleteElementaryStreams(mpMuxList->videoFilePathAt(mCurrentMuxIndex),
                          mpMuxList->audioFilePathsAt(mCurrentMuxIndex));
}

//! This signal is emitted whenever the state changed
void TTMplexProvider::onProcStateChanged(QProcess::ProcessState procState)
{
  QString stateMsg;

  switch (procState) {
    case QProcess::NotRunning:
      stateMsg = "The process is not running.";
      break;
    case QProcess::Starting:
      stateMsg = "The process is starting, but program has not yet been invoked.";
      break;
    case QProcess::Running:
      stateMsg = "The process is running and is ready for reading and writing.";
      break;
    default:
      stateMsg = "Unknown process state!";
      break;
  }

  log->debugMsg(__FILE__, __LINE__, stateMsg);
  emit statusReport(StatusReportArgs::AddProcessLine, stateMsg, 0);
  qApp->processEvents();
}

//! Write the process output to process form
void TTMplexProvider::procOutput()
{
  if (proc == 0) return;

  QString    line;
  QByteArray ba = proc->readAll();
  QTextStream out(&ba);

  while (!out.atEnd())
  {
    line = out.readLine();
    log->debugMsg(__FILE__, __LINE__, QString("* %1").arg(line));
    emit statusReport(StatusReportArgs::AddProcessLine, line, 0);
    qApp->processEvents();
  }
}

