/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttvideotreeview.cpp                                             */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/28/2005 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTVIDEOTREEVIEW
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


#include "ttvideotreeview.h"

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
  :QWidget(parent)
{
  setupUi( this );

  videoListView->setRootIsDecorated(false);
  QHeaderView* header = videoListView->header();
  header->resizeSection(0, 320);
  header->resizeSection(1, 220);
  header->resizeSection(2, 140);

  mAVData = 0;
  allowSelectionChanged = true;

  // Use theme icons with Qt standard icon fallback for cross-platform support
  QStyle* style = QApplication::style();
  pbVideoFileOpen->setIcon(QIcon::fromTheme("document-open", style->standardIcon(QStyle::SP_DialogOpenButton)));
  pbEntryUp->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  pbEntryDown->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  pbEntryDelete->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));

  createActions();

  // signal and slot connections
  connect(pbVideoFileOpen,  SIGNAL(clicked()),              SIGNAL(openFile()));
  connect(pbEntryUp,        SIGNAL(clicked()),              SLOT(onItemUp()));
  connect(pbEntryDown,      SIGNAL(clicked()),              SLOT(onItemDown()));
  connect(pbEntryDelete,    SIGNAL(clicked()),              SLOT(onRemoveItem()));
  connect(videoListView,    SIGNAL(itemSelectionChanged()), SLOT(onItemSelectionChanged()));
  connect(videoListView,    SIGNAL(customContextMenuRequested(const QPoint&)), SLOT( onContextMenuRequest(const QPoint&)));
}

//! Set the group box title string. This method is needed by designer.
void TTVideoTreeView::setTitle ( __attribute__((unused))const QString& title )
{
}

/* /////////////////////////////////////////////////////////////////////////////
 * setAVData
 */
void TTVideoTreeView::setAVData(TTAVData* avData)
{
  mAVData = avData;

  connect(mAVData, SIGNAL(avItemAppended(const TTAVItem&)), SLOT(onAppendItem(const TTAVItem&)));
  connect(mAVData, SIGNAL(avItemRemoved(int)),              SLOT(onItemRemoved(int)));
  connect(mAVData, SIGNAL(avItemsSwapped(int, int)),        SLOT(onItemsSwapped(int, int)));
  connect(mAVData, SIGNAL(avDataReloaded()),                SLOT(onReloadList()));

  connect(this,    SIGNAL(removeItem(int)),       mAVData, SLOT(onRemoveAVItem(int)));
  connect(this,    SIGNAL(swapItems(int, int)),   mAVData, SLOT(onSwapAVItems(int, int)));
  connect(this,    SIGNAL(selectionChanged(int)), mAVData, SLOT(onChangeCurrentAVItem(int)));
}

void TTVideoTreeView::clear()
{
  disconnect(mAVData, SIGNAL(avItemAppended(const TTAVItem&)), this, SLOT(onAppendItem(const TTAVItem&)));
  disconnect(mAVData, SIGNAL(avItemRemoved(int)),              this, SLOT(onItemRemoved(int)));
  disconnect(mAVData, SIGNAL(avItemsSwapped(int, int)),        this, SLOT(onItemsSwapped(int, int)));
  disconnect(mAVData, SIGNAL(avDataReloaded()),                this, SLOT(onReloadList()));

  disconnect(this,    SIGNAL(removeItem(int)),       mAVData, SLOT(onRemoveAVItem(int)));
  disconnect(this,    SIGNAL(swapItems(int, int)),   mAVData, SLOT(onSwapAVItems(int, int)));
  disconnect(this,    SIGNAL(selectionChanged(int)), mAVData, SLOT(onChangeCurrentAVItem(int)));

	videoListView->clear();
}

/* /////////////////////////////////////////////////////////////////////////////
 * Enable or disable the widget
 */
