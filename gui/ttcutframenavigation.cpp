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
// TTCUTFRAMENAVIGATION
// ----------------------------------------------------------------------------

#include "ttcutframenavigation.h"
#include "ttthemedicon.h"
#include "../common/ttcut.h"
#include "../common/ttsettings.h"
#include "../data/ttavlist.h"
#include "../avstream/ttcommon.h"

#include <QApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QStyle>

TTCutFrameNavigation::TTCutFrameNavigation(QWidget* parent) :
  QWidget(parent)
{
  setupUi(this);

  // message logger instance
  log = TTMessageLogger::getInstance();

  isControlEnabled = true;
  isEditCut = false;
  editCutData = nullptr;
  isCutInPosition = false;
  isCutOutPosition = false;

  cutInPosition = -1;
  cutOutPosition = -1;
  markerPosition = -1;

  // Frame navigation buttons - video editing industry color scheme
  // I-Frame = Blue (key frames, "anchor" frames)
  pbPrevIFrame->setIcon(QIcon());
  pbPrevIFrame->setText(tr("◀ I"));
  pbPrevIFrame->setStyleSheet("QPushButton { color: #4488ff; font-weight: bold; }");
  pbNextIFrame->setIcon(QIcon());
  pbNextIFrame->setText(tr("I ▶"));
  pbNextIFrame->setStyleSheet("QPushButton { color: #4488ff; font-weight: bold; }");

  // P-Frame = Green (predicted/derived frames)
  pbPrevPFrame->setIcon(QIcon());
  pbPrevPFrame->setText(tr("◀ P"));
  pbPrevPFrame->setStyleSheet("QPushButton { color: #44cc44; font-weight: bold; }");
  pbNextPFrame->setIcon(QIcon());
  pbNextPFrame->setText(tr("P ▶"));
  pbNextPFrame->setStyleSheet("QPushButton { color: #44cc44; font-weight: bold; }");

  // B-Frame = Orange (bidirectional frames)
  pbPrevBFrame->setIcon(QIcon());
  pbPrevBFrame->setText(tr("◀ B"));
  pbPrevBFrame->setStyleSheet("QPushButton { color: #ff9933; font-weight: bold; }");
  pbNextBFrame->setIcon(QIcon());
  pbNextBFrame->setText(tr("B ▶"));
  pbNextBFrame->setStyleSheet("QPushButton { color: #ff9933; font-weight: bold; }");

  // Black Frame = Monitor icon with directional arrows
  pbPrevBlackFrame->setText(tr("◀"));
  pbPrevBlackFrame->setLayoutDirection(Qt::RightToLeft);  // arrow before icon
  pbNextBlackFrame->setText(tr("▶"));

  // Scene change navigation buttons - Cyan (#44cccc)
  pbPrevSceneChange->setIcon(QIcon());
  pbPrevSceneChange->setText(tr("\u25C0 \u25E7"));
  pbPrevSceneChange->setStyleSheet("QPushButton { color: #44cccc; font-weight: bold; }");
  pbNextSceneChange->setIcon(QIcon());
  pbNextSceneChange->setText(tr("\u25E7 \u25B6"));
  pbNextSceneChange->setStyleSheet("QPushButton { color: #44cccc; font-weight: bold; }");

  // Logo detection buttons - Magenta (#cc44cc)
  pbSelectLogoROI->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; font-size: 16px; }"
                                  "QPushButton:checked { background-color: #cc44cc; color: white; }");
  pbSelectLogoROI->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(pbSelectLogoROI, &QPushButton::customContextMenuRequested, this, &TTCutFrameNavigation::onLogoContextMenu);
  pbPrevLogo->setIcon(QIcon());
  pbPrevLogo->setText(tr("\u25C0 \u2B26"));  // ◀ ⬦
  pbPrevLogo->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; }");
  pbNextLogo->setIcon(QIcon());
  pbNextLogo->setText(tr("\u2B26 \u25B6"));  // ⬦ ▶
  pbNextLogo->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; }");

  // Set before wireSearch() connects the spin box, so the initial value
  // is not written back to the settings. (Black and scene are set by the
  // main window through setThresholds() after construction.)
  sbLogoThreshold->setValue(TTSettings::instance()->navLogoThreshold());

  mBlack = { pbPrevBlackFrame,  pbNextBlackFrame,  pbCancelBlackSearch, sbBlackThreshold };
  mScene = { pbPrevSceneChange, pbNextSceneChange, pbCancelSceneSearch, sbSceneThreshold };
  mLogo  = { pbPrevLogo,        pbNextLogo,        pbCancelLogoSearch,  sbLogoThreshold  };
  TTSettings* settings = TTSettings::instance();
  wireSearch(mBlack, &TTCutFrameNavigation::searchBlackFrame,  &TTCutFrameNavigation::abortBlackSearch,
             [settings](double v) { settings->setNavBlackThreshold(v); });
  wireSearch(mScene, &TTCutFrameNavigation::searchSceneChange, &TTCutFrameNavigation::abortSceneSearch,
             [settings](double v) { settings->setNavSceneThreshold(v); });
  wireSearch(mLogo,  &TTCutFrameNavigation::searchLogo,        &TTCutFrameNavigation::abortLogoSearch,
             [settings](double v) { settings->setNavLogoThreshold(v); });

  // Cut-In = Green background (start/go - universal convention)
  pbSetCutIn->setStyleSheet("QPushButton { background-color: #2d7a2d; color: white; font-weight: bold; }"
                            "QPushButton:disabled { background-color: #1a4a1a; color: #666666; }");

  // Cut-Out = Yellow/Gold background (end/attention, not alarm)
  pbSetCutOut->setStyleSheet("QPushButton { background-color: #997700; color: white; font-weight: bold; }"
                             "QPushButton:disabled { background-color: #554400; color: #666666; }");

  // Use theme icons with Qt standard icon fallback for action buttons
  pbGotoCutIn->setIcon(ttThemedIcon("go-first", QStyle::SP_ArrowBack));
  pbGotoCutIn->setStyleSheet("QPushButton { color: #44cc44; }");
  pbGotoCutOut->setIcon(ttThemedIcon("go-last", QStyle::SP_ArrowForward));
  pbGotoCutOut->setStyleSheet("QPushButton { color: #ccaa00; }");

  // Add Cut = Blue accent (main action)
  pbAddCut->setIcon(ttThemedIcon("list-add", QStyle::SP_FileDialogNewFolder));
  pbAddCut->setStyleSheet("QPushButton { background-color: #2a5a8a; color: white; }"
                          "QPushButton:hover { background-color: #3a6a9a; }");

  // Marker button = Purple (industry convention for notes/music)
  pbSetMarker->setIcon(ttThemedIcon("bookmark-new", QStyle::SP_DialogApplyButton));
  pbSetMarker->setStyleSheet("QPushButton { color: #9966cc; }");

  connect(pbNextIFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onNextIFrame);
  connect(pbPrevIFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onPrevIFrame);
  connect(pbNextPFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onNextPFrame);
  connect(pbPrevPFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onPrevPFrame);
  connect(pbNextBFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onNextBFrame);
  connect(pbPrevBFrame,        &QPushButton::clicked, this, &TTCutFrameNavigation::onPrevBFrame);
  connect(pbSetCutIn,          &QPushButton::clicked, this, &TTCutFrameNavigation::onSetCutIn);
  connect(pbSetCutOut,         &QPushButton::clicked, this, &TTCutFrameNavigation::onSetCutOut);
  connect(pbGotoCutIn,         &QPushButton::clicked, this, &TTCutFrameNavigation::onGotoCutIn);
  connect(pbGotoCutOut,        &QPushButton::clicked, this, &TTCutFrameNavigation::onGotoCutOut);
  connect(pbAddCut,            &QPushButton::clicked, this, &TTCutFrameNavigation::onAddCutRange);
  connect(pbQuickJump,         &QPushButton::clicked, this, &TTCutFrameNavigation::openQuickJump);
  connect(pbSetMarker,         &QPushButton::clicked, this, &TTCutFrameNavigation::onSetMarker);
  connect(pbSelectLogoROI,     &QPushButton::clicked, this, &TTCutFrameNavigation::onSelectLogoROI);
}

