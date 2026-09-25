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
// TTCUTREEVIEW
// ----------------------------------------------------------------------------

#include "../data/ttcutlist.h"
#include "ttthemedicon.h"
#include "tttreeviewutil.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavstream.h"
#include "../avstream/ttac3acmod.h"
#include "../avstream/ttaspectwindow.h"
#include "../avstream/ttmpeg2videoheader.h"

#include "ttcuttreeview.h"

#include <QApplication>
#include <QDebug>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QStringList>
#include <QStyle>

/*!
 * TTCutTreeView
 */
TTCutTreeView::TTCutTreeView(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  // set list view header (column) width
  videoCutList->setRootIsDecorated( false );
  QHeaderView* header = videoCutList->header();
  header->resizeSection(0, 200);
  header->resizeSection(1, 140);
  header->resizeSection(2, 140);
  header->resizeSection(3, 150);
  header->resizeSection(4,  80);
  videoCutList->headerItem()->setText(5, tr("Notice"));
  videoCutList->setColumnWidth(5, 120);

  mAllowSelectionChanged = true;
  mEditItemIndex = -1;

  // Use theme icons with Qt standard icon fallback for cross-platform support
  // Video editing industry color scheme
  pbEntryUp->setIcon(ttThemedIcon("go-up", QStyle::SP_ArrowUp));
  pbEntryDown->setIcon(ttThemedIcon("go-down", QStyle::SP_ArrowDown));
  pbEntryDelete->setIcon(ttThemedIcon("edit-delete", QStyle::SP_TrashIcon));
  pbEntryDelete->setStyleSheet("QPushButton { color: #cc4444; }");  // Red for destructive
  pbEntryCopy->setIcon(ttThemedIcon("edit-copy", QStyle::SP_FileDialogNewFolder));

  // Cut/Preview button accent palette.
  // Backgrounds picked for >=5:1 contrast with white text (WCAG AA) so the
  // labels stay readable in dark *and* light Qt themes — we fix both fg and
  // bg, the surrounding theme only colours neighbouring widgets.
  // "All-cuts" buttons get bold weight; "selection" buttons get regular.
  static const QString kPreviewBg = "#1f6868";  // teal
  static const QString kPreviewHv = "#2a8585";
  static const QString kAVBg      = "#2d7a2d";  // green
  static const QString kAVHv      = "#3d8a3d";
  static const QString kAudioBg   = "#2a5cad";  // blue
  static const QString kAudioHv   = "#3a6dbd";

  auto styleAccent = [](const QString& bg, const QString& hv, bool bold) -> QString {
    return QString("QPushButton { background-color: %1; color: white; %2 }"
                   "QPushButton:hover { background-color: %3; }")
                   .arg(bg, bold ? "font-weight: bold;" : "", hv);
  };

  // Theme icons (Breeze/Plasma etc.) tinted to white via alpha-mask paint.
  // Plasma's icon cache renders the same theme key slightly differently
  // between sibling buttons; replacing the colour with a flat fill makes
  // every instance pixel-identical and matches the white button label.
  auto tintIcon = [](const QIcon& src, const QColor& color, int size) -> QIcon {
    QPixmap pm = src.pixmap(size, size);
    if (pm.isNull()) return src;
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    return QIcon(pm);
  };

  const int kIconSize = 16;
  const QColor kIconColor = Qt::white;
  QIcon iconCut    = tintIcon(ttThemedIcon("edit-cut", QStyle::SP_DialogSaveButton),  kIconColor, kIconSize);
  QIcon iconCutAlt = tintIcon(ttThemedIcon("edit-cut", QStyle::SP_DialogApplyButton), kIconColor, kIconSize);
  QIcon iconAudio  = tintIcon(ttThemedIcon("audio-x-generic", QStyle::SP_DriveCDIcon),       kIconColor, kIconSize);

  pbPreview->setIcon(iconCutAlt);
  pbPreview->setStyleSheet(styleAccent(kPreviewBg, kPreviewHv, false));

  pbCutAudioVideo->setIcon(iconCut);
  pbCutAudioVideo->setStyleSheet(styleAccent(kAVBg, kAVHv, true));
  pbCutSelected->setIcon(iconCut);
  pbCutSelected->setStyleSheet(styleAccent(kAVBg, kAVHv, false));

  pbCutAudio->setIcon(iconAudio);
  pbCutAudio->setStyleSheet(styleAccent(kAudioBg, kAudioHv, true));
  pbCutAudioSelected->setIcon(iconAudio);
  pbCutAudioSelected->setStyleSheet(styleAccent(kAudioBg, kAudioHv, false));

  // actions for context menu
  createActions();

  // signal and slot connections
  connect(pbEntryUp,          &QPushButton::clicked, this, &TTCutTreeView::onEntryUp);
  connect(pbEntryDown,        &QPushButton::clicked, this, &TTCutTreeView::onEntryDown);
  connect(pbEntryDelete,      &QPushButton::clicked, this, &TTCutTreeView::onEntryDelete);
  connect(pbEntryCopy,        &QPushButton::clicked, this, &TTCutTreeView::onEntryDuplicate);
  connect(pbPreview,          &QPushButton::clicked, this, &TTCutTreeView::onPreview);
  connect(pbCutAudioVideo,    &QPushButton::clicked, this, &TTCutTreeView::onAVCut);
  connect(pbCutSelected,      &QPushButton::clicked, this, &TTCutTreeView::onAVSelCut);
  connect(pbCutAudio,         &QPushButton::clicked, this, &TTCutTreeView::onAudioCut);
  connect(pbCutAudioSelected, &QPushButton::clicked, this, &TTCutTreeView::onAudioSelCut);
  connect(videoCutList, &QTreeWidget::itemSelectionChanged,        this, &TTCutTreeView::onItemSelectionChanged);
  connect(videoCutList, &QTreeWidget::itemClicked,                 this, &TTCutTreeView::onEntrySelected);
  connect(videoCutList, &QTreeWidget::itemDoubleClicked,           this, &TTCutTreeView::onEntryEdit);
  connect(videoCutList, &QTreeWidget::customContextMenuRequested,  this, &TTCutTreeView::onContextMenuRequest);
}

