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
// TTCUTPREVIEW
// ----------------------------------------------------------------------------

#ifndef TTCUTPREVIEW_H
#define TTCUTPREVIEW_H

#include "ui_previewwidget.h"

#include <QThread>
#include <QProcess>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QLabel>

#include "../common/ttcut.h"
#include "../data/ttcutlist.h"

class TTAVData;
class QProgressDialog;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QSpacerItem;
class QComboBox;
class QPushButton;
class QFrame;
class TTVideoPlayer;

/* /////////////////////////////////////////////////////////////////////////////
 * Class TTCutPreview
 */
class TTCutPreview: public QDialog, Ui::TTPreviewWidget
{
Q_OBJECT

public:
	TTCutPreview(QWidget* parent = 0, int prevW = 640, int prevH = 480);
	~TTCutPreview();

	void resizeEvent(QResizeEvent* event);
	void initPreview(TTCutList* previewCutList, TTCutList* originalCutList, TTAVData* avData = nullptr, bool skipFirst = false, bool skipLast = false);
	void createPreview();

protected:
	void closeEvent(QCloseEvent* event);
	void cleanUp();

protected slots:
  void onOptimalSizeChanged();
  void onPlayerPlaying();
	void onPlayerFinished();
	void onCutSelectionChanged(int iCut);
	void onPlayPreview();
	void onExitPreview();
	void onPrevCut();
	void onNextCut();
	void onBurstShift();

private:
  TTVideoPlayer* videoPlayer;
	int            previewWidth;
	int            previewHeight;
	QString        current_video_file;
	QString        current_audio_file;

    // Burst warning
    QLabel*      lblBurstWarning;
    QPushButton* pbBurstShift;
    TTCutList*   mpCutList;
    TTCutList*   mpOriginalCutList;
    TTAVData*    mpAVData;
    int          mBurstSegmentIdx;
    int          mClipOffset;
    bool         mBurstIsCutOut;

    void checkBurstForCurrentCut(int iCut);
    void regeneratePreviewClip(int iCut);
    void regenerateMpeg2PreviewClip(int fileIndex, TTCutList* tmpCutList, QProgressDialog* progress);
    void regenerateSmartCutPreviewClip(int fileIndex, TTCutList* tmpCutList, QProgressDialog* progress);
};

#endif // TTCUTPREVIEW_H
