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
#include "../extern/ttmkvmergeprovider.h"

#include <QFileDialog>

TTCutSettingsMuxer::TTCutSettingsMuxer(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  // Initialize combo boxes
  initMuxProgList();
  initMuxTargetList();
  initOutputContainerList();

  // Enable muxer selection now that we have multiple options
  cbMuxerProg->setEnabled(true);
  cbMuxTarget->setEnabled(true);
  pbConfigureMuxer->setEnabled(false);

  connect(rbCreateMuxScript, SIGNAL(clicked()),         SLOT(onCreateMuxScript()));
  connect(rbMuxStreams,      SIGNAL(clicked()),         SLOT(onCreateMuxStreams()));
  connect(pbConfigureMuxer,  SIGNAL(clicked()),         SLOT(onConfigureMuxer()));
  connect(btnOutputPath,     SIGNAL(clicked()),         SLOT(onOpenOutputPath()));
  connect(cbDeleteES,        SIGNAL(stateChanged(int)), SLOT(onStateDeleteES(int)));
  connect(cbPause,           SIGNAL(stateChanged(int)), SLOT(onStatePause(int)));
  connect(cbMuxerProg,       SIGNAL(currentIndexChanged(int)), SLOT(onMuxerProgChanged(int)));
  connect(cbMkvCreateChapters, SIGNAL(stateChanged(int)), SLOT(onMkvChaptersChanged(int)));
}

void TTCutSettingsMuxer::setTitle(__attribute__((unused))const QString& title)
{
}

void TTCutSettingsMuxer::initMuxProgList()
{
  cbMuxerProg->clear();
  cbMuxerProg->insertItem(0, "Mplex (MPEG-2)");
  cbMuxerProg->insertItem(1, "mkvmerge (MKV)");
  cbMuxerProg->insertItem(2, "FFmpeg (MP4/TS)");

  // Check availability and set default
  if (TTMkvMergeProvider::isMkvMergeInstalled()) {
    cbMuxerProg->setCurrentIndex(1);  // Default to mkvmerge if available
  } else {
    cbMuxerProg->setCurrentIndex(0);  // Fallback to mplex
  }
}

void TTCutSettingsMuxer::initMuxTargetList()
{
  cbMuxTarget->insertItem(0, "Generic MPEG1 (f0)");
  cbMuxTarget->insertItem(1, "VCD (f1)");
  cbMuxTarget->insertItem(2, "user-rate VCD (f2)");
  cbMuxTarget->insertItem(3, "Generic MPEG2 (f3)");
  cbMuxTarget->insertItem(4, "SVCD (f4)");
  cbMuxTarget->insertItem(5, "user-rate SVCD (f5)");
  cbMuxTarget->insertItem(6, "VCD Stills (f6)");
  cbMuxTarget->insertItem(7, "DVD with NAV sectors (f8)");
  cbMuxTarget->insertItem(8, "DVD (f9)");

  cbMuxTarget->setCurrentIndex(7);
}

void TTCutSettingsMuxer::setTabData()
{
  switch(TTCut::muxMode)
  {
    case 0:
      rbMuxStreams->setChecked(true);
      cbDeleteES->setEnabled(true);
      cbPause->setEnabled(true);
      break;

    case 1:
      rbCreateMuxScript->setChecked(true);
      cbDeleteES->setEnabled(false);
      cbPause->setEnabled(false);
      break;
  }

  // Set muxer program based on outputContainer setting
  cbMuxerProg->setCurrentIndex(TTCut::outputContainer);
  cbMuxTarget->setCurrentIndex(TTCut::mpeg2Target);
  updateMuxerVisibility();

  leOutputPath->setText(TTCut::muxOutputPath);

  if (TTCut::muxDeleteES)
    cbDeleteES->setCheckState(Qt::Checked);
  else
    cbDeleteES->setCheckState(Qt::Unchecked);

  if(TTCut::muxPause)
    cbPause->setCheckState(Qt::Checked);
  else
    cbPause->setCheckState(Qt::Unchecked);

  // MKV chapter settings
  cbMkvCreateChapters->setChecked(TTCut::mkvCreateChapters);
  sbMkvChapterInterval->setValue(TTCut::mkvChapterInterval);
  sbMkvChapterInterval->setEnabled(TTCut::mkvCreateChapters);
}

