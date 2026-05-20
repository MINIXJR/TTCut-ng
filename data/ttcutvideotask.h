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
// TTCUTVIDEOTASK
// ----------------------------------------------------------------------------

#ifndef TTCUTVIDEOTASK_H
#define TTCUTVIDEOTASK_H

#include "../common/ttthreadtask.h"
#include "../data/ttmuxlistdata.h"

class TTAVData;
class TTFileBuffer;
class TTCutParameter;
class TTVideoStream;
class TTCutList;
class TTCutTask;

//! Runable task for cutting video streams
class TTCutVideoTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTCutVideoTask(TTAVData* avData);
    void init(QString tgtFilePath, TTCutList* cutList);
    TTMuxListDataItem* muxListItem();

  protected:
    void cleanUp();
    void operation();

  public slots:
    void onUserAbort();

  signals:
    void finished(const TTMuxListDataItem& muxListItem);

  private:
    TTAVData*         mpAVData;
    QString           mTgtFilePath;
    TTMuxListDataItem mMuxListItem;
    TTCutList*        mpCutList;
    TTFileBuffer*     mpTgtStream;
    TTCutParameter*   mpCutParams;
    TTVideoStream*    mpCutStream;
    TTCutTask*        mpCutTask;
};

class TTCutTask : public TTThreadTask
{
  Q_OBJECT

  public:
    TTCutTask();
    void init(TTVideoStream* cutStream, TTCutParameter* cutParameter);

  protected:
    void cleanUp();;
    void operation();

  public slots:
    void onUserAbort();

  private:  
    int             mCutIn;
    int             mCutOut;
    TTVideoStream*  mpCutStream;
    TTCutParameter* mpCutParameter;
};
#endif
