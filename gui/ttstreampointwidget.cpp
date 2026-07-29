/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampointwidget.h"
#include "../data/ttstreampointmodel.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"

#include <QListView>
#include <QTabWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QKeyEvent>
#include <QApplication>
#include <QCursor>

TTStreamPointWidget::TTStreamPointWidget(TTStreamPointModel* model, QWidget* parent)
  : QWidget(parent),
    mModel(model),
    mAnalysisRunning(false)
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(2);

  mTabWidget = new QTabWidget(this);

  QWidget* landezonenTab = new QWidget();
  setupLandezonenTab(landezonenTab);
  mTabWidget->addTab(landezonenTab, tr("Stream Points"));

  QWidget* settingsTab = new QWidget();
  setupSettingsTab(settingsTab);
  mTabWidget->addTab(settingsTab, tr("Settings"));

  mainLayout->addWidget(mTabWidget);

  loadSettings();
}

void TTStreamPointWidget::setupLandezonenTab(QWidget* tab)
{
  QVBoxLayout* layout = new QVBoxLayout(tab);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  // List view
  mListView = new QListView(tab);
  mListView->setModel(mModel);
  mListView->setSelectionMode(QAbstractItemView::SingleSelection);
  mListView->setContextMenuPolicy(Qt::CustomContextMenu);
  mListView->setAlternatingRowColors(true);
  layout->addWidget(mListView, 1);

  connect(mListView, &QListView::doubleClicked,
          this, &TTStreamPointWidget::onItemDoubleClicked);
  connect(mListView, &QListView::customContextMenuRequested,
          this, &TTStreamPointWidget::onContextMenu);

  // Delete key shortcut
  QShortcut* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), mListView);
  deleteShortcut->setContext(Qt::WidgetShortcut);
  connect(deleteShortcut, &QShortcut::activated, this, &TTStreamPointWidget::onDeleteKey);

  // Status label
  mLblStatus = new QLabel(tab);
  mLblStatus->setStyleSheet("QLabel { color: #666; font-style: italic; }");
  mLblStatus->hide();
  layout->addWidget(mLblStatus);

  // Buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(4);

  mBtnAnalyze = new QPushButton(tr("Start analysis"), tab);
  connect(mBtnAnalyze, &QPushButton::clicked, this, &TTStreamPointWidget::onAnalyzeClicked);
  btnLayout->addWidget(mBtnAnalyze);

  mBtnDeleteAll = new QPushButton(tr("Delete all"), tab);
  connect(mBtnDeleteAll, &QPushButton::clicked, this, &TTStreamPointWidget::onDeleteAllClicked);
  btnLayout->addWidget(mBtnDeleteAll);

  layout->addLayout(btnLayout);
}

