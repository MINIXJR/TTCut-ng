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
// TTVIDEOTREEVIEW
// ----------------------------------------------------------------------------


#include "ttvideotreeview.h"
#include "tttreeviewutil.h"

#include "../data/ttavdata.h"
#include "../data/ttavlist.h"
#include "../avstream/ttmpeg2videostream.h"
#include "../avstream/tth264videostream.h"
#include "../avstream/tth265videostream.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * Construct a new TTVideoFileList widget.
 */
TTVideoTreeView::TTVideoTreeView(QWidget* parent)
  :TTTrackTreeView(parent)
{
  setupUi( this );

  QHeaderView* header = videoListView->header();
  header->resizeSection(0, 320);
  header->resizeSection(1, 220);
  header->resizeSection(2, 140);

  mAVData = 0;
  allowSelectionChanged = true;

  bindListWidgets(videoListView, pbVideoFileOpen, pbEntryUp, pbEntryDown, pbEntryDelete,
      { tr("&Insert videofile"), tr("Open a new videofile and insert to list"),
        tr("Move selected file one position upward"),
        tr("Remove selected file from list"),
        tr("Move selected file one position downward") });
  connect(videoListView, &QTreeWidget::itemSelectionChanged, this, &TTVideoTreeView::onItemSelectionChanged);
}

/* /////////////////////////////////////////////////////////////////////////////
 * setAVData
 */
void TTVideoTreeView::setAVData(TTAVData* avData)
{
  mAVData = avData;

  connect(mAVData, &TTAVData::avItemAppended,    this, &TTVideoTreeView::onAppendItem);
  connect(mAVData, &TTAVData::avItemRemoved,     this, &TTVideoTreeView::onItemRemoved);
  connect(mAVData, &TTAVData::avItemsSwapped,    this, &TTVideoTreeView::onItemsSwapped);
  connect(mAVData, &TTAVData::avDataReloaded,    this, &TTVideoTreeView::onReloadList);

  connect(this,    &TTVideoTreeView::removeItem,        mAVData, &TTAVData::onRemoveAVItem);
  connect(this,    &TTVideoTreeView::swapItems,         mAVData, &TTAVData::onSwapAVItems);
  connect(this,    &TTVideoTreeView::selectionChanged,  mAVData, qOverload<int>(&TTAVData::onChangeCurrentAVItem));
}

/* /////////////////////////////////////////////////////////////////////////////
 * Enable or disable the widget
 */
/* /////////////////////////////////////////////////////////////////////////////
 * onAppendItem
 */
void TTVideoTreeView::onAppendItem(const TTAVItem& item)
{
  QTreeWidgetItem* treeItem = new QTreeWidgetItem(videoListView);
  TTVideoStream*   vStream  = item.videoStream();

  if (vStream == nullptr) {
    return;
  }

  // Column 0: Filename
  treeItem->setText(0, vStream->fileName());

  // Column 1: Length (time + frame count)
  treeItem->setText(1, QString("%1 (%2)")
      .arg(vStream->streamLengthTime().toString("hh:mm:ss.zzz"))
      .arg(vStream->frameCount()));

  // Column 4: Framerate
  treeItem->setText(4, QString::number(vStream->frameRate(), 'f', 2));

  // Column 5: Bitrate
  treeItem->setText(5, QString("%1 kbit/s").arg(vStream->bitRate(), 0, 'f', 0));

  // Get resolution and aspect ratio based on stream type
  QString resolution;
  QString aspectRatio;
  QString vbvDelay;

  // Check for MPEG-2
  TTMpeg2VideoStream* mpeg2Stream = dynamic_cast<TTMpeg2VideoStream*>(vStream);
  if (mpeg2Stream != nullptr) {
    TTSequenceHeader* seqHeader = mpeg2Stream->currentSequenceHeader();
    if (seqHeader != nullptr) {
      resolution = QString("%1x%2").arg(seqHeader->horizontalSize()).arg(seqHeader->verticalSize());
      aspectRatio = seqHeader->aspectRatioText();
      vbvDelay = QString("%1 kB").arg(seqHeader->vbvBufferSize() * 2);
    }
  }

  // Check for H.264
  const TTH264VideoStream* h264Stream = dynamic_cast<const TTH264VideoStream*>(vStream);
  if (h264Stream != nullptr && h264Stream->getSPS() != nullptr) {
    resolution = QString("%1x%2").arg(h264Stream->getSPS()->width()).arg(h264Stream->getSPS()->height());
    aspectRatio = "H.264";
  }

  // Check for H.265
  const TTH265VideoStream* h265Stream = dynamic_cast<const TTH265VideoStream*>(vStream);
  if (h265Stream != nullptr && h265Stream->getSPS() != nullptr) {
    resolution = QString("%1x%2").arg(h265Stream->getSPS()->width()).arg(h265Stream->getSPS()->height());
    aspectRatio = "H.265";
  }

  // Column 2: Resolution
  treeItem->setText(2, resolution);

  // Column 3: Ratio
  treeItem->setText(3, aspectRatio);

  // Column 6: VBVDelay (MPEG-2 only)
  treeItem->setText(6, vbvDelay);
}

void TTVideoTreeView::onItemSelectionChanged()
{
	if (!allowSelectionChanged)
		return;

	QList<QTreeWidgetItem*> selectedItems = videoListView->selectedItems();

	if (selectedItems.count() == 0)
		return;

	int currentIndex = videoListView->indexOfTopLevelItem(selectedItems[0]);

  emit selectionChanged(currentIndex);
}

void TTVideoTreeView::onItemsSwapped(int oldIndex, int newIndex)
{
  QTreeWidgetItem* listItem = videoListView->takeTopLevelItem(oldIndex);

  videoListView->insertTopLevelItem(newIndex, listItem);
  videoListView->setCurrentItem(listItem);
}



void TTVideoTreeView::onReloadList()
{
  onClearList();

  for (int i = 0; i < mAVData->avCount(); i++) {
    TTAVItem* videoItem = mAVData->avItemAt(i);
    onAppendItem(*videoItem);
  }
}

/* /////////////////////////////////////////////////////////////////////////////
 * Up/down/remove act only once a TTAVData is attached (the list is filled
 * from its signals); the base class does the selection handling.
 */
void TTVideoTreeView::onItemUp()
{
  if (mAVData == 0) return;
  TTTrackTreeView::onItemUp();
}

void TTVideoTreeView::onItemDown()
{
  if (mAVData == 0) return;
  TTTrackTreeView::onItemDown();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for remove item button: the selection-change round trip is
 * suppressed until onItemRemoved() has re-established the current row.
 */
void TTVideoTreeView::onRemoveItem()
{
  if (mAVData == 0 || currentRow() < 0)
    return;

  allowSelectionChanged = false;
  TTTrackTreeView::onRemoveItem();
}

void TTVideoTreeView::onItemRemoved(int index)
{
  TTTrackTreeView::onItemRemoved(index);
  emit selectionChanged(currentRow());
  allowSelectionChanged = true;
}