TTCutTreeView::~TTCutTreeView()
{
  delete mpJobCutList;
}

/*!
 * enableControl
 */
void TTCutTreeView::controlEnabled(bool value)
{
	pbEntryUp->setEnabled(value);
	pbEntryDown->setEnabled(value);
	pbEntryDelete->setEnabled(value);
	pbEntryCopy->setEnabled(value);
	pbPreview->setEnabled(value);
	pbCutAudioVideo->setEnabled(value);
	pbCutSelected->setEnabled(value);
	pbCutAudio->setEnabled(value);
	videoCutList->setEnabled(value);
}

/*!
 * setAVData
 */
void TTCutTreeView::setAVData(TTAVData* avData)
{
  mpAVData = avData;

  connect(mpAVData, &TTAVData::cutItemAppended,    this, &TTCutTreeView::onAppendItem);
  connect(mpAVData, &TTAVData::cutItemRemoved,     this, &TTCutTreeView::onRemoveItem);
  connect(mpAVData, &TTAVData::cutItemUpdated,     this, &TTCutTreeView::onUpdateItem);
  connect(mpAVData, &TTAVData::cutDataReloaded,    this, &TTCutTreeView::onReloadList);
  connect(this,    &TTCutTreeView::removeItem,         mpAVData, &TTAVData::onRemoveCutItem);
  connect(this,    &TTCutTreeView::itemOrderChanged,   mpAVData, &TTAVData::onCutOrderChanged);
}

/*!
 * onClearList
 */
void TTCutTreeView::onClearList()
{
  videoCutList->clear();
}

/*!
 * onReloadList
 */
void TTCutTreeView::onReloadList()
{
	onClearList();

	for (int i = 0; i < mpAVData->cutCount(); i++) {
	    TTCutItem cutItem = mpAVData->cutItemAt(i);
	    onAppendItem(cutItem);
	  }
}

/*!
 * onAppendItem
 */
