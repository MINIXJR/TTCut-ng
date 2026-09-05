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

#include "tttracktreeview.h"

class TTAVData;
class TTAVItem;

class QMenu;
class QAction;

class TTVideoTreeView : public TTTrackTreeView, Ui::TTVideoFileListWidget
{
  Q_OBJECT

  public:
    explicit TTVideoTreeView(QWidget* parent=0);
    void setAVData(TTAVData* avData);

  signals:
    void selectionChanged(int index);

  public slots:
    void onItemSelectionChanged();
    //! Up/down/remove act only once a TTAVData is attached; remove also
    //! suppresses the selection-change round trip until onItemRemoved().
    void onItemUp() override;
    void onItemDown() override;
    void onRemoveItem() override;
    void onItemRemoved(int index) override;
    void onItemsSwapped(int oldIndex, int newIndex);
    void onAppendItem(const TTAVItem& item);
    void onReloadList();

  private:
    TTAVData* mAVData;
    bool      allowSelectionChanged;
};

#endif
