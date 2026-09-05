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
// TTAUDIOFILELIST
// ----------------------------------------------------------------------------

#ifndef TTAUDIOFILELIST_H
#define TTAUDIOFILELIST_H

#include "ui_audiofilelistwidget.h"

#include "tttracktreeview.h"

class TTAVData;
class TTAVItem;
class TTAudioItem;

class QMenu;
class QAction;
class QComboBox;
class QSpinBox;

class TTAudioTreeView : public TTTrackTreeView, Ui::TTAudioFileListWidget
{
  Q_OBJECT

  public:
    explicit TTAudioTreeView(QWidget* parent=0);

  public slots:
    void onAVDataChanged(const TTAVItem* avData);
    void onAppendItem(const TTAudioItem& item);
    void onSwapItems(int oldIndex, int newIndex);
    void onReloadList(const TTAVItem* avData);

  private:
    const TTAVItem* mpAVItem;
};
#endif
