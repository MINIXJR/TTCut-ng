/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/* Originally: TriTime (c) 2003-2010 / www.tritime.org                        */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutavcutdlg.h                                                 */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 04/01/2005 */
/* MODIFIED: b. altendorf                                    DATE: 03/05/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTCUTAVCUTDLG
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

#ifndef TTCUTAVCUTDLG_H
#define TTCUTAVCUTDLG_H

#include "ui_avcutdialog.h"

#include "../common/ttmessagelogger.h"
#include "../common/ttcut.h"

class QString;

// -----------------------------------------------------------------------------
// TTCut A/V cut dialog;
// -----------------------------------------------------------------------------
class TTCutAVCutDlg : public QDialog, Ui::TTCutAVCutDlg
{
  Q_OBJECT

  public:
    TTCutAVCutDlg( QWidget* parent=0, bool audioOnly = false);
    ~TTCutAVCutDlg();

    void setGlobalData();
    void setCommonData();
    void getCommonData();

  protected slots:
    void onDlgStart();
    void onDlgCancel();
    void onDirectoryOpen();

  private slots:
    void updateOutputFilename();
    void onMuxerProgChanged(int index);
    void onCodecChangedForVisibility(int codecIndex);
    // Reset the working set (Mux/Audio + Encoder) back to the App-Defaults
    // from the Settings dialog, then reload the UI. Does not close the
    // dialog — user can still Cancel or change values before clicking Start.
    void onResetDefaults();

  private:
    void populateMuxerProg();
    void populateMuxTarget();
    void updateMuxerVisibility();
    void updateFreeSpaceLine();
    static QString expectedContainerExtension(int container);
    static QString expectedEsExtension(int container, int codec);

  private:
    TTMessageLogger* log;
};

#endif // TTCUTAVCUTDLG_H