TTCutFrameNavigation::~TTCutFrameNavigation()
{
  // An edit that was armed but never completed (the user did not press
  // "Update range in cut list") still holds its copy here.
  delete editCutData;
}

void TTCutFrameNavigation::wireSearch(const SearchControls& c, SearchSignal search,
                                      void (TTCutFrameNavigation::*abort)(), std::function<void(double)> store)
{
  c.cancel->setStyleSheet("QPushButton { background-color: #cc2222; color: white; font-weight: bold; font-size: 14px; }"
                          "QPushButton:hover { background-color: #ee3333; }");
  c.cancel->hide();
  connect(c.prev,   &QPushButton::clicked, this, [this, c, search]() { startSearch(search, c.threshold, -1); });
  connect(c.next,   &QPushButton::clicked, this, [this, c, search]() { startSearch(search, c.threshold, +1); });
  connect(c.cancel, &QPushButton::clicked, this, abort);
  connect(c.threshold, qOverload<double>(&QDoubleSpinBox::valueChanged), this, std::move(store));
}

void TTCutFrameNavigation::startSearch(SearchSignal search, QDoubleSpinBox* threshold, int direction)
{
  if (!isControlEnabled) return;
  emit (this->*search)(currentPosition, direction, threshold->value());
}

void TTCutFrameNavigation::setSearchRunning(const SearchControls& c, bool running)
{
  c.cancel->setVisible(running);
  c.prev->setEnabled(!running);
  c.next->setEnabled(!running);
}

