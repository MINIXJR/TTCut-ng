/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttstreampointwidget.h"
#include "ttthemedicon.h"
#include "ttaudiorepairdialog.h"
#include "../data/ttstreampointmodel.h"
#include "../data/ttavlist.h"
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
  btnSettings->setIcon(ttThemedIcon("preferences-system", QStyle::SP_ComputerIcon));
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
  const QModelIndex index = mListView->indexAt(pos);

  QMenu menu(this);
  ContextMenuActions acts;
  buildContextMenu(menu, index, acts);
  if (menu.isEmpty()) return;

  const QAction* chosen = menu.exec(mListView->viewport()->mapToGlobal(pos));
  handleContextAction(chosen, index, acts);
}

QStringList TTStreamPointWidget::contextMenuTextsForTest(int row)
{
  QMenu menu;
  ContextMenuActions acts;
  buildContextMenu(menu, mModel->index(row, 0), acts);
  QStringList texts;
  for (const QAction* a : menu.actions())
    if (!a->isSeparator()) texts << a->text();
  return texts;
}

void TTStreamPointWidget::buildContextMenu(QMenu& menu, const QModelIndex& index, ContextMenuActions& acts)
{
  if (index.isValid()) {
    acts.frameIndex = mModel->data(index, TTStreamPointModel::FrameIndexRole).toInt();
    acts.actDelete = menu.addAction(tr("Delete"));
    menu.addSeparator();
    acts.actCutIn = menu.addAction(tr("Set as Cut-In"));
    acts.actCutOut = menu.addAction(tr("Set as Cut-Out"));

    // Audio repair (audio-anomaly-repair Task 7): only for AudioAnomaly
    // markers, and only once an AC3 track and a current AVItem exist (the
    // marker widget has neither immediately after project close).
    auto type = static_cast<StreamPointType>(mModel->data(index, TTStreamPointModel::TypeRole).toInt());
    if (type == StreamPointType::AudioAnomaly && mpAvItem) {
      acts.repairTrackIndex = mpAvItem->firstAc3TrackIndex();
      if (acts.repairTrackIndex >= 0) {
        const TTStreamPoint point = mModel->pointAt(index.row());
        acts.repairIndex = TTAudioRepairDialog::repairIndexForMarker(
            mpAvItem, point, mExtraFrameIndices);

        // An existing repair can always be edited and removed - the marker
        // is the only handle on it. A new one is not offered for every kind
        // of anomaly marker (TTStreamPoint::offersNewAudioRepair).
        // A hole is filled from a second track: that repair needs a donor
        // candidate, to plan it and to edit it.
        const bool donorThere = !mpAvItem->donorCandidateTracks(acts.repairTrackIndex).isEmpty();
        if (acts.repairIndex >= 0) {
          const bool fill = mpAvItem->audioRepairList().at(acts.repairIndex).isDonorFill();
          menu.addSeparator();
          if (!fill || donorThere)
            acts.actEditRepair = menu.addAction(tr("Edit repair..."));
          acts.actRemoveRepair = menu.addAction(tr("Remove repair"));
        } else if (point.offersNewAudioRepair()
                   || (point.audioAnomalyKind() == AudioAnomalyKind::Hole && donorThere)) {
          menu.addSeparator();
          acts.actRepair = menu.addAction(tr("Repair..."));
        }
      }
    }
    menu.addSeparator();
  }

  if (mModel->rowCount() > 0) {
    acts.actDeleteAll = menu.addAction(tr("Delete all"));
  }
}

void TTStreamPointWidget::handleContextAction(const QAction* chosen, const QModelIndex& index,
                                              const ContextMenuActions& acts)
{
  // A menu closed without a choice. The actions a menu does not offer are
  // null as well, so without this the first such comparison below would
  // match - it opened the repair dialog on every marker.
  if (!chosen) return;

  if (chosen == acts.actDelete) {
    emit deleteRequested(index.row());
  } else if (chosen == acts.actCutIn) {
    emit setCutIn(acts.frameIndex);
  } else if (chosen == acts.actCutOut) {
    emit setCutOut(acts.frameIndex);
  } else if (chosen == acts.actRepair || chosen == acts.actEditRepair) {
    const TTStreamPoint pt = mModel->pointAt(index.row());
    // The dialog is modal: show the picture of the spot before it opens (a
    // right click does not navigate).
    emit jumpToFrame(pt.frameIndex());
    TTAudioRepairDialog dlg(mpAvItem, pt, acts.repairTrackIndex, mExtraFrameIndices, this);
    if (dlg.exec() == QDialog::Accepted) {
      QString desc = pt.description();
      // A fill that was disabled and is planned anew is no longer disabled.
      TTStreamPoint::stripSuffixVariant(desc, TTStreamPoint::repairDisabledSuffixVariants());
      // Check every known-language variant (residuals R6) - a marker
      // reloaded from a project saved in a different UI language already
      // carries a suffix tr() in THIS session would not recognize.
      if (!TTStreamPoint::hasSuffixVariant(desc, TTStreamPoint::repairPlannedSuffixVariants()))
        desc += tr(" (repair planned)");
      mModel->setDescriptionAt(index.row(), desc);
    }
  } else if (chosen == acts.actRemoveRepair) {
    mpAvItem->removeAudioRepairAt(acts.repairIndex);
    QString desc = mModel->pointAt(index.row()).description();
    TTStreamPoint::stripSuffixVariant(desc, TTStreamPoint::repairPlannedSuffixVariants());
    mModel->setDescriptionAt(index.row(), desc);
  } else if (chosen == acts.actDeleteAll) {
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

void TTStreamPointWidget::setAVItem(TTAVItem* avItem)
{
  if (mpAvItem == avItem) return;
  if (mpAvItem) disconnect(mpAvItem, &TTAVItem::audioRepairsChanged, this, &TTStreamPointWidget::refreshRepairSuffixes);
  mpAvItem = avItem;
  if (mpAvItem) connect(mpAvItem, &TTAVItem::audioRepairsChanged, this, &TTStreamPointWidget::refreshRepairSuffixes);
}

void TTStreamPointWidget::refreshRepairSuffixes()
{
  if (!mpAvItem) return;
  const QList<TTAudioRepairItem> repairs = mpAvItem->audioRepairList();
  for (int row = 0; row < mModel->rowCount(); ++row) {
    const TTStreamPoint point = mModel->pointAt(row);
    const int index = TTAudioRepairDialog::repairIndexForMarker(mpAvItem, point, mExtraFrameIndices);
    if (index < 0 || repairs.at(index).isEnabled()) continue;
    QString desc = point.description();
    if (TTStreamPoint::hasSuffixVariant(desc, TTStreamPoint::repairDisabledSuffixVariants())) continue;
    TTStreamPoint::stripSuffixVariant(desc, TTStreamPoint::repairPlannedSuffixVariants());
    mModel->setDescriptionAt(row, desc + disabledRepairSuffix(mpAvItem, repairs.at(index)));
  }
}

QString TTStreamPointWidget::disabledRepairSuffix(const TTAVItem* item, const TTAudioRepairItem& repair)
{
  if (repair.isDonorFill() && (!item || !item->donorCandidateTracks(repair.trackIndex()).contains(repair.donorTrack())))
    return tr(" (repair DISABLED - its donor track is missing)");
  return tr(" (repair DISABLED - it no longer fits the audio file)");
}
