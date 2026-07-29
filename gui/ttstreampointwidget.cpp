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

// Two heading levels group the detectors by section (Audio/Video) and, under
// each section, by which codecs the detector applies to (e.g. "MPEG-2 only"
// vs "All codecs"). The indentation used to imply that "Pillarbox detection"
// depended on the aspect-ratio check box; that dependency was removed in
// commit acd4b3e0, so the flat layout was left claiming something no longer
// true. The heading levels replace it with the real grouping: which detector
// applies under which codec.
//
// QLabel with a stylesheet is used for both heading levels rather than
// QGroupBox - the application pins QGroupBox titles to the centre via a
// global stylesheet in main(), which would look wrong for these left-aligned
// section headings. No colours are hardcoded; emphasis comes from font
// weight/size only, so both heading levels render correctly on light and
// dark themes alike.
namespace {

QLabel* addSectionHeading(QGridLayout* gl, int row, const QString& text, QWidget* tab)
{
  // Strongest emphasis level - marks the two top-level detector groups.
  // Displayed upper-case for visual weight; the translatable source string
  // itself stays normal-case (see trans/ttcut-ng_de_DE.ts).
  QLabel* lbl = new QLabel(text.toUpper(), tab);
  lbl->setStyleSheet("QLabel { font-weight: bold; font-size: 11pt; }");
  gl->addWidget(lbl, row, 0, 1, 2);
  return lbl;
}

QLabel* addSubHeading(QGridLayout* gl, int row, const QString& text, QWidget* tab)
{
  // One level down from the section heading - bold, but visibly weaker
  // (default point size, indented under its section).
  QLabel* lbl = new QLabel(text, tab);
  lbl->setStyleSheet("QLabel { font-weight: bold; padding-left: 10px; }");
  gl->addWidget(lbl, row, 0, 1, 2);
  return lbl;
}

// Indentation for the detector check boxes that belong to a codec
// sub-heading - one level in from the sub-heading itself. Matches the 20px
// indent already used for the pillarbox controls before this rework.
const char* kControlIndentStyle = "padding-left: 20px;";

// Indentation for the value fields that belong to a detector check box - one
// level in again, so a glance shows which detector a value configures. 40px
// puts the label roughly under the check box's text rather than under its
// indicator (20px padding + indicator width + spacing).
const char* kValueIndentStyle = "padding-left: 40px;";

} // namespace

