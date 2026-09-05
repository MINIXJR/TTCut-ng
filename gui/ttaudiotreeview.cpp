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
// TTAUDIOTREEVIEW
// ----------------------------------------------------------------------------


#include "ttaudiotreeview.h"
#include "tttreeviewutil.h"

#include "../data/ttaudiolist.h"
#include "../data/ttavlist.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QSpinBox>
#include <QStyle>

/* /////////////////////////////////////////////////////////////////////////////
 * Construct a new TTAudioFileList widget.
 */
TTAudioTreeView::TTAudioTreeView(QWidget* parent)
  :TTTrackTreeView(parent)
{
  setupUi( this );

  mpAVItem = 0;

  QHeaderView* header = audioListView->header();
  header->resizeSection(0, 220);
  header->resizeSection(1, 220);
  header->resizeSection(2, 140);
  header->resizeSection(3, 100);
  header->resizeSection(4, 100);
  header->resizeSection(5, 100);
  header->resizeSection(6,  60);
  header->resizeSection(7, 100);

  bindListWidgets(audioListView, pbAudioFileOpen, pbAudioEntryUp, pbAudioEntryDown, pbAudioEntryDelete,
      { tr("&Insert audiofile"), tr("Open a new audiofile and insert to list"),
        tr("Move selected audiofile one position upward"),
        tr("Remove selected audiofile from list"),
        tr("Move selected audiofile one position downward") });
}

/* /////////////////////////////////////////////////////////////////////////////
 * setAVData
 */
void TTAudioTreeView::onAVDataChanged(const TTAVItem* avData)
{
	if (avData == 0) {
		mpAVItem = 0;
		clear();
		return;
	}

	if (mpAVItem != 0) {
	disconnect(this,     &TTAudioTreeView::removeItem,       mpAVItem, &TTAVItem::onRemoveAudioItem);
  disconnect(this,     &TTAudioTreeView::swapItems,        mpAVItem, &TTAVItem::onSwapAudioItems);
  disconnect(this,     &TTAudioTreeView::languageChanged,  mpAVItem, &TTAVItem::onAudioLanguageChanged);
  disconnect(this,     &TTAudioTreeView::delayChanged,     mpAVItem, &TTAVItem::onAudioDelayChanged);

  disconnect(mpAVItem, &TTAVItem::audioItemAppended,        this, &TTAudioTreeView::onAppendItem);
  disconnect(mpAVItem, qOverload<int>(&TTAVItem::audioItemRemoved), this, &TTAudioTreeView::onItemRemoved);
  disconnect(mpAVItem, &TTAVItem::audioItemsSwapped,        this, &TTAudioTreeView::onSwapItems);
	}

  mpAVItem = avData;

  connect(mpAVItem, &TTAVItem::audioItemAppended,        this, &TTAudioTreeView::onAppendItem);
  connect(mpAVItem, qOverload<int>(&TTAVItem::audioItemRemoved), this, &TTAudioTreeView::onItemRemoved);
  connect(mpAVItem, &TTAVItem::audioItemsSwapped,        this, &TTAudioTreeView::onSwapItems);

  connect(this,     &TTAudioTreeView::removeItem,        mpAVItem, &TTAVItem::onRemoveAudioItem);
  connect(this,     &TTAudioTreeView::swapItems,         mpAVItem, &TTAVItem::onSwapAudioItems);
  connect(this,     &TTAudioTreeView::languageChanged,   mpAVItem, &TTAVItem::onAudioLanguageChanged);
  connect(this,     &TTAudioTreeView::delayChanged,      mpAVItem, &TTAVItem::onAudioDelayChanged);

  onReloadList(mpAVItem);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onAppendItem
 */
void TTAudioTreeView::onAppendItem(const TTAudioItem& item)
{
  QTreeWidgetItem* treeItem = new QTreeWidgetItem(audioListView);

  treeItem->setText(0, item.getFileName());
  treeItem->setText(1, item.getLength());
  treeItem->setText(2, item.getVersion());
  treeItem->setText(3, item.getBitrate());
  treeItem->setText(4, item.getSamplerate());
  treeItem->setText(5, item.getMode());
  addDelaySpin(treeItem, 6, item.getDelayMs(), tr("Positive values play the track later, negative values earlier (mkvmerge convention)"));
  addLanguageCombo(treeItem, 7, item.getLanguage());
}

/* //////////////////////////////////////////////////////////////////////////////
 * Swap two items in list
 */
void TTAudioTreeView::onSwapItems(int, int)
{
    // Rebuild entire list because QComboBox widgets are destroyed on takeTopLevelItem
    if (mpAVItem != 0) {
      onReloadList(mpAVItem);
    }
}

void TTAudioTreeView::onReloadList(const TTAVItem* avData)
{
  onClearList();

  for (int i = 0; i < avData->audioCount(); i++) {
    onAppendItem(avData->audioListItemAt(i));
  }
}

