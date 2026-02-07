/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                      */
/* FILE     : ttnavigatordisplay.cpp                                          */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 03/03/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTNAVIGATORDISPLAY
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

  mAVDataItem      = 0;
  isControlEnabled = false;
  minValue         = 0;
  maxValue         = 1;
  scaleFactor      = 1.0;

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
  isControlEnabled = enabled;
  update();
}

/*!
 * resizeEvent
 */
void TTNavigatorDisplay::resizeEvent(QResizeEvent* event)
{
  QFrame::resizeEvent(event);

  if (maxValue > minValue) {
    scaleFactor = width() / (double)(maxValue - minValue);
  } else {
    scaleFactor = 1.0;
  }
}

/*!
 * paintEvent
 */
void TTNavigatorDisplay::paintEvent(QPaintEvent*)
{
  if (mAVDataItem != 0 && isControlEnabled)
    drawCutList();
}

/*!
 * drawCutList
 */
void TTNavigatorDisplay::drawCutList()
{
  int   cutIn;
  int   cutOut;
  QRect clientRect = rect();
  int   startX = clientRect.x();
  int   startY = clientRect.y();
  int   height = clientRect.height();
  int   segmentWidth = 0;

  if (maxValue > minValue) {
    scaleFactor = clientRect.width() / (double)(maxValue - minValue);
  } else {
    scaleFactor = 1.0;
  }

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

      cutIn    = item.cutInIndex();
      cutOut   = item.cutOutIndex();
      startX   = clientRect.x() + (int)(cutIn * scaleFactor);
      segmentWidth = (int)((cutOut - cutIn) * scaleFactor);

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
    mAVDataItem      = 0;
    minValue         = 0;
    maxValue         = 1;
    isControlEnabled = false;
    update();
    return;
  }

  minValue         = 0;
  maxValue         = avDataItem->videoStream()->frameCount() - 1;
  if (maxValue < 1) maxValue = 1;
  mAVDataItem      = avDataItem;
  isControlEnabled = true;

  update();
}


