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
// TTNAVIGATORDISPLAY
// ----------------------------------------------------------------------------

#include "ttnavigatordisplay.h"

#include "../common/ttcut.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavlist.h"
#include "../data/ttcutlist.h"

#include <QPainter>

/*!
 * TTNavigatorDisplay
 */
TTNavigatorDisplay::TTNavigatorDisplay(QWidget* parent)
  :QFrame(parent)
{
  setupUi( this );

  mAVDataItem     = 0;
  mControlEnabled = false;
  mMinValue       = 0;
  mMaxValue       = 1;

  // Hide the child QFrame so it doesn't cover our paint area
  navigatorDisplay->hide();

  // Ensure widget has a visible height
  setMinimumHeight(20);

  // Allow transparent background for proper painting
  setAttribute(Qt::WA_OpaquePaintEvent, false);
}

/*!
 * controlEnabled
 */
void TTNavigatorDisplay::controlEnabled(bool enabled)
{
  mControlEnabled = enabled;
  update();
}

/*!
 * paintEvent
 */
void TTNavigatorDisplay::paintEvent(QPaintEvent*)
{
  if (mAVDataItem != 0 && mControlEnabled)
    drawCutList();
}

/*!
 * drawCutList
 */
void TTNavigatorDisplay::drawCutList()
{
  QRect clientRect = rect();
  int   startY = clientRect.y();
  int   height = clientRect.height();

  const double scaleFactor = (mMaxValue > mMinValue)
      ? clientRect.width() / (double)(mMaxValue - mMinValue)
      : 1.0;

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);

  // Dark background for removed/cut-away segments
  painter.fillRect(clientRect, QBrush(QColor(40, 30, 30)));

  if (mAVDataItem == 0) return;

  // Draw kept segments in green gradient
  QColor keepColorStart(45, 120, 45);   // Dark green
  QColor keepColorEnd(75, 180, 75);     // Lighter green

  for (int i = 0; i < mAVDataItem->cutCount(); i++) {
      TTCutItem item = mAVDataItem->cutListItemAt(i);

      const int cutIn  = item.cutInIndex();
      const int cutOut = item.cutOutIndex();
      const int startX       = clientRect.x() + (int)(cutIn * scaleFactor);
      const int segmentWidth = (int)((cutOut - cutIn) * scaleFactor);

      // Gradient fill for kept segments
      QLinearGradient gradient(startX, startY, startX, startY + height);
      gradient.setColorAt(0, keepColorEnd);
      gradient.setColorAt(1, keepColorStart);
      painter.fillRect(startX, startY, segmentWidth, height, gradient);

      // Draw cut-in marker (bright green line)
      painter.setPen(QPen(QColor(100, 255, 100), 2));
      painter.drawLine(startX, startY, startX, startY + height);

      // Draw cut-out marker (yellow/gold line)
      int cutOutX = startX + segmentWidth;
      painter.setPen(QPen(QColor(204, 170, 0), 2));
      painter.drawLine(cutOutX, startY, cutOutX, startY + height);
  }

  // Draw frame border
  painter.setPen(QPen(QColor(80, 80, 80), 1));
  painter.drawRect(clientRect.adjusted(0, 0, -1, -1));
}

/*!
 * onAVItemChanged
 */
void TTNavigatorDisplay::onAVItemChanged(TTAVItem* avDataItem)
{
  if (avDataItem == 0) {
    mAVDataItem     = 0;
    mMinValue       = 0;
    mMaxValue       = 1;
    mControlEnabled = false;
    update();
    return;
  }

  mMinValue       = 0;
  mMaxValue       = avDataItem->videoStream()->frameCount() - 1;
  if (mMaxValue < 1) mMaxValue = 1;
  mAVDataItem     = avDataItem;
  mControlEnabled = true;

  update();
}


