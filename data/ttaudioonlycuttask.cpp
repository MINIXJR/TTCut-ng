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
// TTAUDIOONLYCUTTASK
//
// The audio-only cut used to run synchronously inside TTAVData::
// doAudioOnlyCut(), on the GUI thread, kept responsive only by sprinkled
// qApp->processEvents() calls. It now runs here, in a thread pool task, so the
// GUI thread stays free and the cut can honour a cancel - same move Task 5/8
// already made for the H.264/H.265 and MPEG-2 final cuts.
//
// Status messages keep the exact shape, wording and order of the synchronous
// version. The tr() calls therefore deliberately name TTAVData as translation
// context - the strings did not change, only the file they live in, and
// re-contexting them would silently drop their existing translations.
// ----------------------------------------------------------------------------

#include "ttaudioonlycuttask.h"

#include "ttavdata.h"
#include "ttavlist.h"
#include "ttaudiolist.h"

#include "../avstream/ttavstream.h"
#include "../common/ttcut.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

/**
 * Audio-only cut task
 */
TTAudioOnlyCutTask::TTAudioOnlyCutTask(TTAVData* avData, TTAVItem* avItem) :
                TTThreadTask("AudioOnlyCutTask")
{
  mpAVData = avData;
  mpAVItem = avItem;
}

/**
 * Init task
 */
void TTAudioOnlyCutTask::init(const TTAudioOnlyCutParams& params)
{
  mParams = params;
}

/**
 * Operation abort request
 *
 * Runs on the GUI thread while operation() runs on the pool. Only atomic flag
 * stores happen here - no worker-owned state is touched, and nothing is read
 * back (the provider's wasAborted() is a plain bool owned by the worker).
 */
void TTAudioOnlyCutTask::onUserAbort()
{
  mCancelRequested.store(true, std::memory_order_relaxed);
  mMkvProvider.requestAbort();
  abort();   // TTThreadTask bookkeeping (mIsAborted; pool Canceled chain)
}

/**
 * Clean up after operation
 */
void TTAudioOnlyCutTask::cleanUp()
{
}

/**
 * Delete everything this run created.
 *
 * Abort only: on a real failure the products of the run stay on disk, which
 * is the behaviour the synchronous cut had. Failures to remove a file are
 * logged and skipped - an abort must always reach the Canceled bracket and
 * must never hang or fail on cleanup.
 */
void TTAudioOnlyCutTask::abortCleanup()
{
  for (const QString& f : mCreatedFiles) {
    if (f.isEmpty() || !QFile::exists(f)) continue;
    if (!QFile::remove(f))
      log->warningMsg(__FILE__, __LINE__,
          QString("abort cleanup: could not remove %1").arg(f));
  }
  mCreatedFiles.clear();
}

/**
 * Leave operation() through the abort path.
 *
 * The message-only TTAbortException constructor is used on purpose. Its
 * (caller, line, msg) sibling logs the text through TTMessageLogger::
 * fatalMsg() - a deliberate cancel must not read as a failure (see
 * TTH26xCutTask::abortNow() for the measurement that established this).
 */
void TTAudioOnlyCutTask::abortNow()
{
  abortCleanup();
  throw TTAbortException("user abort");
}

/**
 * Poll point between two phases of the pipeline
 */
void TTAudioOnlyCutTask::abortIfRequested()
{
  if (!cancelRequested()) return;
  abortNow();
}

/**
 * Forward one Step report to the GUI.
 */
void TTAudioOnlyCutTask::reportStep(const QString& msg, quint64 percent)
{
  mpAVData->onStatusReport(StatusReportArgs::Step, msg, percent);
}

/**
 * Announce a stage change (feeds the remaining-time estimator)
 */
void TTAudioOnlyCutTask::reportStage(int stage)
{
  mpAVData->onStatusReport(StatusReportArgs::Stage, QString(), stage);
}

/**
 * Abort funnel around the audio-only cut - identical in purpose and in its
 * cancel gate to TTH26xCutTask::operation(); see the reasoning there.
 */
void TTAudioOnlyCutTask::operation()
{
  try {
    runAudioCut();
  }
  catch (const TTAbortException&) {
    if (cancelRequested()) abortCleanup();
    throw;
  }
}

/**
 * Run the audio-only cut: extract the audio track(s) for the kept segments,
 * then (depending on the working audio-only format) mux them into one .mka.
 */
