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
    TTCutTreeView(QWidget* parent=0);

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
    void onEditCutOut(const TTCutItem& item, int cutOut);
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
    void cutOutUpdated(const TTCutItem& item);
    void gotoCutIn(int index);
    void gotoCutOut(int index);
    void refreshDisplay();
    void previewCut(TTCutList* cutList, bool skipFirst = false, bool skipLast = false);
    void audioVideoCut(bool, TTCutList* cutList);

  private:
  	QTreeWidgetItem* findItem(const TTCutItem& cutItem);
    TTCutList* cutListFromSelection(bool ignoreSelection=false);
    void createActions();
    void updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item);

  public:
    // Re-evaluate the burst icons (column 5) of all entries — call after the
    // burst filter setting changed so the list matches without cut edits.
    void refreshBurstIcons();

  private:
    void updateAcmodIcon(QTreeWidgetItem* treeItem, const TTCutItem& item);

  private:
    TTAVData*        mAVData;
    int              editItemIndex;
    bool             allowSelectionChanged;
    QTreeWidgetItem* currentEditItem;
    QAction*         itemUpAction;
    QAction*         itemDownAction;
    QAction*         itemDeleteAction;
    QAction*         itemEditAction;
    QAction*         gotoCutInAction;
    QAction*         gotoCutOutAction;
    QAction*         itemPreviewAction;
    QAction*         itemCutAction;
    QAction*         itemDuplicateAction;
};
#endif
