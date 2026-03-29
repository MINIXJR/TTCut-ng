/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2010 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2008                                                     */
/* FILE     : ttnavigation.cpp                                                */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTFRAMENAVIGATION
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#include "ttcutframenavigation.h"
#include "../common/ttcut.h"
#include "../data/ttavlist.h"

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

	// Cancel button: red, initially hidden
	pbCancelBlackSearch->setStyleSheet("QPushButton { background-color: #cc2222; color: white; font-weight: bold; font-size: 14px; }"
	                                   "QPushButton:hover { background-color: #ee3333; }");
	pbCancelBlackSearch->hide();

	// Scene change navigation buttons - Cyan (#44cccc)
	pbPrevSceneChange->setIcon(QIcon());
	pbPrevSceneChange->setText(tr("\u25C0 \u25E7"));
	pbPrevSceneChange->setStyleSheet("QPushButton { color: #44cccc; font-weight: bold; }");
	pbNextSceneChange->setIcon(QIcon());
	pbNextSceneChange->setText(tr("\u25E7 \u25B6"));
	pbNextSceneChange->setStyleSheet("QPushButton { color: #44cccc; font-weight: bold; }");

	// Scene cancel button: red, initially hidden
	pbCancelSceneSearch->setStyleSheet("QPushButton { background-color: #cc2222; color: white; font-weight: bold; font-size: 14px; }"
	                                    "QPushButton:hover { background-color: #ee3333; }");
	pbCancelSceneSearch->hide();

	// Logo detection buttons - Magenta (#cc44cc)
	pbSelectLogoROI->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; font-size: 16px; }"
	                                "QPushButton:checked { background-color: #cc44cc; color: white; }");
	pbSelectLogoROI->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(pbSelectLogoROI, SIGNAL(customContextMenuRequested(QPoint)), SLOT(onLogoContextMenu(QPoint)));
	pbPrevLogo->setIcon(QIcon());
	pbPrevLogo->setText(tr("\u25C0 \u2B26"));  // ◀ ⬦
	pbPrevLogo->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; }");
	pbNextLogo->setIcon(QIcon());
	pbNextLogo->setText(tr("\u2B26 \u25B6"));  // ⬦ ▶
	pbNextLogo->setStyleSheet("QPushButton { color: #cc44cc; font-weight: bold; }");

	// Logo cancel button: red, initially hidden
	pbCancelLogoSearch->setStyleSheet("QPushButton { background-color: #cc2222; color: white; font-weight: bold; font-size: 14px; }"
	                                  "QPushButton:hover { background-color: #ee3333; }");
	pbCancelLogoSearch->hide();

	sbLogoThreshold->setValue(TTCut::navLogoThreshold);

	// Keep TTCut variables in sync with spinbox changes
	connect(sbBlackThreshold, SIGNAL(valueChanged(double)), SLOT(onBlackThresholdChanged(double)));
	connect(sbSceneThreshold, SIGNAL(valueChanged(double)), SLOT(onSceneThresholdChanged(double)));

	// Cut-In = Green background (start/go - universal convention)
	pbSetCutIn->setStyleSheet("QPushButton { background-color: #2d7a2d; color: white; font-weight: bold; }"
	                          "QPushButton:disabled { background-color: #1a4a1a; color: #666666; }");

	// Cut-Out = Yellow/Gold background (end/attention, not alarm)
	pbSetCutOut->setStyleSheet("QPushButton { background-color: #997700; color: white; font-weight: bold; }"
	                           "QPushButton:disabled { background-color: #554400; color: #666666; }");

	// Use theme icons with Qt standard icon fallback for action buttons
	QStyle* style = QApplication::style();
	pbGotoCutIn->setIcon(QIcon::fromTheme("go-first", style->standardIcon(QStyle::SP_ArrowBack)));
	pbGotoCutIn->setStyleSheet("QPushButton { color: #44cc44; }");
	pbGotoCutOut->setIcon(QIcon::fromTheme("go-last", style->standardIcon(QStyle::SP_ArrowForward)));
	pbGotoCutOut->setStyleSheet("QPushButton { color: #ccaa00; }");

	// Add Cut = Blue accent (main action)
	pbAddCut->setIcon(QIcon::fromTheme("list-add", style->standardIcon(QStyle::SP_FileDialogNewFolder)));
	pbAddCut->setStyleSheet("QPushButton { background-color: #2a5a8a; color: white; }"
	                        "QPushButton:hover { background-color: #3a6a9a; }");

	// Marker button = Purple (industry convention for notes/music)
	pbSetMarker->setIcon(QIcon::fromTheme("bookmark-new", style->standardIcon(QStyle::SP_DialogApplyButton)));
	pbSetMarker->setStyleSheet("QPushButton { color: #9966cc; }");

	connect(pbNextIFrame, SIGNAL(clicked()), SLOT(onNextIFrame()));
	connect(pbPrevIFrame, SIGNAL(clicked()), SLOT(onPrevIFrame()));
	connect(pbNextPFrame, SIGNAL(clicked()), SLOT(onNextPFrame()));
	connect(pbPrevPFrame, SIGNAL(clicked()), SLOT(onPrevPFrame()));
	connect(pbNextBFrame, SIGNAL(clicked()), SLOT(onNextBFrame()));
	connect(pbPrevBFrame, SIGNAL(clicked()), SLOT(onPrevBFrame()));
	connect(pbSetCutIn, SIGNAL(clicked()), SLOT(onSetCutIn()));
	connect(pbSetCutOut, SIGNAL(clicked()), SLOT(onSetCutOut()));
	connect(pbGotoCutIn, SIGNAL(clicked()), SLOT(onGotoCutIn()));
	connect(pbGotoCutOut, SIGNAL(clicked()), SLOT(onGotoCutOut()));
	connect(pbAddCut, SIGNAL(clicked()), SLOT(onAddCutRange()));
	connect(pbQuickJump, SIGNAL(clicked()), SIGNAL(openQuickJump()));
	connect(pbSetMarker, SIGNAL(clicked()), SLOT(onSetMarker()));
	connect(pbPrevBlackFrame, SIGNAL(clicked()), SLOT(onPrevBlackFrame()));
	connect(pbNextBlackFrame, SIGNAL(clicked()), SLOT(onNextBlackFrame()));
	connect(pbCancelBlackSearch, SIGNAL(clicked()), SLOT(onCancelBlackSearch()));
	connect(pbPrevSceneChange, SIGNAL(clicked()), SLOT(onPrevSceneChange()));
	connect(pbNextSceneChange, SIGNAL(clicked()), SLOT(onNextSceneChange()));
	connect(pbCancelSceneSearch, SIGNAL(clicked()), SLOT(onCancelSceneSearch()));
	connect(pbSelectLogoROI, SIGNAL(clicked()), SLOT(onSelectLogoROI()));
	connect(pbPrevLogo, SIGNAL(clicked()), SLOT(onPrevLogo()));
	connect(pbNextLogo, SIGNAL(clicked()), SLOT(onNextLogo()));
	connect(pbCancelLogoSearch, SIGNAL(clicked()), SLOT(onCancelLogoSearch()));
	connect(sbLogoThreshold, SIGNAL(valueChanged(double)), SLOT(onLogoThresholdChanged(double)));
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
	pbPrevSceneChange->setEnabled(enabled);
	pbNextSceneChange->setEnabled(enabled);
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

		// backward TTCut::stepPlusAlt
		case Qt::AltModifier:
			steps -= TTCut::stepPlusAlt;
			break;
			// backward TTCut::stepPlusCtrl
		case Qt::ControlModifier:
			steps -= TTCut::stepPlusCtrl;
			break;
			// backward TTCut::stepPlusShift
		case Qt::ShiftModifier:
			steps -= TTCut::stepPlusShift;
			break;
			// backward one frame
		default:
			steps -= 1;
			break;
		}

		emit moveNumSteps(steps);
		break;

		// right arrow key
	case Qt::Key_Right:

		switch (e->modifiers()) {

		// forward TTCut::stepPlusAlt
		case Qt::AltModifier:
			steps += TTCut::stepPlusAlt;
			break;
			// forward TTCut::stepPlusCtrl
		case Qt::ControlModifier:
			steps += TTCut::stepPlusCtrl;
			break;
			// forward TTCut::stepPlusShift
		case Qt::ShiftModifier:
			steps += TTCut::stepPlusShift;
			break;
			// forward one frame
		default:
			steps += 1;
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
		steps -= TTCut::stepPgUpDown;
		emit moveNumSteps(steps);
		break;
		// page up
	case Qt::Key_PageDown:
		steps += TTCut::stepPgUpDown;
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
	case Qt::Key_B:
		// previous B-Frame
		if (e->modifiers() == Qt::ControlModifier)
			emit prevBFrame();
		// next B-frame
		else
			emit nextBFrame();
		break;
		// ---------------------------------------------------------------------------
		// Single frame
		// ---------------------------------------------------------------------------
	case Qt::Key_F:
		// previous frame
		if (e->modifiers() == Qt::ControlModifier)
			emit prevBFrame();
		// next frame
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

	if (currentFrameType == 1)
		szTemp2 += " [I]";
	if (currentFrameType == 2)
		szTemp2 += " [P]";
	if (currentFrameType == 3)
		szTemp2 += " [B]";

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

	if (currentFrameType == 1)
		szTemp2 += " [I]";
	if (currentFrameType == 2)
		szTemp2 += " [P]";
	if (currentFrameType == 3)
		szTemp2 += " [B]";

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

	if (cutData.cutInFrameType() == 1)
		szTemp2 += " [I]";
	if (cutData.cutInFrameType() == 2)
		szTemp2 += " [P]";
	if (cutData.cutInFrameType() == 3)
		szTemp2 += " [B]";

	szTemp1 += szTemp2;
	laCutInPosition->setText(szTemp1);

	//szTemp1 = cutData.getCutOutTime().toString("hh:mm:ss.zzz");
	szTemp1 = cutData.cutOutTime().toString("hh:mm:ss");
	szTemp2 = QString(" (%1)").arg(cutOutPosition);

	if (cutData.cutOutFrameType() == 1)
		szTemp2 += " [I]";
	if (cutData.cutOutFrameType() == 2)
		szTemp2 += " [P]";
	if (cutData.cutOutFrameType() == 3)
		szTemp2 += " [B]";

	szTemp1 += szTemp2;
	laCutOutPosition->setText(szTemp1);

	isEditCut = true;
	editCutData = new TTCutItem(cutData);

	pbAddCut->setText(tr("Update range in cut list"));

	emit gotoCutIn(cutInPosition);
}