//void TTCutFrameNavigation::setTitle(const QString & title)
//{
//}

void TTCutFrameNavigation::setThresholds(float blackThreshold, float sceneThreshold)
{
  sbBlackThreshold->setValue(blackThreshold);
  sbSceneThreshold->setValue(sceneThreshold);
}

void TTCutFrameNavigation::controlEnabled(bool enabled)
{
  isControlEnabled = enabled;
  pbNextIFrame->setEnabled(enabled);
  pbPrevIFrame->setEnabled(enabled);
  pbNextPFrame->setEnabled(enabled);
  pbPrevPFrame->setEnabled(enabled);
  pbNextBFrame->setEnabled(enabled);
  pbPrevBFrame->setEnabled(enabled);
  pbSetCutIn->setEnabled(enabled);
  pbSetCutOut->setEnabled(enabled);
  pbGotoCutIn->setEnabled(enabled);
  pbGotoCutOut->setEnabled(enabled);
  pbAddCut->setEnabled(enabled);
  pbQuickJump->setEnabled(enabled);
  pbSetMarker->setEnabled(enabled);
  pbPrevBlackFrame->setEnabled(enabled);
  pbNextBlackFrame->setEnabled(enabled);
  pbPrevSceneChange->setEnabled(enabled);
  pbNextSceneChange->setEnabled(enabled);
  pbPrevLogo->setEnabled(enabled);
  pbNextLogo->setEnabled(enabled);
  pbSelectLogoROI->setEnabled(enabled);
}

void TTCutFrameNavigation::checkCutPosition(TTAVItem* avData, int pos)
{
  TTVideoStream* vs = avData->videoStream();
  currentPosition  = (pos >= 0) ? pos : vs->currentIndex();
  currentTime      = vs->frameTime(currentPosition).toString("hh:mm:ss");
  currentFrameType = vs->frameType(currentPosition);

  pbSetCutIn->setEnabled(vs->isCutInPoint(currentPosition));
  pbSetCutOut->setEnabled(vs->isCutOutPoint(currentPosition));
}