void TTVideoTreeView::setControlEnabled( bool enabled )
{
  pbVideoFileOpen->setEnabled(enabled);
  pbEntryUp->setEnabled(enabled);
  pbEntryDelete->setEnabled(enabled);
  pbEntryDown->setEnabled(enabled);
  videoListView->setEnabled(enabled);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onClearList
 */
void TTVideoTreeView::onClearList()
{
  videoListView->clear();
}

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
  TTH264VideoStream* h264Stream = dynamic_cast<TTH264VideoStream*>(vStream);
  if (h264Stream != nullptr && h264Stream->getSPS() != nullptr) {
    resolution = QString("%1x%2").arg(h264Stream->getSPS()->width()).arg(h264Stream->getSPS()->height());
    aspectRatio = "H.264";
  }

  // Check for H.265
  TTH265VideoStream* h265Stream = dynamic_cast<TTH265VideoStream*>(vStream);
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

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item up button
 */
void TTVideoTreeView::onItemUp()
{
  if (mAVData == 0 || videoListView->currentItem() == 0)
    return;

  int index = videoListView->indexOfTopLevelItem(videoListView->currentItem());

  if (index <= 0)
    return;

  emit swapItems(index, index-1);
}

/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for item down button
 */
void TTVideoTreeView::onItemDown()
{
  if (mAVData == 0 || videoListView->currentItem() == 0)
    return;

  int index = videoListView->indexOfTopLevelItem(videoListView->currentItem());

  if (index >= videoListView->topLevelItemCount()-1)
    return;

  emit swapItems(index, index+1);
}

void TTVideoTreeView::onItemRemoved(int index)
{
	delete videoListView->takeTopLevelItem(index);

	int indexNew = videoListView->indexOfTopLevelItem(videoListView->currentItem());

  emit selectionChanged(indexNew);
  allowSelectionChanged = true;
}

void TTVideoTreeView::onItemsSwapped(int oldIndex, int newIndex)
{
  QTreeWidgetItem* listItem = videoListView->takeTopLevelItem(oldIndex);

  videoListView->insertTopLevelItem(newIndex, listItem);
  videoListView->setCurrentItem(listItem);
}


/* /////////////////////////////////////////////////////////////////////////////
 * Event handler for delete item button
 */
void TTVideoTreeView::onRemoveItem()
{
 if (mAVData == 0 || videoListView->currentItem() == 0)
    return;

  allowSelectionChanged = false;
  int index = videoListView->indexOfTopLevelItem(videoListView->currentItem());

  emit removeItem(index);
}


/* /////////////////////////////////////////////////////////////////////////////
 * onContextMenuRequest
 * User requested a context menu
 */
void TTVideoTreeView::onContextMenuRequest(const QPoint& point)
{
  if (videoListView->currentItem() == 0)
    return;

  QMenu contextMenu(this);
  contextMenu.addAction(itemNewAction);
  contextMenu.addSeparator();
  contextMenu.addAction(itemUpAction);
  contextMenu.addAction(itemDeleteAction);
  contextMenu.addAction(itemDownAction);

  contextMenu.exec(videoListView->mapToGlobal(point));
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
 * createAction
 * Create the actions used by the context menu.
 */
void TTVideoTreeView::createActions()
{
  QStyle* style = QApplication::style();

  itemNewAction = new QAction(tr("&Insert videofile"), this);
  itemNewAction->setIcon(QIcon::fromTheme("document-open", style->standardIcon(QStyle::SP_DialogOpenButton)));
  itemNewAction->setStatusTip(tr("Open a new videofile and insert to list"));
  connect(itemNewAction, SIGNAL(triggered()), SIGNAL(openFile()));

  itemUpAction = new QAction(tr("Move &up"), this);
  itemUpAction->setIcon(QIcon::fromTheme("go-up", style->standardIcon(QStyle::SP_ArrowUp)));
  itemUpAction->setStatusTip(tr("Move selected file one position upward"));
  connect(itemUpAction, SIGNAL(triggered()), SLOT(onItemUp()));

  itemDeleteAction = new QAction(tr("&Delete"), this);
  itemDeleteAction->setIcon(QIcon::fromTheme("edit-delete", style->standardIcon(QStyle::SP_TrashIcon)));
  itemDeleteAction->setStatusTip(tr("Remove selected file from list"));
  connect(itemDeleteAction, SIGNAL(triggered()), SLOT(onRemoveItem()));

  itemDownAction = new QAction(tr("Move d&own"), this);
  itemDownAction->setIcon(QIcon::fromTheme("go-down", style->standardIcon(QStyle::SP_ArrowDown)));
  itemDownAction->setStatusTip(tr("Move selected file one position downward"));
  connect(itemDownAction, SIGNAL(triggered()), SLOT(onItemDown()));
}

