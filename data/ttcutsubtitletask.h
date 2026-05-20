/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTSUBTITLETASK
// ----------------------------------------------------------------------------

#ifndef TTCUTSUBTITLETASK_H
#define TTCUTSUBTITLETASK_H

#include "../common/ttthreadtask.h"
#include "ttmuxlistdata.h"

class TTFileBuffer;
class TTCutParameter;
class TTSubtitleStream;
class TTCutList;

//! Runable task for cuttting subtitle streams
class TTCutSubtitleTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTCutSubtitleTask();
    void init(QString tgtFilePath, TTCutList* cutList, int srcSubtitleIndex, TTMuxListDataItem* muxListItem,
              const QString& language = QString());

  protected:
    void cleanUp();
    void operation();

  public slots:
    void onUserAbort();

  signals:
    void finished(QString filePath);

  private:
    QString            mTgtFilePath;
    TTCutList*         mpCutList;
    int                mSrcSubtitleIndex;
    TTFileBuffer*      mpTgtStream;
    TTCutParameter*    mpCutParams;
    TTSubtitleStream*  mpCutStream;
    TTMuxListDataItem* mMuxListItem;
    QString            mLanguage;
};

#endif
