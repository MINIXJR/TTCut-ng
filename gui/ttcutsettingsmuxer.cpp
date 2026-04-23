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

#include <QFileDialog>
#include <QStandardItemModel>

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

  connect(rbCreateMuxScript, SIGNAL(clicked()),         SLOT(onCreateMuxScript()));
  connect(rbMuxStreams,      SIGNAL(clicked()),         SLOT(onCreateMuxStreams()));
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
  // Display order: MKV first (default/modern), MPG second.
  // userData holds the internal TTCut::outputContainer value
  // (0 = mplex, 1 = MKV) so the stored semantics stay stable.
  cbMuxerProg->insertItem(0, "MKV (libav)", 1);
  cbMuxerProg->insertItem(1, "MPG (mplex)", 0);

  // Initial selection: MKV. This is overwritten by setTabData() from
  // the stored preference as soon as the tab is populated.
  cbMuxerProg->setCurrentIndex(indexForMuxerValue(1));
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
  cbMuxerProg->setCurrentIndex(indexForMuxerValue(TTCut::outputContainer));
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
  // Read the live combo selection (container value, not display index)
  // so this method is self-contained and does not depend on
  // TTCut::outputContainer having been updated first.
  int current = muxerValueAt(cbMuxerProg->currentIndex());

  // MPEG-2 Target is only relevant when:
  // 1. Using mplex (current == 0)
  // 2. Encoder codec is MPEG-2 (encoderCodec == 0)
  bool enableMpeg2Target = (current == 0 && TTCut::encoderCodec == 0);
  cbMuxTarget->setEnabled(enableMpeg2Target);

  // MKV chapter settings only visible when using MKV (current == 1)
  bool enableMkvChapters = (current == 1);
  gbMkvChapters->setVisible(enableMkvChapters);
}

void TTCutSettingsMuxer::onMuxerProgChanged(int index)
{
  int value = muxerValueAt(index);
  TTCut::outputContainer = value;

  // Save the muxer preference for the current codec
  switch (TTCut::encoderCodec) {
    case 0:  TTCut::mpeg2Muxer = value; break;
    case 1:  TTCut::h264Muxer  = value; break;
    case 2:  TTCut::h265Muxer  = value; break;
  }

  updateMuxerVisibility();
  emit containerChanged(value);
}

int TTCutSettingsMuxer::muxerValueAt(int displayIndex) const
{
  return cbMuxerProg->itemData(displayIndex).toInt();
}

int TTCutSettingsMuxer::indexForMuxerValue(int outputContainerValue) const
{
  for (int i = 0; i < cbMuxerProg->count(); ++i) {
    if (cbMuxerProg->itemData(i).toInt() == outputContainerValue) return i;
  }
  return 0;  // fall back to first row (MKV)
}

void TTCutSettingsMuxer::onMkvChaptersChanged(int state)
{
  TTCut::mkvCreateChapters = (state == Qt::Checked);
  sbMkvChapterInterval->setEnabled(TTCut::mkvCreateChapters);
}

void TTCutSettingsMuxer::onEncoderCodecChanged(int codecIndex)
{
  // Disable the MPG (mplex) row for H.264/H.265; enable it for MPEG-2.
  bool mpgSupported = (codecIndex == 0);
  QStandardItemModel* model = qobject_cast<QStandardItemModel*>(cbMuxerProg->model());
  if (model) {
    int mpgRow = indexForMuxerValue(0);  // 0 = mplex
    QStandardItem* mpgItem = model->item(mpgRow);
    if (mpgItem) {
      Qt::ItemFlags f = mpgItem->flags();
      mpgItem->setFlags(mpgSupported ? (f |  Qt::ItemIsEnabled)
                                     : (f & ~Qt::ItemIsEnabled));
    }
  }

  // Fetch stored preference for this codec.
  int preferred;
  switch (codecIndex) {
    case 0:  preferred = TTCut::mpeg2Muxer; break;
    case 1:  preferred = TTCut::h264Muxer;  break;
    case 2:  preferred = TTCut::h265Muxer;  break;
    default: preferred = 1;  // MKV
  }
  if (!mpgSupported && preferred == 0) {
    preferred = 1;  // MPG invalid for this codec → fall back to MKV
  }

  cbMuxerProg->setCurrentIndex(indexForMuxerValue(preferred));
  // Defensive write: QComboBox suppresses currentIndexChanged when the new
  // index equals the current one. In that case onMuxerProgChanged does not
  // fire, so we must set outputContainer explicitly to keep it in sync.
  TTCut::outputContainer = preferred;

  // Update visibility (MPEG-2 Target only for mplex + MPEG-2)
  updateMuxerVisibility();
}
