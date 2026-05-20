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
// TTVIDEOTREEVIEW
// ----------------------------------------------------------------------------

#ifndef TTVIDEOTREEVIEW_H
#define TTVIDEOTREEVIEW_H

#include "ui_videofilelistwidget.h"

#include "../common/ttcut.h"

class TTAVData;
class TTAVItem;

class QMenu;
class QAction;

class TTVideoTreeView : public QWidget, Ui::TTVideoFileListWidget
{
  Q_OBJECT

  public:
    TTVideoTreeView(QWidget* parent=0);

    void clear();
    void setTitle (const QString& title);
    void setAVData(TTAVData* avData);
    void setControlEnabled(bool enabled);

  signals:
    void openFile();
    void selectionChanged(int index);
    void removeItem(int index);
    void swapItems(int oldIndex, int newIndex);

  public slots:
    void onItemSelectionChanged();
    void onItemUp();
    void onItemDown();
    void onRemoveItem();
    void onItemRemoved(int index);
    void onItemsSwapped(int oldIndex, int newIndex);
    void onClearList();
    void onContextMenuRequest(const QPoint& point);
    void onAppendItem(const TTAVItem& item);
    void onReloadList();

  private:
    void createActions();

  private:
    TTAVData* mAVData;
    bool      allowSelectionChanged;
    QMenu*    contextMenu;
    QAction*  itemUpAction;
    QAction*  itemDownAction;
    QAction*  itemDeleteAction;
    QAction*  itemNewAction;
};

#endif
