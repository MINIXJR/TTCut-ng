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

class QListView;
class QTabWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class TTStreamPointModel;

class TTStreamPointWidget : public QWidget
{
  Q_OBJECT

public:
  TTStreamPointWidget(TTStreamPointModel* model, QWidget* parent = 0);

  void setAnalysisRunning(bool running, bool aborted = false);

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

  // Settings widgets
};

#endif // TTSTREAMPOINTWIDGET_H
