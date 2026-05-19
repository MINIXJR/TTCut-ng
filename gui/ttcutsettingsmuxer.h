/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingsmuxer.h                                            */
/*----------------------------------------------------------------------------*/

#ifndef TTCUTSETTINGSMUXER_H
#define TTCUTSETTINGSMUXER_H

#include "ui_ttcutsettingsmuxer.h"
#include <QGroupBox>

class TTCutSettingsMuxer : public QGroupBox, private Ui_TTCutSettingsMuxer
{
  Q_OBJECT

  public:
    enum Mode { Defaults, Override };

    explicit TTCutSettingsMuxer(QWidget* parent = nullptr);

    void setTabData();
    void saveTabData();
    void setMode(Mode m);

  private:
    void populateCodecMuxers();
    void populateMpgTarget();
    void populateMpgMode();

  protected slots:
    void onMkvChaptersChanged(int state);

  private slots:
    void resetToDefaults();
};

#endif
