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
// TTOPENVIDEOTASK
// ----------------------------------------------------------------------------

#ifndef TTOPENVIDEOTASK_H
#define TTOPENVIDEOTASK_H

#include <QString>
#include <QFileInfo>

#include "../common/ttthreadtask.h"

class TTVideoType;
class TTVideoStream;
class TTAVItem;

//! Runable task for opening video streams
class TTOpenVideoTask : public TTThreadTask
{
	Q_OBJECT

	public:
		TTOpenVideoTask(TTAVItem* avItem, QString fileName, int order);

  protected:
    void operation();
    void cleanUp();

	public slots:
		void onUserAbort();

  signals:
    void finished(TTAVItem*, TTVideoStream*, int, const QString& demuxedAudio);

	private:
    TTAVItem*      mpAVItem;          /**<AV Data item              */
    int            mOrder;            /**<AV item order in list     */
    QString        mFileName;         /**<Stream file name          */
    QString        mOriginalFileName; /**<Original file before demux*/
    QString        mDemuxedAudio;     /**<Demuxed audio file path   */
    TTVideoType*   mpVideoType;       /**<Stream video type         */
    TTVideoStream* mpVideoStream;     /**<Stream object reference   */
};

#endif