void TTCutTreeView::onAppendItem(const TTCutItem& item)
{
  QTreeWidgetItem* treeItem = new QTreeWidgetItem(videoCutList);

  treeItem->setText(0, item.fileName());
  treeItem->setText(1, item.cutInString());
  treeItem->setText(2, item.cutOutString());
  treeItem->setText(3, item.cutLengthString());

  treeItem->setText(4, QString::fromUtf8("\u2014"));  // em-dash "—"
  treeItem->setToolTip(4, tr("Audio drift is calculated during preview (first audio track)"));

  updateHintColumn(treeItem, item);

  //emit refreshDisplay();
}

/*!
 * onRemoveItem
 */
void TTCutTreeView::onRemoveItem(int index)
{
  delete videoCutList->takeTopLevelItem(index);
  mAllowSelectionChanged = true;
}

/*!
 * onUpdateItem
 */
void TTCutTreeView::onUpdateItem(const TTCutItem& cItem, const TTCutItem& uitem)
{
  QTreeWidgetItem* treeItem = (mEditItemIndex < 0)
      ? findItem(cItem) //videoCutList->currentItem()
      : videoCutList->topLevelItem(mEditItemIndex);

  if (treeItem == 0) {
    qDebug("TTCutTreeView::item not found!");
   	return;
  }

  treeItem->setText(0, uitem.fileName());
  treeItem->setText(1, uitem.cutInString());
  treeItem->setText(2, uitem.cutOutString());
  treeItem->setText(3, uitem.cutLengthString());

  updateHintColumn(treeItem, uitem);

  if (mEditItemIndex >= 0) {
    mEditItemIndex = -1;
    // Reset to default background (empty brush respects theme colors)
    treeItem->setBackground(0, QBrush());
    treeItem->setBackground(1, QBrush());
    treeItem->setBackground(2, QBrush());
    treeItem->setBackground(3, QBrush());
    treeItem->setBackground(4, QBrush());
    treeItem->setBackground(5, QBrush());
  }

  emit itemUpdated(cItem);
  emit refreshDisplay();
}

/*!
 * findItem
 */
QTreeWidgetItem* TTCutTreeView::findItem(const TTCutItem& cutItem)
{
	if (!mpAVData) return 0;

	// Match by model index via UUID (column 4 contains A/V offset, not UUID)
	int modelIdx = mpAVData->cutIndexOf(cutItem);
	if (modelIdx >= 0 && modelIdx < videoCutList->topLevelItemCount())
		return videoCutList->topLevelItem(modelIdx);

	return 0;
}

/*!
 * onEntryUp
 */
void TTCutTreeView::onEntryUp()
{
  if (videoCutList->topLevelItemCount() == 0)
    return;

  if (videoCutList->topLevelItem(0)->isSelected() && mEditItemIndex < 0)
    return;

  QTreeWidgetItem* pCurItem = videoCutList->currentItem();
  QList<QTreeWidgetItem*> SelectedItems = videoCutList->selectedItems();
  for ( int i=0; i<videoCutList->topLevelItemCount(); ++i ) {
    if ( videoCutList->topLevelItem(i)->isSelected() ) {
      QTreeWidgetItem* pTmpItem = videoCutList->takeTopLevelItem( i );
      videoCutList->insertTopLevelItem( i-1, pTmpItem );

      emit itemOrderChanged(i, i-1);
    }
  }

  // restore current item, and item selection
  videoCutList->setCurrentItem( pCurItem );
  for ( int i=0; i<SelectedItems.count(); ++i )
    SelectedItems[i]->setSelected( true );
}

/*!
 * onEntryDown
 */
void TTCutTreeView::onEntryDown()
{
  if (videoCutList->topLevelItemCount() == 0)
    return;

  if (videoCutList->topLevelItem(videoCutList->topLevelItemCount()-1)->isSelected() && mEditItemIndex < 0)
    return;

  QTreeWidgetItem* pCurItem = videoCutList->currentItem();
  QList<QTreeWidgetItem*> SelectedItems = videoCutList->selectedItems();
  for ( int i=videoCutList->topLevelItemCount()-1; i>=0; --i ) {
    if ( videoCutList->topLevelItem(i)->isSelected() ) {
      QTreeWidgetItem* pTmpItem = videoCutList->takeTopLevelItem( i );
      videoCutList->insertTopLevelItem( i+1, pTmpItem );

      emit itemOrderChanged(i, i+1);
    }
  }

  // restore current item, and item selection
  videoCutList->setCurrentItem( pCurItem );
  for ( int i=0; i<SelectedItems.count(); ++i )
    SelectedItems[i]->setSelected( true );
}