void TTAudioOnlyCutTask::runAudioCut()
{
  if (mpAVItem->audioCount() > 0)
    reportStage(StatusReportArgs::StageAudio);

  // Stage 1: stream-copy each track to its source codec (consolidated onto
  // TTAVData::cutAudioTracks).
  QStringList trackFiles;
  QStringList trackLanguages;
  QList<float> firstTrackDrifts = mpAVData->cutAudioTracks(
      mpAVItem, mParams.videoKeepList, mParams.normalizeAcmod,
      [&](int i, const QString& /*ext*/) {
        return mpAVData->createCutFileName(mParams.targetFileName,
            mpAVItem->audioStreamAt(i)->fileName(), i + 1);
      },
      [&](int i, const QString& path, const QString& lang, bool ok) {
        // Registered even when the cut did NOT succeed: an aborted or failed
        // audio cut leaves a partial file behind, and abortCleanup() can
        // only remove what it knows about. mCreatedFiles is read on the
        // abort path only, so this changes nothing for a real failure.
        mCreatedFiles.append(path);
        if (ok) {
          trackFiles     << path;
          trackLanguages << lang;
          log->infoMsg(__FILE__, __LINE__, QString("Audio track %1 cut: %2").arg(i+1).arg(path));
        }
        reportStep(TTAVData::tr("Audio track %1 done").arg(i+1),
                   (i + 1) * 100 / qMax(1, mpAVItem->audioCount()));
      },
      {},
      [&](int i, int percent) {
        int overall = (i * 100 + percent) / qMax(1, mpAVItem->audioCount());
        reportStep(TTAVData::tr("Cutting audio track %1 of %2...")
                       .arg(i+1).arg(mpAVItem->audioCount()), overall);
      },
      // Abort predicate: polled inside cutAudioStream's read loop and
      // between tracks, so a cancel stops the audio phase at the next
      // packet - same wiring as TTH26xCutTask's audio phase.
      [this] { return cancelRequested(); });

  mDrifts = firstTrackDrifts;

  // Poll point: a cancel mid-track-loop breaks cutAudioTracks() out early
  // (the loop stops, and the in-progress track's cutAudioStream already
  // returned false via its own abort check). Caught here regardless of how
  // many tracks had already completed, so a cancel is never misread as the
  // "no output files" failure below.
  abortIfRequested();

  if (trackFiles.isEmpty()) {
    log->errorMsg(__FILE__, __LINE__, "Audio-only cut produced no output files");
    mError         = TTAVData::tr("Audio-only cut produced no output files");
    mOutputSummary = TTAVData::tr("Audio cut failed");
    mExitMessage   = TTAVData::tr("Audio cut failed");
    return;
  }

  // Some, but not all, requested tracks failed (cutAudioTracks() already
  // logged an errorMsg() per failed track - see TTAVData::cutAudioTracks()).
  // trackFiles.isEmpty() above already covers "all failed"; the two checks
  // are mutually exclusive by construction (that branch returns), so this
  // one only ever fires on a genuine partial failure and never overwrites
  // or is overwritten by the all-failed message.
  const int requestedTrackCount = mpAVItem->audioCount();
  if (trackFiles.size() < requestedTrackCount) {
    log->errorMsg(__FILE__, __LINE__,
                  QString("Audio-only cut: only %1 of %2 track(s) produced an output file")
                      .arg(trackFiles.size()).arg(requestedTrackCount));
    mError       = TTAVData::tr("Only %1 of %2 audio track(s) could be cut")
                       .arg(trackFiles.size()).arg(requestedTrackCount);
    mExitMessage = TTAVData::tr("Audio cut failed");
  }

  // Dispatch by chosen output format (working set, per-cut/per-project;
  // copied into mParams.audioOnlyFormat on the GUI thread before this task
  // started).
  switch (mParams.audioOnlyFormat) {
    case TTCut::AOF_OriginalES: {
      QString dir = QFileInfo(trackFiles.first()).absolutePath();
      log->infoMsg(__FILE__, __LINE__,
                   QString("Audio-only cut complete: %1 ES file(s) in %2")
                     .arg(trackFiles.size()).arg(dir));
      mOutputSummary = TTAVData::tr("%1 audio file(s) in %2").arg(trackFiles.size()).arg(dir);
      break;
    }

    case TTCut::AOF_OriginalMKA: {
      if (QFileInfo(mParams.mkaOutputPath).exists()) QFile::remove(mParams.mkaOutputPath);

      reportStage(StatusReportArgs::StageMux);
      reportStep(TTAVData::tr("Muxing audio tracks into MKA..."), 0);

      // Registered unconditionally, like the per-track files above: a
      // cancelled or failed mux still leaves a partial .mka on disk.
      mCreatedFiles.append(mParams.mkaOutputPath);
      if (!mMkvProvider.muxAudioOnly(mParams.mkaOutputPath, trackFiles, trackLanguages)) {
        // A cancel comes back through the same false return as a real mux
        // failure.
        if (mMkvProvider.wasAborted() || cancelRequested()) abortNow();
        log->errorMsg(__FILE__, __LINE__,
                      QString("MKA mux failed: %1").arg(mMkvProvider.lastError()));
        const QString muxError = TTAVData::tr("MKA mux failed: %1").arg(mMkvProvider.lastError());
        mError         = muxError;
        mOutputSummary = muxError;
        mExitMessage   = TTAVData::tr("Audio cut failed");
      } else {
        log->infoMsg(__FILE__, __LINE__, QString("Audio-only cut complete: %1").arg(mParams.mkaOutputPath));
        for (const QString& f : trackFiles) QFile::remove(f);
        mOutputSummary = mParams.mkaOutputPath;
      }
      break;
    }

    case TTCut::AOF_MP3:
    case TTCut::AOF_AAC: {
      QString dir = QFileInfo(trackFiles.first()).absolutePath();
      log->warningMsg(__FILE__, __LINE__,
                      "MP3/AAC re-encoding not implemented yet — leaving original ES files");
      mOutputSummary = TTAVData::tr("MP3/AAC re-encoding not implemented yet — original ES files in %1").arg(dir);
      break;
    }
  }

  // Exit bracket text: both failure branches above (partial-track failure,
  // failed MKA mux) already set mExitMessage to the short "Audio cut failed"
  // form - same text the "no output files at all" branch uses via its own
  // early return. Only fall back to the success wording here when neither
  // branch ran; mError/mOutputSummary keep carrying the reason-specific
  // detail regardless of which of the two set mExitMessage. No poll point
  // after this, deliberately: the run is complete and there is nothing left
  // to cancel (same rule as TTH26xCutTask/TTMuxTask after a successful mux).
  if (mExitMessage.isEmpty())
    mExitMessage = TTAVData::tr("Audio cut complete");
}
