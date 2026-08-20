/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#ifndef TTSTREAMPOINTWIDGET_H
#define TTSTREAMPOINTWIDGET_H

#include <QWidget>
#include <QList>

class QListView;
class QTabWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class TTStreamPointModel;
class TTAVItem;

class TTStreamPointWidget : public QWidget
{
  Q_OBJECT

public:
  TTStreamPointWidget(TTStreamPointModel* model, QWidget* parent = 0);

  void setAnalysisRunning(bool running, bool aborted = false);

  //! Current AV item, injected by TTCutMainWindow whenever it changes
  //! (onAVItemChanged/closeProject). Needed only for the AudioAnomaly
  //! context-menu entries (audio-anomaly-repair Task 7): which AC3 track a
  //! repair belongs to and whether one already covers a given marker.
  //! nullptr while no project is open, in which case those entries are
  //! omitted entirely.
  void setAVItem(TTAVItem* avItem) { mpAvItem = avItem; }

  //! Same list TTAVData::extraFrameIndices() exposes (MPEG-2 field-picture
  //! extras), injected alongside setAVItem() so the AudioAnomaly repair
  //! dialog can correctly invert TTAudioAnomalyScanTask::videoFrameForTime()
  //! (audio-anomaly-repair Task 7 review fix 1 - ignoring this can be off
  //! by seconds on real DVB material). Empty is fine (no extras/no item).
  void setExtraFrameIndices(const QList<int>& extras) { mExtraFrameIndices = extras; }

signals:
  void analyzeRequested();
  void abortRequested();
  void settingsRequested();
  void jumpToFrame(int frameIndex);
  void deleteRequested(int row);
  void deleteAllRequested();
  void setCutIn(int frameIndex);
  void setCutOut(int frameIndex);

private slots:
  void onAnalyzeClicked();
  void onDeleteAllClicked();
  void onItemDoubleClicked(const QModelIndex& index);
  void onContextMenu(const QPoint& pos);
  void onDeleteKey();

public:

private:
  void setupLandezonenTab(QWidget* tab);

  TTStreamPointModel* mModel;
  QListView*          mListView;
  QPushButton*        mBtnAnalyze;
  QPushButton*        mBtnDeleteAll;
  QLabel*             mLblStatus;
  bool                mAnalysisRunning;
  TTAVItem*           mpAvItem = nullptr;
  QList<int>          mExtraFrameIndices;

  // Settings widgets
};

#endif // TTSTREAMPOINTWIDGET_H
