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

#include "ttcutsettingsmuxer.h"

#include "../common/ttcut.h"
#include "../common/ttsettings.h"


TTCutSettingsMuxer::TTCutSettingsMuxer(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  connect(cbDeleteES,          &QCheckBox::stateChanged, this, &TTCutSettingsMuxer::onStateDeleteES);
  connect(cbMkvCreateChapters, &QCheckBox::stateChanged, this, &TTCutSettingsMuxer::onMkvChaptersChanged);
}

void TTCutSettingsMuxer::setTitle(__attribute__((unused))const QString& title)
{
}

void TTCutSettingsMuxer::setTabData()
{
  if (TTSettings::instance()->muxDeleteES())
    cbDeleteES->setCheckState(Qt::Checked);
  else
    cbDeleteES->setCheckState(Qt::Unchecked);

  // MKV chapter settings
  cbMkvCreateChapters->setChecked(TTSettings::instance()->mkvCreateChapters());
  sbMkvChapterInterval->setValue(TTSettings::instance()->mkvChapterInterval());
  sbMkvChapterInterval->setEnabled(TTSettings::instance()->mkvCreateChapters());
}

void TTCutSettingsMuxer::getTabData()
{
  // muxDeleteES was only being persisted via the per-widget
  // signal handlers. Persist them here too — symmetric with setTabData.
  TTSettings::instance()->setMuxDeleteES(cbDeleteES->isChecked());

  // MKV chapter settings
  TTSettings::instance()->setMkvCreateChapters(cbMkvCreateChapters->isChecked());
  TTSettings::instance()->setMkvChapterInterval(sbMkvChapterInterval->value());
}

void TTCutSettingsMuxer::onStateDeleteES(int state)
{
  if (state == Qt::Unchecked)
    TTSettings::instance()->setMuxDeleteES(false);
  else
    TTSettings::instance()->setMuxDeleteES(true);
}

void TTCutSettingsMuxer::onMkvChaptersChanged(int state)
{
  TTSettings::instance()->setMkvCreateChapters(state == Qt::Checked);
  sbMkvChapterInterval->setEnabled(TTSettings::instance()->mkvCreateChapters());
}
