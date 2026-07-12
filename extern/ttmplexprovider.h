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
#include "../data/ttmuxlistdata.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QHash>

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

  private:
    QString     createOutputFilePath(const QString& videoFilePath);
    QStringList createMplexArguments(const QString& videoFilePath, const QStringList& audioFilePaths, bool escapeFileNames);
    void        deleteElementaryStreams(const QString& videoFilePath, const QStringList& audioFilePaths);

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
};

#endif //TTMPLEXPROVIDER
