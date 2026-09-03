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

#include <QMutex>

class TTAVData;
class TTCutList;
class TTCutVideoTask;
class TTESSmartCut;

//! Runable task for creating cut preview clips
class TTCutPreviewTask : public TTThreadTask
{
	Q_OBJECT

	public:
		TTCutPreviewTask(TTAVData* avData, TTCutList* cutList);
		~TTCutPreviewTask() override;

		static QString createPreviewFileName(int index, QString extension);

		//! Reason the preview aborted, or empty for a clean run / plain user cancel.
		//! Set only for real errors (e.g. an un-cuttable damaged stream) so the
		//! abort handler can decide whether to inform the user with a dialog.
		QString errorMessage() const { return mErrorMessage; }

	protected:
    void cleanUp();
    void operation();

  public slots:
		void onUserAbort();

  signals:
    void finished(TTCutList* cutList);
    void audioDriftCalculated(const QList<float>& driftsMs);

	private:
		static TTCutList* createPreviewCutList(TTCutList* cutList);
		void createH264PreviewClip(TTCutList* cutList, const QString& outputFile,
		                           TTESSmartCut* sharedSmartCut = nullptr);
		void removePreviewClipFiles(int clipIndex, const QString& videoExt,
		                            TTCutList* clipCutList, const QString& outputFile);

	private:
		TTAVData*          mpAVData;
		TTCutList*         mpCutList;
		TTCutList*         mpPreviewCutList;
		TTCutVideoTask*    cutVideoTask;
		QString            mErrorMessage;

		//! Guards mpActiveSmartCut against the worker thread creating,
		//! reassigning or destroying its Smart Cut engine while onUserAbort()
		//! (GUI thread) reaches for it. mpActiveSmartCut is the engine currently
		//! doing work (shared instance across clips, or a clip-local one), or
		//! nullptr between clips / for non-H.26x previews. Set/cleared by the
		//! worker under mSmartCutMutex, always BEFORE any delete of the pointee -
		//! never after - so onUserAbort() can never see a dangling pointer.
		QMutex             mSmartCutMutex;
		TTESSmartCut*      mpActiveSmartCut = nullptr;
};


#endif
