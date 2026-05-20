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
// TTCUTPREVIEWTASK
// ----------------------------------------------------------------------------

#ifndef TTCUTPREVIEWTASK_H
#define TTCUTPREVIEWTASK_H

#include "../common/ttthreadtask.h"

class TTAVData;
class TTCutList;
class TTCutVideoTask;
class TTCutSubtitleTask;
class TTESSmartCut;

//! Runable task for creating cut preview clips
class TTCutPreviewTask : public TTThreadTask
{
	Q_OBJECT

	public:
		TTCutPreviewTask(TTAVData* avData, TTCutList* cutList);

		static QString createPreviewFileName(int index, QString extension);

	protected:
    void cleanUp();
    void operation();

  public slots:
		void onUserAbort();

  signals:
    void finished(TTCutList* cutList);
    void audioDriftCalculated(const QList<float>& driftsMs);

	private:
		TTCutList* createPreviewCutList(TTCutList* cutList);
		void createH264PreviewClip(TTCutList* cutList, const QString& outputFile,
		                           TTESSmartCut* sharedSmartCut = nullptr);

	private:
		TTAVData*          mpAVData;
		TTCutList*         mpCutList;
		TTCutList*         mpPreviewCutList;
		TTCutVideoTask*    cutVideoTask;
		TTCutSubtitleTask* cutSubtitleTask;
};


#endif
