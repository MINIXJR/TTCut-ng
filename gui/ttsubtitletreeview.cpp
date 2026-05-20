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
// TTSUBTITLETREEVIEW
// ----------------------------------------------------------------------------


#include "ttsubtitletreeview.h"

#include "../data/ttsubtitlelist.h"
#include "../data/ttavlist.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * Construct a new TTSubtitleFileList widget.
 */
TTSubtitleTreeView::TTSubtitleTreeView(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  mpAVItem = 0;

  subtitleListView->setRootIsDecorated(false);
  QHeaderView* header = subtitleListView->header();
  header->resizeSection(0, 320);
  header->resizeSection(1, 220);
  header->resizeSection(2, 140);
  header->resizeSection(3, 100);

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  pbSubtitleFileOpen->setIcon(QIcon::fromTheme("document-open", style->standardIcon(QStyle::SP_DialogOpenButton)));
  pbSubtitleEntryUp->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  pbSubtitleEntryDown->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  pbSubtitleEntryDelete->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));

  createActions();

  // signal and slot connections
  connect(pbSubtitleFileOpen,    &QPushButton::clicked, this, &TTSubtitleTreeView::openFile);
  connect(pbSubtitleEntryUp,     &QPushButton::clicked, this, &TTSubtitleTreeView::onItemUp);
  connect(pbSubtitleEntryDown,   &QPushButton::clicked, this, &TTSubtitleTreeView::onItemDown);
  connect(pbSubtitleEntryDelete, &QPushButton::clicked, this, &TTSubtitleTreeView::onRemoveItem);
  connect(subtitleListView,      &QTreeWidget::customContextMenuRequested, this, &TTSubtitleTreeView::onContextMenuRequest);
}

//! Set the group box title string. This method is needed by designer.
void TTSubtitleTreeView::setTitle (const QString&)
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * setAVData
 */
void TTSubtitleTreeView::onAVDataChanged(const TTAVItem* avData)
{
  if (avData == 0) {
    mpAVItem = 0;
    clear();
    return;
  }

  if (mpAVItem != 0) {
    disconnect(this,     &TTSubtitleTreeView::removeItem,       mpAVItem, &TTAVItem::onRemoveSubtitleItem);
    disconnect(this,     &TTSubtitleTreeView::swapItems,        mpAVItem, &TTAVItem::onSwapSubtitleItems);
    disconnect(this,     &TTSubtitleTreeView::languageChanged,  mpAVItem, &TTAVItem::onSubtitleLanguageChanged);

    disconnect(mpAVItem, &TTAVItem::subtitleItemAppended,        this, &TTSubtitleTreeView::onAppendItem);
    disconnect(mpAVItem, qOverload<int>(&TTAVItem::subtitleItemRemoved), this, &TTSubtitleTreeView::onItemRemoved);
    disconnect(mpAVItem, &TTAVItem::subtitleItemsSwapped,        this, &TTSubtitleTreeView::onSwapItems);
  }

  mpAVItem = avData;

  connect(mpAVItem, &TTAVItem::subtitleItemAppended,        this, &TTSubtitleTreeView::onAppendItem);
  connect(mpAVItem, qOverload<int>(&TTAVItem::subtitleItemRemoved), this, &TTSubtitleTreeView::onItemRemoved);
  connect(mpAVItem, &TTAVItem::subtitleItemsSwapped,        this, &TTSubtitleTreeView::onSwapItems);

  connect(this,     &TTSubtitleTreeView::removeItem,        mpAVItem, &TTAVItem::onRemoveSubtitleItem);
  connect(this,     &TTSubtitleTreeView::swapItems,         mpAVItem, &TTAVItem::onSwapSubtitleItems);
  connect(this,     &TTSubtitleTreeView::languageChanged,   mpAVItem, &TTAVItem::onSubtitleLanguageChanged);

  onReloadList(mpAVItem);
}

