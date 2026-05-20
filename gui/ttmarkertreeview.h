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
// TTMARKERTREEVIEW
// ----------------------------------------------------------------------------

#ifndef TTMARKERTREEVIEW_H
#define TTMARKERTREEVIEW_H

#include "ui_markerlistwidget.h"

#include "../common/ttcut.h"
#include "../data/ttavdata.h"

class TTAVItem;
class TTMarkerItem;

class QMenu;
class QAction;

class TTMarkerTreeView : public QWidget, Ui::TTMarkerListWidget
{
  Q_OBJECT

  public:
    TTMarkerTreeView(QWidget* parent=0);

    void controlEnabled(bool enabled);
    void setAVData(TTAVData* avData);
    void clear();

  signals:
    void removeItem(const TTMarkerItem& mItem);
    void itemOrderChanged(int oldIndex, int newIndex);
    void itemUpdated(const TTMarkerItem& mItem);
    void refreshDisplay();
    void swapItems(int oldIndex, int newIndex);
    void gotoMarker(const TTMarkerItem& mItem);

  public slots:
		void onActivateMarker();
    void onItemUp();
    void onItemDown();
    void onRemoveItem();
    void onItemRemoved(int index);
    void onClearList();
    void onContextMenuRequest(const QPoint& point);
    void onAppendItem(const TTMarkerItem& item);
    void onUpdateItem(const TTMarkerItem& mItem, const TTMarkerItem& uItem);
    void onSwapItems(int oldIndex, int newIndex);
    void onReloadList();

  private:
  	QTreeWidgetItem* findItem(const TTMarkerItem& cutItem);
    void createActions();

  private:
  	TTAVData* mpAVData;
    QMenu*        contextMenu;
    QAction*      itemUpAction;
    QAction*      itemDownAction;
    QAction*      itemDeleteAction;
};
#endif
