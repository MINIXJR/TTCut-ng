/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttstreampointwidget.h"
#include "../data/ttstreampointmodel.h"
#include "../common/ttcut.h"

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
  mTabWidget->addTab(landezonenTab, tr("Landezonen"));

  QWidget* settingsTab = new QWidget();
  setupSettingsTab(settingsTab);
  mTabWidget->addTab(settingsTab, tr("Einstellungen"));

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

  connect(mListView, SIGNAL(doubleClicked(const QModelIndex&)),
          this, SLOT(onItemDoubleClicked(const QModelIndex&)));
  connect(mListView, SIGNAL(customContextMenuRequested(const QPoint&)),
          this, SLOT(onContextMenu(const QPoint&)));

  // Delete key shortcut
  QShortcut* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), mListView);
  deleteShortcut->setContext(Qt::WidgetShortcut);
  connect(deleteShortcut, SIGNAL(activated()), this, SLOT(onDeleteKey()));

  // Status label
  mLblStatus = new QLabel(tab);
  mLblStatus->setStyleSheet("QLabel { color: #666; font-style: italic; }");
  mLblStatus->hide();
  layout->addWidget(mLblStatus);

  // Buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(4);

  mBtnAnalyze = new QPushButton(tr("Analyse starten"), tab);
  connect(mBtnAnalyze, SIGNAL(clicked()), this, SLOT(onAnalyzeClicked()));
  btnLayout->addWidget(mBtnAnalyze);

  mBtnDeleteAll = new QPushButton(tr("Delete all"), tab);
  connect(mBtnDeleteAll, SIGNAL(clicked()), this, SLOT(onDeleteAllClicked()));
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
  mCbSilence = new QCheckBox(tr("Stille"), tab);
  gl->addWidget(mCbSilence, row, 0, 1, 2);
  row++;

  gl->addWidget(new QLabel(tr("Schwellwert (dB):"), tab), row, 0);
  mSbSilenceThreshold = new QSpinBox(tab);
  mSbSilenceThreshold->setRange(-80, -20);
  mSbSilenceThreshold->setSuffix(" dB");
  gl->addWidget(mSbSilenceThreshold, row, 1);
  row++;

  gl->addWidget(new QLabel(tr("Mindestdauer (s):"), tab), row, 0);
  mSbSilenceMinDuration = new QDoubleSpinBox(tab);
  mSbSilenceMinDuration->setRange(0.1, 5.0);
  mSbSilenceMinDuration->setSingleStep(0.1);
  mSbSilenceMinDuration->setDecimals(1);
  mSbSilenceMinDuration->setSuffix(" s");
  gl->addWidget(mSbSilenceMinDuration, row, 1);
  row++;

  // Audio change detection
  mCbAudioChange = new QCheckBox(tr("Audioformatwechsel"), tab);
  gl->addWidget(mCbAudioChange, row, 0, 1, 2);
  row++;

  // Aspect ratio change detection (MPEG-2 only)
  mCbAspectChange = new QCheckBox(tr("Aspect ratio (4:3/16:9)"), tab);
  gl->addWidget(mCbAspectChange, row, 0, 1, 2);
  row++;

  // Vertical spacer
  gl->setRowStretch(row, 1);
}

void TTStreamPointWidget::loadSettings()
{
  mCbSilence->setChecked(TTCut::spDetectSilence);
  mSbSilenceThreshold->setValue(TTCut::spSilenceThresholdDb);
  mSbSilenceMinDuration->setValue(TTCut::spSilenceMinDuration);
  mCbAudioChange->setChecked(TTCut::spDetectAudioChange);
  mCbAspectChange->setChecked(TTCut::spDetectAspectChange);
}

void TTStreamPointWidget::saveSettings()
{
  TTCut::spDetectSilence      = mCbSilence->isChecked();
  TTCut::spSilenceThresholdDb = mSbSilenceThreshold->value();
  TTCut::spSilenceMinDuration = mSbSilenceMinDuration->value();
  TTCut::spDetectAudioChange  = mCbAudioChange->isChecked();
  TTCut::spDetectAspectChange = mCbAspectChange->isChecked();
}

void TTStreamPointWidget::setAnalysisRunning(bool running)
{
  mAnalysisRunning = running;

  if (running) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    mBtnAnalyze->setText(tr("Abbrechen"));
    mLblStatus->setText(tr("Analyse l\303\244uft..."));
    mLblStatus->show();
  } else {
    QApplication::restoreOverrideCursor();
    mBtnAnalyze->setText(tr("Analyse starten"));
    int count = mModel->rowCount();
    if (count > 0) {
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