void TTCutFrameNavigation::onSetMarker()
{
	emit setMarker();
}

void TTCutFrameNavigation::onPrevBlackFrame()
{
  if (!isControlEnabled) return;
  emit searchBlackFrame(currentPosition, -1, sbBlackThreshold->value());
}

void TTCutFrameNavigation::onNextBlackFrame()
{
  if (!isControlEnabled) return;
  emit searchBlackFrame(currentPosition, +1, sbBlackThreshold->value());
}

void TTCutFrameNavigation::onCancelBlackSearch()
{
  emit abortBlackSearch();
}

void TTCutFrameNavigation::setBlackSearchRunning(bool running)
{
  pbCancelBlackSearch->setVisible(running);
  pbPrevBlackFrame->setEnabled(!running);
  pbNextBlackFrame->setEnabled(!running);
}

void TTCutFrameNavigation::onPrevSceneChange()
{
  if (!isControlEnabled) return;
  emit searchSceneChange(currentPosition, -1, sbSceneThreshold->value());
}

void TTCutFrameNavigation::onNextSceneChange()
{
  if (!isControlEnabled) return;
  emit searchSceneChange(currentPosition, +1, sbSceneThreshold->value());
}

void TTCutFrameNavigation::onCancelSceneSearch()
{
  emit abortSceneSearch();
}

