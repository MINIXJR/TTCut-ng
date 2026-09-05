/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : tttracktreeview.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : MINIXJR                                       DATE: 09/05/2026 */
/*----------------------------------------------------------------------------*/

#include "tttracktreeview.h"
#include "tttreeviewutil.h"
#include "../common/ttcut.h"

#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTreeWidget>

TTTrackTreeView::TTTrackTreeView(QWidget* parent)
  : QWidget(parent)
{
}

void TTTrackTreeView::bindListWidgets(QTreeWidget* list, QPushButton* open, QPushButton* up,
                                      QPushButton* down, QPushButton* del, const ActionTexts& texts)
{
  mpListView = list;
  mpListView->setRootIsDecorated(false);

  // Use theme icons with Qt standard icon fallback for cross-platform support
  open->setIcon(ttThemeIcon("document-open", QStyle::SP_DialogOpenButton));
  up->setIcon(ttThemeIcon("go-up", QStyle::SP_ArrowUp));
  down->setIcon(ttThemeIcon("go-down", QStyle::SP_ArrowDown));
  del->setIcon(ttThemeIcon("edit-delete", QStyle::SP_TrashIcon));

  // Actions of the context menu
  mpItemNewAction = ttMakeAction(this, texts.insertText, "document-open", QStyle::SP_DialogOpenButton, texts.insertTip);
  connect(mpItemNewAction, &QAction::triggered, this, &TTTrackTreeView::openFile);

  mpItemUpAction = ttMakeAction(this, tr("Move &up"), "go-up", QStyle::SP_ArrowUp, texts.upTip);
  connect(mpItemUpAction, &QAction::triggered, this, &TTTrackTreeView::onItemUp);

  mpItemDeleteAction = ttMakeAction(this, tr("&Delete"), "edit-delete", QStyle::SP_TrashIcon, texts.deleteTip);
  connect(mpItemDeleteAction, &QAction::triggered, this, &TTTrackTreeView::onRemoveItem);

  mpItemDownAction = ttMakeAction(this, tr("Move d&own"), "go-down", QStyle::SP_ArrowDown, texts.downTip);
  connect(mpItemDownAction, &QAction::triggered, this, &TTTrackTreeView::onItemDown);

  // signal and slot connections
  connect(open, &QPushButton::clicked, this, &TTTrackTreeView::openFile);
  connect(up,   &QPushButton::clicked, this, &TTTrackTreeView::onItemUp);
  connect(down, &QPushButton::clicked, this, &TTTrackTreeView::onItemDown);
  connect(del,  &QPushButton::clicked, this, &TTTrackTreeView::onRemoveItem);
  connect(mpListView, &QTreeWidget::customContextMenuRequested, this, &TTTrackTreeView::onContextMenuRequest);
}

int TTTrackTreeView::currentRow() const
{
  if (mpListView->currentItem() == nullptr) return -1;
  return mpListView->indexOfTopLevelItem(mpListView->currentItem());
}

void TTTrackTreeView::clear()
{
  mpListView->clear();
}

void TTTrackTreeView::onClearList()
{
  mpListView->clear();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item up button
 */
void TTTrackTreeView::onItemUp()
{
  int index = currentRow();
  if (index <= 0) return;

  emit swapItems(index, index-1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item down button
 */
void TTTrackTreeView::onItemDown()
{
  int index = currentRow();
  if (index < 0 || index >= mpListView->topLevelItemCount()-1) return;

  emit swapItems(index, index+1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for remove item button
 */
void TTTrackTreeView::onRemoveItem()
{
  int index = currentRow();
  if (index < 0) return;

  emit removeItem(index);
}

void TTTrackTreeView::onItemRemoved(int index)
{
  delete mpListView->takeTopLevelItem(index);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onContextMenuRequest
 * User requested a context menu
 */
void TTTrackTreeView::onContextMenuRequest(const QPoint& point)
{
  if (mpListView->currentItem() == nullptr)
    return;

  QMenu contextMenu(this);
  contextMenu.addAction(mpItemNewAction);
  contextMenu.addSeparator();
  contextMenu.addAction(mpItemUpAction);
  contextMenu.addAction(mpItemDeleteAction);
  contextMenu.addAction(mpItemDownAction);

  contextMenu.exec(mpListView->mapToGlobal(point));
}

void TTTrackTreeView::addDelaySpin(QTreeWidgetItem* item, int column, int delayMs, const QString& toolTip)
{
  QSpinBox* delaySpin = ttMakeDelaySpin(delayMs, toolTip);
  mpListView->setItemWidget(item, column, delaySpin);

  connect(delaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, column, delaySpin](int value) {
    int row = ttRowOfItemWidget(mpListView, column, delaySpin);
    if (row >= 0) emit delayChanged(row, value);
  });
}

void TTTrackTreeView::addLanguageCombo(QTreeWidgetItem* item, int column, const QString& currentLang)
{
  QComboBox* combo = new QComboBox();
  TTCut::populateLanguageCombo(combo, currentLang);

  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, column, combo](int idx) {
    int row = ttRowOfItemWidget(mpListView, column, combo);
    if (row >= 0) emit languageChanged(row, combo->itemData(idx).toString());
  });

  mpListView->setItemWidget(item, column, combo);
}