void TTStreamPointWidget::setupSettingsTab(QWidget* tab)
{
  QGridLayout* gl = new QGridLayout(tab);
  gl->setContentsMargins(4, 4, 4, 4);
  gl->setSpacing(4);
  int row = 0;

  // Silence detection
  mCbSilence = new QCheckBox(tr("Silence"), tab);
  gl->addWidget(mCbSilence, row, 0, 1, 2);
  row++;

  gl->addWidget(new QLabel(tr("Threshold (dB):"), tab), row, 0);
  mSbSilenceThreshold = new QSpinBox(tab);
  mSbSilenceThreshold->setRange(-80, -20);
  mSbSilenceThreshold->setSuffix(" dB");
  gl->addWidget(mSbSilenceThreshold, row, 1);
  row++;

  gl->addWidget(new QLabel(tr("Minimum duration (s):"), tab), row, 0);
  mSbSilenceMinDuration = new QDoubleSpinBox(tab);
  mSbSilenceMinDuration->setRange(0.1, 5.0);
  mSbSilenceMinDuration->setSingleStep(0.1);
  mSbSilenceMinDuration->setDecimals(1);
  mSbSilenceMinDuration->setSuffix(" s");
  gl->addWidget(mSbSilenceMinDuration, row, 1);
  row++;

  // Audio change detection
  mCbAudioChange = new QCheckBox(tr("Audio format change"), tab);
  gl->addWidget(mCbAudioChange, row, 0, 1, 2);
  row++;

  // Aspect ratio change detection (MPEG-2 only)
  mCbAspectChange = new QCheckBox(tr("Aspect ratio (4:3/16:9, MPEG-2 only)"), tab);
  gl->addWidget(mCbAspectChange, row, 0, 1, 2);
  row++;

  // Pillarbox detection (sub-option of aspect ratio)
  mCbPillarbox = new QCheckBox(tr("Pillarbox detection (4:3 in 16:9)"), tab);
  mCbPillarbox->setStyleSheet("QCheckBox { padding-left: 20px; }");
  gl->addWidget(mCbPillarbox, row, 0, 1, 2);
  row++;

  mLblPillarboxThreshold = new QLabel(tr("Threshold (luminance):"), tab);
  mLblPillarboxThreshold->setStyleSheet("QLabel { padding-left: 20px; }");
  gl->addWidget(mLblPillarboxThreshold, row, 0);
  mSbPillarboxThreshold = new QSpinBox(tab);
  mSbPillarboxThreshold->setRange(5, 50);
  gl->addWidget(mSbPillarboxThreshold, row, 1);
  row++;

  mLblPillarboxSample = new QLabel(tr("Sample distance (s):"), tab);
  mLblPillarboxSample->setStyleSheet("QLabel { padding-left: 20px; }");
  gl->addWidget(mLblPillarboxSample, row, 0);
  mSbPillarboxSampleSeconds = new QDoubleSpinBox(tab);
  mSbPillarboxSampleSeconds->setRange(0.2, 10.0);
  mSbPillarboxSampleSeconds->setSingleStep(0.1);
  mSbPillarboxSampleSeconds->setDecimals(1);
  mSbPillarboxSampleSeconds->setSuffix(" s");
  gl->addWidget(mSbPillarboxSampleSeconds, row, 1);
  row++;

  // The pillarbox sub-controls belong to the pillarbox check box. The aspect
  // check box drives header-based detection only (MPEG-2) and must not gate
  // them - that coupling made pillarbox detection unreachable for anyone who
  // switched the MPEG-2-only option off.
  connect(mCbPillarbox, &QCheckBox::toggled, mSbPillarboxThreshold,      &QWidget::setEnabled);
  connect(mCbPillarbox, &QCheckBox::toggled, mLblPillarboxThreshold,     &QWidget::setEnabled);
  connect(mCbPillarbox, &QCheckBox::toggled, mSbPillarboxSampleSeconds,  &QWidget::setEnabled);
  connect(mCbPillarbox, &QCheckBox::toggled, mLblPillarboxSample,        &QWidget::setEnabled);

  // Vertical spacer
  gl->setRowStretch(row, 1);
}

void TTStreamPointWidget::loadSettings()
{
  mCbSilence->setChecked(TTSettings::instance()->spDetectSilence());
  mSbSilenceThreshold->setValue(TTSettings::instance()->spSilenceThresholdDb());
  mSbSilenceMinDuration->setValue(TTSettings::instance()->spSilenceMinDuration());
  mCbAudioChange->setChecked(TTSettings::instance()->spDetectAudioChange());
  mCbAspectChange->setChecked(TTSettings::instance()->spDetectAspectChange());
  mCbPillarbox->setChecked(TTSettings::instance()->spDetectPillarbox());
  mSbPillarboxThreshold->setValue(TTSettings::instance()->spPillarboxThreshold());
  mSbPillarboxSampleSeconds->setValue(TTSettings::instance()->spPillarboxSampleSeconds());
  // Sync enabled state
  mSbPillarboxThreshold->setEnabled(mCbPillarbox->isChecked());
  mLblPillarboxThreshold->setEnabled(mCbPillarbox->isChecked());
  mSbPillarboxSampleSeconds->setEnabled(mCbPillarbox->isChecked());
  mLblPillarboxSample->setEnabled(mCbPillarbox->isChecked());
}

