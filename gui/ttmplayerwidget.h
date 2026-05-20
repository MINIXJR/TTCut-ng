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
// TTMPLAYERWIDGET
// ----------------------------------------------------------------------------

#include <QObject>
#include <QProcess>

#include "../common/ttcut.h"
#include "../common/ttmessagelogger.h"
#include "ttvideoplayer.h"

#ifndef TTMPLAYERWIDGET_H
#define TTMPLAYERWIDGET_H

class TTMplayerWidget : public TTVideoPlayer
{
    Q_OBJECT
    Q_PROPERTY(bool getControlsVisible READ getControlsVisible WRITE setControlsVisible)

public:
    TTMplayerWidget(QWidget *parent);
    ~TTMplayerWidget();
    
    QSize sizeHint() const;

    bool getControlsVisible() const;
    void setControlsVisible(bool visible);

    void cleanUp();
    void load(QString value);
    void play();
    void stop();
    void setSubtitleFile(QString subtitleFile);
    void clearSubtitleFile();

protected:
    bool playMplayer(QString videoFile);
    bool stopMplayer();
    void connectSignals();

protected slots:
    void mplayerStarted();
    void readFromStdout();
    void exitMplayer(int e_code, QProcess::ExitStatus e_status);
    void errorMplayer(QProcess::ProcessError);
    void stateChangedMplayer(QProcess::ProcessState newState);

private:
    TTMessageLogger*   log;
    QProcess*          mplayerProc;
    QString            currentSubtitleFile;
};

#endif
