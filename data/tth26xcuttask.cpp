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
// TTH26XCUTTASK
//
// The H.264/H.265 final cut used to run synchronously inside
// TTAVData::doH264Cut(), on the GUI thread, kept responsive only by sprinkled
// qApp->processEvents() calls. It now runs here, in a thread pool task, so the
// GUI thread stays free (and, from the next step on, can honour a cancel).
//
// Everything the pipeline needs is copied into TTH26xCutParams on the GUI
// thread before the task starts; the only live objects the worker touches are
// TTAVData (for its stateless cut helpers and the status forwarding) and
// TTAVItem's streams, which cannot change while the cut runs because the main
// window is disabled for the duration. That is the same arrangement
// TTCutPreviewTask has used for its worker-side stream access all along.
//
// Status messages keep the exact shape, wording and order of the synchronous
// version: the progress dialog and its remaining-time estimator are calibrated
// on them. The tr() calls therefore deliberately name TTAVData as translation
// context - the strings did not change, only the file they live in, and
// re-contexting them would silently drop their existing translations.
// ----------------------------------------------------------------------------

#include "tth26xcuttask.h"

#include "ttavdata.h"
#include "ttavlist.h"
#include "ttaudiolist.h"

#include "../avstream/ttavstream.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

extern "C" {
#include <libavcodec/codec_id.h>
}

/**
 * H.26x final cut task
 */
TTH26xCutTask::TTH26xCutTask(TTAVData* avData, TTAVItem* avItem) :
                TTThreadTask("H26xCutTask")
{
  mpAVData = avData;
  mpAVItem = avItem;
}

/**
 * Init task
 */
void TTH26xCutTask::init(const TTH26xCutParams& params)
{
  mParams = params;
}

/**
 * Operation abort request
 *
 * Runs on the GUI thread while operation() runs on the pool. Only atomic flag
 * stores happen here - no worker-owned state is touched, and nothing is read
 * back (the engines' wasAborted() is a plain bool owned by the worker).
 */
void TTH26xCutTask::onUserAbort()
{
  mCancelRequested.store(true, std::memory_order_relaxed);
  mSmartCut.requestAbort();
  mMkvProvider.requestAbort();
  abort();   // TTThreadTask bookkeeping (mIsAborted; pool Canceled chain)
}

/**
 * Clean up after operation
 */
void TTH26xCutTask::cleanUp()
{
}

/**
 * Delete everything this run created.
 *
 * Abort only: on a real failure the products of the run stay on disk, which is
 * the behaviour the synchronous cut had. Failures to remove a file are logged
 * and skipped - an abort must always reach the Canceled bracket and must never
 * hang or fail on cleanup.
 */
