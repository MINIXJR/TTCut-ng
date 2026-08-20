/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampointwidget.h"
#include "ttaudiorepairdialog.h"
#include "../data/ttstreampointmodel.h"
#include "../data/ttavlist.h"
#include "../data/ttaudiorepairitem.h"
#include "../avstream/ttavstream.h"
#include "../common/ttcut.h"

#include <QListView>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStyle>
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

  // The tab bar used to label the list; with the settings gone there is only
  // one page left, and an unlabelled list would not say what it holds.
  //
  // A QLabel with a stylesheet rather than a QGroupBox: the application pins
  // QGroupBox titles to the centre via a global stylesheet in main(), which
  // would look wrong for a left-aligned heading. No colour is hardcoded -
  // emphasis comes from font weight only, so it renders correctly on light
  // and dark themes alike.
  QHBoxLayout* headingRow = new QHBoxLayout();
  headingRow->setContentsMargins(0, 0, 0, 0);

  QLabel* heading = new QLabel(tr("Stream Points"), this);
  heading->setStyleSheet("QLabel { font-weight: bold; }");
  headingRow->addWidget(heading);
  headingRow->addStretch(1);

  // Settings live in the central dialog now. A small icon button here rather
  // than a third push button in the row below: the navigation column is only
  // 280 px wide, and three labelled buttons no longer fit side by side.
  // Same icon as the Settings menu action, so it reads as the same thing.
  QToolButton* btnSettings = new QToolButton(this);
  btnSettings->setIcon(QIcon::fromTheme("preferences-system",
                       QApplication::style()->standardIcon(QStyle::SP_ComputerIcon)));
  btnSettings->setAutoRaise(true);
  btnSettings->setToolTip(tr("Opens the detection settings in the settings dialog."));
  connect(btnSettings, &QToolButton::clicked, this, &TTStreamPointWidget::settingsRequested);
  headingRow->addWidget(btnSettings);

  mainLayout->addLayout(headingRow);

  QWidget* landezonenTab = new QWidget();
  setupLandezonenTab(landezonenTab);
  mainLayout->addWidget(landezonenTab, 1);
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

  // Status label. No hardcoded colour - #666 used to make the cancelled-run
  // message nearly invisible on a dark theme. Bold weight (not a fixed
  // colour) keeps it distinguishable from surrounding form text on any
  // theme, and reads as a warning rather than a whisper.
  mLblStatus = new QLabel(tab);
  mLblStatus->setStyleSheet("QLabel { font-weight: bold; }");
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
    // Nothing to save here: the detection parameters live in TTSettings and
    // are written by the settings dialog, not by this widget.
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
  QAction* actRepair = nullptr;
  QAction* actEditRepair = nullptr;
  QAction* actRemoveRepair = nullptr;
  int frameIndex = 0;
  int repairTrackIndex = -1;
  int repairIndex = -1;

  if (index.isValid()) {
    frameIndex = mModel->data(index, TTStreamPointModel::FrameIndexRole).toInt();
    actDelete = menu.addAction(tr("Delete"));
    menu.addSeparator();
    actCutIn = menu.addAction(tr("Set as Cut-In"));
    actCutOut = menu.addAction(tr("Set as Cut-Out"));

    // Audio repair (audio-anomaly-repair Task 7): only for AudioAnomaly
    // markers, and only once an AC3 track and a current AVItem exist (the
    // marker widget has neither immediately after project close).
    auto type = static_cast<StreamPointType>(mModel->data(index, TTStreamPointModel::TypeRole).toInt());
    if (type == StreamPointType::AudioAnomaly && mpAvItem) {
      repairTrackIndex = mpAvItem->firstAc3TrackIndex();
      if (repairTrackIndex >= 0) {
        const TTStreamPoint pt = mModel->pointAt(index.row());
        const double frameRate = mpAvItem->videoStream() ? mpAvItem->videoStream()->frameRate() : 25.0;
        qint64 approxFrom = 0, approxTo = 0;
        TTAudioRepairDialog::approxAc3RangeForMarker(pt, frameRate, mExtraFrameIndices, approxFrom, approxTo);

        const QList<TTAudioRepairItem> repairs = mpAvItem->audioRepairList();
        for (int i = 0; i < repairs.size(); ++i) {
          const TTAudioRepairItem& r = repairs.at(i);
          if (r.trackIndex() != repairTrackIndex) continue;
          if (r.frameTo() < approxFrom || r.frameFrom() > approxTo) continue; // no overlap
          repairIndex = i;
          break;
        }

        menu.addSeparator();
        if (repairIndex >= 0) {
          actEditRepair = menu.addAction(tr("Edit repair..."));
          actRemoveRepair = menu.addAction(tr("Remove repair"));
        } else {
          actRepair = menu.addAction(tr("Repair..."));
        }
      }
    }
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
  } else if (chosen == actRepair || chosen == actEditRepair) {
    const TTStreamPoint pt = mModel->pointAt(index.row());
    TTAudioRepairDialog dlg(mpAvItem, pt, repairTrackIndex, mExtraFrameIndices, this);
    connect(&dlg, &TTAudioRepairDialog::jumpToFrameRequested, this, &TTStreamPointWidget::jumpToFrame);
    if (dlg.exec() == QDialog::Accepted) {
      QString desc = pt.description();
      // Check every known-language variant (residuals R6) - a marker
      // reloaded from a project saved in a different UI language already
      // carries a suffix tr() in THIS session would not recognize.
      bool alreadyPlanned = false;
      for (const QString& v : TTStreamPoint::repairPlannedSuffixVariants())
        if (desc.endsWith(v)) { alreadyPlanned = true; break; }
      if (!alreadyPlanned) desc += tr(" (repair planned)");
      mModel->setDescriptionAt(index.row(), desc);
    }
  } else if (chosen == actRemoveRepair) {
    mpAvItem->removeAudioRepairAt(repairIndex);
    QString desc = mModel->pointAt(index.row()).description();
    for (const QString& v : TTStreamPoint::repairPlannedSuffixVariants())
      if (desc.endsWith(v)) { desc.chop(v.length()); break; }
    mModel->setDescriptionAt(index.row(), desc);
  } else if (chosen == actDeleteAll) {
    emit deleteAllRequested();
  }
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
