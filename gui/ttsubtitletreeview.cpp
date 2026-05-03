/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: Minei3oat (c) 2019 / github.com/Minei3oat                     */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2019                                                      */
/* FILE     : ttsubtitletreeview.cpp                                          */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : Minei3oat                                       DATE: 12/30/2019 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTSUBTITLETREEVIEW
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/


#include "ttsubtitletreeview.h"

#include "data/ttsubtitlelist.h"
#include "data/ttavlist.h"
#include "avstream/ttavstream.h"

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

