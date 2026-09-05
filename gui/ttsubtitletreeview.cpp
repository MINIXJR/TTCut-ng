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
#include "tttreeviewutil.h"

#include "../data/ttsubtitlelist.h"
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
 * Construct a new TTSubtitleFileList widget.
 */
TTSubtitleTreeView::TTSubtitleTreeView(QWidget* parent)
  :TTTrackTreeView(parent)
{
  setupUi( this );

  mpAVItem = 0;

  QHeaderView* header = subtitleListView->header();
  header->resizeSection(0, 320);
  header->resizeSection(1, 220);
  header->resizeSection(2, 140);
  header->resizeSection(3, 100);

  bindListWidgets(subtitleListView, pbSubtitleFileOpen, pbSubtitleEntryUp, pbSubtitleEntryDown, pbSubtitleEntryDelete,
      { tr("&Insert subtitlefile"), tr("Open a new subtitlefile and insert to list"),
        tr("Move selected subtitlefile one position upward"),
        tr("Remove selected subtitlefile from list"),
        tr("Move selected subtitlefile one position downward") });
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
    disconnect(this,     &TTSubtitleTreeView::delayChanged,     mpAVItem, &TTAVItem::onSubtitleDelayChanged);

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
  connect(this,     &TTSubtitleTreeView::delayChanged,      mpAVItem, &TTAVItem::onSubtitleDelayChanged);

  onReloadList(mpAVItem);
}

/* /////////////////////////////////////////////////////////////////////////////
 * onAppendItem
 */
void TTSubtitleTreeView::onAppendItem(const TTSubtitleItem& item)
{
  QTreeWidgetItem* treeItem = new QTreeWidgetItem(subtitleListView);

  treeItem->setText(0, item.getFileName());
  treeItem->setText(1, item.getLength());
  addDelaySpin(treeItem, 2, item.getDelayMs(), tr("Positive values show the subtitles later, negative values earlier (mkvmerge convention)"));
  addLanguageCombo(treeItem, 3, item.getLanguage());
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

void TTSubtitleTreeView::onReloadList(const TTAVItem* avData)
{
  onClearList();

  for (int i = 0; i < avData->subtitleCount(); i++) {
    onAppendItem(avData->subtitleListItemAt(i));
  }
}