void TTCutFrameNavigation::keyPressEvent(QKeyEvent* e)
{
  int steps = 0;

  if (!isControlEnabled)
    return;

  //log->infoMsg(oName, "key press event");

  switch (e->key()) {

  // left arrow key
  case Qt::Key_Left:

    switch (e->modifiers()) {

    // backward stepPlusAlt
    case Qt::AltModifier:
      steps -= TTSettings::instance()->stepPlusAlt();
      break;
      // backward stepPlusCtrl
    case Qt::ControlModifier:
      steps -= TTSettings::instance()->stepPlusCtrl();
      break;
      // backward stepPlusShift
    case Qt::ShiftModifier:
      steps -= TTSettings::instance()->stepPlusShift();
      break;
      // backward stepArrowKeys frames
    default:
      steps -= TTSettings::instance()->stepArrowKeys();
      break;
    }

    emit moveNumSteps(steps);
    break;

    // right arrow key
  case Qt::Key_Right:

    switch (e->modifiers()) {

    // forward stepPlusAlt
    case Qt::AltModifier:
      steps += TTSettings::instance()->stepPlusAlt();
      break;
      // forward stepPlusCtrl
    case Qt::ControlModifier:
      steps += TTSettings::instance()->stepPlusCtrl();
      break;
      // forward stepPlusShift
    case Qt::ShiftModifier:
      steps += TTSettings::instance()->stepPlusShift();
      break;
      // forward stepArrowKeys frames
    default:
      steps += TTSettings::instance()->stepArrowKeys();
      break;
    }

    emit moveNumSteps(steps);
    break;
    // home key: show first frame
  case Qt::Key_Home:
    emit moveToHome();
    break;
    // end key: show last frame
  case Qt::Key_End:
    emit moveToEnd();
    break;
    // page down
  case Qt::Key_PageUp:
    steps -= TTSettings::instance()->stepPgUpDown();
    emit moveNumSteps(steps);
    break;
    // page up
  case Qt::Key_PageDown:
    steps += TTSettings::instance()->stepPgUpDown();
    emit moveNumSteps(steps);
    break;
    // I-frame
  case Qt::Key_I:
    // previous I-Frame
    if (e->modifiers() == Qt::ControlModifier)
      emit prevIFrame();
    // next I-frame
    else
      emit nextIFrame();
    break;
    // ---------------------------------------------------------------------------
    // P-frame
    // ---------------------------------------------------------------------------
  case Qt::Key_P:
    // previous P-Frame
    if (e->modifiers() == Qt::ControlModifier)
      emit prevPFrame();
    // next P-frame
    else
      emit nextPFrame();
    break;
    // ---------------------------------------------------------------------------
    // B-frame
    // ---------------------------------------------------------------------------
  case Qt::Key_B:   // B-frame ...
  case Qt::Key_F:   // ... and single frame: the same step, Ctrl reverses it
    if (e->modifiers() == Qt::ControlModifier)
      emit prevBFrame();
    else
      emit nextBFrame();
    break;

  // ---------------------------------------------------------------------------
  // Vim-like navigation: j/k for frame, g/G for home/end
  // ---------------------------------------------------------------------------
  case Qt::Key_J:
    // j = next frame (vim: down/forward)
    emit nextBFrame();
    break;

  case Qt::Key_K:
    // k = previous frame (vim: up/backward)
    emit prevBFrame();
    break;

  case Qt::Key_G:
    // G (shift+g) = end, g = home
    if (e->modifiers() == Qt::ShiftModifier)
      emit moveToEnd();
    else
      emit moveToHome();
    break;

  // ---------------------------------------------------------------------------
  // Cut shortcuts: [ for cut-in, ] for cut-out
  // ---------------------------------------------------------------------------
  case Qt::Key_BracketLeft:
    // [ = set cut-in
    onSetCutIn();
    break;

  case Qt::Key_BracketRight:
    // ] = set cut-out
    onSetCutOut();
    break;

  default:
    break;
  }
}

void TTCutFrameNavigation::onPrevIFrame()
{
  emit prevIFrame();
}

void TTCutFrameNavigation::onNextIFrame()
{
  emit nextIFrame();
}

void TTCutFrameNavigation::onPrevPFrame()
{
  emit prevPFrame();
}

void TTCutFrameNavigation::onNextPFrame()
{
  emit nextPFrame();
}

void TTCutFrameNavigation::onPrevBFrame()
{
  emit prevBFrame();
}

void TTCutFrameNavigation::onNextBFrame()
{
  emit nextBFrame();
}

void TTCutFrameNavigation::onSetCutIn()
{
  QString szTemp1, szTemp2;

  isCutInPosition = true;
  cutInPosition = currentPosition;

  szTemp1 = currentTime;
  szTemp2 = QString(" (%1)").arg(cutInPosition);
  szTemp2 += ttFrameTypeTag(currentFrameType);

  szTemp1 += szTemp2;
  laCutInPosition->setText(szTemp1);

  emit setCutIn(cutInPosition);
}