/*!
 * onEntryDelete
 */
void TTCutTreeView::onEntryDelete()
{
  if (videoCutList->topLevelItemCount() == 0) return;
  if (mEditItemIndex >= 0)  return;

  mAllowSelectionChanged = false;

  for (int i = videoCutList->selectedItems().count()-1; i >= 0; i--) {
    int index = videoCutList->indexOfTopLevelItem(videoCutList->selectedItems()[i]);
    TTCutItem cutItem = mpAVData->cutItemAt(index);
    emit removeItem(cutItem);
  }

  emit refreshDisplay();
}

/*!
 * onEntryDuplicate
 */
void TTCutTreeView::onEntryDuplicate()
{
  //if (videoCutList->topLevelItemCount() == 0) return;
  if (mpAVData == 0 || videoCutList->currentItem() == 0) return;
  if (mEditItemIndex >= 0) return;

   for ( int i=0; i<videoCutList->topLevelItemCount(); ++i ) {
    if ( videoCutList->topLevelItem(i)->isSelected() ) {
      TTCutItem cutItem = mpAVData->cutItemAt(i);
    	mpAVData->copyCutEntry(cutItem);
    }
  }
}

/*!
 * onEntrySelected
 */
void TTCutTreeView::onEntrySelected(QTreeWidgetItem*, int column)
{
  const int index = currentCutIndex();
  if (index < 0) return;

  emit selectionChanged(mpAVData->cutItemAt(index), column);
}

/*!
 * onItemSelectionChanged
 */
void TTCutTreeView::onItemSelectionChanged()
{
	if (!mAllowSelectionChanged) return;
  // Skip if triggered by mouse click — onEntrySelected handles that with correct column
  if (QApplication::mouseButtons() != Qt::NoButton) return;
  const int index = currentCutIndex();
  if (index < 0) return;

  emit selectionChanged(mpAVData->cutItemAt(index), 0);
}

/*!
 * onEntryEdit
 */
void TTCutTreeView::onEntryEdit()
{
  //if (videoCutList->topLevelItemCount() == 0) return;
  if (mpAVData == 0 || videoCutList->currentItem() == 0) return;

  QTreeWidgetItem* curItem = videoCutList->currentItem();
  int newIndex = videoCutList->indexOfTopLevelItem(curItem);

  // Re-entry: user starts editing a different row while another edit is
  // still active. Drop the previous row's edit highlight so we don't end
  // up with two visually highlighted rows and stale mEditItemIndex state.
  // Same-row re-entry is a no-op.
  if (mEditItemIndex >= 0 && mEditItemIndex != newIndex) {
    QTreeWidgetItem* prev = videoCutList->topLevelItem(mEditItemIndex);
    if (prev) {
      for (int c = 0; c < videoCutList->columnCount(); c++)
        prev->setBackground(c, QBrush());
    }
  } else if (mEditItemIndex == newIndex) {
    return;
  }

  mEditItemIndex = newIndex;

  // Use palette mid color for edit highlight (works with light and dark themes)
  QBrush editBrush = palette().mid();
  curItem->setBackground(0, editBrush);
  curItem->setBackground(1, editBrush);
  curItem->setBackground(2, editBrush);
  curItem->setBackground(3, editBrush);
  curItem->setBackground(4, editBrush);
  curItem->setBackground(5, editBrush);

  // deselect item
  curItem->setSelected(false);

  TTCutItem cutItem = mpAVData->cutItemAt(mEditItemIndex);
  emit entryEdit(cutItem);
}

/*!
 * onGotoCutIn
 */
void TTCutTreeView::onGotoCutIn()
{
  const int index = currentCutIndex();
  if (index < 0) return;

  emit gotoCutIn(mpAVData->cutItemAt(index).cutInIndex());
}