void TTH26xCutTask::abortCleanup()
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
 * TTThreadTask::run() catches TTAbortException, calls cleanUp() and emits
 * aborted() - which reaches TTAVData::onCutAborted() and closes the operation
 * with the Canceled bracket. No fail() text is recorded: a deliberate cancel is
 * not a failure, so neither the error dialog nor the log must show one.
 *
 * The message-only TTAbortException constructor is used on purpose. Its
 * (caller, line, msg) sibling logs the text through TTMessageLogger::fatalMsg()
 * - measured: a cancel wrote "[][..][tth26xcuttask:124] user abort" into the
 * persistent log, i.e. a fatal-level line for something the user asked for.
 * (TTCutVideoTask and TTCutPreviewTask still use that constructor; their
 * pre-existing fatal line on cancel is out of this task's scope.)
 */
void TTH26xCutTask::abortNow()
{
  abortCleanup();
  throw TTAbortException("user abort");
}

/**
 * Poll point between two phases of the pipeline
 */
void TTH26xCutTask::abortIfRequested()
{
  if (!cancelRequested()) return;
  abortNow();
}

/**
 * Forward one Step report to the GUI.
 *
 * TTAVData::onStatusReport re-emits TTAVData::statusReport with a null task
 * pointer, which is what the progress dialog treats as "value IS the percent" -
 * exactly what the synchronous code did. The signal crosses to the GUI thread
 * through the queued connection of its receivers, in emission order.
 */
void TTH26xCutTask::reportStep(const QString& msg, quint64 percent)
{
  mpAVData->onStatusReport(StatusReportArgs::Step, msg, percent);
}

/**
 * Announce a stage change (feeds the remaining-time estimator)
 */
void TTH26xCutTask::reportStage(int stage)
{
  mpAVData->onStatusReport(StatusReportArgs::Stage, QString(), stage);
}

/**
 * Record a failure and let operation() return.
 *
 * The closing Exit bracket and cutFinished() are emitted by
 * TTAVData::onH26xCutFinished() on the GUI thread, from these two strings.
 */
void TTH26xCutTask::fail(const QString& exitMessage, const QString& errorText)
{
  mExitMessage = exitMessage;
  mError       = errorText;
}

/**
 * Abort funnel around the cut pipeline.
 *
 * The poll points (abortIfRequested()/abortNow()) already clean up before they
 * throw, so this catch is the safety net for a TTAbortException thrown by a
 * CALLEE - today none of them does (cutAudioTracks() and cutSubtitleTracks()
 * report through return values), but a future one would otherwise reach
 * TTThreadTask::run() with the run's files still on disk.
 *
 * The cleanup is gated on cancelRequested() for the same reason
 * TTAVData::onCutAborted() gates on mSyncPhaseAbort: the standing rule is
 * "delete everything on a cancel, leave the files for diagnosis on a real
 * error", and TTAbortException is also thrown for real errors elsewhere in the
 * code base. abortCleanup() clears mCreatedFiles, so running it twice (once in
 * abortNow(), once here) is a no-op the second time.
 */
void TTH26xCutTask::operation()
{
  try {
    runCut();
  }
  catch (const TTAbortException&) {
    if (cancelRequested()) abortCleanup();
    throw;
  }
}

/**
 * Run the whole H.264/H.265 cut pipeline
 */
void TTH26xCutTask::runCut()
{
  TTVideoStream* vStream = mpAVItem->videoStream();

  // Initialize Smart Cut engine. The engine is a member (see the header):
  // onUserAbort() has to reach it from the GUI thread at any moment.
  // Direct connection on purpose - this is what keeps the thread guard in
  // TTAVData::onStatusReport effective. The engine lives in this thread, the
  // task object's affinity is the GUI thread, so the default AutoConnection
  // would queue the report and run reportStep() ON the GUI thread, where the
  // guard passes and processEvents() re-enters the event loop from inside a
  // queued slot invocation. (It would NOT reorder anything against the closing
  // Exit: both travel the same GUI event queue, first in first out.)
  connect(&mSmartCut, &TTESSmartCut::progressChanged, this,
      [this](int percent, const QString& msg) { reportStep(msg, percent); },
      Qt::DirectConnection);

  if (!mSmartCut.initialize(mParams.sourceFile, mParams.frameRate)) {
    // A cancel during the ES parse comes back through the same false return as
    // a real parse failure. wasAborted() is a plain bool owned by this thread,
    // so reading it here is safe; cancelRequested() catches the (tiny) window
    // in which the request arrived before initialize() cleared the engine's
    // own flag.
    if (mSmartCut.wasAborted() || cancelRequested()) abortNow();
    log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut init failed: %1").arg(mSmartCut.lastError()));
    fail(TTAVData::tr("Cutting failed - could not initialize"),
         TTAVData::tr("Could not initialize the cut engine: %1").arg(mSmartCut.lastError()));
    return;
  }
  abortIfRequested();

  // Inject frame-granularity display-order map from the open stream's wrapper.
  // Required for PAFF: buildFromFile fallback is field-granularity and would
  // mismatch the parser's frame count, aborting smartCutFrames.
  if (mParams.hasDisplayMap) {
    mSmartCut.setDisplayOrderMap(mParams.displayMap);
    if (TTSettings::instance()->logCutPipeline())
        qDebug() << "doH264Cut: Injected display-order map ("
                 << mParams.displayMap.count() << "entries)";
  }

  // SPS boundary check (H.264/H.265 only)
  for (int i = 0; i < mParams.cutFrames.size(); i++) {
    // Check CutOut (skip last segment)
    if (i < mParams.cutFrames.size() - 1) {
      if (mSmartCut.hasSPSChangeAtBoundary(mParams.cutFrames[i].second, true)) {
        log->warningMsg(__FILE__, __LINE__,
            QString("SPS change at CutOut segment %1 (frame %2) - possible aspect ratio change")
            .arg(i + 1).arg(mParams.cutFrames[i].second));
      }
    }
    // Check CutIn (skip first segment)
    if (i > 0) {
      if (mSmartCut.hasSPSChangeAtBoundary(mParams.cutFrames[i].first, false)) {
        log->warningMsg(__FILE__, __LINE__,
            QString("SPS change at CutIn segment %1 (frame %2) - possible aspect ratio change")
            .arg(i + 1).arg(mParams.cutFrames[i].first));
      }
    }
  }

  // The SPS boundary scan above can run for a while on a long cut list
  abortIfRequested();

  // Perform frame-accurate video cut
  reportStage(StatusReportArgs::StageVideo);
  reportStep(TTAVData::tr("Cutting video (Smart Cut)..."), 0);
  mCreatedFiles.append(mParams.tempVideoFile);
  if (!mSmartCut.smartCutFrames(mParams.tempVideoFile, mParams.cutFrames)) {
    // Same false return for a cancel as for a real failure - see the
    // initialize() branch above.
    if (mSmartCut.wasAborted() || cancelRequested()) abortNow();
    log->errorMsg(__FILE__, __LINE__, QString("TTESSmartCut failed: %1").arg(mSmartCut.lastError()));
    fail(TTAVData::tr("Cutting failed"),
         TTAVData::tr("Cutting failed: %1").arg(mSmartCut.lastError()));
    return;
  }
  abortIfRequested();

  log->infoMsg(__FILE__, __LINE__, QString("Smart Cut complete: %1 frames re-encoded, %2 frames stream-copied")
      .arg(mSmartCut.framesReencoded()).arg(mSmartCut.framesStreamCopied()));

  // HEVC seam fallback notes (Defekt A / H.265): surface in the progress
  // window and the log so affected seams are visible (spec decision 1).
  mSeamNotes = mSmartCut.seamNotes();
  for (const QString& note : mSeamNotes) {
    log->warningMsg(__FILE__, __LINE__, note);
    reportStep(note, 0);
  }

  // Adjust audio keepList to match actual video output ranges.
  // B-frame reorder delay can shift the display-order CutIn forward, causing
  // the video Smart Cut to output fewer frames than the cut list specifies.
  // Without adjustment, audio would be cut for the original (wider) range,
  // resulting in cumulative A/V drift across segments.
  QList<QPair<double, double>> keepList = mParams.keepList;
  QList<QPair<int, int>> actualRanges = mSmartCut.actualOutputFrameRanges();
  if (actualRanges.size() == keepList.size()) {
    for (int i = 0; i < keepList.size(); i++) {
      double origStart = keepList[i].first;
      // actualRanges[i].first is a decode-order AU index; the keepList is in
      // display space. Map AU -> display (identity for MPEG-2 / no-B streams)
      // so the comparison is space-consistent. After the display-order fix the
      // video starts exactly at the requested display cut-in, so this is
      // normally a no-op; the guard remains defensive.
      int actualStartDisplay = vStream->decodeToDisplayIndex(actualRanges[i].first);
      double newStart = actualStartDisplay / mParams.frameRate;
      if (qAbs(newStart - origStart) > 0.001) {
        log->infoMsg(__FILE__, __LINE__, QString("Audio segment %1: adjusting start %2 -> %3 (B-frame reorder shift: %4 frames)")
            .arg(i+1).arg(origStart, 0, 'f', 3).arg(newStart, 0, 'f', 3)
            .arg(actualStartDisplay - mParams.cutFrames[i].first));
        keepList[i].first = newStart;
      }
    }
  }

  // Cut audio tracks
  QStringList cutAudioFiles;
  const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
  // Cut all audio tracks against the (B-frame-adjusted) video keepList
  // (consolidated onto TTAVData::cutAudioTracks).
  if (mpAVItem->audioCount() > 0)
    reportStage(StatusReportArgs::StageAudio);
  mpAVData->cutAudioTracks(mpAVItem, keepList, normalizeAcmod,
      [&](int i, const QString& ext) {
        return QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
            QFileInfo(mParams.sourceFile).completeBaseName()
              + QString("_audio%1.").arg(i+1) + ext).absoluteFilePath();
      },
      [&](int i, const QString& path, const QString& /*lang*/, bool ok) {
        // Register the path even when the cut did NOT succeed: an aborted
        // audio cut leaves a partial file behind, and abortCleanup() can only
        // remove what it knows about. mCreatedFiles is read on the abort path
        // only, so this changes nothing for a real failure.
        mCreatedFiles.append(path);
        if (ok) {
          cutAudioFiles.append(path);
          log->infoMsg(__FILE__, __LINE__, QString("Audio track %1 cut: %2").arg(i+1).arg(path));
        }
      },
      [&](int i) {
        reportStep(TTAVData::tr("Cutting audio track %1 of %2...")
                       .arg(i+1).arg(mpAVItem->audioCount()),
                   i * 100 / qMax(1, mpAVItem->audioCount()));
      },
      [&](int i, int percent) {
        int overall = (i * 100 + percent) / qMax(1, mpAVItem->audioCount());
        reportStep(TTAVData::tr("Cutting audio track %1 of %2...")
                       .arg(i+1).arg(mpAVItem->audioCount()), overall);
      },
      // Abort predicate (Task 3): polled inside cutAudioStream's read loop and
      // between tracks, so a cancel stops the audio phase at the next packet.
      [this] { return cancelRequested(); });
  abortIfRequested();

  // A missing track is a failure, not a footnote. cutAudioTracks() skips a
  // failed track silently (out-of-range index, missing stream, empty plan,
  // or cutAudioStream returning false) and reports that only through the ok
  // flag of the callback above - which is also why cutAudioFiles counts
  // exactly the successful tracks. Without this check the cut muxed an MKV
  // short of a track, reported success, and wrote a calibration factor on a
  // wrong work basis (measured: tools/diag/test_partial_track). Stopping
  // BEFORE the mux keeps the finished ES files - video and the successful
  // tracks - for a retry; a genuine error never cleans up (standing rule).
  // Per-track reasons are in the log as errorMsg entries.
  if (cutAudioFiles.size() < mpAVItem->audioCount()) {
    // Per-track reasons in user wording, not just in the log (final review
    // M14) - see TTAVData::audioCutFailureReasons().
    QString detail = TTAVData::tr("Only %1 of %2 audio track(s) could be cut - "
                                  "the finished streams were kept.")
                         .arg(cutAudioFiles.size()).arg(mpAVItem->audioCount());
    const QStringList reasons = mpAVData->audioCutFailureReasons();
    if (!reasons.isEmpty()) detail += "\n\n" + reasons.join("\n");
    else                    detail += "\n" + TTAVData::tr("See the log for the reason.");
    fail(TTAVData::tr("Cutting failed"), detail);
    return;
  }

  // Collect audio languages from data model
  QStringList cutAudioLanguages;
  for (int i = 0; i < mpAVItem->audioCount(); i++) {
    cutAudioLanguages.append(mpAVItem->audioListItemAt(i).getLanguage());
  }

  // Cut subtitle tracks against the same (B-frame-adjusted) keepList as
  // the audio (consolidated onto TTAVData::cutSubtitleTracks)
  QStringList cutSubtitleFiles;
  QStringList cutSubtitleLanguages;
  mpAVData->cutSubtitleTracks(mpAVItem, keepList,
      [&](int i) {
        return QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
            QFileInfo(mParams.sourceFile).completeBaseName()
              + QString("_sub%1.srt").arg(i+1)).absoluteFilePath();
      },
      [&](int i, const QString& path, const QString& lang, bool ok) {
        // Registered unconditionally, for the same reason as the audio files
        // above (a partial .srt of an interrupted write must be cleaned up).
        mCreatedFiles.append(path);
        if (ok) {
          cutSubtitleFiles.append(path);
          cutSubtitleLanguages.append(lang);
          log->infoMsg(__FILE__, __LINE__,
              QString("Subtitle track %1 cut: %2").arg(i+1).arg(path));
        }
      });
  // Last poll before the mux phase. The subtitle cut has no abort predicate of
  // its own: it writes an in-memory header list to a text file and finishes in
  // milliseconds even for a full recording.
  abortIfRequested();

  // Mux video and audio into final MKV
  log->infoMsg(__FILE__, __LINE__, QString("tempVideoFile: %1 (%2 bytes)")
      .arg(mParams.tempVideoFile).arg(QFileInfo(mParams.tempVideoFile).size()));
  for (int i = 0; i < cutAudioFiles.size(); i++) {
    log->infoMsg(__FILE__, __LINE__, QString("cutAudioFile[%1]: %2 (%3 bytes)")
        .arg(i).arg(cutAudioFiles[i]).arg(QFileInfo(cutAudioFiles[i]).size()));
  }
  reportStage(StatusReportArgs::StageMux);
  reportStep(TTAVData::tr("Muxing video and audio..."), 0);
  // Also a member (see the header) so onUserAbort() can reach it.
  // Direct connection for the same reason as the Smart Cut one above: keep the
  // forwarding on this thread so the processEvents() guard actually bites.
  connect(&mMkvProvider, &TTMkvMergeProvider::progressChanged, this,
      [this](int percent, const QString& msg) { reportStep(msg, percent); },
      Qt::DirectConnection);

  // Calculate frame duration in nanoseconds (e.g., "0:20000000ns" for 50fps)
  int frameDurationNs = (int)(1000000000.0 / mParams.frameRate);
  mMkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
  mMkvProvider.setIsPAFF(mParams.isPAFF, mParams.paffLog2MaxFrameNum);
  AVCodecID codecId = mParams.isH265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
  mMkvProvider.setVideoCodecId(codecId);
  // Display-PTS: SmartCut-supplied output order (empty = legacy linear PTS)
  mMkvProvider.setVideoDisplayOrder(mSmartCut.outputDisplayOrder());

  // Apply A/V sync offset if present
  if (mParams.avOffsetMs != 0) {
    mMkvProvider.setAudioSyncOffset(mParams.avOffsetMs);
  }

  // Note: per-track audio delay is already baked into each track's cut audio
  // file via audioKeepList above. Do NOT apply it again here via setAudioDelays()
  // — that would double-apply the delay.

  mMkvProvider.setAudioLanguages(cutAudioLanguages);
  mMkvProvider.setSubtitleLanguages(cutSubtitleLanguages);

  // Add chapters in first mux pass (no second container remux needed)
  QString chapterFile;
  if (TTSettings::instance()->workingMkvCreateChapters() && TTSettings::instance()->workingMkvChapterInterval() > 0 &&
      mParams.finalOutput.endsWith(".mkv", Qt::CaseInsensitive)) {

    qint64 totalDurationMs = mParams.totalDurationMs;

    log->infoMsg(__FILE__, __LINE__, QString("Total cut duration: %1 ms").arg(totalDurationMs));

    if (totalDurationMs > 0) {
      mMkvProvider.setTotalDurationMs(totalDurationMs);
      chapterFile = TTMkvMergeProvider::generateChapterFile(
          totalDurationMs, TTSettings::instance()->workingMkvChapterInterval(), TTSettings::instance()->cutDirPath());
      if (!chapterFile.isEmpty()) {
        mMkvProvider.setChapterFile(chapterFile);
        mCreatedFiles.append(chapterFile);
      }
    }
  }

  mCreatedFiles.append(mParams.finalOutput);
  bool success = mMkvProvider.mux(mParams.finalOutput, mParams.tempVideoFile,
                                  cutAudioFiles, cutSubtitleFiles);

  if (success) {
    log->infoMsg(__FILE__, __LINE__, QString("Muxing complete: %1").arg(mParams.finalOutput));
    // Delete cut elementary streams only if the option says so — same
    // semantics as the MPEG-2 path (workingMuxDeleteES)
    if (TTSettings::instance()->workingMuxDeleteES()) {
      QFile::remove(mParams.tempVideoFile);
      for (const QString& f : cutAudioFiles) {
        QFile::remove(f);
      }
      for (const QString& f : cutSubtitleFiles) {
        QFile::remove(f);
      }
    }
  } else {
    // Same false return for a cancel as for a real mux failure.
    if (mMkvProvider.wasAborted() || cancelRequested()) abortNow();
    log->errorMsg(__FILE__, __LINE__, QString("Muxing failed: %1").arg(mMkvProvider.lastError()));
    if (!chapterFile.isEmpty()) QFile::remove(chapterFile);
    fail(TTAVData::tr("Muxing failed"),
         TTAVData::tr("Muxing failed: %1").arg(mMkvProvider.lastError()));
    return;
  }

  // Clean up chapter file
  if (!chapterFile.isEmpty()) QFile::remove(chapterFile);

  // No poll point after a SUCCESSFUL mux, deliberately: at this point the cut
  // is complete and there is nothing left to cancel. A cancel that arrives in
  // the microseconds between the mux returning and this line would otherwise
  // delete a finished result - the run reports its regular Exit instead.
  mExitMessage = TTAVData::tr("H.264/H.265 cutting complete");
}