void TTCutFrameNavigation::onSetCutOut()
{
  QString szTemp1, szTemp2;

  isCutOutPosition = true;
  cutOutPosition = currentPosition;

  szTemp1 = currentTime;
  szTemp2 = QString(" (%1)").arg(cutOutPosition);
  szTemp2 += ttFrameTypeTag(currentFrameType);

  szTemp1 += szTemp2;
  laCutOutPosition->setText(szTemp1);

  emit setCutOut(cutOutPosition);
}

void TTCutFrameNavigation::onGotoCutIn()
{
  if (isCutInPosition)
    emit gotoCutIn(cutInPosition);
}

void TTCutFrameNavigation::onGotoCutOut()
{
  if (isCutOutPosition)
    emit gotoCutOut(cutOutPosition);
}

void TTCutFrameNavigation::onAddCutRange()
{
  if (isCutInPosition && isCutOutPosition) {
    isCutInPosition = false;
    isCutOutPosition = false;
    laCutInPosition->setText("...");
    laCutOutPosition->setText("...");

    if (isEditCut) {
      editCutData->avDataItem()->updateCutEntry(*editCutData,
          cutInPosition, cutOutPosition);
      pbAddCut->setText(tr("Add range to cut list"));
      isEditCut = false;
      delete editCutData;
      editCutData = nullptr;
      return;
    }

    emit addCutRange(cutInPosition, cutOutPosition);
  }
}

void TTCutFrameNavigation::onEditCut(const TTCutItem& cutData)
{
  QString szTemp1, szTemp2;

  isCutInPosition = true;
  isCutOutPosition = true;
  cutInPosition = cutData.cutInIndex();
  cutOutPosition = cutData.cutOutIndex();

  //szTemp1 = cutData.getCutInTime().toString("hh:mm:ss.zzz");
  szTemp1 = cutData.cutInTime().toString("hh:mm:ss");
    szTemp2 = QString(" (%1)").arg(cutInPosition);
  szTemp2 += ttFrameTypeTag(cutData.cutInFrameType());

  szTemp1 += szTemp2;
  laCutInPosition->setText(szTemp1);

  //szTemp1 = cutData.getCutOutTime().toString("hh:mm:ss.zzz");
  szTemp1 = cutData.cutOutTime().toString("hh:mm:ss");
  szTemp2 = QString(" (%1)").arg(cutOutPosition);
  szTemp2 += ttFrameTypeTag(cutData.cutOutFrameType());

  szTemp1 += szTemp2;
  laCutOutPosition->setText(szTemp1);

  isEditCut = true;
  delete editCutData;                    // a second edit without an update in between
  editCutData = new TTCutItem(cutData);

  pbAddCut->setText(tr("Update range in cut list"));

  emit gotoCutIn(cutInPosition);
}

void TTCutFrameNavigation::onSetMarker()
{
  emit setMarker();
}

void TTCutFrameNavigation::setBlackSearchRunning(bool running)
{
  setSearchRunning(mBlack, running);
}

void TTCutFrameNavigation::setSceneSearchRunning(bool running)
{
  setSearchRunning(mScene, running);
}

void TTCutFrameNavigation::onSelectLogoROI()
{
  if (pbSelectLogoROI->isChecked()) {
    // Entering selection mode
    emit selectLogoROI();
  } else {
    // Clicked again while in selection mode → cancel and clear profile
    emit cancelLogoROI();
  }
}

void TTCutFrameNavigation::setLogoSearchRunning(bool running)
{
  setSearchRunning(mLogo, running);
  pbSelectLogoROI->setEnabled(!running);
}

void TTCutFrameNavigation::setLogoSearchEnabled(bool enabled)
{
  pbPrevLogo->setEnabled(enabled);
  pbNextLogo->setEnabled(enabled);
  // Uncheck select button when profile state changes
  pbSelectLogoROI->setChecked(false);
}

void TTCutFrameNavigation::onLogoContextMenu(const QPoint& pos)
{
  QMenu menu(this);
  menu.addAction(tr("Load logo file..."), this, &TTCutFrameNavigation::loadLogoFile);
  menu.exec(pbSelectLogoROI->mapToGlobal(pos));
}
