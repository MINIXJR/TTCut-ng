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

#include <QCloseEvent>
#include <QLabel>

#include <optional>

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
class QWidget;
class TTMpvWrapper;

/* /////////////////////////////////////////////////////////////////////////////
 * Class TTCutPreview
 */
class TTCutPreview: public QDialog, Ui::TTPreviewWidget
{
Q_OBJECT

public:
	TTCutPreview(QWidget* parent = 0, int prevW = 640, int prevH = 480);
	~TTCutPreview();

	void initPreview(TTCutList* previewCutList, TTCutList* originalCutList, TTAVData* avData = nullptr, bool skipFirst = false, bool skipLast = false);
	void createPreview();

protected:
	void closeEvent(QCloseEvent* event);
	void showEvent(QShowEvent* event);
	void cleanUp();

protected slots:
  void onPlayerPlaying();
	void onPlayerFinished();
	void onPlayerError(const QString& message);
	void onCutSelectionChanged(int iCut);
	void onPlayPreview();
	void onExitPreview();
	void onPrevCut();
	void onNextCut();
	void onBurstShift();
	void onAspectJump();

private:
  TTMpvWrapper*  mPlayer;
	int            previewWidth;
	int            previewHeight;
	QString        current_video_file;

    // Burst warning
    QLabel*      lblBurstWarning;
    QPushButton* pbBurstShift;
    TTCutList*   mpCutList;
    TTCutList*   mpOriginalCutList;
    TTAVData*    mpAVData;
    int          mBurstSegmentIdx;
    int          mClipOffset;
    bool         mBurstIsCutOut;

    // Aspect change row (own grid row, hidden entirely when there is nothing
    // to report). mAspectTarget is the frame the jump moves the edge to.
    QLabel*      lblAspectWarning;
    QPushButton* pbAspectJump;
    int          mAspectSegmentIdx;
    bool         mAspectIsCutOut;
    int          mAspectTarget;

    //! False while the dialog loads its first clip, true once the user drives
    //! the selection. Decides whether a cut change starts playback: opening
    //! the dialog must not play, a deliberate cut change must.
    bool mAutoPlayOnSelect = false;

    //! True until showEvent() has loaded the first clip. The load cannot happen
    //! in initPreview(): TTCutMainWindow calls that before exec(), so the mpv
    //! render widget has never been painted and mpv has no render target yet.
    bool mInitialLoadPending = true;

    void checkBurstForCurrentCut(int iCut);
    void configureBurstShiftButton(bool isCutOut);
    void checkAspectForCurrentCut(int iCut);
    void updateHintRowSpace();
    void setBurstMessage(const QString& message, bool resolved);
    void setAspectMessage(const QString& message, bool resolved);
    static void applyHintMessage(QLabel* label, const QString& message, bool resolved);
    //! The cut behind a preview-list index (two entries per cut) in the copy
    //! of the original list; nullopt when out of range.
    std::optional<TTCutItem> originalCutItem(int segmentIdx) const;
    void updateRealCutItem(const TTCutItem& copyItem, bool isCutOut, int oldIdx, int newIdx);
    void applyEdgeMoveToLists(const TTCutItem& copyItem, int segmentIdx, bool isCutOut, int newIdx);
    void moveCutEdge(int segmentIdx, bool isCutOut, int oldIdx, int newIdx);
    //! Pin the output channel layout for the track this preview plays, if
    //! the user asked for it. Called before every load; the clip carries the
    //! source track's codec, so the source's type decides.
    void applyOutputChannels();
    void regeneratePreviewClip(int iCut);
};

#endif // TTCUTPREVIEW_H
