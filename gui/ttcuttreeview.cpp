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
#include "../avstream/ttesinfo.h"
#include "../extern/ttffmpegwrapper.h"

#include "ttcuttreeview.h"

#include <QApplication>
#include <QDebug>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
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
  videoCutList->headerItem()->setText(5, "Burst");
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

  // Preview button - Cyan (playback/transport)
  pbPreview->setIcon(QIcon::fromTheme("media-playback-start", style->standardIcon(QStyle::SP_MediaPlay)));
  pbPreview->setStyleSheet("QPushButton { background-color: #227777; color: white; }"
                           "QPushButton:hover { background-color: #338888; }");

  // Cut buttons - green accent (main action, same as Cut-In)
  pbCutAudioVideo->setIcon(QIcon::fromTheme("edit-cut", style->standardIcon(QStyle::SP_DialogSaveButton)));
  pbCutAudioVideo->setStyleSheet("QPushButton { background-color: #2d7a2d; color: white; font-weight: bold; }"
                                  "QPushButton:hover { background-color: #3d8a3d; }");
  pbCutSelected->setIcon(QIcon::fromTheme("edit-cut", style->standardIcon(QStyle::SP_DialogSaveButton)));
  pbCutSelected->setStyleSheet("QPushButton { background-color: #2d7a2d; color: white; }"
                                "QPushButton:hover { background-color: #3d8a3d; }");
  pbCutAudio->setIcon(QIcon::fromTheme("audio-x-generic", style->standardIcon(QStyle::SP_DriveCDIcon)));
  pbCutAudio->setStyleSheet("QPushButton { color: #4488ff; }");  // Blue like I-Frame

  // actions for context menu
  createActions();

  // signal and slot connections
  connect(pbEntryUp,       SIGNAL(clicked()),                                 SLOT(onEntryUp()));
  connect(pbEntryDown,     SIGNAL(clicked()),                                 SLOT(onEntryDown()));
  connect(pbEntryDelete,   SIGNAL(clicked()),                                 SLOT(onEntryDelete()));
  connect(pbEntryCopy,     SIGNAL(clicked()),                                 SLOT(onEntryDuplicate()));
  connect(pbPreview,       SIGNAL(clicked()),                                 SLOT(onPreview()));
  connect(pbCutAudioVideo, SIGNAL(clicked()),                                 SLOT(onAVCut()));
  connect(pbCutSelected,   SIGNAL(clicked()),                                 SLOT(onAVSelCut()));
  connect(pbCutAudio,      SIGNAL(clicked()),                                 SLOT(onAudioCut()));
  connect(videoCutList,    SIGNAL(itemSelectionChanged()),                    SLOT(onItemSelectionChanged()));
  connect(videoCutList,    SIGNAL(itemClicked(QTreeWidgetItem*, int)),        SLOT(onEntrySelected(QTreeWidgetItem*, int)));
  connect(videoCutList,    SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(onContextMenuRequest(const QPoint&)));
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

  connect(mAVData, SIGNAL(cutItemAppended(const TTCutItem&)),                  SLOT(onAppendItem(const TTCutItem&)));
  connect(mAVData, SIGNAL(cutItemRemoved(int)),                                SLOT(onRemoveItem(int)));
  connect(mAVData, SIGNAL(cutItemUpdated(const TTCutItem&, const TTCutItem&)), SLOT(onUpdateItem(const TTCutItem&, const TTCutItem&)));
  connect(mAVData, SIGNAL(cutDataReloaded()),                                  SLOT(onReloadList()));
  connect(this,    SIGNAL(removeItem(const TTCutItem&)),              mAVData, SLOT(onRemoveCutItem(const TTCutItem&)));
  connect(this,    SIGNAL(itemOrderChanged(int, int)),                mAVData, SLOT(onCutOrderChanged(int , int)));
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

  // Get A/V offset from .info file if available
  QString offsetStr = "-";  // No .info file
  if (item.avDataItem() != nullptr && item.avDataItem()->videoStream() != nullptr) {
    QString videoPath = item.avDataItem()->videoStream()->filePath();
    QString infoFile = TTESInfo::findInfoFile(videoPath);
    if (!infoFile.isEmpty()) {
      TTESInfo esInfo(infoFile);
      if (esInfo.isLoaded() && esInfo.hasTimingInfo()) {
        int offsetMs = esInfo.avOffsetMs();
        offsetStr = QString("%1 ms").arg(offsetMs);
      }
    }
  }
  treeItem->setText(4, offsetStr);

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

  videoCutList->indexOfTopLevelItem(videoCutList->currentItem());

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
  editItemIndex = videoCutList->indexOfTopLevelItem(curItem);

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
 * onSetCutOut
 */
