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
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
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
  open->setIcon(ttThemedIcon("document-open", QStyle::SP_DialogOpenButton));
  up->setIcon(ttThemedIcon("go-up", QStyle::SP_ArrowUp));
  down->setIcon(ttThemedIcon("go-down", QStyle::SP_ArrowDown));
  del->setIcon(ttThemedIcon("edit-delete", QStyle::SP_TrashIcon));

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

void TTTrackTreeView::addLanguageCombo(QTreeWidgetItem* item, int column, const QString& currentLang,
                                       const QString& toolTip)
{
  QComboBox* combo = new QComboBox();
  TTCut::populateLanguageCombo(combo, currentLang);

  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, column, combo](int idx) {
    int row = ttRowOfItemWidget(mpListView, column, combo);
    if (row >= 0) emit languageChanged(row, combo->itemData(idx).toString());
  });

  combo->setToolTip(toolTip);
  mpListView->setItemWidget(item, column, combo);
}

void TTTrackTreeView::addNameEdit(QTreeWidgetItem* item, int column, const QString& name)
{
  QLineEdit* edit = new QLineEdit(name);
  edit->setPlaceholderText(tr("Track name in the MKV"));

  connect(edit, &QLineEdit::textEdited, this, [this, column, edit](const QString& text) {
    int row = ttRowOfItemWidget(mpListView, column, edit);
    if (row >= 0) emit nameChanged(row, text);
  });

  mpListView->setItemWidget(item, column, edit);
}

void TTTrackTreeView::addFlagCheck(QTreeWidgetItem* item, int column, TTTrackFlag flag, const TTTrackProps& props)
{
  QCheckBox* check = new QCheckBox();
  check->setChecked(props.flag(flag));
  TTTrackPrefill prefill = TTTrackPrefill::None;
  if (flag == TTTrackFlag::VisualImpaired)  prefill = props.visualImpairedPrefill;
  if (flag == TTTrackFlag::HearingImpaired) prefill = props.hearingImpairedPrefill;
  check->setToolTip(prefillToolTip(prefill));

  connect(check, &QCheckBox::toggled, this, [this, column, check, flag](bool on) {
    int row = ttRowOfItemWidget(mpListView, column, check);
    if (row >= 0) emit flagChanged(row, flag, on);
  });

  mpListView->setItemWidget(item, column, check);
}

QString TTTrackTreeView::prefillToolTip(TTTrackPrefill prefill)
{
  switch (prefill) {
    case TTTrackPrefill::LanguageMis:
      return tr("Prefilled from language code \"mis\"");
    case TTTrackPrefill::LanguageQks:
      return tr("Prefilled from language code \"qks\"");
    case TTTrackPrefill::RecordingHearingImpaired:
      return tr("Prefilled from the recording: the broadcaster flags this subtitle for the hard of hearing");
    case TTTrackPrefill::None:
      break;
  }
  return QString();
}

QString TTTrackTreeView::languagePrefillToolTip(const TTTrackProps& props)
{
  switch (props.languagePrefill) {
    case TTTrackPrefill::LanguageMis:
      return tr("Prefilled: the recording marks this track \"mis\" (audio description)");
    case TTTrackPrefill::LanguageQks:
      return tr("Prefilled: the recording marks this track \"qks\" (clear speech)");
    case TTTrackPrefill::RecordingHearingImpaired:
    case TTTrackPrefill::None:
      break;
  }
  return QString();
}
