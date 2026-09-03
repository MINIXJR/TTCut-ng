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

#ifndef TTMPLEXPROVIDER_H
#define TTMPLEXPROVIDER_H

#include "../extern/imuxprovider.h"
#include "../common/istatusreporter.h"
#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"
#include "ttmuxlistdata.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QHash>

#include <atomic>

class TTMplexProvider : public IStatusReporter, public IMuxProvider
{
  Q_OBJECT

  public:
    TTMplexProvider(TTMuxListData* muxListData);
    ~TTMplexProvider();

    void writeMuxScript();
    void mplexPart(int index);

    // A/V sync offset in milliseconds (from .info file)
    // mplex uses --sync-offset (positive = video ahead of audio)
    void setAudioSyncOffset(int offsetMs) { mAudioSyncOffsetMs = offsetMs; }

    // Cooperative abort, same shape as TTMkvMergeProvider: requestAbort() is
    // thread-safe, the wait loop in mplexPart() polls the flag, and
    // wasAborted() lets the caller tell a cancel from a real failure.
    //
    // Unlike the other engines this one does not abort a loop of its own - it
    // stops the external mplex process (see stopProcess()). mplexPart() runs
    // synchronously on the GUI thread and pumps the event loop while it waits,
    // so the request arrives re-entrantly from TTAVData::onUserAbortRequest()
    // while mplexPart() is still on the stack.
    void requestAbort() { mAbortRequested.store(true, std::memory_order_relaxed); }
    bool wasAborted() const { return mWasAborted; }

    //! False when the external muxer ended with a non-zero exit code or did
    //! not end normally. Before this the exit code was discarded, so a failed
    //! multiplex still reported "Cut complete" to the user.
    bool    succeeded() const { return mSucceeded; }
    QString lastError() const { return mLastError; }

  private:
    QString     createOutputFilePath(const QString& videoFilePath);
    QStringList createMplexArguments(const QString& videoFilePath, const QStringList& audioFilePaths, bool escapeFileNames);
    void        deleteElementaryStreams(const QString& videoFilePath, const QStringList& audioFilePaths);
    //! Examine one line of mplex output for silent data loss (see the .cpp).
    void        inspectMplexLine(const QString& line);

    // Poll point for the cooperative abort (see requestAbort()).
    bool        checkAbort();
    // Stop the running mplex child process: SIGTERM first, SIGKILL as fallback.
    void        stopProcess();

    void procOutput();

  public slots:
    void onProcError(QProcess::ProcessError procError);
    void onProcReadOut();
    void onProcStarted();
    void onProcFinished(int exitCode, QProcess::ExitStatus);
    void onProcStateChanged(QProcess::ProcessState procState);

  private:
    TTMessageLogger*    log;
    TTMuxListData*      mpMuxList;
    int                 mCurrentMuxIndex;
    QProcess*           proc;
    QHash<QString, int> verbose;
    QHash<QString, int> format;
    int                 mAudioSyncOffsetMs;
    // Providers are created fresh per mux operation (TTAVData::onCutFinished),
    // so mAbortRequested needs no clearing point of its own; mWasAborted is an
    // output and is cleared at the top of mplexPart(). Same arrangement as
    // TTMkvMergeProvider.
    std::atomic<bool>   mAbortRequested { false };
    bool                mWasAborted     = false;
    bool                mSucceeded      = true;   //!< set by onProcFinished()
    QString             mLastError;
};

#endif //TTMPLEXPROVIDER
