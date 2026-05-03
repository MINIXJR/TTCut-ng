/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingsfiles.cpp                                          */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSFILES
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

#include "ttcutsettingsfiles.h"

#include "../common/ttcut.h"
#include "../common/ttsettings.h"

  
TTCutSettingsFiles::TTCutSettingsFiles(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  connect(cbCreateLog, &QCheckBox::stateChanged, this, &TTCutSettingsFiles::onCreateLogStateChanged);
}

void TTCutSettingsFiles::setTitle(__attribute__((unused))const QString& title)
{
}

// set the tab data from the global parameter
void TTCutSettingsFiles::setTabData()
{
  // IDD files
  cbCreateVideoIDD->setChecked( TTSettings::instance()->createVideoIDD() );
  cbCreateAudioIDD->setChecked( TTSettings::instance()->createAudioIDD() );
  cbCreateCutIDD->setChecked( TTCut::createCutIDD );
  cbReadVideoIDD->setChecked( TTSettings::instance()->readVideoIDD() );
  cbReadAudioIDD->setChecked( TTSettings::instance()->readAudioIDD() );

  // Logfile
  cbCreateLog->setChecked( TTSettings::instance()->createLogFile() );
  cbLogConsole->setChecked( TTSettings::instance()->logModeConsole() );
  cbLogExtended->setChecked( TTSettings::instance()->logModeExtended() );
  cbLogPlusVideoIndex->setChecked( TTSettings::instance()->logVideoIndexInfo() );

  gbLogfileOptions->setEnabled(TTSettings::instance()->createLogFile());
}

// get the tab data and fill the global parameter
void TTCutSettingsFiles::getTabData()
{
  // IDD files
  TTSettings::instance()->setCreateVideoIDD( cbCreateVideoIDD->isChecked( ) );
  TTSettings::instance()->setCreateAudioIDD( cbCreateAudioIDD->isChecked( ) );
  TTCut::createCutIDD      = cbCreateCutIDD->isChecked( );
  TTSettings::instance()->setReadVideoIDD( cbReadVideoIDD->isChecked( ) );
  TTSettings::instance()->setReadAudioIDD( cbReadAudioIDD->isChecked( ) );

  // logfile
  TTSettings::instance()->setCreateLogFile( cbCreateLog->isChecked( ) );
  TTSettings::instance()->setLogModeConsole( cbLogConsole->isChecked() );
  TTSettings::instance()->setLogModeExtended( cbLogExtended->isChecked( ) );
  TTSettings::instance()->setLogVideoIndexInfo( cbLogPlusVideoIndex->isChecked( ) );
}

void TTCutSettingsFiles::onCreateLogStateChanged(int state)
{
  if (state == Qt::Unchecked) {
    gbLogfileOptions->setEnabled(false);
  } else {
    gbLogfileOptions->setEnabled(true);
  }
}