/*!
 * onGotoCutOut
 */
void TTCutTreeView::onGotoCutOut()
{
  const int index = currentCutIndex();
  if (index < 0) return;

  emit gotoCutOut(mpAVData->cutItemAt(index).cutOutIndex());
}

//! Creates the cut list from current selection
int TTCutTreeView::currentCutIndex() const
{
  if (mpAVData == 0 || videoCutList->currentItem() == 0) return -1;
  return videoCutList->indexOfTopLevelItem(videoCutList->currentItem());
}

TTCutList* TTCutTreeView::newJobCutList()
{
  delete mpJobCutList;
  mpJobCutList = new TTCutList();
  return mpJobCutList;
}

TTCutList* TTCutTreeView::cutListFromSelection(bool ignoreSelection)
{
  TTCutList* cutList = newJobCutList();

  for (int i = 0; i < videoCutList->topLevelItemCount(); i++) {
    TTCutItem cutItem = mpAVData->cutItemAt(i);

    if (ignoreSelection) {
      cutList->append(cutItem.avDataItem(), cutItem.cutInIndex(), cutItem.cutOutIndex());
      continue;
    }

    if (videoCutList->topLevelItem(i)->isSelected())
      cutList->append(cutItem.avDataItem(), cutItem.cutInIndex(), cutItem.cutOutIndex());
  }

  return cutList;
}

/*!
 * onEntryPreview
 * Preview selected cuts with neighboring cuts for transition visibility
 */
void TTCutTreeView::onEntryPreview()
{
  if (mpAVData == 0 || videoCutList->currentItem() == 0)  return;

  int totalCuts = videoCutList->topLevelItemCount();

  // Collect selected indices
  QSet<int> selected;
  for (int i = 0; i < totalCuts; i++) {
    if (videoCutList->topLevelItem(i)->isSelected())
      selected.insert(i);
  }

  // Build set of selected indices + their neighbors
  QSet<int> indices = selected;
  for (int i : selected) {
    if (i > 0) indices.insert(i - 1);
    if (i < totalCuts - 1) indices.insert(i + 1);
  }

  TTCutList* cutList = newJobCutList();
  QList<int> sorted = indices.values();
  std::sort(sorted.begin(), sorted.end());
  for (int idx : sorted) {
    TTCutItem cutItem = mpAVData->cutItemAt(idx);
    cutList->append(cutItem.avDataItem(), cutItem.cutInIndex(), cutItem.cutOutIndex());
  }

  // Skip standalone start/end clips when neighbor exists on that side
  bool skipFirst = !selected.contains(sorted.first());
  bool skipLast  = !selected.contains(sorted.last());

  emit previewCut(cutList, skipFirst, skipLast);
}

/*!
 * onEntryCut
 */
void TTCutTreeView::onEntryCut()
{
  if (mpAVData == 0 || videoCutList->currentItem() == 0)
    return;

  emit audioVideoCut(false, cutListFromSelection());
}

/*!
 * onPreview
 */
void TTCutTreeView::onPreview()
{
  if (mpAVData == 0) return;

  emit previewCut(cutListFromSelection(true));
}

/*!
 * onAVCut
 */
void TTCutTreeView::onAVCut()
{
  if (mpAVData == 0) return;

  emit audioVideoCut(false, cutListFromSelection(true));
}

/*!
 * onAVSelCut
 */
void TTCutTreeView::onAVSelCut()
{
  if (mpAVData == 0) return;

  emit audioVideoCut(false, cutListFromSelection());
}

/*!
 * onAudioCut — audio variant of "A/V Cut": always all entries, ignoring selection.
 */
void TTCutTreeView::onAudioCut()
{
  if (mpAVData == 0) return;

  emit audioVideoCut(true, cutListFromSelection(true));
}

/*!
 * onAudioSelCut — audio variant of "Selection Cut": only selected entries.
 */
void TTCutTreeView::onAudioSelCut()
{
  if (mpAVData == 0) return;

  emit audioVideoCut(true, cutListFromSelection());
}

/*!
 * onContextMenuRequest
 */