void TTStreamPointWidget::saveSettings()
{
  TTSettings::instance()->setSpDetectSilence(mCbSilence->isChecked());
  TTSettings::instance()->setSpSilenceThresholdDb(mSbSilenceThreshold->value());
  TTSettings::instance()->setSpSilenceMinDuration(mSbSilenceMinDuration->value());
  TTSettings::instance()->setSpDetectAudioChange(mCbAudioChange->isChecked());
  TTSettings::instance()->setSpDetectAspectChange(mCbAspectChange->isChecked());
  TTSettings::instance()->setSpDetectPillarbox(mCbPillarbox->isChecked());
  TTSettings::instance()->setSpPillarboxThreshold(mSbPillarboxThreshold->value());
  TTSettings::instance()->setSpPillarboxSampleSeconds(mSbPillarboxSampleSeconds->value());
}

void TTStreamPointWidget::setAnalysisRunning(bool running, bool aborted)
{
  mAnalysisRunning = running;

  if (running) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    mBtnAnalyze->setText(tr("Cancel"));
    mLblStatus->setText(tr("Analysis running..."));
    mLblStatus->show();
  } else {
    QApplication::restoreOverrideCursor();
    mBtnAnalyze->setText(tr("Start analysis"));
    int count = mModel->rowCount();
    if (aborted) {
      mLblStatus->setText(tr("Analysis cancelled - list incomplete"));
      mLblStatus->show();
    } else if (count > 0) {
      mLblStatus->setText(tr("%1 stream points detected").arg(count));
      mLblStatus->show();
    } else {
      mLblStatus->hide();
    }
  }
}

void TTStreamPointWidget::onAnalyzeClicked()
{
  if (mAnalysisRunning) {
    emit abortRequested();
  } else {
    saveSettings();
    emit analyzeRequested();
  }
}

void TTStreamPointWidget::onItemDoubleClicked(const QModelIndex& index)
{
  if (!index.isValid()) return;

  int frameIndex = mModel->data(index, TTStreamPointModel::FrameIndexRole).toInt();
  emit jumpToFrame(frameIndex);
}

void TTStreamPointWidget::onContextMenu(const QPoint& pos)
{
  QModelIndex index = mListView->indexAt(pos);

  QMenu menu(this);

  QAction* actDelete = nullptr;
  QAction* actCutIn = nullptr;
  QAction* actCutOut = nullptr;
  int frameIndex = 0;

  if (index.isValid()) {
    frameIndex = mModel->data(index, TTStreamPointModel::FrameIndexRole).toInt();
    actDelete = menu.addAction(tr("Delete"));
    menu.addSeparator();
    actCutIn = menu.addAction(tr("Set as Cut-In"));
    actCutOut = menu.addAction(tr("Set as Cut-Out"));
    menu.addSeparator();
  }

  QAction* actDeleteAll = nullptr;
  if (mModel->rowCount() > 0) {
    actDeleteAll = menu.addAction(tr("Delete all"));
  }

  if (menu.isEmpty()) return;

  QAction* chosen = menu.exec(mListView->viewport()->mapToGlobal(pos));
  if (chosen == actDelete) {
    emit deleteRequested(index.row());
  } else if (chosen == actCutIn) {
    emit setCutIn(frameIndex);
  } else if (chosen == actCutOut) {
    emit setCutOut(frameIndex);
  } else if (chosen == actDeleteAll) {
    emit deleteAllRequested();
  }
}

void TTStreamPointWidget::showLandezonenTab()
{
  if (mTabWidget) mTabWidget->setCurrentIndex(0);
}

void TTStreamPointWidget::showSettingsTab()
{
  if (mTabWidget) mTabWidget->setCurrentIndex(1);
}

void TTStreamPointWidget::onDeleteAllClicked()
{
  emit deleteAllRequested();
}

void TTStreamPointWidget::onDeleteKey()
{
  QModelIndex index = mListView->currentIndex();
  if (!index.isValid()) return;

  emit deleteRequested(index.row());
}
