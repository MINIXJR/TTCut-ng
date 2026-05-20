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

#ifndef TTNAVIGATORDISPLAY_H
#define TTNAVIGATORDISPLAY_H

#include "ui_navigatordisplaywidget.h"

class TTAVItem;

class TTNavigatorDisplay : public QFrame, public Ui::TTNavigatorDisplayWidget
{
  Q_OBJECT

  public:
    TTNavigatorDisplay(QWidget* parent);

    void controlEnabled(bool enabled);

  public slots:
    void onAVItemChanged(TTAVItem* avItem);

  protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent* event);
    void drawCutList();

  private:
    TTAVItem* mAVDataItem;
    bool      isControlEnabled;
    int       minValue;
    int       maxValue;
    double    scaleFactor;
};

#endif //TTNAVIGATORDISPLAY_H