void TTCutTreeView::onContextMenuRequest( const QPoint& point)
{
  if (mpAVData == 0 || videoCutList->currentItem() == 0)
    return;

  QMenu contextMenu(this);
  bool bMultipleSelected = ( videoCutList->selectedItems().count() > 1 );

  if ( !bMultipleSelected ) {
    contextMenu.addAction(mpGotoCutInAction);
    contextMenu.addAction(mpGotoCutOutAction);
    contextMenu.addSeparator();
    contextMenu.addAction(mpItemEditAction);
  }
  contextMenu.addAction(mpItemPreviewAction);
  contextMenu.addAction(mpItemCutAction);
  contextMenu.addSeparator();
  contextMenu.addAction(mpItemUpAction);
  contextMenu.addAction(mpItemDuplicateAction);
  contextMenu.addAction(mpItemDownAction);
  contextMenu.addSeparator();
  contextMenu.addAction(mpItemDeleteAction);

  contextMenu.exec(videoCutList->mapToGlobal(point));
}

/*!
 * refreshHintIcons
 * Re-evaluate column 5 for all entries (tree order == cut list order).
 * Call after the burst filter setting changed so the list matches without
 * requiring cut edits. Covers burst AND AC3 format-change hints.
 */
void TTCutTreeView::refreshHintIcons()
{
    if (!mpAVData || !mpAVData->cutList()) return;
    TTCutList* list = mpAVData->cutList();
    int n = qMin(videoCutList->topLevelItemCount(), list->count());
    for (int i = 0; i < n; i++)
        updateHintColumn(videoCutList->topLevelItem(i), list->at(i));
}

/*!
 * updateHintColumn
 * The single entry point for column 5. Three producers, one writer: the
 * burst hint comes first, then the MPEG-2 aspect hint, then the AC3
 * format-change hint, and the cell is written once. The icon is the warning
 * icon if there is a burst or an aspect change, else the information icon if
 * there is an AC3 format change, else none. No producer touches the widget,
 * so their order is a composition rule here, not a contract they have to
 * know about.
 */
void TTCutTreeView::updateHintColumn(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    const HintCell burst  = burstHint(item);
    const HintCell aspect = aspectHint(item);
    const HintCell acmod  = acmodHint(item);

    // Text order burst, aspect, acmod; joined as before (" + " / newline), so
    // a burst+acmod cell reads exactly as it did with two producers.
    QStringList texts, tips;
    for (const HintCell* cell : {&burst, &aspect, &acmod}) {
        if (!cell->text.isEmpty()) texts << cell->text;
        if (!cell->tip.isEmpty())  tips  << cell->tip;
    }
    // A burst and an aspect change both damage the output (a click, a whole
    // file shown in the wrong shape); an AC3 change is normalised by the cut.
    QIcon icon;
    if (!burst.text.isEmpty() || !aspect.text.isEmpty())
        icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    else if (!acmod.text.isEmpty())
        icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);

    treeItem->setIcon(5, icon);
    treeItem->setText(5, texts.join(" + "));
    treeItem->setToolTip(5, tips.join("\n"));
}

/*!
 * burstHint
 * Audio burst near the cut boundaries; empty when there is none.
 */
TTCutTreeView::HintCell TTCutTreeView::burstHint(const TTCutItem& item) const
{
    HintCell cell;
    if (!mpAVData || !item.avDataItem() || item.avDataItem()->audioCount() == 0)
        return cell;

    TTAVData::CutBurstInfo bout = mpAVData->detectCutOutBurst(item);
    TTAVData::CutBurstInfo bin  = mpAVData->detectCutInBurst(item);
    if (!bout.present && !bin.present) return cell;

    if (bout.present && bin.present)
        cell.text = tr("Burst start+end");
    else if (bout.present)
        cell.text = tr("Burst end");
    else
        cell.text = tr("Burst start");

    if (bout.present)
        cell.tip += tr("Audio burst at end: %1 dB (context: %2 dB)").arg(bout.burstDb, 0, 'f', 1).arg(bout.contextDb, 0, 'f', 1);
    if (bin.present) {
        if (!cell.tip.isEmpty()) cell.tip += "\n";
        cell.tip += tr("Audio burst at start: %1 dB (context: %2 dB)").arg(bin.burstDb, 0, 'f', 1).arg(bin.contextDb, 0, 'f', 1);
    }
    return cell;
}

