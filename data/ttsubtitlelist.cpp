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
// TTSUBTITLELISTDATA
// ----------------------------------------------------------------------------

#include "ttsubtitlelist.h"
#include "ttavlist.h"
#include "../avstream/ttavheader.h"
#include "../avstream/ttsubtitleheaderlist.h"
#include "../common/ttcut.h"
#include "../avstream/ttavstream.h"

#include <QFileInfo>
#include <QLocale>
#include <QString>
#include <algorithm>

/* /////////////////////////////////////////////////////////////////////////////
 * Class TTSubtitleListDataItem
 */

/*!
 * TTSubtitleListDataItem
 * Default constructor
 */
TTSubtitleItem::TTSubtitleItem(TTAVItem* avDataItem, TTSubtitleStream* sStream)
{
  mpAVDataItem   = avDataItem;
  subtitleStream = sStream;
  mOrder         = -1;
  mLanguage      = "und";

  setItemData();
}

/*!
 * TTSubtitleListDataItem
 * Copy constructor
 */
TTSubtitleItem::TTSubtitleItem(const TTSubtitleItem& item)
{
  mpAVDataItem    = item.mpAVDataItem;
  mOrder          = item.mOrder;
  subtitleStream  = item.subtitleStream;
  subtitleLength  = item.subtitleLength;
  subtitleDelay   = item.subtitleDelay;
  mLanguage       = item.mLanguage;
}

/*!
 * setItemData
 */
void TTSubtitleItem::setItemData()
{
  subtitleLength = QString("%1 (%2)")
                        .arg(subtitleStream->streamLengthTime().toString("hh:mm:ss.zzz"))
                        .arg(subtitleStream->headerList()->count());

  // FIXME: use real delay value for subtitle delay
  subtitleDelay  = "0";

  // Extract language from filename: Show_deu.srt, Show_deu_1.srt (else fall
  // back to system locale). Shared helper with TTAudioItem.
  mLanguage = TTCut::langFromFilename(subtitleStream->fileName());
}

/*!
 * operator =
 */
const TTSubtitleItem& TTSubtitleItem::operator=(const TTSubtitleItem& item)
{
  if (this == &item)
    return *this;

  mOrder          = item.mOrder;
  mpAVDataItem    = item.mpAVDataItem;
  subtitleStream  = item.subtitleStream;
  subtitleLength  = item.subtitleLength;
  subtitleDelay   = item.subtitleDelay;
  mLanguage       = item.mLanguage;

  return *this;
}

/*!
 * operator <
 */
bool TTSubtitleItem::operator<(const TTSubtitleItem& item) const
{
  return mOrder < item.mOrder;
}

/*!
 * operator ==
 */
bool TTSubtitleItem::operator==(const TTSubtitleItem& item) const
{
  return subtitleStream == item.subtitleStream && mpAVDataItem == item.mpAVDataItem;
}

/*!
 * fileInfo
 */
QFileInfo TTSubtitleItem::getFileInfo() const
{
  return *subtitleStream->fileInfo();
}

/*!
 * subtitleStream
 */
TTSubtitleStream* TTSubtitleItem::getSubtitleStream() const
{
  return subtitleStream;
}

/*!
 * fileName
 */
QString TTSubtitleItem::getFileName() const
{
  return subtitleStream->fileName();
}

/*!
 * length
 */
QString TTSubtitleItem::getLength() const
{
  return subtitleLength;
}

/*!
 * delay
 */
QString TTSubtitleItem::getDelay() const
{
  return subtitleDelay;
}

QString TTSubtitleItem::getLanguage() const
{
  return mLanguage;
}

void TTSubtitleItem::setLanguage(const QString& lang)
{
  mLanguage = lang;
}

/* /////////////////////////////////////////////////////////////////////////////
 * Class TTSubtitleListData
 */

/*!
 * TTSubtitleListData
 */
TTSubtitleList::TTSubtitleList()
{
}

/*!
 * ~TTSubtitleListData
 */
TTSubtitleList::~TTSubtitleList()
{
  clear();
}

/*!
 * clear
 */
void TTSubtitleList::clear()
{
  for (int i = data.count()-1; i >= 0; i--) {
    TTSubtitleItem item = data.takeAt(i);
    //emit itemRemoved(item);
    emit itemRemoved(i);
    if (item.subtitleStream != 0) delete item.subtitleStream;
    item.subtitleStream = 0;
  }
}

/*!
 * append
 * Create and append an new TTSubtitleListDataItem to the list
 */
void TTSubtitleList::append(TTAVItem* avDataItem, TTSubtitleStream* sStream, int order)
{
  TTSubtitleItem item(avDataItem, sStream);

  item.setOrder(order);
  data.append(item);
  emit itemAppended(item);
}

/*!
 * append
 * Appends an existing TTSubtitleListDataItem to the list
 */
void TTSubtitleList::append(const TTSubtitleItem& sItem)
{
  TTSubtitleItem item(sItem);

  if (item.order() < 0) {
    item.setOrder(data.count());
    emit orderUpdated(item, item.order());
  }

  data.append(item);
  emit itemAppended(item);
}

/*!
 * remove
 * Removes the specified item from the list
 */
void TTSubtitleList::remove(const TTSubtitleItem& item)
{
  int index = data.indexOf(item);
  TTSubtitleItem rItem = data.takeAt(index);
  //emit itemRemoved(rItem);
  emit itemRemoved(index);
}

/*!
 * update
 */
void TTSubtitleList::update(const TTSubtitleItem& sItem, const TTSubtitleItem& uItem)
{
  int index = data.indexOf(sItem); /*! index of item to update */
  data.replace(index, uItem);
  emit itemUpdated(sItem, uItem);
}

/*!
 * at
 */
const TTSubtitleItem& TTSubtitleList::at(int index)
{
  return data.at(index);
}

/*!
 * count
 */
int  TTSubtitleList::count()
{
  return data.count();
}

/*!
 * sortByOrder
 */
void TTSubtitleList::sortByOrder()
{
  std::sort(data.begin(), data.end());
}

/*!
 * indexOf
 */
int TTSubtitleList::indexOf(const TTSubtitleItem& item)
{
  return data.indexOf(item);
}

/*!
 * swap
 */
void TTSubtitleList::swap(int a, int b)
{
  data.swapItemsAt(a, b);
  emit itemsSwapped(a, b);
}

/*!
 * updateOrder
 */
void TTSubtitleList::updateOrder()
{
  for (int i = 0; i < data.count(); i++) {
    data[i].setOrder(i);
    emit orderUpdated(data.at(i), i);
  }
}

/*!
 * onAppendItem
 */
void TTSubtitleList::onAppendItem(const TTSubtitleItem& item)
{
  append(item);
}

/*!
 * onRemoveItem
 */
void TTSubtitleList::onRemoveItem(const TTSubtitleItem& item)
{
  remove(item);
}

/*!
 * onUpdateItem
 */
void TTSubtitleList::onUpdateItem(const TTSubtitleItem& sItem, const TTSubtitleItem& uItem)
{
  update(sItem, uItem);
}

/*!
 * onUpdateOrder
 */
void TTSubtitleList::onUpdateOrder(const TTSubtitleItem& item, int order)
{
  int index = data.indexOf(item);
  if (index < 0) return;

  data[index].setOrder(order);

  emit orderUpdated(data.at(index), order);
}

//! Request to refresh the list data
void TTSubtitleList::onRefreshData(TTAVItem*)
{
  qDebug("TTSubtitleList::onRefreshData");

  emit dataChanged();
}

