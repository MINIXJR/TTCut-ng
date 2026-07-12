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
// TTAUDIOFILELIST
// ----------------------------------------------------------------------------

#ifndef TTAUDIOFILELIST_H
#define TTAUDIOFILELIST_H

#include "ui_audiofilelistwidget.h"

#include "../common/ttcut.h"

class TTAVData;
class TTAVItem;
class TTAudioItem;

class QMenu;
class QAction;
class QComboBox;
class QSpinBox;

class TTAudioTreeView : public QWidget, Ui::TTAudioFileListWidget
{
  Q_OBJECT

  public:
    TTAudioTreeView(QWidget* parent=0);

    void clear();

  signals:
    void openFile();
    void removeItem(int index);
    void swapItems(int oldIndex, int newIndex);
    void languageChanged(int index, const QString& language);
    void delayChanged(int index, int delayMs);

  public slots:
    void onAVDataChanged(const TTAVItem* avData);
    void onItemUp();
    void onItemDown();
    void onRemoveItem();
    void onItemRemoved(int index);
    void onClearList();
    void onContextMenuRequest(const QPoint& point);
    void onAppendItem(const TTAudioItem& item);
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