void TTStreamPointWidget::setupSettingsTab(QWidget* tab)
{
  QGridLayout* gl = new QGridLayout(tab);
  gl->setContentsMargins(4, 4, 4, 4);
  gl->setSpacing(4);
  int row = 0;

  // ---------------------------------------------------------------- Audio --
  addSectionHeading(gl, row++, tr("Audio"), tab);

  // Both current audio detectors are format-independent; this sub-heading is
  // a deliberate placeholder for a future mp2/EAC3 distinction.
  addSubHeading(gl, row++, tr("All formats"), tab);

  // Silence detection
  mCbSilence = new QCheckBox(tr("Silence"), tab);
  mCbSilence->setStyleSheet(kControlIndentStyle);
  mCbSilence->setToolTip(tr("Finds passages quieter than the threshold for at least the minimum duration."));
  gl->addWidget(mCbSilence, row, 0, 1, 2);
  row++;

  mLblSilenceThreshold = new QLabel(tr("Threshold (dB):"), tab);
  mLblSilenceThreshold->setStyleSheet(kValueIndentStyle);
  gl->addWidget(mLblSilenceThreshold, row, 0);
  mSbSilenceThreshold = new QSpinBox(tab);
  mSbSilenceThreshold->setRange(-80, -20);
  mSbSilenceThreshold->setSuffix(" dB");
  gl->addWidget(mSbSilenceThreshold, row, 1);
  row++;

  mLblSilenceMinDuration = new QLabel(tr("Minimum duration (s):"), tab);
  mLblSilenceMinDuration->setStyleSheet(kValueIndentStyle);
  gl->addWidget(mLblSilenceMinDuration, row, 0);
  mSbSilenceMinDuration = new QDoubleSpinBox(tab);
  mSbSilenceMinDuration->setRange(0.1, 5.0);
  mSbSilenceMinDuration->setSingleStep(0.1);
  mSbSilenceMinDuration->setDecimals(1);
  mSbSilenceMinDuration->setSuffix(" s");
  gl->addWidget(mSbSilenceMinDuration, row, 1);
  row++;

  // The silence sub-controls belong to the silence check box - same defect
  // and same fix as the pillarbox controls (commit a7b9d2bf): wire both the
  // label and the spin box, and setChecked() in loadSettings() does not emit
  // toggled() when the value does not change, so the enabled state is also
  // synced explicitly there.
  connect(mCbSilence, &QCheckBox::toggled, mSbSilenceThreshold,   &QWidget::setEnabled);
  connect(mCbSilence, &QCheckBox::toggled, mLblSilenceThreshold,  &QWidget::setEnabled);
  connect(mCbSilence, &QCheckBox::toggled, mSbSilenceMinDuration, &QWidget::setEnabled);
  connect(mCbSilence, &QCheckBox::toggled, mLblSilenceMinDuration,&QWidget::setEnabled);

  // Audio change detection
  mCbAudioChange = new QCheckBox(tr("Audio format change"), tab);
  mCbAudioChange->setStyleSheet(kControlIndentStyle);
  mCbAudioChange->setToolTip(tr("Finds points where the audio format changes (e.g. stereo to 5.1)."));
  gl->addWidget(mCbAudioChange, row, 0, 1, 2);
  row++;

  // ---------------------------------------------------------------- Video --
  addSectionHeading(gl, row++, tr("Video"), tab);

  addSubHeading(gl, row++, tr("MPEG-2 only"), tab);

  // Aspect ratio change detection. "MPEG-2 only" now lives in the
  // sub-heading above, not in this label.
  mCbAspectChange = new QCheckBox(tr("Aspect ratio (4:3/16:9)"), tab);
  mCbAspectChange->setStyleSheet(kControlIndentStyle);
  mCbAspectChange->setToolTip(tr("Reads the aspect ratio from MPEG-2 sequence headers. Has no effect on H.264/H.265, which carry no such headers."));
  gl->addWidget(mCbAspectChange, row, 0, 1, 2);
  row++;

  addSubHeading(gl, row++, tr("All codecs"), tab);

  // Pillarbox detection. Its own group, not a sub-option of the aspect check
  // box above: the aspect check box drives header-based detection only
  // (MPEG-2) and must not gate pillarbox detection - that coupling made
  // pillarbox detection unreachable for anyone who switched the MPEG-2-only
  // option off.
  mCbPillarbox = new QCheckBox(tr("Pillarbox detection (4:3 in 16:9)"), tab);
  mCbPillarbox->setStyleSheet(kControlIndentStyle);
  mCbPillarbox->setToolTip(tr("Detects 4:3 content inside a 16:9 frame by measuring the black bars. Works for MPEG-2, H.264 and H.265."));
  gl->addWidget(mCbPillarbox, row, 0, 1, 2);
  row++;

  mLblPillarboxThreshold = new QLabel(tr("Threshold (luminance):"), tab);
  mLblPillarboxThreshold->setStyleSheet(kValueIndentStyle);
  gl->addWidget(mLblPillarboxThreshold, row, 0);
  mSbPillarboxThreshold = new QSpinBox(tab);
  mSbPillarboxThreshold->setRange(5, 50);
  mSbPillarboxThreshold->setToolTip(tr("A column counts as black below this brightness. Higher values detect more, but risk false hits in dark scenes."));
  gl->addWidget(mSbPillarboxThreshold, row, 1);
  row++;

  mLblPillarboxSample = new QLabel(tr("Sample distance (s):"), tab);
  mLblPillarboxSample->setStyleSheet(kValueIndentStyle);
  gl->addWidget(mLblPillarboxSample, row, 0);
  mSbPillarboxSampleSeconds = new QDoubleSpinBox(tab);
  mSbPillarboxSampleSeconds->setRange(0.2, 10.0);
  mSbPillarboxSampleSeconds->setSingleStep(0.1);
  mSbPillarboxSampleSeconds->setDecimals(1);
  mSbPillarboxSampleSeconds->setSuffix(" s");
  mSbPillarboxSampleSeconds->setToolTip(tr("Distance between analysed frames. Smaller is slower; the reported position stays frame-exact either way."));
  gl->addWidget(mSbPillarboxSampleSeconds, row, 1);
  row++;

  // The pillarbox sub-controls belong to the pillarbox check box - see the
  // comment above mCbPillarbox for why it is not gated by the aspect check
  // box instead.
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
  // Sync enabled state. setChecked() above does not emit toggled() when the
  // value does not change (e.g. it is already false on the very first load),
  // so the sub-controls' enabled state is not guaranteed to follow purely
  // from the toggled() connections made in setupSettingsTab() - it has to be
  // set explicitly here for both detectors.
  mSbSilenceThreshold->setEnabled(mCbSilence->isChecked());
  mLblSilenceThreshold->setEnabled(mCbSilence->isChecked());
  mSbSilenceMinDuration->setEnabled(mCbSilence->isChecked());
  mLblSilenceMinDuration->setEnabled(mCbSilence->isChecked());
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
