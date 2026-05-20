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

#ifndef TTCUTFRAMENAVIGATION_H
#define TTCUTFRAMENAVIGATION_H

#include "ui_ttcutframenavigationwidget.h"

#include "../common/ttmessagelogger.h"
#include "../data/ttcutlist.h"
#include "../avstream/ttavstream.h"

class TTAVItem;

class TTCutFrameNavigation : public QWidget, Ui::TTCutFrameNavigationWidget
{
  Q_OBJECT

  public:
    TTCutFrameNavigation(QWidget* parent=0);

    //void setTitle ( const QString & title );

    void controlEnabled( bool enabled );
    void setThresholds(float blackThreshold, float sceneThreshold);
    void checkCutPosition(TTAVItem* avData, int pos = -1);
    void keyPressEvent(QKeyEvent* e);

  public slots:
    void onPrevIFrame();
    void onNextIFrame();
    void onPrevPFrame();
    void onNextPFrame();
    void onPrevBFrame();
    void onNextBFrame();
    void onSetCutIn();
    void onSetCutOut();
    void onGotoCutIn();
    void onGotoCutOut();
    void onAddCutRange();
    void onSetMarker();
    void onPrevBlackFrame();
    void onNextBlackFrame();
    void onCancelBlackSearch();
    void setBlackSearchRunning(bool running);
    void onPrevSceneChange();
    void onNextSceneChange();
    void onCancelSceneSearch();
    void setSceneSearchRunning(bool running);
    void onBlackThresholdChanged(double value);
    void onSceneThresholdChanged(double value);
    void onSelectLogoROI();
    void onPrevLogo();
    void onNextLogo();
    void onCancelLogoSearch();
    void setLogoSearchRunning(bool running);
    void setLogoSearchEnabled(bool enabled);
    void onLogoThresholdChanged(double value);
    void onLogoContextMenu(const QPoint& pos);

    void onEditCut(const TTCutItem& cutData);

  signals:
    void prevIFrame();
    void nextIFrame();
    void prevPFrame();
    void nextPFrame();
    void prevBFrame();
    void nextBFrame();
    void setCutIn(int);
    void setCutOut(int);
    void gotoCutIn(int);
    void gotoCutOut(int);
    void addCutRange(int, int);
    void moveNumSteps(int);
    void moveToHome();
    void moveToEnd();
    void setMarker();
    void openQuickJump();
    void searchBlackFrame(int currentPos, int direction, float threshold);
    void abortBlackSearch();
    void searchSceneChange(int currentPos, int direction, float threshold);
    void abortSceneSearch();
    void selectLogoROI();
    void cancelLogoROI();
    void loadLogoFile();
    void searchLogo(int currentPos, int direction, float threshold);
    void abortLogoSearch();

  protected:

  private:
    TTMessageLogger* log;
    TTCutItem* editCutData;
    bool    isControlEnabled;
    bool    isEditCut;
    bool    isCutInPosition;
    bool    isCutOutPosition;
    int     currentPosition;
    int     currentFrameType;
    QString currentTime;
    int     cutInPosition;
    int     cutOutPosition;
    int     markerPosition;
};

#endif
