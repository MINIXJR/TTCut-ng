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
// TTSTREAMNAVIGATOR
// ----------------------------------------------------------------------------

#include "ttstreamnavigator.h"
#include "../avstream/ttavstream.h"
#include "../data/ttavlist.h"
#include "../common/ttsettings.h"

TTStreamNavigator::TTStreamNavigator(QWidget* parent)
  :QWidget(parent)
{
  setupUi( this );

  connect(videoSlider, &QAbstractSlider::valueChanged, this, &TTStreamNavigator::onNewSliderValue);
  // No sliderMoved -> processEvents() connection any more: it re-entered the
  // event loop in the middle of the synchronous per-event decode and pumped
  // further slider events into the queue. The main window debounces slider
  // positions now, which also keeps the UI painting during a drag.

  videoSlider->setPageStep(TTSettings::instance()->stepSliderClick());
  connect(TTSettings::instance(), &TTSettings::stepSliderClickChanged,
          this, [this](int v) { videoSlider->setPageStep(v); });
}

void TTStreamNavigator::setTitle(const QString&)
{
}

void TTStreamNavigator::controlEnabled(bool enabled)
{
  videoSlider->setEnabled(enabled);
  navigatorDisplay->controlEnabled(enabled);
  navigatorDisplay->repaint();
}

QSlider* TTStreamNavigator::slider()
{
  return videoSlider;
}

void TTStreamNavigator::onNewSliderValue(int val)
{
  emit sliderValueChanged(val);
}

void TTStreamNavigator::onRefreshDisplay()
{
  navigatorDisplay->repaint();
}

void TTStreamNavigator::onAVItemChanged(TTAVItem* avDataItem)
{
	if (avDataItem == 0) {
		controlEnabled(false);
		videoSlider->setMinimum(0);
		videoSlider->setMaximum(0);
		navigatorDisplay->onAVItemChanged(avDataItem);
		return;
	}

  videoSlider->setMinimum(0);
  videoSlider->setMaximum(avDataItem->videoStream()->frameCount()-1);

  navigatorDisplay->onAVItemChanged(avDataItem);
}