void TTCutTreeView::onSetCutOut()
{
  if (mAVData == 0 || videoCutList->currentItem() == 0) return;

  int index = videoCutList->indexOfTopLevelItem(videoCutList->currentItem());

  TTCutItem cutItem = mAVData->cutItemAt(index);

  emit setCutOut(cutItem);
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
 */
void TTCutTreeView::onEntryPreview()
{
  if (mAVData == 0 || videoCutList->currentItem() == 0)  return;

  emit previewCut(cutListFromSelection());
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
 * onAudioCut
 */
void TTCutTreeView::onAudioCut()
{
  if (mAVData == 0) return;

  emit audioVideoCut(true, cutListFromSelection());
}

/*!
 * onEditCutOut
 */
void TTCutTreeView::onEditCutOut(const TTCutItem& cutItem, int cutOut)
{
  if (mAVData == 0) return

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
  	contextMenu.addAction(setCutOutAction);
    contextMenu.addAction(gotoCutInAction);
    contextMenu.addAction(gotoCutOutAction);
    contextMenu.addSeparator();
  }
  contextMenu.addAction(itemUpAction);
  contextMenu.addAction(itemDeleteAction);
  contextMenu.addAction(itemDuplicateAction);
  contextMenu.addAction(itemDownAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemCutAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemPreviewAction);
  if ( !bMultipleSelected ) {
    contextMenu.addSeparator();
    contextMenu.addAction(itemEditAction);
  }

  contextMenu.exec(videoCutList->mapToGlobal(point));
}

/*!
 * updateBurstIcon
 * Detect audio burst near cut boundaries and show warning icon
 */
void TTCutTreeView::updateBurstIcon(QTreeWidgetItem* treeItem, const TTCutItem& item)
{
    if (!item.avDataItem() || item.avDataItem()->audioCount() == 0) {
        treeItem->setIcon(5, QIcon());
        treeItem->setToolTip(5, "");
        return;
    }

    TTVideoStream* vStream = item.avDataItem()->videoStream();
    if (!vStream) return;
    double frameRate = vStream->frameRate();
    QString audioFile = item.avDataItem()->audioStreamAt(0)->filePath();
    int threshold = TTCut::burstThresholdDb;

    // Check cut-out boundary
    double cutOutTime = (item.cutOutIndex() + 1) / frameRate;
    double outBurstDb = 0, outContextDb = 0;
    bool hasCutOutBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutOutTime, true, outBurstDb, outContextDb);
    if (hasCutOutBurst && threshold != 0 && outBurstDb < threshold)
        hasCutOutBurst = false;

    // Check cut-in boundary
    double cutInTime = item.cutInIndex() / frameRate;
    double inBurstDb = 0, inContextDb = 0;
    bool hasCutInBurst = TTFFmpegWrapper::detectAudioBurst(audioFile, cutInTime, false, inBurstDb, inContextDb);
    if (hasCutInBurst && threshold != 0 && inBurstDb < threshold)
        hasCutInBurst = false;

    if (hasCutOutBurst || hasCutInBurst) {
        treeItem->setIcon(5, style()->standardIcon(QStyle::SP_MessageBoxWarning));
        QString shortText;
        if (hasCutOutBurst && hasCutInBurst)
            shortText = tr("Burst start+end");
        else if (hasCutOutBurst)
            shortText = tr("Burst end");
        else
            shortText = tr("Burst start");
        treeItem->setText(5, shortText);

        QString tip;
        if (hasCutOutBurst)
            tip += QString("Audio-Burst am Ende: %1 dB (Context: %2 dB)").arg(outBurstDb, 0, 'f', 1).arg(outContextDb, 0, 'f', 1);
        if (hasCutInBurst) {
            if (!tip.isEmpty()) tip += "\n";
            tip += QString("Audio-Burst am Anfang: %1 dB (Context: %2 dB)").arg(inBurstDb, 0, 'f', 1).arg(inContextDb, 0, 'f', 1);
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
 * createActions
 */
void TTCutTreeView::createActions()
{
  QStyle* style = QApplication::style();

  itemUpAction = new QAction(tr("Move &up"), this);
  itemUpAction->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  itemUpAction->setStatusTip(tr("Move selected cut one position upward"));
  connect(itemUpAction, SIGNAL(triggered()), this, SLOT(onEntryUp()));

  itemDeleteAction = new QAction(tr("&Delete"), this);
  itemDeleteAction->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));
  itemDeleteAction->setStatusTip(tr("Remove selected cut from list"));
  connect(itemDeleteAction, SIGNAL(triggered()), this, SLOT(onEntryDelete()));

  itemDuplicateAction = new QAction(tr("Duplicate Cut"), this);
  itemDuplicateAction->setIcon(QIcon::fromTheme("edit-copy", style->standardIcon(QStyle::SP_FileDialogNewFolder)));
  itemDuplicateAction->setStatusTip(tr("Duplicate the selected cut"));
  connect(itemDuplicateAction, SIGNAL(triggered()), this, SLOT(onEntryDuplicate()));

  itemDownAction = new QAction(tr("Move d&own"), this);
  itemDownAction->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  itemDownAction->setStatusTip(tr("Move selected cut one position downward"));
  connect(itemDownAction, SIGNAL(triggered()), this, SLOT(onEntryDown()));

  itemEditAction = new QAction(tr("Edit &cut"), this);
  itemEditAction->setIcon(QIcon::fromTheme("document-edit", style->standardIcon(QStyle::SP_FileDialogDetailedView)));
  itemEditAction->setStatusTip(tr("Edit selected cut"));
  connect(itemEditAction, SIGNAL(triggered()), this, SLOT(onEntryEdit()));

  itemPreviewAction = new QAction(tr("Preview cut"), this);
  itemPreviewAction->setIcon(QIcon::fromTheme("media-playback-start", style->standardIcon(QStyle::SP_MediaPlay)));
  itemPreviewAction->setStatusTip(tr("Preview selected cut"));
  connect(itemPreviewAction, SIGNAL(triggered()), this, SLOT(onEntryPreview()));

  itemCutAction = new QAction(tr("Cut selected entries"), this);
  itemCutAction->setIcon(QIcon::fromTheme("edit-cut", style->standardIcon(QStyle::SP_DialogSaveButton)));
  itemCutAction->setStatusTip(tr("Cut the selected entries"));
  connect(itemCutAction, SIGNAL(triggered()), this, SLOT(onEntryCut()));

  gotoCutInAction = new QAction(tr("Goto Cut-In"), this);
  gotoCutInAction->setIcon(QIcon::fromTheme("go-first", style->standardIcon(QStyle::SP_MediaSkipBackward)));
  gotoCutInAction->setStatusTip(tr("Goto selected cut-in position"));
  connect(gotoCutInAction, SIGNAL(triggered()), this, SLOT(onGotoCutIn()));

  gotoCutOutAction = new QAction(tr("Goto Cut-Out"), this);
  gotoCutOutAction->setIcon(QIcon::fromTheme("go-last", style->standardIcon(QStyle::SP_MediaSkipForward)));
  gotoCutOutAction->setStatusTip(tr("Goto selected cut-out position"));
  connect(gotoCutOutAction, SIGNAL(triggered()), this, SLOT(onGotoCutOut()));

  setCutOutAction = new QAction(tr("Set Cut-Out"), this);
  setCutOutAction->setIcon(QIcon::fromTheme("go-jump", style->standardIcon(QStyle::SP_ArrowRight)));
  setCutOutAction->setStatusTip(tr("Show selected cut-out frame in cut-out window"));
  connect(setCutOutAction, SIGNAL(triggered()), this, SLOT(onSetCutOut()));
}
