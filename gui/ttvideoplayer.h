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
// TTMOVIEWIDGET
// ----------------------------------------------------------------------------

#ifndef TTVIDEOPLAYER_H
#define TTVIDEOPLAYER_H

#include <QObject>
#include <QWidget>

class TTVideoPlayer : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool  getControlsVisible READ getControlsVisible WRITE setControlsVisible)
    Q_PROPERTY(QSize getMovieSize       READ getMovieSize       WRITE setMovieSize)

public:
    TTVideoPlayer(QWidget *parent);
    virtual ~TTVideoPlayer() {};
 
    virtual QSize getMovieSize()                  const { return movieSize; };
    virtual void  setMovieSize(const QSize& size)       { movieSize = size; };
    virtual bool  getControlsVisible()            const { return areControlsVisible; };
    virtual void  setControlsVisible(bool visible)      { areControlsVisible = visible; };
    virtual bool  isPlaying()                     const { return mIsPlaying; }

    virtual void cleanUp()                         {};
    virtual void load(QString)                     {};
    virtual void play()                            {};
    virtual void stop()                            {};
    virtual void setSubtitleFile(QString)          {};
    virtual void clearSubtitleFile()               {};

signals:
    void optimalSizeChanged();
    void playerPlaying();
    void playerFinished();

protected:
    QString currentMovie;
    QSize   movieSize;
    bool    mIsPlaying;
    bool    areControlsVisible;
};

#endif
