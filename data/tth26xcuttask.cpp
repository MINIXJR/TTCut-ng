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
#include "../avstream/ttcommon.h"

#include "ttavdata.h"
#include "ttavlist.h"
#include "ttaudiolist.h"

#include "../avstream/ttavstream.h"
#include "../avstream/ttavtypes.h"
#include "../common/ttexception.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttsettings.h"
#include "../common/ttstreamfiles.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

/**
 * H.26x final cut task
 */
TTH26xCutTask::TTH26xCutTask(TTAVData* avData, TTAVItem* avItem) :
                TTAbortableTask(avData, "H26xCutTask")
{
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
  requestCancel();
  mSmartCut.requestAbort();
  mMkvProvider.requestAbort();
  abort();   // TTThreadTask bookkeeping (mIsAborted; pool Canceled chain)
}

/**
 * Record a failure and let operation() return.
 *
 * The closing Exit bracket and cutFinished() are emitted by
 * TTAVData::onH26xCutFinished() on the GUI thread, from these two strings.
 */
void TTH26xCutTask::fail(const QString& exitText, const QString& errorText)
{
  mExitMessage = exitText;
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
  const TTVideoStream* vStream = mpAVItem->videoStream();

  // Initialize Smart Cut engine. The engine is a member (see the header):
  // onUserAbort() has to reach it from the GUI thread at any moment.
  // Its progress is forwarded on this thread (see forwardProgressOf); a
  // queued forwarding would NOT reorder anything against the closing Exit
  // either - both travel the same GUI event queue, first in first out.
  forwardProgressOf(&mSmartCut);

  if (!mSmartCut.initialize(mParams.sourceFile, mParams.frameRate)) {
    // A cancel during the ES parse comes back through the same false return as
    // a real parse failure. wasAborted() is a plain bool owned by this thread,
    // so reading it here is safe; the task's own flag catches the (tiny) window
    // in which the request arrived before initialize() cleared the engine's.
    abortIfEngineAborted(mSmartCut.wasAborted());
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
    abortIfEngineAborted(mSmartCut.wasAborted());
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
  mUnrewrittenFrames = mSmartCut.unrewrittenSourceFrames();
  mSourceFrameRate   = mSmartCut.frameRate();
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

  // The cut ES files sit next to each other in the cut directory, named after
  // the source: <base>_audio1.ac3, <base>_sub1.srt, ...
  const QString cutBase = QFileInfo(mParams.sourceFile).completeBaseName();
  auto cutFilePath = [&cutBase](const QString& suffix) {
    return QFileInfo(QDir(TTSettings::instance()->cutDirPath()),
                     cutBase + suffix).absoluteFilePath();
  };

  // Cut audio tracks
  QStringList cutAudioFiles;
  const bool normalizeAcmod = TTSettings::instance()->normalizeAcmod();
  // Cut all audio tracks against the (B-frame-adjusted) video keepList
  // (consolidated onto TTAVData::cutAudioTracks).
  if (mpAVItem->audioCount() > 0)
    reportStage(StatusReportArgs::StageAudio);
  mpAVData->cutAudioTracks(mpAVItem, keepList, normalizeAcmod,
      [&](int i, const QString& ext) {
        return cutFilePath(QString("_audio%1.").arg(i+1) + ext);
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
      audioTrackProgress(mpAVItem->audioCount()),
      // Abort predicate (Task 3): polled inside TTAudioCutter::cut's read loop and
      // between tracks, so a cancel stops the audio phase at the next packet.
      [this] { return cancelRequested(); });
  abortIfRequested();

  // A missing track is a failure, not a footnote. cutAudioTracks() skips a
  // failed track silently (out-of-range index, missing stream, empty plan,
  // or TTAudioCutter::cut returning false) and reports that only through the ok
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
      [&](int i) { return cutFilePath(QString("_sub%1.srt").arg(i+1)); },
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
  forwardProgressOf(&mMkvProvider);

  // Frame rate, PAFF, codec and A/V offset were fixed on the GUI thread;
  // the display order is the Smart Cut's output order (empty = linear PTS).
  TTMkvVideoOptions videoOptions = mParams.mux;
  videoOptions.displayOrder = mSmartCut.outputDisplayOrder();
  mMkvProvider.setVideoOptions(videoOptions);

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
    if (TTSettings::instance()->workingMuxDeleteES())
      ttRemoveElementaryStreams(mParams.tempVideoFile, cutAudioFiles, cutSubtitleFiles);
  } else {
    // Same false return for a cancel as for a real mux failure.
    abortIfEngineAborted(mMkvProvider.wasAborted());
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