/*!
 * aspectHint
 * MPEG-2 aspect ratio change at the cut boundaries, judged against the
 * majority aspect of the cut (ttAnalyzeAspectWindow - the preview and the
 * warning before the cut call the same function). The MKV muxer takes the
 * aspect of the whole track from the first picture, so a stray 4:3 picture
 * at the start of a 16:9 cut marks the entire output 4:3. Empty when both
 * edges carry the majority aspect.
 */
TTCutTreeView::HintCell TTCutTreeView::aspectHint(const TTCutItem& item) const
{
    HintCell cell;
    if (!item.avDataItem()) return cell;

    const TTAspectWindowInfo info = ttAnalyzeAspectWindow(
        item.avDataItem()->videoStream(), item.cutInIndex(), item.cutOutIndex());
    const bool atIn  = info.cutInTarget  >= 0;
    const bool atOut = info.cutOutTarget >= 0;
    if (!atIn && !atOut) return cell;

    if (atIn && atOut)
        cell.text = tr("Aspect start+end");
    else if (atIn)
        cell.text = tr("Aspect start");
    else
        cell.text = tr("Aspect end");

    if (atIn)
        cell.tip = tr("Starts in %1, the cut is %2 from frame %3 (+%4)")
                       .arg(TTSequenceHeader::aspectText(info.cutInAspect), TTSequenceHeader::aspectText(info.mainAspect))
                       .arg(info.cutInTarget)
                       .arg(info.cutInTarget - item.cutInIndex());
    if (atOut) {
        if (!cell.tip.isEmpty()) cell.tip += "\n";
        cell.tip += tr("Ends in %1, the cut is %2 up to frame %3 (-%4)")
                        .arg(TTSequenceHeader::aspectText(info.cutOutAspect), TTSequenceHeader::aspectText(info.mainAspect))
                        .arg(info.cutOutTarget)
                        .arg(item.cutOutIndex() - info.cutOutTarget);
    }

    if (TTSettings::instance()->logUI())
        qDebug() << "aspectHint: cutIn=" << item.cutInIndex() << "cutOut=" << item.cutOutIndex()
                 << "main=" << info.mainAspect << "inTarget=" << info.cutInTarget
                 << "outTarget=" << info.cutOutTarget;
    return cell;
}

/*!
 * acmodHint
 * AC3 format change at the cut boundaries, judged against the majority acmod
 * of the cut window (ttAnalyzeAcmodWindow - the same function the cut
 * pipeline uses for its normalisation target, so hint and cut agree).
 * Empty when there is no change.
 */
TTCutTreeView::HintCell TTCutTreeView::acmodHint(const TTCutItem& item) const
{
    HintCell cell;
    if (!item.avDataItem() || item.avDataItem()->audioCount() == 0) return cell;

    TTAudioStream* audioStream = item.avDataItem()->audioStreamAt(0);
    if (!audioStream || audioStream->streamType() != TTAVTypes::ac3_audio) return cell;

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    if (!vStream || vStream->frameRate() <= 0) return cell;
    const double frameRate = vStream->frameRate();

    const double cutInSec  = item.cutInIndex() / frameRate;
    const double cutOutSec = (item.cutOutIndex() + 1) / frameRate;
    const TTAcmodInfo info = ttAnalyzeAcmodWindow(audioStream, cutInSec, cutOutSec);
    if (info.mainAcmod < 0) return cell;

    if (TTSettings::instance()->logUI())
        qDebug() << "acmodHint: cutIn=" << item.cutInIndex() << "cutOut=" << item.cutOutIndex()
                 << "main=" << info.mainAcmod << "in=" << info.cutInAcmod << "out=" << info.cutOutAcmod;

    const bool hasInChange  = (info.cutInAcmod  != info.mainAcmod);
    const bool hasOutChange = (info.cutOutAcmod != info.mainAcmod);
    if (!hasInChange && !hasOutChange) return cell;

    if (hasInChange && hasOutChange)
        cell.text = tr("AC3 start+end");
    else if (hasInChange)
        cell.text = tr("AC3 start");
    else
        cell.text = tr("AC3 end");

    static const char* AC3ModeName[] = {"1+1","1/0","2/0","3/0","2/1","3/1","2/2","3/2"};
    if (hasInChange)
        cell.tip += tr("Audio format change at start: %1 → %2")
            .arg(AC3ModeName[info.cutInAcmod]).arg(AC3ModeName[info.mainAcmod]);
    if (hasOutChange) {
        if (!cell.tip.isEmpty()) cell.tip += "\n";
        cell.tip += tr("Audio format change at end: %1 → %2")
            .arg(AC3ModeName[info.mainAcmod]).arg(AC3ModeName[info.cutOutAcmod]);
    }
    if (TTSettings::instance()->normalizeAcmod())
        cell.tip += tr("\n(Will be normalized during cut)");
    return cell;
}

