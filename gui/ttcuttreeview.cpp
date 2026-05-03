/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttcuttreeview.cpp                                               */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTREEVIEW
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

#include "../data/ttcutlist.h"
#include "../common/ttcut.h"
#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../avstream/ttavstream.h"
#include "../avstream/ttac3audiostream.h"
#include "../avstream/ttaudioheaderlist.h"
#include "../extern/ttffmpegwrapper.h"

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

  allowSelectionChanged = true;
  editItemIndex = -1;

  // Use theme icons with Qt standard icon fallback for cross-platform support
  // Video editing industry color scheme
  QStyle* style = QApplication::style();
  pbEntryUp->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  pbEntryDown->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  pbEntryDelete->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));
  pbEntryDelete->setStyleSheet("QPushButton { color: #cc4444; }");  // Red for destructive
  pbEntryCopy->setIcon(QIcon::fromTheme("edit-copy", style->standardIcon(QStyle::SP_FileDialogNewFolder)));

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
  QIcon iconCut    = tintIcon(QIcon::fromTheme("edit-cut",        style->standardIcon(QStyle::SP_DialogSaveButton)),  kIconColor, kIconSize);
  QIcon iconCutAlt = tintIcon(QIcon::fromTheme("edit-cut",        style->standardIcon(QStyle::SP_DialogApplyButton)), kIconColor, kIconSize);
  QIcon iconAudio  = tintIcon(QIcon::fromTheme("audio-x-generic", style->standardIcon(QStyle::SP_DriveCDIcon)),       kIconColor, kIconSize);

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
  mAVData = avData;

  connect(mAVData, &TTAVData::cutItemAppended,    this, &TTCutTreeView::onAppendItem);
  connect(mAVData, &TTAVData::cutItemRemoved,     this, &TTCutTreeView::onRemoveItem);
  connect(mAVData, &TTAVData::cutItemUpdated,     this, &TTCutTreeView::onUpdateItem);
  connect(mAVData, &TTAVData::cutDataReloaded,    this, &TTCutTreeView::onReloadList);
  connect(this,    &TTCutTreeView::removeItem,         mAVData, &TTAVData::onRemoveCutItem);
  connect(this,    &TTCutTreeView::itemOrderChanged,   mAVData, &TTAVData::onCutOrderChanged);
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

	for (int i = 0; i < mAVData->cutCount(); i++) {
	    TTCutItem cutItem = mAVData->cutItemAt(i);
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

  updateBurstIcon(treeItem, item);
  updateAcmodIcon(treeItem, item);

  //emit refreshDisplay();
}

/*!
 * onRemoveItem
 */
void TTCutTreeView::onRemoveItem(int index)
{
  delete videoCutList->takeTopLevelItem(index);
  allowSelectionChanged = true;
}

/*!
 * onUpdateItem
 */