void TTSubtitleTreeView::clear()
{
  subtitleListView->clear();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Enable or disable the widget
 */
void TTSubtitleTreeView::setControlEnabled( bool enabled )
{
  pbSubtitleFileOpen->setEnabled(enabled);
  pbSubtitleEntryUp->setEnabled(enabled);
  pbSubtitleEntryDelete->setEnabled(enabled);
  pbSubtitleEntryDown->setEnabled(enabled);
  subtitleListView->setEnabled(enabled);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onClearList
 */
void TTSubtitleTreeView::onClearList()
{
  subtitleListView->clear();
}

/* /////////////////////////////////////////////////////////////////////////////
 * onAppendItem
 */
void TTSubtitleTreeView::onAppendItem(const TTSubtitleItem& item)
{
  QTreeWidgetItem* treeItem = new QTreeWidgetItem(subtitleListView);

  treeItem->setText(0, item.getFileName());
  treeItem->setText(1, item.getLength());
  treeItem->setText(2, item.getDelay());

  QComboBox* combo = createLanguageCombo(item.getLanguage());
  subtitleListView->setItemWidget(treeItem, 3, combo);
}

/* //////////////////////////////////////////////////////////////////////////////
 * Swap two items in list
 */
void TTSubtitleTreeView::onSwapItems(int, int)
{
  // Rebuild entire list because QComboBox widgets are destroyed on takeTopLevelItem
  if (mpAVItem != 0) {
    onReloadList(mpAVItem);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item up button
 */
void TTSubtitleTreeView::onItemUp()
{
  if (subtitleListView->currentItem() == 0)  return;

  int index = subtitleListView->indexOfTopLevelItem(subtitleListView->currentItem());

  if (index <= 0) return;

  emit swapItems(index, index-1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item down button
 */
void TTSubtitleTreeView::onItemDown()
{
  if (subtitleListView->currentItem() == 0)  return;

  int index = subtitleListView->indexOfTopLevelItem(subtitleListView->currentItem());

  if (index >= subtitleListView->topLevelItemCount()-1) return;

  emit swapItems(index, index+1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for remove item button
 */
void TTSubtitleTreeView::onRemoveItem()
{
  if (subtitleListView->currentItem() == 0) return;

  int index = subtitleListView->indexOfTopLevelItem(subtitleListView->currentItem());

  emit removeItem(index);
}

/* //////////////////////////////////////////////////////////////////////////////
 *
 */
void TTSubtitleTreeView::onItemRemoved(int index)
{
  delete subtitleListView->takeTopLevelItem(index);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onContextMenuRequest
 * User requested a context menu
 */
void TTSubtitleTreeView::onContextMenuRequest(const QPoint& point)
{
  if (subtitleListView->currentItem() == 0)
    return;

  QMenu contextMenu(this);
  contextMenu.addAction(itemNewAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemUpAction);
  contextMenu.addAction(itemDeleteAction);
  contextMenu.addAction(itemDownAction);

  contextMenu.exec(subtitleListView->mapToGlobal(point));
}

void TTSubtitleTreeView::onReloadList(const TTAVItem* avData)
{
  onClearList();

  for (int i = 0; i < avData->subtitleCount(); i++) {
    onAppendItem(avData->subtitleListItemAt(i));
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * createAction
 * Create the actions used by the context menu.
 */
void TTSubtitleTreeView::createActions()
{
  QStyle* style = QApplication::style();

  itemNewAction = new QAction(tr("&Insert subtitlefile"), this);
  itemNewAction->setIcon(QIcon::fromTheme("document-open", style->standardIcon(QStyle::SP_DialogOpenButton)));
  itemNewAction->setStatusTip(tr("Open a new subtitlefile and insert to list"));
  connect(itemNewAction, &QAction::triggered, this, &TTSubtitleTreeView::openFile);

  itemUpAction = new QAction(tr("Move &up"), this);
  itemUpAction->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  itemUpAction->setStatusTip(tr("Move selected subtitlefile one position upward"));
  connect(itemUpAction, &QAction::triggered, this, &TTSubtitleTreeView::onItemUp);

  itemDeleteAction = new QAction(tr("&Delete"), this);
  itemDeleteAction->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));
  itemDeleteAction->setStatusTip(tr("Remove selected subtitlefile from list"));
  connect(itemDeleteAction, &QAction::triggered, this, &TTSubtitleTreeView::onRemoveItem);

  itemDownAction = new QAction(tr("Move d&own"), this);
  itemDownAction->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  itemDownAction->setStatusTip(tr("Move selected subtitlefile one position downward"));
  connect(itemDownAction, &QAction::triggered, this, &TTSubtitleTreeView::onItemDown);
}

QComboBox* TTSubtitleTreeView::createLanguageCombo(const QString& currentLang)
{
  QComboBox* combo = new QComboBox();
  TTCut::populateLanguageCombo(combo, currentLang);

  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, combo](int idx) {
    for (int row = 0; row < subtitleListView->topLevelItemCount(); row++) {
      QTreeWidgetItem* item = subtitleListView->topLevelItem(row);
      if (subtitleListView->itemWidget(item, 3) == combo) {
        emit languageChanged(row, combo->itemData(idx).toString());
        break;
      }
    }
  });

  return combo;
}