/*!
 * onAudioDriftUpdated
 * Display accumulated audio boundary drift per cut after preview
 */
void TTCutTreeView::onAudioDriftUpdated(const QList<float>& driftsMs)
{
    for (int i = 0; i < driftsMs.size() && i < videoCutList->topLevelItemCount(); i++) {
        QTreeWidgetItem* treeItem = videoCutList->topLevelItem(i);
        float driftMs = driftsMs.at(i);
        treeItem->setText(4, QString("%1 ms").arg(driftMs, 0, 'f', 1));
    }
}

/*!
 * createActions
 */
void TTCutTreeView::createActions()
{
  mpItemUpAction = ttMakeAction(this, tr("Move &up"), "go-up", QStyle::SP_ArrowUp, tr("Move selected cut one position upward"));
  connect(mpItemUpAction, &QAction::triggered, this, &TTCutTreeView::onEntryUp);

  mpItemDeleteAction = ttMakeAction(this, tr("&Delete"), "edit-delete", QStyle::SP_TrashIcon, tr("Remove selected cut from list"));
  connect(mpItemDeleteAction, &QAction::triggered, this, &TTCutTreeView::onEntryDelete);

  mpItemDuplicateAction = ttMakeAction(this, tr("Duplicate Cut"), "edit-copy", QStyle::SP_FileDialogNewFolder, tr("Duplicate the selected cut"));
  connect(mpItemDuplicateAction, &QAction::triggered, this, &TTCutTreeView::onEntryDuplicate);

  mpItemDownAction = ttMakeAction(this, tr("Move d&own"), "go-down", QStyle::SP_ArrowDown, tr("Move selected cut one position downward"));
  connect(mpItemDownAction, &QAction::triggered, this, &TTCutTreeView::onEntryDown);

  mpItemEditAction = ttMakeAction(this, tr("Edit &cut"), "document-edit", QStyle::SP_FileDialogDetailedView, tr("Edit selected cut"));
  connect(mpItemEditAction, &QAction::triggered, this, &TTCutTreeView::onEntryEdit);

  mpItemPreviewAction = ttMakeAction(this, tr("Preview cut"), "edit-cut", QStyle::SP_DialogApplyButton, tr("Preview selected cut"));
  connect(mpItemPreviewAction, &QAction::triggered, this, &TTCutTreeView::onEntryPreview);

  mpItemCutAction = ttMakeAction(this, tr("Cut selected entries"), "edit-cut", QStyle::SP_DialogSaveButton, tr("Cut the selected entries"));
  connect(mpItemCutAction, &QAction::triggered, this, &TTCutTreeView::onEntryCut);

  mpGotoCutInAction = ttMakeAction(this, tr("Goto Cut-In"), "go-first", QStyle::SP_MediaSkipBackward, tr("Goto selected cut-in position"));
  connect(mpGotoCutInAction, &QAction::triggered, this, &TTCutTreeView::onGotoCutIn);

  mpGotoCutOutAction = ttMakeAction(this, tr("Goto Cut-Out"), "go-last", QStyle::SP_MediaSkipForward, tr("Goto selected cut-out position"));
  connect(mpGotoCutOutAction, &QAction::triggered, this, &TTCutTreeView::onGotoCutOut);
}