void TTCutSettingsMuxer::getTabData()
{
  TTCut::mpeg2Target   = cbMuxTarget->currentIndex();
  TTCut::muxOutputPath = leOutputPath->text();

  // MKV chapter settings
  TTCut::mkvCreateChapters  = cbMkvCreateChapters->isChecked();
  TTCut::mkvChapterInterval = sbMkvChapterInterval->value();

  QFileInfo fInfo(TTCut::muxOutputPath);

  if (!fInfo.exists())
    TTCut::muxOutputPath = TTCut::cutDirPath;
}

void TTCutSettingsMuxer::onCreateMuxStreams()
{
  TTCut::muxMode = 0;

  cbDeleteES->setEnabled(true);
  cbPause->setEnabled(true);
}

void TTCutSettingsMuxer::onCreateMuxScript()
{
  TTCut::muxMode = 1;

  cbDeleteES->setEnabled(false);
  cbPause->setEnabled(false);
}

void TTCutSettingsMuxer::onConfigureMuxer()
{
}

void TTCutSettingsMuxer::onOpenOutputPath()
{
  QString strDir = QFileDialog::getExistingDirectory(
      this,
      tr("Select directory for mplex result"),
      TTCut::muxOutputPath,
      (QFileDialog::DontResolveSymlinks | QFileDialog::ShowDirsOnly));

  if (!strDir.isEmpty())
  {
    TTCut::muxOutputPath = strDir;
    leOutputPath->setText(strDir);
    qApp->processEvents();
  }
}

void TTCutSettingsMuxer::onStateDeleteES(int state)
{
  if (state == Qt::Unchecked)
    TTCut::muxDeleteES = false;
  else
    TTCut::muxDeleteES = true;
}

void TTCutSettingsMuxer::onStatePause(int state)
{
  if (state == Qt::Unchecked)
    TTCut::muxPause = false;
  else
    TTCut::muxPause = true;
}

void TTCutSettingsMuxer::initOutputContainerList()
{
  // This function is for future use when we add a separate output container combo box
  // For now, the muxer program selection determines the output format
}

void TTCutSettingsMuxer::updateMuxerVisibility()
{
  int muxerIndex = cbMuxerProg->currentIndex();

  // MPEG-2 Target is only relevant when:
  // 1. Using mplex (muxerIndex == 0)
  // 2. Encoder codec is MPEG-2 (TTCut::encoderCodec == 0)
  bool enableMpeg2Target = (muxerIndex == 0 && TTCut::encoderCodec == 0);
  cbMuxTarget->setEnabled(enableMpeg2Target);

  // MKV chapter settings only visible when using mkvmerge (index 1)
  bool enableMkvChapters = (muxerIndex == 1);
  gbMkvChapters->setVisible(enableMkvChapters);
}

void TTCutSettingsMuxer::onMuxerProgChanged(int index)
{
  TTCut::outputContainer = index;

  // Save the muxer preference for the current codec
  switch (TTCut::encoderCodec) {
    case 0:  // MPEG-2
      TTCut::mpeg2Muxer = index;
      break;
    case 1:  // H.264
      TTCut::h264Muxer = index;
      break;
    case 2:  // H.265
      TTCut::h265Muxer = index;
      break;
  }

  updateMuxerVisibility();
}

void TTCutSettingsMuxer::onOutputContainerChanged(int index)
{
  TTCut::outputContainer = index;
}

void TTCutSettingsMuxer::onMkvChaptersChanged(int state)
{
  TTCut::mkvCreateChapters = (state == Qt::Checked);
  sbMkvChapterInterval->setEnabled(TTCut::mkvCreateChapters);
}

void TTCutSettingsMuxer::onEncoderCodecChanged(int codecIndex)
{
  // Get the preferred muxer for this codec
  int preferredMuxer;
  switch (codecIndex) {
    case 0:  // MPEG-2
      preferredMuxer = TTCut::mpeg2Muxer;
      break;
    case 1:  // H.264
      preferredMuxer = TTCut::h264Muxer;
      break;
    case 2:  // H.265
      preferredMuxer = TTCut::h265Muxer;
      break;
    default:
      preferredMuxer = 1;  // Default to mkvmerge
      break;
  }

  // Update the muxer selection to the preferred muxer
  cbMuxerProg->setCurrentIndex(preferredMuxer);
  TTCut::outputContainer = preferredMuxer;

  // Update visibility (MPEG-2 Target only for mplex + MPEG-2)
  updateMuxerVisibility();
}
