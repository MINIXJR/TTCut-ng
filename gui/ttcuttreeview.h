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
// TTCUTTREEVIEW
// ----------------------------------------------------------------------------

#ifndef TTCUTTREEVIEW_H
#define TTCUTTREEVIEW_H

#include "ui_cutlistwidget.h"

class TTCutItem;
class TTCutList;
class QMenu;
class QAction;
class TTAVData;

class TTCutTreeView : public QWidget, Ui::TTCutListWidget
{
  Q_OBJECT

  public:
    explicit TTCutTreeView(QWidget* parent=0);
    //! Frees the job list of the last preview or cut (see newJobCutList).
    ~TTCutTreeView() override;

    void controlEnabled(bool value);
    void setAVData(TTAVData* avData);
    void clear();

  public slots:
    void onAppendItem(const TTCutItem& item);
    void onRemoveItem(int index);
    void onUpdateItem(const TTCutItem& citem, const TTCutItem& uitem);
    void onEntryUp();
    void onEntryDown();
    void onEntryDelete();
    void onEntrySelected(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void onEntryEdit();
    void onGotoCutIn();
    void onGotoCutOut();
    void onEntryPreview();
    void onEntryCut();
    void onPreview();
    void onAVCut();
    void onAVSelCut();
    void onAudioCut();
    void onAudioSelCut();
    void onContextMenuRequest(const QPoint& point);
    void onEntryDuplicate();
    void onClearList();
    void onReloadList();
    void onAudioDriftUpdated(const QList<float>& driftsMs);

  signals:
    void removeItem(const TTCutItem& item);
    void itemOrderChanged(int oldIndex, int newIndex);
    void itemUpdated(const TTCutItem& item);
    void selectionChanged(const TTCutItem& item, int column);
    void entryEdit(const TTCutItem& item);
    void gotoCutIn(int index);
    void gotoCutOut(int index);
    void refreshDisplay();
    void previewCut(TTCutList* cutList, bool skipFirst = false, bool skipLast = false);
    void audioVideoCut(bool, TTCutList* cutList);

  private:
  	QTreeWidgetItem* findItem(const TTCutItem& cutItem);
    TTCutList* mpJobCutList = nullptr;
    TTCutList* cutListFromSelection(bool ignoreSelection=false);
    //! An empty job list for the next preview or cut, freeing the previous
    //! one. The receivers (main window, TTAVData, TTCutPreviewTask) only
    //! borrow the pointer, so the producer owns it; an operation always ends
    //! before the next one starts.
    TTCutList* newJobCutList();
    //! Position of the current row, which is also the position of its entry
    //! in TTAVData's cut list (row i <-> cutItemAt(i)). -1 when there is no
    //! model or no current row.
    int currentCutIndex() const;
    void createActions();

    // Column 5 ("hint"): three producers that only compute, one writer that
    // composes and sets the cell (updateHintColumn).
    struct HintCell { QString text; QString tip; };
    void updateHintColumn(QTreeWidgetItem* treeItem, const TTCutItem& item);
    HintCell burstHint(const TTCutItem& item) const;
    HintCell aspectHint(const TTCutItem& item) const;
    HintCell acmodHint(const TTCutItem& item) const;

  public:
    // Re-evaluate the hint column (column 5) of all entries — call after the
    // burst filter setting changed so the list matches without cut edits.
    // Covers burst, MPEG-2 aspect and AC3 format-change hints.
    void refreshHintIcons();

  private:

  private:
    TTAVData*        mpAVData;
    int              mEditItemIndex;
    bool             mAllowSelectionChanged;
    QAction*         mpItemUpAction;
    QAction*         mpItemDownAction;
    QAction*         mpItemDeleteAction;
    QAction*         mpItemEditAction;
    QAction*         mpGotoCutInAction;
    QAction*         mpGotoCutOutAction;
    QAction*         mpItemPreviewAction;
    QAction*         mpItemCutAction;
    QAction*         mpItemDuplicateAction;
};
#endif
