/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingscommon.cpp                                         */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSCOMMON
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

#include "ttcutsettingscommon.h"

#include "../common/ttcut.h"

#include <QFileDialog>
#include <QDir>
#include <QGridLayout>
#include <QLabel>

TTCutSettingsCommon::TTCutSettingsCommon(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  // Audio burst threshold spinbox (programmatically added)
  sbBurstThreshold = new QSpinBox(this);
  sbBurstThreshold->setRange(-60, 0);
  sbBurstThreshold->setSuffix(" dB");
  QLabel* lblBurst = new QLabel(tr("Audio-Burst Threshold (dB, 0=off)"), this);
  QGridLayout* gl = qobject_cast<QGridLayout*>(layout());
  if (gl) {
    int row = gl->rowCount();
    gl->addWidget(lblBurst, row, 0);
    gl->addWidget(sbBurstThreshold, row, 1);
  }

  // Preview I-frame hint
  QLabel* lblPreviewHint = new QLabel(
      tr("Die Vorschau beginnt für jeden Schnitt bei einem I-Frame."), this);
  lblPreviewHint->setStyleSheet("QLabel { color: #666; font-style: italic; }");
  lblPreviewHint->setWordWrap(true);
  if (gl) {
    int row2 = gl->rowCount();
    gl->addWidget(lblPreviewHint, row2, 0, 1, 3);
  }

  connect(btnDirOpen, SIGNAL(clicked()), SLOT(onOpenDir()));
}

void TTCutSettingsCommon::setTitle(__attribute__((unused))const QString& title)
{
}

// select a temp directory path
void TTCutSettingsCommon::onOpenDir()
{
  QString str_dir = QFileDialog::getExistingDirectory( this,
						       "Select temporary directory",
						       TTCut::tempDirPath,
						       (QFileDialog::DontResolveSymlinks |
							QFileDialog::ShowDirsOnly) );

  if ( !str_dir.isEmpty() )
  {
    TTCut::tempDirPath = str_dir;

    leTempDirectory->setText( TTCut::tempDirPath );
  }
}

// set the tab data from the global parameter
void TTCutSettingsCommon::setTabData()
{
  // --------------------------------------------------------------
  // common settings
  // --------------------------------------------------------------

  // Navigation
  sbSliderClickPlacement->setValue( TTCut::stepSliderClick );
  sbPgUpDown->setValue( TTCut::stepPgUpDown );
  sbArrowKeyPlacement->setValue( TTCut::stepArrowKeys );
  sbAltDistance->setValue( TTCut::stepPlusAlt );
  sbCtrlDistance->setValue( TTCut::stepPlusCtrl );
  sbQuickJumpDistance->setValue( TTCut::stepQuickJump );
  sbMouseWheel->setValue( TTCut::stepMouseWheel );

  // Preview
  spPreviewLength->setValue( TTCut::cutPreviewSeconds );

  // Frame search
  sbSearchIntervall->setValue( TTCut::searchLength );
  sbSkipFrames->setValue( TTCut::playSkipFrames );

  // Options, directories
  cbQuickSearch->setChecked(TTCut::fastSlider);
  leTempDirectory->setText( TTCut::tempDirPath );

  // Audio burst detection
  sbBurstThreshold->setValue(TTCut::burstThresholdDb);
}

// get the tab data and fill the global parameter
void TTCutSettingsCommon::getTabData()
{
  // Navigation
  TTCut::stepSliderClick = sbSliderClickPlacement->value( );
  TTCut::stepPgUpDown    = sbPgUpDown->value( );
  TTCut::stepArrowKeys   = sbArrowKeyPlacement->value( );
  TTCut::stepPlusAlt     = sbAltDistance->value( );
  TTCut::stepPlusCtrl    = sbCtrlDistance->value( );
  TTCut::stepQuickJump   = sbQuickJumpDistance->value( );
  TTCut::stepMouseWheel  = sbMouseWheel->value( );

  // Preview
  TTCut::cutPreviewSeconds = spPreviewLength->value( );

  // Frame search
  TTCut::searchLength   = sbSearchIntervall->value( );
  TTCut::playSkipFrames = sbSkipFrames->value( );

  // Options, directories
  TTCut::fastSlider  = cbQuickSearch->isChecked();
  TTCut::tempDirPath = leTempDirectory->text( );

  if ( !QDir(TTCut::tempDirPath).exists() )
    TTCut::tempDirPath = QDir::tempPath();

  // Audio burst detection
  TTCut::burstThresholdDb = sbBurstThreshold->value();
}