void TTCutTreeView::onUpdateItem(const TTCutItem& cItem, const TTCutItem& uitem)
{
  QTreeWidgetItem* treeItem = (editItemIndex < 0)
      ? findItem(cItem) //videoCutList->currentItem()
      : videoCutList->topLevelItem(editItemIndex);

  if (treeItem == 0) {
    qDebug("TTCutTreeView::item not found!");
   	return;
  }

  treeItem->setText(0, uitem.fileName());
  treeItem->setText(1, uitem.cutInString());
  treeItem->setText(2, uitem.cutOutString());
  treeItem->setText(3, uitem.cutLengthString());

  updateBurstIcon(treeItem, uitem);
  updateAcmodIcon(treeItem, uitem);

  if (editItemIndex >= 0) {
    editItemIndex = -1;
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
	if (!mAVData) return 0;

	// Match by model index via UUID (column 4 contains A/V offset, not UUID)
	int modelIdx = mAVData->cutIndexOf(cutItem);
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

  if (videoCutList->topLevelItem(0)->isSelected() && editItemIndex < 0)
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

  if (videoCutList->topLevelItem(videoCutList->topLevelItemCount()-1)->isSelected() && editItemIndex < 0)
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
  if (editItemIndex >= 0)  return;

  allowSelectionChanged = false;

  for (int i = videoCutList->selectedItems().count()-1; i >= 0; i--) {
    int index = videoCutList->indexOfTopLevelItem(videoCutList->selectedItems()[i]);
    TTCutItem cutItem = mAVData->cutItemAt(index);
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
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;
  if (editItemIndex >= 0) return;

   for ( int i=0; i<videoCutList->topLevelItemCount(); ++i ) {
    if ( videoCutList->topLevelItem(i)->isSelected() ) {
      TTCutItem cutItem = mAVData->cutItemAt(i);
    	mAVData->copyCutEntry(cutItem);
    }
  }
}

/*!
 * onEntrySelected
 */
void TTCutTreeView::onEntrySelected(QTreeWidgetItem*, int column)
{
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;

  int index = videoCutList->indexOfTopLevelItem(videoCutList->currentItem());
  TTCutItem cutItem = mAVData->cutItemAt(index);

  emit selectionChanged(cutItem, column);
}

/*!
 * onItemSelectionChanged
 */
void TTCutTreeView::onItemSelectionChanged()
{
	if (!allowSelectionChanged) return;
  // Skip if triggered by mouse click — onEntrySelected handles that with correct column
  if (QApplication::mouseButtons() != Qt::NoButton) return;
  if (mAVData == 0 || videoCutList->currentItem() == 0)  return;

  int index = videoCutList->indexOfTopLevelItem(videoCutList->currentItem());
  TTCutItem cutItem = mAVData->cutItemAt(index);

  emit selectionChanged(cutItem, 0);
}

/*!
 * onEntryEdit
 */
void TTCutTreeView::onEntryEdit()
{
  //if (videoCutList->topLevelItemCount() == 0) return;
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;

  QTreeWidgetItem* curItem = videoCutList->currentItem();
  int newIndex = videoCutList->indexOfTopLevelItem(curItem);

  // Re-entry: user starts editing a different row while another edit is
  // still active. Drop the previous row's edit highlight so we don't end
  // up with two visually highlighted rows and stale editItemIndex state.
  // Same-row re-entry is a no-op.
  if (editItemIndex >= 0 && editItemIndex != newIndex) {
    QTreeWidgetItem* prev = videoCutList->topLevelItem(editItemIndex);
    if (prev) {
      for (int c = 0; c < videoCutList->columnCount(); c++)
        prev->setBackground(c, QBrush());
    }
  } else if (editItemIndex == newIndex) {
    return;
  }

  editItemIndex = newIndex;

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

  TTCutItem cutItem = mAVData->cutItemAt(editItemIndex);
  emit entryEdit(cutItem);
}

/*!
 * onGotoCutIn
 */
void TTCutTreeView::onGotoCutIn()
{
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;

  int index = videoCutList->indexOfTopLevelItem(videoCutList->currentItem());
  TTCutItem cutItem = mAVData->cutItemAt(index);

  emit gotoCutIn(cutItem.cutInIndex());
}

/*!
 * onGotoCutOut
 */
void TTCutTreeView::onGotoCutOut()
{
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;

  int index = videoCutList->indexOfTopLevelItem(videoCutList->currentItem());
  TTCutItem cutItem = mAVData->cutItemAt(index);

  emit gotoCutOut(cutItem.cutOutIndex());
}

//! Creates the cut list from current selection
TTCutList* TTCutTreeView::cutListFromSelection(bool ignoreSelection)
{
  TTCutList* cutList = new TTCutList();

  for (int i = 0; i < videoCutList->topLevelItemCount(); i++) {
    TTCutItem cutItem = mAVData->cutItemAt(i);

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
  if (mAVData == 0 || videoCutList->currentItem() == 0)  return;

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

  TTCutList* cutList = new TTCutList();
  QList<int> sorted = indices.values();
  std::sort(sorted.begin(), sorted.end());
  for (int idx : sorted) {
    TTCutItem cutItem = mAVData->cutItemAt(idx);
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
  if (mAVData == 0 || videoCutList->currentItem() == 0)
    return;

  emit audioVideoCut(false, cutListFromSelection());
}

/*!
 * onPreview
 */
void TTCutTreeView::onPreview()
{
  if (mAVData == 0) return;

  emit previewCut(cutListFromSelection(true));
}

/*!
 * onAVCut
 */
void TTCutTreeView::onAVCut()
{
  if (mAVData == 0) return;

  emit audioVideoCut(false, cutListFromSelection(true));
}

/*!
 * onAVSelCut
 */
void TTCutTreeView::onAVSelCut()
{
  if (mAVData == 0) return;

  emit audioVideoCut(false, cutListFromSelection());
}

/*!
 * onAudioCut — audio variant of "A/V Cut": always all entries, ignoring selection.
 */
void TTCutTreeView::onAudioCut()
{
  if (mAVData == 0) return;

  emit audioVideoCut(true, cutListFromSelection(true));
}

/*!
 * onAudioSelCut — audio variant of "Selection Cut": only selected entries.
 */
void TTCutTreeView::onAudioSelCut()
{
  if (mAVData == 0) return;

  emit audioVideoCut(true, cutListFromSelection());
}

/*!
 * onEditCutOut
 */
void TTCutTreeView::onEditCutOut(const TTCutItem& cutItem, int cutOut)
{
  if (mAVData == 0) return;

  cutItem.avDataItem()->updateCutEntry(cutItem, cutItem.cutInIndex(), cutOut);
  emit cutOutUpdated(cutItem);
}

/*!
 * onContextMenuRequest
 */
void TTCutTreeView::onContextMenuRequest( const QPoint& point)
{
  if (mAVData == 0 || videoCutList->currentItem() == 0)
    return;

  QMenu contextMenu(this);
  bool bMultipleSelected = ( videoCutList->selectedItems().count() > 1 );

  if ( !bMultipleSelected ) {
    contextMenu.addAction(gotoCutInAction);
    contextMenu.addAction(gotoCutOutAction);
    contextMenu.addSeparator();
    contextMenu.addAction(itemEditAction);
  }
  contextMenu.addAction(itemPreviewAction);
  contextMenu.addAction(itemCutAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemUpAction);
  contextMenu.addAction(itemDuplicateAction);
  contextMenu.addAction(itemDownAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemDeleteAction);

  contextMenu.exec(videoCutList->mapToGlobal(point));
}

/*!
 * updateBurstIcon
 * Detect audio burst near cut boundaries and show warning icon
 */
void TTCutTreeView::updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (!mAVData || !item.avDataItem() || item.avDataItem()->audioCount() == 0) {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
        return;
    }

    TTAVData::CutBurstInfo bout = mAVData->detectCutOutBurst(item);
    TTAVData::CutBurstInfo bin  = mAVData->detectCutInBurst(item);

    if (bout.present || bin.present) {
        treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxWarning));
        QString shortText;
        if (bout.present && bin.present)
            shortText = tr("Burst start+end");
        else if (bout.present)
            shortText = tr("Burst end");
        else
            shortText = tr("Burst start");
        treeItem->setText(5, shortText);

        QString tip;
        if (bout.present)
            tip += QString("Audio-Burst am Ende: %1 dB (Context: %2 dB)").arg(bout.burstDb, 0, 'f', 1).arg(bout.contextDb, 0, 'f', 1);
        if (bin.present) {
            if (!tip.isEmpty()) tip += "\n";
            tip += QString("Audio-Burst am Anfang: %1 dB (Context: %2 dB)").arg(bin.burstDb, 0, 'f', 1).arg(bin.contextDb, 0, 'f', 1);
        }
        treeItem->setToolTip(5, tip);
    } else {
        treeItem->setIcon(5, QIcon());
        treeItem->setText(5, "");
        treeItem->setToolTip(5, "");
    }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Update acmod change icon for a cut item (column 5, if no burst detected)
 * Uses the in-memory AC3 header list (no file I/O, no libav).
 */
void TTCutTreeView::updateAcmodIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (!item.avDataItem() || item.avDataItem()->audioCount() == 0) return;

    TTAudioStream* audioStream = item.avDataItem()->audioStreamAt(0);
    if (!audioStream || !audioStream->headerList()) return;
    if (audioStream->streamType() != TTAVTypes::ac3_audio) return;

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    if (!vStream) return;
    double frameRate = vStream->frameRate();

    TTAudioHeaderList* hdrList = audioStream->headerList();

    // Calculate audio frame index directly from video time and audio frame duration
    // (searchTimeIndex is O(n) linear scan — too slow for large files)
    TTAC3AudioHeader* firstHdr = dynamic_cast<TTAC3AudioHeader*>(hdrList->audioHeaderAt(0));
    if (!firstHdr) return;
    double audioFrameDurMs = firstHdr->frame_time;  // ms per AC3 frame (~32ms)
    if (audioFrameDurMs <= 0) audioFrameDurMs = 32.0;

    double cutInTimeMs = (item.cutInIndex() / frameRate) * 1000.0;
    double cutOutTimeMs = ((item.cutOutIndex() + 1) / frameRate) * 1000.0;
    int startIdx = qBound(0, static_cast<int>(cutInTimeMs / audioFrameDurMs), hdrList->count() - 1);
    int endIdx   = qBound(0, static_cast<int>(cutOutTimeMs / audioFrameDurMs), hdrList->count() - 1);

    if (startIdx < 0 || endIdx < 0 || startIdx >= hdrList->count()) return;
    if (endIdx >= hdrList->count()) endIdx = hdrList->count() - 1;

    // Read acmod at exact CutIn and CutOut positions
    TTAC3AudioHeader* hFirst = dynamic_cast<TTAC3AudioHeader*>(hdrList->audioHeaderAt(startIdx));
    TTAC3AudioHeader* hLast  = dynamic_cast<TTAC3AudioHeader*>(hdrList->audioHeaderAt(endIdx));
    if (!hFirst || !hLast) return;

    int firstAcmod = hFirst->acmod;
    int lastAcmod  = hLast->acmod;

    qDebug() << "updateAcmodIcon: cutIn=" << item.cutInIndex() << "cutOut=" << item.cutOutIndex()
             << "cutInMs=" << cutInTimeMs << "cutOutMs=" << cutOutTimeMs
             << "startIdx=" << startIdx << "endIdx=" << endIdx
             << "totalFrames=" << hdrList->count()
             << "firstAcmod=" << firstAcmod << "lastAcmod=" << lastAcmod;

    // Sample first ~100 frames to determine majority acmod
    static const int SAMPLE = 100;
    int acmodCount[8] = {0};
    int sampleEnd = qMin(startIdx + SAMPLE, endIdx + 1);
    for (int i = startIdx; i < sampleEnd; i++) {
        TTAC3AudioHeader* h = dynamic_cast<TTAC3AudioHeader*>(hdrList->audioHeaderAt(i));
        if (!h) continue;
        acmodCount[h->acmod]++;
    }
    // Also sample last ~100 frames if segment is long enough
    if (endIdx - startIdx >= 2 * SAMPLE) {
      for (int i = endIdx - SAMPLE + 1; i <= endIdx; i++) {
          TTAC3AudioHeader* h = dynamic_cast<TTAC3AudioHeader*>(hdrList->audioHeaderAt(i));
          if (!h) continue;
          acmodCount[h->acmod]++;
      }
    }

    // Majority acmod
    int mainAcmod = 0, maxCount = 0;
    for (int i = 0; i < 8; i++) {
        if (acmodCount[i] > maxCount) { maxCount = acmodCount[i]; mainAcmod = i; }
    }

    bool hasInChange  = (firstAcmod != mainAcmod);
    bool hasOutChange = (lastAcmod != mainAcmod);

    if (!hasInChange && !hasOutChange) return;

    // If burst icon already set, append acmod info to existing text
    QString existingText = treeItem->text(5);
    QString existingTip = treeItem->toolTip(5);

    if (treeItem->icon(5).isNull())
        treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxInformation));

    QString shortText;
    if (hasInChange && hasOutChange)
        shortText = tr("AC3 start+end");
    else if (hasInChange)
        shortText = tr("AC3 start");
    else
        shortText = tr("AC3 end");

    if (!existingText.isEmpty())
        shortText = existingText + " + " + shortText;
    treeItem->setText(5, shortText);

    static const char* AC3ModeName[] = {"1+1","1/0","2/0","3/0","2/1","3/1","2/2","3/2"};
    QString tip;
    if (hasInChange)
        tip += tr("Audio format change at start: %1 → %2")
            .arg(AC3ModeName[firstAcmod]).arg(AC3ModeName[mainAcmod]);
    if (hasOutChange) {
        if (!tip.isEmpty()) tip += "\n";
        tip += tr("Audio format change at end: %1 → %2")
            .arg(AC3ModeName[mainAcmod]).arg(AC3ModeName[lastAcmod]);
    }
    if (TTCut::normalizeAcmod)
        tip += tr("\n(Will be normalized during cut)");
    if (!existingTip.isEmpty())
        tip = existingTip + "\n" + tip;
    treeItem->setToolTip(5, tip);
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
  QStyle* style = QApplication::style();

  itemUpAction = new QAction(tr("Move &up"), this);
  itemUpAction->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  itemUpAction->setStatusTip(tr("Move selected cut one position upward"));
  connect(itemUpAction, &QAction::triggered, this, &TTCutTreeView::onEntryUp);

  itemDeleteAction = new QAction(tr("&Delete"), this);
  itemDeleteAction->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));
  itemDeleteAction->setStatusTip(tr("Remove selected cut from list"));
  connect(itemDeleteAction, &QAction::triggered, this, &TTCutTreeView::onEntryDelete);

  itemDuplicateAction = new QAction(tr("Duplicate Cut"), this);
  itemDuplicateAction->setIcon(QIcon::fromTheme("edit-copy", style->standardIcon(QStyle::SP_FileDialogNewFolder)));
  itemDuplicateAction->setStatusTip(tr("Duplicate the selected cut"));
  connect(itemDuplicateAction, &QAction::triggered, this, &TTCutTreeView::onEntryDuplicate);

  itemDownAction = new QAction(tr("Move d&own"), this);
  itemDownAction->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  itemDownAction->setStatusTip(tr("Move selected cut one position downward"));
  connect(itemDownAction, &QAction::triggered, this, &TTCutTreeView::onEntryDown);

  itemEditAction = new QAction(tr("Edit &cut"), this);
  itemEditAction->setIcon(QIcon::fromTheme("document-edit", style->standardIcon(QStyle::SP_FileDialogDetailedView)));
  itemEditAction->setStatusTip(tr("Edit selected cut"));
  connect(itemEditAction, &QAction::triggered, this, &TTCutTreeView::onEntryEdit);

  itemPreviewAction = new QAction(tr("Preview cut"), this);
  itemPreviewAction->setIcon(QIcon::fromTheme("edit-cut", style->standardIcon(QStyle::SP_DialogApplyButton)));
  itemPreviewAction->setStatusTip(tr("Preview selected cut"));
  connect(itemPreviewAction, &QAction::triggered, this, &TTCutTreeView::onEntryPreview);

  itemCutAction = new QAction(tr("Cut selected entries"), this);
  itemCutAction->setIcon(QIcon::fromTheme("edit-cut", style->standardIcon(QStyle::SP_DialogSaveButton)));
  itemCutAction->setStatusTip(tr("Cut the selected entries"));
  connect(itemCutAction, &QAction::triggered, this, &TTCutTreeView::onEntryCut);

  gotoCutInAction = new QAction(tr("Goto Cut-In"), this);
  gotoCutInAction->setIcon(QIcon::fromTheme("go-first", style->standardIcon(QStyle::SP_MediaSkipBackward)));
  gotoCutInAction->setStatusTip(tr("Goto selected cut-in position"));
  connect(gotoCutInAction, &QAction::triggered, this, &TTCutTreeView::onGotoCutIn);

  gotoCutOutAction = new QAction(tr("Goto Cut-Out"), this);
  gotoCutOutAction->setIcon(QIcon::fromTheme("go-last", style->standardIcon(QStyle::SP_MediaSkipForward)));
  gotoCutOutAction->setStatusTip(tr("Goto selected cut-out position"));
  connect(gotoCutOutAction, &QAction::triggered, this, &TTCutTreeView::onGotoCutOut);
}
