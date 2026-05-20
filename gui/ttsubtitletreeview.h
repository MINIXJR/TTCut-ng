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

#include "../common/ttcut.h"

class TTAVData;
class TTAVItem;
class TTSubtitleItem;

class QMenu;
class QAction;
class QComboBox;

class TTSubtitleTreeView : public QWidget, Ui::TTSubtitleFileListWidget
{
  Q_OBJECT

  public:
    TTSubtitleTreeView(QWidget* parent=0);

    void setTitle (const QString& title);
    void setControlEnabled(bool enabled);
    void clear();

  signals:
    void openFile();
    void removeItem(int index);
    void swapItems(int oldIndex, int newIndex);
    void languageChanged(int index, const QString& language);

  public slots:
    void onAVDataChanged(const TTAVItem* avData);
    void onItemUp();
    void onItemDown();
    void onRemoveItem();
    void onItemRemoved(int index);
    void onClearList();
    void onContextMenuRequest(const QPoint& point);
    void onAppendItem(const TTSubtitleItem& item);
    void onSwapItems(int oldIndex, int newIndex);
    void onReloadList(const TTAVItem* avData);

  private:
    void createActions();
    QComboBox* createLanguageCombo(const QString& currentLang);

  private:
    const TTAVItem* mpAVItem;
    QMenu*        contextMenu;
    QAction*      itemUpAction;
    QAction*      itemDownAction;
    QAction*      itemDeleteAction;
    QAction*      itemNewAction;
};
#endif
