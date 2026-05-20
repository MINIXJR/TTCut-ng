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
// TTOPENAUDIOTASK
// ----------------------------------------------------------------------------

#include "../common/ttthreadtask.h"

#include <QString>
#include <QFileInfo>

class TTAudioType;
class TTAudioStream;
class TTAVItem;

//! Runable task for opening audio streams
class TTOpenAudioTask : public TTThreadTask
{
	Q_OBJECT;

	public:
		TTOpenAudioTask(TTAVItem* avItem, QString filePath, int order);

  protected:
    void cleanUp();
    void operation();

	public slots:
		void onUserAbort();

	signals:
		void finished(TTAVItem*, TTAudioStream*, int);

	private:
    TTAVItem*      mpAVItem;
    int            mOrder;
		QString        mFilePath;
		TTAudioStream* mpAudioStream;
    TTAudioType*   mpAudioType;
};