void TTCutFrameNavigation::setSceneSearchRunning(bool running)
{
  pbCancelSceneSearch->setVisible(running);
  pbPrevSceneChange->setEnabled(!running);
  pbNextSceneChange->setEnabled(!running);
}

void TTCutFrameNavigation::onBlackThresholdChanged(double value)
{
  TTCut::navBlackThreshold = value;
}

void TTCutFrameNavigation::onSceneThresholdChanged(double value)
{
  TTCut::navSceneThreshold = value;
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

void TTCutFrameNavigation::onPrevLogo()
{
  if (!isControlEnabled) return;
  emit searchLogo(currentPosition, -1, sbLogoThreshold->value());
}

void TTCutFrameNavigation::onNextLogo()
{
  if (!isControlEnabled) return;
  emit searchLogo(currentPosition, +1, sbLogoThreshold->value());
}

void TTCutFrameNavigation::onCancelLogoSearch()
{
  emit abortLogoSearch();
}

void TTCutFrameNavigation::setLogoSearchRunning(bool running)
{
  pbCancelLogoSearch->setVisible(running);
  pbPrevLogo->setEnabled(!running);
  pbNextLogo->setEnabled(!running);
  pbSelectLogoROI->setEnabled(!running);
}

void TTCutFrameNavigation::setLogoSearchEnabled(bool enabled)
{
  pbPrevLogo->setEnabled(enabled);
  pbNextLogo->setEnabled(enabled);
  // Uncheck select button when profile state changes
  pbSelectLogoROI->setChecked(false);
}

void TTCutFrameNavigation::onLogoThresholdChanged(double value)
{
  TTCut::navLogoThreshold = value;
}

void TTCutFrameNavigation::onLogoContextMenu(const QPoint& pos)
{
  QMenu menu(this);
  menu.addAction(tr("Logo-Datei laden..."), this, SIGNAL(loadLogoFile()));
  menu.exec(pbSelectLogoROI->mapToGlobal(pos));
}
