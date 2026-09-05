/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally (c) 2019 Minei3oat / github.com/Minei3oat                       */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTSUBTITLEFILELIST
// ----------------------------------------------------------------------------

#ifndef TTSUBTITLEFILELIST_H
#define TTSUBTITLEFILELIST_H

#include "ui_subtitlefilelistwidget.h"

#include "tttracktreeview.h"

class TTAVData;
class TTAVItem;
class TTSubtitleItem;

class QMenu;
class QAction;
class QComboBox;

class TTSubtitleTreeView : public TTTrackTreeView, Ui::TTSubtitleFileListWidget
{
  Q_OBJECT

  public:
    explicit TTSubtitleTreeView(QWidget* parent=0);

  public slots:
    void onAVDataChanged(const TTAVItem* avData);
    void onAppendItem(const TTSubtitleItem& item);
    void onSwapItems(int oldIndex, int newIndex);
    void onReloadList(const TTAVItem* avData);

  private:
    const TTAVItem* mpAVItem;
};
#endif
