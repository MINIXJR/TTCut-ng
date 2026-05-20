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
// *** TTSTREAMNAVIGATOR
// ----------------------------------------------------------------------------

#ifndef TTSTREAMNAVIGATOR_H
#define TTSTREAMNAVIGATOR_H

#include "ui_streamnavigationwidget.h"

class TTAVItem;

class TTStreamNavigator : public QWidget, public Ui::TTStreamNavigatorWidget
{
  Q_OBJECT

  public:
    TTStreamNavigator(QWidget* parent);

    void setTitle(const QString& title);
    void controlEnabled(bool enabled);

    QSlider* slider();

  public slots:
    void onNewSliderValue(int value);
    void onSliderMoved(int value);
    void onRefreshDisplay();
    void onAVItemChanged(TTAVItem* avDataItem);

  signals:
    void sliderValueChanged(int value);
};

#endif //TTSTREAMNAVIGATOR_H
